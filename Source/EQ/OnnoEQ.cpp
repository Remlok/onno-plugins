#include "OnnoEQ.h"

using namespace onno;
using juce::String;

const juce::StringArray EQProcessor::typeNames = [] { juce::StringArray a; for (auto& n : EQProcessor::Core::typeNames()) a.add (n); return a; }();
const juce::StringArray EQProcessor::typeShort { "LC", "LS", "BELL", "NOTCH", "HS", "HC", "BP" };

juce::AudioProcessorValueTreeState::ParameterLayout EQProcessor::layout()
{
    prm::Layout p;
    prm::fromCore (p, Core::makeParams());
    return prm::done (p);
}

EQProcessor::EQProcessor() : ModernProcessor ("Onno EQ", "OnnoEQ", stereoBuses(), layout())
{
    static const char* const names[] = { "on", "type", "freq", "gain", "q", "slope" };
    for (int b = 0; b < numBands; ++b)
        for (int k = 0; k < 6; ++k) bp[(size_t) b][(size_t) k] = raw (id (b, names[k]));
    pAnalyzer = raw ("analyzer");
    link.attach (apvts, core);
}

void EQProcessor::prepare (double sampleRate, int blockSize)
{
    analyzer.prepare (sampleRate);
    link.pull (core);
    core.prepare (sampleRate, blockSize);
}

EQProcessor::Band EQProcessor::band (int b) const
{
    const auto& p = bp[(size_t) b];
    Band r;
    r.on = p[pOn]->load() > 0.5f;
    r.type = (int) p[pType]->load();
    r.freq = p[pFreq]->load();
    r.gain = p[pGain]->load();
    r.q = p[pQ]->load();
    r.slope = (int) p[pSlope]->load();
    return r;
}

void EQProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = std::min (2, buf.getNumChannels()), n = buf.getNumSamples();
    const int mode = (int) pAnalyzer->load();
    if (mode == 1) analyzer.push (buf.getReadPointer (0), nch > 1 ? buf.getReadPointer (1) : nullptr, n);
    link.pull (core);
    core.process (buf.getWritePointer (0), buf.getWritePointer (nch > 1 ? 1 : 0), n, nch, nullptr, nullptr);
    if (mode == 2) analyzer.push (buf.getReadPointer (0), nch > 1 ? buf.getReadPointer (1) : nullptr, n);
    atomicMax (outPeak, core.outPk);
}

juce::AudioProcessorEditor* EQProcessor::createEditor() { return new EQEditor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EQProcessor(); }
#endif

// ======================================================================== Editor
namespace
{
const juce::Rectangle<float> rDisplay { 16, 50, 868, 236 };
}

