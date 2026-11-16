#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace epinox
{
namespace colours
{
    inline const juce::Colour bg      { 0xff03060a };
    inline const juce::Colour panel   { 0xff09110f };
    inline const juce::Colour neon    { 0xff2bffa0 };
    inline const juce::Colour neonDim { 0xff0f6b45 };
    inline const juce::Colour cyan    { 0xff30e8ff };
    inline const juce::Colour text    { 0xffd6fff0 };
    inline const juce::Colour textDim { 0xff6f9d8d };
    inline const juce::Colour warn    { 0xffffb020 };
    inline const juce::Colour danger  { 0xffff3d5a };
}

namespace ui
{
    // Soft neon halo drawn around a rounded rectangle.
    void glowRoundedRect (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c, float strength);

    class NeonLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        NeonLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
    };
} // namespace ui
} // namespace epinox
