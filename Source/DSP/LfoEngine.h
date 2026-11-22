#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace epinox::dsp
{
struct LfoParams
{
    bool on = false;
    int shape = 0;          // 0 sine, 1 triangle, 2 square, 3 saw down, 4 random smooth, 5 saw up, 6 random step
    int sync = 0;           // 0 free (rateHz), otherwise an entry of LfoEngine::syncBeats()
    float rateHz = 1.0f;
    float phase = 0.0f;     // start phase offset, 0..1 of a cycle
    float bpm = 128.0f;
    bool hostLocked = false; // synced rates follow the host position (ppq) while the transport runs
    double hostPpq = 0.0;    // host position, in quarter notes, at the start of the chunk
    float pitch = 0.0f;     // 0..1, 1 = +/- 2 semitones
    float formant = 0.0f;   // 0..1, 1 = +/- 4 semitones
    float filter = 0.0f;    // 0..1, 1 = low-pass sweeping 150 Hz .. 12 kHz
    float volume = 0.0f;    // 0..1, 1 = tremolo down to silence

    bool active() const noexcept { return on && (pitch > 0.0f || formant > 0.0f || filter > 0.0f || volume > 0.0f); }
};

// One LFO that moves four things: the pitch and the formant of the Transform stage, and a low-pass filter and the level
// after it. The LFO value is generated once per chunk so every destination sees the same shape.
class LfoEngine
{
public:
    static constexpr float maxPitchSt = 2.0f, maxFormantSt = 6.0f;

    // Cycle length in beats for every sync entry (index 0 = free running).
    static const std::vector<float>& syncBeats()
    {
        // Free, 8/4/2/1 bars, then half, quarter, eighth, sixteenth and thirty-second notes, each straight, dotted and triplet.
        static const std::vector<float> beats { 0.0f, 32.0f, 16.0f, 8.0f, 4.0f,
                                                2.0f, 3.0f, 4.0f / 3.0f,
                                                1.0f, 1.5f, 2.0f / 3.0f,
                                                0.5f, 0.75f, 1.0f / 3.0f,
                                                0.25f, 0.375f, 1.0f / 6.0f,
                                                0.125f };
        return beats;
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        phase = 0.0;
        held = 0.0f;
        smoothed = 0.0f;
        slew = 1.0f - std::exp (-1.0f / (0.006f * static_cast<float> (sr)));
        slowSlew = 1.0f - std::exp (-1.0f / (0.04f * static_cast<float> (sr)));
        for (auto& c : svf) c = {};
        cutoffSm.reset (sr, 0.003);
        cutoffSm.setCurrentAndTargetValue (12000.0f);
    }

    static float rateFor (const LfoParams& p)
    {
        const auto& b = syncBeats();
        const int idx = juce::jlimit (0, static_cast<int> (b.size()) - 1, p.sync);

        if (idx == 0)
            return juce::jlimit (0.02f, 30.0f, p.rateHz);

        return juce::jmax (0.001f, p.bpm) / 60.0f / b[static_cast<size_t> (idx)];
    }

    // Fills out[0..n) with the bipolar LFO (-1..1) and moves the phase on. Synced rates lock to the host position while
    // the transport runs, so the LFO always starts a bar on the same point of its cycle.
    void generate (int n, const LfoParams& p, float* out)
    {
        const double inc = static_cast<double> (rateFor (p)) / sr;
        const auto& beats = syncBeats();
        const int idx = juce::jlimit (0, static_cast<int> (beats.size()) - 1, p.sync);

        if (p.hostLocked && idx > 0)
        {
            const double cycles = p.hostPpq / static_cast<double> (beats[static_cast<size_t> (idx)]);
            phase = cycles - std::floor (cycles);
        }

        for (int i = 0; i < n; ++i)
        {
            double ph = phase + static_cast<double> (p.phase);
            ph -= std::floor (ph);
            float s = 0.0f;

            switch (p.shape)
            {
                case 0:  s = std::sin (static_cast<float> (ph) * juce::MathConstants<float>::twoPi); break;
                case 1:  s = static_cast<float> (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph); break;
                case 2:  smoothed += slew * ((ph < 0.5 ? 1.0f : -1.0f) - smoothed); s = smoothed; break;
                case 3:  s = static_cast<float> (1.0 - 2.0 * ph); break;
                case 5:  s = static_cast<float> (2.0 * ph - 1.0); break;
                case 6:  smoothed += slew * (held - smoothed); s = smoothed; break;
                default: smoothed += slowSlew * (held - smoothed); s = smoothed; break;
            }

            out[i] = s;
            phase += inc;

            if (phase >= 1.0)
            {
                phase -= 1.0;
                rnd = rnd * 1664525u + 1013904223u;
                held = static_cast<float> (rnd >> 8) / 8388608.0f - 1.0f;   // -1..1
            }
        }
    }

    // Low-pass sweep and tremolo, driven by the LFO values that generate() just produced.
    void applyAudio (float* l, float* r, int n, const LfoParams& p, const float* lfo)
    {
        if (p.filter <= 0.0f && p.volume <= 0.0f)
            return;

        const float twoPiOverSr = juce::MathConstants<float>::pi / static_cast<float> (sr);

        for (int i = 0; i < n; ++i)
        {
            const float u = 0.5f + 0.5f * lfo[i];          // 0..1

            if (p.filter > 0.0f)
            {
                cutoffSm.setTargetValue (12000.0f * std::pow (0.0125f, p.filter * (1.0f - u)));

                if ((i & 7) == 0 || i == 0)
                {
                    const float fc = juce::jmin (cutoffSm.getNextValue(), 0.45f * static_cast<float> (sr));
                    g = std::tan (twoPiOverSr * fc);
                    a1 = 1.0f / (1.0f + g * (g + k));
                    a2 = g * a1;
                    a3 = g * a2;
                }

                l[i] = svfLow (svf[0], l[i]);
                r[i] = svfLow (svf[1], r[i]);
            }

            if (p.volume > 0.0f)
            {
                const float gain = 1.0f - p.volume * (1.0f - u);
                l[i] *= gain;
                r[i] *= gain;
            }
        }
    }

private:
    struct Svf { float ic1 = 0.0f, ic2 = 0.0f; };

    float svfLow (Svf& s, float v0)
    {
        const float v3 = v0 - s.ic2;
        const float v1 = a1 * s.ic1 + a2 * v3;
        const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;
        return v2;
    }

    double sr = 44100.0, phase = 0.0;
    float held = 0.0f, smoothed = 0.0f, slew = 0.01f, slowSlew = 0.001f;
    std::uint32_t rnd = 12345u;
    float g = 0.1f, k = 0.8f, a1 = 0.5f, a2 = 0.05f, a3 = 0.005f;
    Svf svf[2];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSm;
};
} // namespace epinox::dsp
