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
const KnobDef knobDefs[] = {
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
constexpr int NK = (int) std::size (knobDefs);

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

// Schaltflächen der Workflow-Leiste und in den Modulen (Raster des Kanals, ohne Kopfzeile)
constexpr float presetX0 = 290, presetW = 44, presetStep = 50, rowY = 322, rowH = 18;
const juce::Rectangle<float> rAutoTrim { 24, 244, 70, 14 }, rLfBell { 311, 244, 40, 14 }, rHfBell { 596, 244, 40, 14 },
                             rPreEq { 784, 244, 60, 14 }, rAutoGain { 985, 268, 70, 14 },
                             rBypass { 1026, 300, 48, 52 }, rOled { 14, 300, 262, 50 }, rEq { 806, 300, 270, 60 };

// Hilfetexte der Regler (mehrere Regler heißen gleich, z. B. FREQ)
const std::map<String, const char*> knobHelp = {
    { "trim", "Eingangspegel vor allem anderen. AUTO TRIM stellt ihn automatisch auf -18 dBFS RMS." },
    { "drive", "Analoge Sättigung (überabgetastet): mehr Dichte und Obertöne. 0 = sauber." },
    { "hpf", "Hochpass 18 dB/Okt: entfernt Rumpeln und Bässe darunter. Ganz links = aus." },
    { "lpf", "Tiefpass 12 dB/Okt: zähmt Höhen und Fizz. Ganz rechts = aus." },
    { "gateThr", "Gate: unter diesem Pegel wird abgesenkt (Übersprechen, Rauschen)." },
    { "gateRange", "Gate: wie weit höchstens abgesenkt wird." },
    { "gateRel", "Gate: wie schnell es nach dem Signal schließt." },
    { "lfGain", "Bässe anheben oder absenken (Shelf; mit BELL als Glocke)." },
    { "lmfGain", "Untere Mitten (Glocke): Matsch, Pappe, Box-Klang." },
    { "hmfGain", "Obere Mitten (Glocke): Präsenz, Anschlag, Härte." },
    { "hfGain", "Höhen anheben oder absenken (Shelf; mit BELL als Glocke)." },
    { "lfFreq", "Frequenz des LF-Bandes." },
    { "lmfFreq", "Frequenz des LMF-Bandes." },
    { "hmfFreq", "Frequenz des HMF-Bandes." },
    { "hfFreq", "Frequenz des HF-Bandes." },
    { "lmfQ", "Bandbreite: hoch = schmal und chirurgisch, niedrig = breit und musikalisch." },
    { "hmfQ", "Bandbreite: hoch = schmal und chirurgisch, niedrig = breit und musikalisch." },
    { "compThr", "Kompressor: ab diesem Pegel wird komprimiert. Tiefer = mehr Wirkung." },
    { "compRatio", "Kompressor: wie stark über dem Threshold gedrückt wird. 2:1 sanft, 4:1 deutlich, ab 10:1 wie ein Limiter." },
    { "compAtt", "Kompressor: wie schnell er einsetzt. Kurz fängt Spitzen, länger lässt den Anschlag durch." },
    { "compRel", "Kompressor: wie schnell er loslässt. Kurz = lebendig, lang = gleichmäßig." },
    { "compMix", "Parallel-Kompression: Anteil des komprimierten Signals." },
    { "dsFreq", "De-Esser: ab dieser Frequenz wird abgesenkt." },
    { "dsThr", "De-Esser: ab diesem Pegel der Höhen greift er." },
    { "dsRange", "De-Esser: wie weit höchstens abgesenkt wird." },
    { "out", "Ausgangspegel des Kanals." },
};

const char* const moduleHelp[] = { "", "", "Gate/Expander: senkt leise Passagen ab, z. B. Übersprechen und Rauschen. Klicken = Modul ein/aus.",
                                   "Vierband-EQ im Konsolen-Stil. Klicken = Modul ein/aus.",
                                   "Kompressor. Klicken = Modul ein/aus.",
                                   "De-Esser: senkt scharfe S-Laute und Fizz über der Frequenz ab. Klicken = Modul ein/aus.", "" };

juce::Rectangle<float> sourceRect (int i) { return { presetX0 + i * presetStep, rowY, presetW, rowH }; }
juce::Rectangle<float> titleRect (int c) { return { columns[c].x0 + 4, 16, columns[c].x1 - columns[c].x0 - 8, 16 }; }
juce::Rectangle<float> down (juce::Rectangle<float> r) { return r.translated (0, CS1Editor::top); }

constexpr float pi = juce::MathConstants<float>::pi;
constexpr float a0 = -0.75f * pi;

float a2db (float x) { return 8.685889638f * std::log (std::max (x, 1.0e-9f)); }

String hz (float v)
{
    if (v >= 1000) return String (v / 1000.0f, v < 10000 ? 1 : 0) + "k";
    return String (juce::roundToInt (v));
}
}

