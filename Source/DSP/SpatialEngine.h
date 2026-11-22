#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include "FdnReverb.h"

namespace epinox::dsp
{
// Stereo space: pre-delay -> feedback delay network room (see FdnReverb.h) -> ducked by the input level.
//   size    : decay time of the room (0.5 s to 4.5 s)
//   density : amount of allpass diffusion (smoother, denser tail)
//   ducking : how much the input signal pushes the reverb tail down
//   mix     : level of the reverb added to the signal
class SpatialEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        room.prepare (sr);

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

        room.setRt60 (0.6f + 4.3f * std::pow (juce::jlimit (0.0f, 1.0f, size01), 2.2f));
        room.setDensity (density01);
        room.setDamping (0.3f);

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
            room.process (delayed, wl, wr);
            wl *= roomGain; wr *= roomGain;

            const float amount = mixSm.getNextValue() * onGain.getNextValue() * duck * 3.0f;
            l[i] += wl * amount;
            r[i] += wr * amount;
        }
    }

private:
    double sr = 44100.0;
    FdnReverb room;
    static constexpr float roomGain = 22.2f;   // matches the loudness of the old comb room
    std::vector<float> preBuf;
    int preLen = 1, prePos = 0;
    float envelope = 0.0f, attack = 0.1f, release = 0.999f;
    juce::SmoothedValue<float> onGain, mixSm, duckSm;
};
} // namespace epinox::dsp
