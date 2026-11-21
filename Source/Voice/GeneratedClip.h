#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace epinox
{
// Everything the generator needs. The same request always renders the same audio.
struct GenerationRequest
{
    juce::String phrase;
    int genre = 0, voiceType = 0, emotion = 0, style = 0;
    float legato = 0.0f, body = 0.0f, layers = 0.0f;   // 0..1: connected notes, low-mid warmth, doubled voices
    float polish = 0.0f, air = 0.0f;                   // 0..1: compression + de-essing, high-frequency shelf
    juce::String engineId = "builtin";   // voice engine: "builtin" or the id of a voice pack
    int morphTo = -1;                    // second voice type to blend towards, -1 for none
    float morph = 0.0f;                  // 0..1, how far towards morphTo
    int octave = 0;                  // -2..+2 octaves
    int melodyVariation = -1;        // -1: same as variation; otherwise the melody seed (Lock Melody)
    int key = 9;                     // 0 = C ... 11 = B (default A)
    int scale = 0;                   // 0 = follow the genre, 1.. = forced scale (see scaleNames)
    double bpm = 128.0;
    juce::uint32 variation = 0;      // changes on every GENERATE press: "infinite possibilities"
    double sampleRate = 44100.0;
};

// A rendered phrase that loops over a whole number of bars.
struct GeneratedClip
{
    double sampleRate = 44100.0, bpm = 128.0, loopBeats = 4.0;
    std::vector<float> left, right;
    int syllables = 0, truncated = 0;
    juce::String description;

    int length() const { return static_cast<int> (left.size()); }
};
} // namespace epinox
