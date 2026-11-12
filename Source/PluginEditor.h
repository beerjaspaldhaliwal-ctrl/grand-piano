#pragma once

#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"

class GrandPianoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GrandPianoEditor (GrandPianoProcessor&);
    ~GrandPianoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Section
    {
        juce::String title;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    Knob& addKnob (const char* paramId, const juce::String& label);
    void timerCallback() override;
    void refreshPresetBox();
    void refreshVariantBox();
    void showSamplesMenu();
    void startDownload();
    void chooseSfz();

    GrandPianoProcessor& processor;
    gp::PianoLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Section> sections;

    juce::ComboBox presetBox, variantBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, samplesButton { "Samples" }, getSamplesButton { "Get Samples" };
    juce::ToggleButton audienceToggle { "Audience view" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> audienceAttachment;

    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String statusText, instrumentText;
    float progress = 0.0f;
    bool showProgress = false;
    float meter = 0.0f;
    int lastVariantCount = -1;
    juce::File lastSfz;

    juce::Rectangle<int> headerArea, statusArea, meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrandPianoEditor)
};
