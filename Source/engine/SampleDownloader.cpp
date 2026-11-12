#include "SampleDownloader.h"

namespace gp
{

static juce::int64 parseOctal (const char* p, int len)
{
    juce::int64 v = 0;
    for (int i = 0; i < len && p[i] != 0; ++i)
    {
        if (p[i] == ' ') continue;
        if (p[i] < '0' || p[i] > '7') break;
        v = v * 8 + (p[i] - '0');
    }
    return v;
}

static bool isSafeRelativePath (const juce::String& path)
{
    if (path.isEmpty() || path.startsWithChar ('/') || path.startsWithChar ('\\') || path.containsChar (':'))
        return false;
    for (auto& part : juce::StringArray::fromTokens (path.replaceCharacter ('\\', '/'), "/", ""))
        if (part == "..")
            return false;
    return true;
}

bool SampleDownloader::extractTarGz (const juce::File& archive, const juce::File& destination,
                                     std::atomic<float>& progress, std::function<bool()> shouldAbort,
                                     juce::String& error)
{
    auto fileIn = std::make_unique<juce::FileInputStream> (archive);
    if (! fileIn->openedOk())
    {
        error = "Cannot open downloaded archive";
        return false;
    }

    auto* rawIn = fileIn.get();
    const auto compressedSize = (double) juce::jmax ((juce::int64) 1, archive.getSize());
    juce::GZIPDecompressorInputStream gz (fileIn.release(), true, juce::GZIPDecompressorInputStream::gzipFormat);

    char header[512];
    juce::String longName;
    juce::HeapBlock<char> chunk (1 << 20);

    for (;;)
    {
        if (shouldAbort()) { error = "Cancelled"; return false; }

        if (gz.read (header, 512) != 512)
            break;

        bool allZero = true;
        for (char c : header) if (c != 0) { allZero = false; break; }
        if (allZero)
            break;

        const auto size = parseOctal (header + 124, 12);
        const char type = header[156];

        juce::String name;
        if (longName.isNotEmpty())
        {
            name = longName;
            longName.clear();
        }
        else
        {
            name = juce::String::fromUTF8 (header, (int) strnlen (header, 100));
            if (std::memcmp (header + 257, "ustar", 5) == 0 && header[345] != 0)
                name = juce::String::fromUTF8 (header + 345, (int) strnlen (header + 345, 155)) + "/" + name;
        }

        const auto padded = (size + 511) & ~(juce::int64) 511;

        if (type == 'L') // GNU long name: data holds the name of the next entry
        {
            juce::MemoryBlock mb;
            gz.readIntoMemoryBlock (mb, static_cast<juce::pointer_sized_int> (padded));
            longName = juce::String::fromUTF8 ((const char*) mb.getData(), (int) strnlen ((const char*) mb.getData(), mb.getSize()));
            continue;
        }

        const bool isFile = (type == '0' || type == 0 || type == '7');
        if (! isSafeRelativePath (name) || ! (isFile || type == '5'))
        {
            gz.skipNextBytes (padded);
            continue;
        }

        auto target = destination.getChildFile (name);
        if (type == '5')
        {
            target.createDirectory();
            continue;
        }

        target.getParentDirectory().createDirectory();
        target.deleteFile();
        juce::FileOutputStream out (target);
        if (out.failedToOpen())
        {
            error = "Cannot write " + target.getFullPathName();
            return false;
        }

        auto remaining = size;
        while (remaining > 0)
        {
            auto n = (int) juce::jmin ((juce::int64) (1 << 20), remaining);
            auto got = gz.read (chunk.getData(), n);
            if (got <= 0) { error = "Archive is truncated"; return false; }
            out.write (chunk.getData(), (size_t) got);
            remaining -= got;
        }
        gz.skipNextBytes (padded - size);

        out.flush();
        if (out.getStatus().failed()) { error = "Disk full?"; return false; }

        progress = (float) juce::jlimit (0.0, 1.0, (double) rawIn->getPosition() / compressedSize);
    }

    return true;
}

bool SampleDownloader::run (const juce::File& destination, std::atomic<float>& progress,
                            std::function<void (const juce::String&)> setStage,
                            std::function<bool()> shouldAbort, juce::String& error)
{
    destination.createDirectory();
    auto archive = destination.getChildFile ("SalamanderGrandPiano.tar.gz.part");

    setStage ("Downloading Salamander Grand Piano (740 MB)...");
    progress = 0.0f;

    int statusCode = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (30000)
                       .withNumRedirectsToFollow (5)
                       .withStatusCode (&statusCode);

    auto stream = juce::URL (url).createInputStream (options);
    if (stream == nullptr || (statusCode != 0 && statusCode >= 400))
    {
        error = "Download failed (no connection or server error " + juce::String (statusCode) + ")";
        return false;
    }

    const auto total = stream->getTotalLength();
    {
        archive.deleteFile();
        juce::FileOutputStream out (archive);
        if (out.failedToOpen())
        {
            error = "Cannot write to " + destination.getFullPathName();
            return false;
        }

        juce::HeapBlock<char> buffer (1 << 18);
        juce::int64 received = 0;
        for (;;)
        {
            if (shouldAbort()) { error = "Cancelled"; out.flush(); archive.deleteFile(); return false; }
            auto n = stream->read (buffer.getData(), 1 << 18);
            if (n <= 0)
                break;
            out.write (buffer.getData(), (size_t) n);
            received += n;
            if (total > 0)
                progress = (float) ((double) received / (double) total);
        }
        out.flush();

        if (out.getStatus().failed() || (total > 0 && received != total))
        {
            error = "Download was interrupted - please try again";
            archive.deleteFile();
            return false;
        }
    }

    setStage ("Unpacking piano samples...");
    progress = 0.0f;
    const bool ok = extractTarGz (archive, destination, progress, shouldAbort, error);
    archive.deleteFile();
    return ok;
}

} // namespace gp
