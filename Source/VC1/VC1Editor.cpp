#include "VC1Editor.h"

using namespace onno;
using juce::String;

namespace
{
constexpr int NK = VC1Processor::numKnobs;

// Lage im 800x300-Raster, Radius, farbiger Ring
struct KnobLayout { float cx, cy, r; int ring; float mx, my; };
const KnobLayout layout[NK] = {
    { 80, 104, 30, ringRed,   314, 112 },
    { 200, 104, 30, ringNone, 414, 112 },
    { 312, 104, 22, ringNone, 514, 112 },
    { 410, 104, 22, ringNone, 614, 112 },
    { 80, 226, 20, ringNone,  364, 218 },
    { 200, 226, 24, ringBlue, 464, 218 },
    { 300, 226, 20, ringNone, 564, 218 },
};

const std::vector<float> scales[NK] = {
    { -60, -48, -36, -24, -12, 0 },
    { 1, 1.5f, 2, 3, 4, 6, 10, 20 },
    { 0.1f, 0.3f, 1, 3, 10, 30, 100 },
    { 10, 30, 100, 300, 1000 },
    { 0, 6, 12, 18, 24 },
    { -12, -6, 0, 6, 12, 18, 24 },
    { 0, 25, 50, 75, 100 },
};

const char* const labels[NK] = { "THRESHOLD", "RATIO", "ATTACK", "RELEASE", "KNEE", "OUTPUT GAIN", "BLEND" };

constexpr float pi = juce::MathConstants<float>::pi;
constexpr float a0 = -0.75f * pi;

float a2db (float x) { return 8.685889638f * std::log (std::max (x, 1.0e-9f)); }
float db2a (float x) { return std::exp (x * 0.11512925f); }
float vuPos (float d) { return (db2a (d) - 0.1f) / (db2a (3) - 0.1f); }

// moderne Ansicht
const juce::Rectangle<float> rDisplay { 16, 50, 768, 194 }, rCurve { 28, 62, 170, 170 }, rHist { 214, 62, 494, 170 };
float histY (float db) { return rHist.getY() + rHist.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); }
}

VC1Editor::VC1Editor (VC1Processor& p) : ModernEditor (p, p.apvts, "VC-1", W0, H0), proc (p)
{
    enableViewSwitch();
    addHelp ({ { "OUTPUT GAIN", "Make-up: hebt den Pegel nach der Kompression wieder an. Mit MATCH automatisch fair." },
               { "BLEND", "Parallel-Kompression: mischt das unbearbeitete Signal dazu. 100 % = nur komprimiert." },
               { "KNEE", "Weicher Übergang am Threshold. Höher = unauffälligerer, VCA-typisch runder Einsatz." } });
    syncView();
}

// ------------------------------------------------------------- Ansichten
void VC1Editor::syncView()
{
    const int v = value ("view") > 0.5f ? 1 : 0;
    if (v == builtView) return;
    builtView = v;
    clearControls();
    if (v == 1) buildModern(); else buildConsole();
    addAnalogStrip();
}

