#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace gp
{

//==============================================================================
/** 8-line feedback delay network reverb with input diffusion and gentle modulation.
    Smooth, dense and free of the metallic ring of simpler designs. */
class FdnReverb
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        const int maxLen = (int) (sr * 0.25) + 8;
        for (auto& d : lines) { d.assign ((size_t) maxLen, 0.0f); }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < numDiffusers; ++i)
                diffusers[(size_t) c][(size_t) i].buffer.assign ((size_t) (sr * 0.02) + 4, 0.0f);
        predelay.assign ((size_t) (sr * 0.1) + 4, 0.0f);
        reset();
        update (size, decay, tone, predelayMs, true);
    }

    void reset()
    {
        for (auto& d : lines) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& c : diffusers) for (auto& a : c) std::fill (a.buffer.begin(), a.buffer.end(), 0.0f);
        std::fill (predelay.begin(), predelay.end(), 0.0f);
        lp.fill (0.0f);
        writePos = 0; preWrite = 0;
    }

    /** size 0..1, decay seconds, tone 0 (dark) .. 1 (bright), predelay ms */
    void update (float newSize, float newDecay, float newTone, float newPredelayMs, bool force = false)
    {
        if (! force && newSize == size && newDecay == decay && newTone == tone && newPredelayMs == predelayMs)
            return;
        size = newSize; decay = newDecay; tone = newTone; predelayMs = newPredelayMs;

        static constexpr float baseMs[numLines] = { 31.3f, 37.9f, 41.7f, 46.1f, 52.3f, 58.9f, 66.7f, 73.1f };
        const float scale = 0.45f + 1.55f * size;
        for (int i = 0; i < numLines; ++i)
        {
            lengths[(size_t) i] = juce::jlimit (8.0f, (float) lines[0].size() - 8.0f,
                                                (float) (baseMs[i] * scale * 0.001 * sampleRate));
            const float seconds = lengths[(size_t) i] / (float) sampleRate;
            gains[(size_t) i] = std::pow (10.0f, -3.0f * seconds / juce::jmax (0.1f, decay));
        }

        // high-frequency damping inside the loop
        const float cutoff = 1500.0f + tone * tone * 14000.0f;
        dampCoef = std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sampleRate);

        static constexpr float diffMs[numDiffusers] = { 4.71f, 3.59f, 12.73f, 9.31f };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < numDiffusers; ++i)
            {
                auto& a = diffusers[(size_t) c][(size_t) i];
                a.length = juce::jlimit (2, (int) a.buffer.size() - 2,
                                         (int) (diffMs[i] * (1.0f + 0.13f * c) * (0.6f + 0.6f * size) * 0.001 * sampleRate));
            }

        preLen = juce::jlimit (1, (int) predelay.size() - 2, (int) (predelayMs * 0.001 * sampleRate));
    }

    /** Processes a stereo block: reads `inL/inR` (dry send), writes wet into outL/outR (replacing). */
    void process (const float* inL, const float* inR, float* outL, float* outR, int num)
    {
        const int len = (int) lines[0].size();
        const int preSize = (int) predelay.size();

        for (int n = 0; n < num; ++n)
        {
            // predelay (mono sum keeps the stereo image of the dry signal intact)
            float in = 0.5f * (inL[n] + inR[n]);
            predelay[(size_t) preWrite] = in;
            int rp = preWrite - preLen; if (rp < 0) rp += preSize;
            float x = predelay[(size_t) rp];
            if (++preWrite >= preSize) preWrite = 0;

            float xl = x, xr = x;
            for (int i = 0; i < numDiffusers; ++i)
            {
                xl = diffusers[0][(size_t) i].process (xl, 0.62f);
                xr = diffusers[1][(size_t) i].process (xr, 0.62f);
            }

            // slow modulation of two delay lines
            lfoPhase += lfoInc;
            if (lfoPhase > juce::MathConstants<float>::twoPi) lfoPhase -= juce::MathConstants<float>::twoPi;
            const float mod = std::sin (lfoPhase) * (float) sampleRate * 0.0009f;

            std::array<float, numLines> y;
            for (int i = 0; i < numLines; ++i)
            {
                float d = lengths[(size_t) i] + (i == 1 ? mod : i == 6 ? -mod : 0.0f);
                float rpos = (float) writePos - d;
                if (rpos < 0) rpos += (float) len;
                int i0 = (int) rpos;
                float fr = rpos - (float) i0;
                int i1 = i0 + 1; if (i1 >= len) i1 = 0;
                auto& line = lines[(size_t) i];
                float s = line[(size_t) i0] + fr * (line[(size_t) i1] - line[(size_t) i0]);
                // damping
                lp[(size_t) i] = s * (1.0f - dampCoef) + lp[(size_t) i] * dampCoef;
                y[(size_t) i] = lp[(size_t) i] * gains[(size_t) i];
            }

            // 8x8 Hadamard mixing (fast Walsh-Hadamard transform)
            std::array<float, numLines> h = y;
            for (int step = 1; step < numLines; step <<= 1)
                for (int i = 0; i < numLines; i += step << 1)
                    for (int j = i; j < i + step; ++j)
                    {
                        float a = h[(size_t) j], b = h[(size_t) (j + step)];
                        h[(size_t) j] = a + b; h[(size_t) (j + step)] = a - b;
                    }
            const float norm = 0.35355339f; // 1/sqrt(8)

            for (int i = 0; i < numLines; ++i)
            {
                float input = (i & 1) ? xr : xl;
                lines[(size_t) i][(size_t) writePos] = h[(size_t) i] * norm + input * 0.5f;
            }
            if (++writePos >= len) writePos = 0;

            outL[n] = (y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] + y[5])) * 0.5f;
            outR[n] = (y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] + y[6])) * 0.5f;
        }
    }

