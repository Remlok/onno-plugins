// Test-Programm: misst die DSP und rendert die Oberfläche in PNG-Dateien (ohne DAW)
// Aufruf: OnnoTests <Ausgabeordner>
#include "VC1/VC1Processor.h"
#include "VC1/VC1Editor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>

static void setParam (VC1Processor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

// Ein Block Testsignal: Sinus mit Amplitude amp plus leises Rauschen
static void fill (juce::AudioBuffer<float>& b, long& t, float freq, float amp, float noise, juce::Random& rnd)
{
    for (int i = 0; i < b.getNumSamples(); ++i, ++t)
    {
        const float v = amp * std::sin (2.0f * juce::MathConstants<float>::pi * freq * (float) t / 48000.0f)
                      + noise * (rnd.nextFloat() * 2 - 1);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
}

static float measureChange (float freq, float amp, float thr, float ratio)
{
    VC1Processor p;
    p.prepareToPlay (48000, 512);
    setParam (p, "thr", thr); setParam (p, "ratio", ratio);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    juce::Random rnd (1);
    long t = 0; double si = 0, so = 0;
    for (int k = 0; k < 200; ++k)
    {
        fill (b, t, freq, amp, 0, rnd);
        double e = 0;
        for (int i = 0; i < 512; ++i) e += b.getSample (0, i) * b.getSample (0, i);
        p.processBlock (b, m);
        if (k >= 150)
        {
            si += e;
            for (int i = 0; i < 512; ++i) so += b.getSample (0, i) * b.getSample (0, i);
        }
    }
    return (float) (10 * std::log10 (so / si));
}

static void render (const juce::File& dir, const juce::String& name, int view, int hot)
{
    VC1Processor p;
    p.prepareToPlay (48000, 512);
    setParam (p, "view", (float) view);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* e = dynamic_cast<VC1Editor*> (ed.get());
    ed->setSize (800, 300);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    juce::Random rnd (2);
    long t = 0;
    for (int f = 0; f < 40; ++f)
    {
        for (int k = 0; k < 4; ++k) { fill (b, t, 220, 0.35f, 0.05f, rnd); p.processBlock (b, m); }
        e->tickMeters();
    }
    e->setHot (hot);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File out = dir.getChildFile (name);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("%s geschrieben (%d x %d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    // DSP: Vergleich mit dem JSFX-Original (gleiche Einstellungen, gleiche Messung)
    std::printf ("1 kHz, -6 dBFS Spitze, Thr -18, 4:1:  %+.2f dB (JSFX-Original: -8.0)\n", measureChange (1000, 0.5f, -18, 4));
    std::printf ("1 kHz, -30 dBFS Spitze (unter Thr):     %+.2f dB (JSFX-Original:  0.0)\n", measureChange (1000, 0.0316f, -18, 4));
    std::printf ("100 Hz, -6 dBFS Spitze, Thr -24, 2:1:   %+.2f dB (JSFX-Original: -8.3)\n", measureChange (100, 0.5f, -24, 2));

    render (dir, "vc1-juce-konsole.png", 0, -1);
    render (dir, "vc1-juce-konsole-hover.png", 0, 0);
    render (dir, "vc1-juce-modern.png", 1, -1);
    return 0;
}
