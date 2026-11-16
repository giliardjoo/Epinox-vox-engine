#include "PluginProcessor.h"
#include "PluginEditor.h"

VoxProcessor::VoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", epinox::createLayout()),
      presets (apvts)
{
    powerParam = apvts.getRawParameterValue (epinox::pid::power);
    gainParam  = apvts.getRawParameterValue (epinox::pid::outGain);

    for (auto& s : scope)
        s.store (0.0f);
}

void VoxProcessor::prepareToPlay (double sampleRate, int)
{
    binSamples = juce::jmax (1, static_cast<int> (sampleRate * 4.0 / scopeSize)); // 4 s window
    binCount = 0;
    binAcc = 0.0f;
    lastGain = juce::Decibels::decibelsToGain (gainParam->load());
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

    if (powerParam->load() > 0.5f)
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

        for (int c = 0; c < numChannels; ++c)
            s = juce::jmax (s, std::abs (buffer.getSample (c, i)));

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
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoxProcessor();
}
