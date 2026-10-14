#include "CS1Editor.h"

using namespace onno;
using juce::String;

namespace
{
enum Fmt { fDb, fHz, fMs, fPct, fRatio, fQ, fRange };
enum Mod { mInput, mFilter, mGate, mEq, mComp, mDs, mOut };

struct KnobDef
{
    const char* id; const char* label; float cx, cy, r; int ring; Fmt fmt;
    float s0, s1, s2; Mod mod;
};

// Lage im 1100x380-Raster; s0..s2 = beschriftete Skalenwerte (links, oben, rechts)
const KnobDef knobs[] = {
    { "trim", "TRIM", 59, 90, 18, ringNone, fDb, -24, 0, 24, mInput },
    { "drive", "DRIVE", 59, 170, 18, ringAmber, fPct, 0, 50, 100, mInput },
    { "hpf", "HPF", 144, 90, 18, ringNone, fHz, 16, 80, 400, mFilter },
    { "lpf", "LPF", 144, 170, 18, ringNone, fHz, 3000, 8000, 22000, mFilter },
    { "gateThr", "THRESH", 234, 90, 18, ringNone, fDb, -80, -40, 0, mGate },
    { "gateRange", "RANGE", 234, 170, 16, ringNone, fRange, 0, 30, 60, mGate },
    { "gateRel", "RELEASE", 234, 250, 16, ringNone, fMs, 10, 100, 1000, mGate },
    { "lfGain", "LF", 331, 90, 20, ringBrown, fDb, -18, 0, 18, mEq },
    { "lmfGain", "LMF", 426, 90, 20, ringBlue, fDb, -18, 0, 18, mEq },
    { "hmfGain", "HMF", 521, 90, 20, ringGreen, fDb, -18, 0, 18, mEq },
    { "hfGain", "HF", 616, 90, 20, ringRed, fDb, -18, 0, 18, mEq },
    { "lfFreq", "FREQ", 331, 170, 18, ringNone, fHz, 30, 120, 450, mEq },
    { "lmfFreq", "FREQ", 426, 170, 18, ringNone, fHz, 200, 700, 2500, mEq },
    { "hmfFreq", "FREQ", 521, 170, 18, ringNone, fHz, 600, 2000, 7000, mEq },
    { "hfFreq", "FREQ", 616, 170, 18, ringNone, fHz, 1500, 5000, 16000, mEq },
    { "lmfQ", "Q", 426, 250, 14, ringNone, fQ, 0.4f, 1.4f, 5, mEq },
    { "hmfQ", "Q", 521, 250, 14, ringNone, fQ, 0.4f, 1.4f, 5, mEq },
    { "compThr", "THRESH", 714, 90, 20, ringNone, fDb, -50, -25, 0, mComp },
    { "compRatio", "RATIO", 814, 90, 18, ringNone, fRatio, 1, 4, 20, mComp },
    { "compAtt", "ATTACK", 714, 170, 16, ringNone, fMs, 0.1f, 2, 30, mComp },
    { "compRel", "RELEASE", 814, 170, 16, ringNone, fMs, 30, 200, 1500, mComp },
    { "compMix", "MIX", 714, 250, 16, ringNone, fPct, 0, 50, 100, mComp },
    { "dsFreq", "FREQ", 909, 90, 16, ringNone, fHz, 3000, 6000, 12000, mDs },
    { "dsThr", "THRESH", 909, 170, 16, ringNone, fDb, -50, -25, 0, mDs },
    { "dsRange", "RANGE", 909, 250, 14, ringNone, fRange, 0, 10, 20, mDs },
    { "out", "LEVEL", 1020, 90, 20, ringBlue, fDb, -24, 0, 24, mOut },
};
constexpr int NK = (int) std::size (knobs);

struct Column { float x0, x1; const char* title; const char* onId; };
const Column columns[] = {
    { 14, 104, "INPUT", nullptr }, { 104, 184, "FILTER", nullptr }, { 184, 284, "GATE", "gateOn" },
    { 284, 664, "EQ", "eqOn" }, { 664, 864, "COMP", "compOn" }, { 864, 954, "DE-ESS", "dsOn" }, { 954, 1086, "OUTPUT", nullptr },
};

const char* const moduleNames[] = { "INPUT", "FILTER", "GATE", "EQ", "COMP", "DE-ESS", "OUTPUT" };

// Akzentfarben der modernen Ansicht je Modul
Colour modColour (int m)
{
    switch (m)
    {
        case mInput:  return rgb (255, 176, 72);
        case mFilter: return rgb (150, 170, 200);
        case mGate:   return rgb (96, 214, 136);
        case mComp:   return rgb (255, 138, 61);
        case mDs:     return rgb (240, 112, 176);
        default:      return modern::accent();
    }
}

Colour ringColour (int ring, int mod)
{
    switch (ring)
    {
        case ringBrown: return rgb (214, 150, 96);
        case ringBlue:  return rgb (96, 146, 255);
        case ringGreen: return rgb (96, 214, 136);
        case ringRed:   return rgb (255, 96, 90);
        default:        return modColour (mod);
    }
}

// Schaltflächen der Workflow-Leiste und in den Modulen
constexpr float presetX0 = 290, presetW = 44, presetStep = 50, rowY = 322, rowH = 18;
const juce::Rectangle<float> rAutoTrim { 24, 244, 70, 14 }, rLfBell { 311, 244, 40, 14 }, rHfBell { 596, 244, 40, 14 },
                             rPreEq { 784, 244, 60, 14 }, rAutoGain { 985, 268, 70, 14 },
                             rA { 806, rowY, 26, rowH }, rB { 838, rowY, 26, rowH }, rView { 876, rowY, 54, rowH },
                             rBypass { 1026, 300, 48, 52 }, rOled { 14, 300, 262, 50 };

juce::Rectangle<float> presetRect (int i) { return { presetX0 + i * presetStep, rowY, presetW, rowH }; }
juce::Rectangle<float> titleRect (int c) { return { columns[c].x0 + 4, 16, columns[c].x1 - columns[c].x0 - 8, 16 }; }

constexpr float pi = juce::MathConstants<float>::pi;
constexpr float a0 = -0.75f * pi;

float a2db (float x) { return 8.685889638f * std::log (std::max (x, 1.0e-9f)); }

String hz (float v)
{
    if (v >= 1000) return String (v / 1000.0f, v < 10000 ? 1 : 0) + "k";
    return String (juce::roundToInt (v));
}
}

