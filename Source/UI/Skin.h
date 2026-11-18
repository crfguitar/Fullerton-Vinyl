#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fv
{

/** Layout of the editor at 1x. Everything is drawn in these coordinates and the
    whole editor is scaled with a transform when the window is resized. */
namespace geo
{
    inline constexpr int   width = 1040, height = 580;

    inline constexpr float cx = 300.0f, cy = 276.0f;      // platter centre
    inline constexpr float platterR = 222.0f;
    inline constexpr float recordR  = 198.0f;
    inline constexpr float labelR   = 66.0f;

    inline constexpr float panelX = 684.0f, panelY = 20.0f, panelW = 336.0f, panelH = 540.0f;

    inline constexpr float knobLarge = 92.0f, knobSmall = 50.0f;
    inline constexpr float rpm = 33.3333f;
}

namespace col
{
    inline const juce::Colour silk   { 0xffcfc9bb };   // silk-screened legends
    inline const juce::Colour amber  { 0xffffa833 };   // display / LEDs
    inline const juce::Colour green  { 0xff5dff8a };
    inline const juce::Colour red    { 0xffff4636 };
}

juce::Font uiFont (float height, bool bold = true, float kerning = 0.0f);
juce::Font monoFont (float height, bool bold = true);

/** Every bitmap in the GUI is rendered procedurally, once, at 2x resolution.
    Shared between all open editors through a SharedResourcePointer. */
struct Skin
{
    Skin();

    static constexpr float ss = 2.0f;   // supersampling factor of every image

    juce::Image background;     // deck plate, control panel, platter, legends
    juce::Image dots;           // strobe dots on the platter rim (rotates)
    juce::Image record;         // vinyl with its light reflections (static)
    juce::Image recordSpin;     // label + scuffs (rotates)
    juce::Image tonearm;        // tonearm and its shadow (static, on top)
    juce::Image knobLarge, knobSmall;

    juce::Rectangle<float> tonearmBounds;
};

} // namespace fv
