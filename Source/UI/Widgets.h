#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include <vector>
#include "NeonLookAndFeel.h"
#include "../PluginProcessor.h"

namespace epinox::ui
{
using namespace epinox::colours;

//==============================================================================
// Rotary knob with caption and value read-out, bound to a parameter.
class NeonKnob : public juce::Component
{
public:
    NeonKnob (juce::AudioProcessorValueTreeState& apvts, const char* paramID, const juce::String& captionText,
              const juce::String& suffix = {}, int decimals = 1, bool big = false)
    {
        caption.setText (captionText, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::Font (juce::FontOptions (big ? 14.0f : 11.0f, juce::Font::bold)));
        caption.setColour (juce::Label::textColourId, textDim);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        slider.setTextValueSuffix (suffix);
        slider.setNumDecimalPlacesToDisplay (decimals);
        slider.getProperties().set ("big", big);

        addAndMakeVisible (caption);
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);

        if (auto* p = apvts.getParameter (paramID))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void resized() override
    {
        auto b = getLocalBounds();
        caption.setBounds (b.removeFromTop (16));
        slider.setBounds (b);
    }

private:
    juce::Label caption;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// Drop-down bound to a choice parameter.
class NeonCombo : public juce::Component
{
public:
    NeonCombo (juce::AudioProcessorValueTreeState& apvts, const char* paramID, const juce::String& captionText)
    {
        caption.setText (captionText, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centredLeft);
        caption.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        caption.setColour (juce::Label::textColourId, textDim);

        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
            box.addItemList (p->choices, 1);

        box.setJustificationType (juce::Justification::centredLeft);

        addAndMakeVisible (caption);
        addAndMakeVisible (box);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramID, box);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        caption.setBounds (b.removeFromTop (16));
        box.setBounds (b.reduced (0, 2));
    }

private:
    juce::Label caption;
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

//==============================================================================
// Processing module: title, on/off switch and four knobs. Knobs dim when the module is off.
class ModulePanel : public juce::Component
{
public:
    struct KnobSpec
    {
        const char* id;
        const char* caption;
        const char* suffix;
        int decimals;
    };

    ModulePanel (juce::AudioProcessorValueTreeState& apvts, const juce::String& titleText,
                 const char* onID, const std::vector<KnobSpec>& specs)
        : title (titleText)
    {
        addAndMakeVisible (toggle);
        toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, onID, toggle);

        for (const auto& s : specs)
        {
            knobs.push_back (std::make_unique<NeonKnob> (apvts, s.id, s.caption, s.suffix, s.decimals));
            addAndMakeVisible (*knobs.back());
        }

        refresh();
    }

    // Called from the editor timer so host automation of the switch is reflected too.
    void refresh()
    {
        const bool on = toggle.getToggleState();

        if (on != lastOn || ! initialised)
        {
            lastOn = on;
            initialised = true;

            for (auto& k : knobs)
                k->setAlpha (on ? 1.0f : 0.35f);

            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = toggle.getToggleState();

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b1713), b.getX(), b.getY(),
                                                 juce::Colour (0xff050a08), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        glowRoundedRect (g, b, 12.0f, on ? neon : neonDim, on ? 1.0f : 0.3f);
        g.setColour (on ? neon.withAlpha (0.7f) : neonDim.withAlpha (0.6f));
        g.drawRoundedRectangle (b, 12.0f, 1.2f);

        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)).withExtraKerningFactor (0.12f));
        g.setColour (on ? text : textDim);
        g.drawText (title, juce::Rectangle<float> (b.getX() + 16.0f, b.getY() + 8.0f, b.getWidth() - 80.0f, 22.0f),
                    juce::Justification::centredLeft);

        g.setColour (neon.withAlpha (on ? 0.25f : 0.1f));
        g.drawHorizontalLine (static_cast<int> (b.getY()) + 36, b.getX() + 14.0f, b.getRight() - 14.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6);
        auto header = b.removeFromTop (36);
        toggle.setBounds (header.removeFromRight (48).reduced (4));

        b.removeFromTop (4);
        const int cw = b.getWidth() / 2;
        const int ch = b.getHeight() / 2;

        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int col = static_cast<int> (i % 2);
            const int row = static_cast<int> (i / 2);
            knobs[i]->setBounds (b.getX() + col * cw, b.getY() + row * ch, cw, ch);
        }
    }

