#pragma once

#include <cmath>
#include <vector>
#include "PhraseParser.h"
#include "VoiceTables.h"

namespace epinox::voice
{
struct Note
{
    double beat = 0.0, beats = 1.0;
    float midi = 60.0f;
    float accent = 1.0f;
    int syllable = 0;          // index into Composition::syllables
};

struct Composition
{
    std::vector<Syllable> syllables;
    std::vector<Note> notes;
    double loopBeats = 4.0;
    int truncated = 0;
};

// Turns syllables into a rhythm and a melody that fits the genre scale and the chosen style.
class Composer
{
public:
    static constexpr double maxLoopBeats = 32.0;

    static Composition compose (std::vector<Syllable> syllables, int genre, int style, int emotion, int voiceType,
                                juce::int64 seed, int key = 9, int scaleChoice = 0, float legatoAmt = 0.0f, int octave = 0)
    {
        juce::Random rng (seed);
        const auto G = genreProfile (genre);
        const auto S = styleProfile (style);
        const auto V = voiceProfile (voiceType);
        const auto E = emotionProfile (emotion);

        Composition c;
        c.syllables = std::move (syllables);

        const int count = static_cast<int> (c.syllables.size());
        const auto forced = scaleIntervals (scaleChoice);
        const std::vector<int>& scaleNotes = forced.empty() ? G.scale : forced;
        const int len = static_cast<int> (scaleNotes.size());

        // Scale root: the chosen key closest to six semitones below the voice centre, so the melody sits around the centre.
        const int keyPc = juce::jlimit (0, 11, key);
        const int base = keyPc + 12 * (static_cast<int> (std::lround ((V.centreMidi - 6.0f - static_cast<float> (keyPc)) / 12.0f))
                                       + juce::jlimit (-2, 2, octave));

        auto degreeToMidi = [&] (int idx)
        {
            const int oct = static_cast<int> (std::floor (static_cast<double> (idx) / len));
            const int pc = scaleNotes[static_cast<size_t> (idx - oct * len)];
            return static_cast<float> (base + 12 * oct + pc);
        };

        // Rhythm of one pass through the phrase.
        struct Slot { double rest, beats, start; };
        std::vector<Slot> slots;
        double passBeats = 0.0;

        for (int i = 0; i < count; ++i)
        {
            const double d = pickDuration (S, rng);
            double rest = 0.0;

            if (i > 0 && rng.nextFloat() < S.restChance * (1.0f - 0.7f * legatoAmt))
                rest = d;

            slots.push_back ({ rest, d, passBeats + rest });
            passBeats += rest + d;
        }

        // Melody of one pass.
        std::vector<float> pitches (static_cast<size_t> (count), static_cast<float> (base));
        int idx = len / 2 + rng.nextInt (2);
        int motif[3] = { 0, 2 % len, 1 % len };

        if (S.mode == StyleProfile::Mode::motif)
        {
            motif[0] = 0;
            motif[1] = 1 + rng.nextInt (juce::jmax (1, len - 1));
            motif[2] = rng.nextInt (len);
        }

        const int motifLength = 2 + rng.nextInt (2);

        // Chord tones (1st, 3rd, 5th of the scale) carry the strong beats; passing notes fill the rest.
        auto snapToChord = [&] (int degree)
        {
            int best = degree, bestDist = 1000;

            for (int oct = -1; oct <= 1; ++oct)
                for (int cd : { 0, 2, 4 })
                {
                    if (cd >= len)
                        continue;

                    const int cand = cd + oct * len + (degree >= 0 ? (degree / len) * len : -((-degree + len - 1) / len) * len);
                    const int dist = std::abs (cand - degree);

                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        best = cand;
                    }
                }

            return best;
        };

