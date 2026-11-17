#include "NeonLookAndFeel.h"

using namespace epinox::colours;

namespace epinox::ui
{
void glowRoundedRect (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c, float strength)
{
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (c.withAlpha (strength * 0.05f * static_cast<float> (5 - i)));
        g.drawRoundedRectangle (r.expanded (static_cast<float> (i) * 1.5f), corner + static_cast<float> (i) * 1.5f, 2.0f);
    }
}

NeonLookAndFeel::NeonLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, neonDim);

    setColour (juce::Label::textColourId, text);

    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, neon);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff070d0b));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, neonDim);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff060c0a));
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, neonDim);
    setColour (juce::TextEditor::focusedOutlineColourId, neon);
    setColour (juce::TextEditor::highlightColourId, neonDim);
    setColour (juce::CaretComponent::caretColourId, neon);
}

void NeonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const bool big = static_cast<bool> (slider.getProperties().getWithDefault ("big", false));

    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height)).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float thick = big ? 8.0f : 4.0f;
    const float arcR = radius - thick * 0.5f - 1.0f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    const juce::PathStrokeType::JointStyle curved = juce::PathStrokeType::curved;
    const juce::PathStrokeType::EndCapStyle rounded = juce::PathStrokeType::rounded;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (neonDim.withAlpha (0.35f));
    g.strokePath (track, juce::PathStrokeType (thick, curved, rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);

    for (int i = 3; i >= 1; --i)
    {
        g.setColour (neon.withAlpha (0.07f * static_cast<float> (4 - i)));
        g.strokePath (value, juce::PathStrokeType (thick + static_cast<float> (i) * 3.0f, curved, rounded));
    }

    g.setColour (neon);
    g.strokePath (value, juce::PathStrokeType (thick, curved, rounded));

    const float bodyR = arcR - thick - 3.0f;
    juce::ColourGradient body (juce::Colour (0xff1d2a26), centre.x - bodyR * 0.4f, centre.y - bodyR * 0.5f,
                               juce::Colour (0xff040706), centre.x + bodyR * 0.6f, centre.y + bodyR * 0.7f, true);
    g.setGradientFill (body);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    g.setColour (neon.withAlpha (0.25f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.2f);

    const auto tip = [&] (float r) { return juce::Point<float> (centre.x + r * std::sin (angle), centre.y - r * std::cos (angle)); };
    const auto a = tip (bodyR * 0.35f);
    const auto b = tip (bodyR * 0.92f);

    g.setColour (neon.withAlpha (0.25f));
    g.drawLine (a.x, a.y, b.x, b.y, 6.0f);
    g.setColour (neon);
    g.drawLine (a.x, a.y, b.x, b.y, 2.4f);
}

void NeonLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const bool on = button.getToggleState();
    auto b = button.getLocalBounds().toFloat();

    if (button.getComponentID() == "power")
    {
        const auto c = b.getCentre();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 3.0f;
        const auto col = on ? neon : neonDim;

        if (on)
        {
            g.setColour (neon.withAlpha (0.10f));
            g.fillEllipse (c.x - r - 4.0f, c.y - r - 4.0f, (r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f);
        }

        g.setColour (col.withAlpha (highlighted ? 1.0f : 0.8f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.6f);

        juce::Path icon;
        icon.addCentredArc (c.x, c.y, r * 0.5f, r * 0.5f, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        icon.startNewSubPath (c.x, c.y - r * 0.62f);
        icon.lineTo (c.x, c.y - r * 0.1f);
        g.setColour (col);
        g.strokePath (icon, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    // Compact on/off switch used by the processing modules.
    auto sw = juce::Rectangle<float> (34.0f, 16.0f).withCentre (b.getCentre());
    g.setColour (on ? neon.withAlpha (0.22f) : juce::Colour (0xff0a1210));
    g.fillRoundedRectangle (sw, 8.0f);

    if (on)
        glowRoundedRect (g, sw, 8.0f, neon, 0.8f);

    g.setColour (on ? neon : neonDim);
    g.drawRoundedRectangle (sw, 8.0f, 1.3f);

    const float knobX = on ? sw.getRight() - 9.0f : sw.getX() + 9.0f;
    g.setColour (on ? neon : textDim);
    g.fillEllipse (knobX - 5.0f, sw.getCentreY() - 5.0f, 10.0f, 10.0f);
}

void NeonLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool highlighted, bool down)
{
    auto b = button.getLocalBounds().toFloat().reduced (2.0f);
    const bool generate = button.getComponentID() == "generate";
    const float corner = generate ? 10.0f : 7.0f;

    if (generate)
    {
        glowRoundedRect (g, b, corner, neon, down ? 0.7f : (highlighted ? 1.6f : 1.1f));
        g.setGradientFill (juce::ColourGradient (neon.brighter (0.2f), b.getX(), b.getY(),
                                                 neon.darker (0.25f), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, corner);
        return;
    }

    g.setColour (highlighted ? neon.withAlpha (0.14f) : juce::Colour (0xff08100d));
    g.fillRoundedRectangle (b, corner);

    if (highlighted)
        glowRoundedRect (g, b, corner, neon, 0.6f);

    g.setColour (down ? neon : neon.withAlpha (0.55f));
    g.drawRoundedRectangle (b, corner, 1.2f);
}

void NeonLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const bool generate = button.getComponentID() == "generate";

    g.setFont (juce::Font (juce::FontOptions (generate ? 17.0f : 12.0f, juce::Font::bold)));
    g.setColour (generate ? juce::Colour (0xff021208) : neon);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
}

void NeonLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto b = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (1.5f);

    g.setColour (juce::Colour (0xff08100d));
    g.fillRoundedRectangle (b, 8.0f);
    glowRoundedRect (g, b, 8.0f, neon, box.hasKeyboardFocus (true) ? 0.9f : 0.3f);
    g.setColour (neon.withAlpha (0.6f));
    g.drawRoundedRectangle (b, 8.0f, 1.2f);

    const float cx = b.getRight() - 16.0f;
    const float cy = b.getCentreY();
    juce::Path arrow;
    arrow.addTriangle (cx - 5.0f, cy - 2.5f, cx + 5.0f, cy - 2.5f, cx, cy + 3.5f);
    g.setColour (neon);
    g.fillPath (arrow);
}

juce::Font NeonLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (14.0f, juce::Font::bold));
}
} // namespace epinox::ui