void VC1Editor::buildConsole()
{
    for (int i = 0; i < NK; ++i)
    {
        MKnob k { VC1Processor::knobIds[i], labels[i], layout[i].cx, layout[i].cy + top, layout[i].r, modern::accent(),
                  [this, i] (float v) { return formatValue (i, v); } };
        k.noValue = true;
        knobs.push_back (k);
    }
    static const char* const ml[] = { "INPUT", "OUTPUT", "G.C." };
    static const char* const mh[] = { "Das VU zeigt den Eingangspegel (0 VU = -18 dBFS).",
                                      "Das VU zeigt den Ausgangspegel (0 VU = -18 dBFS).",
                                      "Das VU zeigt die Gain Reduction des Kompressors." };
    for (int m = 0; m < 3; ++m)
    {
        MButton b { { 354.0f + 38.0f * m, 206 + top, 30, 18 }, [m] { return String (ml[m]); },
                    [this, m] { return (int) value ("meter") == m; }, [this, m] { setValue ("meter", (float) m); } };
        b.help = String::fromUTF8 (mh[m]);
        b.paint = [m] (Graphics& g, juce::Rectangle<float> r, bool on, bool) { drawButton (g, r.getX(), r.getY(), r.getWidth(), r.getHeight(), on, ml[m]); };
        buttons.push_back (b);
    }
    // Kippschalter: das Bild zeichnet die Konsole, hier nur die Fläche
    MButton byp { { 724, 192 + top, 36, 56 }, [] { return String ("BYPASS"); }, [this] { return flag ("bypass"); }, [this] { toggle ("bypass"); } };
    byp.paint = [] (Graphics&, juce::Rectangle<float>, bool, bool) {};
    buttons.push_back (byp);
}

void VC1Editor::buildModern()
{
    display = rDisplay;
    struct K { int i; Colour c; bool bip; };
    const K ks[] = { { 0, mc::red(), false }, { 1, mc::red(), false }, { 2, mc::amber(), false }, { 3, mc::amber(), false },
                     { 4, modern::accent(), false }, { 5, modern::accent(), false }, { 6, mc::green(), false } };
    float x = 88;
    for (auto& k : ks)
    {
        knobs.push_back ({ VC1Processor::knobIds[k.i], labels[k.i], x, 298, 22, k.c,
                           [this, i = k.i] (float v) { return formatValue (i, v); }, k.bip });
        x += 104;
    }
}

// ---------------------------------------------------------------- Anzeige
void VC1Editor::tick()
{
    const float in = dsp::a2db (proc.inPeak.exchange (0.0f));
    const float out = dsp::a2db (proc.outPeak.exchange (0.0f));
    const float gr = proc.grPeak.exchange (0.0f);
    inH.push (in); outH.push (out); grH.push (gr);
    inDisp = in > inDisp ? in : std::max (-90.0f, inDisp - 1.2f);
    outDisp = out > outDisp ? out : std::max (-90.0f, outDisp - 1.2f);
    grDisp = gr < grDisp ? gr : std::min (0.0f, grDisp + 0.6f);

    // VU-Zeiger mit Trägheit
    const int meter = (int) value ("meter");
    const float vt = meter == 2 ? grDisp : (meter == 1 ? outDisp + 18 : inDisp + 18);
    needle += (juce::jlimit (-0.03f, 1.06f, vuPos (vt)) - needle) * 0.22f;
}

String VC1Editor::formatValue (int i, float v) const
{
    switch (i)
    {
        case 0: case 4: return String (v, 1) + " dB";
        case 5: return (v > 0.05f ? "+" : "") + String (v, 1) + " dB";
        case 1: return String (v, 1) + " : 1";
        case 2: case 3: return String (v, v < 10 ? 2 : (v < 100 ? 1 : 0)) + " ms";
        default: return String (juce::roundToInt (v)) + " % WET";
    }
}

String VC1Editor::formatScale (int i, float v) const
{
    if (i == 6) return v == 0 ? "DRY" : (v == 100 ? "WET" : "");
    if (v >= 1000) return "1k";
    if (v < 1 && v > 0) return "." + String (juce::roundToInt (v * 10));
    if (i == 5 && v > 0) return "+" + String ((int) v);
    if (v == std::floor (v)) return String ((int) v);
    return String (v, 1);
}

void VC1Editor::rebuildSprites (float ps)
{
    for (int i = 0; i < NK; ++i)
        knobSprites[(size_t) i] = buildKnob (layout[i].cx, layout[i].cy, layout[i].r, layout[i].ring, ps);
    toggleSprites[0] = buildToggle (742, 220, false, ps);
    toggleSprites[1] = buildToggle (742, 220, true, ps);
    builtPs = ps;
}