CS1Editor::CS1Editor (CS1Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setResizable (true, true);
    setResizeLimits (550, 190, 2200, 760);
    getConstrainer()->setFixedAspectRatio (W / H);
    setSize ((int) W, (int) H);
    knobSprites.resize ((size_t) NK);
    startTimerHz (30);
}

void CS1Editor::toggle (const char* id)
{
    proc.setParam (id, flag (id) ? 0.0f : 1.0f);
    repaint();
}

int CS1Editor::hitKnob (juce::Point<float> b) const
{
    for (int i = 0; i < NK; ++i)
    {
        const float dx = b.x - knobs[i].cx, dy = b.y - knobs[i].cy, rr = knobs[i].r + 8;
        if (dx * dx + dy * dy < rr * rr) return i;
    }
    return -1;
}

// ------------------------------------------------------------------- Maus
void CS1Editor::mouseMove (const juce::MouseEvent& e)
{
    mouseBase = toBase (e.position);
    hot = hitKnob (mouseBase);
    repaint();
}

void CS1Editor::mouseExit (const juce::MouseEvent&)
{
    mouseBase = { -1000, -1000 };
    if (drag < 0) hot = -1;
    repaint();
}

void CS1Editor::mouseDown (const juce::MouseEvent& e)
{
    const auto b = toBase (e.position);
    for (int c = 0; c < (int) std::size (columns); ++c)
        if (columns[c].onId != nullptr && titleRect (c).contains (b)) { toggle (columns[c].onId); return; }
    for (int i = 0; i < CS1Processor::numPresets; ++i)
        if (presetRect (i).contains (b)) { proc.applyPreset (i); repaint(); return; }
    if (rAutoTrim.contains (b)) { proc.autoTrimArmed = ! proc.autoTrimArmed.load(); proc.autoTrimProgress = 0; repaint(); return; }
    if (rLfBell.contains (b)) { toggle ("lfBell"); return; }
    if (rHfBell.contains (b)) { toggle ("hfBell"); return; }
    if (rPreEq.contains (b)) { toggle ("compPre"); return; }
    if (rAutoGain.contains (b)) { toggle ("autoGain"); return; }
    if (rA.contains (b)) { proc.selectSlot (0); repaint(); return; }
    if (rB.contains (b)) { proc.selectSlot (1); repaint(); return; }
    if (rView.contains (b)) { toggle ("view"); return; }
    if (rBypass.contains (b)) { toggle ("bypass"); return; }

    hot = hitKnob (b);
    if (hot >= 0)
    {
        drag = hot;
        auto* p = param (knobs[drag].id);
        dragNorm = p->getValue();
        lastY = b.y;
        p->beginChangeGesture();
    }
}

