#include "Skin.h"

namespace fv
{

juce::Font uiFont (float height, bool bold, float kerning)
{
    return juce::Font (juce::FontOptions ("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain))
               .withExtraKerningFactor (kerning);
}

juce::Font monoFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions ("Consolas", height, bold ? juce::Font::bold : juce::Font::plain));
}

namespace
{
    using juce::Colour;
    using juce::Graphics;
    using juce::Image;
    using Pt = juce::Point<float>;

    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // The key light sits up and to the left; on spun metal and vinyl that shows up
    // as a "bow-tie" pair of highlights along this axis.
    constexpr float lobeAngle  = -55.0f * pi / 180.0f;
    constexpr float lobeAngle2 =  22.0f * pi / 180.0f;

    //==========================================================================
    float hash1 (uint32_t n) noexcept
    {
        n = (n << 13u) ^ n;
        n = n * (n * n * 15731u + 789221u) + 1376312589u;
        return (float) (n & 0x7fffffffu) / 2147483647.0f;
    }

    float vnoise (float x, uint32_t seed) noexcept
    {
        const float fl = std::floor (x);
        const auto i = (uint32_t) (int32_t) fl;
        float t = x - fl;
        t = t * t * (3.0f - 2.0f * t);
        const float a = hash1 (i + seed * 7919u);
        const float b = hash1 (i + 1u + seed * 7919u);
        return a + (b - a) * t;
    }

