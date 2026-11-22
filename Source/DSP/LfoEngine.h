#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace epinox::dsp
{
struct LfoParams
{
    bool on = false;
    int shape = 0;          // 0 sine, 1 triangle, 2 square, 3 saw, 4 random (smoothed sample & hold)
    int sync = 0;           // 0 free (rateHz), otherwise an entry of LfoEngine::syncBeats()
    float rateHz = 1.0f;
    float bpm = 128.0f;
    float pitch = 0.0f;     // 0..1, 1 = +/- 2 semitones
    float formant = 0.0f;   // 0..1, 1 = +/- 4 semitones
    float filter = 0.0f;    // 0..1, 1 = low-pass sweeping 240 Hz .. 12 kHz
    float volume = 0.0f;    // 0..1, 1 = tremolo down to silence

    bool active() const noexcept { return on && (pitch > 0.0f || formant > 0.0f || filter > 0.0f || volume > 0.0f); }
};

// One LFO that moves four things: the pitch and the formant of the Transform stage, and a low-pass filter and the level
// after it. The LFO value is generated once per chunk so every destination sees the same shape.
class LfoEngine
{
public:
    static constexpr float maxPitchSt = 2.0f, maxFormantSt = 4.0f;

    // Cycle length in beats for every sync entry (index 0 = free running).
    static const std::vector<float>& syncBeats()
    {
        static const std::vector<float> beats { 0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
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

    // Fills out[0..n) with the bipolar LFO (-1..1) and moves the phase on.
    void generate (int n, const LfoParams& p, float* out)
    {
        const double inc = static_cast<double> (rateFor (p)) / sr;

        for (int i = 0; i < n; ++i)
        {
            float raw = 0.0f;
            float s = 0.0f;

            switch (p.shape)
            {
                case 0:  raw = std::sin (static_cast<float> (phase) * juce::MathConstants<float>::twoPi); s = raw; break;
                case 1:  raw = static_cast<float> (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase); s = raw; break;
                case 2:  raw = phase < 0.5 ? 1.0f : -1.0f; smoothed += slew * (raw - smoothed); s = smoothed; break;
                case 3:  raw = static_cast<float> (1.0 - 2.0 * phase); s = raw; break;
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
                cutoffSm.setTargetValue (12000.0f * std::pow (0.02f, p.filter * (1.0f - u)));

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
    float g = 0.1f, k = 1.2f, a1 = 0.5f, a2 = 0.05f, a3 = 0.005f;
    Svf svf[2];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSm;
};
} // namespace epinox::dsp
