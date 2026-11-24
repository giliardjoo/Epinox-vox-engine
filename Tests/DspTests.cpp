// Numerical checks of the VOX ENGINE audio engines. Exit code is the number of failed checks.
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "DSP/VoxEngine.h"
#include "Voice/PhraseRenderer.h"
#include "VoiceEngines.h"
#include "Voice/MelodySearch.h"
#include "DSP/KeyDetector.h"

using namespace epinox::dsp;

namespace
{
int failures = 0;
constexpr double sr = 44100.0;
constexpr double twoPi = 6.283185307179586;

void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());

    if (! ok)
        ++failures;
}

std::string fmt (const char* f, double a, double b = 0.0, double c = 0.0)
{
    char buf[256];
    std::snprintf (buf, sizeof (buf), f, a, b, c);
    return buf;
}

std::vector<float> sine (double freq, double amp, int n)
{
    std::vector<float> v (static_cast<size_t> (n));

    for (int i = 0; i < n; ++i)
        v[static_cast<size_t> (i)] = static_cast<float> (amp * std::sin (twoPi * freq * i / sr));

    return v;
}

// Harmonic signal with two formant-like humps (700 Hz and 2200 Hz) on a 150 Hz fundamental.
std::vector<float> vowel (int n)
{
    std::vector<float> v (static_cast<size_t> (n), 0.0f);

    for (int h = 1; h <= 60; ++h)
    {
        const double f = 150.0 * h;
        const double a = std::exp (-std::pow ((f - 700.0) / 300.0, 2.0)) + 0.3 * std::exp (-std::pow ((f - 2200.0) / 500.0, 2.0));

        for (int i = 0; i < n; ++i)
            v[static_cast<size_t> (i)] += static_cast<float> (0.15 * a * std::sin (twoPi * f * i / sr));
    }

    return v;
}

double rms (const std::vector<float>& v, int a, int b)
{
    double s = 0.0;

    for (int i = a; i < b; ++i)
        s += static_cast<double> (v[static_cast<size_t> (i)]) * v[static_cast<size_t> (i)];

    return std::sqrt (s / (b - a));
}

double zeroCrossingFrequency (const std::vector<float>& v, int a, int b)
{
    int crossings = 0;

    for (int i = a + 1; i < b; ++i)
        if (v[static_cast<size_t> (i - 1)] < 0.0f && v[static_cast<size_t> (i)] >= 0.0f)
            ++crossings;

    return crossings / ((b - a) / sr);
}

int bestLag (const std::vector<float>& x, const std::vector<float>& y, int a, int b, int lagMin, int lagMax)
{
    std::vector<double> value (static_cast<size_t> (lagMax - lagMin + 1));
    double top = -1.0e30;

    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        double s = 0.0;

        for (int i = a; i < b; ++i)
            s += static_cast<double> (x[static_cast<size_t> (i)]) * y[static_cast<size_t> (i + lag)];

        value[static_cast<size_t> (lag - lagMin)] = s;
        top = std::max (top, s);
    }

    // First local maximum that reaches 90 % of the best value (avoids picking a multiple of the period).
    for (int lag = lagMin + 1; lag < lagMax; ++lag)
    {
        const double v = value[static_cast<size_t> (lag - lagMin)];

        if (v >= 0.9 * top && v >= value[static_cast<size_t> (lag - lagMin - 1)] && v >= value[static_cast<size_t> (lag - lagMin + 1)])
            return lag;
    }

    return lagMin;
}

double spectralCentroid (const std::vector<float>& v, int start)
{
    constexpr int order = 13, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (2 * n, 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (twoPi * i / n);
        data[static_cast<size_t> (i)] = static_cast<float> (w * v[static_cast<size_t> (start + i)]);
    }

    fft.performRealOnlyForwardTransform (data.data(), true);

    double num = 0.0, den = 0.0;

    for (int k = 1; k < n / 2; ++k)
    {
        const double f = k * sr / n;

        if (f < 100.0 || f > 6000.0)
            continue;

        const double m = std::hypot (static_cast<double> (data[static_cast<size_t> (2 * k)]),
                                     static_cast<double> (data[static_cast<size_t> (2 * k + 1)]));
        num += f * m;
        den += m;
    }

    return num / den;
}

std::vector<float> runVoice (const std::vector<float>& in, float pitch, float formant)
{
    SpectralVoice v;
    v.prepare (sr);
    v.setShift (pitch, formant);

    std::vector<float> out (in.size());

    for (size_t i = 0; i < in.size(); ++i)
        out[i] = v.processSample (in[i]);

    return out;
}

bool allFinite (const std::vector<float>& v, float limit)
{
    for (float s : v)
        if (! std::isfinite (s) || std::abs (s) > limit)
            return false;

    return true;
}

void testSpectralVoice()
{
    const int n = static_cast<int> (sr * 2.0);
    const auto in = sine (440.0, 0.5, n);

    const auto id = runVoice (in, 0.0f, 0.0f);
    {
        juce::Random rng (7);
        std::vector<float> noise (static_cast<size_t> (n));

        for (auto& v : noise)
            v = rng.nextFloat() - 0.5f;

        const auto delayed = runVoice (noise, 0.0f, 0.0f);
        const int lag = bestLag (noise, delayed, 20000, 40000, 1200, 2600);
        check (lag == SpectralVoice::latency, fmt ("identity latency is %.0f samples (expected %.0f)", lag, SpectralVoice::latency));
    }

    check (std::abs (rms (id, 30000, 60000) / rms (in, 30000, 60000) - 1.0) < 0.05,
           fmt ("identity gain %.3f (expected 1.0 +- 0.05)", rms (id, 30000, 60000) / rms (in, 30000, 60000)));

    const auto up = runVoice (in, 12.0f, 0.0f);
    const double fUp = zeroCrossingFrequency (up, 40000, 80000);
    check (std::abs (fUp - 880.0) < 12.0, fmt ("+12 st turns 440 Hz into %.1f Hz (expected 880)", fUp));

    const auto down = runVoice (in, -5.0f, 0.0f);
    const double fDown = zeroCrossingFrequency (down, 40000, 80000);
    check (std::abs (fDown - 329.6) < 8.0, fmt ("-5 st turns 440 Hz into %.1f Hz (expected 329.6)", fDown));

    const auto voiced = vowel (n);
    const double base = spectralCentroid (voiced, 30000);

    const auto formantUp = runVoice (voiced, 0.0f, 4.0f);
    const double cUp = spectralCentroid (formantUp, 30000 + SpectralVoice::latency);
    check (cUp / base > 1.10 && cUp / base < 1.50, fmt ("formant +4 st moves the spectral centroid by x%.2f (expected 1.1 to 1.5)", cUp / base));
    const int f0lag = bestLag (formantUp, formantUp, 40000, 60000, 150, 600);
    check (std::abs (f0lag - sr / 150.0) < 4.0, fmt ("formant shift keeps pitch: period %.0f samples (expected 294)", f0lag));

    const auto pitchUp = runVoice (voiced, 7.0f, 0.0f);
    const double lvl = rms (pitchUp, 40000, 80000) / rms (voiced, 40000, 80000);
    check (lvl > 0.5 && lvl < 2.0, fmt ("+7 st on a voice keeps the level (ratio %.2f, expected 0.5 to 2.0)", lvl));
    const int p7lag = bestLag (pitchUp, pitchUp, 40000, 60000, 100, 600);
    check (std::abs (p7lag - sr / (150.0 * std::pow (2.0, 7.0 / 12.0))) < 4.0,
           fmt ("+7 st period %.0f samples (expected 196)", p7lag));
    const double cPitch = spectralCentroid (pitchUp, 30000 + SpectralVoice::latency);
    check (cPitch / base < 1.30, fmt ("formants stay put when pitch shifts: centroid x%.2f (< 1.30; would be about 1.5 without preservation)", cPitch / base));

    check (allFinite (runVoice (in, 24.0f, -12.0f), 4.0f) && allFinite (runVoice (in, -24.0f, 12.0f), 4.0f),
           "extreme pitch/formant stays finite and bounded");
}

