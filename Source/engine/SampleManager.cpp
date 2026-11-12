#include "SampleManager.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_dsp/juce_dsp.h>
#include "SampleDownloader.h"
#include <map>

namespace gp
{

static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "GrandPiano";
    opts.filenameSuffix = ".settings";
   #if JUCE_LINUX || JUCE_BSD
    opts.folderName = ".config/GrandPiano";
   #else
    opts.folderName = "GrandPiano";
   #endif
    opts.osxLibrarySubFolder = "Application Support";
    return opts;
}

//==============================================================================
void Library::measureTuning()
{
    measuredCents.assign (sfz.regions.size(), 0.0f);

    constexpr int order = 15, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> work ((size_t) size * 2);
    std::vector<float> window ((size_t) size);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (size - 1));

    const double rate = cache.getSampleRate();
    std::map<int, float> perSample; // the same sample can be used by several regions

    for (size_t ri = 0; ri < sfz.regions.size(); ++ri)
    {
        auto& r = sfz.regions[ri];
        // bass partials are too inharmonic to measure reliably and need no correction
        if (r.trigger != Trigger::Attack || r.pitchKeytrack == 0.0f || r.keyCenter < 48 || r.sampleIndex < 0)
            continue;

        if (auto it = perSample.find (r.sampleIndex); it != perSample.end())
        {
            measuredCents[ri] = it->second;
            continue;
        }

        auto& smp = cache.get (r.sampleIndex);
        const int start = (int) (rate * 0.06);
        if ((int) smp.frames < start + size)
            continue;

        for (int i = 0; i < size; ++i)
        {
            auto* f = smp.data + (size_t) (start + i) * 2;
            work[(size_t) i] = (float) (f[0] + f[1]) * window[(size_t) i];
        }
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data());

        const double f0 = 440.0 * std::pow (2.0, (r.keyCenter - 69) / 12.0);
        const double binHz = rate / size;
        int lo = (int) (f0 * 0.955 / binHz), hi = (int) (f0 * 1.045 / binHz) + 1;
        lo = juce::jmax (1, lo); hi = juce::jmin (size / 2 - 2, hi);
        int best = lo;
        for (int b = lo; b <= hi; ++b)
            if (work[(size_t) b] > work[(size_t) best]) best = b;

        // parabolic interpolation on log magnitude
        const double a = std::log (work[(size_t) best - 1] + 1e-9f), b0 = std::log (work[(size_t) best] + 1e-9f),
                     c = std::log (work[(size_t) best + 1] + 1e-9f);
        const double denom = a - 2.0 * b0 + c;
        const double offset = std::abs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
        const double freq = (best + offset) * binHz;

        float cents = (float) (1200.0 * std::log2 (freq / f0));
        cents -= r.tuneCents; // the sfz may already correct part of it
        cents = juce::jlimit (-120.0f, 120.0f, cents);
        // fade the correction in over the tenor range
        cents *= juce::jlimit (0.0f, 1.0f, (r.keyCenter - 48) / 12.0f);

        measuredCents[ri] = cents;
        perSample[r.sampleIndex] = cents;
    }
}

//==============================================================================
/** Keeps the audio that playing voices are about to read resident in memory. */
struct SampleManager::Prefetcher : public juce::Thread
{
    explicit Prefetcher (SampleManager& o) : juce::Thread ("GrandPiano prefetch"), owner (o) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            owner.prefetchActiveVoices();
            owner.collectGarbage();
            wait (10);
        }
    }

    SampleManager& owner;
};

//==============================================================================
SampleManager::SampleManager (VoiceMonitor& m)
    : juce::Thread ("GrandPiano loader"), monitor (m)
{
    downloader = std::make_unique<SampleDownloader>();
    prefetcher = std::make_unique<Prefetcher> (*this);
    prefetcher->startThread (juce::Thread::Priority::high);
    startThread (juce::Thread::Priority::normal);
}

SampleManager::~SampleManager()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (30000);
    prefetcher->stopThread (2000);
    requested = nullptr;
    libraries.clear();
}

