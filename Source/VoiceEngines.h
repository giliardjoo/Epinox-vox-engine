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
//   { "id": "my-voice", "name": "My Voice", "type": "profile", "license": "...",
//     "voice": { "centreMidi": 55, "breath": 0.06, "vibratoHz": 5.4, "vibratoCents": 18, "brightness": 1.0, "attackMs": 14,
//                "vowels": { "a": [f1,f2,f3], "e": [...], "i": [...], "o": [...], "u": [...] } } }
//
// A pack is valid only when it names itself and states its license. An "onnx" pack also needs every listed model file;
// a "profile" pack needs a plausible measured voice (the synthesiser then sings with that person's formants and range).
struct VoicePack
{
    juce::String id, name, license, type;
    juce::File folder;
    std::vector<juce::String> models;
    std::shared_ptr<const voice::CustomVoice> custom;   // for packs of type "profile"
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

    // Reads and checks the "voice" block of a profile pack. Returns the problem, or an empty string.
    inline juce::String readProfile (const juce::var& v, VoicePack& p)
    {
        auto* o = v.getDynamicObject();

        if (o == nullptr)
            return "a profile pack needs a \"voice\" block";

        auto cv = std::make_shared<voice::CustomVoice>();
        cv->name = p.name;
        auto num = [&] (const char* k, double lo, double hi, double def, double& out)
        {
            const auto val = o->getProperty (k);
            out = val.isVoid() ? def : static_cast<double> (val);
            return out >= lo && out <= hi;
        };

        double centre = 0, breath = 0, vibHz = 0, vibCents = 0, bright = 0, attack = 0;

        if (! num ("centreMidi", 36, 90, -1, centre) || o->getProperty ("centreMidi").isVoid())
            return "centreMidi must be between 36 and 90";

        if (! num ("breath", 0, 0.6, 0.06, breath))         return "breath must be between 0 and 0.6";
        if (! num ("vibratoHz", 3, 8, 5.2, vibHz))          return "vibratoHz must be between 3 and 8";
        if (! num ("vibratoCents", 0, 80, 20, vibCents))    return "vibratoCents must be between 0 and 80";
        if (! num ("brightness", 0.5, 1.6, 1.0, bright))    return "brightness must be between 0.5 and 1.6";
        if (! num ("attackMs", 2, 120, 14, attack))         return "attackMs must be between 2 and 120";

        cv->profile.centreMidi = static_cast<float> (centre);
        cv->profile.breath = static_cast<float> (breath);
        cv->profile.vibratoHz = static_cast<float> (vibHz);
        cv->profile.vibratoCents = static_cast<float> (vibCents);
        cv->profile.brightness = static_cast<float> (bright);
        cv->profile.attackMs = static_cast<float> (attack);

        auto* vowels = o->getProperty ("vowels").getDynamicObject();

        if (vowels == nullptr)
            return "the voice needs a \"vowels\" block";

        double ratioSum = 0.0;
        const char* names[5] = { "a", "e", "i", "o", "u" };

        for (int i = 0; i < 5; ++i)
        {
            auto* arr = vowels->getProperty (names[i]).getArray();

            if (arr == nullptr || arr->size() != 3)
                return juce::String ("vowel ") + names[i] + " needs three formants [f1, f2, f3]";

            const double f1 = (*arr)[0], f2 = (*arr)[1], f3 = (*arr)[2];

            if (! (f1 >= 150 && f1 <= 1100 && f2 >= 500 && f2 <= 3200 && f3 >= 1500 && f3 <= 4200 && f1 < f2 && f2 < f3))
                return juce::String ("vowel ") + names[i] + " has formants outside the human range";

            cv->vowels[static_cast<size_t> (i)] = { static_cast<float> (f1), static_cast<float> (f2), static_cast<float> (f3) };
            const auto ref = voice::vowelFormants (names[i][0]);
            ratioSum += (f1 / ref.f1 + f2 / ref.f2 + f3 / ref.f3) / 3.0;
        }

        // Consonants use the average table scaled by this overall ratio.
        cv->profile.formantScale = static_cast<float> (ratioSum / 5.0);
        p.custom = cv;
        return {};
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
        else if (p.type != "onnx" && p.type != "profile")
            p.problem = "unsupported type (use \"onnx\" or \"profile\")";
        else if (p.type == "onnx" && p.models.empty())
            p.problem = "the pack lists no model files";

        if (p.problem.isEmpty() && p.type == "onnx")
            for (auto& m : p.models)
                if (p.problem.isEmpty() && ! folder.getChildFile (m).existsAsFile())
                    p.problem = "model file not found: " + m;

        if (p.problem.isEmpty() && p.type == "profile")
            p.problem = readProfile (obj->getProperty ("voice"), p);

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
            {
                if (p.type == "profile")
                    list.push_back ({ p.id, p.name, "Voice profile: the built-in synth sings with this voice's formants and range.", true });
                else
                    list.push_back ({ p.id, p.name, neuralRuntimeIncluded ? "Neural voice pack." : "Detected. Needs the neural runtime, not in this version.",
                                      neuralRuntimeIncluded });
            }

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
            else if (it->type == "profile")
            {
                GenerationRequest withVoice = request;
                withVoice.custom = it->custom;
                out.clip = voice::renderPhrase (withVoice, cancelled);
                out.note = "Voice: " + it->name + ".";
                return out;
            }
            else if (! neuralRuntimeIncluded)
                out.note = "The voice pack \"" + it->name + "\" needs the neural runtime, which is not in this version yet. The built-in synth was used.";
            // When the runtime exists, the neural render for `*it` goes here and returns early.
        }

        out.clip = voice::renderPhrase (request, cancelled);
        return out;
    }
} // namespace engines
} // namespace epinox
