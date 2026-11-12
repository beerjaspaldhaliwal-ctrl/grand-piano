#pragma once

#include "SfzParser.h"
#include "SampleCache.h"
#include <array>

namespace gp
{

/** A fully loaded, immutable instrument: the parsed SFZ plus its mapped samples. */
struct Library
{
    SfzInstrument sfz;
    SampleCache cache;
    juce::String name;

    std::array<std::vector<int>, 128> attackRegions;   // region indices per key
    std::array<std::vector<int>, 128> releaseRegions;
    std::vector<int> controllerRegions;
    std::vector<float> measuredCents;   // detected tuning offset of each attack region

    /** Detects how far each recorded note deviates from equal temperament so the
        engine can blend between the recorded (stretched) tuning and exact tuning. */
    void measureTuning();

    void buildLookup()
    {
        for (auto& v : attackRegions) v.clear();
        for (auto& v : releaseRegions) v.clear();
        controllerRegions.clear();

        for (int i = 0; i < (int) sfz.regions.size(); ++i)
        {
            auto& r = sfz.regions[(size_t) i];
            if (r.trigger == Trigger::Controller)
            {
                controllerRegions.push_back (i);
                continue;
            }
            for (int k = juce::jmax (0, r.loKey); k <= juce::jmin (127, r.hiKey); ++k)
                (r.trigger == Trigger::Release ? releaseRegions : attackRegions)[(size_t) k].push_back (i);
        }
    }
};

/** Per-voice playback positions, published by the audio thread so the prefetch
    thread can keep the upcoming audio resident in memory. */
struct VoiceMonitor
{
    static constexpr int maxSlots = 320;
    struct Slot
    {
        std::atomic<int> sample { -1 };
        std::atomic<int> frame { 0 };
    };
    std::array<Slot, maxSlots> slots;
};

} // namespace gp