CS1Editor::CS1Editor (CS1Processor& p) : ModernEditor (p, p.apvts, "CS-1", W0, H0), proc (p)
{
    knobSprites.resize ((size_t) NK);
    enableViewSwitch();
    addHelp ({ { "AUTO TRIM", "Misst 3 s Signal und stellt TRIM auf -18 dBFS RMS. Nochmal klicken bricht ab." },
               { "BELL", "Band als Glocke statt als Shelf." },
               { "PRE EQ", "Kompressor vor dem EQ statt danach." },
               { "AUTO GAIN", "Hält den Ausgang so laut wie den getrimmten Eingang. Teil des Klangs, bleibt im Mixdown." } });
    syncView();
}

// ------------------------------------------------------------- Ansichten
void CS1Editor::syncView()
{
    const int v = value ("view") > 0.5f ? 1 : 0;
    if (v == builtView) return;
    builtView = v;
    clearControls();
    if (v == 1) buildModern(); else buildConsole();
}

void CS1Editor::addWorkflowButtons (bool modern)
{
    auto rackPaint = [] (const String& label)
    {
        return [label] (Graphics& g, juce::Rectangle<float> r, bool on, bool) { drawButton (g, r.getX(), r.getY(), r.getWidth(), r.getHeight(), on, label); };
    };
    auto add = [&] (juce::Rectangle<float> r, const String& label, std::function<bool()> on, std::function<void()> click, Colour c = modern::accent())
    {
        MButton b { down (r), [label] { return label; }, on, click, c };
        if (! modern) b.paint = rackPaint (label);
        buttons.push_back (b);
    };
    add (rAutoTrim, "AUTO TRIM", [this] { return proc.autoTrimArmed.load(); },
         [this] { proc.autoTrimArmed = ! proc.autoTrimArmed.load(); proc.autoTrimProgress = 0; });
    add (rLfBell, "BELL", [this] { return flag ("lfBell"); }, [this] { toggle ("lfBell"); }, modColour (mEq));
    add (rHfBell, "BELL", [this] { return flag ("hfBell"); }, [this] { toggle ("hfBell"); }, modColour (mEq));
    add (rPreEq, "PRE EQ", [this] { return flag ("compPre"); }, [this] { toggle ("compPre"); }, modColour (mComp));
    add (rAutoGain, "AUTO GAIN", [this] { return flag ("autoGain"); }, [this] { toggle ("autoGain"); });
    // Quell-Presets: ein Klick lädt das Werkspreset; TRIM, OUTPUT und AUTO GAIN bleiben
    for (int i = 0; i < CS1Processor::numPresets; ++i)
    {
        const String name = CS1Processor::presetNames[i];
        add (sourceRect (i), name.toUpperCase(), [this, name] { return proc.presetName == name; }, [this, i] { proc.applyPreset (i); });
        buttons.back().help = String::fromUTF8 ("Quell-Preset ") + name + String::fromUTF8 (": lädt einen Startpunkt für diese Spur. TRIM, OUTPUT und AUTO GAIN bleiben.");
    }
}

