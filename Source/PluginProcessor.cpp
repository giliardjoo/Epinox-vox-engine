#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

namespace
{
    // Settings of the generated vocal, stored inside the project state so it can be rebuilt exactly.
    const juce::Identifier genValid ("gen_valid"), genPhrase ("gen_phrase"), genGenre ("gen_genre"),
        genVoice ("gen_voice"), genEmotion ("gen_emotion"), genStyle ("gen_style"), genBpm ("gen_bpm"),
        genVariation ("gen_variation");

    juce::String keyFor (const epinox::GenerationRequest& r)
    {
        return r.phrase + "|" + juce::String (r.genre) + "|" + juce::String (r.voiceType) + "|" + juce::String (r.emotion)
               + "|" + juce::String (r.style) + "|" + juce::String (r.bpm, 3) + "|" + juce::String (static_cast<int> (r.variation));
    }

    epinox::GenerationRequest requestFromState (const juce::ValueTree& s, double sampleRate)
    {
        epinox::GenerationRequest r;
        r.phrase = s.getProperty (genPhrase).toString();
        r.genre = s.getProperty (genGenre, 0);
        r.voiceType = s.getProperty (genVoice, 0);
        r.emotion = s.getProperty (genEmotion, 0);
        r.style = s.getProperty (genStyle, 0);
        r.bpm = s.getProperty (genBpm, 128.0);
        r.variation = static_cast<juce::uint32> (static_cast<int> (s.getProperty (genVariation, 0)));
        r.sampleRate = sampleRate;
        return r;
    }
}

VoxProcessor::VoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", epinox::createLayout()),
      presets (apvts)
{
    gainParam = apvts.getRawParameterValue (epinox::pid::outGain);

    for (auto& s : scope)
        s.store (0.0f);
}

VoxProcessor::~VoxProcessor()
{
    alive->store (false);
    clip.store (nullptr);
}

epinox::dsp::EngineParams VoxProcessor::readParams() const
{
    using namespace epinox;
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    dsp::EngineParams p;
    p.power = v (pid::power) > 0.5f;
    p.mix = v (pid::mix) / 100.0f;
    p.bpm = v (pid::bpm);
    p.pitch = v (pid::pitch);
    p.formant = v (pid::formant);

    p.chopOn = v (pid::chopOn) > 0.5f;
    p.chopRate = v (pid::chopRate);
    p.chopSteps = static_cast<int> (v (pid::chopSteps));
    p.chopSmooth = v (pid::chopSmooth) / 100.0f;
    p.chopMix = v (pid::chopMix) / 100.0f;

    p.trOn = v (pid::trOn) > 0.5f;
    p.trPitch = v (pid::trPitch);
    p.trFormant = v (pid::trFormant);
    p.trRobot = v (pid::trRobot) / 100.0f;
    p.trGrain = v (pid::trGrain) / 100.0f;

    p.fxOn = v (pid::fxOn) > 0.5f;
    p.fxReverb = v (pid::fxReverb) / 100.0f;
    p.fxDelay = v (pid::fxDelay) / 100.0f;
    p.fxDistort = v (pid::fxDistort) / 100.0f;
    p.fxWidth = v (pid::fxWidth) / 100.0f;

    p.spOn = v (pid::spOn) > 0.5f;
    p.spSize = v (pid::spSize) / 100.0f;
    p.spDensity = v (pid::spDensity) / 100.0f;
    p.spDucking = v (pid::spDucking) / 100.0f;
    p.spMix = v (pid::spMix) / 100.0f;
    return p;
}

void VoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = std::abs (sampleRate - currentSampleRate) > 0.5;
    currentSampleRate = sampleRate;

    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (epinox::dsp::VoxEngine::latencySamples);

    binSamples = juce::jmax (1, static_cast<int> (sampleRate * 4.0 / scopeSize)); // 4 s window
    binCount = 0;
    binAcc = 0.0f;
    lastGain = juce::Decibels::decibelsToGain (gainParam->load());

    // A clip is rendered for one sample rate. After a change, rebuild it from the saved settings.
    if (rateChanged || (clip.load() == nullptr && apvts.state.getProperty (genValid, false)))
    {
        auto flag = alive;
        juce::MessageManager::callAsync ([this, flag]
                                         {
                                             if (flag->load())
                                                 rebuildGeneratedVocal();
                                         });
    }
}

bool VoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && layouts.getMainInputChannelSet() == out;
}

void VoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    auto measure = [&] (std::atomic<float>& peakOut, std::atomic<float>& rmsOut)
    {
        float peak = 0.0f, rms = 0.0f;

        for (int c = 0; c < numChannels; ++c)
        {
            peak = juce::jmax (peak, buffer.getMagnitude (c, 0, numSamples));
            rms += buffer.getRMSLevel (c, 0, numSamples);
        }

        peakOut.store (peak);
        rmsOut.store (numChannels > 0 ? rms / static_cast<float> (numChannels) : 0.0f);
        return peak;
    };

    measure (inPeak, inRms);

    // Transport information from the host.
    bool hostSynced = false, hostPlaying = true;
    double ppq = 0.0;

    if (auto* head = getPlayHead())
    {
        if (const auto pos = head->getPosition())
        {
            if (pos->getBpm().hasValue())
                hostBpm.store (*pos->getBpm());

            if (pos->getPpqPosition().hasValue())
            {
                ppq = *pos->getPpqPosition();
                hostSynced = true;
                hostPlaying = pos->getIsPlaying();
            }
        }
    }

    // Generated vocal: locked to the host transport, or free running when there is no transport (standalone).
    epinox::dsp::GeneratedInput generated;
    const auto* c = clip.load (std::memory_order_acquire);

    if (c != nullptr && c->length() > 0 && std::abs (c->sampleRate - currentSampleRate) < 0.5)
    {
        generated.left = c->left.data();
        generated.right = c->right.data();
        generated.length = c->length();

        if (hostSynced)
        {
            double beat = std::fmod (ppq, c->loopBeats);

            if (beat < 0.0)
                beat += c->loopBeats;

            generated.startPos = static_cast<int> (juce::jlimit (0.0, static_cast<double> (c->length() - 1),
                                                                 beat * 60.0 / c->bpm * currentSampleRate));
            generated.play = hostPlaying;
        }
        else
        {
            if (c != lastClipSeen)
            {
                lastClipSeen = c;
                freeRunPosition = 0;
            }

            generated.startPos = freeRunPosition;
            generated.play = true;
            freeRunPosition = (freeRunPosition + numSamples) % c->length();
        }
    }

    engine.process (buffer, readParams(), generated);

    {
        const float target = juce::Decibels::decibelsToGain (gainParam->load());
        buffer.applyGainRamp (0, numSamples, lastGain, target);
        lastGain = target;
    }

    if (measure (outPeak, outRms) >= 1.0f)
        clipped.store (true);

    // Rolling waveform of the output for the visualizer.
    int write = scopeWrite.load();

    for (int i = 0; i < numSamples; ++i)
    {
        float s = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            s = juce::jmax (s, std::abs (buffer.getSample (ch, i)));

        binAcc = juce::jmax (binAcc, s);

        if (++binCount >= binSamples)
        {
            scope[static_cast<size_t> (write)].store (binAcc);
            write = (write + 1) % scopeSize;
            binCount = 0;
            binAcc = 0.0f;
        }
    }

    scopeWrite.store (write);
}

//==============================================================================
void VoxProcessor::setStatus (const juce::String& text, bool warning)
{
    generationStatus = text;
    generationWarning = warning;
    ++generationStatusVersion;
}

void VoxProcessor::setClip (std::shared_ptr<epinox::GeneratedClip> newClip, const juce::String& key)
{
    const auto now = juce::Time::getMillisecondCounter();

    if (ownedClip != nullptr)
        retiredClips.emplace_back (std::move (ownedClip), now);

    ownedClip = std::move (newClip);
    ownedClipKey = key;
    clip.store (ownedClip.get(), std::memory_order_release);

    // The audio thread finishes with a clip within one block, so a few seconds is far more than enough.
    retiredClips.erase (std::remove_if (retiredClips.begin(), retiredClips.end(),
                                        [now] (const auto& r) { return now - r.second > 5000u; }),
                        retiredClips.end());
}

bool VoxProcessor::requestGeneration (epinox::GenerationRequest request)
{
    if (generation.isBusy())
        return false;

    request.sampleRate = currentSampleRate;
    auto flag = alive;

    return generation.start (request, [this, flag, request] (epinox::GenerationResult result)
    {
        if (! flag->load())
            return;

        if (result.ok)
        {
            setClip (result.clip, keyFor (request));

            auto& s = apvts.state;
            s.setProperty (genValid, true, nullptr);
            s.setProperty (genPhrase, request.phrase, nullptr);
            s.setProperty (genGenre, request.genre, nullptr);
            s.setProperty (genVoice, request.voiceType, nullptr);
            s.setProperty (genEmotion, request.emotion, nullptr);
            s.setProperty (genStyle, request.style, nullptr);
            s.setProperty (genBpm, request.bpm, nullptr);
            s.setProperty (genVariation, static_cast<int> (request.variation), nullptr);
        }

        setStatus (result.message, ! result.ok);
    });
}

void VoxProcessor::rebuildGeneratedVocal (bool clearIfNone)
{
    const auto& s = apvts.state;

    if (! static_cast<bool> (s.getProperty (genValid, false)))
    {
        if (clearIfNone && ownedClip != nullptr)
        {
            setClip (nullptr, {});
            setStatus ({}, false);
        }

        return;
    }

    if (generation.isBusy())
        return;

    const auto r = requestFromState (s, currentSampleRate);
    const auto key = keyFor (r);

    if (ownedClip != nullptr && ownedClipKey == key && std::abs (ownedClip->sampleRate - currentSampleRate) < 0.5)
        return;

    auto flag = alive;

    generation.start (r, [this, flag, key] (epinox::GenerationResult result)
    {
        if (! flag->load())
            return;

        if (result.ok)
        {
            setClip (result.clip, key);
            setStatus ("Vocal restored: " + result.clip->description + ".", false);
        }
        else
        {
            setStatus ("Could not rebuild the saved vocal. Press GENERATE again.", true);
        }

        // The project state or the sample rate may have changed while this was running.
        rebuildGeneratedVocal();
    });
}

//==============================================================================
juce::AudioProcessorEditor* VoxProcessor::createEditor()
{
    return new VoxEditor (*this);
}

void VoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            // The saved project holds the generator settings, not the audio: rebuild the vocal from them.
            auto flag = alive;
            juce::MessageManager::callAsync ([this, flag]
                                             {
                                                 if (flag->load())
                                                     rebuildGeneratedVocal (true);
                                             });
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoxProcessor();
}
