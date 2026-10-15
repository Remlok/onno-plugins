#include "OnnoTame.h"
#include "Common/OnnoDSP.h"

using namespace onno;
using juce::String;

const juce::StringArray TameProcessor::profileNames { "Custom", "Rhythm Gtr", "Lead Gtr", "Scream", "Clean Vox", "Snare", "Overheads", "Bass", "Mix Bus" };

namespace
{
struct Profile { float depth, sharp, sel, att, rel, low, high; float nf[4], ng[4]; };
// Startpunkte für Metal-Quellen (Knoten: Frequenz, Empfindlichkeit in dB)
const Profile profiles[] = {
    { 50, 50, 4, 5, 80, 120, 16000, { 250, 1000, 3500, 8000 }, { 0, 0, 0, 0 } },          // Custom (Standard)
    { 45, 55, 3, 5, 60, 150, 9000, { 280, 1000, 3800, 7000 }, { 4, -3, 5, 2 } },         // Rhythmusgitarre: Mulm und Fizz
    { 40, 65, 3, 4, 70, 400, 11000, { 700, 1500, 2800, 5500 }, { -2, 0, 4, 3 } },       // Leadgitarre: harte Präsenz
    { 55, 45, 3, 3, 50, 700, 13000, { 900, 2500, 4500, 8000 }, { -2, 5, 3, 4 } },       // Scream: Schärfe und Zischen
    { 35, 50, 4, 5, 80, 180, 13000, { 320, 1200, 3000, 7500 }, { 2, -1, 3, 3 } },       // Clean-Gesang: Boxiness, Härte, S
    { 50, 75, 3, 2, 60, 150, 10000, { 220, 900, 3000, 6000 }, { 4, 3, 0, 1 } },         // Snare: Ringen der Felle
    { 45, 50, 3, 3, 90, 1500, 16000, { 2500, 4500, 8000, 12000 }, { 1, 3, 4, 2 } },     // Overheads: Becken-Schärfe
    { 35, 60, 4, 8, 100, 40, 4000, { 90, 250, 800, 2000 }, { 2, 0, 3, 1 } },            // Bass: Dröhnen und Klacken
    { 20, 50, 5, 8, 120, 100, 16000, { 250, 1000, 3500, 8000 }, { 0, 0, 2, 1 } },       // Summe: sanft
};
const float defNodeF[4] = { 250, 1000, 3500, 8000 };
}

juce::AudioProcessorValueTreeState::ParameterLayout TameProcessor::layout()
{
    prm::Layout p;
    prm::lin (p, "depth", "Depth", 0, 100, 50, "%");
    prm::lin (p, "sharp", "Sharpness", 0, 100, 50, "%");
    prm::lin (p, "sel", "Selectivity", 0, 20, 4, "dB");
    prm::lg (p, "attack", "Attack", 1, 100, 5, "ms");
    prm::lg (p, "release", "Release", 10, 500, 80, "ms");
    prm::lg (p, "low", "Range Low", 20, 2000, 120, "Hz");
    prm::lg (p, "high", "Range High", 1000, 20000, 16000, "Hz");
    prm::lin (p, "mix", "Mix", 0, 100, 100, "%");
    prm::lin (p, "output", "Output", -12, 12, 0, "dB");
    for (int n = 0; n < numNodes; ++n)
    {
        prm::lg (p, nid (n, "freq"), "Node " + String (n + 1) + " Freq", 20, 20000, defNodeF[n], "Hz");
        prm::lin (p, nid (n, "gain"), "Node " + String (n + 1) + " Sensitivity", -12, 12, 0, "dB");
    }
    prm::ch (p, "profile", "Profile", profileNames, 0, false);
    prm::bl (p, "delta", "Delta", false);
    prm::bl (p, "link", "Stereo Link", true);
    prm::bl (p, "bypass", "Bypass", false);
    return prm::done (p);
}

TameProcessor::TameProcessor() : ModernProcessor ("Onno Tame", "OnnoTame", stereoBuses(), layout())
{
    pDepth = raw ("depth"); pSharp = raw ("sharp"); pSel = raw ("sel"); pAtt = raw ("attack"); pRel = raw ("release");
    pLow = raw ("low"); pHigh = raw ("high"); pMix = raw ("mix"); pOut = raw ("output");
    pDelta = raw ("delta"); pLink = raw ("link"); pBypass = raw ("bypass");
    for (int n = 0; n < numNodes; ++n) { pNodeF[(size_t) n] = raw (nid (n, "freq")); pNodeG[(size_t) n] = raw (nid (n, "gain")); }
    for (auto& g : grDisplay) g.store (0.0f);
}