private:
    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;

    struct Allpass
    {
        std::vector<float> buffer;
        int length = 10, pos = 0;
        float process (float x, float g)
        {
            int rp = pos - length; if (rp < 0) rp += (int) buffer.size();
            float d = buffer[(size_t) rp];
            float v = x + g * d;
            buffer[(size_t) pos] = v;
            if (++pos >= (int) buffer.size()) pos = 0;
            return d - g * v;
        }
    };

    double sampleRate = 44100.0;
    std::array<std::vector<float>, numLines> lines;
    std::array<float, numLines> lengths {}, gains {}, lp {};
    std::array<std::array<Allpass, numDiffusers>, 2> diffusers;
    std::vector<float> predelay;
    int writePos = 0, preWrite = 0, preLen = 1;
    float dampCoef = 0.5f;
    float lfoPhase = 0.0f, lfoInc = 0.00012f;
    float size = 0.5f, decay = 2.0f, tone = 0.5f, predelayMs = 10.0f;
};

//==============================================================================
/** Bass / mid / treble EQ voiced for piano. */
class PianoEq
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& b : bands) for (auto& f : b) f.reset();
        update (0, 0, 0, true);
    }

    void update (float bassDb, float midDb, float trebleDb, bool force = false)
    {
        if (! force && bassDb == bass && midDb == mid && trebleDb == treble)
            return;
        bass = bassDb; mid = midDb; treble = trebleDb;
        using C = juce::dsp::IIR::Coefficients<float>;
        auto c0 = C::makeLowShelf (sampleRate, 140.0, 0.6, juce::Decibels::decibelsToGain (bass));
        auto c1 = C::makePeakFilter (sampleRate, 900.0, 0.65, juce::Decibels::decibelsToGain (mid));
        auto c2 = C::makeHighShelf (sampleRate, 5500.0, 0.55, juce::Decibels::decibelsToGain (treble));
        for (int ch = 0; ch < 2; ++ch)
        {
            *bands[0][(size_t) ch].coefficients = *c0;
            *bands[1][(size_t) ch].coefficients = *c1;
            *bands[2][(size_t) ch].coefficients = *c2;
        }
        active = std::abs (bass) > 0.01f || std::abs (mid) > 0.01f || std::abs (treble) > 0.01f;
    }

    void process (float* l, float* r, int num)
    {
        if (! active) return;
        for (int b = 0; b < 3; ++b)
        {
            if (b == 0 && std::abs (bass) < 0.01f) continue;
            if (b == 1 && std::abs (mid) < 0.01f) continue;
            if (b == 2 && std::abs (treble) < 0.01f) continue;
            auto& fl = bands[(size_t) b][0];
            auto& fr = bands[(size_t) b][1];
            for (int i = 0; i < num; ++i)
            {
                l[i] = fl.processSample (l[i]);
                r[i] = fr.processSample (r[i]);
            }
        }
    }

