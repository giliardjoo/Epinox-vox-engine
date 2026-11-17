#pragma once

#include <functional>
#include <memory>
#include "Composer.h"
#include "GeneratedClip.h"
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
                             + static_cast<juce::int64> (r.variation) * 0x9E3779B97F4A7C15LL;

    auto composition = Composer::compose (std::move (syllables), r.genre, r.style, r.emotion, r.voiceType, seed);
    return VocalSynth::render (composition, r, cancelled);
}
} // namespace epinox::voice
