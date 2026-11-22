#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "ChopEngine.h"
#include "EffectsEngine.h"
#include "LfoEngine.h"
#include "SpatialEngine.h"
#include "TransformEngine.h"

namespace epinox::dsp
{
// Looping vocal produced by the generator. The host side decides where in the loop each block starts.
struct GeneratedInput
{
    const float* left = nullptr;
    const float* right = nullptr;
    int length = 0;
    int startPos = 0;       // loop position (in samples) of the first sample of the block
    bool play = false;
};

// Snapshot of every parameter the engine needs, normalised to the units the engines use.
struct EngineParams
{
    bool power = true;
    float mix = 0.7f;       // 0..1
    float bpm = 128.0f;
    float pitch = 0.0f;     // semitones, global
    float formant = 0.0f;   // semitones, global

    bool chopOn = true;
    float chopRate = 8.0f;  // Hz
    int chopSteps = 16;
    float chopSmooth = 0.2f, chopMix = 1.0f;

    bool trOn = true;
    float trPitch = 0.0f, trFormant = 0.0f, trRobot = 0.0f, trGrain = 0.0f;

    bool fxOn = true;
    float fxReverb = 0.0f, fxDelay = 0.0f, fxDistort = 0.0f, fxWidth = 1.0f; // width: 0..2

    bool spOn = true;
    float spSize = 0.5f, spDensity = 0.5f, spDucking = 0.0f, spMix = 0.0f;

    LfoParams lfo;
};

// Signal flow: input (+ generated vocal) -> Vocal Chop -> Transform (+ LFO on pitch/formant, then LFO filter and level) -> Effects -> Spatial -> Mix (against the aligned dry signal).
class VoxEngine
{
public:
    static constexpr int latencySamples = SpectralVoice::latency;

    void prepare (double sampleRate, int maxBlockSize)
    {
        maxBlock = juce::jmax (16, maxBlockSize);
        chop.prepare (sampleRate);
        transform.prepare (sampleRate);
        lfo.prepare (sampleRate);
        lfoBuf.assign (static_cast<size_t> (maxBlock), 0.0f);
        effects.prepare (sampleRate);
        spatial.prepare (sampleRate);
        dryDelayL.prepare (latencySamples);
        dryDelayR.prepare (latencySamples);
        dryL.assign (static_cast<size_t> (maxBlock), 0.0f);
        dryR.assign (static_cast<size_t> (maxBlock), 0.0f);

        mixSm.reset (sampleRate, 0.02);
        powerSm.reset (sampleRate, 0.02);
        genGain.reset (sampleRate, 0.005);
        genGain.setCurrentAndTargetValue (0.0f);
        firstCall = true;
    }

    // Processes a stereo buffer in place.
    void process (juce::AudioBuffer<float>& buffer, const EngineParams& p, const GeneratedInput& gen = {})
    {
        if (buffer.getNumChannels() < 2)
            return;

        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        const int total = buffer.getNumSamples();

        if (firstCall)
        {
            mixSm.setCurrentAndTargetValue (p.mix);
            powerSm.setCurrentAndTargetValue (p.power ? 1.0f : 0.0f);
            firstCall = false;
        }

        for (int start = 0; start < total; start += maxBlock)
            processChunk (l + start, r + start, juce::jmin (maxBlock, total - start), p, gen, start);
    }

private:
    void processChunk (float* l, float* r, int n, const EngineParams& p, const GeneratedInput& gen, int offset)
    {
        for (int i = 0; i < n; ++i)
        {
            dryL[static_cast<size_t> (i)] = dryDelayL.process (l[i]);
            dryR[static_cast<size_t> (i)] = dryDelayR.process (r[i]);
        }

        powerSm.setTargetValue (p.power ? 1.0f : 0.0f);
        mixSm.setTargetValue (p.mix);

        if (! p.power && ! powerSm.isSmoothing() && powerSm.getCurrentValue() < 0.0001f)
        {
            // Bypassed: the delayed dry signal keeps the plugin latency constant.
            for (int i = 0; i < n; ++i)
            {
                l[i] = dryL[static_cast<size_t> (i)];
                r[i] = dryR[static_cast<size_t> (i)];
            }
            return;
        }

        if (gen.length > 0 && gen.left != nullptr && gen.right != nullptr)
        {
            genGain.setTargetValue (gen.play ? 1.0f : 0.0f);
            int pos = (gen.startPos + offset) % gen.length;

            for (int i = 0; i < n; ++i)
            {
                const float g = genGain.getNextValue();
                l[i] += gen.left[pos] * g;
                r[i] += gen.right[pos] * g;

                if (++pos >= gen.length)
                    pos = 0;
            }
        }
        else
        {
            genGain.setCurrentAndTargetValue (0.0f);
        }

        chop.process (l, r, n, p.chopOn, p.chopRate, p.chopSteps, p.chopSmooth, p.chopMix);

        float pitchTotal = p.pitch + (p.trOn ? p.trPitch : 0.0f);
        float formantTotal = p.formant + (p.trOn ? p.trFormant : 0.0f);

        // One LFO value per sample; pitch and formant follow its average over the chunk.
        const bool lfoOn = p.lfo.active();
        bool forceShift = false;

        if (lfoOn)
        {
            lfo.generate (n, p.lfo, lfoBuf.data());
            float mean = 0.0f;

            for (int i = 0; i < n; ++i)
                mean += lfoBuf[static_cast<size_t> (i)];

            mean /= static_cast<float> (n);
            pitchTotal += mean * p.lfo.pitch * LfoEngine::maxPitchSt;
            formantTotal += mean * p.lfo.formant * LfoEngine::maxFormantSt;
            forceShift = p.lfo.pitch > 0.0f || p.lfo.formant > 0.0f;
        }

        transform.process (l, r, n, pitchTotal, formantTotal, p.trOn, p.trRobot, p.trGrain, forceShift);

        if (lfoOn)
            lfo.applyAudio (l, r, n, p.lfo, lfoBuf.data());

        effects.process (l, r, n, p.fxOn, p.fxReverb, p.fxDelay, p.fxDistort, p.fxWidth, p.bpm);
        spatial.process (l, r, n, p.spOn, p.spSize, p.spDensity, p.spDucking, p.spMix);

        for (int i = 0; i < n; ++i)
        {
            const float m = mixSm.getNextValue();
            const float pw = powerSm.getNextValue();
            const float dl = dryL[static_cast<size_t> (i)];
            const float dr = dryR[static_cast<size_t> (i)];

            const float wl = dl * (1.0f - m) + l[i] * m;
            const float wr = dr * (1.0f - m) + r[i] * m;

            l[i] = dl + (wl - dl) * pw;
            r[i] = dr + (wr - dr) * pw;
        }
    }

    int maxBlock = 512;
    bool firstCall = true;
    ChopEngine chop;
    TransformEngine transform;
    LfoEngine lfo;
    std::vector<float> lfoBuf;
    EffectsEngine effects;
    SpatialEngine spatial;
    FixedDelay dryDelayL, dryDelayR;
    std::vector<float> dryL, dryR;
    juce::SmoothedValue<float> mixSm, powerSm, genGain;
};
} // namespace epinox::dsp
