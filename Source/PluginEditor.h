#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class FullertonVinylEditor final : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit FullertonVinylEditor (FullertonVinylProcessor&);
    ~FullertonVinylEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Advances the platter animation by dt seconds using the processor's
        current speed. Called by the timer; public so tools can step it. */
    void tick (double dtSeconds);

private:
    void timerCallback() override;

    class Content;

    FullertonVinylProcessor& vinyl;
    std::unique_ptr<Content> content;
    double lastTickMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullertonVinylEditor)
};
