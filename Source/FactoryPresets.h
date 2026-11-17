#pragma once

#include <initializer_list>
#include <utility>
#include <vector>
#include <juce_core/juce_core.h>

namespace epinox
{
// Factory presets: a phrase plus the parameters that differ from the defaults (values in the parameter's own units:
// choice index, dB, %, semitones, BPM...). Loading one resets everything else to default, so presets never mix.
struct FactoryPreset
{
    const char* name;
    const char* phrase;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list = {
        { "Afterglow Anthem", "Hold me till the light comes back",
          { { "genre", 0 }, { "voiceType", 0 }, { "emotion", 1 }, { "style", 2 }, { "key", 9 }, { "scale", 0 }, { "bpm", 124 },
            { "fxReverb", 40 }, { "fxDelay", 28 }, { "spMix", 35 }, { "mix", 75 } } },
        { "Midnight Choir", "We are the ones who stay awake",
          { { "genre", 0 }, { "voiceType", 6 }, { "emotion", 8 }, { "style", 1 }, { "key", 4 }, { "scale", 3 }, { "bpm", 122 },
            { "fxReverb", 55 }, { "spSize", 70 }, { "spMix", 45 }, { "chopOn", 0 } } },
        { "Sunrise Lift", "Take me higher", 
          { { "genre", 1 }, { "voiceType", 0 }, { "emotion", 5 }, { "style", 5 }, { "key", 0 }, { "scale", 2 }, { "bpm", 126 },
            { "fxReverb", 35 }, { "fxDelay", 30 }, { "mix", 80 }, { "chopOn", 0 } } },
        { "Tribal Whisper", "Ngoma ya moyo", 
          { { "genre", 2 }, { "voiceType", 7 }, { "emotion", 7 }, { "style", 3 }, { "key", 2 }, { "scale", 8 }, { "bpm", 120 },
            { "chopRate", 8 }, { "chopSteps", 8 }, { "fxDelay", 35 }, { "spMix", 25 } } },
        { "Savanna Chops", "Dance with me tonight", 
          { { "genre", 2 }, { "voiceType", 2 }, { "emotion", 5 }, { "style", 7 }, { "key", 7 }, { "scale", 9 }, { "bpm", 122 },
            { "chopRate", 12 }, { "chopSteps", 16 }, { "chopSmooth", 10 }, { "fxDelay", 25 } } },
        { "Machine Preacher", "Obey the rhythm", 
          { { "genre", 3 }, { "voiceType", 4 }, { "emotion", 9 }, { "style", 4 }, { "key", 9 }, { "scale", 4 }, { "bpm", 132 },
            { "trRobot", 45 }, { "fxDistort", 20 }, { "chopRate", 8 }, { "chopSteps", 8 }, { "fxReverb", 15 } } },
        { "Peak Time Siren", "Feel the fire", 
          { { "genre", 3 }, { "voiceType", 0 }, { "emotion", 11 }, { "style", 9 }, { "key", 5 }, { "scale", 4 }, { "bpm", 134 },
            { "fxDistort", 25 }, { "fxReverb", 30 }, { "trPitch", 7 }, { "spDucking", 40 } } },
        { "Warehouse Voice", "No light no sound", 
          { { "genre", 4 }, { "voiceType", 8 }, { "emotion", 2 }, { "style", 4 }, { "key", 9 }, { "scale", 4 }, { "bpm", 150 },
            { "fxDistort", 55 }, { "fxReverb", 20 }, { "chopRate", 16 }, { "chopSteps", 16 }, { "mix", 85 } } },
        { "Hard Grain", "Break the silence", 
          { { "genre", 4 }, { "voiceType", 5 }, { "emotion", 4 }, { "style", 7 }, { "key", 7 }, { "scale", 4 }, { "bpm", 148 },
            { "trGrain", 50 }, { "fxDistort", 40 }, { "chopRate", 16 } } },
        { "Hypnotic Mantra", "Breathe in breathe out", 
          { { "genre", 5 }, { "voiceType", 2 }, { "emotion", 7 }, { "style", 8 }, { "key", 2 }, { "scale", 0 }, { "bpm", 126 },
            { "fxDelay", 40 }, { "fxReverb", 30 }, { "chopRate", 6 }, { "chopSteps", 8 }, { "chopSmooth", 40 } } },
        { "Trance Angel", "Fly away with me", 
          { { "genre", 6 }, { "voiceType", 3 }, { "emotion", 5 }, { "style", 5 }, { "key", 4 }, { "scale", 7 }, { "bpm", 138 },
            { "fxReverb", 50 }, { "fxDelay", 30 }, { "spMix", 40 }, { "fxWidth", 150 }, { "chopOn", 0 } } },
        { "Euphoria Rush", "We are infinite", 
          { { "genre", 6 }, { "voiceType", 0 }, { "emotion", 5 }, { "style", 2 }, { "key", 9 }, { "scale", 7 }, { "bpm", 140 },
            { "fxReverb", 40 }, { "fxDelay", 35 }, { "mix", 80 } } },
        { "Velvet Room", "Stay a little longer", 
          { { "genre", 7 }, { "voiceType", 1 }, { "emotion", 10 }, { "style", 5 }, { "key", 7 }, { "scale", 3 }, { "bpm", 122 },
            { "fxReverb", 30 }, { "fxDelay", 20 }, { "spMix", 25 }, { "chopOn", 0 }, { "mix", 65 } } },
        { "Late Night Chop", "Say it again", 
          { { "genre", 7 }, { "voiceType", 0 }, { "emotion", 3 }, { "style", 7 }, { "key", 5 }, { "scale", 3 }, { "bpm", 120 },
            { "chopRate", 8 }, { "chopSteps", 16 }, { "chopSmooth", 25 }, { "fxDelay", 30 } } },
        { "Groove Spoken", "Everybody move your body", 
          { { "genre", 8 }, { "voiceType", 1 }, { "emotion", 11 }, { "style", 4 }, { "key", 9 }, { "scale", 10 }, { "bpm", 126 },
            { "chopRate", 8 }, { "chopSteps", 16 }, { "fxDelay", 22 }, { "fxReverb", 18 } } },
        { "Offbeat Hook", "Let it go let it flow", 
          { { "genre", 8 }, { "voiceType", 2 }, { "emotion", 5 }, { "style", 3 }, { "key", 0 }, { "scale", 10 }, { "bpm", 128 },
            { "chopRate", 8 }, { "chopSteps", 8 }, { "fxDelay", 28 } } },
        { "Dream Cloud", "Float into the sky", 
          { { "genre", 9 }, { "voiceType", 9 }, { "emotion", 6 }, { "style", 0 }, { "key", 5 }, { "scale", 6 }, { "bpm", 100 },
            { "fxReverb", 70 }, { "spSize", 80 }, { "spMix", 55 }, { "fxWidth", 160 }, { "chopOn", 0 }, { "mix", 70 } } },
        { "Cinematic Horizon", "The world is waking up", 
          { { "genre", 9 }, { "voiceType", 6 }, { "emotion", 8 }, { "style", 1 }, { "key", 2 }, { "scale", 1 }, { "bpm", 90 },
            { "fxReverb", 65 }, { "spSize", 75 }, { "spMix", 50 }, { "chopOn", 0 } } },
        { "Glass Voice", "Where did you go", 
          { { "genre", 0 }, { "voiceType", 3 }, { "emotion", 3 }, { "style", 2 }, { "key", 9 }, { "scale", 1 }, { "bpm", 120 },
            { "trFormant", 3 }, { "fxReverb", 45 }, { "fxDelay", 35 }, { "spMix", 35 }, { "chopOn", 0 } } },
        { "Broken Radio", "Can you hear me now", 
          { { "genre", 3 }, { "voiceType", 5 }, { "emotion", 2 }, { "style", 9 }, { "key", 6 }, { "scale", 4 }, { "bpm", 130 },
            { "trGrain", 35 }, { "trRobot", 25 }, { "fxDistort", 30 }, { "chopRate", 16 } } },
    };

    return list;
}
} // namespace epinox
