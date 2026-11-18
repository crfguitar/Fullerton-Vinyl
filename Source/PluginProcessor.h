#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "DSP/VinylEngine.h"

namespace fv::id
{
    inline constexpr const char* stop      = "stop";
    inline constexpr const char* stopTime  = "stopTime";
    inline constexpr const char* startTime = "startTime";
    inline constexpr const char* sync      = "sync";
    inline constexpr const char* stopDiv   = "stopDiv";
    inline constexpr const char* startDiv  = "startDiv";
    inline constexpr const char* curve     = "curve";
    inline constexpr const char* fade      = "fade";
    inline constexpr const char* crackle   = "crackle";
    inline constexpr const char* output    = "output";
}

class FullertonVinylProcessor final : public juce::AudioProcessor
{
public:
    FullertonVinylProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "Fullerton Vinyl"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return fv::VinylEngine::kMaxRampSeconds; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Length in quarter notes of each tempo-sync division. */
    static double divisionBeats (int index) noexcept;
    static juce::StringArray divisionNames();

    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor to animate the platter.
    std::atomic<float> uiSpeed { 1.0f };
    std::atomic<int>   uiState { 0 };

    int editorWidth = 1040, editorHeight = 580;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    fv::VinylEngine engine;
    juce::SmoothedValue<float> outGain { 1.0f };
    double bpm = 120.0;

    std::atomic<float>* pStop = nullptr;
    std::atomic<float>* pStopTime = nullptr;
    std::atomic<float>* pStartTime = nullptr;
    std::atomic<float>* pSync = nullptr;
    std::atomic<float>* pStopDiv = nullptr;
    std::atomic<float>* pStartDiv = nullptr;
    std::atomic<float>* pCurve = nullptr;
    std::atomic<float>* pFade = nullptr;
    std::atomic<float>* pCrackle = nullptr;
    std::atomic<float>* pOutput = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullertonVinylProcessor)
};
