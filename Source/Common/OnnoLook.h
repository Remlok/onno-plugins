#pragma once
// Gemeinsamer Look aller Onno-Plugins: Konsolen-Frontplatte mit pro Pixel beleuchteten
// 3D-Reglern und Chrom-Kippschalter, dazu der moderne Studio-Look.
// Alles wird in einem festen Raster (z. B. 800x300) gezeichnet und mit dem Fenster skaliert.

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace onno
{
using juce::Colour;
using juce::Graphics;
using juce::Image;
using juce::String;

// Licht von oben links, Halbvektor für Glanzlichter
constexpr float Lx = -0.42f, Ly = -0.62f, Lz = 0.66f;
constexpr float Hx = -0.2306f, Hy = -0.3405f, Hz = 0.9116f;

enum Ring { ringNone = 0, ringRed, ringBlue, ringGreen, ringAmber, ringBrown };

inline Colour rgb (int r, int g, int b, float a = 1.0f) { return Colour::fromFloatRGBA (r / 255.0f, g / 255.0f, b / 255.0f, a); }
inline Colour silk (float a = 1.0f) { return rgb (232, 232, 226, a); }

inline juce::Font font (float h, bool bold = true, bool italic = false)
{
    int style = (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0);
    return juce::Font (juce::FontOptions (h, style));
}

// Plattenfarbe in Höhe y (Raster-Koordinaten), gleicher Verlauf wie die Frontplatte
inline float plateAt (float y, float H = 300.0f) { return 0.105f - 0.03f * (y - 14.0f) / (H - 28.0f); }

inline void ctext (Graphics& g, const String& s, float cx, float y, float h)
{
    g.drawText (s, juce::Rectangle<float> (cx - 200.0f, y, 400.0f, h * 1.25f), juce::Justification::centredTop, false);
}

inline void ltext (Graphics& g, const String& s, float x, float y, float h)
{
    g.drawText (s, juce::Rectangle<float> (x, y, 600.0f, h * 1.25f), juce::Justification::topLeft, false);
}

inline void rtext (Graphics& g, const String& s, float right, float y, float h)
{
    g.drawText (s, juce::Rectangle<float> (right - 600.0f, y, 600.0f, h * 1.25f), juce::Justification::topRight, false);
}

inline void line (Graphics& g, float x1, float y1, float x2, float y2, float w)
{
    g.drawLine (x1, y1, x2, y2, w);
}

inline void arc (Graphics& g, float cx, float cy, float r, float a0, float a1, float w)
{
    juce::Path p;
    p.addCentredArc (cx, cy, r, r, 0.0f, a0, a1, true);
    g.strokePath (p, juce::PathStrokeType (w));
}

// ------------------------------------------------------------------ 3D-Regler
struct RGBf { float r, g, b; };

inline RGBf knobShade (int ring, float dx, float dy, float r, float ps)
{
    const float dist = std::max (0.0001f, std::sqrt (dx * dx + dy * dy));
    const float d = dist / r;
    const float ux = dx / dist, uy = dy / dist, tx = -uy, ty = ux;
    const float th = std::atan2 (dy, dx);
    static const float thL = std::atan2 (Ly, Lx);

    if (d < 0.58f)
    {
        // Alu-Kappe: leicht gewölbt, gebürstet, mit Fase am Rand
        float t = d < 0.52f ? 0.42f * d / 0.52f : 0.42f + 0.5f * (d - 0.52f) / 0.06f;
        float nx = ux * t, ny = uy * t, nz = std::sqrt (std::max (0.01f, 1 - t * t));
        float dif = std::max (0.0f, nx * Lx + ny * Ly + nz * Lz);
        float spec = std::pow (std::max (0.0f, nx * Hx + ny * Hy + nz * Hz), 80.0f);
        float an = std::cos (th - thL); an *= an; an *= an; an *= an;
        float c = 0.20f + 0.48f * dif + 0.45f * spec;
        if (d < 0.52f)
            c += 0.30f * an * (0.4f + 0.6f * d / 0.52f) + 0.012f * std::sin (dist / ps * 2.3f) + 0.010f * std::sin (dist / ps * 5.9f + 1.3f);
        return { c * 0.93f, c * 0.945f, c * 0.98f };
    }
    if (d < 0.645f)
    {
        // erhabener Ring zwischen Kappe und Rock (farbig oder dunkel)
        float t = (d - 0.6125f) / 0.0325f * 0.75f;
        float nx = ux * t, ny = uy * t, nz = std::sqrt (std::max (0.01f, 1 - t * t));
        float dif = std::max (0.0f, nx * Lx + ny * Ly + nz * Lz);
        float spec = std::pow (std::max (0.0f, nx * Hx + ny * Hy + nz * Hz), 40.0f);
        float sh = 0.35f + 0.65f * dif;
        switch (ring)
        {
            case ringRed:   return { 0.80f * sh + 0.45f * spec, 0.20f * sh + 0.40f * spec, 0.15f * sh + 0.40f * spec };
            case ringBlue:  return { 0.18f * sh + 0.40f * spec, 0.42f * sh + 0.40f * spec, 0.88f * sh + 0.45f * spec };
            case ringGreen: return { 0.20f * sh + 0.40f * spec, 0.72f * sh + 0.40f * spec, 0.36f * sh + 0.40f * spec };
            case ringAmber: return { 0.92f * sh + 0.45f * spec, 0.62f * sh + 0.40f * spec, 0.12f * sh + 0.35f * spec };
            case ringBrown: return { 0.62f * sh + 0.40f * spec, 0.40f * sh + 0.38f * spec, 0.24f * sh + 0.35f * spec };
            default: { float c = 0.02f + 0.05f * dif + 0.2f * spec; return { c, c, c }; }
        }
    }
    // schwarzer Rock: kegelförmig, gerändelt, gerundete Außenkante
    float t = d < 0.88f ? 0.36f : 0.36f + 0.58f * std::min (1.0f, (d - 0.88f) / 0.12f);
    float tn = d < 0.88f ? 0.22f * std::sin (th * 48) : 0.0f;
    float nx = ux * t + tx * tn, ny = uy * t + ty * tn, nz = std::sqrt (std::max (0.02f, 1 - t * t - tn * tn));
    float nl = std::sqrt (nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
    float dif = std::max (0.0f, nx * Lx + ny * Ly + nz * Lz);
    float spec = std::pow (std::max (0.0f, nx * Hx + ny * Hy + nz * Hz), 36.0f);
    float c = 0.025f + 0.08f * dif + 0.55f * spec;
    if (d < 0.70f) c *= 0.55f + 0.45f * (d - 0.645f) / 0.055f;   // Kontaktschatten an der Kappe
    return { c, c, c * 1.05f };
}

inline juce::PixelARGB px (float r, float g, float b)
{
    auto u8 = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, (int) std::lround (v * 255.0f)); };
    return juce::PixelARGB (255, u8 (r), u8 (g), u8 (b));
}

