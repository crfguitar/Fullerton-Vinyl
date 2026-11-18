#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String timeText (float v, int)
    {
        return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                        : juce::String (v, 2) + " s";
    }

    juce::String percentText (float v, int)
    {
        return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    }

    juce::String curveText (float v, int)
    {
        const int n = juce::roundToInt (v * 100.0f);
        return n == 0 ? juce::String ("LINEAR") : (n > 0 ? "+" + juce::String (n) : juce::String (n));
    }

    juce::String dbText (float v, int)
    {
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    }
}

FullertonVinylProcessor::FullertonVinylProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FullertonVinyl", createLayout())
{
    pStop      = apvts.getRawParameterValue (fv::id::stop);
    pStopTime  = apvts.getRawParameterValue (fv::id::stopTime);
    pStartTime = apvts.getRawParameterValue (fv::id::startTime);
    pSync      = apvts.getRawParameterValue (fv::id::sync);
    pStopDiv   = apvts.getRawParameterValue (fv::id::stopDiv);
    pStartDiv  = apvts.getRawParameterValue (fv::id::startDiv);
    pCurve     = apvts.getRawParameterValue (fv::id::curve);
    pFade      = apvts.getRawParameterValue (fv::id::fade);
    pCrackle   = apvts.getRawParameterValue (fv::id::crackle);
    pOutput    = apvts.getRawParameterValue (fv::id::output);
}

juce::StringArray FullertonVinylProcessor::divisionNames()
{
    return { "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS" };
}

double FullertonVinylProcessor::divisionBeats (int index) noexcept
{
    static constexpr double beats[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
    return beats[juce::jlimit (0, 6, index)];
}

juce::AudioProcessorValueTreeState::ParameterLayout FullertonVinylProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const NormalisableRange<float> timeRange (0.05f, (float) fv::VinylEngine::kMaxRampSeconds, 0.0f, 0.35f);

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { fv::id::stop, 1 }, "Stop", false));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { fv::id::stopTime, 1 }, "Stop Time", timeRange, 1.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (timeText)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { fv::id::startTime, 1 }, "Start Time", timeRange, 0.6f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (timeText)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { fv::id::sync, 1 }, "Tempo Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { fv::id::stopDiv, 1 }, "Stop Division", divisionNames(), 3));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { fv::id::startDiv, 1 }, "Start Division", divisionNames(), 2));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { fv::id::curve, 1 }, "Curve",
                                                       NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (curveText)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { fv::id::fade, 1 }, "Fade",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.6f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { fv::id::crackle, 1 }, "Crackle",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { fv::id::output, 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    return layout;
}

bool FullertonVinylProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void FullertonVinylProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    uiSpeed.store (1.0f);
    uiState.store (0);
}

void FullertonVinylProcessor::reset()
{
    engine.reset();
}

void FullertonVinylProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto hostBpm = position->getBpm())
                if (*hostBpm > 1.0)
                    bpm = *hostBpm;

    fv::VinylEngine::Params p;
    p.stop    = pStop->load() > 0.5f;
    p.curve   = pCurve->load();
    p.fade    = pFade->load();
    p.crackle = pCrackle->load();

    if (pSync->load() > 0.5f)
    {
        const double secondsPerBeat = 60.0 / bpm;
        p.stopSeconds  = divisionBeats (juce::roundToInt (pStopDiv->load())) * secondsPerBeat;
        p.startSeconds = divisionBeats (juce::roundToInt (pStartDiv->load())) * secondsPerBeat;
    }
    else
    {
        p.stopSeconds  = pStopTime->load();
        p.startSeconds = pStartTime->load();
    }

    engine.process (buffer, p);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));

    if (outGain.isSmoothing())
    {
        const int numCh = buffer.getNumChannels();

        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            const float g = outGain.getNextValue();

            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[n] *= g;
        }
    }
    else if (! juce::exactlyEqual (outGain.getTargetValue(), 1.0f))
    {
        buffer.applyGain (outGain.getTargetValue());
    }

    uiSpeed.store ((float) engine.getSpeed(), std::memory_order_relaxed);
    uiState.store ((int) engine.getState(), std::memory_order_relaxed);
}

void FullertonVinylProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("editorWidth", editorWidth, nullptr);
    state.setProperty ("editorHeight", editorHeight, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FullertonVinylProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            editorWidth  = state.getProperty ("editorWidth", editorWidth);
            editorHeight = state.getProperty ("editorHeight", editorHeight);
            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessorEditor* FullertonVinylProcessor::createEditor()
{
    return new FullertonVinylEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FullertonVinylProcessor();
}
