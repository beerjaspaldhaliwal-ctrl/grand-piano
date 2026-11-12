#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"
#include "engine/PianoEngine.h"
#include "engine/SampleManager.h"
#include "dsp/Effects.h"

class GrandPianoProcessor : public juce::AudioProcessor
{
public:
    GrandPianoProcessor();
    ~GrandPianoProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    gp::SampleManager& getSampleManager() noexcept           { return *sampleManager; }
    juce::MidiKeyboardState& getKeyboardState() noexcept     { return keyboardState; }

    float getOutputLevel() const noexcept     { return outputLevel.load(); }
    float getGainReduction() const noexcept   { return gainReduction.load(); }
    int getActiveVoices() const noexcept      { return activeVoices.load(); }
    bool isUsingSamples() const noexcept      { return usingSamples.load(); }

private:
    void updateParameters();

    juce::AudioProcessorValueTreeState apvts;
    gp::VoiceMonitor voiceMonitor;
    gp::PianoEngine engine { voiceMonitor };
    std::unique_ptr<gp::SampleManager> sampleManager;
    juce::MidiKeyboardState keyboardState;

    gp::FdnReverb reverb, resonanceBody;
    gp::PianoEq eq;
    gp::GlueCompressor compressor;
    gp::SafetyLimiter limiter;

    juce::AudioBuffer<float> wetBuffer, sendBuffer;
    juce::SmoothedValue<float> outputGain, resonanceSend, reverbWet, reverbDry, widthSmoothed;

    struct ParamPointers
    {
        std::atomic<float>* volume; std::atomic<float>* transpose; std::atomic<float>* tune; std::atomic<float>* stretch;
        std::atomic<float>* touch; std::atomic<float>* dynamics; std::atomic<float>* hardness;
        std::atomic<float>* release; std::atomic<float>* releaseNoise; std::atomic<float>* stringRelease;
        std::atomic<float>* pedalNoise; std::atomic<float>* resonance;
        std::atomic<float>* bass; std::atomic<float>* mid; std::atomic<float>* treble;
        std::atomic<float>* compression; std::atomic<float>* width; std::atomic<float>* audience;
        std::atomic<float>* reverbMix; std::atomic<float>* reverbSize; std::atomic<float>* reverbTone;
        std::atomic<float>* polyphony;
    } p {};

    int currentProgram = 0;
    juce::String savedSfzPath;

    std::atomic<float> outputLevel { 0.0f }, gainReduction { 0.0f };
    std::atomic<int> activeVoices { 0 };
    std::atomic<bool> usingSamples { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrandPianoProcessor)
};
