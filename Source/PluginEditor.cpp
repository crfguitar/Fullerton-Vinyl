#include "PluginEditor.h"
#include "UI/Skin.h"
#include "UI/Widgets.h"

using namespace fv;

//==============================================================================
/** The whole GUI at its native 1040 x 580; the editor scales it to the window. */
class FullertonVinylEditor::Content final : public juce::Component
{
public:
    explicit Content (FullertonVinylProcessor& p) : vinyl (p)
    {
        setOpaque (true);
        setSize (geo::width, geo::height);

        auto& state = vinyl.apvts;
        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

        const auto addKnob = [this, &state] (Knob& knob, const char* paramId)
        {
            addAndMakeVisible (knob);
            sliderAttachments.add (new SliderAttachment (state, paramId, knob));
            knob.onContextMenu = [this, paramId] { showHostMenu (paramId); };
        };

        addKnob (stopTime, id::stopTime);
        addKnob (startTime, id::startTime);
        addKnob (stopDiv, id::stopDiv);
        addKnob (startDiv, id::startDiv);
        addKnob (curve, id::curve);
        addKnob (fade, id::fade);
        addKnob (crackle, id::crackle);
        addKnob (output, id::output);

        addAndMakeVisible (startStop);
        addAndMakeVisible (sync);
        addAndMakeVisible (display);
        buttonAttachments.add (new ButtonAttachment (state, id::stop, startStop));
        buttonAttachments.add (new ButtonAttachment (state, id::sync, sync));
        startStop.onContextMenu = [this] { showHostMenu (id::stop); };
        sync.onContextMenu      = [this] { showHostMenu (id::sync); };


        // Control panel layout.
        const int left = (int) geo::panelX + 20;
        display.setBounds (left, 92, 296, 58);

        for (auto* k : { &stopTime, &stopDiv })    k->setBounds (left, 160, 148, 164);
        for (auto* k : { &startTime, &startDiv })  k->setBounds (left + 148, 160, 148, 164);

        int x = left;
        for (auto* k : { &curve, &fade, &crackle, &output })
        {
            k->setBounds (x, 336, 74, 122);
            x += 74;
        }

        sync.setBounds (left, 498, 150, 28);
        startStop.setBounds (24, 504, 104, 56);

        syncOn = ! isSyncOn();   // force the first update
        updateSyncVisibility();
    }