void TameProcessor::applyProfile (int pr)
{
    pr = juce::jlimit (0, (int) std::size (profiles) - 1, pr);
    const auto& P = profiles[pr];
    auto set = [this] (const String& id, float v)
    {
        auto* prm = apvts.getParameter (id);
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture();
    };
    set ("depth", P.depth); set ("sharp", P.sharp); set ("sel", P.sel); set ("attack", P.att); set ("release", P.rel);
    set ("low", P.low); set ("high", P.high);
    for (int n = 0; n < numNodes; ++n) { set (nid (n, "freq"), P.nf[n]); set (nid (n, "gain"), P.ng[n]); }
    set ("profile", (float) pr);
}

float TameProcessor::sensitivityAt (float f) const
{
    float s = 0;
    for (int n = 0; n < numNodes; ++n)
    {
        const float d = std::log2 (std::max (f, 1.0f) / pNodeF[(size_t) n]->load()) / 0.6f;
        s += pNodeG[(size_t) n]->load() * std::exp (-0.5f * d * d);
    }
    return s;
}

void TameProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    order = sr <= 50000 ? 10 : (sr <= 100000 ? 11 : 12);
    N = 1 << order; hop = N / 4;
    fft = std::make_unique<juce::dsp::FFT> (order);
    win.resize ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / N));
    const size_t K = (size_t) N / 2 + 1;
    for (int c = 0; c < 2; ++c)
    {
        inRing[c].assign ((size_t) N, 0.0f);
        outAcc[c].assign ((size_t) N, 0.0f);
        work[c].assign ((size_t) N * 2, 0.0f);
        pdb[c].assign (K, -120.0f);
        gdb[c].assign (K, 0.0f);
        state[c].assign (K, 0.0f);
        det[c].assign (K, 0.0f);
    }
    sens.assign (K, 0.0f);
    prefix.assign (K + 1, 0.0f);
    lo.assign (K, 0); hi.assign (K, 0);
    lastWidth = -1;
    pos = hopCount = 0;
    analyzer.prepare (sr);
    setLatencySamples (N);
    mixS.reset (sr, 0.03); mixS.setCurrentAndTargetValue (pMix->load() * 0.01f);
    outS.reset (sr, 0.03); outS.setCurrentAndTargetValue (dsp::db2a (pOut->load()));
}

