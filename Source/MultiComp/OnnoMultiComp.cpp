#include "OnnoMultiComp.h"

using namespace onno;
using juce::String;

const char* const MultiCompProcessor::bandNames[numBands] = { "LOW", "LOW-MID", "HIGH-MID", "HIGH" };

juce::AudioProcessorValueTreeState::ParameterLayout MultiCompProcessor::layout()
{
    prm::Layout p;
    const float att[] = { 20, 10, 5, 3 }, rel[] = { 200, 150, 100, 80 };
    for (int b = 0; b < numBands; ++b)
    {
        const String n = String ("Band ") + String (b + 1) + " ";
        prm::lin (p, id (b, "thr"), n + "Threshold", -60, 0, -18, "dB");
        prm::lg (p, id (b, "ratio"), n + "Ratio", 1, 20, 2, ":1");
        prm::lg (p, id (b, "attack"), n + "Attack", 0.1f, 200, att[b], "ms");
        prm::lg (p, id (b, "release"), n + "Release", 10, 2000, rel[b], "ms");
        prm::lin (p, id (b, "gain"), n + "Gain", -24, 24, 0, "dB");
        prm::bl (p, id (b, "solo"), n + "Solo", false);
        prm::bl (p, id (b, "byp"), n + "Bypass", false);
    }
    prm::lg (p, "x1", "Crossover Low", 20, 1000, 120, "Hz");
    prm::lg (p, "x2", "Crossover Mid", 150, 6000, 1000, "Hz");
    prm::lg (p, "x3", "Crossover High", 1000, 16000, 5000, "Hz");
    prm::lin (p, "mix", "Mix", 0, 100, 100, "%");
    prm::lin (p, "output", "Output", -24, 24, 0, "dB");
    Analog::addParams (p);
    prm::bl (p, "bypass", "Bypass", false);
    return prm::done (p);
}

MultiCompProcessor::MultiCompProcessor() : ModernProcessor ("Onno MultiComp", "OnnoMultiComp", stereoBuses(), layout())
{
    static const char* const names[] = { "thr", "ratio", "attack", "release", "gain", "solo", "byp" };
    for (int b = 0; b < numBands; ++b)
    {
        for (int k = 0; k < numP; ++k) bp[(size_t) b][(size_t) k] = raw (id (b, names[k]));
        bandGr[(size_t) b].store (0.0f);
    }
    pX1 = raw ("x1"); pX2 = raw ("x2"); pX3 = raw ("x3"); pMix = raw ("mix"); pOut = raw ("output"); pBypass = raw ("bypass");
    analog.attach (*this);
}

void MultiCompProcessor::crossovers (float& x1, float& x2, float& x3) const
{
    const float ny = (float) sr * 0.45f;
    x1 = std::min (pX1->load(), ny / 2.0f);
    x2 = juce::jlimit (x1 * 1.25f, ny / 1.5f, pX2->load());
    x3 = juce::jlimit (x2 * 1.25f, ny, pX3->load());
}

void MultiCompProcessor::prepare (double sampleRate, int)
{
    sr = sampleRate;
    analyzer.prepare (sr);
    fx1 = fx2 = fx3 = -1;
    for (auto* f : { &lp2[0], &lp2[1], &hp2[0], &hp2[1], &ap3, &lp1[0], &lp1[1], &hp1[0], &hp1[1], &ap1, &lp3[0], &lp3[1], &hp3[0], &hp3[1] })
        f->reset();
    env.fill (0.0f);
    analog.prepare (sr);
    for (int b = 0; b < numBands; ++b)
    {
        makeup[(size_t) b].reset (sr, 0.03);
        makeup[(size_t) b].setCurrentAndTargetValue (dsp::db2a (bp[(size_t) b][pGain]->load()));
    }
    outS.reset (sr, 0.03); outS.setCurrentAndTargetValue (dsp::db2a (pOut->load()));
    mixS.reset (sr, 0.03); mixS.setCurrentAndTargetValue (pMix->load() * 0.01f);
}

void MultiCompProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    const int nch = std::min (2, getMainBusNumInputChannels());
    if (nch == 0) return;
    float* L = buf.getWritePointer (0);
    float* R = buf.getWritePointer (nch > 1 ? 1 : 0);

    if (pBypass->load() > 0.5f)
    {
        analyzer.push (L, nch > 1 ? R : nullptr, n);
        return;
    }

    float x1, x2, x3;
    crossovers (x1, x2, x3);
    if (x1 != fx1) { fx1 = x1; for (auto& f : lp1) f.lowPass (sr, x1, 0.7071); for (auto& f : hp1) f.highPass (sr, x1, 0.7071); ap1.allPass (sr, x1, 0.7071); }
    if (x2 != fx2) { fx2 = x2; for (auto& f : lp2) f.lowPass (sr, x2, 0.7071); for (auto& f : hp2) f.highPass (sr, x2, 0.7071); }
    if (x3 != fx3) { fx3 = x3; for (auto& f : lp3) f.lowPass (sr, x3, 0.7071); for (auto& f : hp3) f.highPass (sr, x3, 0.7071); ap3.allPass (sr, x3, 0.7071); }

    struct B { float thr, ratio, att, rel; bool solo, byp; };
    std::array<B, numBands> bs;
    bool anySolo = false;
    for (int b = 0; b < numBands; ++b)
    {
        const auto& p = bp[(size_t) b];
        bs[(size_t) b] = { p[pThr]->load(), std::max (1.0f, p[pRatio]->load()), dsp::coef (p[pAtt]->load(), sr), dsp::coef (p[pRel]->load(), sr),
                           p[pSolo]->load() > 0.5f, p[pByp]->load() > 0.5f };
        anySolo = anySolo || bs[(size_t) b].solo;
        makeup[(size_t) b].setTargetValue (dsp::db2a (p[pGain]->load()));
    }
    outS.setTargetValue (dsp::db2a (pOut->load()));
    mixS.setTargetValue (pMix->load() * 0.01f);
    analog.beginBlock();

    std::array<float, numBands> grMin {};
    float outPk = 0;
    for (int i = 0; i < n; ++i)
    {
        float band[numBands][2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = c == 0 ? L[i] : R[i];
            // Aufteilen bei x2, dann unten bei x1 und oben bei x3 (mit Allpass-Ausgleich)
            float lo = lp2[1].process (lp2[0].process (x, c), c);
            float hi = hp2[1].process (hp2[0].process (x, c), c);
            lo = ap3.process (lo, c);
            hi = ap1.process (hi, c);
            band[0][c] = lp1[1].process (lp1[0].process (lo, c), c);
            band[1][c] = hp1[1].process (hp1[0].process (lo, c), c);
            band[2][c] = lp3[1].process (lp3[0].process (hi, c), c);
            band[3][c] = hp3[1].process (hp3[0].process (hi, c), c);
        }
        float ol = 0, orr = 0;
        for (int b = 0; b < numBands; ++b)
        {
            const auto& s = bs[(size_t) b];
            const float lvl = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
            const float xdb = dsp::a2db (lvl);
            const float target = s.byp ? 0.0f : gainComputer (xdb, s.thr, s.ratio, 6.0f) - xdb;
            auto& e = env[(size_t) b];
            e = target < e ? s.att * e + (1 - s.att) * target : s.rel * e + (1 - s.rel) * target;
            grMin[(size_t) b] = std::min (grMin[(size_t) b], e);
            const float g = dsp::db2a (e) * makeup[(size_t) b].getNextValue();
            if (anySolo && ! s.solo) continue;
            ol += band[b][0] * g; orr += band[b][1] * g;
        }
        const float m = mixS.getNextValue(), og = outS.getNextValue();
        // Dry für den Mix: Summe der Bänder ohne Bearbeitung entspricht dem Allpass-gefilterten Eingang
        const float dl = band[0][0] + band[1][0] + band[2][0] + band[3][0];
        const float dr = band[0][1] + band[1][1] + band[2][1] + band[3][1];
        ol = (m * ol + (1 - m) * (anySolo ? ol : dl)) * og;
        orr = (m * orr + (1 - m) * (anySolo ? orr : dr)) * og;
        if (analog.active()) { analog.advance(); ol = analog.tick (ol, 0); orr = analog.tick (orr, 1); }
        L[i] = ol;
        if (nch > 1) R[i] = orr;
        outPk = std::max (outPk, std::max (std::abs (ol), std::abs (orr)));
    }
    for (int b = 0; b < numBands; ++b) atomicMin (bandGr[(size_t) b], grMin[(size_t) b]);
    atomicMax (outPeak, outPk);
    analyzer.push (L, nch > 1 ? R : nullptr, n);
}

juce::AudioProcessorEditor* MultiCompProcessor::createEditor() { return new MultiCompEditor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MultiCompProcessor(); }
#endif

// ======================================================================== Editor
namespace
{
const juce::Rectangle<float> rDisplay { 16, 50, 868, 236 };
Colour bandColour (int b) { static const int idx[] = { 0, 2, 4, 6 }; return mc::band (idx[b]); }
}

