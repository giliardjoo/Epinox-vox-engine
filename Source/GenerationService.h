#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <memory>
#include "Voice/GeneratedClip.h"

namespace epinox
{
struct GenerationResult
{
    bool ok = false;
    juce::String message;
    std::shared_ptr<GeneratedClip> clip;
};

// Boundary between the UI and the vocal generation backend. Generation always runs on a worker thread,
// never on the audio thread, and the finished clip is handed back on the message thread.
// The current backend is the offline synthesiser (Source/Voice); a cloud or model backend can replace
// `renderPhrase` here without touching the rest of the plugin.
class GenerationService
{
public:
    using Callback = std::function<void (GenerationResult)>;

    GenerationService();
    ~GenerationService();

    // Message thread only. Returns false when a generation is already running.
    bool start (const GenerationRequest& request, Callback onDone);

    bool isBusy() const { return busy->load(); }

private:
    juce::ThreadPool pool;
    std::shared_ptr<std::atomic<bool>> alive, busy;
};
} // namespace epinox