// Ein vorberechneter Bildpuffer mit seiner Lage im Raster
struct Sprite
{
    Image img;
    juce::Rectangle<float> area;
    void draw (Graphics& g) const { g.setOpacity (1.0f); g.drawImage (img, area, juce::RectanglePlacement::stretchToFit); }
};

// Reglerkörper inklusive Schlagschatten und Plattenhintergrund (deckend).
// ps = Bildpunkte pro Rastereinheit
inline Sprite buildKnob (float cx, float cy, float rBase, int ring, float ps, float H = 300.0f)
{
    const float m = std::ceil (rBase * 0.4f) + 2.0f;
    const float half = rBase + m;
    const int sz = (int) std::ceil (2 * half * ps);
    Sprite sp;
    sp.area = { cx - half, cy - half, sz / ps, sz / ps };
    sp.img = Image (Image::ARGB, sz, sz, false);
    Image::BitmapData bd (sp.img, Image::BitmapData::writeOnly);
    const float r = rBase * ps;
    for (int y = 0; y < sz; ++y)
    {
        const float pl = plateAt (sp.area.getY() + (y + 0.5f) / ps, H);
        for (int x = 0; x < sz; ++x)
        {
            const float dx = x + 0.5f - half * ps, dy = y + 0.5f - half * ps;
            const float dist = std::sqrt (dx * dx + dy * dy);
            const float sd = std::sqrt (dx * dx + (dy - r * 0.16f) * (dy - r * 0.16f)) / r;
            const float sa = 0.7f * juce::jlimit (0.0f, 1.0f, (1.2f - sd) / 0.35f);
            float cr = pl * (1 - sa), cg = cr, cb = (pl + 0.007f) * (1 - sa);
            if (dist < r + 1)
            {
                auto k = knobShade (ring, dx, dy, r, ps);
                const float cov = juce::jlimit (0.0f, 1.0f, r - dist + 0.5f);
                cr = cr * (1 - cov) + k.r * cov; cg = cg * (1 - cov) + k.g * cov; cb = cb * (1 - cov) + k.b * cov;
            }
            bd.setPixelColour (x, y, Colour (px (cr, cg, cb)));
        }
    }
    return sp;
}