void testChop()
{
    const int n = static_cast<int> (sr * 2.0);
    auto run = [&] (int steps)
    {
        auto l = sine (1000.0, 0.5, n);
        auto r = l;
        ChopEngine chop;
        chop.prepare (sr);
        chop.process (l.data(), r.data(), n, true, 8.0f, steps, 0.2f, 1.0f);

        double lo = 1.0e9, hi = 0.0;

        for (int a = 4000; a + 441 < n; a += 441)
        {
            const double e = rms (l, a, a + 441);
            lo = std::min (lo, e);
            hi = std::max (hi, e);
        }

        return std::pair<double, double> { lo, hi };
    };

    const auto p16 = run (16);
    check (p16.first < 0.05 && p16.second > 0.3, fmt ("16-step chop opens and closes (min %.3f, max %.3f)", p16.first, p16.second));
    const auto p1 = run (1);
    check (p1.first > 0.3, fmt ("1-step chop never closes (min %.3f)", p1.first));
}

void testEffects()
{
    const int n = static_cast<int> (sr);

    {
        std::vector<float> l (static_cast<size_t> (n), 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        EffectsEngine fx;
        fx.prepare (sr);
        fx.process (l.data(), r.data(), n, true, 0.0f, 1.0f, 0.0f, 1.0f, 120.0f);

        int idx = 0;
        float peak = 0.0f;

        for (int i = 5000; i < 25000; ++i)
            if (std::abs (l[static_cast<size_t> (i)]) > peak)
            {
                peak = std::abs (l[static_cast<size_t> (i)]);
                idx = i;
            }

        check (std::abs (idx - 16537) <= 3 && peak > 0.2f, fmt ("delay echo at sample %.0f (expected 16537), level %.2f", idx, peak));
    }

    {
        const int m = 4000;
        std::vector<float> l (static_cast<size_t> (m)), r (static_cast<size_t> (m));

        for (int i = 0; i < m; ++i)
        {
            l[static_cast<size_t> (i)] = 0.5f;
            r[static_cast<size_t> (i)] = -0.2f;
        }

        EffectsEngine fx;
        fx.prepare (sr);
        fx.process (l.data(), r.data(), m, true, 0.0f, 0.0f, 0.0f, 0.0f, 120.0f);
        check (std::abs (l[3900] - r[3900]) < 1.0e-3f, "width 0 collapses the signal to mono");
    }

    {
        auto l = sine (440.0, 0.5, n);
        auto r = l;
        const double before = rms (l, 5000, n);
        EffectsEngine fx;
        fx.prepare (sr);
        fx.process (l.data(), r.data(), n, true, 0.0f, 0.0f, 0.0f, 1.0f, 120.0f);
        check (std::abs (rms (l, 5000, n) / before - 1.0) < 0.03, fmt ("effects with all amounts at zero are transparent (gain %.3f)", rms (l, 5000, n) / before));
    }

    {
        auto l = sine (220.0, 1.0, n);
        auto r = l;
        EffectsEngine fx;
        fx.prepare (sr);
        fx.process (l.data(), r.data(), n, true, 0.0f, 0.0f, 1.0f, 1.0f, 120.0f);
        double peak = 0.0;

        for (int i = 1000; i < n; ++i)
            peak = std::max (peak, static_cast<double> (std::abs (l[static_cast<size_t> (i)])));

        check (peak <= 1.01 && rms (l, 1000, n) > 0.85, fmt ("distortion is bounded (peak %.3f) and squares the wave (rms %.3f)", peak, rms (l, 1000, n)));
    }

    {
        std::vector<float> l (static_cast<size_t> (n), 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        EffectsEngine fx;
        fx.prepare (sr);
        fx.process (l.data(), r.data(), n, true, 1.0f, 0.0f, 0.0f, 1.0f, 120.0f);
        check (rms (l, 3000, 20000) > 1.0e-4, fmt ("reverb produces a tail (rms %.5f)", rms (l, 3000, 20000)));
    }
}

void testSpatial()
{
    const int n = static_cast<int> (sr * 3.0);

    {
        std::vector<float> l (static_cast<size_t> (n), 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        SpatialEngine sp;
        sp.prepare (sr);
        sp.process (l.data(), r.data(), n, true, 0.8f, 0.7f, 0.0f, 1.0f);

        const double early = rms (l, 13230, 26460);
        const double late = rms (l, 66150, 79380);
        check (early > 1.0e-5 && late < early && allFinite (l, 10.0f), fmt ("spatial tail decays (0.3 s %.6f, 1.5 s %.6f)", early, late));
    }

    {
        auto measure = [&] (float ducking)
        {
            auto in = sine (220.0, 0.5, n);
            auto l = in, r = in;
            SpatialEngine sp;
            sp.prepare (sr);
            sp.process (l.data(), r.data(), n, true, 0.7f, 0.5f, ducking, 1.0f);

            std::vector<float> diff (l.size());

            for (size_t i = 0; i < l.size(); ++i)
                diff[i] = l[i] - in[i];

            return rms (diff, 20000, n);
        };

        const double open = measure (0.0f), ducked = measure (1.0f);
        check (ducked < 0.5 * open, fmt ("ducking lowers the tail while the input plays (%.4f -> %.4f)", open, ducked));
    }
}

void testEngine()
{
    const int n = static_cast<int> (sr * 3.0);
    juce::Random rng (42);
    std::vector<float> noise (static_cast<size_t> (n));

    for (auto& s : noise)
        s = rng.nextFloat() - 0.5f;

    auto runEngine = [&] (const EngineParams& p)
    {
        VoxEngine engine;
        engine.prepare (sr, 512);

        juce::AudioBuffer<float> buf (2, n);

        for (int i = 0; i < n; ++i)
        {
            buf.setSample (0, i, noise[static_cast<size_t> (i)]);
            buf.setSample (1, i, noise[static_cast<size_t> (i)]);
        }

        const int sizes[] = { 1000, 333, 64, 2048, 477 };
        int pos = 0, k = 0;

        while (pos < n)
        {
            const int len = std::min (sizes[k++ % 5], n - pos);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, len);
            engine.process (view, p);
            pos += len;
        }

        std::vector<float> out (static_cast<size_t> (n));

        for (int i = 0; i < n; ++i)
            out[static_cast<size_t> (i)] = buf.getSample (0, i);

        return out;
    };

    {
        EngineParams p;
        p.mix = 0.0f;
        p.fxReverb = p.fxDelay = p.spMix = 1.0f;
        const auto out = runEngine (p);
        double maxDiff = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const float expected = i >= VoxEngine::latencySamples ? noise[static_cast<size_t> (i - VoxEngine::latencySamples)] : 0.0f;
            maxDiff = std::max (maxDiff, static_cast<double> (std::abs (out[static_cast<size_t> (i)] - expected)));
        }

        check (maxDiff < 1.0e-6, fmt ("mix 0 returns the input delayed by exactly the reported latency (max diff %.2e)", maxDiff));
    }

    {
        EngineParams p;
        p.power = false;
        const auto out = runEngine (p);
        double maxDiff = 0.0;

        for (int i = VoxEngine::latencySamples; i < n; ++i)
            maxDiff = std::max (maxDiff, static_cast<double> (std::abs (out[static_cast<size_t> (i)] - noise[static_cast<size_t> (i - VoxEngine::latencySamples)])));

        check (maxDiff < 1.0e-6, fmt ("power off passes the delayed dry signal (max diff %.2e)", maxDiff));
    }

    {
        EngineParams p;
        p.mix = 1.0f;
        p.pitch = 24.0f;
        p.formant = -12.0f;
        p.chopRate = 32.0f;
        p.chopSteps = 64;
        p.trPitch = -24.0f;
        p.trFormant = 12.0f;
        p.trRobot = p.trGrain = 1.0f;
        p.fxReverb = p.fxDelay = p.fxDistort = 1.0f;
        p.fxWidth = 2.0f;
        p.spSize = p.spDensity = p.spDucking = p.spMix = 1.0f;
        p.bpm = 40.0f;
        check (allFinite (runEngine (p), 50.0f), "every control at its extreme stays finite and bounded");
    }
}

void testGeneratedInput()
{
    const int len = 20000, n = 30000;
    juce::Random rng (11);
    std::vector<float> gl (static_cast<size_t> (len)), gr (static_cast<size_t> (len));

    for (int i = 0; i < len; ++i)
    {
        gl[static_cast<size_t> (i)] = (rng.nextFloat() - 0.5f) * 0.5f;
        gr[static_cast<size_t> (i)] = (rng.nextFloat() - 0.5f) * 0.5f;
    }

    auto run = [&] (bool play, int startPos)
    {
        VoxEngine engine;
        engine.prepare (sr, 512);

        EngineParams p;
        p.mix = 1.0f;
        p.chopOn = false;

        GeneratedInput gen;
        gen.left = gl.data();
        gen.right = gr.data();
        gen.length = len;
        gen.startPos = startPos;
        gen.play = play;

        juce::AudioBuffer<float> buf (2, n);
        buf.clear();
        engine.process (buf, p, gen);

        std::vector<float> out (static_cast<size_t> (n));

        for (int i = 0; i < n; ++i)
            out[static_cast<size_t> (i)] = buf.getSample (0, i);

        return out;
    };

    {
        const int start = 15000;   // starts near the end of the loop, so the block wraps around
        const auto out = run (true, start);
        double maxDiff = 0.0;

        for (int i = 1000; i < n - VoxEngine::latencySamples; ++i)
        {
            const float expected = gl[static_cast<size_t> ((start + i) % len)];
            maxDiff = std::max (maxDiff, static_cast<double> (std::abs (out[static_cast<size_t> (i + VoxEngine::latencySamples)] - expected)));
        }

        check (maxDiff < 2.0e-3, fmt ("generated vocal plays from the requested loop position and wraps (max diff %.2e)", maxDiff));
    }

    {
        const auto out = run (false, 0);
        check (rms (out, 0, n) < 1.0e-6, "generated vocal is silent while the transport is stopped");
    }
}

//==============================================================================
// Vocal generator
double periodicity (const std::vector<float>& x, int a, int b)
{
    double energy = 0.0;

    for (int i = a; i < b; ++i)
        energy += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];

    double best = 0.0;

    for (int lag = 80; lag <= 600; ++lag)
    {
        double s = 0.0;

        for (int i = a; i < b - lag; ++i)
            s += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i + lag)];

        best = std::max (best, s);
    }

    return best / std::max (1.0e-12, energy);
}

