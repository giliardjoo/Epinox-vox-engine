#pragma once

#include <juce_core/juce_core.h>

namespace epinox
{
struct GenerationRequest
{
    juce::String phrase;
    int   genre = 0, voiceType = 0, emotion = 0, style = 0;
    float bpm = 128.0f, pitch = 0.0f, formant = 0.0f;
};

struct GenerationResult
{
    bool ok = false;
    juce::String message;
};

// Boundary between the UI and the vocal generation backend.
// Generation always runs outside the audio thread; the audio engine only plays back
// the finished buffer. The backend (voice models) is plugged in here in a later phase.
class GenerationService
{
public:
    GenerationResult generate (const GenerationRequest& request) const;
};
} // namespace epinox
