// Test-Programm: misst die DSP und rendert die Oberflächen in PNG-Dateien (ohne DAW)
// Aufruf: OnnoTests <Ausgabeordner>
#include "VC1/VC1Processor.h"
#include "VC1/VC1Editor.h"
#include "CS1/CS1Processor.h"
#include "CS1/CS1Editor.h"
#include "RV1/RV1Processor.h"
#include "RV1/RV1Editor.h"
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
static void render (const juce::File& dir, const juce::String& name, int w, int h, std::function<void (Proc&)> setup, int hot)
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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    testVC1 (dir);
    testCS1 (dir);
    testRV1 (dir);
    std::printf ("\n%s (%d Fehler)\n", failures == 0 ? "ALLE TESTS OK" : "TESTS FEHLGESCHLAGEN", failures);
    return failures == 0 ? 0 : 1;
}
