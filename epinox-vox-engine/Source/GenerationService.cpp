#include "GenerationService.h"

namespace epinox
{
GenerationResult GenerationService::generate (const GenerationRequest& request) const
{
    if (request.phrase.trim().isEmpty())
        return { false, "Type a phrase first." };

    // No voice model is installed yet, so nothing is generated and no placeholder audio is played.
    return { false, "Vocal generation engine not installed yet. Controls and presets are active." };
}
} // namespace epinox
