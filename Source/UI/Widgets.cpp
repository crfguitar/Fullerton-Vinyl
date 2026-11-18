#include "Widgets.h"
#include "../DSP/VinylEngine.h"

namespace fv
{

using juce::Colour;
using juce::Graphics;

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr float knobStart = pi * 1.25f, knobEnd = pi * 2.75f;

    void glowDot (Graphics& g, juce::Rectangle<float> r, Colour c, float glow)
    {
        if (glow > 0.0f)
        {
            const auto halo = r.expanded (r.getHeight() * 1.6f);
            juce::ColourGradient grad (c.withAlpha (0.45f * glow), halo.getCentreX(), halo.getCentreY(),
                                       c.withAlpha (0.0f), halo.getRight(), halo.getCentreY(), true);
            g.setGradientFill (grad);
            g.fillEllipse (halo);
        }

        g.setColour (Colour (0xff0a0a0b));
        g.fillRoundedRectangle (r.expanded (0.8f), r.getHeight() * 0.5f + 0.8f);
        g.setColour (c.interpolatedWith (Colour (0xff1a1a1a), 1.0f - juce::jlimit (0.0f, 1.0f, glow) * 0.88f - 0.12f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

        if (glow > 0.3f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.45f * glow));
            g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.25f, r.getHeight() * 0.3f).translated (0.0f, -r.getHeight() * 0.12f),
                                    r.getHeight() * 0.2f);
        }
    }
}

//==============================================================================
Knob::Knob (const juce::String& legendText, bool isLarge)
    : legend (legendText), large (isLarge)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (knobStart, knobEnd, true);
    setMouseDragSensitivity (220);
    setRepaintsOnMouseActivity (false);
}

// Right-clicks never move the control: they belong to the host's parameter menu.
void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu != nullptr)
            onContextMenu();

        return;
    }

    juce::Slider::mouseDown (e);
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Slider::mouseDrag (e);
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Slider::mouseUp (e);
}

void Knob::paint (Graphics& g)
{
    const float D = large ? geo::knobLarge : geo::knobSmall;
    const float R = D * 0.5f;
    const float cx = (float) getWidth() * 0.5f;
    const float cy = (large ? 18.0f : 13.0f) + R;

    // Scale marks.
    const int ticks = 11;

    for (int i = 0; i < ticks; ++i)
    {
        const float a = knobStart + (knobEnd - knobStart) * (float) i / (float) (ticks - 1);
        const bool major = (i % 5) == 0;
        const float r1 = R + (large ? 5.0f : 4.0f);
        const float r2 = r1 + (major ? (large ? 6.0f : 4.5f) : (large ? 3.5f : 2.5f));
        const float sn = std::sin (a), cs = -std::cos (a);
        g.setColour (col::silk.withAlpha (major ? 0.85f : 0.42f));
        g.drawLine (cx + sn * r1, cy + cs * r1, cx + sn * r2, cy + cs * r2, major ? 1.3f : 1.0f);
    }

    // Shadow cast down and to the right.
    {
        const float sr = R * 1.32f;
        const float sx = cx + R * 0.10f, sy = cy + R * 0.26f;
        juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.75f), sx, sy,
                                     juce::Colours::transparentBlack, sx + sr, sy, true);
        shadow.addColour (0.62, juce::Colours::black.withAlpha (0.55f));
        g.setGradientFill (shadow);
        g.fillEllipse (sx - sr, sy - sr, sr * 2.0f, sr * 2.0f);
    }

    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (large ? skin->knobLarge : skin->knobSmall, juce::Rectangle<float> (cx - R, cy - R, D, D));

    // Engraved pointer on the cap.
    {
        const float pos = (float) valueToProportionOfLength (getValue());
        const float angle = knobStart + pos * (knobEnd - knobStart);
        const float w = juce::jmax (1.8f, R * 0.085f);
        juce::Path pointer;
        pointer.addRoundedRectangle (-w * 0.5f, -R * 0.635f, w, R * 0.40f, w * 0.5f);
        const auto tf = juce::AffineTransform::rotation (angle).translated (cx, cy);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillPath (pointer, tf.translated (0.5f, 0.6f));
        g.setColour (Colour (0xff131416));
        g.fillPath (pointer, tf);
    }

    const float textY = cy + R + (large ? 15.0f : 11.0f);
    g.setColour (col::silk.withAlpha (0.9f));
    g.setFont (uiFont (large ? 11.0f : 9.4f, true, large ? 0.20f : 0.16f));
    g.drawText (legend, juce::Rectangle<float> (0.0f, textY, (float) getWidth(), 13.0f), juce::Justification::centred, false);

    g.setColour (col::amber.withAlpha (isEnabled() ? 0.95f : 0.4f));
    g.setFont (monoFont (large ? 13.5f : 11.0f));
    g.drawText (getTextFromValue (getValue()),
                juce::Rectangle<float> (0.0f, textY + (large ? 15.0f : 13.0f), (float) getWidth(), 14.0f),
                juce::Justification::centred, false);
}

