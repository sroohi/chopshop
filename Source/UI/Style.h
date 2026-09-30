#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace ui
{
namespace col
{
    const juce::Colour bg        { 0xff111214 };
    const juce::Colour chassis   { 0xff17181b };
    const juce::Colour panel     { 0xff1e1f23 };
    const juce::Colour panelEdge { 0xff2d2e33 };
    const juce::Colour screen    { 0xff0b0d10 };
    const juce::Colour text      { 0xffe4e4e7 };
    const juce::Colour dim       { 0xff8b8c93 };
    const juce::Colour faint     { 0xff4a4b52 };
    const juce::Colour accent    { 0xffe8412c };
    const juce::Colour amber     { 0xffffb03a };
    const juce::Colour pad       { 0xff2a2b30 };
    const juce::Colour knob      { 0xff303137 };
}

inline juce::Colour sliceColour (int i)
{
    const float hue = std::fmod (0.02f + (float) i * 0.61803f, 1.0f);
    return juce::Colour::fromHSV (hue, 0.62f, 0.98f, 1.0f);
}

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
class ChopLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChopLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, col::panel);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextButton::textColourOffId, col::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, col::panel);
        setColour (juce::TooltipWindow::textColourId, col::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                           float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float ringW = juce::jmax (2.5f, radius * 0.12f);
        const float arcR = radius - ringW * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (col::faint.withAlpha (0.55f));
        g.strokePath (track, { ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path val;
            val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (accent);
            g.strokePath (val, { ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        const float bodyR = radius - ringW - 3.0f;
        juce::ColourGradient grad (col::knob.brighter (0.25f), c.x, c.y - bodyR, col::knob.darker (0.5f), c.x, c.y + bodyR, false);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        const auto tip = c.getPointOnCircumference (bodyR * 0.85f, angle);
        const auto base = c.getPointOnCircumference (bodyR * 0.3f, angle);
        g.setColour (col::text);
        g.drawLine ({ base, tip }, 2.2f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool lit = b.getToggleState() || (bool) b.getProperties().getWithDefault ("active", false);
        const bool big = (bool) b.getProperties().getWithDefault ("perf", false);
        const float corner = big ? 6.0f : 4.0f;

        if (lit)
        {
            if (big)
            {
                g.setColour (col::accent.withAlpha (0.35f));
                g.fillRoundedRectangle (r.expanded (1.5f), corner + 1.5f);
            }
            juce::ColourGradient grad (col::accent.brighter (0.2f), r.getX(), r.getY(), col::accent.darker (0.3f), r.getX(), r.getBottom(), false);
            g.setGradientFill (grad);
        }
        else
        {
            auto base = col::pad.brighter (over ? 0.15f : 0.0f).darker (down ? 0.2f : 0.0f);
            juce::ColourGradient grad (base.brighter (0.12f), r.getX(), r.getY(), base.darker (0.25f), r.getX(), r.getBottom(), false);
            g.setGradientFill (grad);
        }
        g.fillRoundedRectangle (r, corner);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        const bool big = (bool) b.getProperties().getWithDefault ("perf", false);
        return font (big ? 14.0f : juce::jmin (12.5f, (float) h * 0.5f), true);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool onState = b.getToggleState();
        g.setColour (onState ? col::accent.withAlpha (0.18f) : col::pad.brighter (over ? 0.1f : 0.0f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (onState ? col::accent.withAlpha (0.8f) : col::panelEdge.brighter (0.2f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

        const float led = r.getHeight() * 0.36f;
        auto ledR = juce::Rectangle<float> (led, led).withCentre ({ r.getX() + r.getHeight() * 0.5f + 1.0f, r.getCentreY() });
        if (onState)
        {
            g.setColour (col::accent.withAlpha (0.4f));
            g.fillEllipse (ledR.expanded (2.5f));
        }
        g.setColour (onState ? col::accent.brighter (0.3f) : col::faint);
        g.fillEllipse (ledR);

        g.setColour (onState ? col::text : col::dim);
        g.setFont (font (11.0f, true));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (r.getHeight() * 0.95f).withTrimmedRight (4.0f),
                    juce::Justification::centred);
    }
};

//==============================================================================
/** Rotary knob with a caption underneath that switches to the value while hovered or dragged. */
class Knob : public juce::Component
{
public:
    explicit Knob (juce::String captionIn, bool bipolar = false, juce::Colour colour = col::accent)
        : caption (std::move (captionIn))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (180);
        slider.setVelocityBasedMode (false);
        slider.getProperties().set ("bipolar", bipolar);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.onValueChange = [this] { repaint(); };
        slider.addMouseListener (this, false);
        addAndMakeVisible (slider);
    }

    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        attachment.reset();
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
        if (auto* prm = state.getParameter (id))
        {
            slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
            slider.setTooltip (prm->getName (64));
        }
        repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromBottom (15);
        const int d = juce::jmin (b.getWidth(), b.getHeight());
        slider.setBounds (b.withSizeKeepingCentre (d, d));
    }

    void paint (juce::Graphics& g) override
    {
        auto label = getLocalBounds().removeFromBottom (15);
        const bool showValue = hovered || slider.isMouseButtonDown();
        g.setColour (showValue ? col::amber : col::dim);
        g.setFont (font (10.5f, true));
        g.drawFittedText (showValue ? slider.getTextFromValue (slider.getValue()) : caption, label,
                          juce::Justification::centred, 1, 0.8f);
    }

    void mouseEnter (const juce::MouseEvent&) override { hovered = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovered = false; repaint(); }

    juce::Slider slider;

private:
    juce::String caption;
    bool hovered = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
} // namespace ui