std::unique_ptr<epinox::GeneratedClip> renderVowel (char vowel, float midi, int voiceType)
{
    epinox::voice::Composition comp;
    epinox::voice::Syllable s;
    s.vowel = vowel;
    comp.syllables.push_back (s);
    comp.notes.push_back ({ 0.0, 8.0, midi, 1.0f, 0 });
    comp.loopBeats = 8.0;

    epinox::GenerationRequest rq;
    rq.voiceType = voiceType;
    rq.emotion = 9;       // Robotic emotion: no vibrato, no scoop
    rq.style = 5;
    rq.bpm = 120.0;
    rq.sampleRate = sr;
    return epinox::voice::VocalSynth::render (comp, rq, {});
}

// Frequency of the strongest spectral peak inside [lo, hi] Hz, measured on a steady part of the clip.
double peakFrequency (const std::vector<float>& v, double lo, double hi)
{
    constexpr int order = 15, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (2 * n, 0.0f);

    for (int i = 0; i < n; ++i)
        data[static_cast<size_t> (i)] = static_cast<float> ((0.5 - 0.5 * std::cos (twoPi * i / n)) * v[static_cast<size_t> (30000 + i)]);

    fft.performRealOnlyForwardTransform (data.data(), true);

    double best = 0.0, bestF = 0.0;

    for (int k = 1; k < n / 2; ++k)
    {
        const double f = k * sr / n;

        if (f < lo || f > hi)
            continue;

        const double m = std::hypot (static_cast<double> (data[static_cast<size_t> (2 * k)]), static_cast<double> (data[static_cast<size_t> (2 * k + 1)]));

        if (m > best)
        {
            best = m;
            bestF = f;
        }
    }

    return bestF;
}

