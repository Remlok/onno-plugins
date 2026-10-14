#include "RV1Editor.h"

using namespace onno;
using juce::String;

namespace
{
enum Fmt { fMs, fSec, fPct, fChar, fHz, fWidth, fMix };

struct KnobDef { const char* id; const char* label; float cx, cy, r; int ring; Fmt fmt; float s0, s1, s2; };

// Lage im 1000x340-Raster
const KnobDef knobs[] = {
    { "predelay", "PRE-DELAY", 407, 100, 26, ringNone, fMs, 0, 60, 250 },
    { "decay", "DECAY", 532, 100, 26, ringRed, fSec, 0.2f, 2, 20 },
    { "size", "SIZE", 657, 100, 26, ringNone, fPct, 0, 50, 100 },
    { "depth", "DEPTH", 782, 100, 26, ringNone, fPct, 0, 50, 100 },
    { "movement", "MOVEMENT", 407, 215, 26, ringBlue, fPct, 0, 50, 100 },
    { "character", "CHARACTER", 532, 215, 26, ringNone, fChar, 0, 50, 100 },
    { "width", "WIDTH", 657, 215, 26, ringNone, fWidth, 0, 100, 150 },
    { "duck", "DUCK", 782, 215, 26, ringAmber, fPct, 0, 50, 100 },
    { "mix", "MIX", 916, 100, 26, ringGreen, fMix, 0, 50, 100 },
    { "lowCut", "LOW CUT", 881, 205, 16, ringNone, fHz, 20, 140, 1000 },
    { "highCut", "HIGH CUT", 951, 205, 16, ringNone, fHz, 1000, 4500, 20000 },
};
constexpr int NK = (int) std::size (knobs);

const juce::Rectangle<float> rOled { 24, 30, 310, 166 }, rPrev { 24, 222, 40, 16 }, rNext { 24, 266, 40, 16 },
                             rSend { 430, 290, 80, 14 }, rFreeze { 560, 290, 70, 14 }, rAlgo { 680, 290, 80, 14 },
                             rView { 862, 290, 50, 14 }, rBypass { 926, 258, 48, 48 };

juce::Rectangle<float> catRect (int c) { return { 74.0f + (float) (c % 4) * 66.0f, c < 4 ? 222.0f : 266.0f, 60.0f, 16.0f }; }

constexpr float pi = juce::MathConstants<float>::pi;
constexpr float a0 = -0.75f * pi;

float a2db (float x) { return 8.685889638f * std::log (std::max (x, 1.0e-9f)); }

Colour knobColour (int ring)
{
    switch (ring)
    {
        case ringRed:   return rgb (255, 96, 90);
        case ringBlue:  return rgb (96, 146, 255);
        case ringAmber: return rgb (255, 176, 72);
        case ringGreen: return rgb (96, 214, 136);
        default:        return modern::accent();
    }
}
}

RV1Editor::RV1Editor (RV1Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setResizable (true, true);
    setResizeLimits (500, 170, 2000, 680);
    getConstrainer()->setFixedAspectRatio (W / H);
    setSize ((int) W, (int) H);
    knobSprites.resize ((size_t) NK);
    startTimerHz (30);
}

int RV1Editor::hitKnob (juce::Point<float> b) const
{
    for (int i = 0; i < NK; ++i)
    {
        const float dx = b.x - knobs[i].cx, dy = b.y - knobs[i].cy, rr = knobs[i].r + 8;
        if (dx * dx + dy * dy < rr * rr) return i;
    }
    return -1;
}

// ------------------------------------------------------------------- Maus
void RV1Editor::mouseMove (const juce::MouseEvent& e)
{
    mouseBase = toBase (e.position);
    hot = hitKnob (mouseBase);
    repaint();
}

void RV1Editor::mouseExit (const juce::MouseEvent&)
{
    mouseBase = { -1000, -1000 };
    if (drag < 0) hot = -1;
    repaint();
}

