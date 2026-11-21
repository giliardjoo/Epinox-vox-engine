#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>
#include "Composer.h"
#include "GeneratedClip.h"
#include "VoiceTables.h"

namespace epinox::voice
{
// Source-filter vocal synthesiser.
//   voiced source : band-limited harmonic stack shaped by a cascade of formant resonators (F1..F5)
//   unvoiced      : filtered noise for fricatives, bursts and breath (breath goes through the formants)
// Everything is deterministic, so a clip can be rebuilt from its request.
class VocalSynth
{
public:
    static std::unique_ptr<GeneratedClip> render (const Composition& comp, const GenerationRequest& req,
                                                  const std::function<bool()>& cancelled)
    {
        Synth s (comp, req, cancelled);
        return s.run();
    }

private:
    struct Seg
    {
        double t0 = 0.0, t1 = 0.0;
        Formants f { 500.0f, 1500.0f, 2500.0f };
        float voice = 0.0f, noise = 0.0f, breath = 0.0f;
        float noiseHz = 4000.0f, noiseBw = 2000.0f;
        float pitch = 60.0f;
        float tauUp = 0.01f, tauDown = 0.03f, tauFormant = 0.016f;
        bool newNote = false;
        float scoop = 0.0f;
    };

    struct Biquad
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void bandpass (double sr, double fc, double bw)
        {
            fc = juce::jlimit (80.0, 0.45 * sr, fc);
            const double q = juce::jlimit (0.4, 40.0, fc / juce::jmax (20.0, bw));
            const double w0 = 2.0 * juce::MathConstants<double>::pi * fc / sr;
            const double alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = static_cast<float> (alpha / a0);
            b1 = 0.0f;
            b2 = static_cast<float> (-alpha / a0);
            a1 = static_cast<float> (-2.0 * std::cos (w0) / a0);
            a2 = static_cast<float> ((1.0 - alpha) / a0);
        }

        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    class Synth
    {
    public:
        Synth (const Composition& c, const GenerationRequest& r, const std::function<bool()>& cancel)
            : comp (c), req (r), cancelled (cancel),
              V (r.custom != nullptr ? r.custom->profile : morphVoice (r.voiceType, r.morphTo, r.morph)), E (emotionProfile (r.emotion)), S (styleProfile (r.style)),
              sr (r.sampleRate), spb (60.0 / r.bpm), rng (0x5eed1234 + r.variation)
        {
            voiced = V.voiced && ! S.whisper;
            flat = V.robotic || E.flat;
            fScale = V.formantScale * E.formantMul;
            breathAmount = voiced ? juce::jlimit (0.0f, 1.0f, V.breath + E.breathAdd + S.breathAdd) : 1.0f;
            brightness = juce::jlimit (0.3f, 2.5f, V.brightness * E.brightnessMul);
            vibCents = flat ? 0.0f : V.vibratoCents * E.vibratoMul;
            attackSec = juce::jmax (0.002f, 0.5f * (V.attackMs + S.attackMs) * E.attackMul * 0.001f);
            glideTau = flat ? 0.001f : juce::jmax (0.001f, S.glideMs * 0.001f / 2.5f);

            // Legato joins the notes: longer voiced part, softer release, shorter consonant gaps.
            S.legato = S.legato + (1.0f - S.legato) * 0.85f * juce::jlimit (0.0f, 1.0f, r.legato);

            // Layers double the voice with detuned, panned copies for a thicker, wider sound.
            const float lay = juce::jlimit (0.0f, 1.0f, r.layers);
            const int wanted = lay > 0.66f ? 3 : (lay > 0.2f ? 2 : 1);

            if (! flat && wanted > V.unison)
            {
                V.unison = wanted;
                V.unisonCents = juce::jmax (V.unisonCents, 5.0f + 9.0f * lay);
            }
        }

        std::unique_ptr<GeneratedClip> run()
        {
            buildTimeline();

            auto clip = std::make_unique<GeneratedClip>();
            clip->sampleRate = sr;
            clip->bpm = req.bpm;
            clip->loopBeats = comp.loopBeats;
            clip->syllables = static_cast<int> (comp.syllables.size());
            clip->truncated = comp.truncated;

            const int total = static_cast<int> (std::ceil (comp.loopBeats * spb * sr));
            clip->left.assign (static_cast<size_t> (total), 0.0f);
            clip->right.assign (static_cast<size_t> (total), 0.0f);

            if (! synthesise (*clip, total))
                return nullptr;

            addBody (*clip, juce::jlimit (0.0f, 1.0f, req.body));
            polishClip (*clip, juce::jlimit (0.0f, 1.0f, req.polish), juce::jlimit (0.0f, 1.0f, req.air));

            normalise (*clip);

            clip->description = juce::String (clip->syllables) + " syllables, "
                                + juce::String (static_cast<int> (comp.loopBeats / 4.0)) + " bar"
                                + (comp.loopBeats > 4.0 ? "s" : "") + " at " + juce::String (req.bpm, 0) + " BPM";
            return clip;
        }

    private:
        static constexpr int hop = 32;
        static constexpr int maxHarm = 128;
        // Breath and whisper noise is shaped by wide formant bands (narrow ones would sound pitched).
        static constexpr double breathBw1 = 160.0, breathBw2 = 240.0, breathBw3 = 320.0;

        //----------------------------------------------------------------------
        // The vowel's formants: the average table, or the measured ones when a voice profile is in use
        // (divided by the profile's own scale, which `scaled` puts back).
        Formants vowelF (char v) const
        {
            if (req.custom == nullptr)
                return vowelFormants (v);

            const int i = v == 'a' ? 0 : v == 'e' ? 1 : v == 'i' ? 2 : v == 'o' ? 3 : 4;
            const auto f = req.custom->vowels[static_cast<size_t> (i)];
            const float k = 1.0f / juce::jmax (0.3f, V.formantScale);
            return { f.f1 * k, f.f2 * k, f.f3 * k };
        }

        Formants scaled (Formants f) const { return { f.f1 * fScale, f.f2 * fScale, f.f3 * fScale }; }

        void addSeg (Seg s) { segs.push_back (s); }

        void buildTimeline()
        {
            const float legato = S.legato;

            for (size_t n = 0; n < comp.notes.size(); ++n)
            {
                const auto& note = comp.notes[n];
                const auto& syl = comp.syllables[static_cast<size_t> (note.syllable)];

                // A human singer is never exactly on the grid: up to 4 ms early or late.
                const double t0 = std::max (0.0, note.beat * spb + (flat ? 0.0 : (rng.nextFloat() - 0.5f) * 0.008));
                const double dur = note.beats * spb;
                const double voicedEnd = t0 + dur * (legato >= 0.97f ? 1.0 : legato);
                auto vf = scaled (vowelF (syl.vowel));

                // No two sung syllables are identical: small formant and tuning differences per note.
                float noteMidi = note.midi;

                if (! flat)
                {
                    vf.f1 *= 1.0f + 0.05f * (rng.nextFloat() - 0.5f);
                    vf.f2 *= 1.0f + 0.05f * (rng.nextFloat() - 0.5f);
                    noteMidi += 0.12f * (rng.nextFloat() - 0.5f);
                }

                auto specs = [] (const std::vector<juce::String>& codes)
                {
                    std::vector<ConsonantSpec> v;

                    for (auto& c : codes)
                        v.push_back (consonantSpec (c));

                    return v;
                };

                const auto onset = specs (syl.onset), coda = specs (syl.coda);
                double onsetSec = 0.0, codaSec = 0.0;

                const double cs = 1.0 - 0.3 * juce::jlimit (0.0f, 1.0f, req.legato);   // legato shortens consonants

                for (auto& c : onset) onsetSec += c.ms * 0.001 * cs;
                for (auto& c : coda)  codaSec += c.ms * 0.001 * cs;

                const double maxCons = 0.5 * (voicedEnd - t0);
                const double k = (onsetSec + codaSec) > maxCons ? maxCons / (onsetSec + codaSec) : 1.0;

                const float scoop = flat ? 0.0f
                                         : E.scoopSemis * static_cast<float> (juce::jlimit (0.0, 1.0, dur / 0.4));

                double cur = t0;
                bool first = true;

                auto emit = [&] (const ConsonantSpec& c, double len, Formants vowelF)
                {
                    auto base = [&] (double a, double b)
                    {
                        Seg s;
                        s.t0 = a;
                        s.t1 = b;
                        s.pitch = noteMidi;
                        s.f = c.f.f1 > 0.0f ? scaled (c.f) : vowelF;
                        s.tauUp = 0.008f;
                        s.tauDown = 0.012f;
                        s.tauFormant = 0.012f;
                        s.scoop = scoop;
                        s.newNote = first;
                        first = false;
                        return s;
                    };

                    switch (c.kind)
                    {
                        case Kind::stop:
                        {
                            auto closure = base (cur, cur + len * 0.6);
                            closure.voice = (voiced && c.voiced) ? 0.30f : 0.0f;
                            addSeg (closure);
                            auto burst = base (cur + len * 0.6, cur + len);
                            burst.voice = (voiced && c.voiced) ? 0.45f : 0.0f;
                            burst.noise = c.level;
                            burst.noiseHz = c.noiseHz;
                            burst.noiseBw = c.noiseBw;
                            burst.tauUp = 0.001f;
                            burst.tauDown = 0.004f;
                            addSeg (burst);
                            break;
                        }
                        case Kind::fricative:
                        {
                            auto s = base (cur, cur + len);
                            s.voice = (voiced && c.voiced) ? 0.45f : 0.0f;
                            s.noise = c.level;
                            s.noiseHz = c.noiseHz;
                            s.noiseBw = c.noiseBw;
                            s.tauUp = 0.004f;
                            addSeg (s);
                            break;
                        }
                        case Kind::nasal:
                        {
                            auto s = base (cur, cur + len);
                            s.voice = voiced ? 0.55f : 0.0f;
                            s.breath = voiced ? 0.0f : 0.4f;
                            addSeg (s);
                            break;
                        }
                        case Kind::liquid:
                        case Kind::glide:
                        {
                            auto s = base (cur, cur + len);
                            s.voice = voiced ? 0.85f : 0.0f;
                            s.breath = voiced ? 0.0f : 0.5f;
                            addSeg (s);
                            break;
                        }
                        case Kind::aspirate:
                        {
                            auto s = base (cur, cur + len);
                            s.breath = 0.6f;
                            s.f = vowelF;
                            addSeg (s);
                            break;
                        }
                    }

                    cur += len;
                };

                for (auto& c : onset)
                    emit (c, c.ms * 0.001 * k * cs, vf);

                const double vowelEnd = voicedEnd - codaSec * k;

                Seg v;
                v.t0 = cur;
                v.t1 = juce::jmax (cur + 0.01, vowelEnd);
                v.f = vf;
                v.voice = voiced ? note.accent : 0.0f;
                v.breath = breathAmount;
                v.pitch = noteMidi;
                v.tauUp = attackSec / 2.5f;
                v.tauDown = 0.03f + 0.05f * juce::jlimit (0.0f, 1.0f, req.legato);
                v.tauFormant = 0.016f;
                v.newNote = first;
                v.scoop = scoop;
                first = false;
                addSeg (v);
                cur = v.t1;

                for (auto& c : coda)
                    emit (c, c.ms * 0.001 * k * cs, vf);

                // Release tail so the next syllable (or silence) is entered smoothly.
                if (cur < voicedEnd - 1.0e-4)
                {
                    Seg tail = v;
                    tail.t0 = cur;
                    tail.t1 = voicedEnd;
                    tail.newNote = false;
                    addSeg (tail);
                }
            }

            std::sort (segs.begin(), segs.end(), [] (const Seg& a, const Seg& b) { return a.t0 < b.t0; });
        }

        //----------------------------------------------------------------------
        // Amplitude of harmonics 1..maxHarm for a given f0 and formant set, normalised to an RMS of `rms`.
        int computeAmps (float f0, float f1, float f2, float f3, float rms, float bright, float singer,
                     std::array<float, maxHarm + 1>& out, std::array<float, maxHarm + 1>& phaseOut) const
        {
            out.fill (0.0f);
            phaseOut.fill (0.0f);

            if (rms <= 1.0e-6f)
                return 0;

            const float limit = static_cast<float> (juce::jmin (7500.0, 0.45 * sr));
            const int h = juce::jlimit (1, maxHarm, static_cast<int> (limit / f0));
            const float fc[5] = { f1, f2, f3, 3300.0f * std::sqrt (fScale), 4500.0f * std::sqrt (fScale) };
            const float bw[5] = { 70.0f, 100.0f, 130.0f, 170.0f, 220.0f };
            const float slope = -6.0f / bright;     // dB per octave of source tilt
            double power = 0.0;

            for (int k = 1; k <= h; ++k)
            {
                const float f = f0 * static_cast<float> (k);
                float g = 1.0f, ph = 0.0f;

                for (int i = 0; i < 5; ++i)
                {
                    const float a = fc[i] * fc[i] - f * f;
                    const float b = f * bw[i];
                    g *= (fc[i] * fc[i]) / std::sqrt (a * a + b * b);
                    ph -= std::atan2 (b, a);      // phase of the resonator: the vocal tract rounds the glottal pulse
                }

                // Robotic voices keep the sharp zero-phase buzz on purpose.
                phaseOut[static_cast<size_t> (k)] = V.robotic ? 0.0f : ph;

                if (V.robotic)
                    g = std::pow (g, 0.5f);

                const float tilt = std::pow (10.0f, slope * std::log2 (juce::jmax (f, 100.0f) / 100.0f) / 20.0f);

                // Singer's formant: a broad lift around 2.9 kHz that lets a trained voice cut through.
                const float sfx = (f - 3000.0f * std::sqrt (fScale)) / 400.0f;
                g *= 1.0f + singer * std::exp (-0.5f * sfx * sfx);

                out[static_cast<size_t> (k)] = g * tilt;
                power += 0.5 * static_cast<double>(out[static_cast<size_t> (k)]) * out[static_cast<size_t> (k)];
            }

            const float norm = rms / static_cast<float> (std::sqrt (juce::jmax (1.0e-12, power)));

            for (int k = 1; k <= h; ++k)
                out[static_cast<size_t> (k)] *= norm;

            return h;
        }

        bool synthesise (GeneratedClip& clip, int total)
        {
            const double dt = static_cast<double> (hop) / sr;
            const double twoPi = juce::MathConstants<double>::twoPi;

            const int voices = V.unison;
            std::array<float, 3> detune {}, pan {};

            if (voices == 2)      { detune = { -V.unisonCents, V.unisonCents, 0.0f }; pan = { -0.35f, 0.35f, 0.0f }; }
            else if (voices == 3) { detune = { -V.unisonCents, 0.0f, V.unisonCents }; pan = { -0.5f, 0.0f, 0.5f }; }

            const float voiceGain = 1.0f / std::sqrt (static_cast<float> (voices));

            struct VoiceState
            {
                double phase = 0.0;
                float f0 = 200.0f;
                int h = 1;
                std::array<float, maxHarm + 1> amp {}, next {}, step {}, ampI {}, nextI {}, stepI {};
            };

            std::array<VoiceState, 3> vs;
            Biquad fric, br1, br2, br3;
            std::array<float, maxHarm + 1> phaseTmp {};

            Formants fCur = scaled (vowelF ('a'));
            float voiceA = 0.0f, noiseA = 0.0f, breathA = 0.0f, noiseHz = 4000.0f, noiseBw = 2000.0f;
            float pitchS = 60.0f, pitchOff = 0.0f, tNote = 0.0f;
            double vibPhase = 0.0;
            bool havePitch = false;

            // Human imperfection: slow intonation drift, cycle-to-cycle jitter, level shimmer, a wandering vibrato rate.
            std::array<float, 3> wander {}, jitter {};
            float shimmer = 0.0f, vibRate = 0.0f;
            const float human = flat ? 0.0f : 1.0f;
            const float singer = (voiced && ! flat) ? 0.3f * (1.0f - juce::jmin (1.0f, breathAmount * 2.5f)) : 0.0f;
            size_t segIdx = 0;
            const Seg* lastSeg = nullptr;
            float pitchTarget = 60.0f;

            const float targetRms = 0.2f;

            auto alpha = [&] (double tau) { return static_cast<float> (1.0 - std::exp (-dt / juce::jmax (1.0e-4, tau))); };

            auto f0For = [&] (int v)
            {
                const float depth = vibCents * static_cast<float> (juce::jlimit (0.0, 1.0, (static_cast<double> (tNote) - 0.15) / 0.3));
                const float vib = depth * (1.0f + 0.2f * vibRate) * 0.01f * static_cast<float> (std::sin (vibPhase + v * 1.7));
                const float drift = (wander[static_cast<size_t> (v)] * 160.0f + jitter[static_cast<size_t> (v)] * 2.5f) * 0.01f * human;
                const float midi = pitchS + pitchOff + vib + drift + detune[static_cast<size_t> (v)] * 0.01f;
                return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
            };

            // First frame: no ramp from a previous frame.
            bool firstFrame = true;

            for (int pos = 0; pos < total; pos += hop)
            {
                if ((pos & 8191) == 0 && cancelled && cancelled())
                    return false;

                const double t = (static_cast<double> (pos) + hop * 0.5) / sr;

                while (segIdx + 1 < segs.size() && t >= segs[segIdx].t1)
                    ++segIdx;

                const Seg* sg = (segIdx < segs.size() && t >= segs[segIdx].t0 && t < segs[segIdx].t1) ? &segs[segIdx] : nullptr;

                float voiceT = 0.0f, noiseT = 0.0f, breathT = 0.0f;
                double tauUp = 0.01, tauDown = 0.03, tauF = 0.016;

                if (sg != nullptr)
                {
                    voiceT = sg->voice;
                    noiseT = sg->noise;
                    breathT = sg->breath;
                    tauUp = sg->tauUp;
                    tauDown = sg->tauDown;
                    tauF = sg->tauFormant;
                    pitchTarget = sg->pitch;

                    if (sg != lastSeg && sg->newNote)
                    {
                        tNote = 0.0f;
                        pitchOff = -sg->scoop;
                    }

                    if (! havePitch)
                    {
                        pitchS = sg->pitch;
                        havePitch = true;
                    }

                    if (noiseT > 0.0f)
                    {
                        noiseHz = sg->noiseHz;
                        noiseBw = sg->noiseBw;
                    }

                    const float af = alpha (tauF);
                    fCur.f1 += (sg->f.f1 - fCur.f1) * af;
                    fCur.f2 += (sg->f.f2 - fCur.f2) * af;
                    fCur.f3 += (sg->f.f3 - fCur.f3) * af;
                    lastSeg = sg;
                }

                voiceA += (voiceT - voiceA) * alpha (voiceT > voiceA ? tauUp : tauDown);
                noiseA += (noiseT - noiseA) * alpha (noiseT > noiseA ? 0.002 : 0.006);
                breathA += (breathT - breathA) * alpha (0.01);
                pitchS += (pitchTarget - pitchS) * alpha (glideTau);
                pitchOff *= static_cast<float> (std::exp (-dt / 0.07));
                tNote += static_cast<float> (dt);
                vibPhase += twoPi * V.vibratoHz * (1.0 + 0.07 * vibRate) * dt;

                for (size_t v = 0; v < 3; ++v)
                {
                    wander[v] += static_cast<float> (dt / 0.12) * ((rng.nextFloat() * 2.0f - 1.0f) - wander[v]);
                    jitter[v] += 0.5f * ((rng.nextFloat() * 2.0f - 1.0f) - jitter[v]);
                }

                shimmer += 0.4f * ((rng.nextFloat() * 2.0f - 1.0f) - shimmer);
                vibRate += static_cast<float> (dt / 0.4) * ((rng.nextFloat() * 2.0f - 1.0f) - vibRate) ;

                // Noise filters follow the current formants and noise colour.
                fric.bandpass (sr, noiseHz, noiseBw);
                br1.bandpass (sr, fCur.f1, breathBw1);
                br2.bandpass (sr, fCur.f2, breathBw2);
                br3.bandpass (sr, fCur.f3, breathBw3);

                const float fricGain = noiseA * 0.30f * 1.73f * static_cast<float> (std::sqrt (sr / (2.0 * juce::jmax (200.0f, noiseBw))));
                const float breathBase = breathA * (voiced ? 0.09f : 0.24f) * 1.73f;
                const float w1 = 1.0f, w2 = 0.7f, w3 = 0.5f;
                const float wn = 1.0f / std::sqrt (w1 * w1 + w2 * w2 + w3 * w3);
                const float g1 = breathBase * wn * w1 * static_cast<float> (std::sqrt (sr / (2.0 * breathBw1)));
                const float g2 = breathBase * wn * w2 * static_cast<float> (std::sqrt (sr / (2.0 * breathBw2)));
                const float g3 = breathBase * wn * w3 * static_cast<float> (std::sqrt (sr / (2.0 * breathBw3)));

                // Harmonic amplitudes for the end of this frame, ramped over the hop.
                for (int v = 0; v < voices; ++v)
                {
                    auto& st = vs[static_cast<size_t> (v)];
                    const float f0 = f0For (v);
                    // Sopranos tune the first formant up to the sung pitch so high vowels keep their power.
                    const float f1Eff = (voiced && ! flat && f0 > 0.8f * fCur.f1) ? f0 * 1.04f : fCur.f1;

                    // Louder singing is brighter, soft onsets are darker.
                    const float level = juce::jlimit (0.5f, 1.4f, voiceA);
                    // Register: higher notes are purer and slightly darker, lower notes carry more harmonics.
                    const float reg = juce::jlimit (-1.0f, 1.0f, (pitchS - V.centreMidi) / 12.0f);
                    const float bright = brightness * (0.8f + 0.2f * level) * (1.0f - 0.12f * reg * human);

                    st.h = computeAmps (f0, f1Eff, fCur.f2, fCur.f3, targetRms * voiceA * (1.0f + 0.04f * shimmer * human),
                                        bright, singer, st.next, phaseTmp);

                    // Complex coefficient per harmonic (real part on sin, imaginary part on cos), so ramps stay smooth.
                    for (int k = 1; k <= st.h; ++k)
                    {
                        const float a = st.next[static_cast<size_t> (k)], ph = phaseTmp[static_cast<size_t> (k)];
                        st.next[static_cast<size_t> (k)] = a * std::cos (ph);
                        st.nextI[static_cast<size_t> (k)] = a * std::sin (ph);
                    }

                    for (int k = st.h + 1; k <= maxHarm; ++k)
                        st.nextI[static_cast<size_t> (k)] = 0.0f;

                    if (firstFrame)
                    {
                        st.amp = st.next;
                        st.ampI = st.nextI;
                    }

                    for (int k = 1; k <= maxHarm; ++k)
                    {
                        st.step[static_cast<size_t> (k)] = (st.next[static_cast<size_t> (k)] - st.amp[static_cast<size_t> (k)]) / hop;
                        st.stepI[static_cast<size_t> (k)] = (st.nextI[static_cast<size_t> (k)] - st.ampI[static_cast<size_t> (k)]) / hop;
                    }

                    const float f0Start = firstFrame ? f0 : st.f0;
                    st.f0 = f0;
                    stF0Start[static_cast<size_t> (v)] = f0Start;
                }

                firstFrame = false;

                const int n = juce::jmin (hop, total - pos);

                for (int i = 0; i < n; ++i)
                {
                    const float white = rng.nextFloat() * 2.0f - 1.0f;
                    float noiseOut = fric.process (white) * fricGain;
                    const float white2 = rng.nextFloat() * 2.0f - 1.0f;

                    // Aspiration noise is pulsed by the vocal folds when the voice is on.
                    const float pulse = voiced ? 1.0f + 0.7f * voiceA * static_cast<float> (std::cos (vs[0].phase)) : 1.0f;
                    noiseOut += (br1.process (white2) * g1 + br2.process (white2) * g2 + br3.process (white2) * g3) * pulse;

                    float l = noiseOut, r = noiseOut;

                    for (int v = 0; v < voices; ++v)
                    {
                        auto& st = vs[static_cast<size_t> (v)];
                        const float frac = static_cast<float> (i) / hop;
                        const float f0 = stF0Start[static_cast<size_t> (v)] + (st.f0 - stF0Start[static_cast<size_t> (v)]) * frac;
                        st.phase += twoPi * f0 / sr;

                        if (st.phase > twoPi)
                            st.phase -= twoPi;

                        const double cs = std::cos (st.phase), sn = std::sin (st.phase);
                        double sk = sn, ck = cs;       // sin and cos of k * phase, advanced by rotation
                        double sum = 0.0;

                        for (int k = 1; k <= maxHarm; ++k)
                        {
                            auto& a = st.amp[static_cast<size_t> (k)];
                            auto& b = st.ampI[static_cast<size_t> (k)];
                            a += st.step[static_cast<size_t> (k)];
                            b += st.stepI[static_cast<size_t> (k)];

                            if (k <= st.h)
                            {
                                sum += static_cast<double> (a) * sk + static_cast<double> (b) * ck;
                                const double sNext = sk * cs + ck * sn;
                                ck = ck * cs - sk * sn;
                                sk = sNext;
                            }
                        }

                        const float out = static_cast<float> (sum) * voiceGain;
                        const float p = (pan[static_cast<size_t> (v)] + 1.0f) * 0.7853982f;
                        l += out * std::cos (p);
                        r += out * std::sin (p);
                    }

                    clip.left[static_cast<size_t> (pos + i)] = l;
                    clip.right[static_cast<size_t> (pos + i)] = r;
                }

                for (int v = 0; v < voices; ++v)
                    {
                        vs[static_cast<size_t> (v)].amp = vs[static_cast<size_t> (v)].next;
                        vs[static_cast<size_t> (v)].ampI = vs[static_cast<size_t> (v)].nextI;
                    }
            }

            return true;
        }

        // Body: a low shelf for chest warmth, then a soft saturation (scaled to the clip peak) that thickens the harmonics.
        static void addBody (GeneratedClip& clip, float amount)
        {
            if (amount <= 0.001f)
                return;

            const double sr = clip.sampleRate;
            const double A = std::pow (10.0, (5.0 * amount) / 40.0);
            const double w0 = juce::MathConstants<double>::twoPi * 230.0 / sr;
            const double cw = std::cos (w0), alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
            const double sq = 2.0 * std::sqrt (A) * alpha;

            const double b0 = A * ((A + 1) - (A - 1) * cw + sq), b1 = 2 * A * ((A - 1) - (A + 1) * cw),
                         b2 = A * ((A + 1) - (A - 1) * cw - sq),
                         a0 = (A + 1) + (A - 1) * cw + sq, a1 = -2 * ((A - 1) + (A + 1) * cw), a2 = (A + 1) + (A - 1) * cw - sq;

            float peak = 1.0e-6f;

            for (auto* ch : { &clip.left, &clip.right })
            {
                double z1 = 0.0, z2 = 0.0;

                for (auto& x : *ch)
                {
                    const double in = x;
                    const double y = (b0 / a0) * in + z1;
                    z1 = (b1 / a0) * in - (a1 / a0) * y + z2;
                    z2 = (b2 / a0) * in - (a2 / a0) * y;
                    x = static_cast<float> (y);
                    peak = juce::jmax (peak, std::abs (x));
                }
            }

            const float drive = 1.0f + 1.4f * amount;
            const float norm = 1.0f / std::tanh (drive);

            for (auto* ch : { &clip.left, &clip.right })
                for (auto& x : *ch)
                    x = std::tanh (drive * x / peak) * norm * peak;
        }

        // Finishing chain: a linked compressor evens the level, a de-esser tames sibilance, an air shelf opens the top.
        static void polishClip (GeneratedClip& clip, float polish, float air)
        {
            const size_t n = clip.left.size();
            const double sr = clip.sampleRate;

            if (n == 0)
                return;

            struct Bq
            {
                double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

                double process (double x)
                {
                    const double y = b0 * x + z1;
                    z1 = b1 * x - a1 * y + z2;
                    z2 = b2 * x - a2 * y;
                    return y;
                }
            };

            auto highpass = [&] (double fc)
            {
                const double w0 = juce::MathConstants<double>::twoPi * fc / sr, cw = std::cos (w0);
                const double alpha = std::sin (w0) / (2.0 * 0.7071);
                const double a0 = 1.0 + alpha;
                Bq q;
                q.b0 = ((1.0 + cw) / 2.0) / a0;
                q.b1 = -(1.0 + cw) / a0;
                q.b2 = q.b0;
                q.a1 = -2.0 * cw / a0;
                q.a2 = (1.0 - alpha) / a0;
                return q;
            };

            auto highShelf = [&] (double fc, double gainDb)
            {
                const double A = std::pow (10.0, gainDb / 40.0);
                const double w0 = juce::MathConstants<double>::twoPi * fc / sr, cw = std::cos (w0);
                const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
                const double sq = 2.0 * std::sqrt (A) * alpha;
                const double a0 = (A + 1) - (A - 1) * cw + sq;
                Bq q;
                q.b0 = A * ((A + 1) + (A - 1) * cw + sq) / a0;
                q.b1 = -2.0 * A * ((A - 1) + (A + 1) * cw) / a0;
                q.b2 = A * ((A + 1) + (A - 1) * cw - sq) / a0;
                q.a1 = 2.0 * ((A - 1) - (A + 1) * cw) / a0;
                q.a2 = ((A + 1) - (A - 1) * cw - sq) / a0;
                return q;
            };

            double rmsBefore = 0.0;

            for (size_t i = 0; i < n; ++i)
                rmsBefore += 0.5 * (static_cast<double> (clip.left[i]) * clip.left[i] + static_cast<double> (clip.right[i]) * clip.right[i]);

            rmsBefore = std::sqrt (rmsBefore / static_cast<double> (n));

            if (rmsBefore < 1.0e-6)
                return;

            if (polish > 0.001f)
            {
                // De-esser: the 5 kHz and up band is turned down when it gets loud compared with the whole signal.
                Bq hpL = highpass (5200.0), hpR = hpL;
                double bandEnv = 0.0;
                const double bandAtt = 1.0 - std::exp (-1.0 / (0.001 * sr)), bandRel = 1.0 - std::exp (-1.0 / (0.040 * sr));

                // Compressor: threshold just above the average level, ratio grows with the amount.
                const double ratio = 1.0 + 3.0 * polish;
                double env = 0.0, grDb = 0.0;
                const double att = 1.0 - std::exp (-1.0 / (0.002 * sr)), rel = 1.0 - std::exp (-1.0 / (0.150 * sr));
                const size_t look = static_cast<size_t> (0.004 * sr);   // the detector looks ahead, so peaks are caught

                // Level detector: short-term RMS (about 12 ms) looking a few ms ahead, closer to how loudness is heard than peaks.
                std::vector<float> det (n);
                {
                    double p2 = 0.0;
                    const double c = 1.0 - std::exp (-1.0 / (0.012 * sr));

                    for (size_t i = 0; i < n; ++i)
                    {
                        const size_t j = std::min (n - 1, i + look);
                        const double l = clip.left[j], r = clip.right[j];
                        p2 += c * (0.5 * (l * l + r * r) - p2);
                        det[i] = static_cast<float> (std::sqrt (p2));
                    }
                }

                // Threshold = the average level of the sung parts (silence ignored), so only the louder parts are pulled down.
                double thrDb = 0.0;
                {
                    double sumDb = 0.0, maxEnv = 1.0e-6;
                    const auto& envs = det;

                    for (size_t i = 0; i < n; ++i)
                        maxEnv = std::max (maxEnv, static_cast<double> (envs[i]));

                    size_t cnt = 0;

                    for (size_t i = 0; i < n; ++i)
                        if (envs[i] > 0.1 * maxEnv)
                        {
                            sumDb += 20.0 * std::log10 (static_cast<double> (envs[i]));
                            ++cnt;
                        }

                    thrDb = cnt > 0 ? sumDb / static_cast<double> (cnt) : -20.0;
                }

                for (size_t i = 0; i < n; ++i)
                {
                    double l = clip.left[i], r = clip.right[i];

                    const double bl = hpL.process (l), br = hpR.process (r);
                    const double band = std::max (std::abs (bl), std::abs (br));
                    bandEnv += (band > bandEnv ? bandAtt : bandRel) * (band - bandEnv);

                    const size_t j = std::min (n - 1, i + look);
                    const double whole = std::max (std::abs (static_cast<double> (clip.left[j])), std::abs (static_cast<double> (clip.right[j])));
                    const double level = det[i];
                    const double ratioHf = bandEnv / std::max (1.0e-5, env + 1.0e-5);   // band against the whole
                    const double over = std::max (0.0, ratioHf - 0.35);
                    const double cut = std::min (0.75, over * 1.4) * polish;   // up to 75 % of the band removed

                    l -= bl * cut;
                    r -= br * cut;

                    env += (whole > env ? att : rel) * (whole - env);
                    const double envDb = 20.0 * std::log10 (std::max (level, 1.0e-6));
                    const double targetGr = envDb > thrDb ? (envDb - thrDb) * (1.0 - 1.0 / ratio) : 0.0;
                    grDb += (targetGr > grDb ? att : rel) * (targetGr - grDb);
                    const double g = std::pow (10.0, -grDb / 20.0);

                    clip.left[i] = static_cast<float> (l * g);
                    clip.right[i] = static_cast<float> (r * g);
                }

                // Make-up gain: back to the original average level.
                double rmsAfter = 0.0;

                for (size_t i = 0; i < n; ++i)
                    rmsAfter += 0.5 * (static_cast<double> (clip.left[i]) * clip.left[i] + static_cast<double> (clip.right[i]) * clip.right[i]);

                rmsAfter = std::sqrt (rmsAfter / static_cast<double> (n));
                const float makeup = static_cast<float> (rmsBefore / std::max (1.0e-9, rmsAfter));

                for (size_t i = 0; i < n; ++i)
                {
                    clip.left[i] *= makeup;
                    clip.right[i] *= makeup;
                }
            }

            if (air > 0.001f)
            {
                auto shelfL = highShelf (5500.0, 5.0 * air), shelfR = shelfL;

                for (size_t i = 0; i < n; ++i)
                {
                    clip.left[i] = static_cast<float> (shelfL.process (clip.left[i]));
                    clip.right[i] = static_cast<float> (shelfR.process (clip.right[i]));
                }
            }
        }

        static void normalise (GeneratedClip& clip)
        {
            float peak = 0.0f;

            for (size_t i = 0; i < clip.left.size(); ++i)
                peak = juce::jmax (peak, std::abs (clip.left[i]), std::abs (clip.right[i]));

            if (peak < 1.0e-6f)
                return;

            const float g = 0.7f / peak;
            const int fade = juce::jmin (static_cast<int> (clip.left.size()) / 2, static_cast<int> (clip.sampleRate * 0.003));

            for (size_t i = 0; i < clip.left.size(); ++i)
            {
                float f = 1.0f;

                if (static_cast<int> (i) < fade)
                    f = static_cast<float> (i) / static_cast<float> (fade);
                else if (static_cast<int> (clip.left.size() - 1 - i) < fade)
                    f = static_cast<float> (clip.left.size() - 1 - i) / static_cast<float> (fade);

                clip.left[i] *= g * f;
                clip.right[i] *= g * f;
            }
        }

        const Composition& comp;
        const GenerationRequest& req;
        const std::function<bool()>& cancelled;
        VoiceProfile V;
        EmotionProfile E;
        StyleProfile S;
        double sr, spb;
        juce::Random rng;
        std::vector<Seg> segs;
        std::array<float, 3> stF0Start {};
        bool voiced = true, flat = false;
        float fScale = 1.0f, breathAmount = 0.05f, brightness = 1.0f, vibCents = 0.0f;
        float attackSec = 0.015f, glideTau = 0.02f;
    };
};
} // namespace epinox::voice