        for (int i = 0; i < count; ++i)
        {
            float m = 0.0f;
            const double progress = count > 1 ? static_cast<double> (i) / (count - 1) : 0.0;

            switch (S.mode)
            {
                case StyleProfile::Mode::stepwise:
                {
                    const float r = rng.nextFloat();
                    int step = r < 0.30f ? 0 : r < 0.55f ? 1 : r < 0.80f ? -1 : r < 0.90f ? 2 : -2;

                    if (E.descend > 0.0f && rng.nextFloat() < E.descend)
                        step = -std::abs (step == 0 ? 1 : step);

                    idx = juce::jlimit (-2, len + 1, idx + step);

                    // Strong beats land on chord tones most of the time.
                    const double startBeat = slots[static_cast<size_t> (i)].start;
                    const bool strong = std::abs (std::fmod (startBeat, 2.0)) < 1.0e-6;

                    if (strong && rng.nextFloat() < 0.7f)
                        idx = juce::jlimit (-2, len + 1, snapToChord (idx));

                    m = degreeToMidi (i == count - 1 ? (idx > len / 2 ? len : 0) : idx);
                    break;
                }
                case StyleProfile::Mode::motif:
                    m = degreeToMidi (motif[i % motifLength]);
                    break;
                case StyleProfile::Mode::repeat:
                    m = degreeToMidi (i % 8 == 7 ? juce::jmin (4, len - 1) : (i % 4 == 3 ? juce::jmin (2, len - 1) : 0));
                    break;
                case StyleProfile::Mode::spoken:
                    m = V.centreMidi - 1.5f * static_cast<float> (progress)
                        + 0.9f * std::sin (static_cast<float> (i) * 0.9f) + (rng.nextFloat() - 0.5f) * 0.8f;
                    break;
                case StyleProfile::Mode::leaps:
                {
                    int d = rng.nextInt (len * 2) - 1;

                    if (slots[static_cast<size_t> (i)].start - std::floor (slots[static_cast<size_t> (i)].start / 2.0) * 2.0 < 1.0e-6)
                        d = snapToChord (d);

                    m = degreeToMidi (i == count - 1 ? 0 : d);   // the phrase resolves on the tonic
                }
                    break;
            }

            pitches[static_cast<size_t> (i)] = (E.flat || V.robotic) ? std::round (m) : m;
        }

        auto accentFor = [&] (const Syllable& s, double beat)
        {
            const bool onBeat = std::abs (beat - std::round (beat)) < 1.0e-6;
            return juce::jlimit (0.7f, 1.4f, 0.85f + (s.wordStart ? E.accent * 0.6f : 0.0f) + (onBeat ? 0.1f : 0.0f));
        };

        const double offset = G.startOffsetBeats;

        if (S.repeat)
        {
            const double pass = juce::jmax (1.0, std::ceil (passBeats));
            const double needed = offset + pass;
            double loop = 4.0;

            while (loop < needed && loop < maxLoopBeats)
                loop *= 2.0;

            c.loopBeats = juce::jmin (loop, maxLoopBeats);
            double start = offset;

            while (start + pass <= c.loopBeats + 1.0e-6)
            {
                double t = start;

                for (int i = 0; i < count; ++i)
                {
                    t += slots[static_cast<size_t> (i)].rest;
                    c.notes.push_back ({ t, slots[static_cast<size_t> (i)].beats, pitches[static_cast<size_t> (i)],
                                         accentFor (c.syllables[static_cast<size_t> (i)], t), i });
                    t += slots[static_cast<size_t> (i)].beats;
                }

                start += pass;
            }

            if (c.notes.empty())
                truncateInto (c, slots, pitches, count, offset, accentFor);
        }
        else
        {
            double loop = 4.0;

            while (loop < offset + passBeats + 0.5 && loop < maxLoopBeats)
                loop *= 2.0;

            c.loopBeats = juce::jmin (loop, maxLoopBeats);
            truncateInto (c, slots, pitches, count, offset, accentFor);
        }

        return c;
    }

private:
    static double pickDuration (const StyleProfile& s, juce::Random& rng)
    {
        float total = 0.0f;

        for (auto& d : s.durations)
            total += d.second;

        float r = rng.nextFloat() * total;

        for (auto& d : s.durations)
        {
            if (r < d.second)
                return d.first;

            r -= d.second;
        }

        return s.durations.back().first;
    }

    template <typename Slots, typename AccentFn>
    static void truncateInto (Composition& c, const Slots& slots, const std::vector<float>& pitches, int count,
                              double offset, AccentFn accentFor)
    {
        double t = offset;

        for (int i = 0; i < count; ++i)
        {
            const auto& sl = slots[static_cast<size_t> (i)];
            t += sl.rest;

            if (t + sl.beats > c.loopBeats - 0.05)
            {
                c.truncated = count - i;
                break;
            }

            c.notes.push_back ({ t, sl.beats, pitches[static_cast<size_t> (i)],
                                 accentFor (c.syllables[static_cast<size_t> (i)], t), i });
            t += sl.beats;
        }
    }
};
} // namespace epinox::voice