void testVoiceGenerator()
{
    using epinox::voice::PhraseParser;

    check (PhraseParser::parse ("Take me higher").size() == 4, "parser: 'Take me higher' has 4 syllables");
    check (PhraseParser::parse ("Hello world").size() == 3, "parser: 'Hello world' has 3 syllables");
    check (PhraseParser::parse ("Eu vou te levar pra longe daqui").size() == 10, "parser: Portuguese phrase has 10 syllables");
    check (PhraseParser::parse ("Coração").size() == 3, "parser: accents are handled ('Coração' = 3 syllables)");
    check (PhraseParser::parse ("").empty() && PhraseParser::parse ("123 !!! ?").empty(), "parser: no letters means no syllables");

    {
        const auto a = renderVowel ('a', 57.0f, 1);
        const int period = bestLag (a->left, a->left, 44100, 88200, 150, 300);
        const double f0 = sr / period;
        check (std::abs (f0 - 220.0) < 3.0, fmt ("sung A3 has f0 %.1f Hz (expected 220)", f0));

        const auto high = renderVowel ('a', 69.0f, 0);
        const int period2 = bestLag (high->left, high->left, 44100, 88200, 60, 150);
        check (std::abs (sr / period2 - 440.0) < 8.0, fmt ("sung A4 has f0 %.1f Hz (expected 440)", sr / period2));
    }

    {
        const auto a = renderVowel ('a', 45.0f, 1);
        const auto i = renderVowel ('i', 45.0f, 1);
        const double f1a = peakFrequency (a->left, 150.0, 1500.0);
        const double f1i = peakFrequency (i->left, 150.0, 1500.0);
        const double f2i = peakFrequency (i->left, 1500.0, 2700.0);   // F2 band; F3 of /i/ (about 3000 Hz) lies above it
        check (f1a > 580.0 && f1a < 880.0, fmt ("vowel 'a' has its first formant near %.0f Hz (expected 730)", f1a));
        check (f1i > 120.0 && f1i < 420.0, fmt ("vowel 'i' has its first formant near %.0f Hz (expected 270)", f1i));
        check (f2i > 2000.0 && f2i < 2600.0, fmt ("vowel 'i' has its second formant near %.0f Hz (expected 2290)", f2i));
    }

    {
        epinox::GenerationRequest r;
        r.phrase = "mama";
        r.sampleRate = sr;
        r.style = 0;
        r.voiceType = 1;
        const auto voiced = epinox::voice::renderPhrase (r);
        r.voiceType = 7;
        const auto whisper = epinox::voice::renderPhrase (r);
        const int n = voiced->length();
        const double pv = periodicity (voiced->left, n / 8, n / 8 + 8000);
        const double pw = periodicity (whisper->left, n / 8, n / 8 + 8000);
        check (pv > 0.7 && pw < 0.4, fmt ("voiced periodicity %.2f vs whisper %.2f (expected > 0.7 and < 0.4)", pv, pw));
    }

    {
        // Legato, Body and Layers: more connected, warmer, thicker.
        auto render = [&] (float legato, float body, float layers)
        {
            epinox::GenerationRequest q;
            q.phrase = "take me higher tonight";
            q.sampleRate = sr;
            q.style = 7;                  // Chopped: lots of gaps to close
            q.legato = legato;
            q.body = body;
            q.layers = layers;
            return epinox::voice::renderPhrase (q);
        };

        auto active = [] (const std::vector<float>& x)
        {
            const int win = 441;
            int on = 0, total = 0;

            for (size_t i = 0; i + static_cast<size_t> (win) < x.size(); i += static_cast<size_t> (win))
            {
                double e = 0.0;

                for (int j = 0; j < win; ++j)
                    e += static_cast<double> (x[i + static_cast<size_t> (j)]) * x[i + static_cast<size_t> (j)];

                on += (std::sqrt (e / win) > 0.01) ? 1 : 0;
                ++total;
            }

            return static_cast<double> (on) / juce::jmax (1, total);
        };

        auto lowShare = [] (const std::vector<float>& x)
        {
            double lp = 0.0, eLow = 0.0, eAll = 0.0;
            const double a = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 300.0 / 44100.0);

            for (auto v : x)
            {
                lp += a * (static_cast<double> (v) - lp);
                eLow += lp * lp;
                eAll += static_cast<double> (v) * v;
            }

            return eLow / juce::jmax (1.0e-9, eAll);
        };

        const auto loose = render (0.0f, 0.0f, 0.0f);
        const auto tied = render (1.0f, 0.0f, 0.0f);
        check (loose != nullptr && tied != nullptr && active (tied->left) > active (loose->left) + 0.05,
               fmt ("legato fills the gaps: active time %.2f -> %.2f", active (loose->left), active (tied->left)));

        const auto thin = render (0.5f, 0.0f, 0.0f);
        const auto warm = render (0.5f, 1.0f, 0.0f);
        check (lowShare (warm->left) > lowShare (thin->left) * 1.15,
               fmt ("body adds low-mid warmth: share below 300 Hz %.3f -> %.3f", lowShare (thin->left), lowShare (warm->left)));

        const auto single = render (0.5f, 0.0f, 0.0f);
        const auto layered = render (0.5f, 0.0f, 1.0f);
        double side = 0.0, mid = 0.0, side0 = 0.0, mid0 = 0.0;

        for (size_t i = 0; i < layered->left.size(); ++i)
        {
            side += std::pow (layered->left[i] - layered->right[i], 2.0);
            mid += std::pow (layered->left[i] + layered->right[i], 2.0);
            side0 += std::pow (single->left[i] - single->right[i], 2.0);
            mid0 += std::pow (single->left[i] + single->right[i], 2.0);
        }

        check (side / mid > 2.0 * (side0 / juce::jmax (1.0e-9, mid0)) + 1.0e-4,
               fmt ("layers widen the voice: side/mid %.4f -> %.4f", side0 / juce::jmax (1.0e-9, mid0), side / mid));
    }

    {
        // Polish and Air: a more even level, calmer sibilance, a brighter top.
        const char* polishPhrases[] = { "she sells sea shells at the seashore", "never let me go", "feel the rhythm tonight", "we are alone in the dark" };
        auto render = [&] (float polish, float air, int which = 0)
        {
            epinox::GenerationRequest q;
            q.phrase = polishPhrases[which];
            q.sampleRate = sr;
            q.style = 5;
            q.polish = polish;
            q.air = air;
            return epinox::voice::renderPhrase (q);
        };

        // Level evenness: standard deviation (dB) of the short-term level over the sung parts.
        auto levelSpread = [] (const std::vector<float>& x)
        {
            const size_t win = 2205;
            std::vector<double> db;
            double peakLevel = 0.0;

            for (size_t i = 0; i + win < x.size(); i += win)
            {
                double e = 0.0;

                for (size_t j = 0; j < win; ++j)
                    e += static_cast<double> (x[i + j]) * x[i + j];

                db.push_back (std::sqrt (e / static_cast<double> (win)));
                peakLevel = std::max (peakLevel, db.back());
            }

            std::vector<double> active;

            for (auto v : db)
                if (v > 0.25 * peakLevel)
                    active.push_back (20.0 * std::log10 (v));

            double mean = 0.0, var = 0.0;

            for (auto v : active) mean += v;
            mean /= static_cast<double> (active.empty() ? 1 : active.size());

            for (auto v : active) var += (v - mean) * (v - mean);
            return std::sqrt (var / static_cast<double> (active.empty() ? 1 : active.size()));
        };

        auto highShare = [] (const std::vector<float>& x)
        {
            double lp = 0.0, eHigh = 0.0, eAll = 0.0;
            const double a = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 5000.0 / 44100.0);

            for (auto v : x)
            {
                lp += a * (static_cast<double> (v) - lp);
                const double hp = static_cast<double> (v) - lp;
                eHigh += hp * hp;
                eAll += static_cast<double> (v) * v;
            }

            return eHigh / juce::jmax (1.0e-9, eAll);
        };

        const auto raw = render (0.0f, 0.0f);
        const auto pol = render (1.0f, 0.0f);
        const auto bright = render (0.0f, 1.0f);
        // The synth is already fairly even, so Polish must not make the level less even, and it must lower the peaks
        // compared with the average (crest factor).
        auto crestDb = [] (const std::vector<float>& x)
        {
            double pk = 0.0, e = 0.0;
            size_t cnt = 0;

            for (auto v : x) { pk = std::max (pk, (double) std::abs (v)); e += (double) v * v; cnt += std::abs (v) > 1.0e-5f ? 1 : 0; }

            return 20.0 * std::log10 (pk / std::sqrt (e / (double) std::max<size_t> (1, cnt)));
        };

        double rawSpread = 0.0, polSpread = 0.0, rawCrest = 0.0, polCrest = 0.0;

        for (int w = 0; w < 4; ++w)
        {
            const auto a = render (0.0f, 0.0f, w), b = render (1.0f, 0.0f, w);

            if (a && b)
            {
                rawSpread += levelSpread (a->left); polSpread += levelSpread (b->left);
                rawCrest += crestDb (a->left);      polCrest += crestDb (b->left);
            }
        }

        check (raw && pol && bright && polSpread <= rawSpread + 0.4 && polCrest < rawCrest,
               fmt ("polish lowers the crest factor %.2f -> %.2f dB (level spread change %.2f dB)",
                    rawCrest / 4.0, polCrest / 4.0, (polSpread - rawSpread) / 4.0));
        // Fricatives are kept natural (about 10 dB below the old level), so the share is small to begin with and polish
        // must not raise it.
        check (highShare (raw->left) < 0.03 && highShare (pol->left) <= highShare (raw->left) * 1.15,
               fmt ("sibilance stays low: share above 5 kHz %.4f -> %.4f with polish", highShare (raw->left), highShare (pol->left)));
        check (highShare (bright->left) > highShare (raw->left) * 1.1,
               fmt ("air opens the top: share above 5 kHz %.4f -> %.4f", highShare (raw->left), highShare (bright->left)));

        // Octave and a locked melody seed.
        epinox::GenerationRequest q;
        q.phrase = "hold on";
        q.sampleRate = sr;
        q.style = 5;
        const auto base = epinox::voice::renderPhrase (q);
        q.octave = 1;
        const auto up = epinox::voice::renderPhrase (q);
        check (base && up && base->left.size() == up->left.size(), "octave keeps the loop length");

        using namespace epinox::voice;
        const auto c0 = Composer::compose (PhraseParser::parse ("hold on"), 0, 5, 0, 0, 777, 9, 0, 0.0f, 0);
        const auto c1 = Composer::compose (PhraseParser::parse ("hold on"), 0, 5, 0, 0, 777, 9, 0, 0.0f, 1);
        check (! c0.notes.empty() && c0.notes.size() == c1.notes.size()
                   && std::abs ((c1.notes.front().midi - c0.notes.front().midi) - 12.0f) < 0.01f,
               "octave +1 raises every note by 12 semitones");

        q.octave = 0;
        q.variation = 1;
        const auto v1 = epinox::voice::renderPhrase (q);
        q.variation = 2;
        const auto v2 = epinox::voice::renderPhrase (q);
        q.melodyVariation = 1;
        const auto locked = epinox::voice::renderPhrase (q);
        check (v1->left != v2->left, "a new variation changes the vocal");
        check (locked->left != v1->left && locked->description == v1->description,
               "with the melody locked, only the voice details change");
    }

    {
        // Key and scale: every composed note belongs to the chosen scale, in every key, and the melody ends on the tonic.
        using namespace epinox::voice;
        bool allIn = true, endsOnTonic = true;
        int tested = 0;

        for (int scaleChoice = 1; scaleChoice <= 10; ++scaleChoice)
            for (int key = 0; key < 12; key += 3)
                for (int style : { 2, 5, 9 })
                {
                    auto comp = Composer::compose (PhraseParser::parse ("lets get lost together tonight"), 0, style, 0, 0,
                                                   12345, key, scaleChoice);
                    const auto notes = scaleIntervals (scaleChoice);
                    ++tested;

                    for (auto& n : comp.notes)
                    {
                        const int pc = ((static_cast<int> (std::lround (n.midi)) - key) % 12 + 12) % 12;
                        allIn = allIn && std::find (notes.begin(), notes.end(), pc) != notes.end();
                    }

                    if (style == 9 && ! comp.notes.empty() && comp.truncated == 0)
                        endsOnTonic = endsOnTonic && (((static_cast<int> (std::lround (comp.notes.back().midi)) - key) % 12 + 12) % 12) == 0;
                }

        check (allIn, fmt ("all notes of %.0f melodies stay in the chosen key and scale", static_cast<double> (tested)));
        check (endsOnTonic, "an experimental (leaping) melody resolves on the tonic");

        epinox::GenerationRequest k;
        k.phrase = "hold me";
        k.sampleRate = sr;
        k.key = 0;
        k.scale = 2;
        const auto inC = renderPhrase (k);
        k.key = 6;
        const auto inFs = renderPhrase (k);
        check (inC != nullptr && inFs != nullptr && inC->left != inFs->left, "changing the key changes the rendered vocal");
    }

    {
        epinox::GenerationRequest r;
        r.phrase = "Take me higher";
        r.sampleRate = sr;
        r.bpm = 124.0;
        const auto a = epinox::voice::renderPhrase (r);
        const auto b = epinox::voice::renderPhrase (r);
        bool same = a->left == b->left && a->right == b->right;
        r.variation = 1;
        const auto c = epinox::voice::renderPhrase (r);
        check (same, "same request renders identical audio (projects reload exactly)");
        check (c->left != a->left, "a new variation renders different audio");

        const int expected = static_cast<int> (std::ceil (a->loopBeats * 60.0 / 124.0 * sr));
        check (a->length() == expected && std::fmod (a->loopBeats, 4.0) == 0.0,
               fmt ("clip loops over whole bars (%.0f beats, %.0f samples)", a->loopBeats, a->length()));
    }

    {
        int silent = 0, bad = 0, total = 0;

        auto run = [&] (int genre, int voice, int emotion, int style)
        {
            epinox::GenerationRequest r;
            r.phrase = "Rise up tonight";
            r.sampleRate = sr;
            r.genre = genre;
            r.voiceType = voice;
            r.emotion = emotion;
            r.style = style;
            r.bpm = 128.0;
            const auto c = epinox::voice::renderPhrase (r);
            ++total;

            if (c == nullptr || c->length() == 0)
            {
                ++bad;
                return;
            }

            float peak = 0.0f;

            for (size_t i = 0; i < c->left.size(); ++i)
            {
                if (! std::isfinite (c->left[i]) || ! std::isfinite (c->right[i]))
                {
                    ++bad;
                    return;
                }

                peak = std::max (peak, std::abs (c->left[i]));
            }

            if (peak < 0.3f || peak > 0.71f)
                ++silent;
        };

        for (int i = 0; i < 10; ++i) run (i, 0, 0, 5);
        for (int i = 0; i < 10; ++i) run (0, i, 0, 5);
        for (int i = 0; i < 12; ++i) run (0, 0, i, 5);
        for (int i = 0; i < 10; ++i) run (0, 0, 0, i);

        check (bad == 0 && silent == 0, fmt ("all %.0f genre/voice/emotion/style choices render finite audio at the target level (bad %.0f, off-level %.0f)", total, bad, silent));
    }

    {
        epinox::GenerationRequest r;
        r.phrase = "this is a very long phrase that does not fit in eight bars at all so it must be truncated safely";
        r.sampleRate = sr;
        r.style = 1;
        const auto c = epinox::voice::renderPhrase (r);
        check (c != nullptr && c->truncated > 0 && c->loopBeats <= 32.0, "very long phrases are truncated to the 8 bar limit");

        int calls = 0;
        r.phrase = "cancel me";
        const auto cancelled = epinox::voice::renderPhrase (r, [&] { ++calls; return true; });
        check (cancelled == nullptr && calls > 0, "rendering can be cancelled");
    }
}