private:
    juce::String title;
    juce::ToggleButton toggle;
    std::vector<std::unique_ptr<NeonKnob>> knobs;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    bool lastOn = true, initialised = false;
};

//==============================================================================
// LFO strip: on/off switch, shape, sync, rate and one depth knob for each destination.
class LfoPanel : public juce::Component
{
public:
    explicit LfoPanel (juce::AudioProcessorValueTreeState& apvts)
        : shape (apvts, epinox::pid::lfoShape, "SHAPE"),
          sync (apvts, epinox::pid::lfoSync, "SYNC"),
          rate (apvts, epinox::pid::lfoRate, "RATE", " Hz", 2),
          pitch (apvts, epinox::pid::lfoPitch, "> PITCH", " %", 0),
          formant (apvts, epinox::pid::lfoFormant, "> FORMANT", " %", 0),
          filter (apvts, epinox::pid::lfoFilter, "> FILTER", " %", 0),
          volume (apvts, epinox::pid::lfoVolume, "> VOLUME", " %", 0)
    {
        addAndMakeVisible (toggle);
        toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, epinox::pid::lfoOn, toggle);

        for (auto* c : std::initializer_list<juce::Component*> { &shape, &sync, &rate, &pitch, &formant, &filter, &volume })
            addAndMakeVisible (c);

        refresh();
    }

    void refresh()
    {
        const bool on = toggle.getToggleState();

        if (on != lastOn || ! initialised)
        {
            lastOn = on;
            initialised = true;

            for (auto* c : std::initializer_list<juce::Component*> { &shape, &sync, &rate, &pitch, &formant, &filter, &volume })
                c->setAlpha (on ? 1.0f : 0.35f);

            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = toggle.getToggleState();

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b1713), b.getX(), b.getY(),
                                                 juce::Colour (0xff050a08), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        glowRoundedRect (g, b, 12.0f, on ? neon : neonDim, on ? 1.0f : 0.3f);
        g.setColour (on ? neon.withAlpha (0.7f) : neonDim.withAlpha (0.6f));
        g.drawRoundedRectangle (b, 12.0f, 1.2f);

        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)).withExtraKerningFactor (0.12f));
        g.setColour (on ? text : textDim);
        g.drawText ("LFO", juce::Rectangle<float> (b.getX() + 16.0f, b.getY() + 10.0f, 60.0f, 22.0f), juce::Justification::centredLeft);

        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::plain)));
        g.setColour (textDim);
        g.drawFittedText ("One LFO moves pitch, formant, a low-pass filter and the level. Set a depth to 0 to leave that one alone.",
                          juce::Rectangle<int> (static_cast<int> (b.getX()) + 16, static_cast<int> (b.getY()) + 40, 130, 62),
                          juce::Justification::topLeft, 5);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6);
        auto left = b.removeFromLeft (160);
        toggle.setBounds (left.removeFromTop (34).removeFromRight (48).reduced (4));

        auto combos = b.removeFromLeft (130);
        shape.setBounds (combos.removeFromTop (b.getHeight() / 2).reduced (4, 4));
        sync.setBounds (combos.reduced (4, 4));

        const int cw = b.getWidth() / 5;
        for (auto* k : std::initializer_list<juce::Component*> { &rate, &pitch, &formant, &filter, &volume })
            k->setBounds (b.removeFromLeft (cw));
    }

private:
    juce::ToggleButton toggle;
    NeonCombo shape, sync;
    NeonKnob rate, pitch, formant, filter, volume;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    bool lastOn = true, initialised = false;
};

