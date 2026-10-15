#include "OnnoComp.h"

using namespace onno;
using juce::String;

juce::AudioProcessorValueTreeState::ParameterLayout CompProcessor::layout()
{
    prm::Layout p;
    prm::lin (p, "thr", "Threshold", -60, 0, -18, "dB");
    prm::lg (p, "ratio", "Ratio", 1, 100, 4, ":1");
    prm::lin (p, "knee", "Knee", 0, 24, 3, "dB");
    prm::lg (p, "attack", "Attack", 0.1f, 200, 5, "ms");
    prm::lg (p, "release", "Release", 5, 2000, 100, "ms");
    prm::lin (p, "lookahead", "Lookahead", 0, 20, 0, "ms");
    prm::lin (p, "rms", "RMS Size", 0, 100, 0, "ms");
    prm::lg (p, "scHpf", "Detector HPF", 20, 1000, 20, "Hz");
    prm::lin (p, "mix", "Mix", 0, 100, 100, "%");
    prm::lin (p, "output", "Output", -24, 24, 0, "dB");
    prm::ch (p, "source", "Detector Input", { "Main", "Sidechain" }, 0);
    prm::bl (p, "autoGain", "Auto Makeup", false);
    prm::bl (p, "limit", "Limit Output", false);
    Analog::addParams (p);
    prm::bl (p, "bypass", "Bypass", false);
    return prm::done (p);
}

CompProcessor::CompProcessor() : ModernProcessor ("Onno Comp", "OnnoComp", stereoBuses (true), layout())
{
    pThr = raw ("thr"); pRatio = raw ("ratio"); pKnee = raw ("knee"); pAtt = raw ("attack"); pRel = raw ("release");
    pLa = raw ("lookahead"); pRms = raw ("rms"); pHpf = raw ("scHpf"); pMix = raw ("mix"); pOut = raw ("output");
    pSrc = raw ("source"); pAuto = raw ("autoGain"); pLimit = raw ("limit"); pBypass = raw ("bypass");
    analog.attach (*this);
}

void CompProcessor::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& d : dl) d.assign ((size_t) (0.021 * sr) + 2, 0.0f);
    dlPos = 0; env = 0; ms = 0; hpfFreq = -1;
    hpf.reset();
    analog.prepare (sr);
    la = (int) std::round (pLa->load() * 0.001 * sr);
    setLatencySamples (la);
    outGain.reset (sr, 0.03); outGain.setCurrentAndTargetValue (dsp::db2a (pOut->load()));
    mixS.reset (sr, 0.03); mixS.setCurrentAndTargetValue (pMix->load() * 0.01f);
}

float CompProcessor::autoMakeup() const
{
    // die halbe Reduktion bei 0 dBFS ausgleichen
    return -0.5f * gainComputer (0.0f, pThr->load(), std::max (1.0f, pRatio->load()), pKnee->load());
}

void CompProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    const int nch = std::min (2, getMainBusNumInputChannels());
    if (nch == 0) return;

    const int newLa = juce::jlimit (0, (int) dl[0].size() - 2, (int) std::round (pLa->load() * 0.001 * sr));
    if (newLa != la) { la = newLa; setLatencySamples (la); }

    const float thr = pThr->load(), ratio = std::max (1.0f, pRatio->load()), knee = pKnee->load();
    const float att = dsp::coef (pAtt->load(), sr), rel = dsp::coef (pRel->load(), sr);
    const float rmsMs = pRms->load();
    const float rmsC = rmsMs > 0.05f ? dsp::coef (rmsMs, sr) : 0.0f;
    const float hf = pHpf->load();
    if (std::abs (hf - hpfFreq) > 0.01f) { hpfFreq = hf; hpf.highPass (sr, std::min ((double) hf, sr * 0.45), 0.7071); }
    const bool useHpf = hf > 20.5f;
    const bool bypass = pBypass->load() > 0.5f;
    const float makeup = pAuto->load() > 0.5f ? autoMakeup() : 0.0f;
    const bool limit = pLimit->load() > 0.5f;
    outGain.setTargetValue (dsp::db2a (pOut->load()));
    mixS.setTargetValue (pMix->load() * 0.01f);
    analog.beginBlock();

    const float* scL = nullptr; const float* scR = nullptr;
    if (pSrc->load() > 0.5f) { scL = sidechain (buf, 0); scR = sidechain (buf, 1); }

    float* L = buf.getWritePointer (0);
    float* R = buf.getWritePointer (nch > 1 ? 1 : 0);
    const int len = (int) dl[0].size();
    float inPk = 0, outPk = 0, grMin = 0, detMax = 0;

    for (int i = 0; i < n; ++i)
    {
        const float xl = L[i], xr = R[i];
        float dL = scL ? scL[i] : xl, dR = scR ? scR[i] : xr;
        if (useHpf) { dL = hpf.process (dL, 0); dR = hpf.process (dR, 1); }
        float lvl = std::max (std::abs (dL), std::abs (dR));
        if (rmsC > 0) { ms = rmsC * ms + (1 - rmsC) * lvl * lvl; lvl = std::sqrt (ms); }
        detMax = std::max (detMax, lvl);
        const float xdb = dsp::a2db (lvl);
        const float target = gainComputer (xdb, thr, ratio, knee) - xdb;
        env = target < env ? att * env + (1 - att) * target : rel * env + (1 - rel) * target;

        // Lookahead: Audio verzögern
        dl[0][(size_t) dlPos] = xl; dl[1][(size_t) dlPos] = xr;
        int rp = dlPos - la; if (rp < 0) rp += len;
        const float yl = dl[0][(size_t) rp], yr = dl[1][(size_t) rp];
        if (++dlPos >= len) dlPos = 0;

        float g = 1.0f;
        const float og = outGain.getNextValue(), mx = mixS.getNextValue();
        if (! bypass)
        {
            g = (mx * dsp::db2a (env + makeup) + (1 - mx)) * og;
            grMin = std::min (grMin, env);
        }
        float ol = yl * g, orr = yr * g;
        if (! bypass && analog.active())
        {
            // Färbung nach Kompression und Mix, vor dem Limit
            analog.advance();
            ol = analog.tick (ol, 0); orr = analog.tick (orr, 1);
        }
        if (limit && ! bypass) { ol = juce::jlimit (-1.0f, 1.0f, ol); orr = juce::jlimit (-1.0f, 1.0f, orr); }
        L[i] = ol;
        if (nch > 1) R[i] = orr;
        inPk = std::max (inPk, std::max (std::abs (xl), std::abs (xr)));
        outPk = std::max (outPk, std::max (std::abs (ol), std::abs (orr)));
    }
    atomicMax (inPeak, inPk);
    atomicMax (outPeak, outPk);
    atomicMin (grPeak, grMin);
    atomicMax (detPeak, detMax);
}

juce::AudioProcessorEditor* CompProcessor::createEditor() { return new CompEditor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CompProcessor(); }
#endif

// ======================================================================== Editor
namespace
{
const juce::Rectangle<float> rDisplay { 16, 50, 868, 236 }, rCurve { 28, 62, 212, 212 }, rHist { 256, 62, 616, 212 };
float histY (float db) { return rHist.getY() + rHist.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); }
}

CompEditor::CompEditor (CompProcessor& p) : ModernEditor (p, p.apvts, "COMP", 900, 470), proc (p)
{
    display = rDisplay;
    addHelp ({ { "OUTPUT", "Pegel nach der Kompression (Make-up). Mit AUTO GAIN oder MATCH automatisch." },
               { "AUTO GAIN", "Gleicht die Pegelreduktion automatisch wieder aus." },
               { "LIMIT 0 dB", "Harte Grenze bei 0 dBFS am Ausgang, damit nichts übersteuert." } });
    const float y = 368, dx = 74;
    float x = 62;
    auto add = [&] (const char* id, const char* label, Colour c, std::function<String (float)> f, bool bip = false)
    {
        knobs.push_back ({ id, label, x, y, 22, c, f, bip });
        x += dx;
    };
    add ("thr", "THRESHOLD", mc::red(), [] (float v) { return fmt::db (v); });
    add ("ratio", "RATIO", mc::red(), [] (float v) { return fmt::ratio (v); });
    add ("knee", "KNEE", modern::accent(), [] (float v) { return fmt::db (v); });
    add ("attack", "ATTACK", mc::amber(), [] (float v) { return fmt::ms (v); });
    add ("release", "RELEASE", mc::amber(), [] (float v) { return fmt::ms (v); });
    add ("lookahead", "LOOKAHEAD", mc::purple(), [] (float v) { return v < 0.05f ? String ("AUS") : fmt::ms (v); });
    add ("rms", "RMS", mc::purple(), [] (float v) { return v < 0.05f ? String ("PEAK") : fmt::ms (v); });
    add ("scHpf", "DET. HPF", mc::blue(), [] (float v) { return v < 20.5f ? String ("AUS") : fmt::hz (v); });
    add ("mix", "MIX", mc::green(), [] (float v) { return fmt::pct (v); });
    add ("output", "OUTPUT", modern::accent(), [] (float v) { return fmt::db (v); }, true);

    buttons.push_back ({ { 776, 312, 96, 20 }, [this] { return String (value ("source") > 0.5f ? "SIDECHAIN" : "DETEKTOR: MAIN"); },
                         [this] { return value ("source") > 0.5f; }, [this] { toggle ("source"); }, mc::blue() });
    buttons.push_back ({ { 776, 340, 96, 20 }, [] { return String ("AUTO GAIN"); }, [this] { return flag ("autoGain"); },
                         [this] { toggle ("autoGain"); }, mc::green() });
    buttons.push_back ({ { 776, 368, 96, 20 }, [] { return String ("LIMIT 0 dB"); }, [this] { return flag ("limit"); },
                         [this] { toggle ("limit"); }, mc::red() });
    addAnalogStrip();
}