void CS1Editor::mouseDrag (const juce::MouseEvent& e)
{
    const auto b = toBase (e.position);
    mouseBase = b;
    if (drag < 0) return;
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + (lastY - b.y) / (e.mods.isShiftDown() ? 900.0f : 220.0f));
    lastY = b.y;
    param (knobs[drag].id)->setValueNotifyingHost (dragNorm);
    repaint();
}

void CS1Editor::mouseUp (const juce::MouseEvent& e)
{
    if (drag >= 0) param (knobs[drag].id)->endChangeGesture();
    drag = -1;
    hot = hitKnob (toBase (e.position));
    repaint();
}

void CS1Editor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int k = hitKnob (toBase (e.position));
    if (k < 0) return;
    auto* p = param (knobs[k].id);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getDefaultValue());
    p->endChangeGesture();
    repaint();
}

void CS1Editor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int k = hitKnob (toBase (e.position));
    if (k < 0) return;
    auto* p = param (knobs[k].id);
    const float dir = w.isReversed ? -1.0f : 1.0f;
    p->beginChangeGesture();
    p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + dir * w.deltaY * (e.mods.isShiftDown() ? 0.035f : 0.17f)));
    p->endChangeGesture();
    repaint();
}

// ---------------------------------------------------------------- Anzeige
void CS1Editor::timerCallback()
{
    auto fall = [] (float& disp, float v, float rate) { disp = v > disp ? v : std::max (-90.0f, disp - rate); };
    auto rise = [] (float& disp, float v) { disp = v < disp ? v : std::min (0.0f, disp + 0.6f); };
    fall (inDisp, a2db (proc.inPeak.exchange (0.0f)), 1.2f);
    fall (outDisp, a2db (proc.outPeak.exchange (0.0f)), 1.2f);
    rise (gateDisp, proc.gateGr.exchange (0.0f));
    rise (compDisp, proc.compGr.exchange (0.0f));
    rise (dsDisp, proc.dsGr.exchange (0.0f));

    if (proc.autoTrimDone.exchange (false))
        proc.setParam ("trim", proc.autoTrimResult.load());
    repaint();
}

String CS1Editor::formatValue (int i, float v) const
{
    const String id = knobs[i].id;
    if (id == "hpf" && v <= 16.5f) return "OFF";
    if (id == "lpf" && v >= 21500.0f) return "OFF";
    switch (knobs[i].fmt)
    {
        case fDb:    return (v > 0.05f ? "+" : "") + String (v, 1) + " dB";
        case fRange: return String (v, 1) + " dB";
        case fHz:    return v >= 1000 ? String (v / 1000.0f, 2) + " kHz" : String (juce::roundToInt (v)) + " Hz";
        case fMs:    return String (v, v < 10 ? 2 : (v < 100 ? 1 : 0)) + " ms";
        case fPct:   return String (juce::roundToInt (v)) + " %";
        case fRatio: return String (v, 1) + " : 1";
        case fQ:     return "Q " + String (v, 2);
    }
    return {};
}