void TameProcessor::computeGains (const float* p, float* g, std::vector<float>& st)
{
    const int K = N / 2 + 1;
    const float binHz = (float) sr / N;

    // Breite der Hüllkurve: weich = breite Hüllkurve (fängt auch breite Buckel), scharf = nur schmale Spitzen
    const float width = 3.0f - 2.5f * pSharp->load() * 0.01f;
    if (std::abs (width - lastWidth) > 1.0e-3f)
    {
        lastWidth = width;
        const float r = std::pow (2.0f, width * 0.5f);
        for (int k = 0; k < K; ++k)
        {
            lo[(size_t) k] = juce::jlimit (1, K - 1, (int) std::floor ((float) k / r));
            hi[(size_t) k] = juce::jlimit (1, K - 1, (int) std::ceil ((float) k * r));
            if (hi[(size_t) k] - lo[(size_t) k] < 2) hi[(size_t) k] = std::min (K - 1, lo[(size_t) k] + 2);
        }
    }

    // Pegel unterhalb der Rauschgrenze zählen nicht (sonst "findet" das Plugin Resonanzen in der Stille)
    const float floorDb = 20.0f * std::log10 (N * 0.25f) - 80.0f;
    prefix[0] = 0;
    for (int k = 0; k < K; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + std::max (p[k], floorDb);

    const float d = pDepth->load() * 0.01f, sel = pSel->load();
    const float amount = std::min (1.0f, 1.5f * d), maxRed = 36.0f * d;
    const float low = pLow->load(), high = pHigh->load();
    const float frameSec = (float) hop / (float) sr;
    const float att = std::exp (-frameSec / (pAtt->load() * 0.001f)), rel = std::exp (-frameSec / (pRel->load() * 0.001f));

    for (int k = 1; k < K; ++k)
    {
        const float f = k * binHz;
        float w = 1.0f;
        if (f < low) w = juce::jlimit (0.0f, 1.0f, 1.0f - 3.0f * std::log2 (low / f));
        else if (f > high) w = juce::jlimit (0.0f, 1.0f, 1.0f - 3.0f * std::log2 (f / high));
        float target = 0;
        if (w > 0 && p[k] > floorDb)
        {
            const float env = (prefix[(size_t) hi[(size_t) k] + 1] - prefix[(size_t) lo[(size_t) k]]) / (float) (hi[(size_t) k] - lo[(size_t) k] + 1);
            const float excess = p[k] - env - sel + sens[(size_t) k];
            if (excess > 0) target = -std::min (maxRed, excess * amount) * w;
        }
        auto& s = st[(size_t) k];
        s = target < s ? att * s + (1 - att) * target : rel * s + (1 - rel) * target;
    }
    // leicht über die Frequenz glätten (gegen "musikalisches Rauschen")
    g[0] = 0;
    for (int k = 1; k < K - 1; ++k) g[k] = 0.25f * st[(size_t) k - 1] + 0.5f * st[(size_t) k] + 0.25f * st[(size_t) k + 1];
    g[K - 1] = st[(size_t) K - 1];
}

// Erkennungsspektrum: Leistung je Bin leicht über Frequenz und Zeit (~12 ms) geglättet, dann in dB.
// Ohne Glättung schwanken einzelne Bins bei Rauschen so stark, dass Zufallsspitzen als Resonanz gelten.
void TameProcessor::detect (int c, const std::vector<float>& power)
{
    const int K = N / 2 + 1;
    const float a = std::exp (-(float) hop / ((float) sr * 0.012f));
    auto& dt = det[c];
    auto& out = pdb[c];
    float prev = power[0];
    for (int k = 0; k < K; ++k)
    {
        const float cur = power[(size_t) k], next = k + 1 < K ? power[(size_t) k + 1] : cur;
        const float sm = 0.25f * prev + 0.5f * cur + 0.25f * next;
        prev = cur;
        dt[(size_t) k] = a * dt[(size_t) k] + (1 - a) * sm;
    }
    for (int k = 0; k < K; ++k) out[(size_t) k] = 10.0f * std::log10 (dt[(size_t) k] + 1.0e-12f);
}

void TameProcessor::processFrame()
{
    const int K = N / 2 + 1;
    const float binHz = (float) sr / N;
    for (int k = 0; k < K; ++k) sens[(size_t) k] = sensitivityAt (k * binHz);

    for (int c = 0; c < nch; ++c)
    {
        auto& w = work[c];
        for (int i = 0; i < N; ++i) w[(size_t) i] = inRing[c][(size_t) ((pos + i) % N)] * win[(size_t) i];
        std::fill (w.begin() + N, w.end(), 0.0f);
        fft->performRealOnlyForwardTransform (w.data(), true);
        for (int k = 0; k < K; ++k)
        {
            const float re = w[(size_t) (2 * k)], im = w[(size_t) (2 * k + 1)];
            pdb[c][(size_t) k] = re * re + im * im;
        }
    }

    const bool link = pLink->load() > 0.5f || nch < 2;
    if (link)
    {
        if (nch > 1)
            for (int k = 0; k < K; ++k) pdb[0][(size_t) k] = 0.5f * (pdb[0][(size_t) k] + pdb[1][(size_t) k]);
        detect (0, pdb[0]);
        computeGains (pdb[0].data(), gdb[0].data(), state[0]);
        if (nch > 1) gdb[1] = gdb[0];
    }
    else
        for (int c = 0; c < nch; ++c)
        {
            detect (c, pdb[c]);
            computeGains (pdb[c].data(), gdb[c].data(), state[c]);
        }

    // Anzeige: stärkste Reduktion je Band
    dispMin.fill (0.0f);
    for (int k = 1; k < K; ++k)
    {
        const float f = k * binHz;
        if (f < 20.0f || f >= 20000.0f) continue;
        const int b = juce::jlimit (0, numDisplayBands - 1, (int) (std::log (f / 20.0f) / std::log (1000.0f) * numDisplayBands));
        dispMin[(size_t) b] = std::min (dispMin[(size_t) b], std::min (gdb[0][(size_t) k], nch > 1 ? gdb[1][(size_t) k] : 0.0f));
    }
    for (int b = 0; b < numDisplayBands; ++b) atomicMin (grDisplay[(size_t) b], dispMin[(size_t) b]);

    // Verstärkung anwenden, zurücktransformieren, überlappend addieren
    for (int c = 0; c < nch; ++c)
    {
        auto& w = work[c];
        for (int k = 0; k < K; ++k)
        {
            const float gl = dsp::db2a (gdb[c][(size_t) k]);
            w[(size_t) (2 * k)] *= gl; w[(size_t) (2 * k + 1)] *= gl;
        }
        fft->performRealOnlyInverseTransform (w.data());
        // sqrt-Hann zweimal = Hann; bei 75 % Überlappung summiert sich Hann zu 2
        for (int i = 0; i < N; ++i)
            outAcc[c][(size_t) ((pos + i) % N)] += w[(size_t) i] * win[(size_t) i] * 0.5f;
    }
}

void TameProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    nch = std::min (2, getMainBusNumInputChannels());
    if (nch == 0) return;
    analyzer.push (buf.getReadPointer (0), nch > 1 ? buf.getReadPointer (1) : nullptr, n);

    const bool bypass = pBypass->load() > 0.5f, delta = pDelta->load() > 0.5f;
    mixS.setTargetValue (pMix->load() * 0.01f);
    outS.setTargetValue (dsp::db2a (pOut->load()));
    float outPk = 0;

    for (int i = 0; i < n; ++i)
    {
        const float m = mixS.getNextValue(), og = outS.getNextValue();
        for (int c = 0; c < nch; ++c)
        {
            float* d = buf.getWritePointer (c);
            const float dryD = inRing[c][(size_t) pos];      // Eingang von vor N Samples = Latenz
            inRing[c][(size_t) pos] = d[i];
            const float wet = outAcc[c][(size_t) pos];
            outAcc[c][(size_t) pos] = 0;
            float y;
            if (bypass) y = dryD;
            else if (delta) y = (dryD - wet) * og;
            else y = (m * wet + (1 - m) * dryD) * og;
            d[i] = y;
            outPk = std::max (outPk, std::abs (y));
        }
        if (++pos >= N) pos = 0;
        if (++hopCount >= hop) { hopCount = 0; processFrame(); }
    }
    atomicMax (outPeak, outPk);
}

