#pragma once

#include "Skin.h"

namespace fv
{

/** Aluminium-capped rotary knob with its legend and value read-out underneath. */
class Knob final : public juce::Slider
{
public:
    Knob (const juce::String& legend, bool isLarge);
    void paint (juce::Graphics&) override;

    /** Right-click: opens the host's menu for this parameter (automation etc). */
    std::function<void()> onContextMenu;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::SharedResourcePointer<Skin> skin;
    juce::String legend;
    bool large;
};

/** The deck's START / STOP button. Toggled on = platter stopped. */
class StartStopButton final : public juce::Button
{
public:
    StartStopButton();
    void setEngineState (int newState, bool blinkPhase);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    std::function<void()> onContextMenu;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int engineState = 0;
    bool blink = true;
};

/** Small slide switch with a legend to its right. */
class SlideSwitch final : public juce::Button
{
public:
    explicit SlideSwitch (const juce::String& legend);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    std::function<void()> onContextMenu;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
};

/** Recessed amber read-out: platter RPM, transport state, pitch offset. */
class Display final : public juce::Component
{
public:
    void setReadout (float newSpeed, int newState);
    void paint (juce::Graphics&) override;

private:
    float speed = 1.0f;
    int state = 0;
};

} // namespace fv