private:
    double sampleRate = 44100.0;
    float bass = 0, mid = 0, treble = 0;
    bool active = false;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, 3> bands {{
        { juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0)),
          juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0)) },
        { juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0)),
          juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0)) },
        { juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0)),
          juce::dsp::IIR::Filter<float> (new juce::dsp::IIR::Coefficients<float> (1, 0, 1, 0)) } }};
};

//==============================================================================
/** One-knob stereo-linked "glue" compressor with soft knee and auto make-up. */
class GlueCompressor
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        envDb = -120.0f;
        attack = std::exp (-1.0f / (0.008f * (float) sr));
        release = std::exp (-1.0f / (0.150f * (float) sr));
    }

    void process (float* l, float* r, int num, float amount)
    {
        gainReductionDb = 0.0f;
        if (amount <= 0.001f) { envDb = -120.0f; return; }

        const float threshold = -8.0f - 22.0f * amount;
        const float ratio = 1.5f + 4.5f * amount;
        const float knee = 8.0f;
        const float makeup = juce::Decibels::decibelsToGain (-threshold * (1.0f - 1.0f / ratio) * 0.45f);

        float maxGr = 0.0f;
        for (int i = 0; i < num; ++i)
        {
            const float peak = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            const float inDb = peak > 1.0e-6f ? 20.0f * std::log10 (peak) : -120.0f;
            const float coef = inDb > envDb ? attack : release;
            envDb = inDb + coef * (envDb - inDb);

            const float over = envDb - threshold;
            float grDb;
            if (over <= -knee * 0.5f)      grDb = 0.0f;
            else if (over >= knee * 0.5f)  grDb = over * (1.0f - 1.0f / ratio);
            else { const float x = over + knee * 0.5f; grDb = (1.0f - 1.0f / ratio) * x * x / (2.0f * knee); }

            maxGr = juce::jmax (maxGr, grDb);
            const float g = juce::Decibels::decibelsToGain (-grDb) * makeup;
            l[i] *= g; r[i] *= g;
        }
        gainReductionDb = maxGr;
    }

    float getGainReductionDb() const noexcept { return gainReductionDb; }

private:
    double sampleRate = 44100.0;
    float envDb = -120.0f, attack = 0.0f, release = 0.0f, gainReductionDb = 0.0f;
};

//==============================================================================
/** Zero-latency output protection: transparent until the signal approaches 0 dBFS,
    then a fast peak limiter followed by a gentle soft clip. */
class SafetyLimiter
{
public:
    void prepare (double sr)
    {
        gain = 1.0f;
        release = std::exp (-1.0f / (0.12f * (float) sr));
    }

    void process (float* l, float* r, int num)
    {
        constexpr float ceiling = 0.944f; // -0.5 dBFS
        for (int i = 0; i < num; ++i)
        {
            const float peak = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            gain = target < gain ? target : target + release * (gain - target);
            l[i] = softClip (l[i] * gain);
            r[i] = softClip (r[i] * gain);
        }
    }

private:
    static float softClip (float x)
    {
        const float a = std::abs (x);
        if (a < 0.9f) return x;
        const float over = a - 0.9f;
        return std::copysign (0.9f + 0.09f * std::tanh (over / 0.09f), x);
    }

    float gain = 1.0f, release = 0.999f;
};

} // namespace gp
