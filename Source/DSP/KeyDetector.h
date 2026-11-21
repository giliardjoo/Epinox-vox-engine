#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>

namespace epinox::dsp
{
// Finds the musical key of a stretch of audio: a pitch-class profile (how much energy each of the 12 notes carries) is
// compared with the Krumhansl-Kessler major and minor key profiles. Runs on the message thread, never in the audio callback.
class KeyDetector
{
public:
    struct Result
    {
        bool valid = false;
        int tonic = 0;          // 0 = C ... 11 = B
        bool minor = false;
        float confidence = 0.0f;   // 0..1, how clearly the best key beats the runner-up
    };

    static Result detect (const std::vector<float>& mono, double sampleRate)
    {
        Result res;
        const int factor = std::max (1, static_cast<int> (std::lround (sampleRate / 11025.0)));
        const double sr = sampleRate / factor;

        std::vector<float> x;
        x.reserve (mono.size() / static_cast<size_t> (factor) + 1);

        for (size_t i = 0; i + static_cast<size_t> (factor) <= mono.size(); i += static_cast<size_t> (factor))
        {
            double a = 0.0;

            for (int k = 0; k < factor; ++k)
                a += mono[i + static_cast<size_t> (k)];

            x.push_back (static_cast<float> (a / factor));
        }

        const size_t chunk = 8192;

        if (x.size() < chunk)
            return res;

        double energy = 0.0;

        for (auto v : x)
            energy += static_cast<double> (v) * v;

        if (std::sqrt (energy / static_cast<double> (x.size())) < 1.0e-4)
            return res;   // silence

        std::array<double, 12> chroma {};
        std::vector<float> win (chunk);

        for (size_t i = 0; i < chunk; ++i)
            win[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (chunk - 1));

        for (size_t start = 0; start + chunk <= x.size(); start += chunk / 2)
            for (int midi = 36; midi < 84; ++midi)   // C2 to B5: where chords and bass lines live
            {
                const double f = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
                const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = 2.0 * std::cos (w);
                double s1 = 0.0, s2 = 0.0;

                for (size_t i = 0; i < chunk; ++i)
                {
                    const double s = static_cast<double> (x[start + i]) * win[i] + c * s1 - s2;
                    s2 = s1;
                    s1 = s;
                }

                const double mag = std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
                chroma[static_cast<size_t> (midi % 12)] += std::sqrt (mag);   // compressed, so one loud note does not decide alone
            }

        static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minorP[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        auto corr = [&] (const double* prof, int tonic)
        {
            double mc = 0.0, mp = 0.0;

            for (int i = 0; i < 12; ++i) { mc += chroma[static_cast<size_t> (i)]; mp += prof[i]; }

            mc /= 12.0; mp /= 12.0;
            double num = 0.0, dc = 0.0, dp = 0.0;

            for (int i = 0; i < 12; ++i)
            {
                const double a = chroma[static_cast<size_t> ((i + tonic) % 12)] - mc, b = prof[i] - mp;
                num += a * b; dc += a * a; dp += b * b;
            }

            return num / std::sqrt (std::max (1.0e-12, dc * dp));
        };

        double best = -2.0, second = -2.0;

        for (int t = 0; t < 12; ++t)
            for (int m = 0; m < 2; ++m)
            {
                const double c = corr (m == 0 ? major : minorP, t);

                if (c > best)
                {
                    second = best;
                    best = c;
                    res.tonic = t;
                    res.minor = m == 1;
                }
                else if (c > second)
                    second = c;
            }

        res.valid = true;
        res.confidence = static_cast<float> (juce::jlimit (0.0, 1.0, (best - second) * 6.0 + 0.2 * std::max (0.0, best)));
        return res;
    }
};
} // namespace epinox::dsp