    void paint (juce::Graphics& g) override
    {
        auto& skin = *skinPtr;
        const float inv = 1.0f / Skin::ss;
        const juce::Point<float> c (geo::cx, geo::cy);

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (skin.background, getLocalBounds().toFloat());

        // Strobe dots: lit by a lamp flashing in step with 33 1/3, so they only
        // appear to move by the amount the platter is off speed.
        g.drawImageTransformed (skin.dots,
                                juce::AffineTransform::scale (inv)
                                    .translated (c.x - geo::platterR, c.y - geo::platterR)
                                    .rotated ((float) dotsAngle, c.x, c.y));

        // Lamp glow over the rim.
        {
            const juce::Point<float> lamp (139.0f, 437.0f);
            juce::ColourGradient glow (col::red.withAlpha (0.42f), lamp.x, lamp.y,
                                       col::red.withAlpha (0.0f), lamp.x + 46.0f, lamp.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (lamp.x - 46.0f, lamp.y - 46.0f, 92.0f, 92.0f);
        }

        const juce::Rectangle<float> recordArea (c.x - geo::recordR, c.y - geo::recordR, geo::recordR * 2.0f, geo::recordR * 2.0f);

        // Record shadow on the platter, the vinyl (its reflections stay put, as
        // the light does), then the label and scuffs that turn with it.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (recordArea.expanded (1.5f).translated (0.8f, 1.6f));
        g.drawImage (skin.record, recordArea);
        g.drawImageTransformed (skin.recordSpin,
                                juce::AffineTransform::scale (inv)
                                    .translated (recordArea.getX(), recordArea.getY())
                                    .rotated ((float) recordAngle, c.x, c.y));

        // Spindle.
        {
            juce::ColourGradient metal (juce::Colour (0xffffffff), c.x - 1.6f, c.y - 2.0f,
                                        juce::Colour (0xff53565c), c.x + 4.0f, c.y + 4.5f, true);
            g.setGradientFill (metal);
            g.fillEllipse (c.x - 4.2f, c.y - 4.2f, 8.4f, 8.4f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawEllipse (c.x - 4.2f, c.y - 4.2f, 8.4f, 8.4f, 0.6f);
        }

        g.drawImage (skin.tonearm, skin.tonearmBounds);
    }

    void tick (double dt)
    {
        const float speed = juce::jlimit (0.0f, 1.0f, vinyl.uiSpeed.load (std::memory_order_relaxed));
        const int state = vinyl.uiState.load (std::memory_order_relaxed);

        const double omega = juce::MathConstants<double>::twoPi * (double) geo::rpm / 60.0;
        const double strobeSlip = (double) speed - std::round ((double) speed);

        if (speed > 0.0f)
        {
            recordAngle = std::fmod (recordAngle + omega * (double) speed * dt, juce::MathConstants<double>::twoPi);
            dotsAngle   = std::fmod (dotsAngle + omega * strobeSlip * dt, juce::MathConstants<double>::twoPi);

            const float r = geo::platterR + 2.0f;
            repaint (juce::Rectangle<float> (geo::cx - r, geo::cy - r, r * 2.0f, r * 2.0f).getSmallestIntegerContainer());
        }

        blinkTime += dt;
        display.setReadout (speed, state);
        startStop.setEngineState (state, std::fmod (blinkTime, 0.36) < 0.18);
        updateSyncVisibility();
    }

private:
    /** Opens the host's own menu for a parameter. In FL Studio this is where
        "Create automation clip" and "Link to controller" live. */
    void showHostMenu (const char* paramId)
    {
        auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
        auto* param = vinyl.apvts.getParameter (paramId);

        if (editor == nullptr || param == nullptr)
            return;

        if (auto* host = editor->getHostContext())
            if (auto menu = host->getContextMenuForParameter (param))
                menu->showNativeMenu (editor->getMouseXYRelative());
    }

    bool isSyncOn() const
    {
        return vinyl.apvts.getRawParameterValue (id::sync)->load() > 0.5f;
    }

    void updateSyncVisibility()
    {
        const bool on = isSyncOn();

        if (on == syncOn)
            return;

        syncOn = on;
        stopTime.setVisible (! on);
        startTime.setVisible (! on);
        stopDiv.setVisible (on);
        startDiv.setVisible (on);
    }

    FullertonVinylProcessor& vinyl;
    juce::SharedResourcePointer<Skin> skinPtr;

    Knob stopTime { "STOP TIME", true }, startTime { "START TIME", true };
    Knob stopDiv { "STOP TIME", true }, startDiv { "START TIME", true };
    Knob curve { "CURVE", false }, fade { "FADE", false }, crackle { "CRACKLE", false }, output { "OUTPUT", false };
    StartStopButton startStop;
    SlideSwitch sync { "TEMPO SYNC" };
    Display display;

    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;

    double recordAngle = 0.4, dotsAngle = 0.0, blinkTime = 0.0;
    bool syncOn = false;
};

//==============================================================================
FullertonVinylEditor::FullertonVinylEditor (FullertonVinylProcessor& p)
    : AudioProcessorEditor (&p), vinyl (p)
{
    // Read the saved size first: setting the resize limits triggers resized(),
    // which writes the current size back to the processor.
    const int savedW = vinyl.editorWidth, savedH = vinyl.editorHeight;

    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) geo::width / (double) geo::height);
    setResizeLimits (geo::width * 3 / 4, geo::height * 3 / 4, geo::width * 2, geo::height * 2);
    setSize (juce::jlimit (geo::width * 3 / 4, geo::width * 2, savedW),
             juce::jlimit (geo::height * 3 / 4, geo::height * 2, savedH));

    lastTickMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

FullertonVinylEditor::~FullertonVinylEditor()
{
    stopTimer();
}

void FullertonVinylEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void FullertonVinylEditor::resized()
{
    if (content == nullptr)
        return;

    const float scale = (float) getWidth() / (float) geo::width;
    content->setTransform (juce::AffineTransform::scale (scale));
    content->setBounds (0, 0, geo::width, geo::height);

    vinyl.editorWidth = getWidth();
    vinyl.editorHeight = getHeight();
}

void FullertonVinylEditor::tick (double dtSeconds)
{
    content->tick (dtSeconds);
}

void FullertonVinylEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.1, (now - lastTickMs) * 0.001);
    lastTickMs = now;
    tick (dt);
}