//==============================================================================
StartStopButton::StartStopButton() : juce::Button ("Start / Stop")
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

// Right-clicks never move the control: they belong to the host's parameter menu.
void StartStopButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu != nullptr)
            onContextMenu();

        return;
    }

    juce::Button::mouseDown (e);
}

void StartStopButton::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Button::mouseDrag (e);
}

void StartStopButton::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Button::mouseUp (e);
}

void StartStopButton::setEngineState (int newState, bool blinkPhase)
{
    if (newState != engineState || blinkPhase != blink)
    {
        engineState = newState;
        blink = blinkPhase;
        repaint();
    }
}

void StartStopButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat();

    // Shadow and aluminium bezel.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (b.reduced (1.0f).translated (1.0f, 2.0f), 5.0f);

    auto bezel = b.reduced (2.0f);
    juce::ColourGradient metal (Colour (0xffe4e5e7), bezel.getX(), bezel.getY(),
                                Colour (0xff5b5e63), bezel.getX(), bezel.getBottom(), false);
    metal.addColour (0.5, Colour (0xff9da0a5));
    g.setGradientFill (metal);
    g.fillRoundedRectangle (bezel, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bezel, 5.0f, 0.7f);

    // Rubberised cap that sinks when pressed.
    auto cap = bezel.reduced (3.5f).translated (0.0f, down ? 1.0f : 0.0f);
    juce::ColourGradient rubber (Colour (down ? 0xff1c1d20 : 0xff303236), cap.getX(), cap.getY(),
                                 Colour (down ? 0xff111113 : 0xff17181a), cap.getX(), cap.getBottom(), false);
    g.setGradientFill (rubber);
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (cap, 3.0f, 0.8f);

    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.16f : 0.09f));
        g.fillRoundedRectangle (cap.reduced (1.2f).withHeight (1.2f), 0.6f);
    }

    // Status LED: green = playing, red = stopped, amber blink = platter moving between the two.
    using S = VinylEngine::State;
    const auto st = (S) engineState;
    Colour led = col::green;
    float glow = 1.0f;

    if (st == S::stopped)                        { led = col::red; }
    else if (st == S::braking || st == S::starting) { led = col::amber; glow = blink ? 1.0f : 0.25f; }

    glowDot (g, juce::Rectangle<float> (cap.getCentreX() - 15.0f, cap.getY() + 9.0f, 30.0f, 5.0f), led, glow);

    g.setColour (col::silk.withAlpha (0.92f));
    g.setFont (uiFont (10.0f, true, 0.18f));
    g.drawText (juce::String ("START ") + juce::String::charToString ((juce::juce_wchar) 0x00b7) + " STOP",
                cap.withTrimmedTop (18.0f), juce::Justification::centred, false);
}

//==============================================================================
SlideSwitch::SlideSwitch (const juce::String& legend) : juce::Button (legend)
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

// Right-clicks never move the control: they belong to the host's parameter menu.
void SlideSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu != nullptr)
            onContextMenu();

        return;
    }

    juce::Button::mouseDown (e);
}

void SlideSwitch::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Button::mouseDrag (e);
}

void SlideSwitch::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Button::mouseUp (e);
}

