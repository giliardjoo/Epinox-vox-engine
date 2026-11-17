#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace epinox::voice
{
// One sung syllable: optional consonant onset, one vowel nucleus, optional consonant coda.
// Consonants are phoneme codes: p b t d k g f v s z sh j m n l r y w h
struct Syllable
{
    std::vector<juce::String> onset, coda;
    char vowel = 'a';
    bool wordStart = false, wordEnd = false;
};

// Rule-based grapheme-to-syllable conversion. It is an approximation that works for Portuguese and
// English style spelling: the goal is a believable vocal texture, not dictionary pronunciation.
class PhraseParser
{
public:
    static std::vector<Syllable> parse (const juce::String& phrase)
    {
        std::vector<Syllable> out;
        juce::String word;

        for (auto c : phrase.toLowerCase() + " ")
        {
            const auto b = baseLetter (c);

            if (b >= 'a' && b <= 'z')
            {
                word += juce::String::charToString (b);
            }
            else if (word.isNotEmpty())
            {
                parseWord (word, out);
                word.clear();
            }
        }

        return out;
    }

private:
    struct Token
    {
        bool vowel = false;
        juce::String code;
    };

    static juce::juce_wchar baseLetter (juce::juce_wchar c)
    {
        switch (c)
        {
            case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5: return 'a';
            case 0xe7: return 's';
            case 0xe8: case 0xe9: case 0xea: case 0xeb: return 'e';
            case 0xec: case 0xed: case 0xee: case 0xef: return 'i';
            case 0xf1: return 'n';
            case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: return 'o';
            case 0xf9: case 0xfa: case 0xfb: case 0xfc: return 'u';
            default: return c;
        }
    }

    static bool isVowelLetter (juce::juce_wchar c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }

    static bool validOnsetCluster (const juce::String& a, const juce::String& b)
    {
        static const char* const pairs[] = { "p r", "p l", "b r", "b l", "t r", "d r", "k r", "k l", "g r", "g l",
                                             "f r", "f l", "s p", "s t", "s k", "s l", "s m", "s n" };
        const auto key = a + " " + b;

        for (auto* p : pairs)
            if (key == p)
                return true;

        return false;
    }

    static std::vector<Token> tokenise (const juce::String& w)
    {
        std::vector<Token> t;
        const int n = w.length();

        auto at = [&] (int i) -> juce::juce_wchar { return (i >= 0 && i < n) ? w[i] : 0; };
        auto addC = [&] (const char* code) { t.push_back ({ false, code }); };
        auto addV = [&] (juce::juce_wchar v) { t.push_back ({ true, juce::String::charToString (v) }); };

        for (int i = 0; i < n; ++i)
        {
            const auto c = at (i), nx = at (i + 1);
            const bool nextVowel = isVowelLetter (nx);

            if (isVowelLetter (c))
            {
                addV (c);
                continue;
            }

            switch (c)
            {
                case 'c':
                    if (nx == 'h')      { addC ("sh"); ++i; }
                    else if (nx == 'k') { addC ("k"); ++i; }
                    else if (nx == 'e' || nx == 'i' || nx == 'y') addC ("s");
                    else                addC ("k");
                    break;
                case 's':
                    if (nx == 'h')      { addC ("sh"); ++i; }
                    else if (nx == 's') { addC ("s"); ++i; }
                    else                addC ("s");
                    break;
                case 'p': if (nx == 'h') { addC ("f"); ++i; } else addC ("p"); break;
                case 't': if (nx == 'h') { addC ("t"); ++i; } else addC ("t"); break;
                case 'l': if (nx == 'h') { addC ("l"); ++i; } else if (nx == 'l') { addC ("l"); ++i; } else addC ("l"); break;
                case 'n': if (nx == 'h') { addC ("n"); ++i; } else addC ("n"); break;
                case 'r': if (nx == 'r') ++i; addC ("r"); break;
                case 'q': addC ("k"); if (nx == 'u') ++i; break;
                case 'g':
                    if (nx == 'u' && (at (i + 2) == 'e' || at (i + 2) == 'i')) { addC ("g"); ++i; }
                    else if (nx == 'e' || nx == 'i') addC ("j");
                    else addC ("g");
                    break;
                case 'x': addC ("sh"); break;
                case 'j': addC ("j"); break;
                case 'z': addC ("z"); break;
                case 'h': addC ("h"); break;
                case 'w': if (nextVowel) addC ("w"); else addV ('u'); break;
                case 'y':
                    if (nextVowel && (t.empty() || ! t.back().vowel)) addC ("y");
                    else addV ('i');
                    break;
                case 'b': addC ("b"); break;
                case 'd': addC ("d"); break;
                case 'f': addC ("f"); break;
                case 'k': addC ("k"); break;
                case 'm': addC ("m"); break;
                case 'v': addC ("v"); break;
                default: break;
            }
        }

        // Silent final "e" after a single consonant that follows a vowel (love, time, noite).
        if (t.size() >= 3 && t.back().vowel && t.back().code == "e")
        {
            const auto& c1 = t[t.size() - 2];
            const auto& v0 = t[t.size() - 3];

            if (! c1.vowel && v0.vowel)
                t.pop_back();
        }

        // Adjacent vowels: identical or with a glide (i/u) become one nucleus, others stay separate.
        std::vector<Token> merged;

        for (auto& tok : t)
        {
            if (tok.vowel && ! merged.empty() && merged.back().vowel)
            {
                const auto a = merged.back().code, b = tok.code;
                const bool glide = (b == "i" || b == "u") || a == b || (a == "e" && b == "a") || (a == "i" && b == "e");

                if (glide)
                    continue;
            }

            merged.push_back (tok);
        }

        return merged;
    }

    static void parseWord (const juce::String& word, std::vector<Syllable>& out)
    {
        const auto tokens = tokenise (word);

        if (tokens.empty())
            return;

        std::vector<size_t> nuclei;

        for (size_t i = 0; i < tokens.size(); ++i)
            if (tokens[i].vowel)
                nuclei.push_back (i);

        std::vector<Syllable> syl;

        if (nuclei.empty())
        {
            Syllable s;
            s.vowel = 'e';

            for (auto& t : tokens)
                s.onset.push_back (t.code);

            syl.push_back (s);
        }
        else
        {
            syl.resize (nuclei.size());

            for (size_t k = 0; k < nuclei.size(); ++k)
                syl[k].vowel = tokens[nuclei[k]].code[0];

            for (size_t i = 0; i < nuclei.front(); ++i)
                syl.front().onset.push_back (tokens[i].code);

            for (size_t i = nuclei.back() + 1; i < tokens.size(); ++i)
                syl.back().coda.push_back (tokens[i].code);

            for (size_t k = 0; k + 1 < nuclei.size(); ++k)
            {
                std::vector<juce::String> cluster;

                for (size_t i = nuclei[k] + 1; i < nuclei[k + 1]; ++i)
                    cluster.push_back (tokens[i].code);

                size_t toOnset = 0;

                if (cluster.size() == 1)
                    toOnset = 1;
                else if (cluster.size() >= 2)
                    toOnset = validOnsetCluster (cluster[cluster.size() - 2], cluster.back()) ? 2 : 1;

                const size_t toCoda = cluster.size() - toOnset;

                for (size_t i = 0; i < toCoda; ++i)
                    syl[k].coda.push_back (cluster[i]);

                for (size_t i = toCoda; i < cluster.size(); ++i)
                    syl[k + 1].onset.push_back (cluster[i]);
            }
        }

        for (auto& s : syl)
        {
            while (s.coda.size() > 2)
                s.coda.pop_back();

            while (s.onset.size() > 3)
                s.onset.erase (s.onset.begin());
        }

        syl.front().wordStart = true;
        syl.back().wordEnd = true;

        for (auto& s : syl)
            out.push_back (std::move (s));
    }
};
} // namespace epinox::voice
