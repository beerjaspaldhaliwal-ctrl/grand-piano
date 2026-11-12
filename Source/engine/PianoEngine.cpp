#include "PianoEngine.h"
#include <cmath>

namespace gp
{

namespace
{
    constexpr float silence = 1.0e-4f;    // -80 dB: voice is finished
    constexpr int damperLimitKey = 90;    // strings above F#6 have no dampers on a grand

    inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

    inline float hermite (float xm1, float x0, float x1, float x2, float t)
    {
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
}

PianoEngine::PianoEngine (VoiceMonitor& m) : monitor (m) {}

void PianoEngine::prepare (double sr, int)
{
    sampleRate = sr;
    allNotesOff (true);
}

void PianoEngine::setLibrary (Library* lib)
{
    if (lib == library)
        return;
    allNotesOff (true);
    for (auto& v : voices) v.kind = Voice::Kind::Off;
    library = lib;
}

void PianoEngine::allNotesOff (bool immediately)
{
    for (auto& v : voices)
    {
        if (v.kind == Voice::Kind::Off)
            continue;
        if (immediately) v.kind = Voice::Kind::Off;
        else             killVoice (v, 0.05f);
    }
    keyDown.fill (false);
    sustainDown = sostenutoDown = softDown = false;
}

//==============================================================================
float PianoEngine::shapeVelocity (int velocity) const
{
    float v = juce::jlimit (1, 127, velocity) / 127.0f;
    // touch > 0: lighter action (easier to play loud)
    return std::pow (v, std::exp2 (-params.touch * 1.3f));
}

int PianoEngine::findRegion (const std::vector<int>& candidates, int vel) const
{
    const auto& regions = library->sfz.regions;
    int best = -1, bestDist = 1000;
    for (int idx : candidates)
    {
        auto& r = regions[(size_t) idx];
        if (vel >= r.loVel && vel <= r.hiVel)
            return idx;
        int d = vel < r.loVel ? r.loVel - vel : vel - r.hiVel;
        if (d < bestDist) { bestDist = d; best = idx; }
    }
    return best;
}

PianoEngine::Voice* PianoEngine::allocateVoice()
{
    int active = 0;
    Voice* freeVoice = nullptr;
    for (auto& v : voices)
    {
        if (v.kind == Voice::Kind::Off) { if (freeVoice == nullptr) freeVoice = &v; }
        else if (! v.killing)           ++active;
    }

    if (active >= params.polyphony || freeVoice == nullptr)
    {
        // Steal: quietest released voice first, otherwise the oldest note.
        Voice* victim = nullptr;
        float bestScore = 1.0e9f;
        for (auto& v : voices)
        {
            if (v.kind == Voice::Kind::Off || v.killing)
                continue;
            float age = (float) (sampleClock - v.startSample) / (float) sampleRate;
            float score = (v.released ? 0.0f : 10.0f) + v.env * (v.gainL + v.gainR) - age * 0.01f;
            if (! v.isNote) score -= 5.0f; // noises go first
            if (score < bestScore) { bestScore = score; victim = &v; }
        }
        if (victim != nullptr)
        {
            if (freeVoice == nullptr) { victim->kind = Voice::Kind::Off; return victim; }
            killVoice (*victim, 0.012f);
        }
    }
    return freeVoice;
}

void PianoEngine::killVoice (Voice& v, float seconds)
{
    if (v.killing && v.killStep * seconds * sampleRate > 1.0)
        return; // already fading faster
    v.killing = true;
    v.killStep = (float) (1.0 / juce::jmax (1.0, seconds * sampleRate));
}

void PianoEngine::releaseVoice (Voice& v, float time)
{
    v.released = true;
    v.sustained = false;
    v.keyHeld = false;
    time = juce::jmax (0.02f, time);
    // reach -60 dB after `time` seconds
    float mul = (float) std::pow (0.001, 1.0 / (time * sampleRate));
    v.envMul = juce::jmin (v.envMul < 1.0f ? v.envMul : 1.0f, mul);
}

//==============================================================================
PianoEngine::Voice* PianoEngine::startRegionVoice (int regionIndex, int pitchKey, int ownerKey, int velocity, float gain)
{
    auto& r = library->sfz.regions[(size_t) regionIndex];
    if (r.sampleIndex < 0 || r.sampleIndex >= library->cache.size())
        return nullptr;

    auto& smp = library->cache.get (r.sampleIndex);
    if (smp.frames < 4)
        return nullptr;

    // exclusive groups (pedal up cuts pedal down, etc.)
    if (r.group != 0)
        for (auto& v : voices)
            if (v.kind != Voice::Kind::Off && v.offBy == r.group)
                killVoice (v, 0.03f);

    auto* v = allocateVoice();
    if (v == nullptr)
        return nullptr;

    const int mappedKey = pitchKey < 0 ? r.keyCenter : pitchKey;
    float correction = 0.0f;
    if ((size_t) regionIndex < library->measuredCents.size())
        correction = -library->measuredCents[(size_t) regionIndex] * (1.0f - params.stretch);
    const float semis = (float) (mappedKey - r.keyCenter) * r.pitchKeytrack * 0.01f
                        + (float) r.transpose + (r.tuneCents + params.tuneCents + correction) * 0.01f;

    const float pan = juce::jlimit (-1.0f, 1.0f, r.pan * 0.01f);
    const float total = gain * dbToGain (r.volumeDb) * smp.gain;

    v->kind = Voice::Kind::Sample;
    v->key = ownerKey;
    v->velocity = velocity;
    v->isNote = r.trigger == Trigger::Attack;
    v->keyHeld = v->isNote;
    v->sustained = false;
    v->sostenuto = false;
    v->released = false;
    v->killing = false;
    v->group = r.group;
    v->offBy = r.offBy;
    v->regionIndex = regionIndex;
    v->releaseTime = r.releaseTime > 0.0f ? r.releaseTime : 1.0f;
    v->sampleIndex = r.sampleIndex;
    v->sample = &smp;
    v->pos = 0.0;
    v->inc = std::exp2 (semis / 12.0f) * library->cache.getSampleRate() / sampleRate;
    v->gainL = total * std::sqrt (0.5f * (1.0f - pan)) * juce::MathConstants<float>::sqrt2;
    v->gainR = total * std::sqrt (0.5f * (1.0f + pan)) * juce::MathConstants<float>::sqrt2;
    v->env = 1.0f;
    v->envMul = 1.0f;
    v->kill = 1.0f;
    v->killStep = 0.0f;
    v->startSample = sampleClock;
    return v;
}

void PianoEngine::noteOn (int key, int velocity)
{
    keyDown[(size_t) key] = true;
    keyVelocity[(size_t) key] = velocity;
    keyOnTime[(size_t) key] = sampleClock;

    // Re-striking a key that is still ringing: the hammer stops the old vibration.
    int sameKey = 0;
    for (auto& v : voices)
    {
        if (v.kind == Voice::Kind::Off || v.key != key || ! v.isNote || v.killing)
            continue;
        if (++sameKey > 2) killVoice (v, 0.02f);
        else               releaseVoice (v, 0.35f);
    }

    int vel = velocity;
    if (softDown)
        vel = juce::jmax (1, (int) std::round (vel * 0.78f)); // una corda

    const float shaped = shapeVelocity (vel);
    const int mappedKey = juce::jlimit (0, 127, key + params.transpose);

    if (library == nullptr)
    {
        startModelVoice (mappedKey, key, juce::roundToInt (shaped * 127.0f));
        return;
    }

    const auto& candidates = library->attackRegions[(size_t) mappedKey];
    if (candidates.empty())
        return;

    // Velocity used to pick the sample (timbre) vs. the one that sets loudness.
    const int naturalVel = juce::jlimit (1, 127, juce::roundToInt (shaped * 127.0f));
    const int timbreVel = juce::jlimit (1, 127, juce::roundToInt (naturalVel + params.hardness * 40.0f));

    const int natural = findRegion (candidates, naturalVel);
    const int chosen = findRegion (candidates, timbreVel);
    if (chosen < 0)
        return;

    // Play every region of the chosen velocity layer (usually one).
    const auto& chosenRegion = library->sfz.regions[(size_t) chosen];
    for (int idx : candidates)
    {
        auto& r = library->sfz.regions[(size_t) idx];
        if (r.loVel != chosenRegion.loVel || r.hiVel != chosenRegion.hiVel)
            continue;

        float peakSel = library->cache.get (r.sampleIndex).peak;
        float peakNat = natural >= 0 ? library->cache.get (library->sfz.regions[(size_t) natural].sampleIndex).peak : peakSel;
        float layerComp = juce::jlimit (0.1f, 10.0f, peakNat / juce::jmax (1.0e-6f, peakSel));

        float track = juce::jlimit (0.0f, 1.0f, r.ampVeltrack * 0.01f * params.dynamics);
        float velGain = (1.0f - track) + track * shaped * shaped;
        if (params.dynamics > 1.0f) // extra range beyond the instrument's own
            velGain *= std::pow (shaped, (params.dynamics - 1.0f) * 2.0f);
        if (softDown)
            velGain *= 0.85f;

        startRegionVoice (idx, mappedKey, key, velocity, velGain * layerComp);
    }
}

void PianoEngine::triggerReleaseSamples (const Voice& v)
{
    if (library == nullptr || v.key < 0)
        return;

    const int key = v.key;
    const int mappedKey = juce::jlimit (0, 127, key + params.transpose);
    const auto& candidates = library->releaseRegions[(size_t) mappedKey];
    if (candidates.empty())
        return;

    const float held = (float) (sampleClock - keyOnTime[(size_t) key]) / (float) sampleRate;
    const float shaped = shapeVelocity (v.velocity);
    const int vel = juce::jlimit (1, 127, v.velocity);

    for (int idx : candidates)
    {
        auto& r = library->sfz.regions[(size_t) idx];
        if (! r.matchesNote (mappedKey, vel))
            continue;

        // keytrack 0 = mechanical key/hammer noise, otherwise string resonance
        const bool isNoise = r.pitchKeytrack == 0.0f;
        const float level = (isNoise ? params.releaseNoise : params.stringRelease) * 2.0f;
        if (level <= 0.001f)
            continue;

        float track = juce::jlimit (0.0f, 1.0f, r.ampVeltrack * 0.01f);
        float g = ((1.0f - track) + track * shaped * shaped) * level;
        g *= dbToGain (-juce::jmin (100.0f, r.rtDecay * held));
        if (g < 1.0e-4f)
            continue;

        if (auto* nv = startRegionVoice (idx, mappedKey, -1, vel, g))
        {
            nv->isNote = false;
            nv->keyHeld = false;
        }
    }
}

void PianoEngine::noteOff (int key)
{
    keyDown[(size_t) key] = false;

    bool releasedSomething = false;
    Voice* reference = nullptr;

    for (auto& v : voices)
    {
        if (v.kind == Voice::Kind::Off || v.key != key || ! v.isNote || ! v.keyHeld)
            continue;

        v.keyHeld = false;
        reference = &v;

        if (sustainDown || v.sostenuto)
        {
            v.sustained = true;
            continue;
        }

        const bool undamped = juce::jlimit (0, 127, key + params.transpose) >= damperLimitKey;
        if (! undamped)
        {
            releaseVoice (v, v.releaseTime * params.releaseScale);
            releasedSomething = true;
        }
    }

    if (reference != nullptr)
    {
        // mechanical key noise always; string resonance only when the damper falls
        Voice copy = *reference;
        if (library != nullptr)
        {
            const float savedString = params.stringRelease;
            if (! releasedSomething)
                params.stringRelease = 0.0f;
            triggerReleaseSamples (copy);
            params.stringRelease = savedString;
        }
    }
}

void PianoEngine::setSustain (bool down)
{
    if (down == sustainDown)
        return;
    sustainDown = down;

    if (down)
        return;

    // Pedal lifted: damp every string whose key is up.
    std::array<bool, 128> triggered {};
    for (auto& v : voices)
    {
        if (v.kind == Voice::Kind::Off || ! v.isNote || ! v.sustained || v.sostenuto || v.keyHeld)
            continue;

        const bool undamped = juce::jlimit (0, 127, v.key + params.transpose) >= damperLimitKey;
        if (undamped)
        {
            v.sustained = false;
            continue;
        }

        // string resonance of the damper falling (key noise already played at key-up)
        if (library != nullptr && v.key >= 0 && ! triggered[(size_t) v.key])
        {
            triggered[(size_t) v.key] = true;
            Voice copy = v;
            const float savedNoise = params.releaseNoise;
            params.releaseNoise = 0.0f;
            triggerReleaseSamples (copy);
            params.releaseNoise = savedNoise;
        }
        releaseVoice (v, v.releaseTime * params.releaseScale);
    }
}

void PianoEngine::setSostenuto (bool down)
{
    if (down == sostenutoDown)
        return;
    sostenutoDown = down;

    for (auto& v : voices)
    {
        if (v.kind == Voice::Kind::Off || ! v.isNote)
            continue;

        if (down)
        {
            if (v.keyHeld) v.sostenuto = true;
        }
        else if (v.sostenuto)
        {
            v.sostenuto = false;
            if (! v.keyHeld && ! sustainDown && ! v.released)
                releaseVoice (v, v.releaseTime * params.releaseScale);
        }
    }
}

void PianoEngine::controllerChanged (int cc, int value)
{
    const int old = ccValues[(size_t) cc];
    ccValues[(size_t) cc] = value;

    if (library != nullptr && params.pedalNoise > 0.001f)
    {
        // pick one region among those whose range was just entered (round-robin via lorand/hirand)
        const float r01 = std::uniform_real_distribution<float> (0.0f, 1.0f) (rng);
        for (int idx : library->controllerRegions)
        {
            auto& r = library->sfz.regions[(size_t) idx];
            if (r.ccNumber != cc)
                continue;
            const bool nowIn = value >= r.ccLo && value <= r.ccHi;
            const bool wasIn = old >= r.ccLo && old <= r.ccHi;
            if (nowIn && ! wasIn && r01 >= r.loRand && r01 < r.hiRand)
                startRegionVoice (idx, -1, -1, 127, params.pedalNoise * 2.0f);
        }
    }

    switch (cc)
    {
        case 64: setSustain (value >= 64); break;
        case 66: setSostenuto (value >= 64); break;
        case 67: softDown = value >= 64; break;
        default: break;
    }
}

void PianoEngine::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc == 120)      allNotesOff (false);   // all sound off
        else if (cc == 123) // all notes off
        {
            for (int k = 0; k < 128; ++k) if (keyDown[(size_t) k]) noteOff (k);
            setSustain (false);
        }
        else controllerChanged (cc, m.getControllerValue());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        allNotesOff (false);
}

