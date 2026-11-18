// Dev tool for Fullerton Vinyl.
//   VinylDevTool <output-folder>
// Runs the DSP self-tests, then renders the editor to PNG files in the folder.

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;

        if (! ok)
            ++failures;
    }

    constexpr double sr = 48000.0;

    struct Rig
    {
        fv::VinylEngine engine;
        fv::VinylEngine::Params params;
        double phase = 0.0;
        float lastOut = 0.0f;
        float maxStep = 0.0f;      // largest sample-to-sample jump seen
        float peak = 0.0f;
        bool finite = true;
        bool exact = true;         // output == input for every sample of the last run

        Rig() { engine.prepare (sr, 2); }

        void run (int numSamples, int blockSize = 256)
        {
            maxStep = 0.0f;
            peak = 0.0f;
            exact = true;
            juce::AudioBuffer<float> buffer (2, blockSize);

            while (numSamples > 0)
            {
                const int n = juce::jmin (blockSize, numSamples);
                juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, n);
                juce::AudioBuffer<float> dry (2, n);

                for (int i = 0; i < n; ++i)
                {
                    const auto v = (float) (0.5 * std::sin (phase));
                    phase += juce::MathConstants<double>::twoPi * 220.0 / sr;
                    block.setSample (0, i, v);
                    block.setSample (1, i, -v);
                    dry.setSample (0, i, v);
                    dry.setSample (1, i, -v);
                }

                engine.process (block, params);

                for (int i = 0; i < n; ++i)
                {
                    const float o = block.getSample (0, i);
                    finite = finite && std::isfinite (o) && std::isfinite (block.getSample (1, i));
                    exact = exact && juce::exactlyEqual (o, dry.getSample (0, i))
                                  && juce::exactlyEqual (block.getSample (1, i), dry.getSample (1, i));
                    maxStep = juce::jmax (maxStep, std::abs (o - lastOut));
                    peak = juce::jmax (peak, std::abs (o));
                    lastOut = o;
                }

                numSamples -= n;
            }
        }
    };

    void dspTests()
    {
        using State = fv::VinylEngine::State;
        const float sineStep = (float) (0.5 * juce::MathConstants<double>::twoPi * 220.0 / sr);   // max step of the dry sine

        std::cout << "DSP" << std::endl;

        for (const float curve : { 0.0f, 1.0f, -1.0f })
        {
            std::cout << " curve " << curve << std::endl;
            Rig rig;
            rig.params.curve = curve;
            rig.params.stopSeconds = 1.0;
            rig.params.startSeconds = 0.5;
            rig.params.fade = 0.6f;

            rig.run (9600, 311);
            check (rig.exact, "running: output is bit-identical to input");

            rig.params.stop = true;
            rig.run ((int) (sr * 0.9), 97);
            check (rig.engine.getState() == State::braking, "still braking at 0.9 s of a 1.0 s stop");
            check (rig.maxStep < sineStep * 1.6f, "brake is click-free (max step " + juce::String (rig.maxStep, 5) + ")");

            rig.run ((int) (sr * 0.2), 512);
            check (rig.engine.getState() == State::stopped, "stopped after the stop time");
            check (rig.maxStep < sineStep * 1.6f, "landing at zero is click-free (max step " + juce::String (rig.maxStep, 5) + ")");

            rig.run (4800);
            check (rig.peak == 0.0f, "stopped: output is silent");

            rig.params.stop = false;
            rig.run ((int) (sr * 0.45), 64);
            check (rig.engine.getState() == State::starting, "still spinning up at 0.45 s of a 0.5 s start");
            check (rig.maxStep < sineStep * 1.6f, "spin-up is click-free (max step " + juce::String (rig.maxStep, 5) + ")");

            rig.run ((int) (sr * 0.15), 128);
            check (rig.engine.getState() == State::running, "running again after start time + relock");
            check (rig.maxStep < sineStep * 2.6f, "relock crossfade is click-free (max step " + juce::String (rig.maxStep, 5) + ")");

            rig.run (9600);
            check (rig.exact, "running again: output is bit-identical to input");
            check (rig.finite, "no NaN / Inf anywhere");
        }

        {
            std::cout << " reversals + crackle" << std::endl;
            Rig rig;
            rig.params.crackle = 1.0f;
            rig.params.stopSeconds = 0.3;
            rig.params.startSeconds = 0.3;
            juce::Random r (7);
            float worst = 0.0f;

            for (int i = 0; i < 400; ++i)
            {
                rig.params.stop = r.nextBool();
                rig.params.curve = r.nextFloat() * 2.0f - 1.0f;
                rig.run (1 + r.nextInt (9000), 1 + r.nextInt (700));
                worst = juce::jmax (worst, rig.peak);
            }

            check (rig.finite, "random start/stop toggling stays finite");
            check (worst < 2.0f, "random start/stop toggling stays bounded (peak " + juce::String (worst, 3) + ")");
        }

        {
            std::cout << " long ramps (lag guard)" << std::endl;
            Rig rig;
            rig.params.stopSeconds = 10.0;
            rig.params.startSeconds = 10.0;

            for (int i = 0; i < 12; ++i)
            {
                rig.params.stop = (i % 2) == 0;
                rig.run ((int) (sr * 4.0), 480);
            }

            check (rig.finite && rig.peak <= 0.75f, "repeated 10 s partial ramps stay inside the delay line");
        }
    }

    void processorTests()
    {
        std::cout << "Processor" << std::endl;
        FullertonVinylProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, 512);
        proc.prepareToPlay (sr, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        juce::Random r (3);
        bool finite = true;

        const auto setParam = [&proc] (const char* paramId, float normalised)
        {
            proc.apvts.getParameter (paramId)->setValueNotifyingHost (normalised);
        };

        for (int block = 0; block < 600; ++block)
        {
            if (block % 40 == 0)
            {
                setParam (fv::id::stop, r.nextBool() ? 1.0f : 0.0f);
                setParam (fv::id::sync, r.nextBool() ? 1.0f : 0.0f);
                setParam (fv::id::stopDiv, r.nextFloat());
                setParam (fv::id::startDiv, r.nextFloat());
                setParam (fv::id::stopTime, r.nextFloat() * 0.5f);
                setParam (fv::id::startTime, r.nextFloat() * 0.5f);
                setParam (fv::id::output, r.nextFloat());
                setParam (fv::id::crackle, r.nextFloat());
            }

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, r.nextFloat() - 0.5f);

            proc.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    finite = finite && std::isfinite (buffer.getSample (ch, i));
        }

        check (finite, "600 blocks of random automation stay finite");

        setParam (fv::id::curve, 0.83f);
        juce::MemoryBlock saved;
        proc.getStateInformation (saved);

        FullertonVinylProcessor other;
        other.setStateInformation (saved.getData(), (int) saved.getSize());
        check (std::abs (other.apvts.getParameter (fv::id::curve)->getValue() - 0.83f) < 0.01f, "state save / restore round-trips");
    }

    void savePng (juce::Component& comp, const juce::File& file)
    {
        const auto image = comp.createComponentSnapshot (comp.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;

        if (out.openedOk() && png.writeImageToStream (image, out))
            std::cout << "  wrote " << file.getFullPathName() << std::endl;
        else
            check (false, "could not write " + file.getFullPathName());
    }

    void snapshots (const juce::File& folder)
    {
        std::cout << "Snapshots" << std::endl;
        folder.createDirectory();

        FullertonVinylProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, 512);
        proc.prepareToPlay (sr, 512);
        proc.editorWidth = 1560;
        proc.editorHeight = 870;

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* editor = dynamic_cast<FullertonVinylEditor*> (ed.get());

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        const auto runAudio = [&] (double seconds)
        {
            for (int n = (int) (seconds * sr / 512.0); n > 0; --n)
            {
                buffer.clear();
                proc.processBlock (buffer, midi);
            }
        };

        const auto setParam = [&proc] (const char* paramId, float normalised)
        {
            proc.apvts.getParameter (paramId)->setValueNotifyingHost (normalised);
        };

        runAudio (0.1);
        editor->tick (0.6);
        savePng (*ed, folder.getChildFile ("gui-playing.png"));

        setParam (fv::id::stop, 1.0f);
        runAudio (0.55);
        editor->tick (0.3);
        savePng (*ed, folder.getChildFile ("gui-braking.png"));

        setParam (fv::id::sync, 1.0f);
        setParam (fv::id::crackle, 0.4f);
        setParam (fv::id::curve, 0.8f);
        runAudio (3.0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        editor->tick (0.3);
        savePng (*ed, folder.getChildFile ("gui-stopped-sync.png"));
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    dspTests();
    processorTests();

    if (argc > 1)
        snapshots (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1])));

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + juce::String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