void RV1Editor::mouseDown (const juce::MouseEvent& e)
{
    const auto b = toBase (e.position);
    const int cur = proc.currentProgram();
    for (int c = 0; c < RV1Processor::numCategories; ++c)
        if (catRect (c).contains (b)) { proc.loadProgram (RV1Processor::firstOfCategory (c)); repaint(); return; }
    if (rPrev.contains (b)) { proc.loadProgram (cur - 1); repaint(); return; }
    if (rNext.contains (b) || rOled.contains (b)) { proc.loadProgram (cur + 1); repaint(); return; }
    if (rSend.contains (b)) { toggle ("send"); return; }
    if (rFreeze.contains (b)) { toggle ("freeze"); return; }
    if (rAlgo.contains (b)) { proc.setParam ("algo", (float) (((int) value ("algo") + 1) % RV1Processor::numAlgos)); repaint(); return; }
    if (rView.contains (b)) { toggle ("view"); return; }
    if (rBypass.contains (b) || (isModern() && juce::Rectangle<float> (rBypass.getX() - 6, 287, rBypass.getWidth() + 12, 20).contains (b))) { toggle ("bypass"); return; }

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

void RV1Editor::mouseDrag (const juce::MouseEvent& e)
{
    const auto b = toBase (e.position);
    mouseBase = b;
    if (drag < 0) return;
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + (lastY - b.y) / (e.mods.isShiftDown() ? 900.0f : 220.0f));
    lastY = b.y;
    param (knobs[drag].id)->setValueNotifyingHost (dragNorm);
    repaint();
}

void RV1Editor::mouseUp (const juce::MouseEvent& e)
{
    if (drag >= 0) param (knobs[drag].id)->endChangeGesture();
    drag = -1;
    hot = hitKnob (toBase (e.position));
    repaint();
}

void RV1Editor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int k = hitKnob (toBase (e.position));
    if (k < 0) return;
    auto* p = param (knobs[k].id);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getDefaultValue());
    p->endChangeGesture();
    repaint();
}

void RV1Editor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto b = toBase (e.position);
    // Mausrad über dem Display blättert durch die Programme
    if (rOled.contains (b))
    {
        proc.loadProgram (proc.currentProgram() + (w.deltaY * (w.isReversed ? -1.0f : 1.0f) > 0 ? -1 : 1));
        repaint();
        return;
    }
    const int k = hitKnob (b);
    if (k < 0) return;
    auto* p = param (knobs[k].id);
    const float dir = w.isReversed ? -1.0f : 1.0f;
    p->beginChangeGesture();
    p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + dir * w.deltaY * (e.mods.isShiftDown() ? 0.035f : 0.17f)));
    p->endChangeGesture();
    repaint();
}

// ---------------------------------------------------------------- Anzeige
void RV1Editor::timerCallback()
{
    auto fall = [] (float& disp, float v) { disp = v > disp ? v : std::max (-90.0f, disp - 1.2f); };
    fall (inDisp, a2db (proc.inPeak.exchange (0.0f)));
    fall (wetDisp, a2db (proc.wetPeak.exchange (0.0f)));
    const float d = proc.duckDb.load();
    duckDisp = d < duckDisp ? d : std::min (0.0f, duckDisp + 0.6f);
    repaint();
}

String RV1Editor::formatValue (int i, float v) const
{
    const String id = knobs[i].id;
    if (id == "lowCut" && v <= 20.5f) return "OFF";
    if (id == "highCut" && v >= 19500.0f) return "OFF";
    if (id == "decay" && (int) value ("algo") == RV1Processor::gated)
        return "GATE " + String (juce::roundToInt (juce::jlimit (0.06f, 1.2f, v * 0.25f) * 1000)) + " ms";
    switch (knobs[i].fmt)
    {
        case fMs:    return String (juce::roundToInt (v)) + " ms";
        case fSec:   return String (v, v < 10 ? 2 : 1) + " s";
        case fPct: case fWidth: return String (juce::roundToInt (v)) + " %";
        case fMix:   return String (juce::roundToInt (v)) + " % WET";
        case fChar:  return v < 45 ? "DARK " + String (juce::roundToInt (50 - v)) : (v > 55 ? "BRIGHT " + String (juce::roundToInt (v - 50)) : String ("NEUTRAL"));
        case fHz:    return v >= 1000 ? String (v / 1000.0f, 2) + " kHz" : String (juce::roundToInt (v)) + " Hz";
    }
    return {};
}