//==============================================================================
void PianoEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    const int numSamples = out.getNumSamples();
    int pos = 0;

    for (const auto meta : midi)
    {
        const int when = juce::jlimit (0, numSamples, meta.samplePosition);
        if (when > pos)
        {
            renderVoices (out, pos, when - pos);
            sampleClock += when - pos;
            pos = when;
        }
        handleMidi (meta.getMessage());
    }

    if (pos < numSamples)
    {
        renderVoices (out, pos, numSamples - pos);
        sampleClock += numSamples - pos;
    }

    // publish positions for the prefetch thread
    int active = 0;
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        auto& slot = monitor.slots[(size_t) i];
        if (v.kind != Voice::Kind::Off) ++active;
        if (v.kind == Voice::Kind::Sample)
        {
            slot.frame.store ((int) v.pos, std::memory_order_relaxed);
            slot.sample.store (v.sampleIndex, std::memory_order_relaxed);
        }
        else
            slot.sample.store (-1, std::memory_order_relaxed);
    }
    activeVoices = active;
}

void PianoEngine::renderVoices (juce::AudioBuffer<float>& out, int start, int num)
{
    float* l = out.getWritePointer (0, start);
    float* r = out.getWritePointer (out.getNumChannels() > 1 ? 1 : 0, start);

    for (auto& v : voices)
    {
        if (v.kind == Voice::Kind::Sample)     renderSampleVoice (v, l, r, num);
        else if (v.kind == Voice::Kind::Model) renderModelVoice (v, l, r, num);
    }
}

