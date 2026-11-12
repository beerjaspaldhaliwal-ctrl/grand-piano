#include "SampleCache.h"
#include <mutex>
#include <random>

namespace gp
{

namespace
{
    constexpr char magic[8] = { 'G', 'P', 'C', 'A', 'C', 'H', 'E', '3' };

    struct FileHeader
    {
        char magic[8];
        uint32_t count;
        uint32_t sampleRate;
        uint64_t signature;
        uint64_t dataOffset;
        uint64_t totalFrames;
    };

    struct FileEntry
    {
        uint64_t frameOffset;
        uint32_t frames;
        float gain;
        float peak;
        uint32_t reserved;
    };

    static_assert (sizeof (FileHeader) == 40, "header layout");
    static_assert (sizeof (FileEntry) == 24, "entry layout");

    uint64_t fnv1a (uint64_t h, const void* data, size_t n)
    {
        auto* p = static_cast<const uint8_t*> (data);
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }

    uint64_t computeSignature (const juce::File& base, const juce::StringArray& paths)
    {
        uint64_t h = 1469598103934665603ull;
        const uint32_t version = 3;
        h = fnv1a (h, &version, sizeof (version));
        for (auto& p : paths)
        {
            auto f = base.getChildFile (p);
            auto name = p.toStdString();
            int64_t sz = f.getSize();
            h = fnv1a (h, name.data(), name.size());
            h = fnv1a (h, &sz, sizeof (sz));
        }
        return h;
    }
}

SampleCache::~SampleCache() = default;

bool SampleCache::open (const juce::File& baseFolder, const juce::StringArray& paths,
                        const juce::File& cacheFolder, std::atomic<float>& progress,
                        std::function<bool()> shouldAbort, juce::String& error)
{
    const auto signature = computeSignature (baseFolder, paths);
    cacheFolder.createDirectory();

    auto cacheFile = cacheFolder.getChildFile ("samples-" + juce::String::toHexString ((juce::int64) signature) + ".gpcache");

    if (cacheFile.existsAsFile() && map (cacheFile, paths.size(), signature, error))
        return true;

    // remove stale caches from older sample sets so disk use does not grow forever
    for (auto& old : cacheFolder.findChildFiles (juce::File::findFiles, false, "*.gpcache"))
        if (old != cacheFile)
            old.deleteFile();

    if (! build (baseFolder, paths, cacheFile, signature, progress, shouldAbort, error))
        return false;

    return map (cacheFile, paths.size(), signature, error);
}

bool SampleCache::map (const juce::File& file, int expectedCount, uint64_t signature, juce::String& error)
{
    mapped = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readOnly, false);

    if (mapped->getData() == nullptr || mapped->getSize() < sizeof (FileHeader))
    {
        error = "Could not map sample cache";
        mapped.reset();
        return false;
    }

    auto* base = static_cast<const char*> (mapped->getData());
    FileHeader header;
    std::memcpy (&header, base, sizeof (header));

    if (std::memcmp (header.magic, magic, 8) != 0 || header.signature != signature
        || (int) header.count != expectedCount
        || header.dataOffset + header.totalFrames * 4 > mapped->getSize())
    {
        error = "Sample cache is out of date";
        mapped.reset();
        return false;
    }

    sampleRate = header.sampleRate;
    samples.resize (header.count);

    auto* entries = reinterpret_cast<const FileEntry*> (base + sizeof (FileHeader));
    auto* audio = reinterpret_cast<const int16_t*> (base + header.dataOffset);

    for (uint32_t i = 0; i < header.count; ++i)
    {
        auto& e = entries[i];
        auto& s = samples[i];
        s.data = audio + e.frameOffset * 2;
        s.frames = e.frames;
        s.gain = e.gain;
        s.peak = e.peak;
    }
    return true;
}