void testVoiceEngines()
{
    using namespace epinox;
    std::printf ("\n[Voice engines]\n");
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("epinox_voices_test");
    root.deleteRecursively();

    auto mk = [&] (const char* dir, const juce::String& manifest, bool model)
    {
        auto d = root.getChildFile (dir);
        d.createDirectory();
        if (manifest.isNotEmpty()) d.getChildFile ("voicepack.json").replaceWithText (manifest);
        if (model) d.getChildFile ("a.onnx").replaceWithText ("x");
    };
    mk ("good", R"({"id":"good","name":"Good","type":"onnx","license":"Commercial use allowed","models":["a.onnx"]})", true);
    mk ("nolic", R"({"id":"nolic","name":"No Lic","type":"onnx","license":"","models":["a.onnx"]})", true);
    mk ("nomodel", R"({"id":"nomodel","name":"No Model","type":"onnx","license":"ok","models":["a.onnx"]})", false);
    mk ("nojson", "", false);
    mk ("badid", R"({"id":"builtin","name":"X","type":"onnx","license":"ok","models":["a.onnx"]})", true);

    const auto packs = engines::scanPacks (root);
    int valid = 0;
    for (auto& p : packs) valid += p.valid ? 1 : 0;
    check (packs.size() == 5 && valid == 1, fmt ("5 packs found, only the complete one is valid (%.0f valid)", static_cast<double> (valid)));

    const auto list = engines::listEngines (root);
    check (list.size() == 2 && list[0].id == "builtin" && list[1].id == "good", "engine list = built-in + the valid pack");

    GenerationRequest r;
    r.phrase = "hello world"; r.sampleRate = 44100.0; r.bpm = 120; r.engineId = "good";
    auto a = engines::render (r, [] { return false; }, root);
    check (a.clip != nullptr && a.note.isNotEmpty(), "pack without runtime falls back to built-in and says so");
    r.engineId = "ghost";
    auto b = engines::render (r, [] { return false; }, root);
    check (b.clip != nullptr && b.note.contains ("not found"), "unknown pack falls back and says so");
    r.engineId = "builtin";
    auto c = engines::render (r, [] { return false; }, root);
    check (c.clip != nullptr && c.note.isEmpty(), "built-in renders with no notice");
    root.deleteRecursively();
}

void testMelodySearch()
{
    using namespace epinox;
    std::printf ("\n[Melody search]\n");
    const char* phrases[] = { "never let me go", "feel the rhythm tonight", "fly away with me", "hold on", "we are alone in the dark" };
    int better = 0, total = 0, inScale = 0, endsHome = 0;
    double plainSum = 0.0, searchedSum = 0.0;

    for (auto* ph : phrases)
        for (int genre : { 0, 3, 7 })
        {
            GenerationRequest r;
            r.phrase = ph; r.genre = genre; r.style = 0; r.sampleRate = 44100.0; r.bpm = 124; r.key = 9; r.scale = 1;
            const juce::int64 seed = juce::String (ph).hashCode64();
            auto plain = voice::Composer::compose (voice::PhraseParser::parse (r.phrase), r.genre, r.style, r.emotion, r.voiceType, seed, r.key, r.scale, 0.0f, 0);
            voice::MelodySearch::Context ctx;
            ctx.rootPc = plain.rootPc; ctx.scalePcs = plain.scalePcs; ctx.centreMidi = plain.centreMidi;
            auto searched = plain;
            voice::MelodySearch::refine (searched, ctx, seed);
            const double a = voice::MelodySearch::score (plain, ctx), b = voice::MelodySearch::score (searched, ctx);
            plainSum += a; searchedSum += b; ++total; better += b >= a ? 1 : 0;

            bool ok = true;
            for (auto& n : searched.notes)
                ok = ok && std::find (ctx.scalePcs.begin(), ctx.scalePcs.end(), (((int) std::lround (n.midi) - ctx.rootPc) % 12 + 12) % 12) != ctx.scalePcs.end();
            inScale += ok ? 1 : 0;

            auto again = plain;
            voice::MelodySearch::refine (again, ctx, seed);
            bool same = again.notes.size() == searched.notes.size();
            for (size_t i = 0; same && i < again.notes.size(); ++i) same = again.notes[i].midi == searched.notes[i].midi;
            check (same, "search is deterministic for a given seed");
            const auto last = (((int) std::lround (searched.notes.back().midi) - ctx.rootPc) % 12 + 12) % 12;
            endsHome += (last == ctx.scalePcs[0] || last == ctx.scalePcs[4 % ctx.scalePcs.size()]) ? 1 : 0;
        }

    check (better == total, fmt ("search never scores below the plain melody (%.0f of %.0f)", (double) better, (double) total));
    check (searchedSum > plainSum + total * 2.0, fmt ("mean musicality score rises %.1f -> %.1f", plainSum / total, searchedSum / total));
    check (inScale == total, "searched melodies stay inside the chosen scale");
    check (endsHome >= total * 0.8, fmt ("most searched phrases end on the root or fifth (%.0f of %.0f)", (double) endsHome, (double) total));
}

