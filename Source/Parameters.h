#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace epinox
{
namespace pid
{
    // Global
    inline constexpr const char* power     = "power";
    inline constexpr const char* outGain   = "outGain";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* genre     = "genre";
    inline constexpr const char* voiceType = "voiceType";
    inline constexpr const char* emotion   = "emotion";
    inline constexpr const char* style     = "style";
    inline constexpr const char* pitch     = "pitch";
    inline constexpr const char* formant   = "formant";
    inline constexpr const char* bpm       = "bpm";
    inline constexpr const char* octave    = "octave";
    inline constexpr const char* polish    = "polish";
    inline constexpr const char* air       = "air";
    inline constexpr const char* lockMelody = "lockMelody";
    inline constexpr const char* legato    = "legato";
    inline constexpr const char* body      = "body";
    inline constexpr const char* layers    = "layers";
    inline constexpr const char* key       = "key";
    inline constexpr const char* scale     = "scale";
    inline constexpr const char* morphTo   = "morphTo";
    inline constexpr const char* morphAmt  = "morphAmt";

    // Vocal Chop Engine
    inline constexpr const char* chopOn     = "chopOn";
    inline constexpr const char* chopRate   = "chopRate";
    inline constexpr const char* chopSteps  = "chopSteps";
    inline constexpr const char* chopSmooth = "chopSmooth";
    inline constexpr const char* chopMix    = "chopMix";

    // Transform
    inline constexpr const char* trOn      = "trOn";
    inline constexpr const char* trPitch   = "trPitch";
    inline constexpr const char* trFormant = "trFormant";
    inline constexpr const char* trRobot   = "trRobot";
    inline constexpr const char* trGrain   = "trGrain";

    // Effects
    inline constexpr const char* fxOn      = "fxOn";
    inline constexpr const char* fxReverb  = "fxReverb";
    inline constexpr const char* fxDelay   = "fxDelay";
    inline constexpr const char* fxDistort = "fxDistort";
    inline constexpr const char* fxWidth   = "fxWidth";

    // LFO
    inline constexpr const char* lfoOn      = "lfoOn";
    inline constexpr const char* lfoShape   = "lfoShape";
    inline constexpr const char* lfoSyncOn  = "lfoSyncOn";
    inline constexpr const char* lfoDiv     = "lfoDiv";
    inline constexpr const char* lfoRate    = "lfoRate";
    inline constexpr const char* lfoPhase   = "lfoPhase";
    inline constexpr const char* lfoFade    = "lfoFade";
    inline constexpr const char* lfoPitch   = "lfoPitch";
    inline constexpr const char* lfoFormant = "lfoFormant";
    inline constexpr const char* lfoFilter  = "lfoFilter";
    inline constexpr const char* lfoVolume  = "lfoVolume";

    // Spatial
    inline constexpr const char* spOn      = "spOn";
    inline constexpr const char* spSize    = "spSize";
    inline constexpr const char* spDensity = "spDensity";
    inline constexpr const char* spDucking = "spDucking";
    inline constexpr const char* spMix     = "spMix";
}

inline juce::StringArray genreNames()
{
    return { "Melodic Techno", "Progressive House", "Afro House", "Peak Time Techno", "Hard Techno",
             "Hypnotic Techno", "Trance", "Deep House", "Tech House", "Ambient" };
}

inline juce::StringArray lfoShapeNames()
{
    return { "Sine", "Triangle", "Square", "Saw Down", "Random Smooth", "Saw Up", "Random Step" };
}

// Tempo divisions, used when LFO Sync is on (Rate is used when it is off). Keep in step with LfoEngine::syncBeats(), which has "free" in front.
inline juce::StringArray lfoDivNames()
{
    return { "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2 D", "1/2 T", "1/4", "1/4 D", "1/4 T",
             "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T", "1/32" };
}

inline juce::StringArray keyNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

// Index 0 follows the genre; the others force a scale whatever the genre is.
inline juce::StringArray scaleNames()
{
    return { "Genre", "Natural Minor", "Major", "Dorian", "Phrygian", "Mixolydian", "Lydian",
             "Harmonic Minor", "Minor Pentatonic", "Major Pentatonic", "Blues" };
}

inline juce::StringArray voiceTypeNames()
{
    return { "Female", "Male", "Androgynous", "Ethereal", "Robotic", "Synthetic", "Choir",
             "Whisper", "Deep Voice", "Atmospheric Voice" };
}

inline juce::StringArray emotionNames()
{
    return { "Ethereal", "Emotional", "Dark", "Melancholic", "Aggressive", "Euphoric",
             "Dreamy", "Mysterious", "Cinematic", "Robotic", "Intimate", "Powerful" };
}

inline juce::StringArray styleNames()
{
    return { "Atmospheric", "Cinematic", "Melodic", "Rhythmic", "Spoken", "Sung",
             "Whispered", "Chopped", "Hypnotic", "Experimental" };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using APF = juce::AudioParameterFloat;
    using API = juce::AudioParameterInt;
    using APC = juce::AudioParameterChoice;
    using APB = juce::AudioParameterBool;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };

    auto addFloat = [&] (const char* pId, const char* name, float lo, float hi, float def,
                         const char* unit, float step = 0.1f)
    {
        layout.add (std::make_unique<APF> (id (pId), name, juce::NormalisableRange<float> (lo, hi, step), def,
                                           juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addPct = [&] (const char* pId, const char* name, float def)
    {
        addFloat (pId, name, 0.0f, 100.0f, def, "%");
    };
    auto addToggle = [&] (const char* pId, const char* name, bool def)
    {
        layout.add (std::make_unique<APB> (id (pId), name, def));
    };

    addToggle (pid::power, "Power", true);
    addFloat  (pid::outGain, "Output Gain", -24.0f, 12.0f, 0.0f, "dB");
    addPct    (pid::mix, "Mix", 70.0f);

    layout.add (std::make_unique<APC> (id (pid::genre),     "Genre",      genreNames(),     0));
    layout.add (std::make_unique<APC> (id (pid::key),       "Key",        keyNames(),       9));
    layout.add (std::make_unique<APC> (id (pid::scale),     "Scale",      scaleNames(),     0));
    layout.add (std::make_unique<APC> (id (pid::voiceType), "Voice Type", voiceTypeNames(), 0));
    {
        juce::StringArray morphChoices { "Off" };
        morphChoices.addArray (voiceTypeNames());
        layout.add (std::make_unique<APC> (id (pid::morphTo), "Morph To", morphChoices, 0));
    }
    layout.add (std::make_unique<APC> (id (pid::emotion),   "Emotion",    emotionNames(),   0));
    layout.add (std::make_unique<APC> (id (pid::style),     "Style",      styleNames(),     0));

    addFloat (pid::pitch,   "Pitch",   -24.0f, 24.0f, 0.0f, "st");
    addFloat (pid::formant, "Formant", -12.0f, 12.0f, 0.0f, "st");
    addFloat (pid::bpm,     "BPM",      40.0f, 220.0f, 128.0f, "bpm");
    layout.add (std::make_unique<API> (id (pid::octave), "Octave", -2, 2, 0));
    addPct   (pid::polish, "Polish", 50.0f);
    addPct   (pid::air,    "Air",    30.0f);
    addToggle (pid::lockMelody, "Lock Melody", false);
    addPct   (pid::morphAmt, "Morph Amount", 50.0f);
    addPct   (pid::legato, "Legato", 60.0f);
    addPct   (pid::body,   "Body",   60.0f);
    addPct   (pid::layers, "Layers", 40.0f);

    addToggle (pid::chopOn, "Chop On", false);
    addFloat  (pid::chopRate, "Chop Rate", 0.5f, 32.0f, 8.0f, "Hz");
    layout.add (std::make_unique<API> (id (pid::chopSteps), "Chop Steps", 1, 64, 16));
    addPct    (pid::chopSmooth, "Chop Smooth", 50.0f);
    addPct    (pid::chopMix, "Chop Mix", 100.0f);

    addToggle (pid::trOn, "Transform On", true);
    addFloat  (pid::trPitch,   "Transform Pitch",   -24.0f, 24.0f, 0.0f, "st");
    addFloat  (pid::trFormant, "Transform Formant", -12.0f, 12.0f, 0.0f, "st");
    addPct    (pid::trRobot, "Transform Robot", 0.0f);
    addPct    (pid::trGrain, "Transform Grain", 0.0f);

    addToggle (pid::fxOn, "Effects On", true);
    addPct    (pid::fxReverb,  "FX Reverb",  25.0f);
    addPct    (pid::fxDelay,   "FX Delay",   20.0f);
    addPct    (pid::fxDistort, "FX Distort",  0.0f);
    addFloat  (pid::fxWidth,   "FX Width",    0.0f, 200.0f, 100.0f, "%");

    addToggle (pid::spOn, "Spatial On", true);
    addPct    (pid::spSize,    "Spatial Size",    50.0f);
    addPct    (pid::spDensity, "Spatial Density", 50.0f);
    addPct    (pid::spDucking, "Spatial Ducking",  0.0f);
    addPct    (pid::spMix,     "Spatial Mix",     30.0f);

    addToggle (pid::lfoOn, "LFO On", false);
    layout.add (std::make_unique<APC> (id (pid::lfoShape), "LFO Shape", lfoShapeNames(), 0));
    addToggle (pid::lfoSyncOn, "LFO Sync", false);
    layout.add (std::make_unique<APC> (id (pid::lfoDiv),   "LFO Division", lfoDivNames(), 7));
    layout.add (std::make_unique<APF> (id (pid::lfoRate), "LFO Rate",
                                       [] { juce::NormalisableRange<float> r (0.02f, 30.0f, 0.01f); r.setSkewForCentre (1.5f); return r; }(), 1.0f,
                                       juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    addFloat (pid::lfoPhase, "LFO Phase", 0.0f, 360.0f, 0.0f, "deg", 1.0f);
    addFloat (pid::lfoFade,  "LFO Fade In", 0.0f, 3000.0f, 0.0f, "ms", 1.0f);
    addPct (pid::lfoPitch,   "LFO Pitch",   0.0f);
    addPct (pid::lfoFormant, "LFO Formant", 0.0f);
    addPct (pid::lfoFilter,  "LFO Filter",  0.0f);
    addPct (pid::lfoVolume,  "LFO Volume",  0.0f);

    return layout;
}
} // namespace epinox
