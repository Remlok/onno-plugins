// Test-Programm: misst die DSP und rendert die Oberflächen in PNG-Dateien (ohne DAW)
// Aufruf: OnnoTests <Ausgabeordner>
#include "VC1/VC1Processor.h"
#include "VC1/VC1Editor.h"
#include "CS1/CS1Processor.h"
#include "CS1/CS1Editor.h"
#include "RV1/RV1Processor.h"
#include "RV1/RV1Editor.h"
#include "EQ/OnnoEQ.h"
#include "Comp/OnnoComp.h"
#include "Gate/OnnoGate.h"
#include "Delay/OnnoDelay.h"
#include "MultiComp/OnnoMultiComp.h"
#include "Limit/OnnoLimit.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <functional>

static int failures = 0;

static void setParam (juce::AudioProcessorValueTreeState& s, const char* id, float v)
{
    auto* prm = s.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

// Ein Block Testsignal: Sinus mit Amplitude amp plus optional Rauschen
static void fill (juce::AudioBuffer<float>& b, long& t, float freq, float amp, float noise, juce::Random& rnd)
{
    for (int i = 0; i < b.getNumSamples(); ++i, ++t)
    {
        const float v = amp * std::sin (2.0f * juce::MathConstants<float>::pi * freq * (float) t / 48000.0f)
                      + noise * (rnd.nextFloat() * 2 - 1);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
}

// Pegeländerung eines Sinustons in dB nach dem Einschwingen
template <typename Proc>
static float measure (float freq, float amp, std::function<void (Proc&)> setup, int blocks = 200)
{
    Proc p;
    p.prepareToPlay (48000, 512);
    setup (p);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    juce::Random rnd (1);
    long t = 0; double si = 0, so = 0;
    for (int k = 0; k < blocks; ++k)
    {
        fill (b, t, freq, amp, 0, rnd);
        double e = 0;
        for (int i = 0; i < 512; ++i) e += b.getSample (0, i) * b.getSample (0, i);
        p.processBlock (b, m);
        if (k >= blocks - 50)
        {
            si += e;
            for (int i = 0; i < 512; ++i) so += b.getSample (0, i) * b.getSample (0, i);
        }
    }
    return (float) (10 * std::log10 (so / si));
}

static void check (const char* what, float got, float want, float tol)
{
    const bool ok = std::abs (got - want) <= tol;
    if (! ok) ++failures;
    std::printf ("%s %-48s %+7.2f dB  (soll %+6.2f)\n", ok ? "OK  " : "FEHL", what, got, want);
}

template <typename Proc, typename Ed>
static void render (const juce::File& dir, const juce::String& name, int w, int h, std::function<void (Proc&)> setup, int hot,
                    int frames = 40, bool pulsed = false)
{
    Proc p;
    p.prepareToPlay (48000, 512);
    setup (p);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* e = dynamic_cast<Ed*> (ed.get());
    ed->setSize (w, h);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    juce::Random rnd (2);
    long t = 0;
    for (int f = 0; f < frames; ++f)
    {
        // gepulst: Schläge im Achtel-Raster mit leisen Pausen (wie Drums)
        const float amp = pulsed ? (f % 24 < 3 ? 0.6f : (f % 24 < 10 ? 0.12f : 0.004f)) : 0.35f;
        const float nz = pulsed ? amp * 0.4f : 0.05f;
        for (int k = 0; k < 4; ++k) { fill (b, t, 220, amp, nz, rnd); p.processBlock (b, m); }
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

static void testVC1 (const juce::File& dir)
{
    std::puts ("--- VC-1 ---");
    using F = std::function<void (VC1Processor&)>;
    // Vergleich mit dem JSFX-Original (gleiche Einstellungen, gleiche Messung)
    check ("1 kHz -6 dBFS, Thr -18, 4:1 (JSFX: -8.0)", measure<VC1Processor> (1000, 0.5f, F ([] (VC1Processor&) {})), -8.0f, 0.1f);
    check ("1 kHz -30 dBFS, unter Threshold (JSFX: 0.0)", measure<VC1Processor> (1000, 0.0316f, F ([] (VC1Processor&) {})), 0.0f, 0.05f);
    check ("100 Hz -6 dBFS, Thr -24, 2:1 (JSFX: -8.3)", measure<VC1Processor> (100, 0.5f, F ([] (VC1Processor& p) {
        setParam (p.apvts, "thr", -24); setParam (p.apvts, "ratio", 2); })), -8.3f, 0.1f);

    render<VC1Processor, VC1Editor> (dir, "vc1-juce-konsole.png", 800, 300, [] (VC1Processor&) {}, -1);
    render<VC1Processor, VC1Editor> (dir, "vc1-juce-konsole-hover.png", 800, 300, [] (VC1Processor&) {}, 0);
    render<VC1Processor, VC1Editor> (dir, "vc1-juce-modern.png", 800, 300, [] (VC1Processor& p) { setParam (p.apvts, "view", 1); }, -1);
}

static void testCS1 (const juce::File& dir)
{
    std::puts ("--- CS-1 ---");
    using F = std::function<void (CS1Processor&)>;
    auto none = F ([] (CS1Processor&) {});
    auto at = [] (float f, float amp, F s) { return measure<CS1Processor> (f, amp, s); };

    for (float f : { 50.0f, 1000.0f, 10000.0f })
        check (("neutral " + juce::String (f) + " Hz").toRawUTF8(), at (f, 0.25f, none), 0.0f, 0.05f);

    auto lmf = F ([] (CS1Processor& p) { setParam (p.apvts, "lmfGain", 6); setParam (p.apvts, "lmfFreq", 1000); });
    check ("LMF +6 dB bei 1 kHz", at (1000, 0.25f, lmf), 6.0f, 0.1f);
    check ("LMF +6 dB, 100 Hz bleibt", at (100, 0.25f, lmf), 0.0f, 0.3f);
    auto lf = F ([] (CS1Processor& p) { setParam (p.apvts, "lfGain", 6); setParam (p.apvts, "lfFreq", 100); });
    check ("LF-Shelf +6 dB, 30 Hz", at (30, 0.25f, lf), 6.0f, 0.4f);
    check ("LF-Shelf +6 dB, 1 kHz", at (1000, 0.25f, lf), 0.0f, 0.1f);
    auto hf = F ([] (CS1Processor& p) { setParam (p.apvts, "hfGain", -6); setParam (p.apvts, "hfFreq", 3000); setParam (p.apvts, "hfBell", 1); });
    check ("HF-Glocke -6 dB bei 3 kHz", at (3000, 0.25f, hf), -6.0f, 0.1f);
    auto eqOff = F ([] (CS1Processor& p) { setParam (p.apvts, "lmfGain", 6); setParam (p.apvts, "lmfFreq", 1000); setParam (p.apvts, "eqOn", 0); });
    check ("EQ aus: 1 kHz unverändert", at (1000, 0.25f, eqOff), 0.0f, 0.05f);

    auto hp = F ([] (CS1Processor& p) { setParam (p.apvts, "hpf", 100); });
    check ("HPF 100 Hz: -3 dB bei 100 Hz", at (100, 0.25f, hp), -3.0f, 0.2f);
    check ("HPF 100 Hz: 50 Hz (18 dB/Okt)", at (50, 0.25f, hp), -18.1f, 0.6f);
    auto lp = F ([] (CS1Processor& p) { setParam (p.apvts, "lpf", 5000); });
    check ("LPF 5 kHz: -3 dB bei 5 kHz", at (5000, 0.25f, lp), -3.0f, 0.3f);

    auto trim = F ([] (CS1Processor& p) { setParam (p.apvts, "trim", -6); });
    check ("Trim -6 dB", at (1000, 0.25f, trim), -6.0f, 0.05f);
    auto out = F ([] (CS1Processor& p) { setParam (p.apvts, "out", 4); });
    check ("Output +4 dB", at (1000, 0.25f, out), 4.0f, 0.05f);

    auto comp = F ([] (CS1Processor& p) { setParam (p.apvts, "compOn", 1); setParam (p.apvts, "compThr", -18); setParam (p.apvts, "compRatio", 4); });
    std::printf ("     Kompressor Thr -18, 4:1, 1 kHz -6 dBFS:          %+7.2f dB  (VC-1 mit 120 ms Release: -8.0)\n", at (1000, 0.5f, comp));
    check ("Kompressor: leiser Ton bleibt", at (1000, 0.0316f, comp), 0.0f, 0.05f);
    auto mix = F ([] (CS1Processor& p) { setParam (p.apvts, "compOn", 1); setParam (p.apvts, "compThr", -18); setParam (p.apvts, "compMix", 0); });
    check ("Kompressor Mix 0 %: unverändert", at (1000, 0.5f, mix), 0.0f, 0.05f);

    auto gate = F ([] (CS1Processor& p) { setParam (p.apvts, "gateOn", 1); setParam (p.apvts, "gateThr", -40); setParam (p.apvts, "gateRange", 30); });
    check ("Gate: Ton -50 dBFS, Thr -40 → -20 dB (1:3)", at (1000, 0.00316f, gate), -20.0f, 0.5f);
    check ("Gate: Ton -10 dBFS geht durch", at (1000, 0.316f, gate), 0.0f, 0.05f);

    auto ds = F ([] (CS1Processor& p) { setParam (p.apvts, "dsOn", 1); setParam (p.apvts, "dsThr", -30); setParam (p.apvts, "dsRange", 6); setParam (p.apvts, "dsFreq", 6000); });
    check ("De-Esser: 9 kHz laut wird um Range gesenkt", at (9000, 0.316f, ds), -5.5f, 0.6f);
    check ("De-Esser: 500 Hz bleibt", at (500, 0.316f, ds), 0.0f, 0.1f);
    check ("De-Esser an, leise: 9 kHz bleibt (Weiche flach)", at (9000, 0.001f, ds), 0.0f, 0.1f);

    auto drive = F ([] (CS1Processor& p) { setParam (p.apvts, "drive", 60); });
    const float dl = at (1000, 0.05f, drive);
    check ("Drive 60 %: leiser Ton fast unverändert", dl, 0.0f, 0.3f);

    auto ag = F ([] (CS1Processor& p) { setParam (p.apvts, "lmfGain", 12); setParam (p.apvts, "lmfFreq", 1000); setParam (p.apvts, "lmfQ", 0.4f); setParam (p.apvts, "autoGain", 1); });
    check ("Auto-Gain gleicht +12 dB EQ aus", measure<CS1Processor> (1000, 0.1f, ag, 600), 0.0f, 0.5f);

    // Auto-Trim: Sinus mit -30 dBFS RMS → Trim soll +12 dB ergeben
    {
        CS1Processor p;
        p.prepareToPlay (48000, 512);
        p.autoTrimArmed = true;
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; juce::Random rnd (3); long t = 0;
        const float amp = std::pow (10.0f, -30.0f / 20.0f) * std::sqrt (2.0f);
        for (int k = 0; k < 400 && ! p.autoTrimDone.load(); ++k) { fill (b, t, 440, amp, 0, rnd); p.processBlock (b, m); }
        check ("Auto-Trim: -30 dBFS RMS → +12 dB", p.autoTrimDone.load() ? p.autoTrimResult.load() : -99.0f, 12.0f, 0.1f);
    }

    // Presets und A/B
    {
        CS1Processor p;
        p.prepareToPlay (48000, 512);
        setParam (p.apvts, "trim", 3);
        p.applyPreset (0);
        check ("Preset KICK: HPF 30 Hz", p.apvts.getRawParameterValue ("hpf")->load(), 30.0f, 0.01f);
        check ("Preset KICK: Trim bleibt", p.apvts.getRawParameterValue ("trim")->load(), 3.0f, 0.01f);
        p.applyPreset (6);
        check ("Preset VOCAL: Gate wieder aus", p.apvts.getRawParameterValue ("gateOn")->load(), 0.0f, 0.01f);
        setParam (p.apvts, "lfGain", 5);
        p.selectSlot (1);
        check ("B startet als Kopie von A", p.apvts.getRawParameterValue ("lfGain")->load(), 5.0f, 0.01f);
        setParam (p.apvts, "lfGain", -5);
        p.selectSlot (0);
        check ("zurück zu A", p.apvts.getRawParameterValue ("lfGain")->load(), 5.0f, 0.01f);
        p.selectSlot (1);
        check ("wieder B", p.apvts.getRawParameterValue ("lfGain")->load(), -5.0f, 0.01f);

        // Zustand speichern und laden (inklusive A/B)
        juce::MemoryBlock mb; p.getStateInformation (mb);
        CS1Processor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        check ("Laden: B aktiv mit -5", q.apvts.getRawParameterValue ("lfGain")->load(), -5.0f, 0.01f);
        q.selectSlot (0);
        check ("Laden: A hat +5", q.apvts.getRawParameterValue ("lfGain")->load(), 5.0f, 0.01f);
    }

    auto scream = F ([] (CS1Processor& p) { p.applyPreset (7); setParam (p.apvts, "gateOn", 1); });
    render<CS1Processor, CS1Editor> (dir, "cs1-konsole.png", 1100, 380, scream, -1);
    render<CS1Processor, CS1Editor> (dir, "cs1-konsole-hover.png", 1100, 380, scream, 12);
    render<CS1Processor, CS1Editor> (dir, "cs1-modern.png", 1100, 380, F ([] (CS1Processor& p) { p.applyPreset (7); setParam (p.apvts, "gateOn", 1); setParam (p.apvts, "view", 1); }), -1);
}

// Impulsantwort des Halls (nur Hallanteil), Länge in Sekunden
static std::vector<float> impulse (RV1Processor& p, float seconds, std::vector<float>* right = nullptr)
{
    p.prepareToPlay (48000, 512);
    juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
    std::vector<float> out;
    const int blocks = (int) (seconds * 48000 / 512);
    for (int k = 0; k < blocks; ++k)
    {
        b.clear();
        if (k == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
        p.processBlock (b, m);
        for (int i = 0; i < 512; ++i) { out.push_back (b.getSample (0, i)); if (right) right->push_back (b.getSample (1, i)); }
    }
    return out;
}

// Nachhallzeit aus der Schroeder-Rückwärtsintegration (T30: -5 bis -35 dB, auf 60 dB hochgerechnet)
static float rt60 (const std::vector<float>& h)
{
    std::vector<double> e (h.size() + 1, 0.0);
    for (int i = (int) h.size() - 1; i >= 0; --i) e[(size_t) i] = e[(size_t) i + 1] + (double) h[(size_t) i] * h[(size_t) i];
    const double e0 = e[0];
    int i5 = -1, i35 = -1;
    for (size_t i = 0; i < h.size(); ++i)
    {
        const double db = 10 * std::log10 (e[i] / e0 + 1e-30);
        if (i5 < 0 && db <= -5) i5 = (int) i;
        if (i35 < 0 && db <= -35) { i35 = (int) i; break; }
    }
    if (i5 < 0 || i35 < 0) return -1;
    return (float) ((i35 - i5) / 48000.0 * 2.0);
}

static void testRV1 (const juce::File& dir)
{
    std::puts ("--- RV-1 ---");
    using F = std::function<void (RV1Processor&)>;
    check ("Mix 0 %: Signal unverändert", measure<RV1Processor> (1000, 0.25f, F ([] (RV1Processor& p) { setParam (p.apvts, "mix", 0); })), 0.0f, 0.01f);

    for (float dec : { 1.0f, 2.4f, 6.0f })
    {
        RV1Processor p;
        setParam (p.apvts, "send", 1); setParam (p.apvts, "decay", dec); setParam (p.apvts, "character", 100);
        setParam (p.apvts, "depth", 0); setParam (p.apvts, "lowCut", 20); setParam (p.apvts, "highCut", 20000); setParam (p.apvts, "movement", 0);
        const float t = rt60 (impulse (p, dec * 1.6f + 0.5f));
        check (("Nachhallzeit bei Decay " + juce::String (dec, 1) + " s (in s)").toRawUTF8(), t, dec, dec * 0.1f);
    }

    {
        RV1Processor p; setParam (p.apvts, "send", 1);
        std::vector<float> r;
        auto l = impulse (p, 1.0f, &r);
        double sl = 0, sr2 = 0, slr = 0;
        for (size_t i = 0; i < l.size(); ++i) { sl += l[i] * l[i]; sr2 += r[i] * r[i]; slr += l[i] * r[i]; }
        check ("Stereo: Korrelation L/R klein (x100)", (float) (100 * slr / std::sqrt (sl * sr2)), 0.0f, 35.0f);
    }

    // alle Programme: stabil, keine ungültigen Werte, sinnvoller Pegel
    {
        int bad = 0; float worst = 0;
        for (int k = 0; k < (int) RV1Processor::programs().size(); ++k)
        {
            RV1Processor p; p.prepareToPlay (48000, 512); p.loadProgram (k); setParam (p.apvts, "send", 1);
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; juce::Random rnd (k); long t = 0;
            for (int j = 0; j < 400; ++j)
            {
                fill (b, t, 220, j < 100 ? 0.3f : 0.0f, j < 100 ? 0.3f : 0.0f, rnd);
                p.processBlock (b, m);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = b.getSample (0, i);
                    if (! std::isfinite (v)) ++bad;
                    worst = std::max (worst, std::abs (v));
                }
            }
            if (worst > 4.0f) std::printf ("     instabil: Programm %d (%s)\n", k, RV1Processor::programs()[(size_t) k].name);
        }
        check ("32 Programme: ungültige Samples", (float) bad, 0.0f, 0.0f);
        check ("32 Programme: höchste Spitze unter 0 dBFS (in dBFS)", 20 * std::log10 (worst), -6.0f, 6.0f);
    }

    // Ducking: Hall unter einem anhaltenden lauten Ton deutlich leiser
    {
        auto wet = [] (float duck) {
            return measure<RV1Processor> (500, 0.5f, F ([duck] (RV1Processor& p) { setParam (p.apvts, "send", 1); setParam (p.apvts, "duck", duck); }), 300);
        };
        check ("Duck 100 % senkt den Hall unter Signal (dB)", wet (100) - wet (0), -24.0f, 1.0f);
    }

    // Gated: nach dem Signal schließt der Hall nach der Gate-Zeit
    {
        RV1Processor p; p.prepareToPlay (48000, 512);
        p.loadProgram (24);  // 80s Snare Gate, Gate-Zeit 400 ms
        setParam (p.apvts, "send", 1);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; juce::Random rnd (5); long t = 0;
        float early = 0, late = 0;
        for (int j = 0; j < 120; ++j)
        {
            fill (b, t, 200, 0.0f, j < 4 ? 0.5f : 0.0f, rnd);
            p.processBlock (b, m);
            const float ms = (float) (j * 512) / 48.0f;
            for (int i = 0; i < 512; ++i)
            {
                if (ms > 100 && ms < 300) early = std::max (early, std::abs (b.getSample (0, i)));
                if (ms > 520) late = std::max (late, std::abs (b.getSample (0, i)));
            }
        }
        check ("Gated: offen während der Gate-Zeit (dBFS)", 20 * std::log10 (early), -20.0f, 14.0f);
        check ("Gated: nach der Gate-Zeit zu (unter -80 dBFS)", std::max (-120.0f, 20 * std::log10 (late + 1e-9f)), -100.0f, 20.0f);
    }

    // Freeze hält den Hall stehen, ohne aufzuschaukeln
    {
        RV1Processor p; p.prepareToPlay (48000, 512); setParam (p.apvts, "send", 1); setParam (p.apvts, "decay", 3);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; juce::Random rnd (6); long t = 0;
        double eA = 0, eB = 0;
        for (int j = 0; j < 1000; ++j)
        {
            if (j == 40) setParam (p.apvts, "freeze", 1);
            fill (b, t, 300, 0.0f, j < 30 ? 0.3f : 0.0f, rnd);
            p.processBlock (b, m);
            for (int i = 0; i < 512; ++i)
            {
                const double v = b.getSample (0, i);
                if (j >= 100 && j < 200) eA += v * v;
                if (j >= 900) eB += v * v;
            }
        }
        check ("Freeze: Energie nach 10 s fast gleich (dB)", (float) (10 * std::log10 (eB / eA)), 0.0f, 3.0f);
    }

    {
        RV1Processor p; setParam (p.apvts, "mix", 42);
        p.loadProgram (5);
        check ("Programmwechsel behält Mix", p.apvts.getRawParameterValue ("mix")->load(), 42.0f, 0.01f);
        check ("Programm Vocal Plate: Duck 50 %", p.apvts.getRawParameterValue ("duck")->load(), 50.0f, 0.01f);
    }

    auto setup = F ([] (RV1Processor& p) { p.loadProgram (5); });
    render<RV1Processor, RV1Editor> (dir, "rv1-konsole.png", 1000, 340, setup, -1);
    render<RV1Processor, RV1Editor> (dir, "rv1-konsole-hover.png", 1000, 340, setup, 1);
    render<RV1Processor, RV1Editor> (dir, "rv1-modern.png", 1000, 340, F ([] (RV1Processor& p) { p.loadProgram (28); setParam (p.apvts, "view", 1); }), -1);
}

static void testEQ (const juce::File& dir)
{
    std::puts ("--- Onno EQ ---");
    using F = std::function<void (EQProcessor&)>;
    auto at = [] (float f, F s) { return measure<EQProcessor> (f, 0.25f, s); };
    auto none = F ([] (EQProcessor&) {});
    for (float f : { 50.0f, 1000.0f, 10000.0f })
        check (("neutral " + juce::String (f) + " Hz").toRawUTF8(), at (f, none), 0.0f, 0.02f);
    auto bell = F ([] (EQProcessor& p) { setParam (p.apvts, "b4gain", 6); setParam (p.apvts, "b4freq", 1000); });
    check ("Bell +6 dB bei 1 kHz", at (1000, bell), 6.0f, 0.05f);
    check ("Bell +6 dB, 100 Hz bleibt", at (100, bell), 0.0f, 0.2f);
    auto ls = F ([] (EQProcessor& p) { setParam (p.apvts, "b2gain", -6); });
    check ("Low Shelf -6 dB bei 30 Hz", at (30, ls), -6.0f, 0.3f);
    auto lc = F ([] (EQProcessor& p) { setParam (p.apvts, "b1on", 1); setParam (p.apvts, "b1freq", 100); setParam (p.apvts, "b1slope", 2); });
    check ("Low Cut 100 Hz 48 dB/Okt: -3 dB bei 100 Hz", at (100, lc), -3.0f, 0.2f);
    check ("Low Cut 100 Hz 48 dB/Okt: 50 Hz", at (50, lc), -48.2f, 1.0f);
    auto lc12 = F ([] (EQProcessor& p) { setParam (p.apvts, "b1on", 1); setParam (p.apvts, "b1freq", 100); setParam (p.apvts, "b1slope", 0); });
    check ("Low Cut 100 Hz 12 dB/Okt: 50 Hz", at (50, lc12), -12.3f, 0.5f);
    auto nt = F ([] (EQProcessor& p) { setParam (p.apvts, "b8on", 1); setParam (p.apvts, "b8type", 3); setParam (p.apvts, "b8freq", 1000); setParam (p.apvts, "b8q", 4); });
    check ("Notch 1 kHz", at (1000, nt), -60.0f, 30.0f);
    auto out = F ([] (EQProcessor& p) { setParam (p.apvts, "output", -3); });
    check ("Output -3 dB", at (1000, out), -3.0f, 0.02f);

    auto look = [] (EQProcessor& p) {
        setParam (p.apvts, "b1on", 1); setParam (p.apvts, "b1freq", 40); setParam (p.apvts, "b2gain", 2.5f);
        setParam (p.apvts, "b3freq", 320); setParam (p.apvts, "b3gain", -4); setParam (p.apvts, "b3q", 1.8f);
        setParam (p.apvts, "b4gain", 3); setParam (p.apvts, "b4freq", 2800); setParam (p.apvts, "b5gain", -2); setParam (p.apvts, "b5freq", 5200); setParam (p.apvts, "b5q", 5);
        setParam (p.apvts, "b6gain", 3.5f); setParam (p.apvts, "b6freq", 10000);
        setParam (p.apvts, "analog", 1);
    };
    render<EQProcessor, EQEditor> (dir, "onno-eq.png", 900, 470, look, -1);
    render<EQProcessor, EQEditor> (dir, "onno-eq-hover.png", 900, 470, look, 1);
}

static void checkBool (const char* what, bool ok)
{
    if (! ok) ++failures;
    std::printf ("%s %s\n", ok ? "OK  " : "FEHL", what);
}

static void testComp (const juce::File& dir)
{
    std::puts ("--- Onno Comp ---");
    using F = std::function<void (CompProcessor&)>;
    check ("-30 dBFS unter Threshold", measure<CompProcessor> (1000, 0.0316f, F ([] (CompProcessor&) {})), 0.0f, 0.05f);
    const float gr = measure<CompProcessor> (1000, 0.5f, F ([] (CompProcessor&) {}));
    check ("-6 dBFS, Thr -18, 4:1 (statisch -9.0)", gr, -8.6f, 0.8f);
    check ("Mix 0 % = trocken", measure<CompProcessor> (1000, 0.5f, F ([] (CompProcessor& p) { setParam (p.apvts, "mix", 0); })), 0.0f, 0.05f);
    check ("Auto Gain hebt an", measure<CompProcessor> (1000, 0.5f, F ([] (CompProcessor& p) { setParam (p.apvts, "autoGain", 1); })), gr + 6.75f, 0.1f);
    check ("Detektor-HPF 500 Hz: 60 Hz kaum komprimiert", measure<CompProcessor> (60, 0.5f, F ([] (CompProcessor& p) { setParam (p.apvts, "scHpf", 500); })), -0.5f, 0.6f);

    {   // Lookahead meldet Latenz
        CompProcessor p; p.prepareToPlay (48000, 512);
        setParam (p.apvts, "lookahead", 5);
        juce::AudioBuffer<float> b (2, 512); b.clear(); juce::MidiBuffer m;
        p.processBlock (b, m);
        checkBool ("Lookahead 5 ms = 240 Samples Latenz", p.getLatencySamples() == 240);
    }
    {   // echter Sidechain: Hauptsignal leise, Sidechain laut
        CompProcessor p;
        auto layout = p.getBusesLayout();
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        checkBool ("Sidechain-Bus aktivierbar", p.setBusesLayout (layout));
        p.prepareToPlay (48000, 512);
        setParam (p.apvts, "source", 1);
        juce::AudioBuffer<float> b (4, 512); juce::MidiBuffer m;
        long t = 0; double e = 0;
        for (int k = 0; k < 100; ++k)
        {
            for (int i = 0; i < 512; ++i, ++t)
            {
                const float v = 0.0316f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) t / 48000.0f);
                b.setSample (0, i, v); b.setSample (1, i, v); b.setSample (2, i, 0.9f); b.setSample (3, i, 0.9f);
            }
            p.processBlock (b, m);
            if (k >= 80) for (int i = 0; i < 512; ++i) e += b.getSample (0, i) * b.getSample (0, i);
        }
        const float lvl = (float) (10 * std::log10 (e / (20 * 512)) - 20 * std::log10 (0.0316 / std::sqrt (2.0)));
        check ("Sidechain laut drückt leises Signal", lvl, -12.8f, 1.0f);
    }
    render<CompProcessor, CompEditor> (dir, "onno-comp.png", 900, 470, [] (CompProcessor& p) { setParam (p.apvts, "thr", -24); setParam (p.apvts, "attack", 3); setParam (p.apvts, "analog", 4); setParam (p.apvts, "drive", 50); }, -1, 320, true);
}

static void testGate (const juce::File& dir)
{
    std::puts ("--- Onno Gate ---");
    using F = std::function<void (GateProcessor&)>;
    check ("-20 dBFS über Threshold: offen", measure<GateProcessor> (1000, 0.1f, F ([] (GateProcessor&) {})), 0.0f, 0.05f);
    check ("-60 dBFS, Range -40: zu", measure<GateProcessor> (1000, 0.001f, F ([] (GateProcessor& p) { setParam (p.apvts, "range", -40); })), -40.0f, 0.1f);
    check ("-60 dBFS, Range -80 (= -inf)", std::max (-200.0f, measure<GateProcessor> (1000, 0.001f, F ([] (GateProcessor&) {}))), -200.0f, 1.0f);
    check ("Detektor-HPF 2 kHz hält 100 Hz zu", measure<GateProcessor> (100, 0.1f, F ([] (GateProcessor& p) {
        setParam (p.apvts, "scHpf", 2000); setParam (p.apvts, "thr", -30); setParam (p.apvts, "range", -30); })), -30.0f, 0.5f);
    {   // Hold und Release: nach Signalende erst Hold, dann Abfall
        GateProcessor p; p.prepareToPlay (48000, 480);
        setParam (p.apvts, "hold", 100); setParam (p.apvts, "release", 20); setParam (p.apvts, "range", -60);
        juce::AudioBuffer<float> b (2, 480); juce::MidiBuffer m;
        std::vector<float> gains;
        for (int k = 0; k < 60; ++k)
        {
            const float lvl = k < 20 ? 0.5f : 0.0001f;    // -80 dBFS nach 200 ms
            for (int i = 0; i < 480; ++i) { b.setSample (0, i, lvl); b.setSample (1, i, lvl); }
            p.processBlock (b, m);
            gains.push_back (b.getSample (0, 479) / lvl);
        }
        checkBool ("Hold: 80 ms nach Ende noch offen", gains[27] > 0.9f);
        checkBool ("Release: 250 ms nach Ende zu", gains[45] < 0.01f);
    }
    render<GateProcessor, GateEditor> (dir, "onno-gate.png", 900, 470, [] (GateProcessor& p) { setParam (p.apvts, "thr", -24); }, -1, 320, true);
}

static void testDelay (const juce::File& dir)
{
    std::puts ("--- Onno Delay ---");
    DelayProcessor p; p.prepareToPlay (48000, 512);
    setParam (p.apvts, "wet", 0);
    juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
    std::vector<float> out;
    for (int k = 0; k < 220; ++k)
    {
        b.clear();
        if (k == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
        p.processBlock (b, m);
        for (int i = 0; i < 512; ++i) out.push_back (b.getSample (0, i));
    }
    check ("Direktsignal", 20 * std::log10 (std::abs (out[0])), 0.0f, 0.01f);
    check ("Echo 1 nach 1/4 @120 BPM (24000)", 20 * std::log10 (std::abs (out[24000])), 0.0f, 0.1f);
    check ("Echo 2 mit 35 % Feedback", 20 * std::log10 (std::abs (out[48000])), 20 * std::log10 (0.35f), 0.1f);
    float stray = 0;
    for (int i = 1; i < 23990; ++i) stray = std::max (stray, std::abs (out[(size_t) i]));
    checkBool ("Nichts zwischen Direktsignal und Echo", stray < 1.0e-4f);

    {   // Stabilität bei 98 % Feedback, Ping-Pong, alle Taps
        DelayProcessor q; q.prepareToPlay (48000, 512);
        for (int t = 0; t < 3; ++t)
        {
            setParam (q.apvts, DelayProcessor::id (t, "on").toRawUTF8(), 1);
            setParam (q.apvts, DelayProcessor::id (t, "fb").toRawUTF8(), 98);
            setParam (q.apvts, DelayProcessor::id (t, "pp").toRawUTF8(), t != 1);
            setParam (q.apvts, DelayProcessor::id (t, "sync").toRawUTF8(), 0);
            setParam (q.apvts, DelayProcessor::id (t, "time").toRawUTF8(), 20.0f + 30.0f * t);
        }
        juce::Random rnd (3); long t = 0; float mx = 0; bool finite = true;
        for (int k = 0; k < 2000; ++k)
        {
            fill (b, t, 300, k < 50 ? 0.5f : 0.0f, k < 50 ? 0.2f : 0.0f, rnd);
            q.processBlock (b, m);
            for (int i = 0; i < 512; ++i) { const float v = b.getSample (0, i); finite = finite && std::isfinite (v); mx = std::max (mx, std::abs (v)); }
        }
        checkBool ((juce::String ("98 % Feedback stabil (Spitze ") + juce::String (mx, 2) + ")").toRawUTF8(), finite && mx < 20.0f);
    }
    render<DelayProcessor, DelayEditor> (dir, "onno-delay.png", 900, 470, [] (DelayProcessor& p) {
        setParam (p.apvts, "t2on", 1); setParam (p.apvts, "t2pp", 1); setParam (p.apvts, "t3on", 1); setParam (p.apvts, "t3level", -12); setParam (p.apvts, "duck", 40); }, -1, 320, true);
}

static void testMultiComp (const juce::File& dir)
{
    std::puts ("--- Onno MultiComp ---");
    using F = std::function<void (MultiCompProcessor&)>;
    auto flat = F ([] (MultiCompProcessor& p) { for (int b = 0; b < 4; ++b) setParam (p.apvts, MultiCompProcessor::id (b, "thr").toRawUTF8(), 0); });
    for (float f : { 40.0f, 120.0f, 500.0f, 1000.0f, 3000.0f, 5000.0f, 12000.0f })
        // Toleranz 0,05 dB: die Allpass-Phase verschiebt bei tiefen Tönen das Messfenster minimal
        check (("Weichen-Summe flach bei " + juce::String (f) + " Hz").toRawUTF8(), measure<MultiCompProcessor> (f, 0.25f, flat), 0.0f, 0.05f);
    check ("Low-Mid -30 dB 4:1 bei 400 Hz", measure<MultiCompProcessor> (400, 0.25f, F ([] (MultiCompProcessor& p) {
        for (int b = 0; b < 4; ++b) setParam (p.apvts, MultiCompProcessor::id (b, "thr").toRawUTF8(), 0);
        setParam (p.apvts, "b2thr", -30); setParam (p.apvts, "b2ratio", 4); })), -12.5f, 1.5f);
    check ("Band-Gain High +6 dB bei 12 kHz", measure<MultiCompProcessor> (12000, 0.25f, F ([] (MultiCompProcessor& p) {
        for (int b = 0; b < 4; ++b) setParam (p.apvts, MultiCompProcessor::id (b, "thr").toRawUTF8(), 0);
        setParam (p.apvts, "b4gain", 6); })), 6.0f, 0.1f);
    check ("Solo High: 100 Hz fast weg", std::max (-80.0f, measure<MultiCompProcessor> (100, 0.25f, F ([] (MultiCompProcessor& p) { setParam (p.apvts, "b4solo", 1); }))), -80.0f, 30.0f);
    render<MultiCompProcessor, MultiCompEditor> (dir, "onno-multicomp.png", 900, 470, [] (MultiCompProcessor& p) { setParam (p.apvts, "b1thr", -30); setParam (p.apvts, "b2thr", -24); }, -1, 320, true);
}

static void testLimit (const juce::File& dir)
{
    std::puts ("--- Onno Limit ---");
    using F = std::function<void (LimitProcessor&)>;
    check ("-30 dBFS unberührt", measure<LimitProcessor> (1000, 0.0316f, F ([] (LimitProcessor&) {})), 0.0f, 0.01f);
    check ("Threshold -6 bei -30 dBFS = +6 dB", measure<LimitProcessor> (1000, 0.0316f, F ([] (LimitProcessor& p) { setParam (p.apvts, "thr", -6); })), 6.0f, 0.01f);
    LimitProcessor p; p.prepareToPlay (48000, 512);
    setParam (p.apvts, "thr", -12);
    checkBool ("Latenz 1,5 ms (71 Samples)", p.getLatencySamples() == 71);
    juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
    juce::Random rnd (4); long t = 0; float mx = 0;
    for (int k = 0; k < 400; ++k)
    {
        fill (b, t, 80, k % 40 < 3 ? 0.9f : 0.2f, 0.3f, rnd);     // Schläge mit Rauschen
        if (k % 17 == 0) { b.setSample (0, 100, 1.0f); b.setSample (1, 100, -1.0f); }
        p.processBlock (b, m);
        mx = std::max (mx, b.getMagnitude (0, 512));     // alle Kanäle
    }
    check ("Spitze bleibt unter Ceiling -0.3", 20 * std::log10 (mx), -0.3f, 0.005f);
    render<LimitProcessor, LimitEditor> (dir, "onno-limit.png", 760, 440, [] (LimitProcessor& p) { setParam (p.apvts, "thr", -8); }, -1, 320, true);
}

// Klirrfaktor eines 1-kHz-Sinus nach dem Plugin (in %) und Pegeländerung (dB)
template <typename Proc>
static std::pair<float, float> thd (float amp, std::function<void (Proc&)> setup)
{
    Proc p; p.prepareToPlay (48000, 480);
    setup (p);
    juce::AudioBuffer<float> b (2, 480); juce::MidiBuffer m;
    long t = 0; juce::Random rnd (5);
    double c = 0, s = 0, e = 0;
    for (int k = 0; k < 100; ++k)
    {
        fill (b, t, 1000, amp, 0, rnd);
        p.processBlock (b, m);
        if (k >= 90)
            for (int i = 0; i < 480; ++i)
            {
                const double ph = 2 * juce::MathConstants<double>::pi * i / 48.0, v = b.getSample (0, i);
                c += v * std::cos (ph); s += v * std::sin (ph); e += v * v;
            }
    }
    const double n = 4800, a1 = 2 * std::sqrt (c * c + s * s) / n;
    const double fund = a1 * a1 / 2 * n;
    return { (float) (100 * std::sqrt (std::max (0.0, e - fund) / fund)), (float) (20 * std::log10 (a1 / amp)) };
}

static void testAnalog()
{
    std::puts ("--- Analog-Klangfarben (im Onno EQ) ---");
    using F = std::function<void (EQProcessor&)>;
    const char* names[] = { "Clean", "Brit", "Console", "Tube", "FET" };
    for (int mdl = 0; mdl < 5; ++mdl)
    {
        auto quiet = thd<EQProcessor> (0.01f, F ([mdl] (EQProcessor& p) { setParam (p.apvts, "analog", (float) mdl); }));
        auto hot = thd<EQProcessor> (0.25f, F ([mdl] (EQProcessor& p) { setParam (p.apvts, "analog", (float) mdl); setParam (p.apvts, "drive", 100); }));
        auto mid = thd<EQProcessor> (0.25f, F ([mdl] (EQProcessor& p) { setParam (p.apvts, "analog", (float) mdl); setParam (p.apvts, "drive", 35); }));
        std::printf ("     %-8s leise: %+5.2f dB, Klirr %.3f %%   -12 dBFS Drive 35: %.2f %%   Drive 100: %.2f %% (%+.2f dB)\n",
                     names[mdl], quiet.second, quiet.first, mid.first, hot.first, hot.second);
        check ((juce::String (names[mdl]) + ": leiser Pegel bleibt").toRawUTF8(), quiet.second, 0.0f, 0.6f);
        if (mdl == 0) checkBool ("Clean: kein Klirr (Messgrenze 0,05 %)", hot.first < 0.05f);
        else checkBool ((juce::String (names[mdl]) + ": Drive erzeugt Obertöne (> 1 %)").toRawUTF8(), hot.first > 1.0f && mid.first < hot.first);
    }
    {   // Delay mit FET im Feedback bleibt bei 98 % stabil
        DelayProcessor q; q.prepareToPlay (48000, 512);
        setParam (q.apvts, "analog", 4); setParam (q.apvts, "drive", 100); setParam (q.apvts, "t1fb", 98);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; juce::Random rnd (6); long t = 0; float mx = 0; bool fin = true;
        for (int k = 0; k < 1500; ++k)
        {
            fill (b, t, 200, k < 30 ? 0.8f : 0.0f, 0, rnd); q.processBlock (b, m);
            for (int i = 0; i < 512; ++i) { fin = fin && std::isfinite (b.getSample (0, i)); mx = std::max (mx, std::abs (b.getSample (0, i))); }
        }
        checkBool ((juce::String ("Delay FET-Feedback 98 % stabil (Spitze ") + juce::String (mx, 2) + ")").toRawUTF8(), fin && mx < 4);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    // optional: nur bestimmte Plugins testen, z. B. "OnnoTests out EQ Comp"
    auto want = [&] (const char* n) {
        if (argc <= 2) return true;
        for (int i = 2; i < argc; ++i) if (juce::String (argv[i]) == n) return true;
        return false;
    };
    if (want ("VC1")) testVC1 (dir);
    if (want ("CS1")) testCS1 (dir);
    if (want ("RV1")) testRV1 (dir);
    if (want ("EQ")) testEQ (dir);
    if (want ("Comp")) testComp (dir);
    if (want ("Gate")) testGate (dir);
    if (want ("Delay")) testDelay (dir);
    if (want ("MultiComp")) testMultiComp (dir);
    if (want ("Limit")) testLimit (dir);
    if (want ("Analog")) testAnalog();
    std::printf ("\n%s (%d Fehler)\n", failures == 0 ? "ALLE TESTS OK" : "TESTS FEHLGESCHLAGEN", failures);
    return failures == 0 ? 0 : 1;
}