EQEditor::EQEditor (EQProcessor& p) : ModernEditor (p, p.apvts, "EQ", 900, 470), proc (p)
{
    display = rDisplay;
    addHelp ({ { "FREQ", "Frequenz des gewählten Bandes. Auch im Display am Knoten ziehbar." },
               { "GAIN", "Anhebung oder Absenkung des gewählten Bandes." },
               { "Q", "Breite des Bandes. Klein = breit und musikalisch, groß = schmal, für Resonanzen. Im Display: Mausrad am Knoten." },
               { "OUTPUT", "Gesamtpegel nach dem EQ. Nach Anhebungen hier zurücknehmen oder MATCH nutzen." },
               { "LOW CUT", "Schneidet alles unterhalb der Frequenz weg, z. B. Rumpeln unter Gitarren." },
               { "HIGH CUT", "Schneidet alles oberhalb der Frequenz weg, z. B. Fizz bei High-Gain-Gitarren." },
               { "LOW SHELF", "Hebt oder senkt alles unterhalb der Frequenz gleichmäßig." },
               { "HIGH SHELF", "Hebt oder senkt alles oberhalb der Frequenz gleichmäßig, z. B. für Luft." },
               { "BELL", "Glocke um die Frequenz: der Allrounder zum Anheben oder Absenken." },
               { "NOTCH", "Sehr schmaler, tiefer Schnitt, um einzelne Pfeiftöne oder Resonanzen zu entfernen." },
               { "BAND PASS", "Lässt nur den Bereich um die Frequenz durch, gut zum Suchen von Problemstellen." },
               { "BAND ON", "Band ist aktiv. Klicken = Band ausschalten." },
               { "BAND OFF", "Band ist aus. Klicken = Band einschalten." },
               { "RESET BAND", "Setzt das gewählte Band auf seine Grundeinstellung zurück." },
               { "ALLE FLACH", "Setzt alle Bänder auf 0 dB." } });
    // Band-Reiter
    for (int b = 0; b < EQProcessor::numBands; ++b)
        buttons.push_back ({ { 28.0f + b * 62.0f, 306, 56, 20 },
                             [this, b] { return String (b + 1) + " " + (flag (EQProcessor::id (b, "on")) ? EQProcessor::typeShort[(int) value (EQProcessor::id (b, "type"))] : String ("OFF")); },
                             [this, b] { return sel == b; }, [this, b] { selectBand (b); }, mc::band (b) });
    // Filtertyp
    for (int t = 0; t < 7; ++t)
        buttons.push_back ({ { 290.0f + (t % 4) * 70.0f, t < 4 ? 342.0f : 366.0f, 64, 18 },
                             [t] { return EQProcessor::typeNames[t].toUpperCase(); },
                             [this, t] { return (int) value (EQProcessor::id (sel, "type")) == t; },
                             [this, t] { setValue (EQProcessor::id (sel, "type"), (float) t); }, modern::accent() });
    // Flankensteilheit (nur bei Low/High Cut)
    for (int sl = 0; sl < 3; ++sl)
        buttons.push_back ({ { 290.0f + sl * 70.0f, 390, 64, 18 }, [sl] { return String (12 << sl) + " dB/OCT"; },
                             [this, sl] { return (int) value (EQProcessor::id (sel, "slope")) == sl; },
                             [this, sl] { setValue (EQProcessor::id (sel, "slope"), (float) sl); }, modern::accent(),
                             [this] { return EQProcessor::isCut ((int) value (EQProcessor::id (sel, "type"))); } });
    buttons.push_back ({ { 500, 390, 64, 18 }, [this] { return String (flag (EQProcessor::id (sel, "on")) ? "BAND ON" : "BAND OFF"); },
                         [this] { return flag (EQProcessor::id (sel, "on")); }, [this] { toggle (EQProcessor::id (sel, "on")); }, mc::green() });
    buttons.push_back ({ { 600, 342, 120, 18 }, [this] { return "ANALYZER: " + param ("analyzer")->getCurrentValueAsText().toUpperCase(); },
                         [this] { return value ("analyzer") > 0.5f; }, [this] { setValue ("analyzer", (float) (((int) value ("analyzer") + 1) % 3)); }, mc::blue() });
    buttons.push_back ({ { 600, 366, 120, 18 }, [] { return String ("RESET BAND"); }, [] { return false; },
                         [this] {
                             setValue (EQProcessor::id (sel, "gain"), 0);
                             auto* q = param (EQProcessor::id (sel, "q"));
                             q->beginChangeGesture(); q->setValueNotifyingHost (q->getDefaultValue()); q->endChangeGesture();
                         } });
    buttons.push_back ({ { 600, 390, 120, 18 }, [] { return String ("ALLE FLACH"); }, [] { return false; },
                         [this] { for (int b = 0; b < EQProcessor::numBands; ++b) setValue (EQProcessor::id (b, "gain"), 0); } });
    addAnalogStrip();
    stripReady = true;
    selectBand (2);
}


void EQEditor::selectBand (int b)
{
    sel = b;
    const auto c = mc::band (b);
    knobs.clear();
    knobs.push_back ({ EQProcessor::id (b, "freq"), "FREQ", 70, 376, 22, c, [] (float v) { return fmt::hz (v); } });
    knobs.push_back ({ EQProcessor::id (b, "gain"), "GAIN", 150, 376, 22, c, [] (float v) { return fmt::db (v); }, true });
    knobs.push_back ({ EQProcessor::id (b, "q"), "Q", 230, 376, 22, c, [] (float v) { return String (v, v < 10 ? 2 : 1); } });
    knobs.push_back ({ "output", "OUTPUT", 810, 372, 26, modern::accent(), [] (float v) { return fmt::db (v); }, true });
    if (stripReady) { MKnob d { "drive", "DRIVE", 548, H - 27, 11, modern::warm(), [] (float v) { return fmt::pct (v); } }; d.compact = true; knobs.push_back (d); }
    repaint();
}

double EQEditor::rate() const { return proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0; }
float EQEditor::dbToY (float db) const { return display.getCentreY() - db / range * (display.getHeight() * 0.5f - 14); }
float EQEditor::yToDb (float y) const { return (display.getCentreY() - y) / (display.getHeight() * 0.5f - 14) * range; }

