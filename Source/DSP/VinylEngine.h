#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace fv
{

/** Turntable stop / start engine.

    The input is written continuously into a delay line. While the platter is
    braking or spinning up, a read head travels through that delay line at the
    platter speed (0..1), so the audio drops in pitch exactly like a record
    slowing down. The distance between the write head and the read head ("lag")
    grows by (1 - speed) every sample.

    - Running:   output == input, bit for bit (no latency, no colouration).
    - Braking:   speed falls 1 -> 0 over the stop time.
    - Stopped:   silence.
    - Starting:  the read head jumps to "now" and speed rises 0 -> 1.
    - Relocking: once back at full speed the output is still late by the lag
                 gathered on the way up, so it is crossfaded onto the live input
                 and the plugin is transparent again.

    The read head never moves faster than the write head, so resampling only
    ever stretches the audio: no aliasing, and no anti-alias filter is needed.
*/
class VinylEngine
{
public:
    enum class State { running = 0, braking, stopped, starting, relocking };

    struct Params
    {
        bool   stop         = false;
        double stopSeconds  = 1.0;
        double startSeconds = 0.6;
        float  curve        = 0.0f;   // -1 .. +1, 0 = linear
        float  fade         = 0.6f;   //  0 .. 1, how much level follows speed
        float  crackle      = 0.0f;   //  0 .. 1
    };

    static constexpr double kMaxRampSeconds = 10.0;

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        // A brake followed by a spin-up can each add up to one ramp of lag.
        maxLag = (int) std::ceil (sr * (2.0 * kMaxRampSeconds + 1.0));
        size = juce::nextPowerOfTwo (maxLag + 16);
        mask = size - 1;

        delay.setSize (juce::jlimit (1, 2, numChannels), size, false, true, false);
        xfadeLen = juce::jmax (8, (int) (sr * 0.04));

        clickHp   = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 350.0f / (float) sr);
        hissLp    = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5500.0f / (float) sr);
        reset();
    }

    void reset()
    {
        delay.clear();
        writePos = 0;
        x = 1.0;
        speed = 1.0;
        lag = 0.0;
        xfadePos = 0;
        state = State::running;
        noisePhase = 0.0;

        for (int c = 0; c < 2; ++c)
            clickEnv[c] = clickLow[c] = hiss[c] = noisePrev[c] = noiseCur[c] = 0.0f;
    }

    State getState() const noexcept   { return state; }
    double getSpeed() const noexcept  { return speed; }

    void process (juce::AudioBuffer<float>& io, const Params& p) noexcept
    {
        const int numCh = juce::jmin (io.getNumChannels(), delay.getNumChannels());
        const int numSamples = io.getNumSamples();

        if (numCh <= 0 || size == 0)
            return;

        const double k         = std::pow (2.0, 2.0 * (double) juce::jlimit (-1.0f, 1.0f, p.curve));
        const double invK      = 1.0 / k;
        const double stopInc   = 1.0 / (juce::jlimit (0.01, kMaxRampSeconds, p.stopSeconds) * sr);
        const double startInc  = 1.0 / (juce::jlimit (0.01, kMaxRampSeconds, p.startSeconds) * sr);
        const float  fade      = juce::jlimit (0.0f, 1.0f, p.fade);
        const float  crackle   = juce::jlimit (0.0f, 1.0f, p.crackle);
        const bool   crackleOn = crackle > 1.0e-4f;
        const float  clickProb = (4.0f + 40.0f * crackle) / (float) sr;

        float* out[2] = { io.getWritePointer (0), io.getWritePointer (numCh > 1 ? 1 : 0) };
        float* dl[2]  = { delay.getWritePointer (0), delay.getWritePointer (numCh > 1 ? 1 : 0) };

        for (int n = 0; n < numSamples; ++n)
        {
            writePos = (writePos + 1) & mask;

            float in[2] = { 0.0f, 0.0f };

            for (int c = 0; c < numCh; ++c)
            {
                in[c] = out[c][n];
                dl[c][writePos] = in[c];
            }

            // ---- react to the start/stop control -------------------------
            switch (state)
            {
                case State::running:
                    if (p.stop) { state = State::braking; x = 1.0; lag = 0.0; }
                    break;

                case State::braking:    // reversed mid-brake: keep the speed continuous
                    if (! p.stop) { state = State::starting; x = 1.0 - std::pow (1.0 - speed, invK); }
                    break;

                case State::stopped:    // nothing is audible, so pick the needle up at "now"
                    if (! p.stop) { state = State::starting; x = 0.0; lag = 0.0; }
                    break;

                case State::starting:
                    if (p.stop) { state = State::braking; x = std::pow (speed, invK); }
                    break;

                case State::relocking:  // 40 ms; let it finish, a stop is picked up right after
                    break;
            }

            // ---- move the platter ----------------------------------------
            switch (state)
            {
                case State::running:
                    speed = 1.0;
                    break;

                case State::braking:
                    x -= stopInc;
                    if (x <= 0.0) { x = 0.0; speed = 0.0; state = State::stopped; }
                    else          { speed = std::pow (x, k); }
                    break;

                case State::stopped:
                    speed = 0.0;
                    break;

                case State::starting:
                    x += startInc;
                    if (x >= 1.0)
                    {
                        x = 1.0;
                        speed = 1.0;
                        state = State::relocking;
                        xfadePos = 0;
                    }
                    else
                    {
                        speed = 1.0 - std::pow (1.0 - x, k);
                    }
                    break;

                case State::relocking:
                    speed = 1.0;
                    if (++xfadePos >= xfadeLen) { state = State::running; lag = 0.0; }
                    break;
            }

            // ---- audio ----------------------------------------------------
            const float s = (float) speed;
            float gain = 1.0f;

            if (state == State::braking || state == State::starting)
            {
                lag = juce::jmin (lag + (1.0 - speed), (double) maxLag);

                // A cartridge's output follows groove velocity, so level drops with
                // speed ("fade"). The last few percent are always faded out so the
                // output can never park on a DC offset.
                const float t = juce::jlimit (0.0f, 1.0f, s * 50.0f);
                gain = (1.0f - fade + fade * s) * (t * t * (3.0f - 2.0f * t));

                for (int c = 0; c < numCh; ++c)
                    out[c][n] = readTap (dl[c]) * gain;
            }
            else if (state == State::relocking)
            {
                const float t = (float) xfadePos / (float) xfadeLen;
                const float a = std::cos (t * juce::MathConstants<float>::halfPi);
                const float b = std::sin (t * juce::MathConstants<float>::halfPi);

                for (int c = 0; c < numCh; ++c)
                    out[c][n] = readTap (dl[c]) * a + in[c] * b;
            }
            else if (state == State::stopped)
            {
                gain = 0.0f;

                for (int c = 0; c < numCh; ++c)
                    out[c][n] = 0.0f;
            }
            // running: the input is already in the buffer, untouched.

            // ---- surface noise, generated in "record time" ----------------
            if (crackleOn && s > 0.0f)
            {
                noisePhase += speed;

                if (noisePhase >= 1.0)
                {
                    noisePhase -= 1.0;
                    nextNoiseSample (clickProb);
                }

                const float f = (float) noisePhase;
                const float level = crackle * gain;

                for (int c = 0; c < numCh; ++c)
                    out[c][n] += (noisePrev[c] + (noiseCur[c] - noisePrev[c]) * f) * level;
            }
        }
    }

