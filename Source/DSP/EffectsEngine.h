#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include "FdnReverb.h"

namespace epinox::dsp
{
// Distortion -> tempo-synced stereo delay -> reverb -> stereo width.
class EffectsEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        delayLen = static_cast<int> (sr * 2.0) + 8;
        delayL.assign (static_cast<size_t> (delayLen), 0.0f);
        delayR.assign (static_cast<size_t> (delayLen), 0.0f);
        writePos = 0;
        lpL = lpR = 0.0f;

        room.prepare (sr);
        room.setRt60 (1.6f);
        room.setDensity (0.6f);
        room.setDamping (0.4f);

        for (auto* s : { &onGain, &distort, &delayMix, &width })
            s->reset (sr, 0.02);

        delayTime.reset (sr, 0.15);
        onGain.setCurrentAndTargetValue (1.0f);
        distort.setCurrentAndTargetValue (0.0f);
        delayMix.setCurrentAndTargetValue (0.0f);
        width.setCurrentAndTargetValue (1.0f);
        delayTime.setCurrentAndTargetValue (static_cast<float> (sr * 0.35));
    }

    // reverb01, delay01, distort01 in 0..1, width in 0..2, bpm in beats per minute.
    void process (float* l, float* r, int n, bool on, float reverb01, float delay01, float distort01,
                  float width02, float bpm)
    {
        onGain.setTargetValue (on ? 1.0f : 0.0f);
        distort.setTargetValue (distort01);
        delayMix.setTargetValue (delay01);
        width.setTargetValue (width02);

        const float seconds = juce::jlimit (0.02f, 1.9f, 60.0f / juce::jmax (20.0f, bpm) * 0.75f); // dotted eighth
        delayTime.setTargetValue (seconds * static_cast<float> (sr));

        // Distortion and delay
        for (int i = 0; i < n; ++i)
        {
            const float g = onGain.getNextValue();
            const float a = distort.getNextValue() * g;

            if (a > 0.0001f)
            {
                const float drive = 1.0f + a * 25.0f;
                const float norm = 1.0f / std::tanh (drive);
                l[i] = l[i] * (1.0f - a) + std::tanh (drive * l[i]) * norm * a;
                r[i] = r[i] * (1.0f - a) + std::tanh (drive * r[i]) * norm * a;
            }

            const float dm = delayMix.getNextValue() * g;
            const float d = delayTime.getNextValue();

            float rp = static_cast<float> (writePos) - d;

            if (rp < 0.0f)
                rp += static_cast<float> (delayLen);

            const int i0 = static_cast<int> (rp);
            const int i1 = (i0 + 1) % delayLen;
            const float frac = rp - static_cast<float> (i0);

            const float dl = delayL[static_cast<size_t> (i0)] * (1.0f - frac) + delayL[static_cast<size_t> (i1)] * frac;
            const float dr = delayR[static_cast<size_t> (i0)] * (1.0f - frac) + delayR[static_cast<size_t> (i1)] * frac;

            // Feedback path is low-passed; the two channels feed each other for a ping-pong feel.
            lpL += 0.45f * (dr - lpL);
            lpR += 0.45f * (dl - lpR);
            delayL[static_cast<size_t> (writePos)] = l[i] + lpL * feedback;
            delayR[static_cast<size_t> (writePos)] = r[i] + lpR * feedback;

            if (++writePos >= delayLen)
                writePos = 0;

            l[i] += dl * dm * 0.7f;
            r[i] += dr * dm * 0.7f;
        }

        // Reverb: the same network as the Spatial room, shorter and darker.
        const float wet = reverb01 * onGain.getCurrentValue() * reverbGain;

        for (int i = 0; i < n; ++i)
        {
            float wl = 0.0f, wr = 0.0f;
            room.process (0.5f * (l[i] + r[i]), wl, wr);
            l[i] += wl * wet;
            r[i] += wr * wet;
        }

        // Stereo width (mid/side)
        for (int i = 0; i < n; ++i)
        {
            const float w = width.getNextValue();
            const float m = 0.5f * (l[i] + r[i]);
            const float s = 0.5f * (l[i] - r[i]) * w;
            l[i] = m + s;
            r[i] = m - s;
        }
    }

private:
    static constexpr float feedback = 0.38f;
    static constexpr float reverbGain = 0.67f;

    double sr = 44100.0;
    int delayLen = 88200, writePos = 0;
    float lpL = 0.0f, lpR = 0.0f;
    std::vector<float> delayL, delayR;
    FdnReverb room;
    juce::SmoothedValue<float> onGain, distort, delayMix, width;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delayTime;
};
} // namespace epinox::dsp