void testKeyDetector()
{
    using namespace epinox::dsp;
    std::printf ("\n[Key detector]\n");

    // Chord progressions with a few harmonics per note (MIDI notes). Minor keys use i-iv-V-i so they are not mistaken for their relative major.
    auto synth = [] (std::vector<std::vector<int>> chords, double sr)
    {
        std::vector<float> x (static_cast<size_t> (sr * 6.0), 0.0f);
        const size_t per = x.size() / chords.size();

        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto& ch = chords[std::min (chords.size() - 1, i / per)];
            double v = 0.0;

            for (int m : ch)
                for (int h = 1; h <= 4; ++h)
                    v += std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * std::pow (2.0, (m - 69) / 12.0) * h * (double) i / sr) / h;

            x[i] = static_cast<float> (0.1 * v / (double) ch.size());
        }

        return x;
    };

    struct Case { const char* name; std::vector<std::vector<int>> chords; int tonic; bool minor; };
    const Case cases[] = {
        { "A minor",  { { 57, 60, 64 }, { 50, 53, 57 }, { 52, 56, 59 }, { 57, 60, 64 } }, 9, true },
        { "C major",  { { 48, 52, 55 }, { 55, 59, 62 }, { 57, 60, 64 }, { 53, 57, 60 } }, 0, false },
        { "F# minor", { { 54, 57, 61 }, { 59, 62, 66 }, { 61, 65, 68 }, { 54, 57, 61 } }, 6, true },
        { "D major",  { { 50, 54, 57 }, { 57, 61, 64 }, { 59, 62, 66 }, { 55, 59, 62 } }, 2, false },
    };

    for (double sr : { 44100.0, 48000.0 })
        for (auto& c : cases)
        {
            const auto r = KeyDetector::detect (synth (c.chords, sr), sr);
            check (r.valid && r.tonic == c.tonic && r.minor == c.minor,
                   std::string ("detects ") + c.name + " at " + std::to_string ((int) sr) + " Hz");
        }

    check (! KeyDetector::detect (std::vector<float> (44100 * 4, 0.0f), 44100.0).valid, "silence gives no key");
    check (! KeyDetector::detect (std::vector<float> (500, 0.1f), 44100.0).valid, "too little audio gives no key");
}

void testMorph()
{
    using namespace epinox::voice;
    std::printf ("\n[Voice morph]\n");
    const auto f = voiceProfile (0), m = voiceProfile (1);
    const auto h = morphVoice (0, 1, 0.5f);
    check (morphVoice (0, 1, 0.0f).centreMidi == f.centreMidi && morphVoice (0, -1, 0.7f).centreMidi == f.centreMidi, "no morph leaves the voice as it was");
    check (morphVoice (0, 1, 1.0f).centreMidi == m.centreMidi && morphVoice (0, 1, 1.0f).formantScale == m.formantScale, "full morph is the target voice");
    check (h.centreMidi > m.centreMidi && h.centreMidi < f.centreMidi && h.formantScale > m.formantScale && h.formantScale < f.formantScale,
           "half morph sits between the two voices");
    check (morphVoice (0, 4, 0.4f).robotic == false && morphVoice (0, 4, 0.6f).robotic, "robotic switches on past the half-way point");

    auto meanMidi = [] (float amt)
    {
        auto c = Composer::compose (PhraseParser::parse ("never let me go"), 0, 0, 0, 0, 42, 9, 1, 0.0f, 0, 1, amt);
        double sum = 0.0;
        for (auto& n : c.notes) sum += n.midi;
        return sum / (double) c.notes.size();
    };
    check (meanMidi (1.0f) < meanMidi (0.0f) - 5.0, fmt ("morphing female to male lowers the melody (%.1f -> %.1f)", meanMidi (0.0f), meanMidi (1.0f)));

    epinox::GenerationRequest r;
    r.phrase = "never let me go"; r.sampleRate = 44100.0; r.bpm = 120;
    auto a = renderPhrase (r);
    r.morphTo = 1; r.morph = 0.6f;
    auto b = renderPhrase (r);
    double diff = 0.0;
    for (size_t i = 0; a && b && i < std::min (a->left.size(), b->left.size()); ++i) diff += std::abs (a->left[i] - b->left[i]);
    check (a && b && diff > 1.0, "a morphed voice renders differently");
}

void testVoiceProfilePack()
{
    using namespace epinox;
    std::printf ("\n[Voice profile pack]\n");
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("epinox_profile_test");
    root.deleteRecursively();
    auto mk = [&] (const char* dir, const juce::String& text)
    {
        auto d = root.getChildFile (dir);
        d.createDirectory();
        d.getChildFile ("voicepack.json").replaceWithText (text);
    };
    const juce::String vowels = R"("vowels":{"a":[850,1300,2800],"e":[600,2100,2900],"i":[330,2500,3300],"o":[620,950,2700],"u":[350,1000,2600]})";
    mk ("mine", R"({"id":"mine","name":"My Voice","type":"profile","license":"Own recordings","voice":{"centreMidi":45,"breath":0.1,"vibratoHz":5.5,"vibratoCents":15,)" + vowels + "}}");
    mk ("badformant", R"({"id":"bad","name":"Bad","type":"profile","license":"x","voice":{"centreMidi":50,"vowels":{"a":[850,1300,2800],"e":[600,2100,2900],"i":[3000,2500,3300],"o":[620,950,2700],"u":[350,1000,2600]}}})");
    mk ("nocentre", R"({"id":"nc","name":"NC","type":"profile","license":"x","voice":{)" + vowels + "}}");
    mk ("novoice", R"({"id":"nv","name":"NV","type":"profile","license":"x"})");

    const auto packs = engines::scanPacks (root);
    int valid = 0;
    for (auto& p : packs) valid += p.valid ? 1 : 0;
    check (packs.size() == 4 && valid == 1, fmt ("only the complete profile is valid (%.0f of 4)", (double) valid));
    check (engines::listEngines (root).size() == 2, "the valid profile shows in the engine list");

    GenerationRequest r;
    r.phrase = "never let me go"; r.sampleRate = 44100.0; r.bpm = 120; r.engineId = "mine";
    auto mineR = engines::render (r, [] { return false; }, root);
    r.engineId = "builtin";
    auto plainR = engines::render (r, [] { return false; }, root);
    check (mineR.clip && plainR.clip && mineR.note.contains ("My Voice"), "a profile renders and says whose voice it is");

    double diff = 0.0;
    for (size_t i = 0; mineR.clip && plainR.clip && i < std::min (mineR.clip->left.size(), plainR.clip->left.size()); ++i)
        diff += std::abs (mineR.clip->left[i] - plainR.clip->left[i]);
    check (diff > 1.0, "the profile voice sounds different from the built-in one");

    // The melody follows the profile's range (centre note 45 against the female default 67).
    auto pack = engines::scanPacks (root);
    std::shared_ptr<const voice::CustomVoice> cv;
    for (auto& p : pack) if (p.valid) cv = p.custom;
    auto low = voice::Composer::compose (voice::PhraseParser::parse (r.phrase), 0, 0, 0, 0, 7, 9, 1, 0.0f, 0, -1, 0.0f, cv.get());
    auto high = voice::Composer::compose (voice::PhraseParser::parse (r.phrase), 0, 0, 0, 0, 7, 9, 1, 0.0f, 0);
    double lm = 0, hm = 0;
    for (auto& n : low.notes) lm += n.midi;
    for (auto& n : high.notes) hm += n.midi;
    check (cv && lm / low.notes.size() < hm / high.notes.size() - 8.0, "the melody sits in the profile's own range");
    root.deleteRecursively();
}