juce::Point<float> EQEditor::nodePos (int b) const
{
    const int t = (int) value (EQProcessor::id (b, "type"));
    const float g = EQProcessor::hasGain (t) ? value (EQProcessor::id (b, "gain")) : 0.0f;
    return { freqToX (value (EQProcessor::id (b, "freq")), display), dbToY (g) };
}

int EQEditor::hitNode (juce::Point<float> p) const
{
    int best = -1; float bd = 13 * 13;
    for (int b = 0; b < EQProcessor::numBands; ++b)
    {
        if (! flag (EQProcessor::id (b, "on"))) continue;
        const float d = nodePos (b).getDistanceSquaredFrom (p);
        if (d < bd || (b == sel && d < 13 * 13)) { bd = d; best = b; if (b == sel) break; }
    }
    return best;
}

void EQEditor::tick()
{
    if (value ("analyzer") > 0.5f) proc.analyzer.update();
}

void EQEditor::drawBackground (juce::Graphics& g)
{
    section (g, { 16, 296, 868, 126 });
    g.setColour (rgb (255, 255, 255, 0.05f));
    g.fillRect (272.0f, 336.0f, 1.0f, 76.0f);
    g.fillRect (740.0f, 336.0f, 1.0f, 76.0f);
}

void EQEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawFreqGrid (g, r);
    g.setFont (font (9, false));
    for (int d = -12; d <= 12; d += 6)
    {
        g.setColour (d == 0 ? rgb (255, 255, 255, 0.14f) : mc::grid());
        g.fillRect (r.getX(), dbToY ((float) d), r.getWidth(), 1.0f);
        g.setColour (mc::gridText()); rtext (g, (d > 0 ? "+" : "") + String (d), r.getRight() - 6, dbToY ((float) d) - 12, 9);
    }

    // Analyzer
    if (value ("analyzer") > 0.5f)
    {
        auto toY = [r] (float db) { return r.getBottom() - (db + 90.0f) / 90.0f * r.getHeight(); };
        auto toF = [r] (float x) { return xToFreq (x, r); };
        g.setColour (rgb (120, 140, 175, 0.22f)); g.fillPath (proc.analyzer.path (r, toF, toY, true));
        g.setColour (rgb (150, 170, 205, 0.45f)); g.strokePath (proc.analyzer.path (r, toF, toY, false), juce::PathStrokeType (1.0f));
    }

    // Kurven
    const double fs = rate();
    std::array<std::array<dsp::Biquad, 4>, EQProcessor::numBands> st;
    std::array<int, EQProcessor::numBands> ns {};
    std::array<EQProcessor::Band, EQProcessor::numBands> bands;
    for (int b = 0; b < EQProcessor::numBands; ++b)
    {
        bands[(size_t) b] = proc.band (b);
        ns[(size_t) b] = EQProcessor::design (bands[(size_t) b], fs, st[(size_t) b].data());
    }
    auto bandDb = [&] (int b, double f)
    {
        double d = 0;
        for (int s = 0; s < ns[(size_t) b]; ++s) d += st[(size_t) b][(size_t) s].magDb (fs, f);
        return d;
    };
    const float y0 = dbToY (0);

    // gewähltes Band einzeln
    if (bands[(size_t) sel].on)
    {
        juce::Path c;
        for (float x = r.getX(); x <= r.getRight(); x += 2)
        {
            const double f = xToFreq (x, r);
            const float y = juce::jlimit (r.getY() - 4, r.getBottom() + 4, dbToY ((float) (f < fs * 0.5 ? bandDb (sel, f) : 0)));
            if (x == r.getX()) c.startNewSubPath (x, y); else c.lineTo (x, y);
        }
        juce::Path p (c);
        p.lineTo (r.getRight(), y0); p.lineTo (r.getX(), y0); p.closeSubPath();
        g.setColour (mc::band (sel).withAlpha (0.16f)); g.fillPath (p);
        g.setColour (mc::band (sel).withAlpha (0.55f)); g.strokePath (c, juce::PathStrokeType (1.0f));
    }

    juce::Path total;
    for (float x = r.getX(); x <= r.getRight(); x += 2)
    {
        const double f = xToFreq (x, r);
        double d = 0;
        if (f < fs * 0.5)
            for (int b = 0; b < EQProcessor::numBands; ++b)
                if (bands[(size_t) b].on) d += bandDb (b, f);
        d += value ("output");
        const float y = juce::jlimit (r.getY() - 4, r.getBottom() + 4, dbToY ((float) d));
        if (x == r.getX()) total.startNewSubPath (x, y); else total.lineTo (x, y);
    }
    juce::Path fill (total);
    fill.lineTo (r.getRight(), y0); fill.lineTo (r.getX(), y0); fill.closeSubPath();
    g.setColour (modern::accent().withAlpha (0.10f)); g.fillPath (fill);
    g.setColour (flag ("bypass") ? modern::dim() : modern::accent()); g.strokePath (total, juce::PathStrokeType (2.2f));

    // Knoten
    for (int b = 0; b < EQProcessor::numBands; ++b)
    {
        if (! bands[(size_t) b].on) continue;
        const auto pnt = nodePos (b);
        const bool hv = b == hoverNode || b == dragNode;
        const float rr = b == sel ? 9.0f : 7.5f;
        g.setColour (mc::band (b).withAlpha (hv ? 0.35f : 0.18f)); g.fillEllipse (pnt.x - rr - 4, pnt.y - rr - 4, 2 * rr + 8, 2 * rr + 8);
        g.setColour (b == sel ? mc::band (b) : mc::band (b).withAlpha (0.85f)); g.fillEllipse (pnt.x - rr, pnt.y - rr, 2 * rr, 2 * rr);
        g.setColour (modern::bg()); g.setFont (font (10));
        g.drawText (String (b + 1), juce::Rectangle<float> (pnt.x - rr, pnt.y - rr, 2 * rr, 2 * rr), juce::Justification::centred, false);
    }
}

