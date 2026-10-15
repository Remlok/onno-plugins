#include "RV1Editor.h"

using namespace onno;
using juce::String;

namespace
{
enum Fmt { fMs, fSec, fPct, fChar, fHz, fWidth, fMix };

struct KnobDef { const char* id; const char* label; float cx, cy, r; int ring; Fmt fmt; float s0, s1, s2; };

// Lage im 1000x340-Raster
const KnobDef knobDefs[] = {
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
constexpr int NK = (int) std::size (knobDefs);

// Raster des Geräts (ohne Kopfzeile)
const juce::Rectangle<float> rOled { 24, 30, 310, 166 }, rPrev { 24, 222, 40, 16 }, rNext { 24, 266, 40, 16 },
                             rSend { 430, 290, 80, 14 }, rFreeze { 560, 290, 70, 14 }, rAlgo { 680, 290, 80, 14 },
                             rBypass { 926, 258, 48, 48 };

const std::map<String, const char*> knobHelp = {
    { "predelay", "Zeit bis der Hall einsetzt. Mehr Pre-Delay trennt den Hall vom Anschlag, der Klang bleibt vorne." },
    { "decay", "Nachhallzeit (RT60). Bei GATED-Programmen die Länge des Gates." },
    { "size", "Raumgröße: Abstände der Reflexionen und Dichte des Halls." },
    { "depth", "Tiefe: mehr = entfernter, dunkler, weniger frühe Reflexionen." },
    { "movement", "Modulation im Hall: macht ihn breiter und lebendiger, viel davon klingt nach Chorus." },
    { "character", "Klangfarbe: links dunkel, rechts hell." },
    { "width", "Stereobreite des Halls. 100 % = natürlich, darüber breiter." },
    { "duck", "Hall wird leiser, solange das Signal spielt, und blüht in den Pausen auf. Hält Gitarren und Vocals klar." },
    { "mix", "Anteil des Halls. Bei 100% WET (für Send-Spuren) zählt nur der Hall." },
    { "lowCut", "Entfernt Bässe aus dem Hall, damit es unten nicht matscht. Ganz links = aus." },
    { "highCut", "Entfernt Höhen aus dem Hall, damit er nicht zischt. Ganz rechts = aus." },
};

juce::Rectangle<float> catRect (int c) { return { 74.0f + (float) (c % 4) * 66.0f, c < 4 ? 222.0f : 266.0f, 60.0f, 16.0f }; }
juce::Rectangle<float> down (juce::Rectangle<float> r) { return r.translated (0, RV1Editor::top); }

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

RV1Editor::RV1Editor (RV1Processor& p) : ModernEditor (p, p.apvts, "RV-1", W0, H0), proc (p)
{
    knobSprites.resize ((size_t) NK);
    enableViewSwitch();
    addHelp ({ { "100% WET", "Nur Hall, kein Originalsignal: für Send-/Effekt-Spuren." },
               { "FREEZE", "Hält den Hall unendlich stehen, neues Signal kommt nicht mehr hinein." },
               { "ALGORITHM", "Wechselt den Hall-Algorithmus (Hall, Plate, Room, Chamber, Ambience, Chorus, Gated)." } });
    syncView();
}

// ------------------------------------------------------------- Ansichten
void RV1Editor::syncView()
{
    const int v = value ("view") > 0.5f ? 1 : 0;
    if (v == builtView) return;
    builtView = v;
    clearControls();
    if (v == 1) buildModern(); else buildConsole();
}

int RV1Editor::category() const
{
    const int fp = proc.factoryProgram();
    return fp >= 0 ? RV1Processor::programs()[(size_t) fp].category : -1;
}

void RV1Editor::stepProgram (int dir)
{
    const int fp = proc.factoryProgram();
    proc.loadProgram ((fp >= 0 ? fp : proc.currentProgram()) + dir);
}

void RV1Editor::addBrowserButtons (bool modern)
{
    auto add = [&] (juce::Rectangle<float> r, std::function<String()> label, std::function<bool()> on, std::function<void()> click, const String& help = {})
    {
        MButton b { down (r), label, on, click, modern::accent() };
        if (! modern) b.paint = [label] (Graphics& g, juce::Rectangle<float> rr, bool o, bool) { drawButton (g, rr.getX(), rr.getY(), rr.getWidth(), rr.getHeight(), o, label()); };
        b.help = help;
        buttons.push_back (b);
    };
    for (int c = 0; c < RV1Processor::numCategories; ++c)
        add (catRect (c), [c] { return String (RV1Processor::categoryNames[c]); }, [this, c] { return category() == c; },
             [this, c] { proc.loadProgram (RV1Processor::firstOfCategory (c)); },
             String::fromUTF8 ("Springt zum ersten Programm der Kategorie ") + RV1Processor::categoryNames[c] + ".");
    add (rPrev, [modern] { return String (modern ? "<" : "PREV"); }, [] { return false; }, [this] { stepProgram (-1); }, String::fromUTF8 ("Vorheriges Programm."));
    add (rNext, [modern] { return String (modern ? ">" : "NEXT"); }, [] { return false; }, [this] { stepProgram (1); }, String::fromUTF8 ("Nächstes Programm."));
    add (rSend, [] { return String ("100% WET"); }, [this] { return flag ("send"); }, [this] { toggle ("send"); });
    add (rFreeze, [] { return String ("FREEZE"); }, [this] { return flag ("freeze"); }, [this] { toggle ("freeze"); });
    add (rAlgo, [this, modern] { return modern ? "ALGO: " + String (RV1Processor::algoNames[juce::jlimit (0, RV1Processor::numAlgos - 1, (int) value ("algo"))]) : String ("ALGORITHM"); },
         [] { return false; }, [this] { setValue ("algo", (float) (((int) value ("algo") + 1) % RV1Processor::numAlgos)); },
         String::fromUTF8 (help.at ("ALGORITHM").toRawUTF8()));
}

void RV1Editor::buildConsole()
{
    for (int i = 0; i < NK; ++i)
    {
        MKnob k { knobDefs[i].id, knobDefs[i].label, knobDefs[i].cx, knobDefs[i].cy + top, knobDefs[i].r, modern::accent(),
                  [this, i] (float v) { return formatValue (i, v); } };
        k.noValue = true;
        k.help = String::fromUTF8 (knobHelp.at (knobDefs[i].id));
        knobs.push_back (k);
    }
    display = down (rOled);
    plainDisplay = true;
    addBrowserButtons (false);
    MButton byp { down (rBypass), [] { return String ("BYPASS"); }, [this] { return flag ("bypass"); }, [this] { toggle ("bypass"); } };
    byp.paint = [] (Graphics&, juce::Rectangle<float>, bool, bool) {};
    buttons.push_back (byp);
}

void RV1Editor::buildModern()
{
    for (int i = 0; i < NK; ++i)
    {
        const auto& d = knobDefs[i];
        MKnob k { d.id, d.label, d.cx, d.cy + top, d.r, knobColour (d.ring), [this, i] (float v) { return formatValue (i, v); } };
        k.help = String::fromUTF8 (knobHelp.at (d.id));
        knobs.push_back (k);
    }
    display = down (rOled);
    addBrowserButtons (true);
}

// ---------------------------------------------------------------- Anzeige
void RV1Editor::tick()
{
    auto fall = [] (float& disp, float v) { disp = v > disp ? v : std::max (-90.0f, disp - 1.2f); };
    fall (inDisp, dsp::a2db (proc.inPeak.exchange (0.0f)));
    fall (wetDisp, dsp::a2db (proc.wetPeak.exchange (0.0f)));
    const float d = proc.duckDb.load();
    duckDisp = d < duckDisp ? d : std::min (0.0f, duckDisp + 0.6f);
}

void RV1Editor::drawDisplay (Graphics& g, juce::Rectangle<float> r)
{
    drawProgram (g, r, builtView == 1);
    if (builtView == 1) headerInfo (g, String::fromUTF8 ("Klicken: nächstes Programm  ·  Mausrad: blättern"));
}

bool RV1Editor::displayDown (juce::Point<float>, const juce::MouseEvent&)
{
    stepProgram (1);
    return false;
}

void RV1Editor::displayWheel (juce::Point<float>, float d, const juce::MouseEvent&)
{
    stepProgram (d > 0 ? -1 : 1);
}

String RV1Editor::formatValue (int i, float v) const
{
    const String id = knobDefs[i].id;
    if (id == "lowCut" && v <= 20.5f) return "OFF";
    if (id == "highCut" && v >= 19500.0f) return "OFF";
    if (id == "decay" && (int) value ("algo") == RV1Processor::gated)
        return "GATE " + String (juce::roundToInt (juce::jlimit (0.06f, 1.2f, v * 0.25f) * 1000)) + " ms";
    switch (knobDefs[i].fmt)
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
    const String id = knobDefs[i].id;
    if ((id == "lowCut" && v <= 20.5f) || (id == "highCut" && v >= 19500.0f)) return "OFF";
    switch (knobDefs[i].fmt)
    {
        case fChar: return v == 0 ? "DARK" : (v == 100 ? "BRIGHT" : "");
        case fMix:  return v == 0 ? "DRY" : (v == 100 ? "WET" : "50");
        case fSec:  return v < 1 ? "." + String (juce::roundToInt (v * 10)) : String (juce::roundToInt (v)) + "s";
        case fHz:   return v >= 1000 ? String (v / 1000.0f, v < 10000 && v != std::floor (v / 1000) * 1000 ? 1 : 0) + "k" : String (juce::roundToInt (v));
        default:    return String (juce::roundToInt (v));
    }
}

void RV1Editor::rebuildSprites (float ps)
{
    for (int i = 0; i < NK; ++i)
        knobSprites[(size_t) i] = buildKnob (knobDefs[i].cx, knobDefs[i].cy, knobDefs[i].r, knobDefs[i].ring, ps, 340.0f);
    toggleSprites[0] = buildToggle (rBypass.getCentreX(), rBypass.getCentreY(), false, ps, 340.0f);
    toggleSprites[1] = buildToggle (rBypass.getCentreX(), rBypass.getCentreY(), true, ps, 340.0f);
    builtPs = ps;
}

void RV1Editor::drawBackground (Graphics& g)
{
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (0, top));
    if (builtView == 1) drawModern (g);
    else
    {
        const float ps = std::round (g.getInternalContext().getPhysicalPixelScaleFactor() * 4.0f) / 4.0f;
        if (std::abs (ps - builtPs) > 0.01f) rebuildSprites (ps);
        g.reduceClipRegion (juce::Rectangle<int> (0, 0, (int) W, 340));
        drawConsole (g);
    }
    g.restoreState();
}

void RV1Editor::drawKnob (Graphics& g, int i)
{
    if (builtView == 1 || i >= NK) { ModernEditor::drawKnob (g, i); return; }
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (0, top));
    drawConsoleKnob (g, i);
    g.restoreState();
}

