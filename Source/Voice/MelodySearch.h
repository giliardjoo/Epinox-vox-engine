#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include "Composer.h"

namespace epinox::voice
{
// "Quantum-inspired" melody search. It runs on a normal CPU: many candidate melodies are scored for musicality, the best
// one is refined by simulated annealing (random moves that are always kept when they improve the score and sometimes kept
// when they do not, less and less often as the search cools), and the result stays inside the chosen scale.
class MelodySearch
{
public:
    struct Context
    {
        int rootPc = 0;
        std::vector<int> scalePcs;
        float centreMidi = 60.0f;
        bool leaping = false;   // leaping style tolerates larger intervals
    };

    // Higher is better. Looks at the notes in time order.
    static double score (const Composition& c, const Context& ctx)
    {
        const auto& n = c.notes;

        if (n.size() < 2)
            return 0.0;

        double s = 0.0;
        int repeats = 0;
        float hi = -1000.0f, lo = 1000.0f;

        for (size_t i = 0; i < n.size(); ++i)
        {
            hi = std::max (hi, n[i].midi);
            lo = std::min (lo, n[i].midi);

            if (i > 0)
            {
                const float d = std::abs (n[i].midi - n[i - 1].midi);
                const float comfy = ctx.leaping ? 9.0f : 5.0f;
                s -= d > comfy ? 1.5 * (d - comfy) : 0.0;
                s += d > 0.0f && d <= 2.0f ? 0.6 : 0.0;       // stepwise motion sounds singable
                repeats = d == 0.0f ? repeats + 1 : 0;
                s -= repeats >= 3 ? 1.0 : 0.0;                  // no stuck notes
            }

            const bool strong = std::abs (std::fmod (n[i].beat, 2.0)) < 1.0e-6;

            if (strong)
                s += isChordTone (n[i].midi, ctx) ? 1.5 : -0.5;

            s -= std::abs (n[i].midi - ctx.centreMidi) > 9.0f ? 1.0 : 0.0;   // stay in the voice's comfortable range
        }

        // A phrase that comes home: last note on the root or the fifth.
        const auto last = n.back().midi;
        s += isDegree (last, ctx, 0) ? 4.0 : (isDegree (last, ctx, 4) ? 2.0 : -2.0);

        // One clear high point, and some variety.
        const auto peaks = std::count_if (n.begin(), n.end(), [hi] (const Note& x) { return x.midi == hi; });
        s += peaks == 1 ? 2.0 : (peaks > 3 ? -1.5 : 0.0);

        std::vector<int> distinct;

        for (auto& x : n)
            distinct.push_back (static_cast<int> (std::lround (x.midi)));

        std::sort (distinct.begin(), distinct.end());
        distinct.erase (std::unique (distinct.begin(), distinct.end()), distinct.end());
        s += std::min<size_t> (distinct.size(), 5) * 0.8;
        s -= (hi - lo) > 14.0f ? (hi - lo - 14.0f) : 0.0;
        return s;
    }

    // Moves a pitch by whole scale steps, staying in the scale.
    static float stepInScale (float midi, int steps, const Context& ctx)
    {
        int m = static_cast<int> (std::lround (midi));

        for (int guard = 0; steps != 0 && guard < 64; ++guard)
        {
            m += steps > 0 ? 1 : -1;

            if (inScale (m, ctx))
                steps += steps > 0 ? -1 : 1;
        }

        return static_cast<float> (m);
    }

    // Improves a composition in place. Deterministic for a given seed.
    static void refine (Composition& c, const Context& ctx, juce::int64 seed, int iterations = 600)
    {
        if (c.notes.size() < 3 || ctx.scalePcs.empty())
            return;

        juce::Random rng (seed);
        Composition cur = c;
        double curScore = score (cur, ctx);
        Composition best = cur;
        double bestScore = curScore;

        for (int it = 0; it < iterations; ++it)
        {
            const double temperature = 1.5 * (1.0 - static_cast<double> (it) / iterations) + 0.02;
            const int syl = cur.notes[static_cast<size_t> (rng.nextInt (static_cast<int> (cur.notes.size())))].syllable;
            const int step = (rng.nextBool() ? 1 : -1) * (1 + rng.nextInt (2));

            Composition trial = cur;

            for (auto& n : trial.notes)
                if (n.syllable == syl)
                    n.midi = stepInScale (n.midi, step, ctx);

            const double ts = score (trial, ctx);

            if (ts >= curScore || rng.nextDouble() < std::exp ((ts - curScore) / temperature))
            {
                cur = std::move (trial);
                curScore = ts;

                if (curScore > bestScore)
                {
                    best = cur;
                    bestScore = curScore;
                }
            }
        }

        c = std::move (best);
    }

    // Phrase dynamics: a gentle swell across the phrase, louder on higher notes, and a small different level for each
    // word, so the line breathes instead of every syllable having the same weight. Never adds silence. Deterministic.
    static void shapeDynamics (Composition& c, juce::int64 seed, float centreMidi, float amount = 1.0f)
    {
        if (c.notes.empty() || amount <= 0.0f)
            return;

        const double total = std::max (1.0, c.loopBeats);
        std::vector<float> wordGain;
        juce::Random rng (seed);
        int word = -1;

        for (size_t i = 0; i < c.notes.size(); ++i)
        {
            auto& n = c.notes[i];
            const auto& syl = c.syllables[static_cast<size_t> (n.syllable)];

            if (syl.wordStart || word < 0)
            {
                ++word;
                wordGain.push_back (1.0f + (rng.nextFloat() - 0.5f) * 0.18f);   // about +-0.75 dB
            }

            const double pos = std::fmod (n.beat, total) / total;
            const float swell = 0.94f + 0.12f * static_cast<float> (std::sin (juce::MathConstants<double>::pi * pos));
            const float register_ = 1.0f + juce::jlimit (-0.08f, 0.1f, (n.midi - centreMidi) * 0.012f);
            const float g = swell * register_ * wordGain[static_cast<size_t> (word)];
            n.accent = juce::jlimit (0.6f, 1.5f, n.accent * (1.0f + (g - 1.0f) * amount));
        }
    }

private:
    static bool inScale (int midi, const Context& ctx)
    {
        const int pc = ((midi - ctx.rootPc) % 12 + 12) % 12;
        return std::find (ctx.scalePcs.begin(), ctx.scalePcs.end(), pc) != ctx.scalePcs.end();
    }

    static bool isDegree (float midi, const Context& ctx, int degree)
    {
        if (degree >= static_cast<int> (ctx.scalePcs.size()))
            return false;

        const int pc = ((static_cast<int> (std::lround (midi)) - ctx.rootPc) % 12 + 12) % 12;
        return pc == ctx.scalePcs[static_cast<size_t> (degree)];
    }

    static bool isChordTone (float midi, const Context& ctx)
    {
        return isDegree (midi, ctx, 0) || isDegree (midi, ctx, 2) || isDegree (midi, ctx, 4);
    }
};
} // namespace epinox::voice
