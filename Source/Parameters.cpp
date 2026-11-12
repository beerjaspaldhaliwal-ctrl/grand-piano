#include "Parameters.h"

namespace gp
{

using APF = juce::AudioParameterFloat;
using Range = juce::NormalisableRange<float>;

static std::unique_ptr<APF> makeFloat (const char* id, const char* name, Range range, float def,
                                       const char* suffix = "", int decimals = 1)
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes()
            .withLabel (suffix)
            .withStringFromValueFunction ([decimals, suffix] (float v, int) { return juce::String (v, decimals) + suffix; }));
}

static std::unique_ptr<APF> makePercent (const char* id, const char* name, float def, float maxValue = 1.0f, bool bipolar = false)
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name,
        Range (bipolar ? -maxValue : 0.0f, maxValue), def,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([bipolar] (float v, int)
            {
                auto pct = juce::roundToInt (v * 100.0f);
                return (bipolar && pct > 0 ? "+" : "") + juce::String (pct) + "%";
            })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() / 100.0f; }));
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeFloat (ParamID::volume, "Volume", Range (-36.0f, 6.0f, 0.1f, 1.8f), -3.0f, " dB"));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ParamID::transpose, 1 }, "Transpose", -12, 12, 0,
        juce::AudioParameterIntAttributes().withLabel ("st")));
    layout.add (makeFloat (ParamID::tune, "Fine Tune", Range (-100.0f, 100.0f, 0.1f), 0.0f, " ct", 1));

    layout.add (makePercent (ParamID::stretch, "Stretch Tuning", 0.5f));
    layout.add (makePercent (ParamID::touch, "Touch", 0.0f, 1.0f, true));
    layout.add (makePercent (ParamID::dynamics, "Dynamics", 1.0f, 1.5f));
    layout.add (makePercent (ParamID::hardness, "Hammer", 0.0f, 1.0f, true));
    layout.add (makeFloat (ParamID::release, "Release", Range (0.2f, 4.0f, 0.01f, 0.5f), 1.0f, "x", 2));
    layout.add (makePercent (ParamID::releaseNoise, "Key Noise", 0.5f));
    layout.add (makePercent (ParamID::stringRelease, "Damper Res.", 0.5f));
    layout.add (makePercent (ParamID::pedalNoise, "Pedal Noise", 0.4f));
    layout.add (makePercent (ParamID::resonance, "Sympathetic", 0.45f));

    layout.add (makeFloat (ParamID::bass, "Bass", Range (-12.0f, 12.0f, 0.1f), 0.0f, " dB"));
    layout.add (makeFloat (ParamID::mid, "Mid", Range (-12.0f, 12.0f, 0.1f), 0.0f, " dB"));
    layout.add (makeFloat (ParamID::treble, "Treble", Range (-12.0f, 12.0f, 0.1f), 0.0f, " dB"));
    layout.add (makePercent (ParamID::compression, "Compression", 0.0f));
    layout.add (makePercent (ParamID::width, "Width", 1.0f, 1.5f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamID::audience, 1 }, "Audience View", false));

    layout.add (makePercent (ParamID::reverbMix, "Reverb", 0.20f));
    layout.add (makePercent (ParamID::reverbSize, "Room Size", 0.65f));
    layout.add (makePercent (ParamID::reverbTone, "Room Tone", 0.5f));

    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ParamID::polyphony, 1 }, "Polyphony", 32, 256, 128));

    return layout;
}

const std::vector<Preset>& getFactoryPresets()
{
    using namespace ParamID;
    static const std::vector<Preset> presets
    {
        { "Concert Grand",   { { reverbMix, 0.20f }, { reverbSize, 0.65f }, { reverbTone, 0.5f } } },

        { "Studio Pop",      { { hardness, 0.35f }, { touch, 0.15f }, { treble, 3.5f }, { mid, -1.5f }, { bass, 1.0f },
                               { compression, 0.45f }, { width, 1.2f }, { reverbMix, 0.14f }, { reverbSize, 0.35f },
                               { reverbTone, 0.65f }, { releaseNoise, 0.3f }, { pedalNoise, 0.2f }, { volume, -4.0f } } },

        { "Warm Ballad",     { { hardness, -0.35f }, { treble, -3.0f }, { bass, 1.5f }, { mid, 0.5f }, { dynamics, 1.15f },
                               { reverbMix, 0.28f }, { reverbSize, 0.75f }, { reverbTone, 0.35f }, { resonance, 0.6f },
                               { release, 1.3f } } },

        { "Bright Rock",     { { hardness, 0.7f }, { touch, 0.3f }, { dynamics, 0.7f }, { treble, 5.0f }, { mid, 2.0f },
                               { bass, -1.0f }, { compression, 0.7f }, { width, 1.25f }, { reverbMix, 0.10f },
                               { reverbSize, 0.3f }, { releaseNoise, 0.25f }, { pedalNoise, 0.15f }, { volume, -6.0f } } },

        { "Intimate Close",  { { width, 0.85f }, { reverbMix, 0.06f }, { reverbSize, 0.2f }, { releaseNoise, 0.75f },
                               { pedalNoise, 0.6f }, { stringRelease, 0.6f }, { dynamics, 1.2f } } },

        { "Cinematic Hall",  { { hardness, -0.2f }, { treble, -1.5f }, { bass, 1.0f }, { reverbMix, 0.42f },
                               { reverbSize, 0.95f }, { reverbTone, 0.4f }, { resonance, 0.7f }, { release, 1.5f },
                               { width, 1.3f } } },

        { "Jazz Club",       { { hardness, 0.1f }, { mid, 1.5f }, { treble, -1.0f }, { compression, 0.25f },
                               { reverbMix, 0.16f }, { reverbSize, 0.4f }, { reverbTone, 0.4f }, { width, 0.95f } } },

        { "Soft & Dark",     { { hardness, -0.8f }, { touch, -0.2f }, { treble, -8.0f }, { mid, -1.0f }, { bass, 2.0f },
                               { reverbMix, 0.25f }, { reverbSize, 0.6f }, { reverbTone, 0.25f }, { releaseNoise, 0.7f },
                               { dynamics, 0.85f } } },

        { "Audience Seat",   { { audience, 1.0f }, { width, 0.8f }, { reverbMix, 0.32f }, { reverbSize, 0.8f },
                               { treble, -2.0f }, { releaseNoise, 0.25f }, { pedalNoise, 0.2f } } },

        { "Pure & Dry",      { { reverbMix, 0.0f }, { resonance, 0.3f }, { releaseNoise, 0.5f } } },
    };
    return presets;
}

} // namespace gp