void RV1Editor::drawScale (juce::Graphics& g, int i, float rOuter, Colour c)
{
    const auto& k = knobDefs[i];
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

void RV1Editor::drawConsoleKnob (juce::Graphics& g, int i)
{
    const auto& k = knobDefs[i];
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

// Programm-Display: Kategorie, Name, Algorithmus und die Hüllkurve des Halls
void RV1Editor::drawProgram (juce::Graphics& g, juce::Rectangle<float> r, bool modern)
{
    const auto& progs = RV1Processor::programs();
    const int fp = proc.factoryProgram();
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
    ltext (g, fp >= 0 ? String (RV1Processor::categoryNames[progs[(size_t) fp].category])
                      : String (proc.presetName == "Init" ? "INIT" : "EIGENES PRESET"), x, r.getY() + 8, 10);
    g.setColour (cDim);
    if (fp >= 0) rtext (g, String (fp + 1).paddedLeft ('0', 2) + " / " + String ((int) progs.size()), rr, r.getY() + 8, 10);
    g.setFont (font (20)); g.setColour (cTx);
    ltext (g, proc.presetName, x, r.getY() + 21, 20);

    g.setFont (font (10));
    const int h = drag >= 0 ? drag : hot;
    if (h >= 0 && h < NK)
    {
        g.setColour (cHi);
        ltext (g, String (knobDefs[h].label) + "  " + formatValue (h, value (knobDefs[h].id)), x, r.getY() + 48, 10);
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

void RV1Editor::drawConsole (juce::Graphics& g)
{
    drawFaceplate (g, W, 340.0f);
    drawSeam (g, 344, 22, 296);
    drawSeam (g, 846, 22, 296);

    g.setFont (font (22)); g.setColour (silk()); ltext (g, "RV-1", 24, 290, 22);
    g.setFont (font (11)); g.setColour (silk (0.95f)); ltext (g, "ONNO", 88, 292, 11);
    g.setFont (font (8)); g.setColour (silk (0.75f)); ltext (g, "ALGORITHMIC REVERB", 88, 306, 8);

    g.setFont (font (8)); g.setColour (silk (0.95f));
    ctext (g, "ON", rBypass.getCentreX(), rBypass.getY() - 12, 8);
    ctext (g, "BYPASS", rBypass.getCentreX(), rBypass.getBottom() + 2, 8);
    toggleSprites[flag ("bypass") ? 1 : 0].draw (g);
}

// ---------------------------------------------------------------- Modern
void RV1Editor::drawModern (Graphics& g)
{
    modern::panelRect (g, 12, 12, 328, 316);
    modern::panelRect (g, 350, 12, 492, 316);
    modern::panelRect (g, 852, 12, 136, 316);
    g.setFont (font (9)); g.setColour (modern::dim());
    ltext (g, "PROGRAMM", 24, 206, 9);
}