juce::AudioProcessorEditor* TameProcessor::createEditor() { return new TameEditor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TameProcessor(); }
#endif

// ======================================================================== Editor
namespace
{
const juce::Rectangle<float> rDisplay { 16, 50, 868, 236 };
}

TameEditor::TameEditor (TameProcessor& p) : ModernEditor (p, p.apvts, "TAME", 900, 430), proc (p)
{
    display = rDisplay;
    addBypass();
    for (int i = 1; i < TameProcessor::profileNames.size(); ++i)
        buttons.push_back ({ { 28.0f + (i - 1) * 105.0f, 306, 99, 20 }, [i] { return TameProcessor::profileNames[i].toUpperCase(); },
                             [this, i] { return (int) value ("profile") == i; }, [this, i] { proc.applyProfile (i); }, mc::amber() });
    buttons.push_back ({ { 776, 340, 96, 20 }, [] { return String ("DELTA"); }, [this] { return flag ("delta"); },
                         [this] { toggle ("delta"); }, modern::warm() });
    buttons.push_back ({ { 776, 368, 96, 20 }, [] { return String ("STEREO LINK"); }, [this] { return flag ("link"); },
                         [this] { toggle ("link"); }, mc::blue() });
    const float y = 376;
    knobs.push_back ({ "depth", "DEPTH", 62, y, 24, mc::red(), [] (float v) { return fmt::pct (v); } });
    knobs.push_back ({ "sharp", "SHARPNESS", 152, y, 22, mc::amber(), [] (float v) { return fmt::pct (v); } });
    knobs.push_back ({ "sel", "SELECTIVITY", 242, y, 22, mc::amber(), [] (float v) { return fmt::db (v); } });
    knobs.push_back ({ "attack", "ATTACK", 332, y, 22, mc::blue(), [] (float v) { return fmt::ms (v); } });
    knobs.push_back ({ "release", "RELEASE", 422, y, 22, mc::blue(), [] (float v) { return fmt::ms (v); } });
    knobs.push_back ({ "mix", "MIX", 552, y, 22, mc::green(), [] (float v) { return fmt::pct (v); } });
    knobs.push_back ({ "output", "OUTPUT", 642, y, 22, modern::accent(), [] (float v) { return fmt::db (v); }, true });
}

