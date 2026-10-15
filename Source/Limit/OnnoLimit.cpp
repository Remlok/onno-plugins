#include "OnnoLimit.h"

using namespace onno;
using juce::String;

juce::AudioProcessorValueTreeState::ParameterLayout LimitProcessor::layout()
{
    prm::Layout p;
    prm::lin (p, "thr", "Threshold", -24, 0, 0, "dB");
    prm::lin (p, "ceiling", "Ceiling", -12, 0, -0.3f, "dB");
    prm::lg (p, "release", "Release", 1, 1000, 60, "ms");
    Analog::addParams (p);
    prm::bl (p, "bypass", "Bypass", false);
    return prm::done (p);
}

LimitProcessor::LimitProcessor() : ModernProcessor ("Onno Limit", "OnnoLimit", stereoBuses(), layout())
{
    pThr = raw ("thr"); pCeil = raw ("ceiling"); pRel = raw ("release"); pBypass = raw ("bypass");
    analog.attach (*this);
}

void LimitProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    L = std::max (2, (int) std::round (0.0015 * sr));
    for (auto& d : dl) d.assign ((size_t) L + 1, 0.0f);
    box.assign ((size_t) L, 1.0f);
    boxSum = L;
    dqv.assign ((size_t) L + 2, 1.0f);
    dqi.assign ((size_t) L + 2, 0);
    dlPos = boxPos = dqHead = dqSize = 0;
    t = 0; env = 1;
    analog.prepare (sr);
    setLatencySamples (L - 1);
    driveS.reset (sr, 0.02); driveS.setCurrentAndTargetValue (dsp::db2a (-pThr->load()));
    ceilS.reset (sr, 0.02); ceilS.setCurrentAndTargetValue (dsp::db2a (pCeil->load()));
}

void LimitProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    const int nch = std::min (2, getMainBusNumInputChannels());
    if (nch == 0) return;
    float* Lp = buf.getWritePointer (0);
    float* Rp = buf.getWritePointer (nch > 1 ? 1 : 0);

    const bool bypass = pBypass->load() > 0.5f;
    driveS.setTargetValue (dsp::db2a (-pThr->load()));
    ceilS.setTargetValue (dsp::db2a (pCeil->load()));
    const float relC = dsp::coef (pRel->load(), sr);
    analog.beginBlock();
    const bool sat = analog.active() && ! bypass;
    const int cap = (int) dqv.size(), D = (int) dl[0].size();
    float inPk = 0, drvPk = 0, outPk = 0, gMin = 1;

    for (int i = 0; i < n; ++i, ++t)
    {
        const float xl = Lp[i], xr = Rp[i];
        float drive = driveS.getNextValue();
        const float ceil = ceilS.getNextValue();
        // Sättigung vor dem Limiter (nach dem Threshold-Gain), danach ohne weiteres Gain
        float sl = xl, sr2 = xr;
        if (sat) { analog.advance(); sl = analog.tick (xl * drive, 0); sr2 = analog.tick (xr * drive, 1); drive = 1.0f; }
        const float pk = std::max (std::abs (sl), std::abs (sr2)) * drive;
        const float r = pk > ceil ? ceil / pk : 1.0f;

        // gleitendes Minimum über L Samples (monotone Warteschlange)
        while (dqSize > 0 && dqv[(size_t) ((dqHead + dqSize - 1) % cap)] >= r) --dqSize;
        const int back = (dqHead + dqSize) % cap;
        dqv[(size_t) back] = r; dqi[(size_t) back] = t; ++dqSize;
        while (dqi[(size_t) dqHead] <= t - L) { dqHead = (dqHead + 1) % cap; --dqSize; }
        const float h = dqv[(size_t) dqHead];

        // Release, dann Rechteck-Glättung über L Samples
        env = h < env ? h : env + (h - env) * (1.0f - relC);
        boxSum += env - box[(size_t) boxPos];
        box[(size_t) boxPos] = env;
        if (++boxPos >= L) boxPos = 0;
        const float g = (float) (boxSum / L);

        // Audio um L-1 Samples verzögern
        dl[0][(size_t) dlPos] = sl; dl[1][(size_t) dlPos] = sr2;
        int rp = dlPos - (L - 1); if (rp < 0) rp += D;
        const float yl = dl[0][(size_t) rp], yr = dl[1][(size_t) rp];
        if (++dlPos >= D) dlPos = 0;

        float ol = yl, orr = yr;
        if (! bypass)
        {
            ol = juce::jlimit (-ceil, ceil, yl * drive * g);
            orr = juce::jlimit (-ceil, ceil, yr * drive * g);
            gMin = std::min (gMin, g);
        }
        Lp[i] = ol;
        if (nch > 1) Rp[i] = orr;
        inPk = std::max (inPk, std::max (std::abs (xl), std::abs (xr)));
        drvPk = std::max (drvPk, pk);
        outPk = std::max (outPk, std::max (std::abs (ol), std::abs (orr)));
    }
    atomicMax (inPeak, inPk);
    atomicMax (drivenPeak, drvPk);
    atomicMax (outPeak, outPk);
    atomicMin (grPeak, dsp::a2db (gMin));
}

juce::AudioProcessorEditor* LimitProcessor::createEditor() { return new LimitEditor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LimitProcessor(); }
#endif

