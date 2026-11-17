#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace epinox::dsp
{
// Stereo space: pre-delay -> 8 damped combs + 4 allpass diffusers per side -> ducked by the input level.
//   size    : decay time of the room
//   density : amount of allpass diffusion (smoother, denser tail)
//   ducking : how much the input signal pushes the reverb tail down
//   mix     : level of the reverb added to the signal
class SpatialEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double scale = sr / 44100.0;

        static constexpr int combLengths[numCombs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr int allpassLengths[numAllpass] = { 556, 441, 341, 225 };
        const int spread = static_cast<int> (23.0 * scale);

        for (int i = 0; i < numCombs; ++i)
        {
            const int len = static_cast<int> (combLengths[i] * scale);
            combL[i].init (len);
            combR[i].init (len + spread);
        }

        for (int i = 0; i < numAllpass; ++i)
        {
            const int len = static_cast<int> (allpassLengths[i] * scale);
            apL[i].init (len);
            apR[i].init (len + spread);
        }

        preLen = juce::jmax (1, static_cast<int> (sr * 0.015));
        preBuf.assign (static_cast<size_t> (preLen), 0.0f);
        prePos = 0;

        onGain.reset (sr, 0.02);
        mixSm.reset (sr, 0.02);
        duckSm.reset (sr, 0.02);
        onGain.setCurrentAndTargetValue (1.0f);
        mixSm.setCurrentAndTargetValue (0.0f);
        duckSm.setCurrentAndTargetValue (0.0f);

        envelope = 0.0f;
        attack = 1.0f - std::exp (-1.0f / (0.005f * static_cast<float> (sr)));
        release = std::exp (-1.0f / (0.15f * static_cast<float> (sr)));
    }

    void process (float* l, float* r, int n, bool on, float size01, float density01, float ducking01, float mix01)
    {
        onGain.setTargetValue (on ? 1.0f : 0.0f);
        mixSm.setTargetValue (mix01);
        duckSm.setTargetValue (ducking01);

        const float fb = juce::jlimit (0.0f, 0.97f, 0.70f + 0.27f * size01);
        const float apGain = 0.30f + 0.40f * density01;
        constexpr float damp = 0.25f;

        for (int i = 0; i < n; ++i)
        {
            const float mono = 0.5f * (l[i] + r[i]);

            const float level = std::abs (mono);
            envelope = level > envelope ? envelope + (level - envelope) * attack : envelope * release;
            const float duck = 1.0f - duckSm.getNextValue() * juce::jlimit (0.0f, 1.0f, envelope * 4.0f);

            const float delayed = preBuf[static_cast<size_t> (prePos)];
            preBuf[static_cast<size_t> (prePos)] = mono * 0.015f;

            if (++prePos >= preLen)
                prePos = 0;

            float wl = 0.0f, wr = 0.0f;

            for (int c = 0; c < numCombs; ++c)
            {
                wl += combL[c].process (delayed, fb, damp);
                wr += combR[c].process (delayed, fb, damp);
            }

            for (int a = 0; a < numAllpass; ++a)
            {
                wl = apL[a].process (wl, apGain);
                wr = apR[a].process (wr, apGain);
            }

            const float amount = mixSm.getNextValue() * onGain.getNextValue() * duck * 3.0f;
            l[i] += wl * amount;
            r[i] += wr * amount;
        }
    }

private:
    static constexpr int numCombs = 8, numAllpass = 4;

    struct Comb
    {
        void init (int len) { buf.assign (static_cast<size_t> (juce::jmax (1, len)), 0.0f); idx = 0; store = 0.0f; }

        float process (float in, float fb, float damp)
        {
            const float out = buf[static_cast<size_t> (idx)];
            store = out * (1.0f - damp) + store * damp;
            buf[static_cast<size_t> (idx)] = in + store * fb;

            if (++idx >= static_cast<int> (buf.size()))
                idx = 0;

            return out;
        }

        std::vector<float> buf;
        int idx = 0;
        float store = 0.0f;
    };

    struct Allpass
    {
        void init (int len) { buf.assign (static_cast<size_t> (juce::jmax (1, len)), 0.0f); idx = 0; }

        float process (float in, float g)
        {
            const float b = buf[static_cast<size_t> (idx)];
            const float out = -in + b;
            buf[static_cast<size_t> (idx)] = in + b * g;

            if (++idx >= static_cast<int> (buf.size()))
                idx = 0;

            return out;
        }

        std::vector<float> buf;
        int idx = 0;
    };

    double sr = 44100.0;
    Comb combL[numCombs], combR[numCombs];
    Allpass apL[numAllpass], apR[numAllpass];
    std::vector<float> preBuf;
    int preLen = 1, prePos = 0;
    float envelope = 0.0f, attack = 0.1f, release = 0.999f;
    juce::SmoothedValue<float> onGain, mixSm, duckSm;
};
} // namespace epinox::dsp
