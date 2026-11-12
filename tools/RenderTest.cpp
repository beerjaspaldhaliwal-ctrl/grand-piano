// Offline renderer used to test the instrument without a DAW.
//   RenderTest <out.wav> [instrument.sfz | --model] [preset-index] [paramId=value ...]
#include "PluginProcessor.h"
#include "engine/SampleDownloader.h"
#include <juce_audio_formats/juce_audio_formats.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::cout << "usage: RenderTest out.wav [file.sfz|--model] [preset]\n";
        return 1;
    }

    if (juce::String (argv[1]) == "--extract" && argc > 3) // test the downloader's unpacker
    {
        std::atomic<float> prog { 0 };
        juce::String err;
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        bool ok = gp::SampleDownloader::extractTarGz (juce::File (argv[2]), juce::File (argv[3]), prog, [] { return false; }, err);
        std::cout << (ok ? "extracted" : ("FAILED: " + err).toStdString()) << " in "
                  << (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 << " s\n";
        return ok ? 0 : 3;
    }

    if (juce::String (argv[1]) == "--download" && argc > 2)
    {
        std::atomic<float> prog { 0 };
        juce::String err;
        gp::SampleDownloader dl;
        bool ok = dl.run (juce::File (argv[2]), prog, [] (const juce::String& s) { std::cout << s << "\n"; },
                          [] { return false; }, err);
        std::cout << (ok ? "download+install OK" : ("FAILED: " + err).toStdString()) << "\n";
        return ok ? 0 : 3;
    }

    juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    juce::String source = argc > 2 ? argv[2] : "";
    int preset = argc > 3 ? juce::String (argv[3]).getIntValue() : 0;

    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& proc = *dynamic_cast<GrandPianoProcessor*> (base.get());

    const double sr = 48000.0;
    const int block = 256;
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);
    proc.setCurrentProgram (preset);
    for (int i = 4; i < argc; ++i) // extra args: paramId=value
    {
        auto kv = juce::String (argv[i]);
        if (auto* param = proc.getState().getParameter (kv.upToFirstOccurrenceOf ("=", false, false)))
            param->setValueNotifyingHost (param->convertTo0to1 (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }

    auto& sm = proc.getSampleManager();

    if (outFile.hasFileExtension ("png")) // UI snapshot mode
    {
        if (source.isNotEmpty() && source != "--model")
        {
            sm.loadAsync (juce::File::getCurrentWorkingDirectory().getChildFile (source));
            while (sm.getState() != gp::SampleManager::State::Ready) juce::Thread::sleep (50);
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer mb; proc.processBlock (b, mb);
        }
        else
            juce::Thread::sleep (300);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorIfNeeded());
        proc.getKeyboardState().noteOn (1, 60, 1.0f);
        proc.getKeyboardState().noteOn (1, 64, 1.0f);
        proc.getKeyboardState().noteOn (1, 67, 1.0f);
        juce::Thread::sleep (100);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        outFile.deleteFile();
        juce::FileOutputStream os (outFile);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << "Snapshot " << img.getWidth() << "x" << img.getHeight() << "\n";
        proc.editorBeingDeleted (ed.get());
        return 0;
    }

    if (source != "--model")
    {
        if (source.isNotEmpty())
            sm.loadAsync (juce::File::getCurrentWorkingDirectory().getChildFile (source));

        auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (;;)
        {
            auto st = sm.getState();
            if (st == gp::SampleManager::State::Ready && sm.getRequestedLibrary() != nullptr) break;
            if (st == gp::SampleManager::State::Error || st == gp::SampleManager::State::NotFound)
            {
                std::cout << "Samples unavailable: " << sm.getStatusText() << "\n";
                return 2;
            }
            juce::Thread::sleep (100);
        }
        std::cout << "Loaded in " << (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 << " s: "
                  << sm.getStatusText() << "\n";
    }

    // --- performance script: (time seconds, message) ---
    std::vector<std::pair<double, juce::MidiMessage>> events;
    auto note = [&] (double t, int n, int v, double len)
    {
        events.push_back ({ t, juce::MidiMessage::noteOn (1, n, (juce::uint8) v) });
        events.push_back ({ t + len, juce::MidiMessage::noteOff (1, n) });
    };
    auto cc = [&] (double t, int c, int v) { events.push_back ({ t, juce::MidiMessage::controllerEvent (1, c, v) }); };

    double t = 0.2;
    // 1. velocity sweep on middle C (layers)
    for (int v : { 10, 30, 50, 70, 90, 110, 127 }) { note (t, 60, v, 0.5); t += 0.7; }
    // 2. chromatic run over the full range
    t += 0.5;
    for (int n = 21; n <= 108; n += 5) { note (t, n, 85, 0.25); t += 0.18; }
    t += 1.5;
    // 3. pedalled chord progression
    cc (t - 0.05, 64, 127);
    for (auto chord : std::vector<std::vector<int>> { { 48, 55, 60, 64, 67 }, { 45, 52, 57, 60, 64 }, { 41, 48, 53, 57, 60 }, { 43, 50, 55, 59, 62 } })
    {
        for (auto n : chord) note (t, n, 75, 0.6);
        t += 1.6;
        cc (t - 0.05, 64, 0); cc (t - 0.02, 64, 127);
    }
    cc (t, 64, 0);
    t += 1.0;
    // 4. fast repeated notes (re-strike) and a fortissimo bass hit
    for (int i = 0; i < 8; ++i) { note (t, 72, 100, 0.08); t += 0.11; }
    t += 0.3;
    note (t, 24, 127, 2.5); note (t, 36, 127, 2.5);
    t += 3.5;
    // 5. stress: pedalled glissando over all 88 keys, up and down, twice
    cc (t - 0.05, 64, 127);
    for (int rep = 0; rep < 2; ++rep)
    {
        for (int n = 21; n <= 108; ++n) { note (t, n, 100, 0.05); t += 0.012; }
        for (int n = 108; n >= 21; --n) { note (t, n, 100, 0.05); t += 0.012; }
    }
    cc (t + 1.5, 64, 0);
    t += 4.0;

    const auto totalSamples = (juce::int64) (t * sr);
    juce::AudioBuffer<float> out (2, (int) totalSamples);
    out.clear();

    std::sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    size_t ev = 0;
    auto start = juce::Time::getMillisecondCounterHiRes();
    int maxVoices = 0;

    for (juce::int64 pos = 0; pos < totalSamples; pos += block)
    {
        const int n = (int) juce::jmin ((juce::int64) block, totalSamples - pos);
        juce::AudioBuffer<float> buf (2, n);
        juce::MidiBuffer midi;
        while (ev < events.size() && (juce::int64) (events[ev].first * sr) < pos + n)
        {
            midi.addEvent (events[ev].second, (int) juce::jmax ((juce::int64) 0, (juce::int64) (events[ev].first * sr) - pos));
            ++ev;
        }
        proc.processBlock (buf, midi);
        maxVoices = juce::jmax (maxVoices, proc.getActiveVoices());
        for (int c = 0; c < 2; ++c) out.copyFrom (c, (int) pos, buf, c, 0, n);
    }

    auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    std::cout << "Rendered " << t << " s of audio in " << elapsed << " s (" << t / elapsed << "x real time), max voices "
              << maxVoices << ", peak " << juce::Decibels::gainToDecibels (out.getMagnitude (0, (int) totalSamples)) << " dBFS\n";

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (outFile), sr, 2, 24, {}, 0));
    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    std::cout << "Wrote " << outFile.getFullPathName() << "\n";
    return 0;
}
