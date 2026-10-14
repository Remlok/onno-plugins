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

bool inRect (juce::Point<float> p, float x, float y, float w, float h)
{
    return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
}
}

VC1Editor::VC1Editor (VC1Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setResizable (true, true);
    setResizeLimits (400, 150, 1600, 600);
    getConstrainer()->setFixedAspectRatio (800.0 / 300.0);
    setSize (800, 300);
    startTimerHz (30);
}

juce::RangedAudioParameter* VC1Editor::param (const char* id) const { return proc.apvts.getParameter (id); }
juce::RangedAudioParameter* VC1Editor::knobParam (int i) const { return param (VC1Processor::knobIds[i]); }
float VC1Editor::paramValue (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
float VC1Editor::knobValue (int i) const { return paramValue (VC1Processor::knobIds[i]); }

void VC1Editor::setChoice (const char* id, int index)
{
    auto* p = param (id);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
    p->endChangeGesture();
    repaint();
}

juce::Point<float> VC1Editor::toBase (juce::Point<float> p) const
{
    return { (p.x - ox) / s, (p.y - oy) / s };
}

int VC1Editor::hitKnob (juce::Point<float> b) const
{
    const bool modern = isModern();
    for (int i = 0; i < NK; ++i)
    {
        const auto& k = layout[i];
        const float dx = b.x - (modern ? k.mx : k.cx), dy = b.y - (modern ? k.my : k.cy);
        const float rr = modern ? 30.0f : k.r + 8.0f;
        if (dx * dx + dy * dy < rr * rr) return i;
    }
    return -1;
}

// ------------------------------------------------------------------- Maus
void VC1Editor::mouseMove (const juce::MouseEvent& e)
{
    mouseBase = toBase (e.position);
    const int h = hitKnob (mouseBase);
    if (h != hot) { hot = h; repaint(); }
}

void VC1Editor::mouseExit (const juce::MouseEvent&)
{
    mouseBase = { -1000, -1000 };
    if (drag < 0) { hot = -1; repaint(); }
}

void VC1Editor::mouseDown (const juce::MouseEvent& e)
{
    const auto b = toBase (e.position);
    if (! isModern())
    {
        if (inRect (b, 354, 206, 30, 18)) setChoice ("meter", 0);
        if (inRect (b, 392, 206, 30, 18)) setChoice ("meter", 1);
        if (inRect (b, 430, 206, 30, 18)) setChoice ("meter", 2);
        if (inRect (b, 724, 192, 36, 56)) setChoice ("bypass", paramValue ("bypass") > 0.5f ? 0 : 1);
        if (inRect (b, 380, 262, 54, 14)) { setChoice ("view", 1); return; }
    }
    else
    {
        if (inRect (b, 616, 13, 76, 20)) setChoice ("bypass", paramValue ("bypass") > 0.5f ? 0 : 1);
        if (inRect (b, 700, 13, 84, 20)) { setChoice ("view", 0); return; }
    }
    hot = hitKnob (b);
    if (hot >= 0)
    {
        drag = hot;
        dragNorm = knobParam (drag)->getValue();
        lastY = b.y;
        knobParam (drag)->beginChangeGesture();
    }
}

void VC1Editor::mouseDrag (const juce::MouseEvent& e)
{
    const auto b = toBase (e.position);
    mouseBase = b;
    if (drag < 0) return;
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + (lastY - b.y) / (e.mods.isShiftDown() ? 900.0f : 220.0f));
    lastY = b.y;
    knobParam (drag)->setValueNotifyingHost (dragNorm);
    repaint();
}

void VC1Editor::mouseUp (const juce::MouseEvent& e)
{
    if (drag >= 0) knobParam (drag)->endChangeGesture();
    drag = -1;
    hot = hitKnob (toBase (e.position));
    repaint();
}

void VC1Editor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int k = hitKnob (toBase (e.position));
    if (k < 0) return;
    auto* p = knobParam (k);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getDefaultValue());
    p->endChangeGesture();
    repaint();
}

void VC1Editor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int k = hitKnob (toBase (e.position));
    if (k < 0) return;
    auto* p = knobParam (k);
    const float dir = w.isReversed ? -1.0f : 1.0f;
    const float n = juce::jlimit (0.0f, 1.0f, p->getValue() + dir * w.deltaY * (e.mods.isShiftDown() ? 0.035f : 0.17f));
    p->beginChangeGesture();
    p->setValueNotifyingHost (n);
    p->endChangeGesture();
    repaint();
}