String CS1Editor::formatScale (int i, float v) const
{
    const String id = knobs[i].id;
    if ((id == "hpf" && v <= 16.5f) || (id == "lpf" && v >= 21500.0f)) return "OFF";
    switch (knobs[i].fmt)
    {
        case fHz: return hz (v);
        case fMs: return v >= 1000 ? String (v / 1000.0f, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "s"
                                   : (v < 1 ? "." + String (juce::roundToInt (v * 10)) : String (juce::roundToInt (v)));
        case fQ:  return v < 1 ? "." + String (juce::roundToInt (v * 10)) : String (v, v == std::floor (v) ? 0 : 1);
        case fDb: return (v > 0 ? "+" : "") + String (juce::roundToInt (v));
        default:  return String (juce::roundToInt (v));
    }
}

void CS1Editor::rebuild (float ps)
{
    for (int i = 0; i < NK; ++i)
        knobSprites[(size_t) i] = buildKnob (knobs[i].cx, knobs[i].cy, knobs[i].r, knobs[i].ring, ps, H);
    toggleSprites[0] = buildToggle (rBypass.getCentreX(), 326, false, ps, H);
    toggleSprites[1] = buildToggle (rBypass.getCentreX(), 326, true, ps, H);
    builtPs = ps;
}

void CS1Editor::paint (juce::Graphics& g)
{
    s = std::max (0.25f, std::min (getWidth() / W, getHeight() / H));
    ox = std::floor ((getWidth() - W * s) * 0.5f);
    oy = std::floor ((getHeight() - H * s) * 0.5f);
    g.fillAll (isModern() ? modern::bg() : rgb (12, 12, 13));
    g.addTransform (juce::AffineTransform::scale (s).translated (ox, oy));
    if (isModern()) drawModern (g);
    else
    {
        const float ps = std::round (g.getInternalContext().getPhysicalPixelScaleFactor() * 4.0f) / 4.0f;
        if (std::abs (ps - builtPs) > 0.01f) rebuild (ps);
        drawConsole (g);
    }
}

void CS1Editor::drawScale (juce::Graphics& g, int i, float rOuter, Colour c)
{
    const auto& k = knobs[i];
    auto* p = param (k.id);
    auto f = font (8);
    g.setFont (f);
    for (float sv : { k.s0, k.s1, k.s2 })
    {
        const float ta = a0 + juce::jlimit (0.0f, 1.0f, p->convertTo0to1 (sv)) * 1.5f * pi;
        g.setColour (c.withMultipliedAlpha (0.9f));
        line (g, k.cx + std::sin (ta) * (rOuter + 3), k.cy - std::cos (ta) * (rOuter + 3),
                 k.cx + std::sin (ta) * (rOuter + 7), k.cy - std::cos (ta) * (rOuter + 7), 1.0f);
        const String t = formatScale (i, sv);
        const float tw = f.getStringWidthFloat (t), th = 8.0f;
        const float rx = k.cx + std::sin (ta) * (rOuter + 9 + tw * 0.5f), ry = k.cy - std::cos (ta) * (rOuter + 9 + th * 0.5f);
        g.setColour (c.withMultipliedAlpha (0.75f));
        g.drawText (t, juce::Rectangle<float> (rx - tw * 0.5f - 2, ry - th * 0.5f - 1, tw + 4, th + 2), juce::Justification::centred, false);
    }
}

void CS1Editor::drawKnob (juce::Graphics& g, int i)
{
    const auto& k = knobs[i];
    const float cx = k.cx, cy = k.cy, r = k.r, rc = r * 0.58f;
    const float n = juce::jlimit (0.0f, 1.0f, param (k.id)->getValue());
    const float a = a0 + n * 1.5f * pi, sa = std::sin (a), ca = -std::cos (a);

    knobSprites[(size_t) i].draw (g);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    line (g, cx + sa * r * 0.68f, cy + ca * r * 0.68f, cx + sa * r * 0.95f, cy + ca * r * 0.95f, 3.2f);
    g.setColour (silk());
    line (g, cx + sa * r * 0.68f, cy + ca * r * 0.68f, cx + sa * r * 0.94f, cy + ca * r * 0.94f, 1.8f);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    line (g, cx + sa * rc * 0.18f, cy + ca * rc * 0.18f, cx + sa * rc * 0.86f, cy + ca * rc * 0.86f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    line (g, cx + sa * rc * 0.18f + 1.2f, cy + ca * rc * 0.18f + 1.2f, cx + sa * rc * 0.86f + 1.2f, cy + ca * rc * 0.86f + 1.2f, 0.8f);

    drawScale (g, i, r + 1, silk());
    g.setFont (font (10)); g.setColour (silk());
    ctext (g, k.label, cx, cy - r - 33, 10);
}

void CS1Editor::drawFlatKnob (juce::Graphics& g, int i)
{
    const auto& k = knobs[i];
    const float cx = k.cx, cy = k.cy, r = k.r;
    const float n = juce::jlimit (0.0f, 1.0f, param (k.id)->getValue());
    const float a = a0 + n * 1.5f * pi;
    const bool hv = (drag >= 0 ? drag : hot) == i;
    const Colour c = ringColour (k.ring, k.mod);
    const float w = r > 17 ? 5.0f : 4.0f;
    g.setColour (rgb (46, 51, 62)); arc (g, cx, cy, r - w * 0.5f, a0, -a0, w);
    // bipolare Regler (Gain) füllen von der Mitte aus
    const bool bipolar = k.fmt == fDb && k.s0 < 0 && k.s2 > 0;
    const float from = bipolar ? 0.0f : a0;
    if (std::abs (a - from) > 0.01f) { g.setColour (c); arc (g, cx, cy, r - w * 0.5f, std::min (from, a), std::max (from, a), w); }
    const float ri = r - w - 2;
    g.setColour (rgb (24, 27, 34)); g.fillEllipse (cx - ri, cy - ri, 2 * ri, 2 * ri);
    g.setColour (hv ? modern::dim() : rgb (56, 62, 75)); g.drawEllipse (cx - ri, cy - ri, 2 * ri, 2 * ri, 1.0f);
    g.setColour (c);
    line (g, cx + std::sin (a) * ri * 0.35f, cy - std::cos (a) * ri * 0.35f, cx + std::sin (a) * ri * 0.9f, cy - std::cos (a) * ri * 0.9f, 2.0f);
    g.setFont (font (9)); g.setColour (modern::dim());
    ctext (g, k.label, cx, cy - r - 14, 9);
    g.setFont (font (9)); g.setColour (hv ? juce::Colours::white : modern::text().withAlpha (0.85f));
    ctext (g, formatValue (i, param (k.id)->convertFrom0to1 (n)), cx, cy + r + 3, 9);
}

void CS1Editor::drawLadder (juce::Graphics& g, float x, float y, float h, float db, bool isGr, bool modern)
{
    static const float lv[] = { 3, 0, -1, -3, -6, -9, -12, -18, -24, -30, -36, -48 };
    static const float gr[] = { 1, 2, 3, 4, 6, 8, 10, 12, 15, 18, 21, 24 };
    const int N = 12;
    const float sh = h / N;
    g.setColour (modern ? rgb (15, 17, 21) : rgb (6, 6, 7));
    g.fillRect (x - 2, y - 2, 18.0f, h + 4);
    for (int k = 0; k < N; ++k)
    {
        // Segment k von oben gezählt
        Colour c; bool lit;
        if (isGr) { lit = -db >= gr[k]; c = rgb (255, 150, 50); }
        else
        {
            lit = db >= lv[k];
            c = lv[k] >= 0 ? rgb (255, 70, 60) : (lv[k] >= -6 ? rgb (255, 200, 60) : (modern ? modern::accent() : rgb (80, 230, 110)));
        }
        const float sy = isGr ? y + k * sh : y + k * sh;
        g.setColour (lit ? c : c.withMultipliedBrightness (0.18f).withMultipliedAlpha (modern ? 0.6f : 1.0f));
        g.fillRect (x, sy + 1, 14.0f, sh - 2);
    }
}

void CS1Editor::drawReadout (juce::Graphics& g, juce::Rectangle<float> r, bool modern)
{
    if (! modern)
    {
        g.setColour (rgb (5, 7, 9)); g.fillRect (r);
        g.setColour (rgb (44, 46, 50)); g.drawRect (r, 1.0f);
    }
    const Colour c1 = modern ? modern::dim() : rgb (120, 200, 255);
    const Colour c2 = modern ? modern::text() : rgb (226, 240, 255);
    const float x = r.getX() + 10, y1 = r.getY() + 8, y2 = r.getY() + 27;
    const int h = drag >= 0 ? drag : hot;
    if (h >= 0)
    {
        g.setFont (font (11)); g.setColour (c1);
        String name = String (moduleNames[knobs[h].mod]) + "  " + knobs[h].label;
        if (knobs[h].mod == mEq)
        {
            static const char* const bands[] = { "LF", "LMF", "HMF", "HF" };
            const String band = bands[juce::jlimit (0, 3, juce::roundToInt ((knobs[h].cx - 331) / 95.0f))];
            name = "EQ  " + band + "  " + (String (knobs[h].label) == band ? String ("GAIN") : String (knobs[h].label));
        }
        ltext (g, name, x, y1, 11);
        g.setFont (font (15)); g.setColour (c2);
        ltext (g, formatValue (h, value (knobs[h].id)), x, y2, 15);
        return;
    }
    g.setFont (font (11)); g.setColour (c1);
    String top = proc.autoTrimArmed.load() ? "AUTO TRIM  " + String (juce::roundToInt (proc.autoTrimProgress.load() * 100)) + " %"
                                           : (proc.presetName.isEmpty() ? String ("CS-1") : proc.presetName);
    ltext (g, top, x, y1, 11);
    rtext (g, String ("SLOT ") + (proc.getSlot() == 0 ? "A" : "B"), r.getRight() - 10, y1, 11);
    g.setFont (font (12)); g.setColour (c2);
    auto grs = [] (float v) { return v > -0.05f ? String ("0.0") : String (v, 1); };
    ltext (g, flag ("bypass") ? String ("BYPASS")
                              : "GATE " + grs (gateDisp) + "   COMP " + grs (compDisp) + "   DS " + grs (dsDisp), x, y2 + 2, 12);
}

void CS1Editor::drawRackOrPill (juce::Graphics& g, float x, float y, float w, float h, bool on, const String& label, bool modern)
{
    if (! modern) { drawButton (g, x, y, w, h, on, label); return; }
    const bool hover = juce::Rectangle<float> (x, y, w, h).contains (mouseBase);
    g.setColour (on ? modern::accent() : (hover ? rgb (56, 62, 75) : rgb (40, 45, 55)));
    g.fillRoundedRectangle (x, y, w, h, h * 0.5f);
    g.setColour (on ? modern::bg() : modern::dim());
    float fh = 9.0f;
    while (fh > 6.5f && font (fh).getStringWidthFloat (label) > w - 6) fh -= 0.5f;
    g.setFont (font (fh));
    g.drawText (label, juce::Rectangle<float> (x, y, w, h), juce::Justification::centred, false);
}

// ---------------------------------------------------------------- Konsole
void CS1Editor::drawConsole (juce::Graphics& g)
{
    drawFaceplate (g, W, H);
    for (int c = 1; c < (int) std::size (columns); ++c) drawSeam (g, columns[c].x0, 22, 258);
    g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillRect (14.0f, 286.0f, W - 28, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRect (14.0f, 287.0f, W - 28, 1.0f);

    // Modultitel; schaltbare Module haben eine LED
    for (int c = 0; c < (int) std::size (columns); ++c)
    {
        const auto& col = columns[c];
        const float cx = (col.x0 + col.x1) * 0.5f;
        const bool en = col.onId == nullptr || flag (col.onId);
        g.setFont (font (11));
        const float tw = font (11).getStringWidthFloat (col.title);
        if (col.onId != nullptr)
        {
            const float lx = cx - tw * 0.5f - 10;
            g.setColour (en ? rgb (255, 170, 60) : rgb (60, 40, 20));
            g.fillEllipse (lx - 3.5f, 22.5f, 7, 7);
            if (en) { g.setColour (rgb (255, 170, 60, 0.25f)); g.fillEllipse (lx - 7, 19, 14, 14); }
        }
        g.setColour (silk (en ? 1.0f : 0.45f));
        ctext (g, col.title, cx, 20, 11);
    }

    for (int i = 0; i < NK; ++i) drawKnob (g, i);

    drawButton (g, rAutoTrim.getX(), rAutoTrim.getY(), rAutoTrim.getWidth(), rAutoTrim.getHeight(), proc.autoTrimArmed.load(), "AUTO TRIM");
    drawButton (g, rLfBell.getX(), rLfBell.getY(), rLfBell.getWidth(), rLfBell.getHeight(), flag ("lfBell"), "BELL");
    drawButton (g, rHfBell.getX(), rHfBell.getY(), rHfBell.getWidth(), rHfBell.getHeight(), flag ("hfBell"), "BELL");
    drawButton (g, rPreEq.getX(), rPreEq.getY(), rPreEq.getWidth(), rPreEq.getHeight(), flag ("compPre"), "PRE EQ");
    drawButton (g, rAutoGain.getX(), rAutoGain.getY(), rAutoGain.getWidth(), rAutoGain.getHeight(), flag ("autoGain"), "AUTO GAIN");

    // Meter
    drawLadder (g, 987, 128, 108, inDisp, false, false);
    drawLadder (g, 1013, 128, 108, compDisp, true, false);
    drawLadder (g, 1039, 128, 108, outDisp, false, false);
    g.setFont (font (8)); g.setColour (silk (0.9f));
    ctext (g, "IN", 994, 240, 8); ctext (g, "GR", 1020, 240, 8); ctext (g, "OUT", 1046, 240, 8);

    // abgeschaltete Module abdunkeln
    for (const auto& col : columns)
        if (col.onId != nullptr && ! flag (col.onId))
        {
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillRect (col.x0 + 2, 36.0f, col.x1 - col.x0 - 3, 246.0f);
        }

    // --- Workflow-Leiste ---
    drawReadout (g, rOled, false);
    for (int i = 0; i < CS1Processor::numPresets; ++i)
    {
        auto r = presetRect (i);
        drawButton (g, r.getX(), r.getY(), r.getWidth(), r.getHeight(), proc.presetName == CS1Processor::presetNames[i], CS1Processor::presetNames[i]);
    }
    drawButton (g, rA.getX(), rA.getY(), rA.getWidth(), rA.getHeight(), proc.getSlot() == 0, "A");
    drawButton (g, rB.getX(), rB.getY(), rB.getWidth(), rB.getHeight(), proc.getSlot() == 1, "B");
    drawButton (g, rView.getX(), rView.getY(), rView.getWidth(), rView.getHeight(), false, "MODERN");

    g.setFont (font (22)); g.setColour (silk()); ltext (g, "CS-1", 944, 304, 22);
    g.setFont (font (8)); g.setColour (silk (0.75f)); ltext (g, "ONNO CHANNEL", 944, 332, 8);

    g.setFont (font (8)); g.setColour (silk (0.95f));
    ctext (g, "ON", rBypass.getCentreX(), 292, 8); ctext (g, "BYPASS", rBypass.getCentreX(), 352, 8);
    toggleSprites[flag ("bypass") ? 1 : 0].draw (g);
}

// ---------------------------------------------------------------- Modern
void CS1Editor::drawModern (juce::Graphics& g)
{
    for (int c = 0; c < (int) std::size (columns); ++c)
    {
        const auto& col = columns[c];
        const bool en = col.onId == nullptr || flag (col.onId);
        modern::panelRect (g, col.x0 + 3, 12, col.x1 - col.x0 - 6, 270);
        auto tr = titleRect (c);
        const Colour mc = modColour (c);
        if (col.onId != nullptr)
        {
            const float pw = font (10).getStringWidthFloat (col.title) + 22;
            auto pr = juce::Rectangle<float> (tr.getCentreX() - pw * 0.5f, 17, pw, 16);
            g.setColour (en ? mc : rgb (46, 51, 62)); g.fillRoundedRectangle (pr, 8);
            g.setColour (en ? modern::bg() : modern::dim()); g.setFont (font (10));
            g.drawText (col.title, pr, juce::Justification::centred, false);
        }
        else
        {
            g.setColour (mc); g.setFont (font (10));
            g.drawText (col.title, juce::Rectangle<float> (tr.getX(), 17, tr.getWidth(), 16), juce::Justification::centred, false);
        }
    }

    for (int i = 0; i < NK; ++i) drawFlatKnob (g, i);

    drawRackOrPill (g, rAutoTrim.getX(), rAutoTrim.getY(), rAutoTrim.getWidth(), rAutoTrim.getHeight(), proc.autoTrimArmed.load(), "AUTO TRIM", true);
    drawRackOrPill (g, rLfBell.getX(), rLfBell.getY(), rLfBell.getWidth(), rLfBell.getHeight(), flag ("lfBell"), "BELL", true);
    drawRackOrPill (g, rHfBell.getX(), rHfBell.getY(), rHfBell.getWidth(), rHfBell.getHeight(), flag ("hfBell"), "BELL", true);
    drawRackOrPill (g, rPreEq.getX(), rPreEq.getY(), rPreEq.getWidth(), rPreEq.getHeight(), flag ("compPre"), "PRE EQ", true);
    drawRackOrPill (g, rAutoGain.getX(), rAutoGain.getY(), rAutoGain.getWidth(), rAutoGain.getHeight(), flag ("autoGain"), "AUTO GAIN", true);

    drawLadder (g, 987, 128, 108, inDisp, false, true);
    drawLadder (g, 1013, 128, 108, compDisp, true, true);
    drawLadder (g, 1039, 128, 108, outDisp, false, true);
    g.setFont (font (9)); g.setColour (modern::dim());
    ctext (g, "IN", 994, 240, 9); ctext (g, "GR", 1020, 240, 9); ctext (g, "OUT", 1046, 240, 9);

    for (const auto& col : columns)
        if (col.onId != nullptr && ! flag (col.onId))
        {
            g.setColour (modern::panel().withAlpha (0.6f));
            g.fillRect (col.x0 + 4, 38.0f, col.x1 - col.x0 - 8, 240.0f);
        }

    // --- Workflow-Leiste ---
    modern::panelRect (g, 10, 292, W - 20, 76);
    drawReadout (g, rOled, true);
    g.setFont (font (9)); g.setColour (modern::dim());
    ltext (g, "QUELLE", presetX0, 306, 9);
    ltext (g, "VERGLEICH", rA.getX(), 306, 9);
    for (int i = 0; i < CS1Processor::numPresets; ++i)
    {
        auto r = presetRect (i);
        drawRackOrPill (g, r.getX(), r.getY(), r.getWidth(), r.getHeight(), proc.presetName == CS1Processor::presetNames[i], CS1Processor::presetNames[i], true);
    }
    drawRackOrPill (g, rA.getX(), rA.getY(), rA.getWidth(), rA.getHeight(), proc.getSlot() == 0, "A", true);
    drawRackOrPill (g, rB.getX(), rB.getY(), rB.getWidth(), rB.getHeight(), proc.getSlot() == 1, "B", true);
    drawRackOrPill (g, rView.getX(), rView.getY(), rView.getWidth(), rView.getHeight(), false, "KONSOLE", true);

    g.setFont (font (18)); g.setColour (modern::text()); ltext (g, "ONNO", 944, 304, 18);
    g.setColour (modern::accent()); ltext (g, "CS-1", 944, 324, 18);

    const bool byp = flag ("bypass");
    auto br = juce::Rectangle<float> (rBypass.getX(), 316, rBypass.getWidth() + 12, 22).translated (-6, 0);
    g.setColour (byp ? modern::warm() : (br.contains (mouseBase) ? rgb (56, 62, 75) : rgb (40, 45, 55)));
    g.fillRoundedRectangle (br, 11);
    g.setColour (byp ? modern::bg() : modern::dim()); g.setFont (font (10));
    g.drawText ("BYPASS", br, juce::Justification::centred, false);
}