private:
    /** Reads the delay line `lag` samples behind the write head. */
    float readTap (const float* d) const noexcept
    {
        const auto   whole = (int) lag;
        const float  t     = 1.0f - (float) (lag - (double) whole);   // 0 = older sample, 1 = newer
        const int    i1    = (writePos - whole) & mask;               // newer
        const int    i0    = (i1 - 1) & mask;                         // older
        const float  y0    = d[i0];
        const float  y1    = d[i1];
        const float  linear = y0 + (y1 - y0) * t;

        // Cubic needs one sample newer than i1, which only exists once the read
        // head is at least a sample behind. Blend it in over the next sample.
        if (lag < 1.0)
            return linear;

        const float ym = d[(i0 - 1) & mask];
        const float y2 = d[(i1 + 1) & mask];
        const float c1 = 0.5f * (y1 - ym);
        const float c2 = ym - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym) + 1.5f * (y0 - y1);
        const float cubic = ((c3 * t + c2) * t + c1) * t + y0;

        const float w = juce::jmin (1.0f, (float) (lag - 1.0));
        return linear + (cubic - linear) * w;
    }

    void nextNoiseSample (float clickProb) noexcept
    {
        if (rng.nextFloat() < clickProb)
        {
            float a = rng.nextFloat();
            a = 0.12f + 0.88f * a * a * a;

            if (rng.nextBool())
                a = -a;

            const float pan = rng.nextFloat();
            clickEnv[0] += a * std::sqrt (1.0f - pan);
            clickEnv[1] += a * std::sqrt (pan);

            const float tau = 0.00003f + 0.00035f * rng.nextFloat();   // seconds
            clickDecay = std::exp (-1.0f / (tau * (float) sr));
        }

        for (int c = 0; c < 2; ++c)
        {
            noisePrev[c] = noiseCur[c];

            clickEnv[c] *= clickDecay;
            clickLow[c] += clickHp * (clickEnv[c] - clickLow[c]);
            hiss[c]     += hissLp * ((rng.nextFloat() * 2.0f - 1.0f) - hiss[c]);

            noiseCur[c] = (clickEnv[c] - clickLow[c]) * 0.4f + hiss[c] * 0.02f;
        }
    }

    juce::AudioBuffer<float> delay;
    double sr = 44100.0;
    int size = 0, mask = 0, maxLag = 0, writePos = 0;
    int xfadeLen = 1, xfadePos = 0;

    State  state = State::running;
    double x = 1.0;         // ramp position, 0 = stopped, 1 = full speed
    double speed = 1.0;     // platter speed derived from x through the curve
    double lag = 0.0;       // samples between write head and read head

    juce::Random rng { 0x33453378 };
    double noisePhase = 0.0;
    float clickEnv[2] {}, clickLow[2] {}, hiss[2] {}, noisePrev[2] {}, noiseCur[2] {};
    float clickDecay = 0.9f, clickHp = 0.05f, hissLp = 0.5f;
};

} // namespace fv