void SlideSwitch::paintButton (Graphics& g, bool highlighted, bool)
{
    const bool on = getToggleState();
    const float h = 18.0f, w = 38.0f;
    const juce::Rectangle<float> track (1.0f, ((float) getHeight() - h) * 0.5f, w, h);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (track.translated (0.0f, 1.0f), h * 0.5f);
    juce::ColourGradient slot (Colour (0xff050506), track.getX(), track.getY(),
                               Colour (0xff1b1c1f), track.getX(), track.getBottom(), false);
    g.setGradientFill (slot);
    g.fillRoundedRectangle (track, h * 0.5f);

    if (on)
    {
        g.setColour (col::amber.withAlpha (0.22f));
        g.fillRoundedRectangle (track.reduced (2.0f), h * 0.5f - 2.0f);
    }

    const float d = h - 4.0f;
    const float x = on ? track.getRight() - d - 2.0f : track.getX() + 2.0f;
    const juce::Rectangle<float> thumb (x, track.getY() + 2.0f, d, d);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (thumb.translated (0.6f, 1.2f));
    juce::ColourGradient metal (Colour (0xfff3f4f5), thumb.getX() + 3.0f, thumb.getY() + 2.0f,
                                Colour (0xff6c6f75), thumb.getRight(), thumb.getBottom(), true);
    g.setGradientFill (metal);
    g.fillEllipse (thumb);

    glowDot (g, juce::Rectangle<float> (track.getRight() + 10.0f, track.getCentreY() - 2.5f, 5.0f, 5.0f),
             col::amber, on ? 1.0f : 0.0f);

    g.setColour (col::silk.withAlpha (highlighted ? 1.0f : 0.88f));
    g.setFont (uiFont (9.6f, true, 0.18f));
    g.drawText (getName(), juce::Rectangle<float> (track.getRight() + 24.0f, 0.0f, (float) getWidth(), (float) getHeight()),
                juce::Justification::centredLeft, false);
}

//==============================================================================
void Display::setReadout (float newSpeed, int newState)
{
    if (newState != state || std::abs (newSpeed - speed) > 0.0004f)
    {
        speed = newSpeed;
        state = newState;
        repaint();
    }
}

void Display::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Recess with a glass front.
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 5.0f);
    g.setColour (Colour (0xff050505));
    g.fillRoundedRectangle (b, 5.0f);

    auto glass = b.reduced (2.0f);
    juce::ColourGradient tint (Colour (0xff1b1208), glass.getX(), glass.getY(),
                               Colour (0xff0b0704), glass.getX(), glass.getBottom(), false);
    g.setGradientFill (tint);
    g.fillRoundedRectangle (glass, 3.5f);

    using S = VinylEngine::State;
    const auto st = (S) state;

    // RPM.
    const float rpm = geo::rpm * speed;
    auto left = glass.reduced (12.0f, 0.0f).withWidth (112.0f);
    g.setFont (monoFont (34.0f));
    g.setColour (col::amber.withAlpha (0.10f));
    g.drawText ("88.8", left, juce::Justification::centredLeft, false);
    g.setColour (col::amber);
    g.drawText (juce::String (rpm, 1).paddedLeft (' ', 4), left, juce::Justification::centredLeft, false);

    g.setFont (uiFont (8.6f, true, 0.2f));
    g.setColour (col::amber.withAlpha (0.7f));
    g.drawText ("RPM", juce::Rectangle<float> (left.getRight() - 30.0f, glass.getBottom() - 21.0f, 30.0f, 10.0f),
                juce::Justification::centredLeft, false);

    // State and pitch.
    const char* name = "PLAYING";
    if (st == S::braking)       name = "BRAKING";
    else if (st == S::stopped)  name = "STOPPED";
    else if (st == S::starting) name = "SPIN-UP";

    juce::String pitch = "0.0 st";
    if (st == S::stopped || speed <= 0.0005f)
        pitch = "- - -";
    else if (speed < 0.9995f)
        pitch = juce::String (12.0f * std::log2 (speed), 1) + " st";

    auto right = glass.reduced (12.0f, 9.0f);
    right.removeFromLeft (118.0f);

    g.setColour (col::amber);
    g.setFont (uiFont (11.5f, true, 0.22f));
    g.drawText (name, right.removeFromTop (14.0f), juce::Justification::centredRight, false);
    g.setColour (col::amber.withAlpha (0.75f));
    g.setFont (monoFont (12.5f));
    g.drawText (pitch, right.removeFromTop (16.0f), juce::Justification::centredRight, false);

    // Speed bar.
    const int segs = 20;
    auto bar = right.removeFromBottom (6.0f);
    const float segW = bar.getWidth() / (float) segs;
    const int lit = juce::roundToInt (speed * (float) segs);

    for (int i = 0; i < segs; ++i)
    {
        g.setColour (col::amber.withAlpha (i < lit ? 0.95f : 0.12f));
        g.fillRect (bar.getX() + (float) i * segW, bar.getY(), segW - 1.6f, bar.getHeight());
    }

    // Reflection on the glass.
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.07f), glass.getX(), glass.getY(),
                                juce::Colours::transparentWhite, glass.getX(), glass.getCentreY(), false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (glass.withHeight (glass.getHeight() * 0.5f), 3.5f);
}

} // namespace fv