// ======================================================================== Editor
namespace
{
const juce::Rectangle<float> rDisplay { 16, 50, 728, 220 }, rHist { 28, 62, 704, 196 };
float histY (float db) { return rHist.getY() + rHist.getHeight() * juce::jlimit (0.0f, 1.0f, (6.0f - db) / 42.0f); }
}

LimitEditor::LimitEditor (LimitProcessor& p) : ModernEditor (p, p.apvts, "LIMIT", 760, 440), proc (p)
{
    display = rDisplay;
    addBypass();
    knobs.push_back ({ "thr", "THRESHOLD", 80, 336, 26, mc::red(), [] (float v) { return fmt::db (v); } });
    knobs.push_back ({ "ceiling", "CEILING", 176, 336, 26, modern::warm(), [] (float v) { return fmt::db (v); } });
    knobs.push_back ({ "release", "RELEASE", 272, 336, 26, mc::amber(), [] (float v) { return fmt::ms (v); } });
    addAnalogStrip();
}

void LimitEditor::tick()
{
    const float in = dsp::a2db (proc.drivenPeak.exchange (0.0f));
    const float out = dsp::a2db (proc.outPeak.exchange (0.0f));
    const float g = proc.grPeak.exchange (0.0f);
    proc.inPeak.exchange (0.0f);
    inH.push (in); outH.push (out); grH.push (g);
    inD = in > inD ? in : inD - 1.5f;
    outD = out > outD ? out : outD - 1.5f;
    grD = g < grD ? g : grD + (g - grD) * 0.2f;
}

void LimitEditor::drawBackground (juce::Graphics& g)
{
    section (g, { 16, 280, 728, 108 });
    g.setColour (rgb (255, 255, 255, 0.05f));
    g.fillRect (330.0f, 294.0f, 1.0f, 80.0f);

    // waagrechte Meter: Eingang (nach Threshold), Gain Reduction, Ausgang
    auto bar = [&] (float y, const char* label, float v, float lo, float hi, Colour c, bool fromRight)
    {
        g.setFont (font (9)); g.setColour (modern::dim()); ltext (g, label, 350, y + 1, 9);
        const juce::Rectangle<float> r { 392, y, 290, 10 };
        g.setColour (rgb (24, 27, 34)); g.fillRoundedRectangle (r, 3);
        const float nrm = juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo));
        g.setColour (c);
        if (fromRight) g.fillRoundedRectangle (r.getRight() - r.getWidth() * (1 - nrm), y, r.getWidth() * (1 - nrm), 10, 3);
        else g.fillRoundedRectangle (r.getX(), y, r.getWidth() * nrm, 10, 3);
        g.setFont (font (10)); g.setColour (modern::text());
        rtext (g, v <= -89 ? String ("-inf") : String (v, 1), 732, y - 1, 10);
    };
    bar (302, "IN", inD, -48, 6, rgb (150, 162, 185), false);
    bar (326, "GR", grD, -24, 0, mc::red(), true);
    bar (350, "OUT", outD, -48, 6, modern::accent(), false);
}

void LimitEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float>)
{
    const auto h = rHist;
    g.setColour (rgb (24, 27, 34)); g.fillRoundedRectangle (h, 4);
    g.setFont (font (9, false));
    for (int d = 0; d >= -30; d -= 6)
    {
        g.setColour (d == 0 ? rgb (255, 255, 255, 0.14f) : mc::grid()); g.fillRect (h.getX(), histY ((float) d), h.getWidth(), 1.0f);
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
        const float gy = h.getY() + h.getHeight() * juce::jlimit (0.0f, 1.0f, -grH[i] / 24.0f);
        if (i == 0) { outP.startNewSubPath (x, histY (outH[i])); grP.startNewSubPath (x, gy); }
        else { outP.lineTo (x, histY (outH[i])); grP.lineTo (x, gy); }
    }
    inP.lineTo (h.getRight(), h.getBottom()); inP.closeSubPath();
    g.setColour (rgb (120, 135, 160, 0.28f)); g.fillPath (inP);
    g.setColour (modern::accent()); g.strokePath (outP, juce::PathStrokeType (1.4f));
    juce::Path grFill (grP); grFill.lineTo (h.getRight(), h.getY()); grFill.lineTo (h.getX(), h.getY()); grFill.closeSubPath();
    g.setColour (mc::red().withAlpha (0.2f)); g.fillPath (grFill);
    g.setColour (mc::red()); g.strokePath (grP, juce::PathStrokeType (1.6f));

    const float cy = histY (value ("ceiling"));
    g.setColour (modern::warm());
    for (float x = h.getX(); x < h.getRight(); x += 8) g.fillRect (x, cy, 4.0f, 1.4f);
    g.setFont (font (9)); ltext (g, "CEILING " + fmt::db (value ("ceiling")), h.getX() + 34, cy + 3, 9);

    g.setColour (rgb (14, 16, 21, 0.85f)); g.fillRoundedRectangle (h.getRight() - 30 - 120, h.getY() + 6, 124, 44, 6);
    g.setFont (font (20)); g.setColour (mc::red());
    rtext (g, String (grD, 1) + " dB", h.getRight() - 30, h.getY() + 8, 20);
    g.setFont (font (9)); g.setColour (modern::dim());
    rtext (g, "GAIN REDUCTION", h.getRight() - 30, h.getY() + 34, 9);
}
