#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>

namespace epinox::dsp
{
// Step-sequenced gate. `rate` is the number of steps per second, `steps` the pattern length.
// The pattern is deterministic (step 0 is always open), `smooth` sets how soft the gate edges are.
class ChopEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        onGain.reset (sr, 0.02);
        onGain.setCurrentAndTargetValue (1.0f);
        phase = 0.0f;
        step = 0;
        gate = 1.0f;
    }

    void process (float* l, float* r, int n, bool on, float rateHz, int steps, float smooth01, float mix01)
    {
        onGain.setTargetValue (on ? 1.0f : 0.0f);

        steps = juce::jlimit (1, 64, steps);
        const float stepLen = static_cast<float> (sr) / juce::jmax (0.1f, rateHz);
        const float ramp = juce::jmax (16.0f, smooth01 * 0.5f * stepLen);
        const float slew = 1.0f / ramp;

        if (step >= steps)
            step = 0;

        for (int i = 0; i < n; ++i)
        {
            phase += 1.0f;

            if (phase >= stepLen)
            {
                phase -= stepLen;
                step = (step + 1) % steps;
            }

            const float target = stepIsOpen (step) ? 1.0f : 0.0f;
            gate += juce::jlimit (-slew, slew, target - gate);

            const float m = mix01 * onGain.getNextValue();
            const float g = 1.0f - m + gate * m;
            l[i] *= g;
            r[i] *= g;
        }
    }

private:
    static bool stepIsOpen (int s)
    {
        if (s == 0)
            return true;

        std::uint32_t h = static_cast<std::uint32_t> (s) * 2654435761u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        return (h & 7u) < 5u;
    }

    double sr = 44100.0;
    float phase = 0.0f, gate = 1.0f;
    int step = 0;
    juce::SmoothedValue<float> onGain;
};
} // namespace epinox::dsp
