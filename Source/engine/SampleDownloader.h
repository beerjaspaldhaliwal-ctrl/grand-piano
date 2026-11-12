#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace gp
{

/** Downloads the free Salamander Grand Piano (Yamaha C5, CC-BY 3.0, Alexander Holm)
    and unpacks it. Runs synchronously – call from a background thread. */
class SampleDownloader
{
public:
    static constexpr const char* url =
        "https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPiano-SFZ+FLAC-V3+20200602.tar.gz";

    bool run (const juce::File& destination,
              std::atomic<float>& progress,
              std::function<void (const juce::String&)> setStage,
              std::function<bool()> shouldAbort,
              juce::String& error);

    /** Extracts a .tar.gz archive (exposed for testing). */
    static bool extractTarGz (const juce::File& archive, const juce::File& destination,
                              std::atomic<float>& progress, std::function<bool()> shouldAbort,
                              juce::String& error);
};

} // namespace gp
