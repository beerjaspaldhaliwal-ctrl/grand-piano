#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace gp
{

namespace Colours
{
    inline const juce::Colour background   { 0xff0d0d0f };
    inline const juce::Colour panel        { 0xff17171b };
    inline const juce::Colour panelEdge    { 0xff2a2a30 };
    inline const juce::Colour gold         { 0xffd4af6a };
    inline const juce::Colour goldDim      { 0xff7d6a45 };
    inline const juce::Colour text         { 0xffe8e3d8 };
    inline const juce::Colour textDim      { 0xff8d8a83 };
    inline const juce::Colour knob         { 0xff26262b };
}

class PianoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PianoLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    static juce::Font titleFont (float height);
    static juce::Font uiFont (float height, bool bold = false);
};

} // namespace gp
