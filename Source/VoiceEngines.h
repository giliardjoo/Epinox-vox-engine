#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <juce_core/juce_core.h>
#include "Voice/PhraseRenderer.h"

namespace epinox
{
// The vocal generator can use more than one voice engine. "builtin" is the offline synthesiser in Source/Voice.
// Neural voice packs are folders with a voicepack.json manifest, installed in the user's EPINOX folder:
//   <user application data>/EPINOX/VOX ENGINE/Voices/<pack>/voicepack.json
//
//   { "id": "my-voice", "name": "My Voice", "type": "onnx", "license": "who allows what, in plain words",
//     "models": ["acoustic.onnx", "vocoder.onnx"] }
//
// A pack is valid only when it names itself, states its license and every listed model file exists.
struct VoicePack
{
    juce::String id, name, license, type;
    juce::File folder;
    std::vector<juce::String> models;
    bool valid = false;
    juce::String problem;
};

struct VoiceEngineInfo
{
    juce::String id, name, note;
    bool available = true;   // false when listed but not usable in this version
};

struct EngineRender
{
    std::unique_ptr<GeneratedClip> clip;
    juce::String note;   // shown after the "Vocal ready" message, e.g. a fallback notice
};

namespace engines
{
    // Whether this build contains the runtime that executes neural voice packs. The runtime arrives in a later
    // version; until then packs are detected and validated, and rendering falls back to the built-in synthesiser.
    inline constexpr bool neuralRuntimeIncluded = false;

    inline juce::File defaultVoicesFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("EPINOX").getChildFile ("VOX ENGINE").getChildFile ("Voices");
    }

    inline VoicePack readPack (const juce::File& folder)
    {
        VoicePack p;
        p.folder = folder;
        p.id = p.name = folder.getFileName();

        const auto manifest = folder.getChildFile ("voicepack.json");

        if (! manifest.existsAsFile())
        {
            p.problem = "voicepack.json is missing";
            return p;
        }

        const auto json = juce::JSON::parse (manifest);
        auto* obj = json.getDynamicObject();

        if (obj == nullptr)
        {
            p.problem = "voicepack.json is not valid JSON";
            return p;
        }

        p.id = obj->getProperty ("id").toString().trim();
        p.name = obj->getProperty ("name").toString().trim();
        p.license = obj->getProperty ("license").toString().trim();
        p.type = obj->getProperty ("type").toString().trim();

        if (auto* arr = obj->getProperty ("models").getArray())
            for (auto& m : *arr)
                p.models.push_back (m.toString());

        if (p.id.isEmpty() || p.name.isEmpty())
            p.problem = "the pack needs an id and a name";
        else if (p.id == "builtin" || p.id.containsAnyOf ("/\\|\"<>"))
            p.problem = "that id is not allowed";
        else if (p.license.isEmpty())
            p.problem = "the pack must state its license";
        else if (p.type != "onnx")
            p.problem = "unsupported type (only \"onnx\" is supported)";
        else if (p.models.empty())
            p.problem = "the pack lists no model files";

        for (auto& m : p.models)
            if (p.problem.isEmpty() && ! folder.getChildFile (m).existsAsFile())
                p.problem = "model file not found: " + m;

        p.valid = p.problem.isEmpty();
        return p;
    }

    inline std::vector<VoicePack> scanPacks (const juce::File& root = defaultVoicesFolder())
    {
        std::vector<VoicePack> packs;

        for (auto& f : root.findChildFiles (juce::File::findDirectories, false))
            packs.push_back (readPack (f));

        std::sort (packs.begin(), packs.end(), [] (const VoicePack& a, const VoicePack& b) { return a.name < b.name; });
        return packs;
    }

    inline std::vector<VoiceEngineInfo> listEngines (const juce::File& root = defaultVoicesFolder())
    {
        std::vector<VoiceEngineInfo> list;
        list.push_back ({ "builtin", "Built-in synth (offline)", "Offline synthesiser included with the plugin.", true });

        for (auto& p : scanPacks (root))
            if (p.valid)
                list.push_back ({ p.id, p.name, neuralRuntimeIncluded ? "Neural voice pack." : "Detected. Needs the neural runtime, not in this version.",
                                  neuralRuntimeIncluded });

        return list;
    }

    // Renders with the engine named in the request. An unknown or not yet usable engine falls back to the
    // built-in synthesiser and says so in `note`, so the user always hears something.
    inline EngineRender render (const GenerationRequest& request, const std::function<bool()>& cancelled,
                                const juce::File& root = defaultVoicesFolder())
    {
        EngineRender out;
        const auto id = request.engineId.trim();

        if (id.isNotEmpty() && id != "builtin")
        {
            const auto packs = scanPacks (root);
            const auto it = std::find_if (packs.begin(), packs.end(), [&] (const VoicePack& p) { return p.valid && p.id == id; });

            if (it == packs.end())
                out.note = "The voice pack \"" + id + "\" was not found, so the built-in synth was used.";
            else if (! neuralRuntimeIncluded)
                out.note = "The voice pack \"" + it->name + "\" needs the neural runtime, which is not in this version yet. The built-in synth was used.";
            // When the runtime exists, the neural render for `*it` goes here and returns early.
        }

        out.clip = voice::renderPhrase (request, cancelled);
        return out;
    }
} // namespace engines
} // namespace epinox
