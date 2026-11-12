#include "LookAndFeel.h"

namespace gp
{

PianoLookAndFeel::PianoLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Colours::goldDim);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::ComboBox::backgroundColourId, Colours::panel);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::panelEdge);
    setColour (juce::ComboBox::arrowColourId, Colours::gold);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::goldDim);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::headerTextColourId, Colours::gold);
    setColour (juce::TextButton::buttonColourId, Colours::panel);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::gold);
    setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::goldDim);
    setColour (juce::TextEditor::backgroundColourId, Colours::background);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::highlightColourId, Colours::goldDim);
    setColour (juce::CaretComponent::caretColourId, Colours::gold);
}

juce::Font PianoLookAndFeel::titleFont (float height)
{
    return juce::Font (juce::FontOptions ("Georgia", height, juce::Font::plain));
}

juce::Font PianoLookAndFeel::uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font PianoLookAndFeel::getLabelFont (juce::Label&)            { return uiFont (13.0f); }
juce::Font PianoLookAndFeel::getComboBoxFont (juce::ComboBox&)      { return uiFont (14.0f); }
juce::Font PianoLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return uiFont (13.0f, true); }

juce::Label* PianoLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (uiFont (12.0f));
    l->setColour (juce::Label::textColourId, Colours::textDim);
    l->setColour (juce::Label::outlineWhenEditingColourId, Colours::goldDim);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void PianoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto area = bounds.withSizeKeepingCentre (size, size);
    const auto centre = area.getCentre();
    const float radius = size * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const bool hover = s.isMouseOverOrDragging();

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::panelEdge);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const float from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (hover ? Colours::gold.brighter (0.2f) : Colours::gold);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // body
    const float bodyR = radius - 8.0f;
    auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (body.translated (0.0f, 2.0f));
    juce::ColourGradient grad (Colours::knob.brighter (0.35f), centre.x, body.getY(),
                               Colours::knob.darker (0.6f), centre.x, body.getBottom(), false);
    g.setGradientFill (grad);
    g.fillEllipse (body);
    g.setColour (Colours::panelEdge.brighter (0.3f));
    g.drawEllipse (body, 1.0f);

    // pointer
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -bodyR + 3.0f, 3.0f, bodyR * 0.45f, 1.5f);
    g.setColour (Colours::gold);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void PianoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    auto sw = r.removeFromLeft (34.0f).withSizeKeepingCentre (34.0f, 18.0f);
    const bool on = b.getToggleState();

    g.setColour (on ? Colours::goldDim : Colours::panelEdge);
    g.fillRoundedRectangle (sw, 9.0f);
    auto knobR = sw.withWidth (14.0f).withHeight (14.0f).translated (2.0f, 2.0f);
    if (on) knobR = knobR.withX (sw.getRight() - 16.0f);
    g.setColour (on ? Colours::gold : (highlighted ? Colours::text : Colours::textDim));
    g.fillEllipse (knobR);

    g.setColour (Colours::text);
    g.setFont (uiFont (13.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
}

void PianoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto base = b.getToggleState() ? Colours::goldDim.darker (0.4f) : Colours::panel;
    if (highlighted) base = base.brighter (0.12f);
    if (down) base = base.darker (0.2f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (highlighted ? Colours::gold : Colours::panelEdge.brighter (0.2f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void PianoLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.isMouseOver (true) ? Colours::gold : Colours::panelEdge.brighter (0.2f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Colours::gold);
    g.fillPath (arrow);
}

} // namespace gp