void TameEditor::tick()
{
    proc.analyzer.update (1.5f);
    proc.outPeak.exchange (0.0f);
    for (int b = 0; b < TameProcessor::numDisplayBands; ++b)
    {
        const float g = proc.grDisplay[(size_t) b].exchange (0.0f);
        auto& d = gr[(size_t) b];
        d = g < d ? g : d + (g - d) * 0.2f;
    }
}

float TameEditor::sensToY (float db) const { return display.getY() + display.getHeight() * 0.62f - db / 12.0f * display.getHeight() * 0.26f; }

juce::Point<float> TameEditor::nodePos (int n) const
{
    return { freqToX (value (TameProcessor::nid (n, "freq")), display), sensToY (value (TameProcessor::nid (n, "gain"))) };
}

int TameEditor::hitObject (juce::Point<float> p) const
{
    for (int n = 0; n < TameProcessor::numNodes; ++n)
        if (nodePos (n).getDistanceFrom (p) < 11) return n;
    if (std::abs (p.x - freqToX (value ("low"), display)) < 6) return 4;
    if (std::abs (p.x - freqToX (value ("high"), display)) < 6) return 5;
    return -1;
}

void TameEditor::drawBackground (juce::Graphics& g)
{
    section (g, { 16, 296, 868, 126 });
    g.setColour (rgb (255, 255, 255, 0.05f));
    g.fillRect (487.0f, 340.0f, 1.0f, 70.0f);
    g.fillRect (762.0f, 340.0f, 1.0f, 70.0f);
}

void TameEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawFreqGrid (g, r);

    // Eingangsspektrum
    auto toY = [r] (float db) { return r.getBottom() - (db + 90.0f) / 90.0f * r.getHeight(); };
    auto toF = [r] (float x) { return xToFreq (x, r); };
    g.setColour (rgb (120, 140, 175, 0.22f)); g.fillPath (proc.analyzer.path (r, toF, toY, true));
    g.setColour (rgb (150, 170, 205, 0.5f)); g.strokePath (proc.analyzer.path (r, toF, toY, false), juce::PathStrokeType (1.0f));

    // außerhalb des Bereichs abdunkeln
    const float xl = freqToX (value ("low"), r), xh = freqToX (value ("high"), r);
    g.setColour (rgb (0, 0, 0, 0.45f));
    g.fillRect (r.getX(), r.getY(), xl - r.getX(), r.getHeight());
    g.fillRect (xh, r.getY(), r.getRight() - xh, r.getHeight());
    for (int k = 0; k < 2; ++k)
    {
        const float x = k == 0 ? xl : xh;
        const bool hv = hoverObj == 4 + k || dragObj == 4 + k;
        g.setColour (hv ? juce::Colours::white : rgb (255, 255, 255, 0.5f));
        g.fillRect (x - 0.5f, r.getY(), hv ? 2.0f : 1.0f, r.getHeight());
        g.fillRoundedRectangle (x - 4, r.getY() + 10, 8, 24, 4);
    }

    // Reduktion: von oben nach unten, je tiefer desto stärker
    const float grH = r.getHeight() * 0.55f;
    juce::Path red;
    red.startNewSubPath (r.getX(), r.getY());
    for (int b = 0; b < TameProcessor::numDisplayBands; ++b)
        red.lineTo (freqToX (TameProcessor::displayBandFreq (b), r), r.getY() + grH * juce::jlimit (0.0f, 1.0f, -gr[(size_t) b] / 18.0f));
    red.lineTo (r.getRight(), r.getY());
    red.closeSubPath();
    g.setColour (mc::red().withAlpha (0.30f)); g.fillPath (red);
    g.setColour (mc::red()); g.strokePath (red, juce::PathStrokeType (1.5f));

    // Empfindlichkeitskurve mit Knoten
    g.setColour (rgb (255, 255, 255, 0.10f));
    g.fillRect (r.getX(), sensToY (0), r.getWidth(), 1.0f);
    juce::Path sc;
    for (float x = r.getX(); x <= r.getRight(); x += 2)
    {
        const float y = sensToY (proc.sensitivityAt (xToFreq (x, r)));
        if (x == r.getX()) sc.startNewSubPath (x, y); else sc.lineTo (x, y);
    }
    g.setColour (mc::amber().withAlpha (0.9f)); g.strokePath (sc, juce::PathStrokeType (1.8f));
    for (int n = 0; n < TameProcessor::numNodes; ++n)
    {
        const auto pnt = nodePos (n);
        const bool hv = hoverObj == n || dragObj == n;
        g.setColour (mc::amber().withAlpha (hv ? 0.35f : 0.18f)); g.fillEllipse (pnt.x - 12, pnt.y - 12, 24, 24);
        g.setColour (mc::amber()); g.fillEllipse (pnt.x - 7.5f, pnt.y - 7.5f, 15, 15);
        g.setColour (modern::bg()); g.setFont (font (9.5f));
        g.drawText (String (n + 1), juce::Rectangle<float> (pnt.x - 7.5f, pnt.y - 7.5f, 15, 15), juce::Justification::centred, false);
    }

    if (flag ("delta"))
    {
        g.setFont (font (12)); g.setColour (modern::warm());
        rtext (g, "DELTA: du hörst nur, was entfernt wird", r.getRight() - 12, r.getBottom() - 34, 12);
    }
}