void PianoEngine::renderSampleVoice (Voice& v, float* outL, float* outR, int num)
{
    const auto* data = v.sample->data;
    const int frames = (int) v.sample->frames;
    double pos = v.pos;
    const double inc = v.inc;
    float env = v.env;
    const float envMul = v.envMul;
    float kill = v.kill;
    const float killStep = v.killing ? v.killStep : 0.0f;
    const float gl = v.gainL, gr = v.gainR;

    for (int i = 0; i < num; ++i)
    {
        const int ip = (int) pos;
        if (ip >= frames || kill <= 0.0f || env < silence)
        {
            v.kind = Voice::Kind::Off;
            break;
        }

        const float t = (float) (pos - ip);
        const int16_t* p = data + ip * 2;
        const int16_t* pm = ip > 0 ? p - 2 : p;

        const float sl = hermite (pm[0], p[0], p[2], p[4], t);
        const float sr = hermite (pm[1], p[1], p[3], p[5], t);

        const float g = env * kill;
        outL[i] += sl * gl * g;
        outR[i] += sr * gr * g;

        pos += inc;
        env *= envMul;
        kill -= killStep;
    }

    v.pos = pos;
    v.env = env;
    v.kill = kill;
}

//==============================================================================
// Modelled piano (fallback when no samples are installed)
//==============================================================================
void PianoEngine::startModelVoice (int key, int ownerKey, int velocity)
{
    auto* v = allocateVoice();
    if (v == nullptr)
        return;

    v->kind = Voice::Kind::Model;
    v->key = ownerKey;
    v->velocity = velocity;
    v->isNote = true;
    v->keyHeld = true;
    v->sustained = v->sostenuto = v->released = v->killing = false;
    v->group = v->offBy = 0;
    v->regionIndex = -1;
    v->sampleIndex = -1;
    v->sample = nullptr;
    v->releaseTime = key >= damperLimitKey ? 5.0f : 0.6f;
    v->env = 1.0f; v->envMul = 1.0f; v->kill = 1.0f; v->killStep = 0;
    v->gainL = v->gainR = 1.0f;
    v->startSample = sampleClock;

    auto& m = v->model;
    const float vel = juce::jlimit (1, 127, velocity) / 127.0f;
    const float f0 = 440.0f * std::exp2 ((key - 69 + params.tuneCents * 0.01f) / 12.0f);
    const float n = (float) (key - 21);
    const float B = 0.00008f * std::exp (n * 0.058f);                   // inharmonicity
    const float tau1 = 11.0f * std::exp (-n / 26.0f) + 0.6f;            // fundamental decay (s)
    const float slope = 1.9f - 1.1f * vel;                               // brighter when harder
    const float strike = 1.0f / 7.5f;                                    // hammer position
    const float nyquist = (float) sampleRate * 0.45f;

    m.rng = (uint32_t) (key * 2654435761u + (uint32_t) sampleClock);
    auto rnd = [&m]
    {
        m.rng = m.rng * 1664525u + 1013904223u;
        return (float) (m.rng >> 8) / 16777216.0f;
    };

    m.numOsc = 0;
    float norm = 0.0f;
    for (int k = 1; k <= maxPartials; ++k)
    {
        const float fk = f0 * k * std::sqrt (1.0f + B * k * k);
        if (fk > nyquist)
            break;

        float a = std::abs (std::sin (juce::MathConstants<float>::pi * k * strike)) / std::pow ((float) k, slope);
        if (key < 45) a *= 1.0f + 0.6f * std::exp (-(float) k / 6.0f) * (k > 1 ? 1.0f : 0.4f);
        const float tauK = tau1 / (1.0f + std::pow (fk / 1800.0f, 1.4f) * 3.0f + k * 0.03f);

        // two unison strings: slightly detuned -> beating and double decay
        const float detune = fk * (0.00025f + 0.0003f * rnd());
        const float freqs[2] = { fk - detune * 0.5f, fk + detune * 0.5f };
        const float taus[2]  = { tauK * 0.35f, tauK * 1.7f };
        const float amps[2]  = { a * 0.62f, a * 0.38f };

        for (int s = 0; s < 2; ++s)
        {
            const int i = m.numOsc++;
            const float w = juce::MathConstants<float>::twoPi * freqs[s] / (float) sampleRate;
            const float phase = rnd() * 0.3f;
            m.re[i] = std::cos (phase);
            m.im[i] = std::sin (phase);
            m.cr[i] = std::cos (w);
            m.ci[i] = std::sin (w);
            m.amp[i] = amps[s];
            m.decay[i] = std::exp (-1.0f / (taus[s] * (float) sampleRate));
        }
        norm += a;
    }

    const float level = 1.1f * (0.06f + 0.94f * vel * vel) / juce::jmax (0.3f, norm);
    for (int i = 0; i < m.numOsc; ++i)
        m.amp[i] *= level;

    // hammer thump: band-passed noise burst
    const float fc = juce::jlimit (200.0f, 5000.0f, f0 * 3.0f);
    const float bw = fc * 0.9f;
    const float rr = std::exp (-juce::MathConstants<float>::pi * bw / (float) sampleRate);
    m.bpA1 = -2.0f * rr * std::cos (juce::MathConstants<float>::twoPi * fc / (float) sampleRate);
    m.bpA2 = rr * rr;
    m.bpB0 = (1.0f - rr);
    m.bpZ1 = m.bpZ2 = 0.0f;
    m.noiseAmp = 0.25f * vel * vel * level / 1.1f;
    m.noiseEnv = 1.0f;
    m.noiseDecay = std::exp (-1.0f / (0.006f * (float) sampleRate));

    m.attack = 0.0f;
    m.attackStep = 1.0f / (0.0015f * (float) sampleRate);

    const float pan = juce::jlimit (-0.4f, 0.4f, (key - 64) / 64.0f * 0.4f);
    m.panL = std::sqrt (0.5f * (1.0f - pan)) * 1.414f;
    m.panR = std::sqrt (0.5f * (1.0f + pan)) * 1.414f;
}