bool SampleCache::build (const juce::File& baseFolder, const juce::StringArray& paths, const juce::File& target,
                         uint64_t signature, std::atomic<float>& progress, std::function<bool()> shouldAbort,
                         juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const int count = paths.size();
    std::vector<FileEntry> entries ((size_t) count);
    uint32_t rate = 0;
    uint64_t totalFrames = 0;

    // Pass 1: read lengths so each sample's position in the file is known up front.
    for (int i = 0; i < count; ++i)
    {
        auto f = baseFolder.getChildFile (paths[i]);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr)
        {
            error = "Cannot read sample: " + paths[i];
            return false;
        }
        if (rate == 0)
            rate = (uint32_t) reader->sampleRate;
        // samples at another rate get resampled on playback via per-sample ratio – keep simple:
        // all samples of an instrument are expected to share one rate.
        entries[(size_t) i].frameOffset = totalFrames;
        entries[(size_t) i].frames = (uint32_t) juce::jmax ((juce::int64) 0, reader->lengthInSamples);
        totalFrames += entries[(size_t) i].frames + 8; // padding frames for interpolation
    }

    const uint64_t headerBytes = sizeof (FileHeader) + sizeof (FileEntry) * (uint64_t) count;
    const uint64_t dataOffset = (headerBytes + 4095) & ~uint64_t (4095);

    auto temp = target.getSiblingFile (target.getFileName() + ".tmp");
    temp.deleteFile();

    std::mutex writeLock;
    auto outStream = std::make_unique<juce::FileOutputStream> (temp);
    auto& out = *outStream;
    if (out.failedToOpen())
    {
        error = "Cannot write sample cache in " + target.getParentDirectory().getFullPathName();
        return false;
    }

    // reserve the full file size up front
    out.setPosition ((juce::int64) (dataOffset + totalFrames * 4 - 1));
    out.writeByte (0);

    std::atomic<int> done { 0 };
    std::atomic<bool> failed { false };
    juce::String failMessage;

    const int numThreads = juce::jlimit (1, 8, juce::SystemStats::getNumCpus() - 1);
    juce::ThreadPool pool (juce::ThreadPoolOptions{}.withNumberOfThreads (numThreads));

    for (int i = 0; i < count; ++i)
    {
        pool.addJob ([&, i]
        {
            if (failed || shouldAbort())
                return;

            juce::AudioFormatManager localFormats;
            localFormats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (localFormats.createReaderFor (baseFolder.getChildFile (paths[i])));
            auto& e = entries[(size_t) i];

            if (reader == nullptr)
            {
                failed = true;
                return;
            }

            const int frames = (int) e.frames;
            juce::AudioBuffer<float> buffer (2, juce::jmax (1, frames));
            buffer.clear();
            reader->read (&buffer, 0, frames, 0, true, true); // mono files fill both channels

            float peak = juce::jmax (buffer.getMagnitude (0, 0, frames), buffer.getMagnitude (1, 0, frames));
            if (peak <= 0.0f)
                peak = 1.0e-9f;

            const float scale = 32766.0f / peak;
            e.gain = 1.0f / scale;
            e.peak = peak;

            std::vector<int16_t> pcm ((size_t) (frames + 8) * 2, 0);
            std::minstd_rand rng ((unsigned) i * 7919u + 17u);
            std::uniform_real_distribution<float> dist (-0.5f, 0.5f);

            auto* l = buffer.getReadPointer (0);
            auto* r = buffer.getReadPointer (1);
            for (int n = 0; n < frames; ++n)
            {
                // TPDF dither keeps quiet tails clean
                float vl = l[n] * scale + dist (rng) + dist (rng);
                float vr = r[n] * scale + dist (rng) + dist (rng);
                pcm[(size_t) n * 2]     = (int16_t) juce::jlimit (-32767, 32767, juce::roundToInt (vl));
                pcm[(size_t) n * 2 + 1] = (int16_t) juce::jlimit (-32767, 32767, juce::roundToInt (vr));
            }

            {
                std::lock_guard<std::mutex> lock (writeLock);
                out.setPosition ((juce::int64) (dataOffset + e.frameOffset * 4));
                out.write (pcm.data(), pcm.size() * sizeof (int16_t));
            }

            progress = (float) ++done / (float) count;
        });
    }

    while (pool.getNumJobs() > 0)
        juce::Thread::sleep (20);

    if (failed || shouldAbort())
    {
        outStream.reset();
        temp.deleteFile();
        error = shouldAbort() ? "Cancelled" : "Failed to decode a sample";
        return false;
    }

    FileHeader header {};
    std::memcpy (header.magic, magic, 8);
    header.count = (uint32_t) count;
    header.sampleRate = rate;
    header.signature = signature;
    header.dataOffset = dataOffset;
    header.totalFrames = totalFrames;

    out.setPosition (0);
    out.write (&header, sizeof (header));
    out.write (entries.data(), entries.size() * sizeof (FileEntry));
    out.flush();

    const bool writeFailed = out.getStatus().failed();
    outStream.reset(); // closes the file

    if (writeFailed)
    {
        error = "Writing the sample cache failed (disk full?)";
        temp.deleteFile();
        return false;
    }

    target.deleteFile();
    if (! temp.moveFileTo (target))
    {
        error = "Could not finalise the sample cache";
        temp.deleteFile();
        return false;
    }
    return true;
}

void SampleCache::prefetch (int index, int64_t startFrame, int64_t numFrames) const noexcept
{
    if (index < 0 || index >= (int) samples.size())
        return;

    auto& s = samples[(size_t) index];
    auto end = juce::jmin ((int64_t) s.frames, startFrame + numFrames);
    startFrame = juce::jmax ((int64_t) 0, startFrame);

    auto* bytes = reinterpret_cast<const volatile uint8_t*> (s.data);
    uint8_t sink = 0;
    for (int64_t b = startFrame * 4; b < end * 4; b += 4096)
        sink ^= bytes[b];
    if (end > startFrame)
        sink ^= bytes[end * 4 - 1];

    static std::atomic<uint8_t> globalSink { 0 };
    globalSink.fetch_xor (sink, std::memory_order_relaxed);
}

} // namespace gp