void testDynamics()
{
    using namespace epinox;
    std::printf ("\n[Phrase dynamics]\n");
    auto make = [] { return voice::Composer::compose (voice::PhraseParser::parse ("never let me go and feel the rhythm tonight"), 0, 0, 1, 0, 11, 9, 1, 0.6f, 0); };
    auto stats = [] (const voice::Composition& c)
    {
        double m = 0, v = 0;
        for (auto& n : c.notes) m += n.accent;
        m /= c.notes.size();
        for (auto& n : c.notes) v += (n.accent - m) * (n.accent - m);
        return std::pair<double, double> (m, std::sqrt (v / c.notes.size()));
    };
    auto a = make(), b = make(), c2 = make();
    const auto before = stats (a);
    voice::MelodySearch::shapeDynamics (b, 5, a.centreMidi);
    voice::MelodySearch::shapeDynamics (c2, 5, a.centreMidi);
    const auto after = stats (b);
    check (after.second > before.second * 1.1, fmt ("levels vary more between syllables (spread %.3f -> %.3f)", before.second, after.second));
    check (std::abs (after.first - before.first) < 0.06, fmt ("average level stays put (%.3f -> %.3f)", before.first, after.first));
    bool same = b.notes.size() == c2.notes.size();
    for (size_t i = 0; same && i < b.notes.size(); ++i) same = b.notes[i].accent == c2.notes[i].accent;
    check (same, "dynamics are deterministic for a seed");
    bool inRange = true;
    for (auto& n : b.notes) inRange = inRange && n.accent >= 0.6f && n.accent <= 1.5f;
    check (inRange, "accents stay in a safe range");

    GenerationRequest r;
    r.phrase = "never let me go"; r.sampleRate = 44100.0; r.bpm = 120; r.voiceType = 4;
    auto robot1 = voice::renderPhrase (r);
    auto robot2 = voice::renderPhrase (r);
    check (robot1 && robot2 && robot1->left == robot2->left, "robotic voice renders identically every time");
}

