#pragma once

#include "Library.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <random>

namespace gp
{

struct EngineParams
{
    float touch = 0.0f;          // -1 heavy .. +1 light
    float dynamics = 1.0f;       // 0..1.5, scales the SFZ velocity tracking
    float hardness = 0.0f;       // -1 mellow .. +1 bright (velocity layer shift)
    float releaseScale = 1.0f;   // damper release time multiplier
    float releaseNoise = 0.5f;   // 0..1 (0.5 = original level)
    float stringRelease = 0.5f;  // 0..1 string resonance on release (0.5 = original)
    float pedalNoise = 0.5f;     // 0..1
    float tuneCents = 0.0f;
    float stretch = 0.5f;        // 0 = exact equal temperament, 1 = tuning as recorded
    int transpose = 0;
    int polyphony = 128;
};

/** Sample-playback piano voice engine driven by an SFZ instrument, with a
    modelled-piano fallback when no samples are installed. Real-time safe. */
class PianoEngine
{
public:
    explicit PianoEngine (VoiceMonitor& monitor);

    void prepare (double sampleRate, int maxBlockSize);
    void setLibrary (Library* lib);          // audio thread only
    Library* getLibrary() const noexcept     { return library; }

    void setParams (const EngineParams& p)   { params = p; }

    /** Renders into `out` (cleared by the caller), handling all MIDI in `midi`. */
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi);

    void allNotesOff (bool immediately);

    float getSustainAmount() const noexcept   { return sustainDown ? 1.0f : 0.0f; }
    int getActiveVoiceCount() const noexcept  { return activeVoices; }
    bool isKeyDown (int key) const noexcept   { return key >= 0 && key < 128 && keyDown[(size_t) key]; }

    static constexpr int maxVoices = VoiceMonitor::maxSlots;
    static constexpr int maxPartials = 40;

private:
    struct ModelState
    {
        int numOsc = 0;
        float re[maxPartials * 2], im[maxPartials * 2];
        float cr[maxPartials * 2], ci[maxPartials * 2];   // rotation per sample
        float amp[maxPartials * 2], decay[maxPartials * 2];
        float noiseEnv = 0, noiseDecay = 0, noiseAmp = 0;
        float bpZ1 = 0, bpZ2 = 0, bpB0 = 0, bpA1 = 0, bpA2 = 0;
        float attack = 0, attackStep = 0;
        float panL = 1, panR = 1;
        uint32_t rng = 1;
    };

    struct Voice
    {
        enum class Kind { Off, Sample, Model };
        Kind kind = Kind::Off;

        int key = -1;              // played key (-1 = pedal noise)
        int velocity = 0;
        bool isNote = false;       // attack region: can be damped / sustained
        bool keyHeld = false;
        bool sustained = false;    // waiting for the sustain pedal to lift
        bool sostenuto = false;
        bool released = false;
        bool killing = false;
        int group = 0, offBy = 0;
        int regionIndex = -1;
        float releaseTime = 1.0f;

        int sampleIndex = -1;
        const CachedSample* sample = nullptr;
        double pos = 0, inc = 1;
        float gainL = 0, gainR = 0;

        float env = 1.0f, envMul = 1.0f;  // exponential release
        float kill = 1.0f, killStep = 0;  // linear fade for stolen voices

        int64_t startSample = 0;
        ModelState model;
    };

    void handleMidi (const juce::MidiMessage& m);
    void noteOn (int key, int velocity);
    void noteOff (int key);
    void setSustain (bool down);
    void setSostenuto (bool down);
    void controllerChanged (int cc, int value);

    void triggerReleaseSamples (const Voice& v);
    Voice* allocateVoice();
    void startModelVoice (int key, int ownerKey, int velocity);
    Voice* startRegionVoice (int regionIndex, int pitchKey, int ownerKey, int velocity, float gain);
    void releaseVoice (Voice& v, float time);
    void killVoice (Voice& v, float seconds);
    int findRegion (const std::vector<int>& candidates, int vel) const;

    void renderVoices (juce::AudioBuffer<float>& out, int start, int num);
    void renderSampleVoice (Voice& v, float* l, float* r, int num);
    void renderModelVoice (Voice& v, float* l, float* r, int num);

    float shapeVelocity (int velocity) const;

    VoiceMonitor& monitor;
    Library* library = nullptr;
    EngineParams params;

    double sampleRate = 44100.0;
    int64_t sampleClock = 0;
    int activeVoices = 0;

    std::array<Voice, maxVoices> voices;
    std::array<bool, 128> keyDown {};
    std::array<int, 128> keyVelocity {};
    std::array<int64_t, 128> keyOnTime {};
    std::array<int, 128> ccValues {};
    bool sustainDown = false, sostenutoDown = false, softDown = false;

    std::minstd_rand rng { 12345 };
};

} // namespace gp