String RV1Editor::formatScale (int i, float v) const
{
    const String id = knobs[i].id;
    if ((id == "lowCut" && v <= 20.5f) || (id == "highCut" && v >= 19500.0f)) return "OFF";
    switch (knobs[i].fmt)
    {
        case fChar: return v == 0 ? "DARK" : (v == 100 ? "BRIGHT" : "");
        case fMix:  return v == 0 ? "DRY" : (v == 100 ? "WET" : "50");
        case fSec:  return v < 1 ? "." + String (juce::roundToInt (v * 10)) : String (juce::roundToInt (v)) + "s";
        case fHz:   return v >= 1000 ? String (v / 1000.0f, v < 10000 && v != std::floor (v / 1000) * 1000 ? 1 : 0) + "k" : String (juce::roundToInt (v));
        default:    return String (juce::roundToInt (v));
    }
}

void RV1Editor::rebuild (float ps)
{
    for (int i = 0; i < NK; ++i)
        knobSprites[(size_t) i] = buildKnob (knobs[i].cx, knobs[i].cy, knobs[i].r, knobs[i].ring, ps, H);
    toggleSprites[0] = buildToggle (rBypass.getCentreX(), rBypass.getCentreY(), false, ps, H);
    toggleSprites[1] = buildToggle (rBypass.getCentreX(), rBypass.getCentreY(), true, ps, H);
    builtPs = ps;
}

void RV1Editor::paint (juce::Graphics& g)
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

void RV1Editor::drawScale (juce::Graphics& g, int i, float rOuter, Colour c)
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
        if (t.isEmpty()) continue;
        const float tw = f.getStringWidthFloat (t), th = 8.0f;
        const float rx = k.cx + std::sin (ta) * (rOuter + 9 + tw * 0.5f), ry = k.cy - std::cos (ta) * (rOuter + 9 + th * 0.5f);
        g.setColour (c.withMultipliedAlpha (0.75f));
        g.drawText (t, juce::Rectangle<float> (rx - tw * 0.5f - 2, ry - th * 0.5f - 1, tw + 4, th + 2), juce::Justification::centred, false);
    }
}

void RV1Editor::drawKnob (juce::Graphics& g, int i)
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
    g.setFont (font (r > 20 ? 11.0f : 9.0f)); g.setColour (silk());
    ctext (g, k.label, cx, cy - r - (r > 20 ? 33.0f : 28.0f), 11);
}

void RV1Editor::drawFlatKnob (juce::Graphics& g, int i)
{
    const auto& k = knobs[i];
    const float cx = k.cx, cy = k.cy, r = k.r;
    const float n = juce::jlimit (0.0f, 1.0f, param (k.id)->getValue());
    const float a = a0 + n * 1.5f * pi;
    const bool hv = (drag >= 0 ? drag : hot) == i;
    const Colour c = knobColour (k.ring);
    const float w = r > 20 ? 6.0f : 4.0f;
    g.setColour (rgb (46, 51, 62)); arc (g, cx, cy, r - w * 0.5f, a0, -a0, w);
    if (n > 0.002f) { g.setColour (c); arc (g, cx, cy, r - w * 0.5f, a0, a, w); }
    const float ri = r - w - 2;
    g.setColour (rgb (24, 27, 34)); g.fillEllipse (cx - ri, cy - ri, 2 * ri, 2 * ri);
    g.setColour (hv ? modern::dim() : rgb (56, 62, 75)); g.drawEllipse (cx - ri, cy - ri, 2 * ri, 2 * ri, 1.0f);
    g.setColour (c);
    g.fillEllipse (cx + std::sin (a) * ri * 0.65f - 2.5f, cy - std::cos (a) * ri * 0.65f - 2.5f, 5, 5);
    g.setFont (font (r > 20 ? 10.0f : 9.0f)); g.setColour (modern::dim());
    ctext (g, k.label, cx, cy - r - 15, 10);
    g.setFont (font (r > 20 ? 11.0f : 9.0f)); g.setColour (hv ? juce::Colours::white : modern::text().withAlpha (0.9f));
    ctext (g, formatValue (i, param (k.id)->convertFrom0to1 (n)), cx, cy + r + 4, 11);
}