MultiCompEditor::MultiCompEditor (MultiCompProcessor& p) : ModernEditor (p, p.apvts, "MULTICOMP", 900, 470), proc (p)
{
    display = rDisplay;
    addHelp ({ { "GAIN", "Pegel des gewählten Bandes nach der Kompression." },
               { "X-OVER 1", "Grenze zwischen LOW und LOW-MID. Auch im Display ziehbar." },
               { "X-OVER 2", "Grenze zwischen LOW-MID und HIGH-MID. Auch im Display ziehbar." },
               { "X-OVER 3", "Grenze zwischen HIGH-MID und HIGH. Auch im Display ziehbar." },
               { "SOLO", "Nur dieses Band hören." },
               { "BAND AUS", "Dieses Band nicht komprimieren (läuft unbearbeitet durch)." },
               { "LOW", "Bassband auswählen." }, { "LOW-MID", "Unteres Mittenband auswählen (Mulm, Körper)." },
               { "HIGH-MID", "Oberes Mittenband auswählen (Präsenz, Härte)." }, { "HIGH", "Höhenband auswählen (Luft, Zischen)." } });
    for (int b = 0; b < MultiCompProcessor::numBands; ++b)
        buttons.push_back ({ { 28.0f + b * 78.0f, 306, 72, 20 }, [b] { return String (MultiCompProcessor::bandNames[b]); },
                             [this, b] { return sel == b; }, [this, b] { selectBand (b); }, bandColour (b) });
    buttons.push_back ({ { 344, 306, 56, 20 }, [] { return String ("SOLO"); }, [this] { return flag (MultiCompProcessor::id (sel, "solo")); },
                         [this] { toggle (MultiCompProcessor::id (sel, "solo")); }, mc::amber() });
    buttons.push_back ({ { 406, 306, 56, 20 }, [] { return String ("BAND AUS"); }, [this] { return flag (MultiCompProcessor::id (sel, "byp")); },
                         [this] { toggle (MultiCompProcessor::id (sel, "byp")); }, modern::warm() });
    addAnalogStrip();
    stripReady = true;
    selectBand (0);
}

void MultiCompEditor::selectBand (int b)
{
    sel = b;
    const auto c = bandColour (b);
    knobs.clear();
    const float y = 372;
    knobs.push_back ({ MultiCompProcessor::id (b, "thr"), "THRESHOLD", 62, y, 22, c, [] (float v) { return fmt::db (v); } });
    knobs.push_back ({ MultiCompProcessor::id (b, "ratio"), "RATIO", 138, y, 22, c, [] (float v) { return fmt::ratio (v); } });
    knobs.push_back ({ MultiCompProcessor::id (b, "attack"), "ATTACK", 214, y, 22, c, [] (float v) { return fmt::ms (v); } });
    knobs.push_back ({ MultiCompProcessor::id (b, "release"), "RELEASE", 290, y, 22, c, [] (float v) { return fmt::ms (v); } });
    knobs.push_back ({ MultiCompProcessor::id (b, "gain"), "GAIN", 366, y, 22, c, [] (float v) { return fmt::db (v); }, true });
    knobs.push_back ({ "x1", "X-OVER 1", 512, y, 20, modern::accent(), [] (float v) { return fmt::hz (v); } });
    knobs.push_back ({ "x2", "X-OVER 2", 588, y, 20, modern::accent(), [] (float v) { return fmt::hz (v); } });
    knobs.push_back ({ "x3", "X-OVER 3", 664, y, 20, modern::accent(), [] (float v) { return fmt::hz (v); } });
    knobs.push_back ({ "mix", "MIX", 744, y, 22, mc::green(), [] (float v) { return fmt::pct (v); } });
    knobs.push_back ({ "output", "OUTPUT", 822, y, 22, modern::accent(), [] (float v) { return fmt::db (v); }, true });
    if (stripReady) { MKnob d { "drive", "DRIVE", 548, H - 27, 11, modern::warm(), [] (float v) { return fmt::pct (v); } }; d.compact = true; knobs.push_back (d); }
    repaint();
}

void MultiCompEditor::tick()
{
    proc.analyzer.update();
    proc.outPeak.exchange (0.0f);
    for (int b = 0; b < MultiCompProcessor::numBands; ++b)
    {
        const float g = proc.bandGr[(size_t) b].exchange (0.0f);
        auto& d = gr[(size_t) b];
        d = g < d ? g : d + (g - d) * 0.25f;
    }
}

float MultiCompEditor::xoverX (int k) const
{
    float x1, x2, x3;
    proc.crossovers (x1, x2, x3);
    return freqToX (k == 0 ? x1 : (k == 1 ? x2 : x3), display);
}

int MultiCompEditor::hitXover (juce::Point<float> p) const
{
    for (int k = 0; k < 3; ++k)
        if (std::abs (p.x - xoverX (k)) < 7) return k;
    return -1;
}

void MultiCompEditor::drawBackground (juce::Graphics& g)
{
    section (g, { 16, 296, 868, 126 });
    g.setColour (rgb (255, 255, 255, 0.05f));
    g.fillRect (470.0f, 310.0f, 1.0f, 100.0f);
}

void MultiCompEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float> r)
{
    float xs[5] = { r.getX(), xoverX (0), xoverX (1), xoverX (2), r.getRight() };
    bool anySolo = false;
    for (int b = 0; b < MultiCompProcessor::numBands; ++b) anySolo = anySolo || flag (MultiCompProcessor::id (b, "solo"));

    // Bandflächen
    for (int b = 0; b < MultiCompProcessor::numBands; ++b)
    {
        const auto c = bandColour (b);
        const bool muted = anySolo && ! flag (MultiCompProcessor::id (b, "solo"));
        g.setColour (c.withAlpha (b == sel ? 0.10f : (muted ? 0.0f : 0.04f)));
        g.fillRect (xs[b], r.getY(), xs[b + 1] - xs[b], r.getHeight());
    }
    drawFreqGrid (g, r);

    auto toY = [r] (float db) { return r.getBottom() - (db + 90.0f) / 90.0f * r.getHeight(); };
    auto toF = [r] (float x) { return xToFreq (x, r); };
    g.setColour (rgb (120, 140, 175, 0.20f)); g.fillPath (proc.analyzer.path (r, toF, toY, true));
    g.setColour (rgb (150, 170, 205, 0.5f)); g.strokePath (proc.analyzer.path (r, toF, toY, false), juce::PathStrokeType (1.0f));

    // Gain Reduction pro Band von oben
    for (int b = 0; b < MultiCompProcessor::numBands; ++b)
    {
        const auto c = bandColour (b);
        const float w = xs[b + 1] - xs[b];
        const float grDb = gr[(size_t) b];
        const float h = (r.getHeight() - 60) * juce::jlimit (0.0f, 1.0f, -grDb / 24.0f);
        g.setColour (c.withAlpha (0.45f));
        g.fillRect (xs[b] + 3, r.getY(), w - 6, h);
        g.setColour (c); g.fillRect (xs[b] + 3, r.getY() + h, w - 6, 2.0f);
        g.setFont (font (w > 90 ? 11.0f : 9.0f));
        g.setColour (b == sel ? juce::Colours::white : modern::text().withAlpha (0.75f));
        ctext (g, MultiCompProcessor::bandNames[b], xs[b] + w * 0.5f, r.getBottom() - 46, 11);
        g.setFont (font (w > 90 ? 10.0f : 8.0f, false)); g.setColour (c);
        String info = flag (MultiCompProcessor::id (b, "byp")) ? String ("AUS") : String (grDb, 1) + " dB";
        if (flag (MultiCompProcessor::id (b, "solo"))) info << "  S";
        ctext (g, info, xs[b] + w * 0.5f, r.getBottom() - 32, 10);
    }

    // Weichen
    for (int k = 0; k < 3; ++k)
    {
        const bool hv = k == hoverX || k == dragX;
        g.setColour (hv ? juce::Colours::white : rgb (255, 255, 255, 0.45f));
        g.fillRect (xs[k + 1] - (hv ? 1.0f : 0.5f), r.getY(), hv ? 2.0f : 1.0f, r.getHeight());
        g.fillRoundedRectangle (xs[k + 1] - 4, r.getCentreY() - 14, 8, 28, 4);
    }
}

void MultiCompEditor::drawOverlay (juce::Graphics& g)
{
    const int k = dragX >= 0 ? dragX : hoverX;
    if (k >= 0)
    {
        float x1, x2, x3;
        proc.crossovers (x1, x2, x3);
        headerInfo (g, "Weiche " + String (k + 1) + ": " + fmt::hz (k == 0 ? x1 : (k == 1 ? x2 : x3)));
    }
    else headerInfo (g, "Weichen ziehen  -  Band anklicken zum Bearbeiten");
}

void MultiCompEditor::displayMove (juce::Point<float> p) { hoverX = display.contains (p) ? hitXover (p) : -1; }

bool MultiCompEditor::displayDown (juce::Point<float> p, const juce::MouseEvent&)
{
    const int k = hitXover (p);
    if (k >= 0)
    {
        dragX = k;
        beginEdit (k == 0 ? "x1" : (k == 1 ? "x2" : "x3"));
        return true;
    }
    int b = 0;
    while (b < 3 && p.x > xoverX (b)) ++b;
    selectBand (b);
    return false;
}

void MultiCompEditor::displayDrag (juce::Point<float> p, const juce::MouseEvent&)
{
    if (dragX < 0) return;
    static const float lo[] = { 20, 150, 1000 }, hi[] = { 1000, 6000, 16000 };
    edit (dragX == 0 ? "x1" : (dragX == 1 ? "x2" : "x3"), juce::jlimit (lo[dragX], hi[dragX], xToFreq (p.x, display)));
}

void MultiCompEditor::displayUp (juce::Point<float>)
{
    if (dragX < 0) return;
    endEdit (dragX == 0 ? "x1" : (dragX == 1 ? "x2" : "x3"));
    dragX = -1;
}