//==============================================================================
// Vertical list of genres, bound to the choice parameter.
class GenreList : public juce::Component
{
public:
    explicit GenreList (juce::AudioProcessorValueTreeState& apvts)
        : names (epinox::genreNames())
    {
        if (auto* p = apvts.getParameter (epinox::pid::genre))
        {
            attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v)
            {
                selected = static_cast<int> (v + 0.5f);
                repaint();
            });
            attachment->sendInitialUpdate();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0a1511), b.getX(), b.getY(),
                                                 juce::Colour (0xff050a08), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        glowRoundedRect (g, b, 12.0f, neon, 0.5f);
        g.setColour (neon.withAlpha (0.55f));
        g.drawRoundedRectangle (b, 12.0f, 1.2f);

        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)).withExtraKerningFactor (0.2f));
        g.setColour (textDim);
        g.drawText ("GENRE", juce::Rectangle<float> (b.getX() + 18.0f, b.getY() + 8.0f, b.getWidth() - 30.0f, 22.0f),
                    juce::Justification::centredLeft);

        for (int i = 0; i < names.size(); ++i)
        {
            auto row = rowBounds (i);
            const bool sel = (i == selected);

            if (sel)
            {
                g.setColour (neon.withAlpha (0.12f));
                g.fillRoundedRectangle (row, 8.0f);
                glowRoundedRect (g, row, 8.0f, neon, 0.7f);
                g.setColour (neon.withAlpha (0.9f));
                g.drawRoundedRectangle (row, 8.0f, 1.3f);
                g.setColour (neon);
                g.fillRoundedRectangle (row.getX() + 6.0f, row.getCentreY() - 8.0f, 3.0f, 16.0f, 1.5f);
            }

            g.setFont (juce::Font (juce::FontOptions (14.0f, sel ? juce::Font::bold : juce::Font::plain)));
            g.setColour (sel ? neon : text.withAlpha (0.75f));
            g.drawText (names[i], row.withTrimmedLeft (20.0f), juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < names.size(); ++i)
            if (rowBounds (i).contains (e.position) && attachment != nullptr)
            {
                attachment->setValueAsCompleteGesture (static_cast<float> (i));
                return;
            }
    }

private:
    juce::Rectangle<float> rowBounds (int i) const
    {
        auto b = getLocalBounds().toFloat().reduced (10.0f);
        b.removeFromTop (28.0f);
        const float h = b.getHeight() / static_cast<float> (names.size());
        return juce::Rectangle<float> (b.getX(), b.getY() + h * static_cast<float> (i), b.getWidth(), h).reduced (0.0f, 2.0f);
    }

    juce::StringArray names;
    int selected = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
// Rolling waveform of the plugin output (4 second window), fed by the audio thread.
class WaveformView : public juce::Component, private juce::Timer
{
public:
    explicit WaveformView (VoxProcessor& p) : proc (p)
    {
        data.fill (0.0f);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (juce::Colour (0xff040907));
        g.fillRoundedRectangle (b, 12.0f);

        g.setColour (neon.withAlpha (0.05f));
        for (float x = b.getX() + 30.0f; x < b.getRight(); x += 30.0f)
            g.drawVerticalLine (static_cast<int> (x), b.getY() + 4.0f, b.getBottom() - 4.0f);
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine (static_cast<int> (b.getY() + b.getHeight() * static_cast<float> (i) / 4.0f), b.getX() + 4.0f, b.getRight() - 4.0f);

        auto area = b.reduced (12.0f, 22.0f);
        const float mid = area.getCentreY();
        const float half = area.getHeight() * 0.5f;

        bool any = false;
        for (float v : data)
            if (v > 0.0005f) { any = true; break; }

        if (! any)
        {
            g.setColour (neon.withAlpha (0.45f));
            g.drawHorizontalLine (static_cast<int> (mid), area.getX(), area.getRight());
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.2f));
            g.setColour (textDim);
            g.drawText ("AWAITING SIGNAL", area, juce::Justification::centred);
        }
        else
        {
            const auto amp = [] (float v) { return std::pow (juce::jlimit (0.0f, 1.0f, v), 0.6f); };
            const int n = static_cast<int> (data.size());
            const auto xAt = [&] (int i) { return area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (n - 1); };

            juce::Path p;
            p.startNewSubPath (xAt (0), mid - amp (data[0]) * half);

            for (int i = 1; i < n; ++i)
                p.lineTo (xAt (i), mid - amp (data[static_cast<size_t> (i)]) * half);

            for (int i = n - 1; i >= 0; --i)
                p.lineTo (xAt (i), mid + amp (data[static_cast<size_t> (i)]) * half);

            p.closeSubPath();

            g.setColour (neon.withAlpha (0.18f));
            g.strokePath (p, juce::PathStrokeType (5.0f));
            g.setGradientFill (juce::ColourGradient (neon.withAlpha (0.9f), area.getX(), 0.0f,
                                                     cyan.withAlpha (0.9f), area.getRight(), 0.0f, false));
            g.fillPath (p);
        }

        glowRoundedRect (g, b, 12.0f, neon, 0.5f);
        g.setColour (neon.withAlpha (0.55f));
        g.drawRoundedRectangle (b, 12.0f, 1.2f);

        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.2f));
        g.setColour (textDim);
        g.drawText ("OUTPUT", juce::Rectangle<float> (b.getX() + 16.0f, b.getY() + 5.0f, 120.0f, 16.0f),
                    juce::Justification::centredLeft);
    }

