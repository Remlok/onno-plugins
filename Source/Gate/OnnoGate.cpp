#include "OnnoGate.h"

using namespace onno;
using juce::String;

juce::AudioProcessorValueTreeState::ParameterLayout GateProcessor::layout()
{
    prm::Layout p;
    prm::fromCore (p, core::GateCore::makeParams());
    return prm::done (p);
}

GateProcessor::GateProcessor() : ModernProcessor ("Onno Gate", "OnnoGate", stereoBuses (true), layout())
{
    link.attach (apvts, core);
}

void GateProcessor::prepare (double sampleRate, int blockSize)
{
    link.pull (core);
    core.prepare (sampleRate, blockSize);
    reportedLatency = core.latency();
    setLatencySamples (reportedLatency);
}

void GateProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    const int nch = std::min (2, getMainBusNumInputChannels());
    if (nch == 0) return;
    link.pull (core);
    core.process (buf.getWritePointer (0), buf.getWritePointer (nch > 1 ? 1 : 0), n, nch, sidechain (buf, 0), sidechain (buf, 1));
    if (core.latency() != reportedLatency) { reportedLatency = core.latency(); setLatencySamples (reportedLatency); }
    atomicMax (inPeak, core.inPk);
    atomicMax (outPeak, core.outPk);
    atomicMin (grPeak, core.grMin);
    atomicMax (detPeak, core.detPk);
    atomicMax (openPeak, core.disp[0]);
    core.resetDisplay();
}

juce::AudioProcessorEditor* GateProcessor::createEditor() { return new GateEditor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GateProcessor(); }
#endif

// ======================================================================== Editor
namespace
{
const juce::Rectangle<float> rDisplay { 16, 50, 868, 236 }, rHist { 28, 62, 760, 212 }, rMeter { 806, 62, 66, 212 };
float histY (float db) { return rHist.getY() + rHist.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 80.0f); }
}

GateEditor::GateEditor (GateProcessor& p) : ModernEditor (p, p.apvts, "GATE", 900, 470), proc (p)
{
    display = rDisplay;
    addHelp ({ { "THRESHOLD", "Unter diesem Pegel schließt das Gate. Knapp über Rauschen und Übersprechen setzen." },
               { "HYSTERESE", "Abstand zwischen Öffnen und Schließen. Verhindert Flattern nahe am Threshold." },
               { "ATTACK", "Wie schnell das Gate öffnet. Sehr kurz für Kick und Snare, sonst klickt es eventuell." },
               { "HOLD", "So lange bleibt das Gate nach dem Unterschreiten noch offen." },
               { "RELEASE", "Wie schnell das Gate danach schließt. Länger = natürliches Ausklingen." },
               { "RANGE", "Wie weit das Gate schließt. -inf = ganz zu, -10 dB = nur leiser, klingt natürlicher." },
               { "LISTEN", "Hört das gefilterte Steuersignal ab, um die Detektor-Filter einzustellen." } });
    const float y = 368, dx = 76;
    float x = 62;
    auto add = [&] (const char* id, const char* label, Colour c, std::function<String (float)> f)
    {
        knobs.push_back ({ id, label, x, y, 22, c, f });
        x += dx;
    };
    add ("thr", "THRESHOLD", mc::green(), [] (float v) { return fmt::db (v); });
    add ("hyst", "HYSTERESE", mc::green(), [] (float v) { return fmt::db (v); });
    add ("attack", "ATTACK", mc::amber(), [] (float v) { return fmt::ms (v); });
    add ("hold", "HOLD", mc::amber(), [] (float v) { return fmt::ms (v); });
    add ("release", "RELEASE", mc::amber(), [] (float v) { return fmt::ms (v); });
    add ("range", "RANGE", mc::red(), [] (float v) { return v <= -79.9f ? String ("-inf dB") : fmt::db (v); });
    add ("lookahead", "LOOKAHEAD", mc::purple(), [] (float v) { return v < 0.05f ? String ("AUS") : fmt::ms (v); });
    add ("scHpf", "DET. HPF", mc::blue(), [] (float v) { return v < 20.5f ? String ("AUS") : fmt::hz (v); });
    add ("scLpf", "DET. LPF", mc::blue(), [] (float v) { return v > 19900 ? String ("AUS") : fmt::hz (v); });

    buttons.push_back ({ { 776, 322, 96, 20 }, [this] { return String (value ("source") > 0.5f ? "SIDECHAIN" : "DETEKTOR: MAIN"); },
                         [this] { return value ("source") > 0.5f; }, [this] { toggle ("source"); }, mc::blue() });
    buttons.push_back ({ { 776, 352, 96, 20 }, [] { return String ("LISTEN"); }, [this] { return flag ("listen"); },
                         [this] { toggle ("listen"); }, mc::amber() });
    addAnalogStrip();
}