    float sstep (float e0, float e1, float v) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (v - e0) / (e1 - e0));
        return t * t * (3.0f - 2.0f * t);
    }

    float mixf (float a, float b, float t) noexcept   { return a + (b - a) * t; }

    struct Shade { float r = 0, g = 0, b = 0, a = 1; };

    Shade grey (float v, float tr = 1.0f, float tg = 1.0f, float tb = 1.0f, float a = 1.0f) noexcept
    {
        return { v * tr, v * tg, v * tb, a };
    }

    void put (Image::BitmapData& bd, int x, int y, const Shade& s) noexcept
    {
        const float a = juce::jlimit (0.0f, 1.0f, s.a);
        const auto to8 = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (v * 255.0f)); };
        ((juce::PixelARGB*) bd.getPixelPointer (x, y))->setARGB (to8 (a), to8 (s.r * a), to8 (s.g * a), to8 (s.b * a));
    }

    Image newImage (int w, int h)
    {
        return Image (Image::ARGB, w, h, true, juce::SoftwareImageType());
    }

    /** Renders a disc pixel by pixel. fn (radiusInPixels, angle) -> Shade. */
    template <typename Fn>
    Image renderDisc (int sizePx, Fn&& fn)
    {
        auto img = newImage (sizePx, sizePx);
        Image::BitmapData bd (img, Image::BitmapData::writeOnly);
        const float c = (float) sizePx * 0.5f;

        for (int y = 0; y < sizePx; ++y)
        {
            for (int x = 0; x < sizePx; ++x)
            {
                const float dx = (float) x + 0.5f - c;
                const float dy = (float) y + 0.5f - c;
                const float r = std::sqrt (dx * dx + dy * dy);

                if (r >= c)
                    continue;

                Shade s = fn (r, std::atan2 (dy, dx));
                s.a *= juce::jlimit (0.0f, 1.0f, c - r);   // anti-aliased rim
                put (bd, x, y, s);
            }
        }

        return img;
    }

    /** Lathe-turned aluminium: bow-tie highlights plus fine concentric machining. */
    float spunShade (float theta, float rPx, uint32_t seed) noexcept
    {
        const float a = theta - lobeAngle;
        const float c = std::abs (std::cos (a));
        const float s = std::abs (std::sin (a));
        const float lobe  = std::pow (c, 5.0f);
        const float lobe2 = std::pow (s, 10.0f);
        const float ring  = vnoise (rPx * 1.7f, seed) - 0.5f;
        const float ring2 = vnoise (rPx * 0.21f, seed + 1u) - 0.5f;

        return 0.40f + 0.50f * lobe + 0.16f * lobe2 + ring * 0.08f * (0.5f + lobe) + ring2 * 0.05f;
    }

    //==========================================================================
    Image renderKnob (int sizePx, int flutes)
    {
        const float R = (float) sizePx * 0.5f;

        return renderDisc (sizePx, [R, flutes] (float r, float th)
        {
            const float rn = r / R;
            const float px = 1.5f / R;   // blend width between zones

            // Fluted black skirt, lit from above.
            const float flute = 0.5f + 0.5f * std::cos (th * (float) flutes);
            const float top   = juce::jmax (0.0f, std::cos (th + pi * 0.58f));
            const float low   = juce::jmax (0.0f, std::cos (th - pi * 0.40f));
            float skirt = 0.060f + 0.30f * top * top * (0.35f + 0.65f * flute) + 0.07f * low * flute + 0.035f * flute;
            skirt *= 1.0f - 0.55f * sstep (0.90f, 1.0f, rn);
            skirt += 0.05f * sstep (0.80f, 0.84f, rn) * (1.0f - sstep (0.84f, 0.88f, rn)) * top;

            // Shadow gap, bright chamfer, spun cap.
            const float gap     = 0.02f;
            const float chamfer = juce::jlimit (0.22f, 1.0f, 0.60f + 0.42f * std::cos (th + pi * 0.70f));
            const float cap     = juce::jmin (1.0f, spunShade (th, r, 3u));

            float v = cap;
            v = mixf (v, chamfer, sstep (0.675f - px, 0.675f + px, rn));
            v = mixf (v, gap,     sstep (0.735f - px, 0.735f + px, rn));
            v = mixf (v, skirt,   sstep (0.785f - px, 0.785f + px, rn));

            const bool metal = rn < 0.735f;
            return metal ? grey (v, 0.985f, 0.975f, 0.955f) : grey (v, 0.98f, 0.99f, 1.04f);
        });
    }

    Image renderPlatter()
    {
        const int size = juce::roundToInt (geo::platterR * 2.0f * Skin::ss);
        const float R = (float) size * 0.5f;
        const float recordEdge = geo::recordR / geo::platterR;

        return renderDisc (size, [R, recordEdge] (float r, float th)
        {
            const float rn = r / R;
            const float c = std::abs (std::cos (th - lobeAngle));

            float flat = juce::jmin (1.0f, spunShade (th, r, 17u));
            flat *= 0.35f + 0.65f * sstep (recordEdge - 0.004f, recordEdge + 0.022f, rn);   // record's contact shadow

            const float band = 0.055f + 0.11f * std::pow (c, 4.0f);     // recessed strobe band
            const float edge = 0.50f + 0.45f * std::pow (c, 3.0f);      // outer chamfer

            float v = flat;
            v = mixf (v, band, sstep (0.926f, 0.932f, rn));
            v = mixf (v, edge, sstep (0.984f, 0.989f, rn));

            return grey (v, 0.99f, 0.98f, 0.96f);
        });
    }

    Image renderDots()
    {
        const int size = juce::roundToInt (geo::platterR * 2.0f * Skin::ss);
        const float R = (float) size * 0.5f;

        struct Row { float radius; int count; };
        static constexpr Row rows[] = { { 0.9445f, 180 }, { 0.9715f, 176 } };

        return renderDisc (size, [R] (float r, float th)
        {
            float alpha = 0.0f;

            for (const auto& row : rows)
            {
                const float rowR = row.radius * R;
                const float dr = std::abs (r - rowR);

                if (dr > 6.0f)
                    continue;

                const float spacing = twoPi * r / (float) row.count;
                const float u = (th + pi) / twoPi * (float) row.count;
                const float dt = std::abs (u - std::floor (u) - 0.5f) * spacing;
                const float d = juce::jmax (dt - spacing * 0.27f, dr - 3.6f);
                alpha = juce::jmax (alpha, juce::jlimit (0.0f, 1.0f, 0.5f - d));
            }

            return grey (0.86f, 1.0f, 0.99f, 0.96f, alpha);
        });
    }

    Image renderRecord()
    {
        const int size = juce::roundToInt (geo::recordR * 2.0f * Skin::ss);
        const float R = (float) size * 0.5f;

        return renderDisc (size, [R] (float r, float th)
        {
            const float rn = r / R;
            const float c  = std::abs (std::cos (th - lobeAngle));
            const float c2 = std::abs (std::cos (th - lobeAngle2));

            // Where the grooves are: between the dead wax and the lead-in, minus
            // the smooth gaps between tracks.
            float grooves = sstep (0.352f, 0.358f, rn) * (1.0f - sstep (0.960f, 0.966f, rn));

            for (const float gapR : { 0.487f, 0.612f, 0.705f, 0.838f })
                grooves *= sstep (0.0022f, 0.0050f, std::abs (rn - gapR));

            const float micro = vnoise (r * 1.9f, 11u);
            const float mid   = vnoise (r * 0.47f, 5u);
            const float macro = vnoise (r * 0.09f, 23u);
            const float groove = (0.30f + 0.70f * micro) * (0.70f + 0.30f * mid) * (0.40f + 0.60f * macro);

            const float tight = std::pow (c, 26.0f);
            const float wide  = std::pow (c, 5.0f);
            const float side  = std::pow (c2, 34.0f);

            const float cut    = groove * (tight * 1.05f + wide * 0.11f + side * 0.26f) + 0.012f * micro;
            const float smooth = wide * 0.09f + std::pow (c, 70.0f) * 0.30f + std::pow (c2, 90.0f) * 0.10f + 0.006f;

            float v = 0.020f + mixf (smooth, cut, grooves);
            v += 0.07f * sstep (0.986f, 0.998f, rn);          // rounded outer edge
            v = juce::jmin (1.0f, v);

            return grey (v, 0.93f, 0.96f, 1.0f);
        });
    }

    //==========================================================================
    void drawArcText (Graphics& g, const juce::String& text, Pt centre, float radius,
                      float startAngle, float stepAngle, float boxHeight)
    {
        for (int i = 0; i < text.length(); ++i)
        {
            Graphics::ScopedSaveState save (g);
            g.addTransform (juce::AffineTransform::rotation (startAngle + stepAngle * (float) i, centre.x, centre.y));
            g.drawText (juce::String::charToString (text[i]),
                        juce::Rectangle<float> (centre.x - 10.0f, centre.y - radius - boxHeight * 0.5f, 20.0f, boxHeight),
                        juce::Justification::centred, false);
        }
    }

    Image renderRecordSpin()
    {
        const int size = juce::roundToInt (geo::recordR * 2.0f * Skin::ss);
        auto img = newImage (size, size);
        Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (Skin::ss));

        const float c = geo::recordR;
        const float L = geo::labelR;
        const Pt centre (c, c);

        // Hairline scuffs and dust: they only read when the record turns.
        juce::Random rng (0x1977);

        for (int i = 0; i < 22; ++i)
        {
            const float r  = juce::jmap (rng.nextFloat(), L + 8.0f, c - 6.0f);
            const float a0 = rng.nextFloat() * twoPi;
            const float len = 0.05f + rng.nextFloat() * 0.45f;
            juce::Path arc;
            arc.addCentredArc (c, c, r, r, 0.0f, a0, a0 + len, true);
            g.setColour (juce::Colours::white.withAlpha (0.035f + 0.06f * rng.nextFloat()));
            g.strokePath (arc, juce::PathStrokeType (0.5f + rng.nextFloat() * 0.5f));
        }

        for (int i = 0; i < 70; ++i)
        {
            const float r = juce::jmap (rng.nextFloat(), L + 4.0f, c - 3.0f);
            const float a = rng.nextFloat() * twoPi;
            const float d = 0.5f + rng.nextFloat() * 0.8f;
            g.setColour (juce::Colours::white.withAlpha (0.05f + 0.12f * rng.nextFloat()));
            g.fillEllipse (c + std::cos (a) * r - d * 0.5f, c + std::sin (a) * r - d * 0.5f, d, d);
        }

        // Paper label.
        {
            juce::ColourGradient paper (Colour (0xfff1e9d4), c - L * 0.3f, c - L * 0.4f,
                                        Colour (0xffd9cdb0), c + L, c + L, true);
            g.setGradientFill (paper);
            g.fillEllipse (c - L, c - L, L * 2.0f, L * 2.0f);

            // Coloured band carrying the rim text.
            juce::Path band;
            band.addEllipse (c - 63.0f, c - 63.0f, 126.0f, 126.0f);
            band.addEllipse (c - 49.0f, c - 49.0f, 98.0f, 98.0f);
            band.setUsingNonZeroWinding (false);
            g.setColour (Colour (0xffb5451f));
            g.fillPath (band);

            g.setColour (Colour (0xff1a1a1a).withAlpha (0.55f));
            g.drawEllipse (c - 63.0f, c - 63.0f, 126.0f, 126.0f, 0.5f);
            g.drawEllipse (c - 49.0f, c - 49.0f, 98.0f, 98.0f, 0.5f);

            const juce::String rim ("FULLERTON AUDIO  *  SPIN-DOWN  *  SPEED-UP  *  HIGH FIDELITY  *  ");
            g.setColour (Colour (0xfff4ecd8));
            g.setFont (uiFont (7.2f, true));
            drawArcText (g, rim, centre, 56.0f, 0.0f, twoPi / (float) rim.length(), 9.0f);

            g.setColour (Colour (0xff1c1a17));
            g.setFont (uiFont (14.0f, true, 0.22f));
            g.drawText ("VINYL", juce::Rectangle<float> (c - 40.0f, c - 36.0f, 80.0f, 16.0f), juce::Justification::centred, false);

            g.setFont (uiFont (5.6f, true, 0.20f));
            g.drawText ("FULLERTON", juce::Rectangle<float> (c - 40.0f, c - 19.5f, 80.0f, 7.0f), juce::Justification::centred, false);

            g.setFont (uiFont (7.0f, true, 0.15f));
            g.drawText ("SIDE A", juce::Rectangle<float> (c - 40.0f, c + 13.0f, 80.0f, 9.0f), juce::Justification::centred, false);

            g.setFont (uiFont (5.4f, true, 0.12f));
            g.drawText ("33 1/3 RPM   STEREO", juce::Rectangle<float> (c - 40.0f, c + 23.5f, 80.0f, 7.0f), juce::Justification::centred, false);
            g.drawText ("FV-33", juce::Rectangle<float> (c - 40.0f, c + 31.5f, 80.0f, 7.0f), juce::Justification::centred, false);

            g.setColour (Colour (0xff1c1a17).withAlpha (0.7f));
            g.fillRect (c - 28.0f, c - 10.2f, 56.0f, 0.6f);
            g.fillRect (c - 28.0f, c + 10.0f, 56.0f, 0.6f);

            // Soft paper shading and the pressed ring around the hole.
            juce::ColourGradient shade (juce::Colours::transparentBlack, c, c,
                                        juce::Colours::black.withAlpha (0.16f), c + L, c, true);
            shade.addColour (0.75, juce::Colours::transparentBlack);
            g.setGradientFill (shade);
            g.fillEllipse (c - L, c - L, L * 2.0f, L * 2.0f);

            g.setColour (juce::Colours::black.withAlpha (0.14f));
            g.drawEllipse (c - 16.0f, c - 16.0f, 32.0f, 32.0f, 0.6f);
            g.setColour (Colour (0xff050505));
            g.fillEllipse (c - 4.6f, c - 4.6f, 9.2f, 9.2f);
        }

        return img;
    }

    //==========================================================================
    /** A tube seen from the side: dark edges, bright stripe along the length. */
    void fillTube (Graphics& g, Pt a, Pt b, float width, Colour dark, Colour light)
    {
        const auto d = b - a;
        const float len = juce::jmax (0.001f, d.getDistanceFromOrigin());
        const Pt n (-d.y / len, d.x / len);

        juce::Path p;
        p.addLineSegment ({ a, b }, width);

        juce::ColourGradient grad (dark, a + n * (width * 0.5f), dark.darker (0.5f), a - n * (width * 0.5f), false);
        grad.addColour (0.30, light);
        grad.addColour (0.48, light.interpolatedWith (dark, 0.35f));
        grad.addColour (0.80, dark);
        g.setGradientFill (grad);
        g.fillPath (p);
    }

    void drawScrew (Graphics& g, float x, float y, float r, float slotAngle)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (x - r - 1.0f, y - r - 0.6f, r * 2.0f + 2.0f, r * 2.0f + 2.4f);

        juce::ColourGradient gr (Colour (0xffc3c6ca), x - r * 0.5f, y - r * 0.6f, Colour (0xff3a3c40), x + r, y + r, true);
        g.setGradientFill (gr);
        g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);

        juce::Path slot;
        slot.addRectangle (-r * 0.82f, -r * 0.15f, r * 1.64f, r * 0.30f);
        g.setColour (Colour (0xff111113));
        g.fillPath (slot, juce::AffineTransform::rotation (slotAngle).translated (x, y));

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 0.6f);
    }

    Image renderTonearm (juce::Rectangle<float> bounds)
    {
        auto img = newImage (juce::roundToInt (bounds.getWidth() * Skin::ss), juce::roundToInt (bounds.getHeight() * Skin::ss));
        Graphics g (img);
        g.addTransform (juce::AffineTransform::translation (-bounds.getX(), -bounds.getY()).scaled (Skin::ss));

        const Pt pivot (598.0f, 112.0f);
        const Pt stylus (438.0f, 348.0f);
        const float armLen = pivot.getDistanceFrom (stylus);
        const Pt d ((stylus.x - pivot.x) / armLen, (stylus.y - pivot.y) / armLen);   // along the arm

        // The headshell is angled in towards the spindle (offset angle).
        const float off = 22.0f * pi / 180.0f;
        const Pt h (d.x * std::cos (off) - d.y * std::sin (off), d.x * std::sin (off) + d.y * std::cos (off));
        const Pt outward (h.y, -h.x);                 // away from the record centre
        const Pt elbow = stylus - h * 30.0f;

        const Colour aluDark (0xff5d6066), aluLight (0xfff4f5f6);
        const Colour blackDark (0xff0e0e10), blackLight (0xff4a4c52);

        const Pt cwA = pivot - d * 40.0f, cwB = pivot - d * 84.0f;
        const Pt shellA = elbow - h * 5.0f, shellB = stylus + h * 17.0f;

        // --- shadows: the base sits on the deck, the arm floats above it -----
        {
            juce::Path low;
            low.addEllipse (pivot.x - 36.0f, pivot.y - 36.0f, 72.0f, 72.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.60f), 10, { 4, 6 }).drawForPath (g, low);

            juce::Path high;
            high.addLineSegment ({ pivot, elbow }, 8.0f);
            high.addLineSegment ({ cwA, cwB }, 34.0f);
            high.addLineSegment ({ shellA, shellB }, 21.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 12, { 9, 13 }).drawForPath (g, high);
        }

        // --- base ------------------------------------------------------------
        {
            juce::ColourGradient base (Colour (0xff70737a), pivot.x - 18.0f, pivot.y - 22.0f,
                                       Colour (0xff1d1e21), pivot.x + 34.0f, pivot.y + 38.0f, true);
            g.setGradientFill (base);
            g.fillEllipse (pivot.x - 36.0f, pivot.y - 36.0f, 72.0f, 72.0f);
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.drawEllipse (pivot.x - 35.3f, pivot.y - 35.3f, 70.6f, 70.6f, 0.9f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawEllipse (pivot.x - 28.0f, pivot.y - 28.0f, 56.0f, 56.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawEllipse (pivot.x - 27.0f, pivot.y - 27.0f, 54.0f, 54.0f, 0.7f);

        }

        // --- counterweight and rear stub --------------------------------------
        fillTube (g, pivot, cwA + d * 2.0f, 9.0f, aluDark, aluLight);
        fillTube (g, cwA, cwB, 34.0f, Colour (0xff2c2e32), Colour (0xffb9bcc2));
        fillTube (g, cwA, cwA - d * 5.0f, 34.0f, aluDark, aluLight);         // graduated collar
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawLine ({ cwB - Pt (-d.y, d.x) * 17.0f, cwB + Pt (-d.y, d.x) * 17.0f }, 1.0f);

        // --- arm tube ---------------------------------------------------------
        fillTube (g, pivot, elbow, 7.6f, aluDark, aluLight);
        fillTube (g, elbow - d * 9.0f, elbow + d * 1.0f, 9.5f, aluDark, aluLight);   // headshell collar

        // --- pivot housing ----------------------------------------------------
        {
            juce::ColourGradient cap (Colour (0xfff2f3f4), pivot.x - 6.0f, pivot.y - 8.0f,
                                      Colour (0xff62656b), pivot.x + 12.0f, pivot.y + 14.0f, true);
            g.setGradientFill (cap);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (pivot.x - 17.0f, pivot.y - 15.5f, 37.0f, 37.0f);
            g.setGradientFill (cap);
            g.fillEllipse (pivot.x - 17.0f, pivot.y - 17.0f, 34.0f, 34.0f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawEllipse (pivot.x - 17.0f, pivot.y - 17.0f, 34.0f, 34.0f, 0.7f);
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.drawEllipse (pivot.x - 11.0f, pivot.y - 11.0f, 22.0f, 22.0f, 0.6f);
            g.setColour (Colour (0xff1b1c1f));
            g.fillEllipse (pivot.x - 3.6f, pivot.y - 3.6f, 7.2f, 7.2f);
        }

        // --- headshell, finger lift, cartridge --------------------------------
        {
            juce::Path lift;
            lift.startNewSubPath (stylus + h * 9.0f + outward * 8.0f);
            lift.lineTo (stylus + h * 11.0f + outward * 20.0f);
            lift.lineTo (stylus + h * 15.0f + outward * 27.0f);
            g.setColour (Colour (0xff3b3d42));
            g.strokePath (lift, juce::PathStrokeType (3.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Colour (0xffe6e7e9));
            g.strokePath (lift, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            fillTube (g, shellA, shellB, 21.0f, blackDark, blackLight);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            juce::Path shellEdge;
            shellEdge.addLineSegment ({ shellA, shellB }, 21.0f);
            g.strokePath (shellEdge, juce::PathStrokeType (0.6f));

            fillTube (g, stylus - h * 9.0f, stylus + h * 13.0f, 14.0f, Colour (0xff5e130e), Colour (0xffd9412f));

            for (const float along : { -3.0f, 8.0f })
            {
                const Pt s = stylus + h * along;
                g.setColour (Colour (0xff0d0d0e));
                g.fillEllipse (s.x - 2.0f, s.y - 2.0f, 4.0f, 4.0f);
                g.setColour (Colour (0xffcdd0d4));
                g.fillEllipse (s.x - 1.3f, s.y - 1.3f, 2.6f, 2.6f);
            }
        }

        return img;
    }

    //==========================================================================
    Image renderBackground (const Image& platter)
    {
        const int w = juce::roundToInt ((float) geo::width * Skin::ss);
        const int h = juce::roundToInt ((float) geo::height * Skin::ss);
        auto img = newImage (w, h);

        // Brushed graphite plate: every row is its own streak of low-passed noise.
        {
            Image::BitmapData bd (img, Image::BitmapData::writeOnly);

            for (int y = 0; y < h; ++y)
            {
                const float fy = (float) y / (float) h;
                const float row = hash1 ((uint32_t) y * 31u + 7u) - 0.5f;
                float streak = 0.5f;

                for (int x = 0; x < w; ++x)
                {
                    const float fx = (float) x / (float) w;
                    const auto seed = (uint32_t) x * 7349u + (uint32_t) y * 9151u;
                    streak += 0.07f * (hash1 (seed) - streak);
                    const float grain = hash1 (seed + 99u) - 0.5f;

                    float v = 0.215f - 0.075f * fy;
                    v += (streak - 0.5f) * 0.42f + row * 0.016f + grain * 0.014f;
                    v += 0.035f * std::exp (-juce::square ((fx + fy * 0.35f - 0.42f) * 3.2f));   // soft sheen
                    v *= 1.0f - 0.55f * (juce::square (fx - 0.5f) + juce::square (fy - 0.5f));  // vignette

                    put (bd, x, y, grey (v, 0.97f, 1.0f, 1.05f));
                }
            }
        }

        Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (Skin::ss));

        const float cx = geo::cx, cy = geo::cy, R = geo::platterR;

        // Edge bevel of the whole plate.
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (0.0f, 0.0f, (float) geo::width, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (0.0f, (float) geo::height - 1.5f, (float) geo::width, 1.5f);

        // Platter well, then the platter itself.
        {
            const float outer = R + 18.0f;
            juce::ColourGradient well (juce::Colours::black.withAlpha (0.9f), cx + 2.0f, cy + 5.0f,
                                       juce::Colours::transparentBlack, cx + 2.0f + outer, cy + 5.0f, true);
            well.addColour ((R - 2.0f) / outer, juce::Colours::black.withAlpha (0.9f));
            g.setGradientFill (well);
            g.fillEllipse (cx + 2.0f - outer, cy + 5.0f - outer, outer * 2.0f, outer * 2.0f);

            g.drawImage (platter, juce::Rectangle<float> (cx - R, cy - R, R * 2.0f, R * 2.0f));
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawEllipse (cx - R, cy - R, R * 2.0f, R * 2.0f, 0.8f);
        }

        // Strobe lamp housing, bottom left of the platter.
        {
            const Pt lamp (124.0f, 452.0f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (lamp.x - 14.0f, lamp.y - 11.0f, 31.0f, 31.0f);
            juce::ColourGradient body (Colour (0xff4a4c51), lamp.x - 6.0f, lamp.y - 8.0f,
                                       Colour (0xff0f0f11), lamp.x + 12.0f, lamp.y + 14.0f, true);
            g.setGradientFill (body);
            g.fillEllipse (lamp.x - 14.0f, lamp.y - 14.0f, 28.0f, 28.0f);
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.drawEllipse (lamp.x - 13.4f, lamp.y - 13.4f, 26.8f, 26.8f, 0.7f);

            juce::Path lens;
            lens.addRoundedRectangle (-6.0f, -2.6f, 12.0f, 5.2f, 1.6f);
            const auto tf = juce::AffineTransform::rotation (pi * 0.25f).translated (lamp.x + 5.5f, lamp.y - 5.5f);
            g.setColour (Colour (0xff200403));
            g.fillPath (lens, tf);
            g.setColour (col::red.withAlpha (0.95f));
            g.fillPath (lens, juce::AffineTransform::scale (0.8f, 0.6f).followedBy (tf));
        }

        // Control panel: an inset plate of black anodised metal.
        {
            const juce::Rectangle<float> panel (geo::panelX, geo::panelY, geo::panelW, geo::panelH);

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (panel.expanded (2.0f), 8.0f);
            g.setColour (juce::Colours::white.withAlpha (0.09f));
            g.drawRoundedRectangle (panel.expanded (2.0f).translated (0.0f, 0.8f), 8.0f, 1.0f);

            juce::ColourGradient face (Colour (0xff1a1b1e), panel.getX(), panel.getY(),
                                       Colour (0xff0c0c0e), panel.getX(), panel.getBottom(), false);
            g.setGradientFill (face);
            g.fillRoundedRectangle (panel, 6.0f);

            juce::ColourGradient lip (juce::Colours::black.withAlpha (0.55f), panel.getX(), panel.getY(),
                                      juce::Colours::transparentBlack, panel.getX(), panel.getY() + 12.0f, false);
            g.setGradientFill (lip);
            g.fillRoundedRectangle (panel.withHeight (14.0f), 6.0f);

            const float left = panel.getX() + 20.0f, right = panel.getRight() - 20.0f;

            g.setColour (col::silk.withAlpha (0.72f));
            g.setFont (uiFont (10.5f, true, 0.46f));
            g.drawText ("FULLERTON", juce::Rectangle<float> (left, 36.0f, 200.0f, 12.0f), juce::Justification::centredLeft, false);

            g.setColour (col::silk);
            g.setFont (uiFont (27.0f, true, 0.30f));
            g.drawText ("VINYL", juce::Rectangle<float> (left - 1.0f, 47.0f, 200.0f, 30.0f), juce::Justification::centredLeft, false);

            g.setColour (col::silk.withAlpha (0.50f));
            g.setFont (uiFont (8.2f, true, 0.22f));
            g.drawText ("SPIN-DOWN / SPEED-UP", juce::Rectangle<float> (right - 160.0f, 52.0f, 160.0f, 10.0f), juce::Justification::centredRight, false);
            g.drawText ("TURNTABLE PROCESSOR", juce::Rectangle<float> (right - 160.0f, 64.0f, 160.0f, 10.0f), juce::Justification::centredRight, false);

            g.setColour (col::amber.withAlpha (0.9f));
            g.fillRect (right - 22.0f, 40.0f, 22.0f, 2.0f);

            for (const float y : { 328.0f, 462.0f })
            {
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.fillRect (left, y, right - left, 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.055f));
                g.fillRect (left, y + 1.0f, right - left, 1.0f);
            }

            g.setColour (col::silk.withAlpha (0.32f));
            g.setFont (uiFont (8.0f, true, 0.22f));
            g.drawText ("MODEL FV-33", juce::Rectangle<float> (right - 140.0f, 506.0f, 140.0f, 10.0f), juce::Justification::centredRight, false);
            g.drawText ("FULLERTON AUDIO", juce::Rectangle<float> (right - 140.0f, 518.0f, 140.0f, 10.0f), juce::Justification::centredRight, false);
        }

        // Deck legends.
        g.setColour (col::silk.withAlpha (0.45f));
        g.setFont (uiFont (7.6f, true, 0.30f));
        g.drawText ("QUARTZ DIRECT DRIVE", juce::Rectangle<float> (480.0f, 520.0f, 186.0f, 10.0f), juce::Justification::centredRight, false);
        g.setColour (col::silk.withAlpha (0.85f));
        g.setFont (uiFont (15.0f, true, 0.40f));
        g.drawText ("FULLERTON", juce::Rectangle<float> (440.0f, 532.0f, 226.0f, 20.0f), juce::Justification::centredRight, false);

        drawScrew (g, 13.0f, 13.0f, 4.6f, 0.6f);
        drawScrew (g, (float) geo::width - 13.0f, 13.0f, 4.6f, 2.1f);
        drawScrew (g, 13.0f, (float) geo::height - 13.0f, 4.6f, 1.4f);
        drawScrew (g, (float) geo::width - 13.0f, (float) geo::height - 13.0f, 4.6f, 0.2f);

        return img;
    }

    Image toNative (const Image& img)
    {
        return juce::NativeImageType().convert (img);
    }
}

//==============================================================================
Skin::Skin()
{
    tonearmBounds = { 400.0f, 10.0f, 290.0f, 400.0f };

    background = toNative (renderBackground (renderPlatter()));
    dots       = toNative (renderDots());
    record     = toNative (renderRecord());
    recordSpin = toNative (renderRecordSpin());
    tonearm    = toNative (renderTonearm (tonearmBounds));
    knobLarge  = toNative (renderKnob (juce::roundToInt (geo::knobLarge * ss), 40));
    knobSmall  = toNative (renderKnob (juce::roundToInt (geo::knobSmall * ss), 28));
}

} // namespace fv
