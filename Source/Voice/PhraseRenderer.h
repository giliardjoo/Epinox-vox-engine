#pragma once

#include <functional>
#include <memory>
#include "Composer.h"
#include "GeneratedClip.h"
#include "MelodySearch.h"
#include "PhraseParser.h"
#include "VocalSynth.h"

namespace epinox::voice
{
// Request in, looping clip out. Returns nullptr when the phrase has no pronounceable letters or when cancelled.
inline std::unique_ptr<GeneratedClip> renderPhrase (const GenerationRequest& r, const std::function<bool()>& cancelled = {})
{
    auto syllables = PhraseParser::parse (r.phrase);

    if (syllables.empty())
        return nullptr;

    const juce::int64 seed = r.phrase.trim().toLowerCase().hashCode64()
                             + static_cast<juce::int64> (r.melodyVariation >= 0 ? r.melodyVariation : r.variation) * 0x9E3779B97F4A7C15LL;

    const auto mode = styleProfile (r.style).mode;
    const bool searchable = mode == StyleProfile::Mode::stepwise || mode == StyleProfile::Mode::motif || mode == StyleProfile::Mode::leaps;

    // The first candidate is the plain composition for this seed, so the search can only improve on it.
    auto composition = Composer::compose (syllables, r.genre, r.style, r.emotion, r.voiceType, seed, r.key, r.scale, r.legato, r.octave);

    if (searchable)
    {
        MelodySearch::Context ctx;
        ctx.rootPc = composition.rootPc;
        ctx.scalePcs = composition.scalePcs;
        ctx.centreMidi = composition.centreMidi;
        ctx.leaping = mode == StyleProfile::Mode::leaps;

        double bestScore = MelodySearch::score (composition, ctx);

        for (int i = 1; i < 16; ++i)
        {
            if (cancelled && cancelled())
                return nullptr;

            auto cand = Composer::compose (syllables, r.genre, r.style, r.emotion, r.voiceType,
                                           seed + static_cast<juce::int64> (i) * 1315423911LL, r.key, r.scale, r.legato, r.octave);
            const double s = MelodySearch::score (cand, ctx);

            if (s > bestScore)
            {
                bestScore = s;
                composition = std::move (cand);
            }
        }

        MelodySearch::refine (composition, ctx, seed ^ 0x51ED270BLL);
    }

    return VocalSynth::render (composition, r, cancelled);
}
} // namespace epinox::voice