void CompEditor::tick()
{
    const float in = dsp::a2db (proc.inPeak.exchange (0.0f));
    const float out = dsp::a2db (proc.outPeak.exchange (0.0f));
    const float g = proc.grPeak.exchange (0.0f);
    const float d = dsp::a2db (proc.detPeak.exchange (0.0f));
    inH.push (in); outH.push (out); grH.push (g);
    det = d > det ? d : std::max (d, det - 2.0f);
    gr = g < gr ? g : gr + (g - gr) * 0.25f;
}

void CompEditor::drawBackground (juce::Graphics& g)
{
    section (g, { 16, 296, 868, 126 });
    g.setColour (rgb (255, 255, 255, 0.05f));
    g.fillRect (762.0f, 310.0f, 1.0f, 100.0f);
}

void CompEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float thr = value ("thr"), ratio = value ("ratio"), knee = value ("knee");
    const float mk = flag ("autoGain") ? proc.autoMakeup() : 0.0f;

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
        const float y = gainComputer (x, thr, std::max (1.0f, ratio), knee) + mk;
        if (x == -60) curve.startNewSubPath (cx (x), cy (y)); else curve.lineTo (cx (x), cy (y));
    }
    g.saveState();
    g.reduceClipRegion (c.toNearestInt());
    g.setColour (modern::accent()); g.strokePath (curve, juce::PathStrokeType (2.2f));
    if (det > -60)
    {
        const float yv = gainComputer (det, thr, std::max (1.0f, ratio), knee) + mk;
        g.setColour (juce::Colours::white); g.fillEllipse (cx (det) - 4, cy (yv) - 4, 8, 8);
    }
    g.restoreState();
    g.setFont (font (9, false)); g.setColour (mc::gridText());
    ltext (g, "IN", c.getRight() - 14, c.getBottom() - 13, 9);
    ltext (g, "OUT", c.getX() + 4, c.getY() + 3, 9);

    // ---- Verlauf
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
    juce::Path inP, outP, grP;
    inP.startNewSubPath (h.getX(), h.getBottom());
    for (int i = 0; i < N; ++i)
    {
        const float x = h.getX() + i * dx;
        inP.lineTo (x, histY (inH[i]));
        if (i == 0) { outP.startNewSubPath (x, histY (outH[i])); grP.startNewSubPath (x, h.getY() + h.getHeight() * juce::jlimit (0.0f, 1.0f, -grH[i] / 24.0f)); }
        else { outP.lineTo (x, histY (outH[i])); grP.lineTo (x, h.getY() + h.getHeight() * juce::jlimit (0.0f, 1.0f, -grH[i] / 24.0f)); }
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
    g.setFont (font (20)); g.setColour (mc::red());
    rtext (g, String (gr, 1) + " dB", h.getRight() - 10, h.getY() + 8, 20);
    g.setFont (font (9)); g.setColour (modern::dim());
    rtext (g, "GAIN REDUCTION", h.getRight() - 10, h.getY() + 34, 9);
    juce::ignoreUnused (r);
}

bool CompEditor::displayDown (juce::Point<float> p, const juce::MouseEvent& e)
{
    beginEdit ("thr");
    displayDrag (p, e);
    return true;
}

void CompEditor::displayDrag (juce::Point<float> p, const juce::MouseEvent&)
{
    float db;
    if (p.x < rHist.getX() - 8) db = -60.0f + 60.0f * (p.x - rCurve.getX()) / rCurve.getWidth();   // Kennlinie: x = Threshold
    else db = -60.0f * (p.y - rHist.getY()) / rHist.getHeight();
    edit ("thr", juce::jlimit (-60.0f, 0.0f, db));
}

void CompEditor::displayUp (juce::Point<float>) { endEdit ("thr"); }

void CompEditor::displayWheel (juce::Point<float>, float d, const juce::MouseEvent&)
{
    setValue ("ratio", juce::jlimit (1.0f, 100.0f, value ("ratio") * std::exp (d * 0.8f)));
}