void RV1Editor::drawBtn (juce::Graphics& g, juce::Rectangle<float> r, bool on, const String& label, bool modern)
{
    if (! modern) { drawButton (g, r.getX(), r.getY(), r.getWidth(), r.getHeight(), on, label); return; }
    const bool hover = r.contains (mouseBase);
    g.setColour (on ? modern::accent() : (hover ? rgb (56, 62, 75) : rgb (40, 45, 55)));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? modern::bg() : modern::dim());
    float fh = 9.0f;
    while (fh > 6.5f && font (fh).getStringWidthFloat (label) > r.getWidth() - 6) fh -= 0.5f;
    g.setFont (font (fh));
    g.drawText (label, r, juce::Justification::centred, false);
}

// Programm-Display: Kategorie, Name, Algorithmus und die Hüllkurve des Halls
void RV1Editor::drawDisplay (juce::Graphics& g, juce::Rectangle<float> r, bool modern)
{
    const auto& progs = RV1Processor::programs();
    const int pi_ = juce::jlimit (0, (int) progs.size() - 1, proc.currentProgram());
    const auto& pr = progs[(size_t) pi_];
    const int algo = (int) value ("algo");

    if (! modern)
    {
        g.setColour (rgb (5, 7, 9)); g.fillRect (r);
        g.setColour (rgb (44, 46, 50)); g.drawRect (r, 1.0f);
    }
    const Colour cHi = modern ? modern::accent() : rgb (120, 200, 255);
    const Colour cTx = modern ? modern::text() : rgb (226, 240, 255);
    const Colour cDim = modern ? modern::dim() : rgb (110, 130, 150);
    const float x = r.getX() + 10, rr = r.getRight() - 10;

    g.setFont (font (10)); g.setColour (cHi);
    ltext (g, RV1Processor::categoryNames[pr.category], x, r.getY() + 8, 10);
    g.setColour (cDim);
    rtext (g, String (pi_ + 1).paddedLeft ('0', 2) + " / " + String ((int) progs.size()), rr, r.getY() + 8, 10);
    g.setFont (font (20)); g.setColour (cTx);
    ltext (g, pr.name, x, r.getY() + 21, 20);

    g.setFont (font (10));
    const int h = drag >= 0 ? drag : hot;
    if (h >= 0)
    {
        g.setColour (cHi);
        ltext (g, String (knobs[h].label) + "  " + formatValue (h, value (knobs[h].id)), x, r.getY() + 48, 10);
    }
    else
    {
        g.setColour (cDim);
        String info = String (RV1Processor::algoNames[algo]);
        if (flag ("freeze")) info << "  FREEZE";
        if (flag ("send")) info << "  100% WET";
        ltext (g, info, x, r.getY() + 48, 10);
    }
    g.setColour (cDim);
    String live = flag ("bypass") ? String ("BYPASS")
                                  : "IN " + (inDisp <= -60 ? String ("-inf") : String (juce::roundToInt (inDisp)))
                                    + "  WET " + (wetDisp <= -60 ? String ("-inf") : String (juce::roundToInt (wetDisp)));
    if (value ("duck") > 0.5f) live << "  DUCK " << String (duckDisp, 0);
    rtext (g, live, rr, r.getY() + 48, 10);

    // Hüllkurve: Pegel über der Zeit (0 bis -60 dB)
    const auto gr = juce::Rectangle<float> (x, r.getY() + 66, r.getWidth() - 20, r.getHeight() - 76);
    g.setColour (modern ? rgb (15, 17, 21) : rgb (10, 13, 17)); g.fillRect (gr);
    const bool gatedAlgo = algo == RV1Processor::gated;
    const float pd = value ("predelay") * 0.001f, dec = value ("decay");
    const float gateT = juce::jlimit (0.06f, 1.2f, dec * 0.25f);
    const float T = gatedAlgo ? std::max (0.5f, pd + gateT * 1.6f) : juce::jlimit (0.5f, 12.0f, pd + dec * 1.1f);
    auto tx = [&] (float t) { return gr.getX() + t / T * gr.getWidth(); };
    auto dy = [&] (float db) { return gr.getY() + juce::jlimit (0.0f, 1.0f, -db / 60.0f) * gr.getHeight(); };

    g.setColour (modern ? rgb (38, 43, 52) : rgb (24, 34, 44));
    const float step = T > 6 ? 2.0f : (T > 2.5f ? 1.0f : (T > 1 ? 0.5f : 0.1f));
    g.setFont (font (8, false));
    for (float t = step; t < T; t += step)
    {
        g.setColour (modern ? rgb (38, 43, 52) : rgb (24, 34, 44));
        g.fillRect (tx (t), gr.getY(), 1.0f, gr.getHeight());
        g.setColour (cDim.withAlpha (0.7f));
        ltext (g, (step < 1 ? String (t, 1) : String (t, 0)) + "s", tx (t) + 2, gr.getBottom() - 11, 8);
    }
    for (int d = 1; d < 3; ++d)
    {
        g.setColour (modern ? rgb (38, 43, 52) : rgb (24, 34, 44));
        g.fillRect (gr.getX(), dy (-20.0f * d), gr.getWidth(), 1.0f);
    }

    juce::Path env;
    env.startNewSubPath (tx (0), gr.getBottom());
    env.lineTo (tx (pd), gr.getBottom());
    if (gatedAlgo)
    {
        env.lineTo (tx (pd), dy (-4));
        env.lineTo (tx (pd + gateT), dy (-8));
        env.lineTo (tx (pd + gateT + 0.015f), gr.getBottom());
    }
    else
    {
        const float sizeMul = 0.5f + value ("size") * 0.01f;
        const float rise = 0.02f + 0.06f * sizeMul;
        env.lineTo (tx (pd + rise), dy (-2));
        const int steps = 60;
        for (int k = 1; k <= steps; ++k)
        {
            const float t = pd + rise + (T - pd - rise) * k / steps;
            env.lineTo (tx (t), dy (-2 - 60.0f * (t - pd - rise) / dec));
        }
    }
    env.lineTo (gr.getRight(), gr.getBottom());
    env.closeSubPath();
    g.setColour (cHi.withAlpha (0.16f)); g.fillPath (env);
    g.setColour (cHi); g.strokePath (env, juce::PathStrokeType (1.4f));

    // frühe Reflexionen als Striche
    if (! gatedAlgo && algo != RV1Processor::plate)
    {
        static const float er[8] = { 4.3f, 9.7f, 14.1f, 21.3f, 28.9f, 37.4f, 47.2f, 58.6f };
        const float sizeMul = 0.5f + value ("size") * 0.01f;
        const float erScale = algo == RV1Processor::room ? 0.45f : (algo == RV1Processor::ambience ? 0.35f : 0.8f);
        const float depth = value ("depth") * 0.01f;
        g.setColour (cTx.withAlpha (0.8f));
        for (int k = 0; k < 8; ++k)
        {
            const float t = pd + er[k] * 0.001f * erScale * sizeMul;
            g.fillRect (tx (t), dy (-6.0f - 3.0f * k - 25.0f * depth), 1.2f, gr.getBottom() - dy (-6.0f - 3.0f * k - 25.0f * depth));
        }
    }
    if (flag ("freeze"))
    {
        g.setColour (cHi.withAlpha (0.5f));
        g.fillRect (gr.getX(), dy (-2), gr.getWidth(), 1.5f);
    }
}

