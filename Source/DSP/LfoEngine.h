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
    float fadeMs = 0.0f;    // the LFO fades in over this long after every new note (0 = off)
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
        env = 0.0f; fade = 1.0f; noteOn = false; quiet = 0;
        held = 0.0f;
        smoothed = 0.0f;
        slew = 1.0f - std::exp (-1.0f / (0.006f * static_cast<float> (sr)));
        slowSlew = 1.0f - std::exp (-1.0f / (0.04f * static_cast<float> (sr)));
        for (auto& c : svf) c = {};
        cutoffSm.reset (sr, 0.003);
        cutoffSm.setCurrentAndTargetValue (12000.0f);
        rateSm.reset (sr, 0.03);
        rateSm.setCurrentAndTargetValue (1.0f);
        gainSm.reset (sr, 0.004);
        gainSm.setCurrentAndTargetValue (1.0f);
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
        rateSm.setTargetValue (rateFor (p));   // automating the rate or the division glides instead of jumping
        const auto& beats = syncBeats();
        const int idx = juce::jlimit (0, static_cast<int> (beats.size()) - 1, p.sync);

        const bool locked = p.hostLocked && idx > 0;
        const double beatsPerSample = static_cast<double> (p.bpm) / 60.0 / sr;
        const double cycleBeats = locked ? static_cast<double> (beats[static_cast<size_t> (idx)]) : 1.0;

        for (int i = 0; i < n; ++i)
        {
            const double inc = static_cast<double> (rateSm.getNextValue()) / sr;

            if (locked)
            {
                const double cycles = (p.hostPpq + i * beatsPerSample) / cycleBeats;
                phase = cycles - std::floor (cycles);
            }

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

    // Fade-in: every time the voice starts after silence the LFO depth ramps up from nothing over fadeMs, so a long
    // note starts straight and the vibrato grows in, as a singer does it. Scales out[0..n) in place.
    void applyFade (const float* l, const float* r, int n, float fadeMs, float* out)
    {
        if (fadeMs <= 0.0f)
        {
            fade = 1.0f;
            return;
        }

        const float inc = 1000.0f / (fadeMs * static_cast<float> (sr));

        for (int i = 0; i < n; ++i)
        {
            const float a = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            env += (a > env ? 0.05f : 0.0008f) * (a - env);

            if (! noteOn && env > 0.003f)
            {
                noteOn = true;
                fade = 0.0f;
            }
            else if (noteOn && env < 0.0015f)
            {
                if (++quiet > static_cast<int> (0.08 * sr))
                {
                    noteOn = false;
                    quiet = 0;
                }
            }
            else
                quiet = 0;

            fade = juce::jmin (1.0f, fade + inc);
            out[i] *= fade * fade * (3.0f - 2.0f * fade);   // smoothstep
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
                gainSm.setTargetValue (1.0f - p.volume * (1.0f - u));
                const float gain = gainSm.getNextValue();
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
    float env = 0.0f, fade = 1.0f;
    bool noteOn = false;
    int quiet = 0;
    float held = 0.0f, smoothed = 0.0f, slew = 0.01f, slowSlew = 0.001f;
    std::uint32_t rnd = 12345u;
    float g = 0.1f, k = 0.8f, a1 = 0.5f, a2 = 0.05f, a3 = 0.005f;
    Svf svf[2];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSm, rateSm;
    juce::SmoothedValue<float> gainSm;
};

// Vibrato as a modulated delay line (the classic way): clean, no phase-vocoder artefacts, and the level stays constant.
// The delay is the integral of the LFO, so the pitch follows the LFO shape (up to +/-2 semitones at full depth).
class VibratoDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = static_cast<int> (0.12 * sr) + 8;
        bufL.assign (static_cast<size_t> (size), 0.0f);
        bufR = bufL;
        pos = 0;
        integral = 0.0f;
        amp.reset (sr, 0.15);
        amp.setCurrentAndTargetValue (0.0f);
    }

    static constexpr float maxDelay = 0.035f;   // seconds; slow, deep settings are limited so the delay stays short

    void process (float* l, float* r, int n, const LfoParams& p, float rateHz, const float* lfo)
    {
        const float k0 = 0.057762265f * LfoEngine::maxPitchSt * p.pitch;    // ln2 / 12 per semitone
        const float peak = k0 * 0.27f / juce::jmax (0.05f, rateHz);          // largest swing of the integral
        const float scale = peak > maxDelay ? maxDelay / peak : 1.0f;
        const float k = k0 * scale;
        amp.setTargetValue (juce::jmin (peak, maxDelay) + 0.0005f);
        const float leak = 2.0f * juce::MathConstants<float>::pi * 0.1f / static_cast<float> (sr);

        for (int i = 0; i < n; ++i)
        {
            integral += lfo[i] / static_cast<float> (sr);
            integral -= leak * integral;

            const float a = amp.getNextValue();
            const float d = juce::jlimit (2.0f, static_cast<float> (size - 4),
                                          static_cast<float> (sr) * juce::jlimit (0.0f, 2.0f * a, a - k * integral) + 2.0f);

            bufL[static_cast<size_t> (pos)] = l[i];
            bufR[static_cast<size_t> (pos)] = r[i];
            l[i] = read (bufL, d);
            r[i] = read (bufR, d);

            if (++pos >= size)
                pos = 0;
        }
    }

private:
    float read (const std::vector<float>& b, float delay) const
    {
        const float rp = static_cast<float> (pos) - delay;
        const float f = rp - std::floor (rp);
        int i1 = static_cast<int> (std::floor (rp));
        i1 = ((i1 % size) + size) % size;
        const float y0 = b[static_cast<size_t> ((i1 + size - 1) % size)], y1 = b[static_cast<size_t> (i1)];
        const float y2 = b[static_cast<size_t> ((i1 + 1) % size)], y3 = b[static_cast<size_t> ((i1 + 2) % size)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    double sr = 44100.0;
    int size = 4096, pos = 0;
    float integral = 0.0f;
    std::vector<float> bufL, bufR;
    juce::SmoothedValue<float> amp;
};
} // namespace epinox::dsp