void testReverbQuality()
{
    std::printf ("\n[Reverb engineering]\n");
    const int n = static_cast<int> (sr * 8.0);

    auto ir = [&] (float size)
    {
        std::vector<float> l (static_cast<size_t> (n), 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        SpatialEngine sp;
        sp.prepare (sr);
        sp.process (l.data(), r.data(), n, true, size, 1.0f, 0.0f, 1.0f);
        return l;
    };

    // time (s) for the energy envelope to fall 30 dB below its early level
    auto t30 = [&] (const std::vector<float>& x)
    {
        const int w = static_cast<int> (sr * 0.05);
        const double ref = rms (x, static_cast<int> (sr * 0.1), static_cast<int> (sr * 0.1) + w);
        for (int i = static_cast<int> (sr * 0.2); i + w < n; i += w)
            if (rms (x, i, i + w) < ref * 0.0316)
                return i / sr;
        return n / sr;
    };

    const auto big = ir (1.0f), small = ir (0.2f);
    check (allFinite (big, 50.0f) && allFinite (small, 50.0f), "reverb stays finite at the longest and shortest sizes");
    check (t30 (big) < 4.0 && t30 (big) > t30 (small), fmt ("RT60 is capped and grows with size (%.2f s vs %.2f s)", t30 (big), t30 (small)));

    // a good diffuse tail has an even energy envelope: few fast ripples between 50 ms blocks
    const int w = static_cast<int> (sr * 0.02);
    double ripple = 0.0;
    int cnt = 0;
    for (int i = static_cast<int> (sr * 0.5); i + 2 * w < static_cast<int> (sr * 1.5); i += w)
    {
        const double a = rms (big, i, i + w), b = rms (big, i + w, i + 2 * w);
        if (a > 1.0e-9 && b > 1.0e-9) { ripple += std::abs (std::log (a / b)); ++cnt; }
    }
    check (cnt > 0 && ripple / cnt < 0.6, fmt ("tail envelope is smooth (mean ripple %.3f)", cnt ? ripple / cnt : 0.0));

    // sustained low tone must not build up a boom
    auto in = sine (110.0, 0.3, n);
    auto l = in, r = in;
    SpatialEngine sp;
    sp.prepare (sr);
    sp.process (l.data(), r.data(), n, true, 0.9f, 0.7f, 0.0f, 1.0f);
    check (allFinite (l, 20.0f) && rms (l, n / 2, n) < 3.0 * 0.3, fmt ("low tone through the room stays under control (%.3f)", rms (l, n / 2, n)));
}

void testLfo()
{
    using namespace epinox;
    std::printf ("\n[LFO]\n");
    const int n = static_cast<int> (sr * 4.0);

    auto run = [&] (const dsp::LfoParams& lp, const std::vector<float>& in)
    {
        dsp::VoxEngine eng;
        eng.prepare (sr, 512);
        dsp::EngineParams p;
        p.mix = 1.0f; p.trOn = true; p.fxOn = false; p.spOn = false; p.chopOn = false;
        p.lfo = lp;
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { b.setSample (0, i, in[static_cast<size_t> (i)]); b.setSample (1, i, in[static_cast<size_t> (i)]); }
        for (int s0 = 0; s0 < n; s0 += 512)
        {
            juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, s0, juce::jmin (512, n - s0));
            eng.process (blk, p);
        }
        std::vector<float> o (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i) o[static_cast<size_t> (i)] = b.getSample (0, i);
        return o;
    };

    const auto tone = sine (440.0, 0.4, n);

    // Off / zero depth: bit for bit the same as no LFO.
    dsp::LfoParams off;
    dsp::LfoParams zero; zero.on = true; zero.rateHz = 2.0f;
    check (run (off, tone) == run (zero, tone), "LFO with every depth at 0 changes nothing");

    // Volume LFO: the level swings at the LFO rate, between silent and full.
    dsp::LfoParams vol; vol.on = true; vol.shape = 0; vol.rateHz = 2.0f; vol.volume = 1.0f;
    const auto v = run (vol, tone);
    double lo = 1.0, hi = 0.0;
    for (int i = static_cast<int> (sr * 1.0); i + 2205 < n; i += 2205)
    {
        const double r2 = rms (v, i, i + 2205);
        lo = std::min (lo, r2); hi = std::max (hi, r2);
    }
    check (allFinite (v, 2.0f) && hi > 0.2 && lo < 0.05, fmt ("volume LFO swings the level (%.3f .. %.3f)", lo, hi));

    // Filter LFO on white noise: the brightness moves.
    std::vector<float> noise (static_cast<size_t> (n));
    juce::Random rnd (7);
    for (auto& x : noise) x = (rnd.nextFloat() - 0.5f) * 0.6f;
    dsp::LfoParams flt; flt.on = true; flt.shape = 1; flt.rateHz = 1.0f; flt.filter = 1.0f;
    const auto f = run (flt, noise);
    auto bright = [&] (int a, int b2)
    {
        double d = 0.0, e = 0.0;
        for (int i = a + 1; i < b2; ++i) { const double df = f[static_cast<size_t> (i)] - f[static_cast<size_t> (i - 1)]; d += df * df; e += f[static_cast<size_t> (i)] * f[static_cast<size_t> (i)]; }
        return e > 0.0 ? d / e : 0.0;
    };
    const int q = static_cast<int> (sr * 0.25);
    double bmin = 1e9, bmax = 0.0;
    for (int i = static_cast<int> (sr); i + q < n; i += q) { const double x = bright (i, i + q); bmin = std::min (bmin, x); bmax = std::max (bmax, x); }
    check (allFinite (f, 2.0f) && bmax > 2.0 * bmin, fmt ("filter LFO sweeps the brightness (%.3f .. %.3f)", bmin, bmax));

    // Pitch LFO: a held tone gets frequency modulation, so the zero-crossing rate moves around.
    dsp::LfoParams pit; pit.on = true; pit.shape = 0; pit.rateHz = 1.0f; pit.pitch = 1.0f;
    const auto pv = run (pit, tone);
    {
        double rlo = 1e9, rhi = 0.0;
        for (int i = static_cast<int> (sr * 2); i + 1024 < n; i += 1024) { const double q2 = rms (pv, i, i + 1024); rlo = std::min (rlo, q2); rhi = std::max (rhi, q2); }
        // zero crossings per window swing up and down with the vibrato
        auto zc = [&] (int a) { int c = 0; for (int i = a + 1; i < a + 2205; ++i) if ((pv[static_cast<size_t> (i - 1)] < 0) != (pv[static_cast<size_t> (i)] < 0)) ++c; return c; };
        int zmin = 1 << 30, zmax = 0;
        for (int i = static_cast<int> (sr * 2); i + 2205 < n; i += 551) { const int c = zc (i); zmin = std::min (zmin, c); zmax = std::max (zmax, c); }
        check (allFinite (pv, 2.0f) && rlo > 0.85 * rhi && zmax > zmin + 4,
               fmt ("pitch LFO is a clean vibrato: level %.3f .. %.3f, crossing swing %.0f", rlo, rhi, static_cast<double> (zmax - zmin)));
    }

    // Sync: 1/4 note at 120 BPM = 2 Hz.
    dsp::LfoParams sy; sy.sync = 8; sy.bpm = 120.0f;
    check (std::abs (dsp::LfoEngine::rateFor (sy) - 2.0f) < 1e-4f, "sync 1/4 at 120 BPM runs at 2 Hz");

    // Host lock: a synced LFO is a pure function of the host position, so two engines started at different times agree.
    {
        dsp::LfoEngine e1, e2; e1.prepare (sr); e2.prepare (sr);
        dsp::LfoParams lp; lp.on = true; lp.sync = 8; lp.bpm = 120.0f; lp.hostLocked = true; lp.hostPpq = 7.3;
        std::vector<float> a (512), b (512);
        e1.generate (512, lp, a.data());
        lp.hostPpq = 7.3;
        e2.generate (512, lp, b.data());
        double d = 0.0; for (size_t i = 0; i < 512; ++i) d = std::max (d, static_cast<double> (std::abs (a[i] - b[i])));
        lp.hostPpq = 7.3 + 1.0;   // one quarter note later = one full cycle for a 1/4 division
        std::vector<float> c (512); e2.generate (512, lp, c.data());
        double d2 = 0.0; for (size_t i = 0; i < 512; ++i) d2 = std::max (d2, static_cast<double> (std::abs (a[i] - c[i])));
        check (d < 1e-6 && d2 < 1e-5, fmt ("synced LFO follows the host position (%.2g, %.2g)", d, d2));
    }


    // Dotted and triplet divisions.
    {
        dsp::LfoParams t; t.bpm = 120.0f; t.sync = 10;   // 1/4 triplet
        dsp::LfoParams dd; dd.bpm = 120.0f; dd.sync = 9;  // 1/4 dotted
        check (std::abs (dsp::LfoEngine::rateFor (t) - 3.0f) < 1e-3f && std::abs (dsp::LfoEngine::rateFor (dd) - 4.0f / 3.0f) < 1e-3f,
               "triplet and dotted divisions run at the right rate");
    }

    // Phase offset: a quarter cycle later a sine starts at its peak.
    {
        dsp::LfoEngine e; e.prepare (sr);
        dsp::LfoParams lp; lp.on = true; lp.shape = 0; lp.rateHz = 1.0f; lp.phase = 0.25f;
        std::vector<float> a (4); e.generate (4, lp, a.data());
        check (std::abs (a[0] - 1.0f) < 1e-3f, "phase offset moves the start of the cycle");
    }

    // Tremolo reaches the tail of the reverb now that the LFO runs last.
    {
        std::vector<float> burst (static_cast<size_t> (n), 0.0f);
        for (int i = 0; i < 4000; ++i) burst[static_cast<size_t> (i)] = 0.4f * std::sin (2.0 * 3.14159265 * 330.0 * i / sr);
        dsp::VoxEngine eng; eng.prepare (sr, 512);
        dsp::EngineParams p; p.mix = 1.0f; p.trOn = false; p.fxOn = true; p.fxReverb = 0.8f; p.fxDelay = 0.0f; p.spOn = false;
        p.lfo.on = true; p.lfo.shape = 0; p.lfo.rateHz = 3.0f; p.lfo.volume = 1.0f;
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { b.setSample (0, i, burst[static_cast<size_t> (i)]); b.setSample (1, i, burst[static_cast<size_t> (i)]); }
        for (int s0 = 0; s0 < n; s0 += 512)
        {
            juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, s0, juce::jmin (512, n - s0));
            eng.process (blk, p);
        }
        std::vector<float> o (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i) o[static_cast<size_t> (i)] = b.getSample (0, i);
        double lo = 1.0, hi = 0.0;
        for (int i = static_cast<int> (sr * 0.5); i + 1100 < static_cast<int> (sr * 2.5); i += 1100)
        { const double r2 = rms (o, i, i + 1100); lo = std::min (lo, r2); hi = std::max (hi, r2); }
        check (hi > 1.0e-4 && lo < 0.35 * hi, fmt ("tremolo also moves the reverb tail (%.5f .. %.5f)", lo, hi));
    }

    // The vibrato reaches the tail: after the note stops, the reverb still carries the pitch movement.
    {
        std::vector<float> burst (static_cast<size_t> (n), 0.0f);
        for (int i = 0; i < 8000; ++i) burst[static_cast<size_t> (i)] = 0.4f * std::sin (2.0 * 3.14159265 * 330.0 * i / sr);
        auto tailZc = [&] (float pitchDepth)
        {
            dsp::VoxEngine eng; eng.prepare (sr, 512);
            dsp::EngineParams p; p.mix = 1.0f; p.trOn = false; p.fxOn = true; p.fxReverb = 0.9f; p.fxDelay = 0.0f; p.spOn = false;
            p.lfo.on = true; p.lfo.shape = 0; p.lfo.rateHz = 5.0f; p.lfo.pitch = pitchDepth;
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i) { b.setSample (0, i, burst[static_cast<size_t> (i)]); b.setSample (1, i, burst[static_cast<size_t> (i)]); }
            for (int s0 = 0; s0 < n; s0 += 512)
            {
                juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, s0, juce::jmin (512, n - s0));
                eng.process (blk, p);
            }
            // spread of the tail's zero-crossing rate between 0.3 s and 1.3 s
            int lo = 1 << 30, hi = 0;
            for (int i = static_cast<int> (sr * 0.3); i + 2205 < static_cast<int> (sr * 1.3); i += 551)
            {
                int c = 0;
                for (int k = i + 1; k < i + 2205; ++k) if ((b.getSample (0, k - 1) < 0) != (b.getSample (0, k) < 0)) ++c;
                lo = std::min (lo, c); hi = std::max (hi, c);
            }
            return hi - lo;
        };
        const int off = tailZc (0.0f), on = tailZc (1.0f);
        check (on > off + 2, fmt ("pitch LFO keeps moving the reverb tail (crossing spread %.0f vs %.0f)", static_cast<double> (on), static_cast<double> (off)));
    }

    // Fade-in: the LFO depth restarts from nothing with every new note and grows over the fade time.
    {
        dsp::LfoEngine e; e.prepare (sr);
        const int total = static_cast<int> (sr * 3.0);
        std::vector<float> sig (static_cast<size_t> (total), 0.0f), lfoV (static_cast<size_t> (total), 1.0f);
        for (int i = 0; i < total; ++i)
            if (i < sr * 1.0 || (i > sr * 1.5 && i < sr * 2.5)) sig[static_cast<size_t> (i)] = 0.3f * std::sin (0.06f * static_cast<float> (i));
        for (int s0 = 0; s0 < total; s0 += 512)
        {
            const int m = std::min (512, total - s0);
            e.applyFade (sig.data() + s0, sig.data() + s0, m, 500.0f, lfoV.data() + s0);
        }
        auto at = [&] (double t) { return lfoV[static_cast<size_t> (t * sr)]; };
        check (at (0.05) < 0.15f && at (0.25) > 0.15f && at (0.25) < 0.85f && at (0.8) > 0.99f,
               fmt ("fade-in grows over the fade time (%.2f, %.2f, %.2f)", at (0.05), at (0.25), at (0.8)));
        check (at (1.55) < 0.15f && at (2.3) > 0.99f, "fade-in restarts on the next note");
    }

    // Every shape stays inside -1..1 and the random one moves.
    for (int sh = 0; sh < 7; ++sh)
    {
        dsp::LfoEngine e; e.prepare (sr);
        dsp::LfoParams lp; lp.on = true; lp.shape = sh; lp.rateHz = 3.0f;
        std::vector<float> buf (static_cast<size_t> (n));
        e.generate (n, lp, buf.data());
        float mn = 9, mx = -9;
        for (auto x : buf) { mn = std::min (mn, x); mx = std::max (mx, x); }
        check (mn >= -1.001f && mx <= 1.001f && mx - mn > 0.5f, fmt ("LFO shape %.0f stays in range and moves (%.2f .. %.2f)", static_cast<double> (sh), static_cast<double> (mn), static_cast<double> (mx)));
    }
}

} // namespace

int main()
{
    testSpectralVoice();
    testChop();
    testEffects();
    testSpatial();
    testEngine();
    testGeneratedInput();
    testVoiceGenerator();
    testVoiceEngines();
    testMelodySearch();
    testKeyDetector();
    testMorph();
    testVoiceProfilePack();
    testDynamics();
    testReverbQuality();
    testLfo();

    std::printf ("\n%s (%d failed)\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", failures);
    return failures;
}
