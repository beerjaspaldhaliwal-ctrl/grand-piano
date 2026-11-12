#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <vector>

namespace gp
{

/** A decoded sample living inside the memory-mapped cache file.
    Audio is stored as normalised, dithered 16-bit interleaved stereo. */
struct CachedSample
{
    const int16_t* data = nullptr;
    uint32_t frames = 0;
    float gain = 1.0f;       // int16 value * gain = original amplitude
    float peak = 0.0f;       // original peak amplitude (linear)
};

/**
    Decodes every sample of an instrument once into a single raw cache file and then
    memory-maps it. The operating system pages audio in on demand (with help from a
    prefetch thread), which works like the disk streaming of commercial samplers:
    RAM use stays low, instances share the same pages, and re-opening is instant.
*/
class SampleCache
{
public:
    ~SampleCache();

    /** Opens the cache for the given sample files, building it first if needed.
        `progress` receives 0..1 while building. Returns false on failure. */
    bool open (const juce::File& baseFolder,
               const juce::StringArray& relativePaths,
               const juce::File& cacheFolder,
               std::atomic<float>& progress,
               std::function<bool()> shouldAbort,
               juce::String& error);

    int size() const noexcept                         { return (int) samples.size(); }
    const CachedSample& get (int index) const noexcept { return samples[(size_t) index]; }
    double getSampleRate() const noexcept             { return sampleRate; }

    /** Touches the pages covering [startFrame, startFrame + numFrames) so they are
        resident before the audio thread needs them. Safe from any thread. */
    void prefetch (int index, int64_t startFrame, int64_t numFrames) const noexcept;

    int64_t getTotalBytes() const noexcept { return mapped != nullptr ? (int64_t) mapped->getSize() : 0; }

private:
    bool build (const juce::File& baseFolder, const juce::StringArray& paths, const juce::File& target,
                uint64_t signature, std::atomic<float>& progress, std::function<bool()> shouldAbort,
                juce::String& error);
    bool map (const juce::File& file, int expectedCount, uint64_t signature, juce::String& error);

    std::unique_ptr<juce::MemoryMappedFile> mapped;
    std::vector<CachedSample> samples;
    double sampleRate = 48000.0;
};

} // namespace gp