private:
    void timerCallback() override
    {
        const int start = proc.scopeWrite.load();

        for (int i = 0; i < VoxProcessor::scopeSize; ++i)
            data[static_cast<size_t> (i)] = proc.scope[static_cast<size_t> ((start + i) % VoxProcessor::scopeSize)].load();

        repaint();
    }

    VoxProcessor& proc;
    std::array<float, VoxProcessor::scopeSize> data;
};

//==============================================================================
// Segmented peak/RMS meter.
class MeterBar : public juce::Component
{
public:
    explicit MeterBar (const juce::String& labelText) : label (labelText) {}

    void setLevels (float peak, float rms)
    {
        displayPeak = juce::jmax (peak, displayPeak * 0.88f);
        displayRms = rms * 0.3f + displayRms * 0.7f;

        if (clipHold > 0)
            --clipHold;

        repaint();
    }

    void triggerClip() { clipHold = 45; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        auto textArea = b.removeFromBottom (16.0f);
        auto bar = b.reduced (b.getWidth() * 0.3f, 2.0f);

        constexpr int segs = 24;
        const float segH = bar.getHeight() / static_cast<float> (segs);
        const auto frac = [] (float lin) { return juce::jmap (juce::Decibels::gainToDecibels (lin, -60.0f), -60.0f, 0.0f, 0.0f, 1.0f); };
        const float pf = frac (displayPeak);
        const float rf = frac (displayRms);

        for (int i = 0; i < segs; ++i)
        {
            const float f = static_cast<float> (i + 1) / static_cast<float> (segs);
            auto seg = juce::Rectangle<float> (bar.getX(), bar.getBottom() - static_cast<float> (i + 1) * segH + 1.0f,
                                               bar.getWidth(), segH - 2.0f);
            const auto col = f > 0.92f ? danger : (f > 0.74f ? warn : neon);
            const bool lit = pf >= f - 0.5f / static_cast<float> (segs);
            const bool rmsLit = rf >= f;

            g.setColour (lit ? col : (rmsLit ? col.withAlpha (0.5f) : neonDim.withAlpha (0.18f)));
            g.fillRoundedRectangle (seg, 1.5f);
        }

        if (clipHold > 0)
        {
            g.setColour (danger);
            g.fillRoundedRectangle (bar.getX() - 2.0f, b.getY() - 1.0f, bar.getWidth() + 4.0f, 3.0f, 1.5f);
        }

        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.15f));
        g.setColour (textDim);
        g.drawText (label, textArea, juce::Justification::centred);
    }

private:
    juce::String label;
    float displayPeak = 0.0f, displayRms = 0.0f;
    int clipHold = 0;
};
} // namespace epinox::ui