// ---------------------------------------------------------------- Anzeige
void VC1Editor::timerCallback()
{
    float v = a2db (proc.inPeak.exchange (0.0f));
    inDisp = v > inDisp ? v : std::max (-90.0f, inDisp - 1.2f);
    v = a2db (proc.outPeak.exchange (0.0f));
    outDisp = v > outDisp ? v : std::max (-90.0f, outDisp - 1.2f);
    v = proc.grPeak.exchange (0.0f);
    grDisp = v < grDisp ? v : std::min (0.0f, grDisp + 0.6f);

    // VU-Zeiger mit Trägheit
    const int meter = (int) paramValue ("meter");
    const float vt = meter == 2 ? grDisp : (meter == 1 ? outDisp + 18 : inDisp + 18);
    needle += (juce::jlimit (-0.03f, 1.06f, vuPos (vt)) - needle) * 0.22f;
    repaint();
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

void VC1Editor::rebuild (float ps)
{
    for (int i = 0; i < NK; ++i)
        knobSprites[(size_t) i] = buildKnob (layout[i].cx, layout[i].cy, layout[i].r, layout[i].ring, ps);
    toggleSprites[0] = buildToggle (742, 220, false, ps);
    toggleSprites[1] = buildToggle (742, 220, true, ps);
    builtPs = ps;
}

void VC1Editor::paint (juce::Graphics& g)
{
    s = std::max (0.25f, std::min (getWidth() / 800.0f, getHeight() / 300.0f));
    ox = std::floor ((getWidth() - 800 * s) * 0.5f);
    oy = std::floor ((getHeight() - 300 * s) * 0.5f);
    g.fillAll (isModern() ? modern::bg() : rgb (12, 12, 13));
    g.addTransform (juce::AffineTransform::scale (s).translated (ox, oy));

    if (isModern()) drawModern (g);
    else
    {
        // 3D-Teile in der tatsächlichen Bildschirmauflösung neu berechnen, wenn sie sich ändert
        const float ps = std::round (g.getInternalContext().getPhysicalPixelScaleFactor() * 4.0f) / 4.0f;
        if (std::abs (ps - builtPs) > 0.01f) rebuild (ps);
        drawConsole (g);
    }
}

void VC1Editor::drawKnob (juce::Graphics& g, int i)
{
    const auto& k = layout[i];
    const float cx = k.cx, cy = k.cy, r = k.r, rc = r * 0.58f;
    const float n = juce::jlimit (0.0f, 1.0f, knobParam (i)->getValue());
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
    auto* p = knobParam (i);
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
    const int meter = (int) paramValue ("meter");
    const bool bypass = paramValue ("bypass") > 0.5f;
    const float thr = knobValue (0), ratio = knobValue (1), knee = knobValue (4), out = knobValue (5);

    drawFaceplate (g);
    drawSeam (g, 472);
    drawSeam (g, 696);

    for (int i = 0; i < NK; ++i) drawKnob (g, i);

    // Meter-Wahl mit Klammer
    drawButton (g, 354, 206, 30, 18, meter == 0, "INPUT");
    drawButton (g, 392, 206, 30, 18, meter == 1, "OUTPUT");
    drawButton (g, 430, 206, 30, 18, meter == 2, "G.C.");
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
    if (h >= 0)
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

    drawButton (g, 380, 262, 54, 14, false, "MODERN");
}

// --------------------------------------------------------- moderne Ansicht
void VC1Editor::drawModernKnob (juce::Graphics& g, int i)
{
    const float cx = layout[i].mx, cy = layout[i].my, r = 24;
    const float n = juce::jlimit (0.0f, 1.0f, knobParam (i)->getValue());
    const float a = a0 + n * 1.5f * pi;
    const bool hv = (drag >= 0 ? drag : hot) == i;
    g.setColour (rgb (46, 51, 62)); arc (g, cx, cy, r - 4, a0, -a0, 8);
    if (n > 0.002f) { g.setColour (modern::accent()); arc (g, cx, cy, r - 4, a0, a, 8); }
    g.setColour (rgb (24, 27, 34)); g.fillEllipse (cx - (r - 7), cy - (r - 7), 2 * (r - 7), 2 * (r - 7));
    g.setColour (hv ? modern::dim().withAlpha (0.8f) : rgb (56, 62, 75));
    g.drawEllipse (cx - (r - 7), cy - (r - 7), 2 * (r - 7), 2 * (r - 7), 1.0f);
    g.setColour (modern::accent());
    g.fillEllipse (cx + std::sin (a) * (r - 12) - 2.5f, cy - std::cos (a) * (r - 12) - 2.5f, 5, 5);
    g.setFont (font (10)); g.setColour (modern::dim()); ctext (g, labels[i], cx, cy - 46, 10);
    g.setFont (font (13)); g.setColour (hv ? juce::Colours::white : modern::text().withAlpha (0.9f));
    ctext (g, formatValue (i, knobValue (i)), cx, cy + 30, 13);
}

void VC1Editor::drawMeterBar (juce::Graphics& g, float x, const String& label, float db, bool isGr)
{
    const float top = 62, h = 180, bw = 16;
    g.setColour (rgb (15, 17, 21)); g.fillRect (x, top, bw, h);
    if (isGr)
    {
        g.setColour (modern::warm()); g.fillRect (x, top, bw, std::min (1.0f, -db / 24) * h);
    }
    else
    {
        const float bh = juce::jlimit (0.0f, 1.0f, (db + 60) / 66) * h;
        const float fy = top + h - bh, y6 = top + h - 54.0f / 66 * h, y0 = top + h - 60.0f / 66 * h;
        g.setColour (modern::accent()); g.fillRect (x, fy, bw, bh);
        if (fy < y6) { g.setColour (rgb (255, 200, 70)); g.fillRect (x, fy, bw, y6 - fy); }
        if (fy < y0) { g.setColour (rgb (255, 80, 80)); g.fillRect (x, fy, bw, y0 - fy); }
        g.setColour (modern::dim().withAlpha (0.8f)); g.fillRect (x - 3, y0, bw + 6, 1.0f);
    }
    g.setColour (modern::panel().withAlpha (0.85f));
    for (int k = 1; k < (int) (h / 4); ++k) g.fillRect (x, top + k * 4, bw, 1.0f);
    g.setFont (font (10)); g.setColour (modern::dim()); ctext (g, label, x + bw * 0.5f, 250, 10);
    g.setFont (font (10, false)); g.setColour (modern::text().withAlpha (0.9f));
    ctext (g, isGr ? String (db, 1) : (db <= -60 ? String ("-inf") : String (juce::roundToInt (db))), x + bw * 0.5f, 266, 10);
}

void VC1Editor::drawModern (juce::Graphics& g)
{
    const bool bypass = paramValue ("bypass") > 0.5f;
    const float thr = knobValue (0), ratio = knobValue (1), knee = knobValue (4), out = knobValue (5);

    modern::header (g, "VC-1");
    modern::pill (g, 616, 76, bypass, inRect (mouseBase, 616, 13, 76, 20), "BYPASS", modern::warm());
    modern::pill (g, 700, 84, false, inRect (mouseBase, 700, 13, 84, 20), "KONSOLE", modern::accent());

    modern::panelRect (g, 16, 48, 236, 240);
    modern::panelRect (g, 264, 48, 400, 240);
    modern::panelRect (g, 676, 48, 108, 240);

    // Kennlinie
    const float gx = 28, gy = 58, gs = 180;
    g.setColour (rgb (15, 17, 21)); g.fillRect (gx, gy, gs, gs);
    g.setColour (rgb (38, 43, 52));
    for (int k = 1; k < 5; ++k)
    {
        g.fillRect (gx + gs * k / 5, gy, 1.0f, gs);
        g.fillRect (gx, gy + gs * k / 5, gs, 1.0f);
    }
    g.setColour (rgb (56, 62, 75)); line (g, gx, gy + gs, gx + gs, gy, 1.0f);
    const float tx = gx + (thr + 60) / 60 * gs;
    g.setColour (modern::warm().withAlpha (0.6f));
    for (int k = 0; k < (int) (gs / 8); ++k) g.fillRect (tx, gy + k * 8, 1.0f, 4.0f);

    auto curveY = [&] (float x) {
        return gy + gs - (juce::jlimit (-60.0f, 0.0f, VC1Processor::gainComputer (x, thr, ratio, knee) + out) + 60) / 60 * gs;
    };
    juce::Path c, fill;
    fill.startNewSubPath (gx, gy + gs);
    for (int k = 0; k <= (int) gs; k += 1)
    {
        const float y = curveY (-60 + 60.0f * k / gs);
        if (k == 0) c.startNewSubPath (gx, y); else c.lineTo (gx + k, y);
        fill.lineTo (gx + k, y);
    }
    fill.lineTo (gx + gs, gy + gs); fill.closeSubPath();
    g.setColour (modern::accent().withAlpha (0.09f)); g.fillPath (fill);
    g.setColour (modern::accent()); g.strokePath (c, juce::PathStrokeType (2.0f));
    if (inDisp > -60)
    {
        const float dx = gx + (inDisp + 60) / 60 * gs, dy = curveY (inDisp);
        g.setColour (juce::Colours::white.withAlpha (0.18f)); g.fillEllipse (dx - 7, dy - 7, 14, 14);
        g.setColour (juce::Colours::white); g.fillEllipse (dx - 3, dy - 3, 6, 6);
    }
    g.setFont (font (10)); g.setColour (modern::dim()); ltext (g, "TRANSFER", 28, 248, 10);
    g.setFont (font (13)); g.setColour (modern::warm());
    rtext (g, bypass ? String ("BYPASS") : "GR " + String (grDisp, 1) + " dB", 240, 246, 13);
    g.setFont (font (10, false)); g.setColour (rgb (100, 108, 122));
    ltext (g, juce::CharPointer_UTF8 ("IN \xe2\x86\x92   OUT \xe2\x86\x91"), 28, 266, 10);

    for (int i = 0; i < NK; ++i) drawModernKnob (g, i);

    drawMeterBar (g, 690, "IN", inDisp, false);
    drawMeterBar (g, 722, "GR", grDisp, true);
    drawMeterBar (g, 754, "OUT", outDisp, false);
}