// ---------------------------------------------------------------- Konsole
void RV1Editor::drawConsole (juce::Graphics& g)
{
    drawFaceplate (g, W, H);
    drawSeam (g, 344, 22, 296);
    drawSeam (g, 846, 22, 296);

    drawDisplay (g, rOled, false);
    const int cat = RV1Processor::programs()[(size_t) juce::jlimit (0, (int) RV1Processor::programs().size() - 1, proc.currentProgram())].category;
    for (int c = 0; c < RV1Processor::numCategories; ++c)
        drawBtn (g, catRect (c), c == cat, RV1Processor::categoryNames[c], false);
    drawBtn (g, rPrev, false, "PREV", false);
    drawBtn (g, rNext, false, "NEXT", false);
    g.setFont (font (22)); g.setColour (silk()); ltext (g, "RV-1", 24, 290, 22);
    g.setFont (font (11)); g.setColour (silk (0.95f)); ltext (g, "ONNO", 88, 292, 11);
    g.setFont (font (8)); g.setColour (silk (0.75f)); ltext (g, "ALGORITHMIC REVERB", 88, 306, 8);

    for (int i = 0; i < NK; ++i) drawKnob (g, i);

    drawBtn (g, rSend, flag ("send"), "100% WET", false);
    drawBtn (g, rFreeze, flag ("freeze"), "FREEZE", false);
    drawBtn (g, rAlgo, false, "ALGORITHM", false);
    drawBtn (g, rView, false, "MODERN", false);

    g.setFont (font (8)); g.setColour (silk (0.95f));
    ctext (g, "ON", rBypass.getCentreX(), rBypass.getY() - 12, 8);
    ctext (g, "BYPASS", rBypass.getCentreX(), rBypass.getBottom() + 2, 8);
    toggleSprites[flag ("bypass") ? 1 : 0].draw (g);
}

