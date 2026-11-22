#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>
#include "SpectralVoice.h"

namespace epinox::dsp
{
// Fixed-length delay used to keep dry and processed paths time aligned.
class FixedDelay
{
public:
    void prepare (int lengthSamples)
    {
        buf.assign (static_cast<size_t> (juce::jmax (1, lengthSamples)), 0.0f);
        pos = 0;
    }

    float process (float x)
    {
        const float y = buf[static_cast<size_t> (pos)];
        buf[static_cast<size_t> (pos)] = x;

        if (++pos >= static_cast<int> (buf.size()))
            pos = 0;

        return y;
    }

private:
    std::vector<float> buf;
    int pos = 0;
};

// Small granular cloud: overlapping 60 ms grains read from the last ~0.3 s of the input.
class Granulator
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = static_cast<int> (sr);
        grainLen = static_cast<int> (sr * 0.06);
        bufL.assign (static_cast<size_t> (size), 0.0f);
        bufR.assign (static_cast<size_t> (size), 0.0f);
        write = 0;
        countdown = 0.0f;

        for (auto& g : grains)
            g.active = false;
    }

    void process (float& l, float& r, float amount)
    {
        bufL[static_cast<size_t> (write)] = l;
        bufR[static_cast<size_t> (write)] = r;

        if (amount > 0.001f)
        {
            countdown -= 1.0f;

            if (countdown <= 0.0f)
            {
                spawn();
                const float grainsPerSecond = 15.0f + 60.0f * amount;
                countdown = static_cast<float> (sr) / grainsPerSecond * (0.7f + 0.6f * rng.nextFloat());
            }
        }

        float gl = 0.0f, gr = 0.0f;

        for (auto& g : grains)
        {
            if (! g.active)
                continue;

            const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                     * static_cast<float> (g.age) / static_cast<float> (g.length));
            const size_t idx = static_cast<size_t> ((g.start + g.age) % size);
            gl += bufL[idx] * w;
            gr += bufR[idx] * w;

            if (++g.age >= g.length)
                g.active = false;
        }

        write = (write + 1) % size;

        const float overlapEstimate = static_cast<float> (grainLen) / static_cast<float> (sr) * (15.0f + 60.0f * amount);
        const float norm = 1.0f / std::sqrt (juce::jmax (1.0f, overlapEstimate));

        l = l * (1.0f - amount) + gl * norm * amount;
        r = r * (1.0f - amount) + gr * norm * amount;
    }

private:
    struct Grain
    {
        bool active = false;
        int start = 0, age = 0, length = 0;
    };

    void spawn()
    {
        for (auto& g : grains)
        {
            if (g.active)
                continue;

            const int offset = grainLen + static_cast<int> (rng.nextFloat() * 0.25f * static_cast<float> (sr));
            g.start = (write - offset + size) % size;
            g.age = 0;
            g.length = grainLen;
            g.active = true;
            return;
        }
    }

    double sr = 44100.0;
    int size = 44100, grainLen = 2646, write = 0;
    float countdown = 0.0f;
    std::vector<float> bufL, bufR;
    std::array<Grain, 12> grains;
    juce::Random rng { 0x5eed };
};

// Pitch + formant (spectral), robot (ring modulator) and grain (granular cloud).
// The processed path always has SpectralVoice::latency samples of delay, so switching the shift
// on and off crossfades between two aligned signals.
class TransformEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        voiceL.prepare (sr);
        voiceR.prepare (sr);
        delayL.prepare (SpectralVoice::latency);
        delayR.prepare (SpectralVoice::latency);
        granulator.prepare (sr);

        useVoice.reset (sr, 0.03);
        useVoice.setCurrentAndTargetValue (0.0f);
        robotAmount.reset (sr, 0.02);
        robotAmount.setCurrentAndTargetValue (0.0f);
        grainAmount.reset (sr, 0.02);
        grainAmount.setCurrentAndTargetValue (0.0f);
        carrierPhase = 0.0f;
    }

    void process (float* l, float* r, int n, float pitchSt, float formantSt, bool on, float robot01, float grain01, bool forceShift = false)
    {
        voiceL.setShift (pitchSt, formantSt);
        voiceR.setShift (pitchSt, formantSt);

        const bool shifting = forceShift || std::abs (pitchSt) > 0.01f || std::abs (formantSt) > 0.01f;
        useVoice.setTargetValue (shifting ? 1.0f : 0.0f);
        robotAmount.setTargetValue (on ? robot01 : 0.0f);
        grainAmount.setTargetValue (on ? grain01 : 0.0f);

        const float carrierInc = 2.0f * juce::MathConstants<float>::pi * 90.0f / static_cast<float> (sr);

        for (int i = 0; i < n; ++i)
        {
            const float vl = voiceL.processSample (l[i]);
            const float vr = voiceR.processSample (r[i]);
            const float dl = delayL.process (l[i]);
            const float dr = delayR.process (r[i]);

            const float g = useVoice.getNextValue();
            float xl = dl + (vl - dl) * g;
            float xr = dr + (vr - dr) * g;

            const float ra = robotAmount.getNextValue();

            if (ra > 0.0f)
            {
                carrierPhase += carrierInc;

                if (carrierPhase > juce::MathConstants<float>::twoPi)
                    carrierPhase -= juce::MathConstants<float>::twoPi;

                const float c = 1.0f - ra + ra * std::sin (carrierPhase);
                xl *= c;
                xr *= c;
            }

            granulator.process (xl, xr, grainAmount.getNextValue());

            l[i] = xl;
            r[i] = xr;
        }
    }

private:
    double sr = 44100.0;
    SpectralVoice voiceL, voiceR;
    FixedDelay delayL, delayR;
    Granulator granulator;
    juce::SmoothedValue<float> useVoice, robotAmount, grainAmount;
    float carrierPhase = 0.0f;
};
} // namespace epinox::dsp
