#include "GenerationService.h"
#include "Voice/PhraseRenderer.h"

namespace epinox
{
namespace
{
    class GenerationJob : public juce::ThreadPoolJob
    {
    public:
        GenerationJob (GenerationRequest r, GenerationService::Callback cb,
                       std::shared_ptr<std::atomic<bool>> aliveFlag, std::shared_ptr<std::atomic<bool>> busyFlag)
            : juce::ThreadPoolJob ("EPINOX vocal generation"),
              request (std::move (r)), callback (std::move (cb)), alive (std::move (aliveFlag)), busy (std::move (busyFlag))
        {
        }

        JobStatus runJob() override
        {
            std::shared_ptr<GeneratedClip> clip = voice::renderPhrase (request, [this] { return shouldExit(); });

            GenerationResult result;

            if (clip != nullptr)
            {
                result.ok = true;
                result.message = "Vocal ready: " + clip->description + ". It plays in time with your host's transport.";

                if (clip->truncated > 0)
                    result.message += " The phrase was too long for 8 bars, " + juce::String (clip->truncated) + " syllables were left out.";

                result.clip = std::move (clip);
            }
            else if (shouldExit())
            {
                busy->store (false);
                return jobHasFinished;
            }
            else
            {
                result.message = "That phrase has no letters to sing. Try a word or a short sentence.";
            }

            auto cb = callback;
            auto aliveFlag = alive;
            auto busyFlag = busy;

            juce::MessageManager::callAsync ([cb, aliveFlag, busyFlag, result]() mutable
                                             {
                                                 busyFlag->store (false);

                                                 if (aliveFlag->load())
                                                     cb (std::move (result));
                                             });

            return jobHasFinished;
        }

    private:
        GenerationRequest request;
        GenerationService::Callback callback;
        std::shared_ptr<std::atomic<bool>> alive, busy;
    };
} // namespace

GenerationService::GenerationService()
    : pool (juce::ThreadPoolOptions().withNumberOfThreads (1).withThreadName ("EPINOX generation")),
      alive (std::make_shared<std::atomic<bool>> (true)),
      busy (std::make_shared<std::atomic<bool>> (false))
{
}

GenerationService::~GenerationService()
{
    alive->store (false);
    pool.removeAllJobs (true, 10000);
}

bool GenerationService::start (const GenerationRequest& request, Callback onDone)
{
    if (busy->exchange (true))
        return false;

    pool.addJob (new GenerationJob (request, std::move (onDone), alive, busy), true);
    return true;
}
} // namespace epinox
