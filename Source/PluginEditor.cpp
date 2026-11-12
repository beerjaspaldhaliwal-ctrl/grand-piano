#include "PluginEditor.h"

using namespace gp;

namespace
{
    constexpr int editorWidth = 1000;
    constexpr int editorHeight = 610;
    constexpr int knobSize = 74;
}

GrandPianoEditor::GrandPianoEditor (GrandPianoProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf);

    using namespace ParamID;
    sections.push_back ({ "PIANO",       { &addKnob (hardness, "Hammer"), &addKnob (touch, "Touch"),
                                           &addKnob (dynamics, "Dynamics"), &addKnob (release, "Release") }, {} });
    sections.push_back ({ "RESONANCE & NOISE", { &addKnob (resonance, "Sympathetic"), &addKnob (stringRelease, "Damper Res."),
                                           &addKnob (releaseNoise, "Key Noise"), &addKnob (pedalNoise, "Pedal Noise") }, {} });
    sections.push_back ({ "TONE",        { &addKnob (bass, "Bass"), &addKnob (mid, "Mid"), &addKnob (treble, "Treble"),
                                           &addKnob (compression, "Compress"), &addKnob (width, "Width") }, {} });
    sections.push_back ({ "ROOM",        { &addKnob (reverbMix, "Reverb"), &addKnob (reverbSize, "Size"),
                                           &addKnob (reverbTone, "Tone") }, {} });
    sections.push_back ({ "MASTER",      { &addKnob (volume, "Volume"), &addKnob (transpose, "Transpose"),
                                           &addKnob (tune, "Fine Tune"), &addKnob (stretch, "Stretch") }, {} });

    addAndMakeVisible (audienceToggle);
    audienceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.getState(), audience, audienceToggle);
    audienceToggle.setTooltip ("Hear the piano from the audience instead of the player's seat (swaps low/high sides)");

    addAndMakeVisible (presetBox);
    presetBox.setJustificationType (juce::Justification::centred);
    refreshPresetBox();
    presetBox.onChange = [this]
    {
        auto idx = presetBox.getSelectedItemIndex();
        if (idx >= 0 && idx != processor.getCurrentProgram())
            processor.setCurrentProgram (idx);
    };

    for (auto* b : { &prevPreset, &nextPreset })
        addAndMakeVisible (*b);
    prevPreset.onClick = [this]
    {
        auto n = processor.getNumPrograms();
        processor.setCurrentProgram ((processor.getCurrentProgram() + n - 1) % n);
        refreshPresetBox();
    };
    nextPreset.onClick = [this]
    {
        processor.setCurrentProgram ((processor.getCurrentProgram() + 1) % processor.getNumPrograms());
        refreshPresetBox();
    };

    addAndMakeVisible (samplesButton);
    samplesButton.onClick = [this] { showSamplesMenu(); };

    addChildComponent (getSamplesButton);
    getSamplesButton.setToggleState (true, juce::dontSendNotification);
    getSamplesButton.onClick = [this] { startDownload(); };

    addChildComponent (variantBox);
    variantBox.setTooltip ("Tuning variant of the loaded instrument");
    variantBox.onChange = [this]
    {
        auto variants = processor.getSampleManager().getVariants();
        auto idx = variantBox.getSelectedItemIndex();
        if (juce::isPositiveAndBelow (idx, variants.size()) && variants[idx] != processor.getSampleManager().getCurrentSfz())
            processor.getSampleManager().loadAsync (variants[idx]);
    };

    addAndMakeVisible (keyboard);
    keyboard.setAvailableRange (21, 108);
    keyboard.setLowestVisibleKey (21);
    keyboard.setKeyWidth (editorWidth / 52.0f - 0.4f);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setVelocity (0.75f, true);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Colours::gold.withAlpha (0.75f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Colours::gold.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xfff4f1ea));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff111113));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff9a968e));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.35f));

    setSize (editorWidth, editorHeight);
    startTimerHz (24);
    timerCallback();
}

GrandPianoEditor::~GrandPianoEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

GrandPianoEditor::Knob& GrandPianoEditor::addKnob (const char* paramId, const juce::String& text)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->slider.setVelocityBasedMode (false);
    k->slider.setMouseDragSensitivity (220);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k->slider);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSize, 16);

    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (PianoLookAndFeel::uiFont (12.5f, true));
    k->label.setColour (juce::Label::textColourId, Colours::text);
    addAndMakeVisible (k->label);

    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getState(), paramId, k->slider);
    if (auto* param = processor.getState().getParameter (paramId))
        k->slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    knobs.push_back (std::move (k));
    return *knobs.back();
}

void GrandPianoEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.setSelectedItemIndex (processor.getCurrentProgram(), juce::dontSendNotification);
}

void GrandPianoEditor::refreshVariantBox()
{
    auto& sm = processor.getSampleManager();
    auto names = sm.getVariantNames();
    auto variants = sm.getVariants();

    variantBox.clear (juce::dontSendNotification);
    variantBox.addItemList (names, 1);
    variantBox.setSelectedItemIndex (variants.indexOf (sm.getCurrentSfz()), juce::dontSendNotification);
    variantBox.setVisible (names.size() > 1);
    lastVariantCount = names.size();
    resized();
}