// ------------------------------------------------------------- Chrom-Schalter
// Chrom spiegelt eine helle "Himmel"-Hälfte und einen dunklen "Boden"; dazu Glanzlicht.
inline float chrome (float nx, float ny, float nz)
{
    float nl = std::sqrt (nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
    float ry = 2 * nz * ny;
    float sky = juce::jlimit (0.0f, 1.0f, 0.45f - ry);
    float dif = std::max (0.0f, nx * Lx + ny * Ly + nz * Lz);
    float spec = std::pow (std::max (0.0f, nx * Hx + ny * Hy + nz * Hz), 50.0f);
    return (0.22f + 0.78f * sky) * (0.60f + 0.40f * dif) + 0.8f * spec;
}

// true, wenn (dx, dy) auf dem Hebel liegt; setzt dabei die Hebel-Normale
inline bool onLever (float dx, float dy, float dir, float ps, float& nx, float& ny, float& nz)
{
    const float L = 15 * ps, rb = 3.8f * ps;
    const float bx = dx, by = dy - dir * L, d2 = bx * bx + by * by;
    if (d2 < rb * rb)
    {
        nx = bx / rb; ny = by / rb; nz = std::sqrt (rb * rb - d2) / rb;
        return true;
    }
    const float along = dy * dir, hw = (3.0f - 1.0f * along / L) * ps;
    if (along >= 0 && along <= L && std::abs (dx) < hw)
    {
        nx = dx / hw * 0.95f; ny = -0.15f; nz = std::sqrt (std::max (0.05f, 1 - (dx / hw * 0.95f) * (dx / hw * 0.95f)));
        return true;
    }
    return false;
}

inline Sprite buildToggle (float cx, float cy, bool down, float ps, float H = 300.0f)
{
    const float half = 24.0f;
    const int sz = (int) std::ceil (2 * half * ps);
    const float dir = down ? 1.0f : -1.0f;
    const float R = 10.5f * ps;
    Sprite sp;
    sp.area = { cx - half, cy - half, sz / ps, sz / ps };
    sp.img = Image (Image::ARGB, sz, sz, false);
    Image::BitmapData bd (sp.img, Image::BitmapData::writeOnly);
    float nx, ny, nz;
    for (int y = 0; y < sz; ++y)
    {
        const float pl = plateAt (sp.area.getY() + (y + 0.5f) / ps, H);
        for (int x = 0; x < sz; ++x)
        {
            const float dx = x + 0.5f - half * ps, dy = y + 0.5f - half * ps;
            const float dist = std::sqrt (dx * dx + dy * dy);
            float cr = pl, cg = pl, cb = pl + 0.007f;
            if (onLever (dx, dy, dir, ps, nx, ny, nz))
            {
                float c = chrome (nx, ny, nz);
                cr = c * 0.97f; cg = c * 0.98f; cb = c;
            }
            else
            {
                // Schatten von Mutter und Hebel auf der Platte
                float sh = 0;
                if (std::hypot (dx - 2 * ps, dy - 3 * ps) < R * 1.12f) sh = 0.55f;
                float tx, ty, tz;
                if (onLever (dx - 3.5f * ps, dy - 4.5f * ps, dir, ps, tx, ty, tz)) sh = std::max (sh, 0.5f);
                cr *= 1 - sh; cg *= 1 - sh; cb *= 1 - sh;
                // Sechskantmutter
                float best = -1000, bk = 0;
                for (int k = 0; k < 6; ++k)
                {
                    float fa = k * juce::MathConstants<float>::pi / 3 + juce::MathConstants<float>::pi / 6;
                    float ap = dx * std::cos (fa) + dy * std::sin (fa);
                    if (ap > best) { best = ap; bk = fa; }
                }
                if (best < R)
                {
                    if (dist < 6.2f * ps)
                    {
                        float c;
                        if (dist < 3.0f * ps)
                        {
                            // Chrom-Kuppel, aus der der Hebel kommt
                            nx = 0.15f * dx / (3 * ps); ny = 0.15f * dy / (3 * ps); nz = std::sqrt (std::max (0.05f, 1 - nx * nx - ny * ny));
                            c = chrome (nx, ny, nz);
                            cr = c * 0.97f; cg = c * 0.98f; cb = c;
                        }
                        else
                        {
                            // Gewindebuchse mit Rillen
                            float t = 0.10f * std::sin (dist / ps * 3.4f);
                            if (dist > 5.4f * ps) t += 0.25f * (dist - 5.4f * ps) / (0.8f * ps);
                            nx = dx / dist * t; ny = dy / dist * t; nz = std::sqrt (std::max (0.05f, 1 - t * t));
                            c = chrome (nx, ny, nz);
                            cr = c * 0.95f; cg = c * 0.96f; cb = c;
                        }
                    }
                    else
                    {
                        // Mutter: flache Oberseite, abgeschrägte Kanten
                        if (best > R * 0.78f) { float t = 0.75f; nx = std::cos (bk) * t; ny = std::sin (bk) * t; nz = std::sqrt (1 - t * t); }
                        else { nx = dx / std::max (1.0f, dist) * 0.12f; ny = dy / std::max (1.0f, dist) * 0.12f; nz = 0.99f; }
                        float c = chrome (nx, ny, nz);
                        if (dist < 7.0f * ps) c *= 0.85f + 0.15f * (dist - 6.2f * ps) / (0.8f * ps);
                        const float cov = juce::jlimit (0.0f, 1.0f, R - best + 0.5f);
                        cr = cr * (1 - cov) + c * 0.93f * cov; cg = cg * (1 - cov) + c * 0.94f * cov; cb = cb * (1 - cov) + c * 0.97f * cov;
                    }
                }
            }
            bd.setPixelColour (x, y, Colour (px (cr, cg, cb)));
        }
    }
    return sp;
}

// ----------------------------------------------------------- Konsolen-Teile
inline void drawFaceplate (Graphics& g, float W = 800.0f, float H = 300.0f)
{
    g.fillAll (rgb (12, 12, 13));
    g.setGradientFill (juce::ColourGradient (Colour::fromFloatRGBA (0.105f, 0.105f, 0.112f, 1), 0, 14,
                                             Colour::fromFloatRGBA (0.075f, 0.075f, 0.082f, 1), 0, H - 14, false));
    g.fillRect (0.0f, 14.0f, W, H - 28);
    g.setGradientFill (juce::ColourGradient (Colour::fromFloatRGBA (0.86f, 0.87f, 0.88f, 1), 0, 0,
                                             Colour::fromFloatRGBA (0.56f, 0.57f, 0.58f, 1), 0, 14, false));
    g.fillRect (0.0f, 0.0f, W, 14.0f);
    g.setGradientFill (juce::ColourGradient (Colour::fromFloatRGBA (0.80f, 0.81f, 0.82f, 1), 0, H - 14,
                                             Colour::fromFloatRGBA (0.50f, 0.51f, 0.52f, 1), 0, H, false));
    g.fillRect (0.0f, H - 14, W, 14.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (0.0f, 14.0f, W, 1.0f);
    g.fillRect (0.0f, H - 15, W, 1.0f);
    g.setColour (rgb (30, 30, 32));
    for (auto [x, y] : { std::pair (24.0f, 7.0f), std::pair (W - 24, 7.0f), std::pair (24.0f, H - 7), std::pair (W - 24, H - 7) })
        g.fillRoundedRectangle (x - 7, y - 3, 14, 6, 3);
}

inline void drawSeam (Graphics& g, float x, float y0 = 26.0f, float h = 248.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRect (x, y0, 1.0f, h);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (x + 1, y0, 1.0f, h);
}

// Rechteckige Rack-Taste mit Beschriftung darüber
inline void drawButton (Graphics& g, float x, float y, float w, float h, bool on, const String& label)
{
    g.setFont (font (8)); g.setColour (silk (0.95f));
    ctext (g, label, x + w * 0.5f, y - 12, 8);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (x - 1, y - 1, w + 2, h + 3);
    if (on)
    {
        g.setColour (rgb (244, 236, 214)); g.fillRect (x, y, w, h);
        g.setColour (rgb (255, 196, 110, 0.35f)); g.fillRect (x + 2, y + 2, w - 4, h - 4);
        g.setColour (juce::Colours::white.withAlpha (0.6f)); g.fillRect (x, y, w, 1.0f);
    }
    else
    {
        g.setColour (rgb (112, 112, 110)); g.fillRect (x, y, w, h);
        g.setColour (juce::Colours::white.withAlpha (0.18f)); g.fillRect (x, y, w, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.3f)); g.fillRect (x, y + h - 2, w, 2.0f);
    }
}

inline void drawLed (Graphics& g, float x, float y, bool on, int r, int gg, int b, const String& label)
{
    if (on)
    {
        g.setColour (rgb (r, gg, b, 0.25f)); g.fillEllipse (x - 9, y - 9, 18, 18);
        g.setColour (rgb (r, gg, b));
    }
    else
        g.setColour (rgb (r / 4, gg / 4, b / 4));
    g.fillEllipse (x - 4.5f, y - 4.5f, 9, 9);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.6f : 0.15f));
    g.fillEllipse (x - 1.3f - 1.4f, y - 1.5f - 1.4f, 2.8f, 2.8f);
    g.setFont (font (8)); g.setColour (silk (0.9f));
    ctext (g, label, x, y + 9, 8);
}

