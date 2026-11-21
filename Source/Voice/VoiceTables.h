#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace epinox::voice
{
struct Formants
{
    float f1, f2, f3;
};

// Average adult male vowel formants in Hz.
inline Formants vowelFormants (char v)
{
    switch (v)
    {
        case 'a': return { 730.0f, 1090.0f, 2440.0f };
        case 'e': return { 530.0f, 1840.0f, 2480.0f };
        case 'i': return { 270.0f, 2290.0f, 3010.0f };
        case 'o': return { 570.0f,  840.0f, 2410.0f };
        default:  return { 300.0f,  870.0f, 2240.0f };
    }
}

enum class Kind { stop, fricative, nasal, liquid, glide, aspirate };

struct ConsonantSpec
{
    Kind kind;
    bool voiced;
    float noiseHz, noiseBw;   // noise colour (fricatives and stop bursts)
    Formants f;               // formant locus for voiced consonants
    float ms;                 // duration
    float level;              // noise level, relative to a vowel
};

inline ConsonantSpec consonantSpec (const juce::String& c)
{
    if (c == "p")  return { Kind::stop,      false, 1100.0f, 1800.0f, { 300.0f, 1100.0f, 2300.0f }, 48.0f, 0.55f };
    if (c == "b")  return { Kind::stop,      true,   900.0f, 1500.0f, { 200.0f,  800.0f, 2200.0f }, 40.0f, 0.40f };
    if (c == "t")  return { Kind::stop,      false, 4200.0f, 3200.0f, { 300.0f, 1800.0f, 2800.0f }, 48.0f, 0.60f };
    if (c == "d")  return { Kind::stop,      true,  3500.0f, 2800.0f, { 250.0f, 1700.0f, 2700.0f }, 40.0f, 0.40f };
    if (c == "k")  return { Kind::stop,      false, 2200.0f, 1600.0f, { 300.0f, 1500.0f, 2400.0f }, 52.0f, 0.60f };
    if (c == "g")  return { Kind::stop,      true,  1800.0f, 1400.0f, { 250.0f, 1400.0f, 2300.0f }, 42.0f, 0.40f };
    if (c == "f")  return { Kind::fricative, false, 5200.0f, 5000.0f, { 300.0f, 1200.0f, 2400.0f }, 85.0f, 0.38f };
    if (c == "v")  return { Kind::fricative, true,  4600.0f, 4200.0f, { 250.0f, 1200.0f, 2300.0f }, 70.0f, 0.26f };
    if (c == "s")  return { Kind::fricative, false, 7000.0f, 3600.0f, { 300.0f, 1500.0f, 2600.0f }, 95.0f, 0.55f };
    if (c == "z")  return { Kind::fricative, true,  6500.0f, 3400.0f, { 250.0f, 1500.0f, 2600.0f }, 80.0f, 0.34f };
    if (c == "sh") return { Kind::fricative, false, 3200.0f, 2200.0f, { 300.0f, 1700.0f, 2400.0f }, 100.0f, 0.60f };
    if (c == "j")  return { Kind::fricative, true,  3000.0f, 2000.0f, { 250.0f, 1700.0f, 2400.0f }, 80.0f, 0.34f };
    if (c == "m")  return { Kind::nasal,     true,  0.0f, 0.0f, { 250.0f, 1000.0f, 2200.0f }, 62.0f, 0.0f };
    if (c == "n")  return { Kind::nasal,     true,  0.0f, 0.0f, { 250.0f, 1500.0f, 2500.0f }, 58.0f, 0.0f };
    if (c == "l")  return { Kind::liquid,    true,  0.0f, 0.0f, { 350.0f, 1200.0f, 2700.0f }, 55.0f, 0.0f };
    if (c == "r")  return { Kind::liquid,    true,  0.0f, 0.0f, { 400.0f, 1300.0f, 1700.0f }, 50.0f, 0.0f };
    if (c == "y")  return { Kind::glide,     true,  0.0f, 0.0f, { 270.0f, 2200.0f, 3000.0f }, 45.0f, 0.0f };
    if (c == "w")  return { Kind::glide,     true,  0.0f, 0.0f, { 300.0f,  700.0f, 2200.0f }, 45.0f, 0.0f };
    return { Kind::aspirate, false, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 55.0f, 0.5f };
}

//==============================================================================
struct VoiceProfile
{
    float centreMidi = 60.0f;
    float formantScale = 1.0f;
    float breath = 0.05f;
    float vibratoCents = 20.0f, vibratoHz = 5.2f;
    int unison = 1;
    float unisonCents = 0.0f;
    float brightness = 1.0f;     // > 1 brighter
    bool voiced = true;
    bool robotic = false;        // no vibrato / glide, flat spectrum
    float attackMs = 15.0f;
};

// Order matches Parameters.h voiceTypeNames()
inline VoiceProfile voiceProfile (int index)
{
    VoiceProfile v;

    switch (index)
    {
        case 0: v.centreMidi = 67.0f; v.formantScale = 1.17f; v.breath = 0.08f; break;                       // Female
        case 1: v.centreMidi = 52.0f; v.formantScale = 1.00f; v.breath = 0.05f; break;                       // Male
        case 2: v.centreMidi = 59.0f; v.formantScale = 1.08f; v.breath = 0.07f; break;                       // Androgynous
        case 3: v.centreMidi = 72.0f; v.formantScale = 1.20f; v.breath = 0.22f; v.vibratoCents = 28.0f;
                v.vibratoHz = 4.6f; v.brightness = 0.85f; v.unison = 2; v.unisonCents = 7.0f; break;         // Ethereal
        case 4: v.centreMidi = 55.0f; v.formantScale = 1.00f; v.breath = 0.0f; v.vibratoCents = 0.0f;
                v.robotic = true; v.brightness = 1.25f; v.attackMs = 4.0f; break;                            // Robotic
        case 5: v.centreMidi = 60.0f; v.formantScale = 1.05f; v.breath = 0.02f; v.vibratoCents = 6.0f;
                v.brightness = 1.35f; v.unison = 2; v.unisonCents = 9.0f; v.attackMs = 6.0f; break;          // Synthetic
        case 6: v.centreMidi = 62.0f; v.formantScale = 1.10f; v.breath = 0.07f; v.vibratoCents = 24.0f;
                v.unison = 3; v.unisonCents = 14.0f; v.attackMs = 40.0f; break;                              // Choir
        case 7: v.centreMidi = 62.0f; v.formantScale = 1.08f; v.breath = 1.0f; v.voiced = false; break;      // Whisper
        case 8: v.centreMidi = 45.0f; v.formantScale = 0.88f; v.breath = 0.06f; v.vibratoCents = 14.0f;
                v.brightness = 0.8f; break;                                                                  // Deep Voice
        default: v.centreMidi = 70.0f; v.formantScale = 1.15f; v.breath = 0.30f; v.vibratoCents = 20.0f;
                 v.vibratoHz = 4.2f; v.brightness = 0.75f; v.unison = 3; v.unisonCents = 12.0f;
                 v.attackMs = 90.0f; break;                                                                  // Atmospheric
    }

    return v;
}

// A voice that is part way between two voice types. At 0 it is `a`, at 1 it is `b`; numbers blend smoothly,
// the on/off properties (robotic, voiced) switch at the half-way point.
inline VoiceProfile morphVoice (int a, int b, float t)
{
    auto A = voiceProfile (a);

    if (b < 0 || t <= 0.0f)
        return A;

    const auto B = voiceProfile (b);
    t = juce::jlimit (0.0f, 1.0f, t);
    auto mix = [t] (float x, float y) { return x + (y - x) * t; };
    VoiceProfile v;
    v.centreMidi = mix (A.centreMidi, B.centreMidi);
    v.formantScale = mix (A.formantScale, B.formantScale);
    v.breath = mix (A.breath, B.breath);
    v.vibratoCents = mix (A.vibratoCents, B.vibratoCents);
    v.vibratoHz = mix (A.vibratoHz, B.vibratoHz);
    v.unison = static_cast<int> (std::lround (mix (static_cast<float> (A.unison), static_cast<float> (B.unison))));
    v.unisonCents = mix (A.unisonCents, B.unisonCents);
    v.brightness = mix (A.brightness, B.brightness);
    v.attackMs = mix (A.attackMs, B.attackMs);
    v.voiced = t < 0.5f ? A.voiced : B.voiced;
    v.robotic = t < 0.5f ? A.robotic : B.robotic;
    return v;
}

struct EmotionProfile
{
    float vibratoMul = 1.0f, breathAdd = 0.0f, brightnessMul = 1.0f, formantMul = 1.0f;
    float scoopSemis = 0.0f;      // pitch glides in from below (positive) or above (negative)
    float attackMul = 1.0f, accent = 0.25f, descend = 0.0f;
    bool flat = false;
};

// Order matches Parameters.h emotionNames()
inline EmotionProfile emotionProfile (int index)
{
    EmotionProfile e;

    switch (index)
    {
        case 0:  e.vibratoMul = 1.2f; e.breathAdd = 0.12f; e.brightnessMul = 0.85f; e.attackMul = 1.6f; break;                  // Ethereal
        case 1:  e.vibratoMul = 1.5f; e.scoopSemis = 1.5f; e.accent = 0.35f; break;                                              // Emotional
        case 2:  e.vibratoMul = 0.5f; e.brightnessMul = 0.55f; e.formantMul = 0.93f; e.breathAdd = 0.04f; break;                 // Dark
        case 3:  e.vibratoMul = 1.1f; e.brightnessMul = 0.7f; e.descend = 0.35f; e.attackMul = 1.4f; e.scoopSemis = -0.8f; break; // Melancholic
        case 4:  e.vibratoMul = 0.0f; e.brightnessMul = 1.5f; e.attackMul = 0.3f; e.accent = 0.55f; break;                       // Aggressive
        case 5:  e.vibratoMul = 1.2f; e.brightnessMul = 1.3f; e.scoopSemis = 2.0f; e.accent = 0.4f; break;                       // Euphoric
        case 6:  e.vibratoMul = 1.0f; e.breathAdd = 0.15f; e.brightnessMul = 0.7f; e.attackMul = 1.8f; break;                    // Dreamy
        case 7:  e.vibratoMul = 0.6f; e.breathAdd = 0.18f; e.brightnessMul = 0.6f; e.formantMul = 0.95f; break;                  // Mysterious
        case 8:  e.vibratoMul = 1.3f; e.attackMul = 2.2f; e.accent = 0.4f; break;                                                // Cinematic
        case 9:  e.vibratoMul = 0.0f; e.flat = true; e.brightnessMul = 1.1f; e.attackMul = 0.5f; break;                          // Robotic
        case 10: e.vibratoMul = 0.7f; e.breathAdd = 0.22f; e.brightnessMul = 0.75f; e.accent = 0.1f; break;                      // Intimate
        default: e.vibratoMul = 1.1f; e.brightnessMul = 1.3f; e.formantMul = 1.03f; e.attackMul = 0.6f; e.accent = 0.5f; break;  // Powerful
    }

    return e;
}

struct StyleProfile
{
    std::vector<std::pair<float, float>> durations;   // (beats, weight)
    float legato = 0.95f;
    bool repeat = false;
    enum class Mode { stepwise, motif, repeat, spoken, leaps } mode = Mode::stepwise;
    float restChance = 0.0f;
    float glideMs = 50.0f;
    float attackMs = 15.0f;
    float breathAdd = 0.0f;
    bool whisper = false;
};

// Order matches Parameters.h styleNames()
inline StyleProfile styleProfile (int index)
{
    StyleProfile s;
    using M = StyleProfile::Mode;

    switch (index)
    {
        case 0: s.durations = { { 2.0f, 0.5f }, { 3.0f, 0.2f }, { 4.0f, 0.3f } }; s.legato = 1.0f; s.glideMs = 140.0f;
                s.attackMs = 120.0f; s.breathAdd = 0.10f; break;                                                           // Atmospheric
        case 1: s.durations = { { 2.0f, 0.6f }, { 4.0f, 0.4f } }; s.legato = 1.0f; s.glideMs = 120.0f; s.attackMs = 200.0f; break; // Cinematic
        case 2: s.durations = { { 1.0f, 0.5f }, { 0.5f, 0.3f }, { 2.0f, 0.2f } }; s.legato = 0.95f; s.glideMs = 60.0f; break;      // Melodic
        case 3: s.durations = { { 0.25f, 0.7f }, { 0.5f, 0.3f } }; s.legato = 0.6f; s.repeat = true; s.mode = M::repeat;
                s.restChance = 0.15f; s.glideMs = 5.0f; s.attackMs = 5.0f; break;                                          // Rhythmic
        case 4: s.durations = { { 0.5f, 0.5f }, { 0.25f, 0.3f }, { 1.0f, 0.2f } }; s.legato = 0.75f; s.mode = M::spoken;
                s.glideMs = 25.0f; s.attackMs = 10.0f; break;                                                              // Spoken
        case 5: s.durations = { { 1.0f, 0.4f }, { 0.5f, 0.3f }, { 2.0f, 0.2f }, { 1.5f, 0.1f } }; s.legato = 0.95f; break; // Sung
        case 6: s.durations = { { 0.5f, 0.5f }, { 1.0f, 0.4f }, { 0.25f, 0.1f } }; s.legato = 0.8f; s.mode = M::spoken;
                s.glideMs = 25.0f; s.whisper = true; break;                                                                // Whispered
        case 7: s.durations = { { 0.25f, 0.8f }, { 0.5f, 0.2f } }; s.legato = 0.4f; s.repeat = true; s.mode = M::repeat;
                s.restChance = 0.25f; s.glideMs = 3.0f; s.attackMs = 3.0f; break;                                          // Chopped
        case 8: s.durations = { { 0.5f, 1.0f } }; s.legato = 0.85f; s.repeat = true; s.mode = M::motif; s.glideMs = 30.0f; break; // Hypnotic
        default: s.durations = { { 0.25f, 0.3f }, { 0.5f, 0.3f }, { 1.0f, 0.2f }, { 1.5f, 0.1f }, { 0.75f, 0.1f } };
                 s.legato = 0.7f; s.mode = M::leaps; s.restChance = 0.15f; break;                                          // Experimental
    }

    return s;
}

struct GenreProfile
{
    std::vector<int> scale;
    float startOffsetBeats = 0.0f;
};

// Order matches Parameters.h genreNames()
inline GenreProfile genreProfile (int index)
{
    switch (index)
    {
        case 0:  return { { 0, 2, 3, 5, 7, 8, 10 }, 0.0f };      // Melodic Techno: natural minor
        case 1:  return { { 0, 2, 4, 5, 7, 9, 10 }, 0.0f };      // Progressive House: mixolydian
        case 2:  return { { 0, 3, 5, 7, 10 }, 0.5f };            // Afro House: minor pentatonic, syncopated
        case 3:  return { { 0, 1, 3, 5, 7, 8, 10 }, 0.0f };      // Peak Time Techno: phrygian
        case 4:  return { { 0, 1, 4, 5, 7, 8, 10 }, 0.0f };      // Hard Techno: phrygian dominant
        case 5:  return { { 0, 3, 5, 7 }, 0.0f };                // Hypnotic Techno: four notes
        case 6:  return { { 0, 2, 3, 5, 7, 8, 11 }, 0.0f };      // Trance: harmonic minor
        case 7:  return { { 0, 2, 3, 5, 7, 9, 10 }, 0.0f };      // Deep House: dorian
        case 8:  return { { 0, 3, 5, 6, 7, 10 }, 0.5f };         // Tech House: blues scale, offbeat
        default: return { { 0, 2, 4, 6, 7, 9, 11 }, 0.0f };      // Ambient: lydian
    }
}

// Forced scales, in the same order as scaleNames() (index 0 means "follow the genre" and is not listed here).
inline std::vector<int> scaleIntervals (int index)
{
    switch (index)
    {
        case 1:  return { 0, 2, 3, 5, 7, 8, 10 };   // natural minor
        case 2:  return { 0, 2, 4, 5, 7, 9, 11 };   // major
        case 3:  return { 0, 2, 3, 5, 7, 9, 10 };   // dorian
        case 4:  return { 0, 1, 3, 5, 7, 8, 10 };   // phrygian
        case 5:  return { 0, 2, 4, 5, 7, 9, 10 };   // mixolydian
        case 6:  return { 0, 2, 4, 6, 7, 9, 11 };   // lydian
        case 7:  return { 0, 2, 3, 5, 7, 8, 11 };   // harmonic minor
        case 8:  return { 0, 3, 5, 7, 10 };         // minor pentatonic
        case 9:  return { 0, 2, 4, 7, 9 };          // major pentatonic
        case 10: return { 0, 3, 5, 6, 7, 10 };      // blues
        default: return {};
    }
}
} // namespace epinox::voice
