#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace gp;

GrandPianoProcessor::GrandPianoProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GrandPiano", createParameterLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    p.volume = get (ParamID::volume);           p.transpose = get (ParamID::transpose);
    p.tune = get (ParamID::tune);               p.stretch = get (ParamID::stretch);               p.touch = get (ParamID::touch);
    p.dynamics = get (ParamID::dynamics);       p.hardness = get (ParamID::hardness);
    p.release = get (ParamID::release);         p.releaseNoise = get (ParamID::releaseNoise);
    p.stringRelease = get (ParamID::stringRelease);
    p.pedalNoise = get (ParamID::pedalNoise);   p.resonance = get (ParamID::resonance);
    p.bass = get (ParamID::bass);               p.mid = get (ParamID::mid);
    p.treble = get (ParamID::treble);           p.compression = get (ParamID::compression);
    p.width = get (ParamID::width);             p.audience = get (ParamID::audience);
    p.reverbMix = get (ParamID::reverbMix);     p.reverbSize = get (ParamID::reverbSize);
    p.reverbTone = get (ParamID::reverbTone);   p.polyphony = get (ParamID::polyphony);

    sampleManager = std::make_unique<SampleManager> (voiceMonitor);
    sampleManager->begin ({});
}

GrandPianoProcessor::~GrandPianoProcessor()
{
    sampleManager.reset();
}

bool GrandPianoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void GrandPianoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    reverb.prepare (sampleRate);
    resonanceBody.prepare (sampleRate);
    resonanceBody.update (0.28f, 2.6f, 0.3f, 0.0f, true);
    eq.prepare (sampleRate);
    compressor.prepare (sampleRate);
    limiter.prepare (sampleRate);

    wetBuffer.setSize (2, samplesPerBlock, false, false, true);
    sendBuffer.setSize (2, samplesPerBlock, false, false, true);

    for (auto* s : { &outputGain, &resonanceSend, &reverbWet, &reverbDry, &widthSmoothed })
        s->reset (sampleRate, 0.05);
    resonanceSend.reset (sampleRate, 0.25); // pedal resonance blooms in / fades out
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.volume->load()));
    widthSmoothed.setCurrentAndTargetValue (p.width->load());
    keyboardState.reset();
}

void GrandPianoProcessor::updateParameters()
{
    EngineParams e;
    e.touch = p.touch->load();
    e.dynamics = p.dynamics->load();
    e.hardness = p.hardness->load();
    e.releaseScale = p.release->load();
    e.releaseNoise = p.releaseNoise->load();
    e.stringRelease = p.stringRelease->load();
    e.pedalNoise = p.pedalNoise->load();
    e.tuneCents = p.tune->load();
    e.stretch = p.stretch->load();
    e.transpose = (int) p.transpose->load();
    e.polyphony = (int) p.polyphony->load();
    engine.setParams (e);

    eq.update (p.bass->load(), p.mid->load(), p.treble->load());

    const float size = p.reverbSize->load();
    reverb.update (size, 0.7f + size * size * 4.3f, p.reverbTone->load(), 4.0f + size * 26.0f);
}

void GrandPianoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // pick up a newly loaded sample library
    auto* lib = sampleManager->getRequestedLibrary();
    if (lib != engine.getLibrary())
        engine.setLibrary (lib);
    sampleManager->acknowledge (lib);
    usingSamples = lib != nullptr;

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    updateParameters();

    if (wetBuffer.getNumSamples() < numSamples)
    {
        wetBuffer.setSize (2, numSamples, false, false, true);
        sendBuffer.setSize (2, numSamples, false, false, true);
    }

    buffer.clear();
    if (buffer.getNumChannels() < 2)
    {
        // render in stereo, fold down at the end
        juce::AudioBuffer<float> stereo (sendBuffer.getArrayOfWritePointers(), 2, numSamples);
        stereo.clear();
        engine.process (stereo, midi);
        buffer.copyFrom (0, 0, stereo, 0, 0, numSamples);
        buffer.addFrom (0, 0, stereo, 1, 0, numSamples);
        buffer.applyGain (0.5f);
        midi.clear();
        return;
    }

    engine.process (buffer, midi);
    activeVoices = engine.getActiveVoiceCount();

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);

    // --- sympathetic string resonance while the sustain pedal is down ---
    resonanceSend.setTargetValue (p.resonance->load() * engine.getSustainAmount());
    if (resonanceSend.isSmoothing() || resonanceSend.getTargetValue() > 0.0001f)
    {
        float* sl = sendBuffer.getWritePointer (0);
        float* sr = sendBuffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = resonanceSend.getNextValue() * 0.55f;
            sl[i] = l[i] * g; sr[i] = r[i] * g;
        }
        float* wl = wetBuffer.getWritePointer (0);
        float* wr = wetBuffer.getWritePointer (1);
        resonanceBody.process (sl, sr, wl, wr, numSamples);
        for (int i = 0; i < numSamples; ++i) { l[i] += wl[i]; r[i] += wr[i]; }
    }
    else
    {
        resonanceSend.skip (numSamples);
        // keep the tail ringing out naturally
        sendBuffer.clear();
        float* wl = wetBuffer.getWritePointer (0);
        float* wr = wetBuffer.getWritePointer (1);
        resonanceBody.process (sendBuffer.getReadPointer (0), sendBuffer.getReadPointer (1), wl, wr, numSamples);
        for (int i = 0; i < numSamples; ++i) { l[i] += wl[i]; r[i] += wr[i]; }
    }

    // --- perspective ---
    if (p.audience->load() > 0.5f)
        for (int i = 0; i < numSamples; ++i) std::swap (l[i], r[i]);

    // --- tone ---
    eq.process (l, r, numSamples);
    compressor.process (l, r, numSamples, p.compression->load());
    gainReduction = compressor.getGainReductionDb();

    // --- stereo width (mid/side) ---
    widthSmoothed.setTargetValue (p.width->load());
    for (int i = 0; i < numSamples; ++i)
    {
        const float w = widthSmoothed.getNextValue();
        const float m = 0.5f * (l[i] + r[i]);
        const float s = 0.5f * (l[i] - r[i]) * w;
        l[i] = m + s; r[i] = m - s;
    }

    // --- room ---
    const float mix = p.reverbMix->load();
    reverbWet.setTargetValue (mix * 1.4f);
    reverbDry.setTargetValue (1.0f - 0.45f * mix);
    {
        float* wl = wetBuffer.getWritePointer (0);
        float* wr = wetBuffer.getWritePointer (1);
        reverb.process (l, r, wl, wr, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float wet = reverbWet.getNextValue();
            const float dry = reverbDry.getNextValue();
            l[i] = l[i] * dry + wl[i] * wet;
            r[i] = r[i] * dry + wr[i] * wet;
        }
    }

    // --- output ---
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (p.volume->load()));
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain.getNextValue();
        l[i] *= g; r[i] *= g;
        peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
    }
    limiter.process (l, r, numSamples);
    outputLevel = juce::jmax (peak, outputLevel.load() * 0.9f);

    midi.clear();
}

//==============================================================================
int GrandPianoProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

const juce::String GrandPianoProcessor::getProgramName (int index)
{
    auto& presets = getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? juce::String (presets[(size_t) index].name) : juce::String();
}

void GrandPianoProcessor::setCurrentProgram (int index)
{
    auto& presets = getFactoryPresets();
    // Hosts sometimes re-send the current program after restoring a project;
    // re-applying it would wipe the user's tweaks.
    if (! juce::isPositiveAndBelow (index, (int) presets.size()) || index == currentProgram)
        return;

    currentProgram = index;

    // reset the sound-shaping parameters to defaults, then apply the preset
    static const char* keepIds[] = { ParamID::transpose, ParamID::tune, ParamID::stretch, ParamID::polyphony, ParamID::volume };
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            bool keep = false;
            for (auto* k : keepIds) keep |= ranged->getParameterID() == k;
            if (! keep)
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
        }

    for (auto& [id, value] : presets[(size_t) index].values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));

    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

//==============================================================================
void GrandPianoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    auto sfz = sampleManager->getCurrentSfz();
    state.setProperty ("sfz", sfz.existsAsFile() ? sfz.getFullPathName() : savedSfzPath, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GrandPianoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    currentProgram = state.getProperty ("program", 0);
    savedSfzPath = state.getProperty ("sfz", "").toString();
    state.removeProperty ("program", nullptr);
    state.removeProperty ("sfz", nullptr);
    apvts.replaceState (state);

    if (juce::File::isAbsolutePath (savedSfzPath))
    {
        juce::File sfz (savedSfzPath);
        if (sfz.existsAsFile() && sfz != sampleManager->getCurrentSfz())
            sampleManager->loadAsync (sfz);
    }
}

juce::AudioProcessorEditor* GrandPianoProcessor::createEditor()
{
    return new GrandPianoEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrandPianoProcessor();
}
