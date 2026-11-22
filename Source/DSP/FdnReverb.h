#pragma once

#include <array>
#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>

namespace epinox::dsp
{
// Feedback delay network reverb, used for both the Effects reverb and the Spatial room.
//
// Why not the classic comb-filter reverb: a few short parallel combs repeat every 25 to 40 ms, so the tail rings at those
// pitches like a metal tube, and at long decay times the ringing is very audible on sustained vowels. Here eight longer,
// mutually prime delay lines feed each other through an orthogonal (Hadamard) matrix, so every echo is spread over all lines
// and the tail is smooth. Each line has a slowly modulated length (no fixed resonances), a damping filter (highs decay
// faster than lows, as in a real room) and the input is diffused and high-passed so the low end does not build up.
class FdnReverb
{
public:
    static constexpr int lines = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static constexpr double ms[lines] = { 29.7, 37.1, 41.1, 43.7, 53.3, 61.7, 67.9, 79.3 };
        static constexpr double lfoHz[lines] = { 0.11, 0.15, 0.19, 0.23, 0.29, 0.31, 0.37, 0.41 };

        for (int i = 0; i < lines; ++i)
        {
            baseLen[i] = ms[i] * 0.001 * sr;
            const int cap = static_cast<int> (baseLen[i] + modDepth * sr + 8.0);
            buf[i].assign (static_cast<size_t> (cap), 0.0f);
            pos[i] = 0;
            damp[i] = 0.0f;
            phase[i] = juce::MathConstants<double>::twoPi * i / lines;
            phaseInc[i] = juce::MathConstants<double>::twoPi * lfoHz[i] / sr;
        }

        static constexpr int apLen[4] = { 142, 107, 379, 277 };

        for (int a = 0; a < 4; ++a)
        {
            ap[a].assign (static_cast<size_t> (juce::jmax (1, static_cast<int> (apLen[a] * sr / 44100.0))), 0.0f);
            apPos[a] = 0;
        }

        hpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 240.0f / static_cast<float> (sr));
        loopHpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 140.0f / static_cast<float> (sr));
        hpState = hpState2 = 0.0f;
        lowState.fill (0.0f);
        setRt60 (2.0f);
        setDensity (0.5f);
        setDamping (0.35f);
    }

    void setRt60 (float seconds) { rt60 = juce::jlimit (0.2f, 8.0f, seconds); updateGains(); }
    void setDensity (float d01) { apGain = 0.35f + 0.35f * juce::jlimit (0.0f, 1.0f, d01); }
    void setDamping (float d) { dampCoef = juce::jlimit (0.0f, 0.9f, d); }

    // Mono in, stereo wet out (not including the dry signal).
    void process (float in, float& outL, float& outR)
    {
        // Keep the low end out of the tail.
        hpState += hpCoef * (in - hpState);
        hpState2 += hpCoef * ((in - hpState) - hpState2);
        float x = (in - hpState) - hpState2;

        for (int a = 0; a < 4; ++a)
        {
            auto& b = ap[a];
            const float d = b[static_cast<size_t> (apPos[a])];
            const float v = x - apGain * d;
            b[static_cast<size_t> (apPos[a])] = v;
            x = d + apGain * v;

            if (++apPos[a] >= static_cast<int> (b.size()))
                apPos[a] = 0;
        }

        std::array<float, lines> y {};

        for (int i = 0; i < lines; ++i)
        {
            auto& b = buf[i];
            const double len = baseLen[i] + modDepth * sr * std::sin (phase[i]);
            phase[i] += phaseInc[i];

            if (phase[i] > juce::MathConstants<double>::twoPi)
                phase[i] -= juce::MathConstants<double>::twoPi;

            double rp = static_cast<double> (pos[i]) - len;
            const int size = static_cast<int> (b.size());

            while (rp < 0.0)
                rp += size;

            const int i0 = static_cast<int> (rp);
            const int i1 = (i0 + 1) % size;
            const float fr = static_cast<float> (rp - i0);
            const float s = b[static_cast<size_t> (i0)] * (1.0f - fr) + b[static_cast<size_t> (i1)] * fr;

            damp[i] += (1.0f - dampCoef) * (s - damp[i]);   // highs decay faster than lows
            lowState[static_cast<size_t> (i)] += loopHpCoef * (damp[i] - lowState[static_cast<size_t> (i)]);   // lows are not recirculated: no boom
            y[static_cast<size_t> (i)] = (damp[i] - lowState[static_cast<size_t> (i)]) * gain[i];
        }

        // Fast Walsh-Hadamard transform (orthogonal, scaled by 1/sqrt(8)): mixes every line into every other.
        for (int h = 1; h < lines; h <<= 1)
            for (int i = 0; i < lines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = y[static_cast<size_t> (j)], c = y[static_cast<size_t> (j + h)];
                    y[static_cast<size_t> (j)] = a + c;
                    y[static_cast<size_t> (j + h)] = a - c;
                }

        constexpr float norm = 0.35355339f;
        float l = 0.0f, r = 0.0f;

        for (int i = 0; i < lines; ++i)
        {
            auto& b = buf[i];
            b[static_cast<size_t> (pos[i])] = x * 0.5f + y[static_cast<size_t> (i)] * norm;

            if (++pos[i] >= static_cast<int> (b.size()))
                pos[i] = 0;

            // Output taps straight from the lines, alternating sign so left and right differ.
            const float t = y[static_cast<size_t> (i)];
            (i & 1 ? r : l) += (i & 2 ? -t : t);
        }

        outL = l * 0.5f;
        outR = r * 0.5f;
    }

private:
    void updateGains()
    {
        for (int i = 0; i < lines; ++i)
            gain[i] = std::pow (10.0f, -3.0f * static_cast<float> (baseLen[i] / sr) / rt60);
    }

    static constexpr double modDepth = 0.0007;   // seconds of delay wobble

    double sr = 44100.0, baseLen[lines] {}, phase[lines] {}, phaseInc[lines] {};
    std::vector<float> buf[lines], ap[4];
    int pos[lines] {}, apPos[4] {};
    float damp[lines] {}, gain[lines] {};
    float rt60 = 2.0f, apGain = 0.5f, dampCoef = 0.35f, hpCoef = 0.02f, loopHpCoef = 0.02f, hpState = 0.0f, hpState2 = 0.0f;
    std::array<float, lines> lowState {};
};
} // namespace epinox::dsp
