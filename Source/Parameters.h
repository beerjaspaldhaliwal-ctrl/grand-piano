#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace gp
{

namespace ParamID
{
    inline constexpr const char* volume        = "volume";
    inline constexpr const char* transpose     = "transpose";
    inline constexpr const char* tune          = "tune";
    inline constexpr const char* stretch       = "stretch";
    inline constexpr const char* touch         = "touch";
    inline constexpr const char* dynamics      = "dynamics";
    inline constexpr const char* hardness      = "hardness";
    inline constexpr const char* release       = "release";
    inline constexpr const char* releaseNoise  = "releaseNoise";
    inline constexpr const char* stringRelease = "stringRelease";
    inline constexpr const char* pedalNoise    = "pedalNoise";
    inline constexpr const char* resonance     = "resonance";
    inline constexpr const char* bass          = "bass";
    inline constexpr const char* mid           = "mid";
    inline constexpr const char* treble        = "treble";
    inline constexpr const char* compression   = "compression";
    inline constexpr const char* width         = "width";
    inline constexpr const char* audience      = "audience";
    inline constexpr const char* reverbMix     = "reverbMix";
    inline constexpr const char* reverbSize    = "reverbSize";
    inline constexpr const char* reverbTone    = "reverbTone";
    inline constexpr const char* polyphony     = "polyphony";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // anything not listed uses the default
};

const std::vector<Preset>& getFactoryPresets();

} // namespace gp