void PianoEngine::renderModelVoice (Voice& v, float* outL, float* outR, int num)
{
    auto& m = v.model;
    float env = v.env, kill = v.kill;
    const float envMul = v.envMul, killStep = v.killing ? v.killStep : 0.0f;

    for (int i = 0; i < num; ++i)
    {
        float s = 0.0f;
        for (int k = 0; k < m.numOsc; ++k)
        {
            const float re = m.re[k] * m.cr[k] - m.im[k] * m.ci[k];
            const float im = m.re[k] * m.ci[k] + m.im[k] * m.cr[k];
            m.re[k] = re; m.im[k] = im;
            s += im * m.amp[k];
            m.amp[k] *= m.decay[k];
        }

        if (m.noiseEnv > 1.0e-4f)
        {
            m.rng = m.rng * 1664525u + 1013904223u;
            const float white = (float) (int32_t) m.rng * (1.0f / 2147483648.0f);
            const float y = m.bpB0 * white - m.bpA1 * m.bpZ1 - m.bpA2 * m.bpZ2;
            m.bpZ2 = m.bpZ1; m.bpZ1 = y;
            s += y * m.noiseAmp * m.noiseEnv;
            m.noiseEnv *= m.noiseDecay;
        }

        if (m.attack < 1.0f) { s *= m.attack; m.attack += m.attackStep; }

        const float g = env * kill;
        outL[i] += s * m.panL * g;
        outR[i] += s * m.panR * g;
        env *= envMul;
        kill -= killStep;
    }

    // keep oscillators on the unit circle and drop inaudible partials
    float total = 0.0f;
    for (int k = 0; k < m.numOsc; ++k)
    {
        const float mag = std::sqrt (m.re[k] * m.re[k] + m.im[k] * m.im[k]);
        if (mag > 0.0f) { m.re[k] /= mag; m.im[k] /= mag; }
        total += m.amp[k];
    }

    v.env = env;
    v.kill = kill;
    if (kill <= 0.0f || env * total < 2.0e-5f)
        v.kind = Voice::Kind::Off;
}

} // namespace gp