void CS1Editor::buildConsole()
{
    for (int i = 0; i < NK; ++i)
    {
        MKnob k { knobDefs[i].id, knobDefs[i].label, knobDefs[i].cx, knobDefs[i].cy + top, knobDefs[i].r, modern::accent(),
                  [this, i] (float v) { return formatValue (i, v); } };
        k.noValue = true;
        k.help = String::fromUTF8 (knobHelp.at (knobDefs[i].id));
        knobs.push_back (k);
    }
    // Modultitel mit LED: Fläche zum Ein-/Ausschalten (gezeichnet von der Konsole)
    for (int c = 0; c < (int) std::size (columns); ++c)
        if (columns[c].onId != nullptr)
        {
            const char* id = columns[c].onId;
            MButton b { down (titleRect (c)), [c] { return String (columns[c].title); }, [this, id] { return flag (id); }, [this, id] { toggle (id); } };
            b.help = String::fromUTF8 (moduleHelp[c]);
            b.paint = [] (Graphics&, juce::Rectangle<float>, bool, bool) {};
            buttons.push_back (b);
        }
    addWorkflowButtons (false);
    MButton byp { down (rBypass), [] { return String ("BYPASS"); }, [this] { return flag ("bypass"); }, [this] { toggle ("bypass"); } };
    byp.paint = [] (Graphics&, juce::Rectangle<float>, bool, bool) {};
    buttons.push_back (byp);
}

void CS1Editor::buildModern()
{
    for (int i = 0; i < NK; ++i)
    {
        const auto& d = knobDefs[i];
        const bool bipolar = d.fmt == fDb && d.s0 < 0 && d.s2 > 0;
        MKnob k { d.id, d.label, d.cx, d.cy + top, d.r, ringColour (d.ring, d.mod), [this, i] (float v) { return formatValue (i, v); }, bipolar };
        k.help = String::fromUTF8 (knobHelp.at (d.id));
        knobs.push_back (k);
    }
    // schaltbare Module: Titel als Pill in der Modulfarbe
    for (int c = 0; c < (int) std::size (columns); ++c)
        if (columns[c].onId != nullptr)
        {
            const char* id = columns[c].onId;
            const auto tr = titleRect (c);
            const float pw = font (10).getStringWidthFloat (columns[c].title) + 22;
            MButton b { { tr.getCentreX() - pw * 0.5f, 17 + top, pw, 16 }, [c] { return String (columns[c].title); },
                        [this, id] { return flag (id); }, [this, id] { toggle (id); }, modColour (c) };
            b.help = String::fromUTF8 (moduleHelp[c]);
            buttons.push_back (b);
        }
    addWorkflowButtons (true);
}

// ---------------------------------------------------------------- Anzeige
void CS1Editor::tick()
{
    auto fall = [] (float& disp, float v, float rate) { disp = v > disp ? v : std::max (-90.0f, disp - rate); };
    auto rise = [] (float& disp, float v) { disp = v < disp ? v : std::min (0.0f, disp + 0.6f); };
    fall (inDisp, dsp::a2db (proc.inPeak.exchange (0.0f)), 1.2f);
    fall (outDisp, dsp::a2db (proc.outPeak.exchange (0.0f)), 1.2f);
    rise (gateDisp, proc.gateGr.exchange (0.0f));
    rise (compDisp, proc.compGr.exchange (0.0f));
    rise (dsDisp, proc.dsGr.exchange (0.0f));

    if (proc.autoTrimDone.exchange (false))
        setValue ("trim", proc.autoTrimResult.load());
}