// ---------------------------------------------------------------- Modern
void RV1Editor::drawModern (juce::Graphics& g)
{
    modern::panelRect (g, 12, 12, 328, 316);
    modern::panelRect (g, 350, 12, 492, 316);
    modern::panelRect (g, 852, 12, 136, 316);

    drawDisplay (g, rOled, true);
    const int cat = RV1Processor::programs()[(size_t) juce::jlimit (0, (int) RV1Processor::programs().size() - 1, proc.currentProgram())].category;
    for (int c = 0; c < RV1Processor::numCategories; ++c)
        drawBtn (g, catRect (c), c == cat, RV1Processor::categoryNames[c], true);
    drawBtn (g, rPrev, false, "<", true);
    drawBtn (g, rNext, false, ">", true);
    g.setFont (font (18));
    g.setColour (modern::text()); ltext (g, "ONNO", 24, 292, 18);
    g.setColour (modern::accent()); ltext (g, "RV-1", 24 + font (18).getStringWidthFloat ("ONNO "), 292, 18);

    for (int i = 0; i < NK; ++i) drawFlatKnob (g, i);

    drawBtn (g, rSend, flag ("send"), "100% WET", true);
    drawBtn (g, rFreeze, flag ("freeze"), "FREEZE", true);
    drawBtn (g, rAlgo, false, RV1Processor::algoNames[(int) value ("algo")], true);
    drawBtn (g, rView, false, "KONSOLE", true);

    const bool byp = flag ("bypass");
    auto br = juce::Rectangle<float> (rBypass.getX() - 6, 287, rBypass.getWidth() + 12, 20);
    g.setColour (byp ? modern::warm() : (br.contains (mouseBase) ? rgb (56, 62, 75) : rgb (40, 45, 55)));
    g.fillRoundedRectangle (br, 11);
    g.setColour (byp ? modern::bg() : modern::dim()); g.setFont (font (10));
    g.drawText ("BYPASS", br, juce::Justification::centred, false);
}