void VC1Editor::drawBackground (Graphics& g)
{
    if (builtView == 1)
    {
        section (g, { 16, 254, 768, 88 });
        return;
    }
    // 3D-Teile in der tatsächlichen Bildschirmauflösung neu berechnen, wenn sie sich ändert
    const float ps = std::round (g.getInternalContext().getPhysicalPixelScaleFactor() * 4.0f) / 4.0f;
    if (std::abs (ps - builtPs) > 0.01f) rebuildSprites (ps);
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> (0, (int) top, (int) W, 300));
    g.addTransform (juce::AffineTransform::translation (0, top));
    drawConsole (g);
    g.restoreState();
}

void VC1Editor::drawKnob (Graphics& g, int i)
{
    if (builtView == 1 || i >= NK) { ModernEditor::drawKnob (g, i); return; }
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (0, top));
    drawConsoleKnob (g, i);
    g.restoreState();
}

void VC1Editor::drawConsoleKnob (juce::Graphics& g, int i)
{
    const auto& k = layout[i];
    const float cx = k.cx, cy = k.cy, r = k.r, rc = r * 0.58f;
    const float n = juce::jlimit (0.0f, 1.0f, param (VC1Processor::knobIds[i])->getValue());
    const float a = a0 + n * 1.5f * pi, sa = std::sin (a), ca = -std::cos (a);

    knobSprites[(size_t) i].draw (g);

    // Zeiger: gravierte weiße Linie im Rock, dunkle Rille in der Kappe
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    line (g, cx + sa * r * 0.68f, cy + ca * r * 0.68f, cx + sa * r * 0.95f, cy + ca * r * 0.95f, 3.2f);
    g.setColour (silk());
    line (g, cx + sa * r * 0.68f, cy + ca * r * 0.68f, cx + sa * r * 0.94f, cy + ca * r * 0.94f, 1.8f);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    line (g, cx + sa * rc * 0.18f, cy + ca * rc * 0.18f, cx + sa * rc * 0.86f, cy + ca * rc * 0.86f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    line (g, cx + sa * rc * 0.18f + 1.2f, cy + ca * rc * 0.18f + 1.2f, cx + sa * rc * 0.86f + 1.2f, cy + ca * rc * 0.86f + 1.2f, 0.8f);

    // Skala mit Zahlen
    auto f = font (8);
    g.setFont (f);
    auto* p = param (VC1Processor::knobIds[i]);
    for (float sv : scales[i])
    {
        const float ta = a0 + juce::jlimit (0.0f, 1.0f, p->convertTo0to1 (sv)) * 1.5f * pi;
        g.setColour (silk (0.9f));
        line (g, cx + std::sin (ta) * (r + 4), cy - std::cos (ta) * (r + 4), cx + std::sin (ta) * (r + 8), cy - std::cos (ta) * (r + 8), 1.0f);
        const String t = formatScale (i, sv);
        const float tw = f.getStringWidthFloat (t), th = 8.0f;
        const float rx = cx + std::sin (ta) * (r + 10 + tw * 0.5f), ry = cy - std::cos (ta) * (r + 10 + th * 0.5f);
        g.setColour (silk (0.75f));
        g.drawText (t, juce::Rectangle<float> (rx - tw * 0.5f - 2, ry - th * 0.5f - 1, tw + 4, th + 2), juce::Justification::centred, false);
    }

    g.setFont (font (11)); g.setColour (silk());
    ctext (g, labels[i], cx, cy - r - 35, 11);
}

void VC1Editor::drawConsole (juce::Graphics& g)
{
    const int meter = (int) value ("meter");
    const bool bypass = flag ("bypass");
    const float thr = knobValue (0), ratio = knobValue (1), knee = knobValue (4), out = knobValue (5);

    drawFaceplate (g);
    drawSeam (g, 472);
    drawSeam (g, 696);

    // Klammer der Meter-Wahl (die Tasten selbst sind Buttons der Basis)
    g.setColour (silk (0.8f));
    g.fillRect (354.0f, 236.0f, 26.0f, 1.0f); g.fillRect (434.0f, 236.0f, 26.0f, 1.0f);
    g.fillRect (354.0f, 231.0f, 1.0f, 6.0f); g.fillRect (459.0f, 231.0f, 1.0f, 6.0f);
    g.setFont (font (8)); g.setColour (silk (0.95f)); ctext (g, "METER", 407, 232, 8);

    // --- VU-Meter ---
    const float vx = 486, vy = 30, vw = 196, vh = 146;
    g.setColour (rgb (4, 4, 4)); g.fillRect (vx - 6, vy - 6, vw + 12, vh + 12);
    g.setColour (rgb (40, 40, 42)); g.fillRect (vx - 6, vy - 6, vw + 12, 1.0f);
    g.setGradientFill (juce::ColourGradient (Colour::fromFloatRGBA (1.0f, 0.80f, 0.47f, 1), 0, vy,
                                             Colour::fromFloatRGBA (0.95f, 0.60f, 0.23f, 1), 0, vy + vh, false));
    g.fillRect (vx, vy, vw, vh);
    for (int k = 0; k < 14; ++k)
    {
        const float q = 1.0f - k / 14.0f;
        g.setColour (Colour::fromFloatRGBA (0.35f, 0.14f, 0.0f, 0.09f * q * q));
        g.drawRect (vx + k, vy + k, vw - 2 * k, vh - 2 * k, 1.0f);
    }
    const float pvx = vx + vw * 0.5f, pvy = vy + vh + 26, vr = 118, amax = 0.64f;
    const float a0db = -amax + vuPos (0) * 2 * amax;
    g.setColour (rgb (62, 32, 12)); arc (g, pvx, pvy, vr - 0.75f, -amax, a0db, 1.5f);
    g.setColour (rgb (196, 36, 22)); arc (g, pvx, pvy, vr + 1.25f, a0db, amax, 2.5f);
    g.setFont (font (9));
    for (int d : { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 })
    {
        const float ta = -amax + vuPos ((float) d) * 2 * amax;
        g.setColour (d > 0 ? rgb (196, 36, 22) : rgb (62, 32, 12));
        line (g, pvx + std::sin (ta) * vr, pvy - std::cos (ta) * vr, pvx + std::sin (ta) * (vr + 8), pvy - std::cos (ta) * (vr + 8), 1.4f);
        ctext (g, (d > 0 ? "+" : "") + String (std::abs (d)), pvx + std::sin (ta) * (vr + 16), pvy - std::cos (ta) * (vr + 16) - 5, 9);
    }
    g.setColour (rgb (62, 32, 12));
    g.setFont (font (15)); ctext (g, "VU", pvx, vy + 82, 15);
    g.setFont (font (8));
    ctext (g, meter == 2 ? "GAIN REDUCTION" : (meter == 1 ? "OUTPUT  0 VU = -18 dBFS" : "INPUT  0 VU = -18 dBFS"), pvx, vy + 102, 8);
    g.setColour (rgb (62, 32, 12, 0.85f));
    g.setFont (juce::Font (juce::FontOptions ("Georgia", 12.0f, juce::Font::italic)));
    ctext (g, "Onno", pvx, vy + 114, 12);

    const float na = -amax + needle * 2 * amax;
    const float rs = (pvy - (vy + vh - 12)) / std::cos (na);
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    line (g, pvx + std::sin (na) * rs + 3, pvy - std::cos (na) * rs + 3, pvx + std::sin (na) * (vr + 10) + 3, pvy - std::cos (na) * (vr + 10) + 3, 1.5f);
    g.setColour (rgb (22, 18, 14));
    line (g, pvx + std::sin (na) * rs, pvy - std::cos (na) * rs, pvx + std::sin (na) * (vr + 10), pvy - std::cos (na) * (vr + 10), 1.3f);
    g.setColour (rgb (14, 12, 10)); g.fillRect (vx, vy + vh - 12, vw, 12.0f);
    {
        juce::Path glass; glass.addTriangle (vx, vy, vx + vw * 0.6f, vy, vx, vy + vh * 0.7f);
        g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillPath (glass);
    }

    // --- OLED-Anzeige ---
    const float dxo = 486, dyo = 190, dwo = 196, dho = 30;
    g.setColour (rgb (5, 7, 9)); g.fillRect (dxo, dyo, dwo, dho);
    g.setColour (rgb (44, 46, 50)); g.drawRect (dxo, dyo, dwo, dho, 1.0f);
    g.setFont (font (12));
    const int h = drag >= 0 ? drag : hot;
    if (h >= 0 && h < NK)
    {
        g.setColour (rgb (120, 200, 255)); ltext (g, labels[h], dxo + 10, dyo + 9, 12);
        g.setColour (rgb (226, 240, 255)); rtext (g, formatValue (h, knobValue (h)), dxo + dwo - 10, dyo + 9, 12);
    }
    else
    {
        g.setColour (rgb (226, 240, 255));
        ltext (g, bypass ? String ("BYPASS") : "GR " + String (grDisp, 1), dxo + 10, dyo + 9, 12);
        g.setColour (rgb (140, 160, 180));
        ltext (g, inDisp <= -60 ? String ("IN -inf") : "IN " + String (juce::roundToInt (inDisp)), dxo + 96, dyo + 9, 12);
        ltext (g, outDisp <= -60 ? String ("OUT -inf") : "OUT " + String (juce::roundToInt (outDisp)), dxo + 140, dyo + 9, 12);
    }

    // --- Markenschild ---
    g.setFont (font (26)); g.setColour (silk()); ltext (g, "VC-1", 486, 236, 26);
    g.setFont (font (11)); g.setColour (silk (0.95f)); ltext (g, "ONNO", 562, 240, 11);
    g.setFont (font (8)); g.setColour (silk (0.75f)); ltext (g, "VCA COMPRESSOR", 562, 255, 8);

    // --- Kennlinie (OLED) ---
    const float gx = 706, gy = 30, gs = 82, gi = gs - 4;
    g.setColour (rgb (5, 7, 9)); g.fillRect (gx, gy, gs, gs);
    g.setColour (rgb (44, 46, 50)); g.drawRect (gx, gy, gs, gs, 1.0f);
    g.setColour (rgb (28, 38, 48));
    for (int k = 1; k < 4; ++k)
    {
        g.fillRect (gx + gs * k / 4, gy + 1, 1.0f, gs - 2);
        g.fillRect (gx + 1, gy + gs * k / 4, gs - 2, 1.0f);
    }
    auto curveY = [&] (float x) {
        const float y = juce::jlimit (-60.0f, 0.0f, VC1Processor::gainComputer (x, thr, ratio, knee) + out);
        return gy + gs - 2 - (y + 60) / 60 * gi;
    };
    juce::Path c;
    for (int k = 0; k <= (int) gi; ++k)
    {
        const float x = gx + 2 + k, y = curveY (-60 + 60.0f * k / gi);
        if (k == 0) c.startNewSubPath (x, y); else c.lineTo (x, y);
    }
    g.setColour (rgb (120, 200, 255)); g.strokePath (c, juce::PathStrokeType (1.0f));
    if (inDisp > -60)
    {
        g.setColour (rgb (226, 240, 255));
        g.fillEllipse (gx + 2 + (inDisp + 60) / 60 * gi - 2.2f, curveY (inDisp) - 2.2f, 4.4f, 4.4f);
    }
    g.setFont (font (8)); g.setColour (silk (0.9f)); ctext (g, "CURVE", gx + gs * 0.5f, gy + gs + 5, 8);

    // --- Status-LEDs ---
    drawLed (g, 726, 152, ! bypass && inDisp > -60 && inDisp <= thr, 70, 230, 100, "BELOW");
    drawLed (g, 766, 152, ! bypass && inDisp > thr, 255, 70, 50, "ABOVE");

    // --- Bypass-Kippschalter ---
    g.setFont (font (8)); g.setColour (silk (0.95f));
    ctext (g, "COMP", 742, 186, 8); ctext (g, "BYPASS", 742, 246, 8);
    toggleSprites[bypass ? 1 : 0].draw (g);
}

// --------------------------------------------------------- moderne Ansicht
void VC1Editor::drawDisplay (Graphics& g, juce::Rectangle<float>)
{
    const float thr = knobValue (0), ratio = knobValue (1), knee = knobValue (4), out = knobValue (5);
    const bool bypass = flag ("bypass");

    // ---- Kennlinie
    const auto c = rCurve;
    auto cx = [c] (float db) { return c.getX() + c.getWidth() * (db + 60.0f) / 60.0f; };
    auto cy = [c] (float db) { return c.getBottom() - c.getHeight() * juce::jlimit (-0.1f, 1.1f, (db + 60.0f) / 60.0f); };
    g.setColour (rgb (24, 27, 34)); g.fillRoundedRectangle (c, 4);
    for (int d = -48; d < 0; d += 12)
    {
        g.setColour (mc::grid());
        g.fillRect (cx ((float) d), c.getY(), 1.0f, c.getHeight());
        g.fillRect (c.getX(), cy ((float) d), c.getWidth(), 1.0f);
    }
    g.setColour (rgb (255, 255, 255, 0.12f));
    line (g, c.getX(), c.getBottom(), c.getRight(), c.getY(), 1.0f);
    g.setColour (mc::red().withAlpha (0.35f));
    g.fillRect (cx (thr), c.getY(), 1.0f, c.getHeight());
    juce::Path curve;
    for (float x = -60; x <= 0.01f; x += 0.5f)
    {
        const float y = VC1Processor::gainComputer (x, thr, std::max (1.0f, ratio), knee) + out;
        if (x == -60) curve.startNewSubPath (cx (x), cy (y)); else curve.lineTo (cx (x), cy (y));
    }
    g.saveState();
    g.reduceClipRegion (c.toNearestInt());
    g.setColour (modern::accent()); g.strokePath (curve, juce::PathStrokeType (2.2f));
    if (inDisp > -60)
    {
        const float yv = VC1Processor::gainComputer (inDisp, thr, std::max (1.0f, ratio), knee) + out;
        g.setColour (juce::Colours::white); g.fillEllipse (cx (inDisp) - 4, cy (yv) - 4, 8, 8);
    }
    g.restoreState();
    g.setFont (font (9, false)); g.setColour (mc::gridText());
    ltext (g, "IN", c.getRight() - 14, c.getBottom() - 13, 9);
    ltext (g, "OUT", c.getX() + 4, c.getY() + 3, 9);

    // ---- Verlauf: Eingang (Fläche), Ausgang (Linie), Gain Reduction (von oben)
    const auto h = rHist;
    g.setColour (rgb (24, 27, 34)); g.fillRoundedRectangle (h, 4);
    g.setFont (font (9, false));
    for (int d = -12; d > -60; d -= 12)
    {
        g.setColour (mc::grid()); g.fillRect (h.getX(), histY ((float) d), h.getWidth(), 1.0f);
        g.setColour (mc::gridText()); ltext (g, String (d), h.getX() + 4, histY ((float) d) - 12, 9);
    }
    const int N = inH.size();
    const float dx = h.getWidth() / (float) (N - 1);
    auto grY = [h] (float v) { return h.getY() + h.getHeight() * juce::jlimit (0.0f, 1.0f, -v / 24.0f); };
    juce::Path inP, outP, grP;
    inP.startNewSubPath (h.getX(), h.getBottom());
    for (int i = 0; i < N; ++i)
    {
        const float x = h.getX() + i * dx;
        inP.lineTo (x, histY (inH[i]));
        if (i == 0) { outP.startNewSubPath (x, histY (outH[i])); grP.startNewSubPath (x, grY (grH[i])); }
        else { outP.lineTo (x, histY (outH[i])); grP.lineTo (x, grY (grH[i])); }
    }
    inP.lineTo (h.getRight(), h.getBottom()); inP.closeSubPath();
    g.setColour (rgb (120, 135, 160, 0.28f)); g.fillPath (inP);
    g.setColour (modern::accent().withAlpha (0.9f)); g.strokePath (outP, juce::PathStrokeType (1.4f));
    juce::Path grFill (grP); grFill.lineTo (h.getRight(), h.getY()); grFill.lineTo (h.getX(), h.getY()); grFill.closeSubPath();
    g.setColour (mc::red().withAlpha (0.18f)); g.fillPath (grFill);
    g.setColour (mc::red()); g.strokePath (grP, juce::PathStrokeType (1.6f));

    // Threshold-Linie (ziehbar)
    const float ty = histY (thr);
    g.setColour (mc::red().withAlpha (0.7f));
    for (float x = h.getX(); x < h.getRight(); x += 8) g.fillRect (x, ty, 4.0f, 1.2f);
    g.setFont (font (9)); ltext (g, "THR " + fmt::db (thr), h.getX() + 34, ty - 13, 9);

    g.setColour (rgb (14, 16, 21, 0.85f)); g.fillRoundedRectangle (h.getRight() - 10 - 120, h.getY() + 6, 124, 44, 6);
    g.setFont (font (20)); g.setColour (bypass ? modern::warm() : mc::red());
    rtext (g, bypass ? String ("BYPASS") : String (grDisp, 1) + " dB", h.getRight() - 10, h.getY() + 8, 20);
    g.setFont (font (9)); g.setColour (modern::dim());
    rtext (g, "GAIN REDUCTION", h.getRight() - 10, h.getY() + 34, 9);

    // ---- Pegel
    const float my = 62, mh = 156;
    drawMeter (g, { 724, my, 12, mh }, inDisp, modern::accent());
    drawMeter (g, { 744, my, 12, mh }, grDisp, mc::red(), true, -24.0f);
    drawMeter (g, { 764, my, 12, mh }, outDisp, modern::accent());
    g.setFont (font (9)); g.setColour (modern::dim());
    ctext (g, "IN", 730, my + mh + 4, 9); ctext (g, "GR", 750, my + mh + 4, 9); ctext (g, "OUT", 770, my + mh + 4, 9);

    headerInfo (g, String::fromUTF8 ("Ziehen: Threshold  ·  Mausrad: Ratio"));
}

bool VC1Editor::displayDown (juce::Point<float> p, const juce::MouseEvent& e)
{
    beginEdit ("thr");
    displayDrag (p, e);
    return true;
}

void VC1Editor::displayDrag (juce::Point<float> p, const juce::MouseEvent&)
{
    float db;
    if (p.x < rHist.getX() - 8) db = -60.0f + 60.0f * (p.x - rCurve.getX()) / rCurve.getWidth();   // Kennlinie: x = Threshold
    else db = -60.0f * (p.y - rHist.getY()) / rHist.getHeight();
    edit ("thr", juce::jlimit (-60.0f, 0.0f, db));
}

void VC1Editor::displayUp (juce::Point<float>) { endEdit ("thr"); }

void VC1Editor::displayWheel (juce::Point<float>, float d, const juce::MouseEvent&)
{
    setValue ("ratio", juce::jlimit (1.0f, 20.0f, value ("ratio") * std::exp (d * 0.8f)));
}