//==============================================================================
void GrandPianoEditor::showSamplesMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Piano samples");
    m.addItem (1, "Download Salamander Grand Piano (free, 740 MB)...");
    m.addItem (2, "Load SFZ instrument...");
    m.addSeparator();
    m.addItem (3, "Open samples folder");
    m.addItem (4, "Re-scan for samples");
    m.addSeparator();
    m.addItem (5, "About / credits");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&samplesButton), [this] (int result)
    {
        auto& sm = processor.getSampleManager();
        switch (result)
        {
            case 1: startDownload(); break;
            case 2: chooseSfz(); break;
            case 3:
            {
                auto cur = sm.getCurrentSfz();
                auto dir = cur.existsAsFile() ? cur.getParentDirectory() : SampleManager::getDataRoot();
                dir.createDirectory();
                dir.startAsProcess();
                break;
            }
            case 4: sm.begin ({}); break;
            case 5:
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Grand Piano",
                    "Grand Piano v" JucePlugin_VersionString "\n\n"
                    "Sampled piano engine with SFZ support, disk streaming, 16 velocity layers, "
                    "damper / key / pedal noises, sympathetic resonance, tone shaping and room reverb.\n\n"
                    "Default instrument: Salamander Grand Piano V3 (Yamaha C5) by Alexander Holm, "
                    "licensed CC-BY 3.0. FLAC/SFZ edition by the FreePats project.", "OK", this);
                break;
            default: break;
        }
    });
}

void GrandPianoEditor::startDownload()
{
    auto dest = SampleManager::getDownloadFolder();
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Download piano samples",
        "This downloads the Salamander Grand Piano (a Yamaha C5 concert grand, 16 velocity layers, "
        "about 740 MB, free under CC-BY 3.0) into:\n\n" + dest.getFullPathName()
        + "\n\nAbout 2 GB of free disk space is needed. Continue?",
        "Download", "Cancel", this,
        juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) processor.getSampleManager().downloadAsync(); }));
}

void GrandPianoEditor::chooseSfz()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an SFZ piano", SampleManager::getDataRoot(), "*.sfz");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile())
            processor.getSampleManager().loadAsync (f);
    });
}

//==============================================================================
void GrandPianoEditor::timerCallback()
{
    auto& sm = processor.getSampleManager();
    const auto state = sm.getState();

    auto text = sm.getStatusText();
    const bool busy = state == SampleManager::State::Building || state == SampleManager::State::Downloading;
    const float prog = sm.getProgress();

    if (text != statusText || busy != showProgress || (busy && std::abs (prog - progress) > 0.002f))
    {
        statusText = text;
        showProgress = busy;
        progress = prog;
        repaint (statusArea);
    }

    juce::String inst = processor.isUsingSamples() ? sm.getCurrentSfz().getFileNameWithoutExtension()
                                                   : juce::String ("Modelled piano (no samples loaded)");
    if (inst.containsIgnoreCase ("salamander"))
        inst = "Yamaha C5 Concert Grand  |  16 velocity layers";
    if (inst != instrumentText)
    {
        instrumentText = inst;
        repaint (headerArea);
    }

    const bool needSamples = state == SampleManager::State::NotFound
                          || (state == SampleManager::State::Error && ! processor.isUsingSamples());
    if (getSamplesButton.isVisible() != needSamples)
    {
        getSamplesButton.setVisible (needSamples);
        resized();
    }

    auto cur = sm.getCurrentSfz();
    if (cur != lastSfz || lastVariantCount < 0)
    {
        lastSfz = cur;
        refreshVariantBox();
    }

    if (presetBox.getSelectedItemIndex() != processor.getCurrentProgram())
        presetBox.setSelectedItemIndex (processor.getCurrentProgram(), juce::dontSendNotification);

    const float level = processor.getOutputLevel();
    if (std::abs (level - meter) > 0.002f)
    {
        meter = level;
        repaint (meterArea);
    }
}

