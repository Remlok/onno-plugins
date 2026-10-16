#include "OnnoTame.h"
#include "Common/OnnoDSP.h"

using namespace onno;
using juce::String;

namespace
{
juce::StringArray makeProfileNames()
{
    juce::StringArray a;
    for (auto& n : onno::core::TameCore::profileNames()) a.add (juce::String (n));
    return a;
}
}

const juce::StringArray TameProcessor::profileNames = makeProfileNames();

juce::AudioProcessorValueTreeState::ParameterLayout TameProcessor::layout()
{
    prm::Layout p;
    prm::fromCore (p, core::TameCore::makeParams());
    return prm::done (p);
}

TameProcessor::TameProcessor() : ModernProcessor ("Onno Tame", "OnnoTame", stereoBuses(), layout())
{
    link.attach (apvts, core);
    for (int n = 0; n < numNodes; ++n) { pNodeF[(size_t) n] = raw (nid (n, "freq")); pNodeG[(size_t) n] = raw (nid (n, "gain")); }
    for (auto& g : grDisplay) g.store (0.0f);
}

void TameProcessor::applyProfile (int pr)
{
    for (auto& [id, val] : core::TameCore::profileValues (pr))
    {
        auto* prm = apvts.getParameter (juce::String (id));
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (val)); prm->endChangeGesture();
    }
}

float TameProcessor::sensitivityAt (float f) const
{
    float nf[numNodes], ng[numNodes];
    for (int n = 0; n < numNodes; ++n) { nf[n] = pNodeF[(size_t) n]->load(); ng[n] = pNodeG[(size_t) n]->load(); }
    return core::TameCore::sensitivity (f, nf, ng);
}

void TameProcessor::prepare (double sampleRate, int blockSize)
{
    link.pull (core);
    core.prepare (sampleRate, blockSize);
    analyzer.prepare (sampleRate);
    reportedLatency = core.latency();
    setLatencySamples (reportedLatency);
}

void TameProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    const int nch = std::min (2, getMainBusNumInputChannels());
    if (nch == 0) return;
    analyzer.push (buf.getReadPointer (0), nch > 1 ? buf.getReadPointer (1) : nullptr, n);
    link.pull (core);
    core.process (buf.getWritePointer (0), buf.getWritePointer (nch > 1 ? 1 : 0), n, nch, nullptr, nullptr);
    if (core.latency() != reportedLatency) { reportedLatency = core.latency(); setLatencySamples (reportedLatency); }
    atomicMax (outPeak, core.outPk);
    for (int b = 0; b < numDisplayBands; ++b) atomicMin (grDisplay[(size_t) b], core.disp[(size_t) b]);
    core.resetDisplay();
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
    addHelp ({ { "DEPTH", "Wie stark Resonanzen gedrückt werden. Mit DELTA abhören, was weggeht." },
               { "SHARPNESS", "Hoch = nur schmale Spitzen, niedrig = auch breitere Buckel." },
               { "SELECTIVITY", "Ab wie viel dB Überstand das Plugin eingreift. Höher = nur deutliche Resonanzen." },
               { "ATTACK", "Wie schnell auf eine Resonanz reagiert wird." },
               { "RELEASE", "Wie schnell die Absenkung wieder losgelassen wird." },
               { "DELTA", "Nur das hören, was entfernt wird. Klingt es nach Musik statt nach Härte, ist es zu viel." },
               { "STEREO LINK", "Beide Seiten gleich bearbeiten, damit das Stereobild stabil bleibt." } });
    for (int i = 1; i < TameProcessor::profileNames.size(); ++i)
        help[TameProcessor::profileNames[i].toUpperCase()] = String::fromUTF8 ("Profil laden: setzt Bereich, Schärfe und Empfindlichkeitskurve passend zur Quelle.");
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