juce::File SampleManager::getDataRoot()
{
    // A local (non-synced) location: Documents is often synced to OneDrive / iCloud,
    // which must not upload gigabytes of samples and cache.
   #if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Grand Piano");
   #elif JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("Grand Piano");
   #else
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share/Grand Piano");
   #endif
}

void SampleManager::begin (const juce::File& preferredSfz)
{
    std::lock_guard<std::mutex> l (jobLock);
    pendingJob = Job::Begin;
    pendingFile = preferredSfz;
    wake.signal();
}

void SampleManager::loadAsync (const juce::File& f)
{
    std::lock_guard<std::mutex> l (jobLock);
    pendingJob = Job::Load;
    pendingFile = f;
    wake.signal();
}

void SampleManager::downloadAsync()
{
    std::lock_guard<std::mutex> l (jobLock);
    pendingJob = Job::Download;
    wake.signal();
}

void SampleManager::setStatus (State s, const juce::String& text)
{
    {
        std::lock_guard<std::mutex> l (statusLock);
        statusText = text;
    }
    state = s;
}

juce::String SampleManager::getStatusText() const
{
    std::lock_guard<std::mutex> l (statusLock);
    return statusText;
}

juce::File SampleManager::getCurrentSfz() const
{
    std::lock_guard<std::mutex> l (statusLock);
    return currentSfz;
}

juce::Array<juce::File> SampleManager::getVariants() const
{
    auto cur = getCurrentSfz();
    juce::Array<juce::File> result;
    if (cur.existsAsFile())
    {
        result = cur.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*.sfz");
        std::sort (result.begin(), result.end());
    }
    return result;
}

juce::StringArray SampleManager::getVariantNames() const
{
    juce::StringArray names;
    for (auto& f : getVariants())
    {
        auto n = f.getFileNameWithoutExtension();
        if (n.containsIgnoreCase ("retuned"))         n = "Mapping: Fiedler retune";
        else if (n.containsIgnoreCase ("salamander")) n = "Mapping: Original";
        names.add (n);
    }
    return names;
}

//==============================================================================
juce::File SampleManager::pickSfzInFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return {};

    auto files = folder.findChildFiles (juce::File::findFiles, true, "*.sfz");
    if (files.isEmpty())
        return {};

    // Prefer the Salamander grand in equal temperament, otherwise the first file found.
    std::sort (files.begin(), files.end());
    for (auto& f : files)
        if (f.getFileName().containsIgnoreCase ("salamander") && ! f.getFileName().containsIgnoreCase ("retuned"))
            return f;
    return files.getFirst();
}

juce::File SampleManager::findDefaultSfz (const juce::File& preferred) const
{
    if (preferred.existsAsFile())
        return preferred;

    auto opts = settingsOptions();
    juce::PropertiesFile props (opts);
    juce::File last (props.getValue ("lastSfz"));
    if (last.existsAsFile())
        return last;

    // Samples shipped inside the plugin bundle: <bundle>/Contents/Resources/...
    auto binary = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir = binary.getParentDirectory(); dir.exists() && ! dir.isRoot(); dir = dir.getParentDirectory())
    {
        auto res = dir.getChildFile ("Resources");
        if (res.isDirectory())
            if (auto f = pickSfzInFolder (res); f.existsAsFile())
                return f;
        if (dir.getFileName().endsWithIgnoreCase (".vst3") || dir.getFileName().endsWithIgnoreCase (".app"))
            break;
    }

    // Downloaded samples, or ones the user copied to Documents/Grand Piano
    if (auto f = pickSfzInFolder (getDataRoot()); f.existsAsFile())
        return f;
    if (auto f = pickSfzInFolder (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                      .getChildFile ("Grand Piano")); f.existsAsFile())
        return f;

    return {};
}