// ------------------------------------------------------------ moderner Look
namespace modern
{
    inline Colour bg()     { return rgb (20, 22, 27); }
    inline Colour panel()  { return rgb (31, 35, 43); }
    inline Colour accent() { return rgb (58, 209, 197); }
    inline Colour warm()   { return rgb (255, 138, 61); }
    inline Colour dim()    { return rgb (132, 140, 156); }
    inline Colour text()   { return rgb (236, 239, 244); }

    inline void panelRect (Graphics& g, float x, float y, float w, float h)
    {
        g.setColour (panel()); g.fillRoundedRectangle (x, y, w, h, 10);
    }

    inline void pill (Graphics& g, float x, float w, bool on, bool hover, const String& label, Colour onColour)
    {
        g.setColour (on ? onColour : (hover ? rgb (56, 62, 75) : panel()));
        g.fillRoundedRectangle (x, 13, w, 20, 9.5f);
        g.setColour (on ? bg() : dim());
        g.setFont (font (10)); ctext (g, label, x + w * 0.5f, 18, 10);
    }

    inline void header (Graphics& g, const String& name)
    {
        g.setColour (accent()); g.fillEllipse (19, 19, 10, 10);
        g.setFont (font (18));
        g.setColour (text()); ltext (g, "ONNO", 36, 13, 18);
        g.setColour (accent()); ltext (g, name, 36 + font (18).getStringWidthFloat ("ONNO "), 13, 18);
    }
}
} // namespace onno