void GateEditor::tick()
{
    detH.push (dsp::a2db (proc.detPeak.exchange (0.0f)));
    outH.push (dsp::a2db (proc.outPeak.exchange (0.0f)));
    openH.push (proc.openPeak.exchange (0.0f));
    proc.inPeak.exchange (0.0f);
    proc.grPeak.exchange (0.0f);
}

void GateEditor::drawBackground (juce::Graphics& g)
{
    section (g, { 16, 296, 868, 126 });
    g.setColour (rgb (255, 255, 255, 0.05f));
    g.fillRect (762.0f, 310.0f, 1.0f, 100.0f);
}

void GateEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float>)
{
    const auto h = rHist;
    const float thr = value ("thr"), cl = thr - value ("hyst");
    g.setColour (rgb (24, 27, 34)); g.fillRoundedRectangle (h, 4);
    g.setFont (font (9, false));
    for (int d = -12; d > -80; d -= 12)
    {
        g.setColour (mc::grid()); g.fillRect (h.getX(), histY ((float) d), h.getWidth(), 1.0f);
        g.setColour (mc::gridText()); ltext (g, String (d), h.getX() + 4, histY ((float) d) - 12, 9);
    }
    const int N = detH.size();
    const float dx = h.getWidth() / (float) (N - 1);

    // Gate offen: grünes Band hinter dem Verlauf
    for (int i = 0; i < N; ++i)
    {
        const float o = openH[i];
        if (o < 0.02f) continue;
        g.setColour (mc::green().withAlpha (0.10f * o));
        g.fillRect (h.getX() + i * dx - dx * 0.5f, h.getY(), dx + 0.5f, h.getHeight());
        g.setColour (mc::green().withAlpha (0.8f * o));
        g.fillRect (h.getX() + i * dx - dx * 0.5f, h.getBottom() - 4, dx + 0.5f, 4.0f);
    }

    juce::Path dp, op;
    dp.startNewSubPath (h.getX(), h.getBottom());
    for (int i = 0; i < N; ++i)
    {
        const float x = h.getX() + i * dx;
        dp.lineTo (x, histY (detH[i]));
        if (i == 0) op.startNewSubPath (x, histY (outH[i])); else op.lineTo (x, histY (outH[i]));
    }
    dp.lineTo (h.getRight(), h.getBottom()); dp.closeSubPath();
    g.setColour (rgb (120, 135, 160, 0.30f)); g.fillPath (dp);
    g.setColour (modern::accent()); g.strokePath (op, juce::PathStrokeType (1.4f));

    // Öffnen/Schließen-Schwellen
    g.setColour (mc::green());
    g.fillRect (h.getX(), histY (thr), h.getWidth(), 1.4f);
    g.setFont (font (9)); ltext (g, "OPEN " + fmt::db (thr), h.getX() + 34, histY (thr) - 13, 9);
    g.setColour (mc::amber().withAlpha (0.8f));
    for (float x = h.getX(); x < h.getRight(); x += 8) g.fillRect (x, histY (cl), 4.0f, 1.2f);
    ltext (g, "CLOSE " + fmt::db (cl), h.getX() + 34, histY (cl) + 3, 9);

    // Statusanzeige
    const bool isOpen = openH[N - 1] > 0.5f;
    const auto m = rMeter;
    g.setColour (rgb (24, 27, 34)); g.fillRoundedRectangle (m, 4);
    g.setColour (isOpen ? mc::green() : rgb (60, 66, 80));
    g.fillEllipse (m.getCentreX() - 14, m.getY() + 16, 28, 28);
    g.setFont (font (10)); g.setColour (isOpen ? mc::green() : modern::dim());
    ctext (g, isOpen ? "OPEN" : "CLOSED", m.getCentreX(), m.getY() + 52, 10);
    drawMeter (g, { m.getCentreX() - 9, m.getY() + 76, 18, m.getHeight() - 92 }, outH[N - 1], modern::accent());
    g.setFont (font (8)); g.setColour (modern::dim());
    ctext (g, "OUT", m.getCentreX(), m.getBottom() - 13, 8);
}

bool GateEditor::displayDown (juce::Point<float> p, const juce::MouseEvent& e)
{
    if (! rHist.contains (p)) return false;
    // nahe der Close-Linie: Hysterese ziehen, sonst Threshold
    const float cl = value ("thr") - value ("hyst");
    dragHyst = std::abs (p.y - histY (cl)) < 6 && std::abs (p.y - histY (value ("thr"))) > 3;
    beginEdit (dragHyst ? "hyst" : "thr");
    displayDrag (p, e);
    return true;
}

void GateEditor::displayDrag (juce::Point<float> p, const juce::MouseEvent&)
{
    const float db = -80.0f * (p.y - rHist.getY()) / rHist.getHeight();
    if (dragHyst) edit ("hyst", juce::jlimit (0.0f, 12.0f, value ("thr") - db));
    else edit ("thr", juce::jlimit (-80.0f, 0.0f, db));
}

void GateEditor::displayUp (juce::Point<float>) { endEdit (dragHyst ? "hyst" : "thr"); }