//==============================================================================
void SampleManager::run()
{
    while (! threadShouldExit())
    {
        wake.wait (500);

        Job job;
        juce::File file;
        {
            std::lock_guard<std::mutex> l (jobLock);
            job = pendingJob;
            file = pendingFile;
            pendingJob = Job::None;
        }

        if (job == Job::None)
            continue;

        if (job == Job::Download)
        {
            setStatus (State::Downloading, "Connecting...");
            juce::String error;
            auto dest = getDownloadFolder();
            bool ok = downloader->run (dest, progress,
                                       [this] (const juce::String& s) { setStatus (State::Downloading, s); },
                                       [this] { return threadShouldExit(); }, error);
            if (! ok)
            {
                setStatus (State::Error, error);
                continue;
            }
            file = pickSfzInFolder (dest);
            job = Job::Load;
        }

        if (job == Job::Begin)
        {
            setStatus (State::Searching, "Looking for piano samples...");
            file = findDefaultSfz (file);
            if (! file.existsAsFile())
            {
                setStatus (State::NotFound, "Piano samples not installed - click \"Get Samples\". Using the built-in modelled piano.");
                continue;
            }
        }
        else if (file.isDirectory())
        {
            file = pickSfzInFolder (file);
        }

        loadNow (file);
    }
}

void SampleManager::loadNow (const juce::File& sfzFile)
{
    if (! sfzFile.existsAsFile())
    {
        setStatus (State::Error, "No .sfz instrument found there");
        return;
    }

    setStatus (State::Loading, "Reading " + sfzFile.getFileName());

    auto lib = std::make_unique<Library>();
    juce::String error;

    if (! parseSfz (sfzFile, lib->sfz, error))
    {
        setStatus (State::Error, error);
        return;
    }

    progress = 0.0f;
    setStatus (State::Building, "Preparing samples for streaming (first time only)...");

    if (! lib->cache.open (sfzFile.getParentDirectory(), lib->sfz.samplePaths,
                           getDataRoot().getChildFile ("Cache"), progress,
                           [this] { return threadShouldExit(); }, error))
    {
        setStatus (State::Error, error);
        return;
    }

    setStatus (State::Loading, "Warming up samples...");
    lib->buildLookup();
    lib->measureTuning();
    lib->name = sfzFile.getFileNameWithoutExtension();

    // Make every attack and the short release/pedal samples resident right away.
    const auto rate = (int64_t) lib->cache.getSampleRate();
    for (auto& r : lib->sfz.regions)
    {
        if (threadShouldExit()) return;
        lib->cache.prefetch (r.sampleIndex, 0, r.trigger == Trigger::Attack ? rate / 2 : rate * 4);
    }

    auto* raw = lib.get();
    {
        std::lock_guard<std::mutex> l (libsLock);
        libraries.push_back (std::move (lib));
    }
    requested.store (raw, std::memory_order_release);

    {
        std::lock_guard<std::mutex> l (statusLock);
        currentSfz = sfzFile;
    }

    auto opts = settingsOptions();
    juce::PropertiesFile props (opts);
    props.setValue ("lastSfz", sfzFile.getFullPathName());
    props.saveIfNeeded();

    auto mb = raw->cache.getTotalBytes() / (1024 * 1024);
    setStatus (State::Ready, raw->name + "  -  " + juce::String ((int) raw->sfz.regions.size()) + " zones, "
                                 + juce::String (mb) + " MB streamed from disk");
}

//==============================================================================
void SampleManager::prefetchActiveVoices()
{
    std::lock_guard<std::mutex> l (libsLock);
    auto* lib = inUseByAudio.load (std::memory_order_acquire);
    if (lib == nullptr)
        return;

    const auto ahead = (int64_t) (lib->cache.getSampleRate() * 1.5);
    for (auto& slot : monitor.slots)
    {
        auto s = slot.sample.load (std::memory_order_relaxed);
        if (s >= 0)
            lib->cache.prefetch (s, slot.frame.load (std::memory_order_relaxed), ahead);
    }
}

void SampleManager::collectGarbage()
{
    std::lock_guard<std::mutex> l (libsLock);
    auto* req = requested.load();
    auto* used = inUseByAudio.load();

    // Only free old libraries once the audio thread has switched to the newest one,
    // otherwise it might be about to pick up a library we are deleting.
    if (req != used && req != nullptr)
        return;

    libraries.erase (std::remove_if (libraries.begin(), libraries.end(),
                                     [&] (auto& p) { return p.get() != req && p.get() != used; }),
                     libraries.end());
}

} // namespace gp