String CS1Editor::formatValue (int i, float v) const
{
    const String id = knobDefs[i].id;
    if (id == "hpf" && v <= 16.5f) return "OFF";
    if (id == "lpf" && v >= 21500.0f) return "OFF";
    switch (knobDefs[i].fmt)
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
    const String id = knobDefs[i].id;
    if ((id == "hpf" && v <= 16.5f) || (id == "lpf" && v >= 21500.0f)) return "OFF";
    switch (knobDefs[i].fmt)
    {
        case fHz: return hz (v);
        case fMs: return v >= 1000 ? String (v / 1000.0f, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "s"
                                   : (v < 1 ? "." + String (juce::roundToInt (v * 10)) : String (juce::roundToInt (v)));
        case fQ:  return v < 1 ? "." + String (juce::roundToInt (v * 10)) : String (v, v == std::floor (v) ? 0 : 1);
        case fDb: return (v > 0 ? "+" : "") + String (juce::roundToInt (v));
        default:  return String (juce::roundToInt (v));
    }
}

void CS1Editor::rebuildSprites (float ps)
{
    for (int i = 0; i < NK; ++i)
        knobSprites[(size_t) i] = buildKnob (knobDefs[i].cx, knobDefs[i].cy, knobDefs[i].r, knobDefs[i].ring, ps, 380.0f);
    toggleSprites[0] = buildToggle (rBypass.getCentreX(), 326, false, ps, 380.0f);
    toggleSprites[1] = buildToggle (rBypass.getCentreX(), 326, true, ps, 380.0f);
    builtPs = ps;
}

void CS1Editor::drawBackground (Graphics& g)
{
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (0, top));
    if (builtView == 1) drawModern (g);
    else
    {
        // 3D-Teile in der tatsächlichen Bildschirmauflösung neu berechnen, wenn sie sich ändert
        const float ps = std::round (g.getInternalContext().getPhysicalPixelScaleFactor() * 4.0f) / 4.0f;
        if (std::abs (ps - builtPs) > 0.01f) rebuildSprites (ps);
        g.reduceClipRegion (juce::Rectangle<int> (0, 0, (int) W, 380));
        drawConsole (g);
    }
    g.restoreState();
}

// abgeschaltete Module abdunkeln (über Reglern und Tasten)
void CS1Editor::drawOverlay (Graphics& g)
{
    for (const auto& col : columns)
        if (col.onId != nullptr && ! flag (col.onId))
        {
            if (builtView == 1) { g.setColour (modern::panel().withAlpha (0.6f)); g.fillRect (col.x0 + 4, 38.0f + top, col.x1 - col.x0 - 8, 240.0f); }
            else { g.setColour (juce::Colours::black.withAlpha (0.38f)); g.fillRect (col.x0 + 2, 36.0f + top, col.x1 - col.x0 - 3, 246.0f); }
        }
}

void CS1Editor::drawKnob (Graphics& g, int i)
{
    if (builtView == 1 || i >= NK) { ModernEditor::drawKnob (g, i); return; }
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (0, top));
    drawConsoleKnob (g, i);
    g.restoreState();
}

void CS1Editor::drawScale (juce::Graphics& g, int i, float rOuter, Colour c)
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
        const float tw = f.getStringWidthFloat (t), th = 8.0f;
        const float rx = k.cx + std::sin (ta) * (rOuter + 9 + tw * 0.5f), ry = k.cy - std::cos (ta) * (rOuter + 9 + th * 0.5f);
        g.setColour (c.withMultipliedAlpha (0.75f));
        g.drawText (t, juce::Rectangle<float> (rx - tw * 0.5f - 2, ry - th * 0.5f - 1, tw + 4, th + 2), juce::Justification::centred, false);
    }
}

void CS1Editor::drawConsoleKnob (juce::Graphics& g, int i)
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
    g.setFont (font (10)); g.setColour (silk());
    ctext (g, k.label, cx, cy - r - 33, 10);
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
    if (h >= 0 && h < NK)
    {
        g.setFont (font (11)); g.setColour (c1);
        String name = String (moduleNames[knobDefs[h].mod]) + "  " + knobDefs[h].label;
        if (knobDefs[h].mod == mEq)
        {
            static const char* const bands[] = { "LF", "LMF", "HMF", "HF" };
            const String band = bands[juce::jlimit (0, 3, juce::roundToInt ((knobDefs[h].cx - 331) / 95.0f))];
            name = "EQ  " + band + "  " + (String (knobDefs[h].label) == band ? String ("GAIN") : String (knobDefs[h].label));
        }
        ltext (g, name, x, y1, 11);
        g.setFont (font (15)); g.setColour (c2);
        ltext (g, formatValue (h, value (knobDefs[h].id)), x, y2, 15);
        return;
    }
    g.setFont (font (11)); g.setColour (c1);
    const String head = proc.autoTrimArmed.load() ? "AUTO TRIM  " + String (juce::roundToInt (proc.autoTrimProgress.load() * 100)) + " %"
                                                  : proc.presetName.toUpperCase();
    ltext (g, head, x, y1, 11);
    rtext (g, String ("SLOT ") + (proc.getSlot() == 0 ? "A" : "B"), r.getRight() - 10, y1, 11);
    g.setFont (font (12)); g.setColour (c2);
    auto grs = [] (float v) { return v > -0.05f ? String ("0.0") : String (v, 1); };
    ltext (g, flag ("bypass") ? String ("BYPASS")
                              : "GATE " + grs (gateDisp) + "   COMP " + grs (compDisp) + "   DS " + grs (dsDisp), x, y2 + 2, 12);
}