void TameEditor::drawOverlay (juce::Graphics& g)
{
    const int o = dragObj >= 0 ? dragObj : hoverObj;
    if (o >= 0 && o < 4)
        headerInfo (g, "Knoten " + String (o + 1) + ": " + fmt::hz (value (TameProcessor::nid (o, "freq"))) + "   Empfindlichkeit "
                       + fmt::db (value (TameProcessor::nid (o, "gain"))));
    else if (o >= 4)
        headerInfo (g, String (o == 4 ? "Bereich unten: " : "Bereich oben: ") + fmt::hz (value (o == 4 ? "low" : "high")));
    else
        headerInfo (g, String::fromUTF8 ("Knoten hoch = empfindlicher  -  Ränder ziehen = Bereich  -  Doppelklick = Knoten auf 0"));
}

void TameEditor::displayMove (juce::Point<float> p) { hoverObj = display.contains (p) ? hitObject (p) : -1; }

bool TameEditor::displayDown (juce::Point<float> p, const juce::MouseEvent&)
{
    dragObj = hitObject (p);
    if (dragObj < 0) return false;
    if (dragObj < 4) { beginEdit (TameProcessor::nid (dragObj, "freq")); beginEdit (TameProcessor::nid (dragObj, "gain")); }
    else beginEdit (dragObj == 4 ? "low" : "high");
    return true;
}

void TameEditor::displayDrag (juce::Point<float> p, const juce::MouseEvent&)
{
    if (dragObj < 0) return;
    const float f = xToFreq (p.x, display);
    if (dragObj < 4)
    {
        edit (TameProcessor::nid (dragObj, "freq"), juce::jlimit (20.0f, 20000.0f, f));
        const float db = (display.getY() + display.getHeight() * 0.62f - p.y) / (display.getHeight() * 0.26f) * 12.0f;
        edit (TameProcessor::nid (dragObj, "gain"), juce::jlimit (-12.0f, 12.0f, db));
    }
    else if (dragObj == 4) edit ("low", juce::jlimit (20.0f, 2000.0f, std::min (f, value ("high") * 0.8f)));
    else edit ("high", juce::jlimit (1000.0f, 20000.0f, std::max (f, value ("low") * 1.25f)));
}

void TameEditor::displayUp (juce::Point<float>)
{
    if (dragObj < 0) return;
    if (dragObj < 4) { endEdit (TameProcessor::nid (dragObj, "freq")); endEdit (TameProcessor::nid (dragObj, "gain")); }
    else endEdit (dragObj == 4 ? "low" : "high");
    dragObj = -1;
}

void TameEditor::displayDoubleClick (juce::Point<float> p)
{
    const int o = hitObject (p);
    if (o >= 0 && o < 4) setValue (TameProcessor::nid (o, "gain"), 0);
}