//==============================================================================
void GrandPianoEditor::paint (juce::Graphics& g)
{
    // lacquer background
    juce::ColourGradient bg (juce::Colour (0xff1a1a1e), 0, 0, Colours::background, 0, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // header
    auto h = headerArea.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (h);
    g.setColour (Colours::goldDim.withAlpha (0.6f));
    g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());

    g.setColour (Colours::gold);
    g.setFont (PianoLookAndFeel::titleFont (30.0f));
    g.drawText ("GRAND PIANO", headerArea.withTrimmedLeft (22).withWidth (300).withTrimmedBottom (18), juce::Justification::bottomLeft);
    g.setColour (Colours::textDim);
    g.setFont (PianoLookAndFeel::uiFont (12.0f));
    g.drawText (instrumentText, headerArea.withTrimmedLeft (24).withWidth (340).withTrimmedTop (headerArea.getHeight() - 22),
                juce::Justification::topLeft);

    // sections
    for (auto& s : sections)
    {
        auto r = s.bounds.toFloat();
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (Colours::gold);
        g.setFont (PianoLookAndFeel::uiFont (11.5f, true));
        g.drawText (s.title, s.bounds.withHeight (26).withTrimmedLeft (14), juce::Justification::centredLeft);
        g.setColour (Colours::panelEdge);
        g.drawHorizontalLine (s.bounds.getY() + 26, r.getX() + 12.0f, r.getRight() - 12.0f);
    }

    // status line
    auto st = statusArea.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (st, 5.0f);
    if (showProgress)
    {
        g.setColour (Colours::goldDim.withAlpha (0.55f));
        g.fillRoundedRectangle (st.withWidth (st.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 5.0f);
    }
    g.setColour (processor.isUsingSamples() ? Colours::textDim : Colours::gold);
    g.setFont (PianoLookAndFeel::uiFont (12.0f));
    auto shown = statusText;
    if (showProgress) shown << "  " << juce::roundToInt (progress * 100.0f) << "%";
    g.drawFittedText (shown, statusArea.reduced (10, 0), juce::Justification::centredLeft, 1);

    // output meter
    if (! meterArea.isEmpty())
    {
        auto m = meterArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (m, 3.0f);
        const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = m.reduced (2.0f);
        fill = fill.removeFromBottom (fill.getHeight() * frac);
        g.setColour (meter > 0.98f ? juce::Colours::indianred : Colours::gold);
        g.fillRoundedRectangle (fill, 2.0f);
    }
}

void GrandPianoEditor::resized()
{
    auto area = getLocalBounds();
    headerArea = area.removeFromTop (70);

    // header controls
    {
        auto h = headerArea.reduced (16, 18);
        samplesButton.setBounds (h.removeFromRight (90));
        h.removeFromRight (8);
        if (getSamplesButton.isVisible())
        {
            getSamplesButton.setBounds (h.removeFromRight (110));
            h.removeFromRight (8);
        }
        if (variantBox.isVisible())
        {
            variantBox.setBounds (h.removeFromRight (150));
            h.removeFromRight (8);
        }

        auto centre = juce::Rectangle<int> (0, h.getY(), 330, h.getHeight()).withCentre ({ getWidth() / 2 + 40, h.getCentreY() });
        prevPreset.setBounds (centre.removeFromLeft (34));
        nextPreset.setBounds (centre.removeFromRight (34));
        presetBox.setBounds (centre.reduced (6, 0));
    }

    auto keyboardArea = area.removeFromBottom (118);
    keyboard.setBounds (keyboardArea);
    keyboard.setKeyWidth ((float) keyboardArea.getWidth() / 52.0f);

    area.removeFromBottom (8);
    statusArea = area.removeFromBottom (24).reduced (16, 0);
    area.removeFromBottom (6);

    auto content = area.reduced (16, 12);
    const int gap = 12;
    const int sectionHeight = (content.getHeight() - gap) / 2;
    auto row1 = content.removeFromTop (sectionHeight);
    content.removeFromTop (gap);
    auto row2 = content;

    auto layoutSection = [&] (Section& s, juce::Rectangle<int> r, bool withExtras = false)
    {
        s.bounds = r;
        auto inner = r.withTrimmedTop (30).reduced (8, 6);
        if (withExtras)
        {
            auto extras = inner.removeFromRight (64);
            meterArea = extras.removeFromRight (10).reduced (0, 8);
            extras.removeFromRight (6);
            audienceToggle.setBounds (juce::Rectangle<int> (0, 0, 160, 24)
                                          .withCentre ({ r.getCentreX(), r.getBottom() - 16 }));
            juce::ignoreUnused (extras);
        }
        const int n = (int) s.knobs.size();
        const int w = inner.getWidth() / juce::jmax (1, n);
        for (int i = 0; i < n; ++i)
        {
            auto cell = inner.withX (inner.getX() + i * w).withWidth (w);
            auto* k = s.knobs[(size_t) i];
            k->label.setBounds (cell.removeFromTop (18));
            k->slider.setBounds (cell.withSizeKeepingCentre (juce::jmin (w, knobSize + 6), juce::jmin (cell.getHeight(), knobSize + 22)));
        }
    };

    const int half = (row1.getWidth() - gap) / 2;
    layoutSection (sections[0], row1.removeFromLeft (half));
    row1.removeFromLeft (gap);
    layoutSection (sections[1], row1);

    const int unit = (row2.getWidth() - 2 * gap) / 12;
    layoutSection (sections[2], row2.removeFromLeft (unit * 5));
    row2.removeFromLeft (gap);
    layoutSection (sections[3], row2.removeFromLeft (unit * 3 - 10));
    row2.removeFromLeft (gap);
    layoutSection (sections[4], row2, true);

    // the master section also hosts the audience toggle below its knobs
    auto& master = sections[4];
    for (auto* k : master.knobs)
        k->slider.setBounds (k->slider.getBounds().withTrimmedBottom (18));
}