// EQ-Kurve der Filter und Bänder, klein in der Workflow-Leiste
void CS1Editor::drawEqCurve (Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (mc::screen()); g.fillRoundedRectangle (r, 6);
    const auto in = r.reduced (6, 5);
    drawFreqGrid (g, in, false);
    auto y = [in] (float db) { return in.getCentreY() - juce::jlimit (-1.0f, 1.0f, db / 18.0f) * in.getHeight() * 0.5f; };
    g.setColour (mc::grid()); g.fillRect (in.getX(), in.getCentreY(), in.getWidth(), 1.0f);
    juce::Path p;
    for (float x = in.getX(); x <= in.getRight(); x += 1.5f)
    {
        const float v = y (proc.eqResponseDb (xToFreq (x, in)));
        if (x == in.getX()) p.startNewSubPath (x, v); else p.lineTo (x, v);
    }
    g.setColour (flag ("eqOn") ? modern::accent() : modern::dim()); g.strokePath (p, juce::PathStrokeType (1.6f));
    g.setFont (font (9)); g.setColour (modern::dim());
    ltext (g, "EQ + FILTER", r.getX() + 8, r.getY() + 5, 9);
}

void CS1Editor::drawConsole (juce::Graphics& g)
{
    drawFaceplate (g, W, 380.0f);
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

    // Meter
    drawLadder (g, 987, 128, 108, inDisp, false, false);
    drawLadder (g, 1013, 128, 108, compDisp, true, false);
    drawLadder (g, 1039, 128, 108, outDisp, false, false);
    g.setFont (font (8)); g.setColour (silk (0.9f));
    ctext (g, "IN", 994, 240, 8); ctext (g, "GR", 1020, 240, 8); ctext (g, "OUT", 1046, 240, 8);

    // --- Workflow-Leiste ---
    drawReadout (g, rOled, false);

    g.setFont (font (22)); g.setColour (silk()); ltext (g, "CS-1", 944, 304, 22);
    g.setFont (font (8)); g.setColour (silk (0.75f)); ltext (g, "ONNO CHANNEL", 944, 332, 8);

    g.setFont (font (8)); g.setColour (silk (0.95f));
    ctext (g, "ON", rBypass.getCentreX(), 292, 8); ctext (g, "BYPASS", rBypass.getCentreX(), 352, 8);
    toggleSprites[flag ("bypass") ? 1 : 0].draw (g);
}

// ---------------------------------------------------------------- Modern
void CS1Editor::drawModern (Graphics& g)
{
    for (int c = 0; c < (int) std::size (columns); ++c)
    {
        const auto& col = columns[c];
        modern::panelRect (g, col.x0 + 3, 12, col.x1 - col.x0 - 6, 270);
        if (col.onId == nullptr)
        {
            auto tr = titleRect (c);
            g.setColour (modColour (c)); g.setFont (font (10));
            g.drawText (col.title, juce::Rectangle<float> (tr.getX(), 17, tr.getWidth(), 16), juce::Justification::centred, false);
        }
    }

    drawLadder (g, 987, 128, 108, inDisp, false, true);
    drawLadder (g, 1013, 128, 108, compDisp, true, true);
    drawLadder (g, 1039, 128, 108, outDisp, false, true);
    g.setFont (font (9)); g.setColour (modern::dim());
    ctext (g, "IN", 994, 240, 9); ctext (g, "GR", 1020, 240, 9); ctext (g, "OUT", 1046, 240, 9);

    // --- Workflow-Leiste ---
    modern::panelRect (g, 10, 292, W - 20, 76);
    g.setColour (mc::screen()); g.fillRoundedRectangle (rOled, 6);
    drawReadout (g, rOled, true);
    g.setFont (font (9)); g.setColour (modern::dim());
    ltext (g, "QUELLE", presetX0, 306, 9);
    drawEqCurve (g, rEq);
}