void EQEditor::drawOverlay (juce::Graphics& g)
{
    const int b = dragNode >= 0 ? dragNode : (hoverNode >= 0 ? hoverNode : -1);
    if (b >= 0)
    {
        const int t = (int) value (EQProcessor::id (b, "type"));
        String s = String (b + 1) + "  " + EQProcessor::typeNames[t] + "   " + fmt::hz (value (EQProcessor::id (b, "freq")));
        if (EQProcessor::hasGain (t)) s << "   " << fmt::db (value (EQProcessor::id (b, "gain")));
        s << "   Q " << String (value (EQProcessor::id (b, "q")), 2);
        headerInfo (g, s);
    }
    else headerInfo (g, "Knoten ziehen  -  Mausrad = Q  -  Doppelklick = Band an/aus");
}

void EQEditor::displayMove (juce::Point<float> p)
{
    hoverNode = display.contains (p) ? hitNode (p) : -1;
}

bool EQEditor::displayDown (juce::Point<float> p, const juce::MouseEvent&)
{
    const int b = hitNode (p);
    if (b < 0) return false;
    selectBand (b);
    dragNode = b;
    beginEdit (EQProcessor::id (b, "freq"));
    beginEdit (EQProcessor::id (b, "gain"));
    return true;
}

void EQEditor::displayDrag (juce::Point<float> p, const juce::MouseEvent&)
{
    if (dragNode < 0) return;
    edit (EQProcessor::id (dragNode, "freq"), juce::jlimit (20.0f, 20000.0f, xToFreq (p.x, display)));
    if (EQProcessor::hasGain ((int) value (EQProcessor::id (dragNode, "type"))))
        edit (EQProcessor::id (dragNode, "gain"), juce::jlimit (-24.0f, 24.0f, yToDb (p.y)));
}

void EQEditor::displayUp (juce::Point<float>)
{
    if (dragNode < 0) return;
    endEdit (EQProcessor::id (dragNode, "freq"));
    endEdit (EQProcessor::id (dragNode, "gain"));
    dragNode = -1;
}

void EQEditor::displayDoubleClick (juce::Point<float> p)
{
    const int b = hitNode (p);
    if (b >= 0) { setValue (EQProcessor::id (b, "on"), 0); hoverNode = -1; return; }
    for (int k = 0; k < EQProcessor::numBands; ++k)
        if (! flag (EQProcessor::id (k, "on")))
        {
            setValue (EQProcessor::id (k, "type"), (float) EQProcessor::bell);
            setValue (EQProcessor::id (k, "freq"), juce::jlimit (20.0f, 20000.0f, xToFreq (p.x, display)));
            setValue (EQProcessor::id (k, "gain"), juce::jlimit (-24.0f, 24.0f, yToDb (p.y)));
            setValue (EQProcessor::id (k, "q"), 1.0f);
            setValue (EQProcessor::id (k, "on"), 1);
            selectBand (k);
            return;
        }
}

void EQEditor::displayWheel (juce::Point<float> p, float d, const juce::MouseEvent&)
{
    int b = hitNode (p);
    if (b < 0) return;
    const auto qid = EQProcessor::id (b, "q");
    setValue (qid, juce::jlimit (0.1f, 18.0f, value (qid) * std::exp (d * 1.2f)));
}
