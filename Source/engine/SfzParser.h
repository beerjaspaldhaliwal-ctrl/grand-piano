#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace gp
{

enum class Trigger { Attack, Release, Controller };

/** One <region> of an SFZ instrument, after header inheritance has been applied.
    Only the opcodes that matter for an acoustic piano are understood. */
struct SfzRegion
{
    juce::String samplePath;      // relative to the .sfz file (forward slashes)
    int sampleIndex = -1;         // index into the cache, resolved at load time

    int loKey = 0, hiKey = 127;
    int loVel = 1, hiVel = 127;
    int keyCenter = 60;
    float pitchKeytrack = 100.0f; // cents per key
    float tuneCents = 0.0f;
    int transpose = 0;

    float volumeDb = 0.0f;
    float ampVeltrack = 100.0f;   // percent
    float releaseTime = 0.0f;     // ampeg_release, seconds (0 = not set)
    float rtDecay = 0.0f;         // dB per second the key was held (release triggers)
    float pan = 0.0f;             // -100..100

    Trigger trigger = Trigger::Attack;
    int ccNumber = -1, ccLo = 0, ccHi = 127; // on_loccN / on_hiccN
    float loRand = 0.0f, hiRand = 1.0f;

    int group = 0, offBy = 0;

    bool matchesNote (int key, int vel) const noexcept
    {
        return key >= loKey && key <= hiKey && vel >= loVel && vel <= hiVel;
    }
};

struct SfzInstrument
{
    juce::File sfzFile;
    std::vector<SfzRegion> regions;
    juce::StringArray samplePaths; // unique, sorted
};

/** Parses an SFZ file. Returns false and fills `error` on failure. */
bool parseSfz (const juce::File& file, SfzInstrument& out, juce::String& error);

/** Converts "C#4", "eb3", "60" ... to a MIDI note number, or -1. */
int sfzNoteNumber (const juce::String& text);

} // namespace gp
