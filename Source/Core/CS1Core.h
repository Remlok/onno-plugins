#pragma once
// Onno CS-1, Klang-Kern: Channel Strip
// Kette: Trim → Drive (2x überabgetastet) → Hoch-/Tiefpass → Gate → EQ ⇄ Kompressor → De-Esser → Output (+ Auto-Gain)
// Quell-Presets (Kick, Snare, …) sind die Werkspresets. Auto-Trim (Messung über 3 s) und das Behalten von
// TRIM/OUTPUT/AUTO GAIN beim Preset-Wechsel gehören zur Bedienung und liegen im Wrapper bzw. in der Oberfläche.
#include "Core/Core.h"
#include <complex>

namespace onno::core
{
// 2-fache Überabtastung mit Halbband-Polyphasen-IIR, gleiche Filter wie bisher juce::dsp::Oversampling
// (Faktor 2, filterHalfBandPolyphaseIIR, höchste Qualität): hoch Übergang 0,05 / -90 dB (7 Allpässe),
// runter 0,06 / -75 dB (6 Allpässe). Die Koeffizienten liefert der elliptische Entwurf aus OnnoDSP.h
// (rechnerisch derselbe wie JUCEs FilterDesign), das Runterrechnen ist wie in JUCE angeordnet
// (Zweig mit geraden Koeffizienten auf dem geraden Sample, der andere einen Takt verzögert), damit Phase
// und Latenz gleich bleiben. Gerechnet wird in double statt float.
class CS1Oversampler
{
public:
    static constexpr int NU = 7, ND = 6;

    CS1Oversampler() { u.c = upCoefs().c; d.c = downCoefs().c; }
    void reset() { u.reset(); d.reset(); odd = 0; }

    void up (float x, float& o0, float& o1) { u.up (x, o0, o1); }

    float down (float i0, float i1)
    {
        const float y = d.down (odd, i0);
        odd = i1;
        return y;
    }

    // Latenz auf der einfachen Rate wie Oversampling::getLatencyInSamples (Phasenlaufzeit bei sehr tiefer Frequenz)
    static double latency()
    {
        static const double l = 0.5 * (phaseDelay (upCoefs().c, NU) + phaseDelay (downCoefs().c, ND));
        return l;
    }

private:
    static const dsp::HalfbandCoefs<NU>& upCoefs() { static const dsp::HalfbandCoefs<NU> c (0.05); return c; }
    static const dsp::HalfbandCoefs<ND>& downCoefs() { static const dsp::HalfbandCoefs<ND> c (0.06); return c; }

    // Phasenlaufzeit (in Samples der doppelten Rate) von A0(z²) + z⁻¹·A1(z²)
    static double phaseDelay (const double* c, int nc)
    {
        const double w = dsp::twoPi * 0.0001;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        std::complex<double> a0 (1, 0), a1 = z1;
        for (int i = 0; i < nc; ++i)
            (i % 2 == 0 ? a0 : a1) *= (c[i] + z2) / (1.0 + c[i] * z2);
        return -std::arg (a0 + a1) / w;
    }

    dsp::Halfband<NU> u;
    dsp::Halfband<ND> d;
    float odd = 0;
};

class CS1Core : public PluginCore
{
public:
    static ParamList makeParams()
    {
        ParamList l;
        p::lin (l, "trim", "Input Trim", -24, 24, 0, "dB");
        p::lin (l, "drive", "Drive", 0, 100, 0, "%");
        p::lg (l, "hpf", "High-Pass", 16, 400, 16, "Hz");
        p::lg (l, "lpf", "Low-Pass", 3000, 22000, 22000, "Hz");

        p::bl (l, "gateOn", "Gate On", false);
        p::lin (l, "gateThr", "Gate Threshold", -80, 0, -50, "dB");
        p::lin (l, "gateRange", "Gate Range", 0, 60, 30, "dB");
        p::lg (l, "gateRel", "Gate Release", 10, 1000, 150, "ms");

        p::bl (l, "eqOn", "EQ On", true);
        p::lin (l, "lfGain", "LF Gain", -18, 18, 0, "dB");   p::lg (l, "lfFreq", "LF Freq", 30, 450, 100, "Hz");     p::bl (l, "lfBell", "LF Bell", false);
        p::lin (l, "lmfGain", "LMF Gain", -18, 18, 0, "dB"); p::lg (l, "lmfFreq", "LMF Freq", 200, 2500, 600, "Hz"); p::lg (l, "lmfQ", "LMF Q", 0.4f, 5, 1, "");
        p::lin (l, "hmfGain", "HMF Gain", -18, 18, 0, "dB"); p::lg (l, "hmfFreq", "HMF Freq", 600, 7000, 2500, "Hz"); p::lg (l, "hmfQ", "HMF Q", 0.4f, 5, 1, "");
        p::lin (l, "hfGain", "HF Gain", -18, 18, 0, "dB");   p::lg (l, "hfFreq", "HF Freq", 1500, 16000, 8000, "Hz"); p::bl (l, "hfBell", "HF Bell", false);

        p::bl (l, "compOn", "Comp On", false);
        p::lin (l, "compThr", "Comp Threshold", -50, 0, -20, "dB");
        p::lg (l, "compRatio", "Comp Ratio", 1, 20, 4, ":1");
        p::lg (l, "compAtt", "Comp Attack", 0.1f, 30, 10, "ms");
        p::lg (l, "compRel", "Comp Release", 30, 1500, 150, "ms");
        p::lin (l, "compMix", "Comp Mix", 0, 100, 100, "%");
        p::bl (l, "compPre", "Comp Pre EQ", false);

        p::bl (l, "dsOn", "De-Esser On", false);
        p::lg (l, "dsFreq", "De-Esser Freq", 3000, 12000, 6500, "Hz");
        p::lin (l, "dsThr", "De-Esser Threshold", -50, 0, -30, "dB");
        p::lin (l, "dsRange", "De-Esser Range", 0, 20, 6, "dB");

        p::lin (l, "out", "Output", -24, 24, 0, "dB");
        p::bl (l, "autoGain", "Auto Gain", false);
        p::bl (l, "bypass", "Bypass", false);
        p::ch (l, "view", "Ansicht", { "Konsole", "Modern" }, 0, false);
        return l;
    }

    enum { trim, drive, hpf, lpf, gateOn, gateThr, gateRange, gateRel,
           eqOn, lfGain, lfFreq, lfBell, lmfGain, lmfFreq, lmfQ, hmfGain, hmfFreq, hmfQ, hfGain, hfFreq, hfBell,
           compOn, compThr, compRatio, compAtt, compRel, compMix, compPre,
           dsOn, dsFreq, dsThr, dsRange, out, autoGain, bypass, view };

    // Quell-Presets (Reihenfolge = Schaltflächen der Workflow-Leiste)
    static constexpr int numPresets = 10;
    static constexpr const char* presetNames[numPresets] = {
        "Kick", "Snare", "Toms", "Bass", "Rhy Gtr", "Lead Gtr", "Vocal", "Scream", "Overhead", "Bus"
    };

    // disp: 0 Gate-GR, 1 Kompressor-GR, 2 De-Esser-GR (dB, tiefster Wert seit dem Abholen),
    //       3 Auto-Gain (dB, aktueller Wert),
    //       4/5 Auto-Trim-Messung vor dem Trim: Summe 0,5·(L²+R²) und Anzahl Samples, nur Blöcke über -60 dBFS
    //       (seit dem Abholen; die Oberfläche summiert, bis 3 s erreicht sind, und stellt TRIM auf -18 dBFS RMS)
    // grMin = Kompressor-GR, detPk unbenutzt
    CS1Core() : PluginCore (makeParams()) { disp.assign (6, 0.0f); }

    int latency() const override { return (int) std::lround (CS1Oversampler::latency()); }
    void resetDisplay() override { disp[0] = disp[1] = disp[2] = 0; disp[4] = disp[5] = 0; }
    double sampleRate() const { return sr; }

    bool keepOnPreset (const std::string& id) const override { return id == "trim" || id == "out" || id == "autoGain"; }

    std::vector<Preset> factoryPresets() const override
    {
        using V = std::vector<std::pair<std::string, float>>;
        // Startpunkte für typische Metal-Produktionen; alles Weitere bleibt auf Neutral
        const V values[numPresets] = {
            // KICK
            { { "hpf", 30 }, { "drive", 15 }, { "gateOn", 1 }, { "gateThr", -40 }, { "gateRange", 30 }, { "gateRel", 120 },
              { "lfGain", 4 }, { "lfFreq", 60 }, { "lfBell", 1 }, { "lmfGain", -5 }, { "lmfFreq", 350 }, { "lmfQ", 1.5f },
              { "hmfGain", 5 }, { "hmfFreq", 4000 }, { "hmfQ", 1.2f }, { "hfGain", 1 }, { "hfFreq", 10000 },
              { "compOn", 1 }, { "compThr", -20 }, { "compRatio", 4 }, { "compAtt", 8 }, { "compRel", 120 } },
            // SNARE
            { { "hpf", 70 }, { "drive", 20 }, { "gateOn", 1 }, { "gateThr", -35 }, { "gateRange", 20 }, { "gateRel", 180 },
              { "lfGain", 3 }, { "lfFreq", 200 }, { "lfBell", 1 }, { "lmfGain", -3 }, { "lmfFreq", 500 }, { "lmfQ", 1.4f },
              { "hmfGain", 3 }, { "hmfFreq", 5000 }, { "hfGain", 3 }, { "hfFreq", 12000 },
              { "compOn", 1 }, { "compThr", -22 }, { "compRatio", 4 }, { "compAtt", 10 }, { "compRel", 150 }, { "compMix", 80 } },
            // TOMS
            { { "hpf", 50 }, { "gateOn", 1 }, { "gateThr", -38 }, { "gateRange", 30 }, { "gateRel", 250 },
              { "lfGain", 3 }, { "lfFreq", 100 }, { "lfBell", 1 }, { "lmfGain", -5 }, { "lmfFreq", 400 }, { "lmfQ", 1.2f },
              { "hmfGain", 4 }, { "hmfFreq", 3500 }, { "hfGain", 1 }, { "hfFreq", 10000 },
              { "compOn", 1 }, { "compThr", -20 }, { "compRatio", 4 }, { "compAtt", 10 }, { "compRel", 200 } },
            // BASS
            { { "hpf", 30 }, { "drive", 35 }, { "lfGain", 2 }, { "lfFreq", 80 }, { "lmfGain", -3 }, { "lmfFreq", 250 },
              { "hmfGain", 3 }, { "hmfFreq", 1500 }, { "compOn", 1 }, { "compThr", -24 }, { "compRatio", 6 }, { "compAtt", 5 }, { "compRel", 80 } },
            // RHY GTR
            { { "hpf", 90 }, { "lpf", 10000 }, { "lfGain", -2 }, { "lfFreq", 120 }, { "lfBell", 1 },
              { "lmfGain", -3 }, { "lmfFreq", 300 }, { "lmfQ", 1.2f }, { "hmfGain", -2.5f }, { "hmfFreq", 3200 }, { "hmfQ", 2.5f },
              { "hfGain", -3 }, { "hfFreq", 7500 }, { "hfBell", 1 },
              { "compOn", 1 }, { "compThr", -12 }, { "compRatio", 2 }, { "compAtt", 20 }, { "compRel", 150 } },
            // LEAD GTR
            { { "hpf", 100 }, { "lpf", 12000 }, { "lmfGain", -2 }, { "lmfFreq", 500 }, { "hmfGain", 2.5f }, { "hmfFreq", 2200 },
              { "hfGain", 1 }, { "hfFreq", 10000 }, { "compOn", 1 }, { "compThr", -18 }, { "compRatio", 3 }, { "compAtt", 15 }, { "compRel", 150 } },
            // VOCAL
            { { "hpf", 90 }, { "drive", 10 }, { "lmfGain", -2 }, { "lmfFreq", 300 }, { "hmfGain", 2 }, { "hmfFreq", 3000 },
              { "hfGain", 3 }, { "hfFreq", 12000 }, { "compOn", 1 }, { "compThr", -20 }, { "compRatio", 4 }, { "compAtt", 5 }, { "compRel", 100 },
              { "dsOn", 1 }, { "dsFreq", 6500 }, { "dsThr", -30 }, { "dsRange", 6 } },
            // SCREAM
            { { "hpf", 120 }, { "lpf", 14000 }, { "drive", 25 }, { "lmfGain", -3 }, { "lmfFreq", 400 },
              { "hmfGain", -2 }, { "hmfFreq", 2500 }, { "hmfQ", 2 }, { "hfGain", 2 }, { "hfFreq", 10000 },
              { "compOn", 1 }, { "compThr", -24 }, { "compRatio", 6 }, { "compAtt", 2 }, { "compRel", 60 },
              { "dsOn", 1 }, { "dsFreq", 5500 }, { "dsThr", -28 }, { "dsRange", 8 } },
            // OVERHEAD
            { { "hpf", 180 }, { "lmfGain", -2 }, { "lmfFreq", 500 }, { "hmfGain", -1.5f }, { "hmfFreq", 3500 }, { "hmfQ", 2 },
              { "hfGain", 2 }, { "hfFreq", 12000 }, { "compOn", 1 }, { "compThr", -16 }, { "compRatio", 2 }, { "compAtt", 25 }, { "compRel", 200 }, { "compMix", 60 } },
            // BUS
            { { "hpf", 25 }, { "drive", 10 }, { "lfGain", 1 }, { "lfFreq", 60 }, { "hfGain", 1 }, { "hfFreq", 12000 },
              { "compOn", 1 }, { "compThr", -14 }, { "compRatio", 2 }, { "compAtt", 30 }, { "compRel", 300 } },
        };
        std::vector<Preset> l;
        for (int i = 0; i < numPresets; ++i) l.push_back ({ presetNames[i], "", values[i] });
        return l;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& o : os) o.reset();
        for (auto* b : { &hp1, &hp2, &lp, &lf, &lmf, &hmf, &hf, &dsLo1, &dsLo2, &dsHi1, &dsHi2, &dcBlock }) b->reset();
        dcBlock.highPass1 (sr, 5.0);
        gateEnv = -120; gateGain = 0; gateHold = 0; compEnv = 0; dsEnv = 0;
        autoIn = autoOut = 0; autoG = 1;
    }

    // EQ-Kurve für die Anzeige (Betrag in dB, inklusive Filter); value(i) = Wert des Parameters i
    template <typename Get>
    static float responseDb (Get value, double sr, float f)
    {
        dsp::Biquad b; double sum = 0;
        auto on = [&] (int i) { return value (i) > 0.5f; };
        const float hpfV = value (hpf), lpfV = value (lpf);
        if (hpfV > 16.5f) { b.highPass1 (sr, hpfV); sum += b.magDb (sr, f); b.highPass (sr, hpfV, 1.0); sum += b.magDb (sr, f); }
        if (lpfV < 21500.0f) { b.lowPass (sr, std::min ((double) lpfV, sr * 0.45), 0.7071); sum += b.magDb (sr, f); }
        if (on (eqOn))
        {
            if (on (lfBell)) b.peak (sr, value (lfFreq), 0.9, value (lfGain)); else b.lowShelf (sr, value (lfFreq), 0.7071, value (lfGain));
            sum += b.magDb (sr, f);
            b.peak (sr, value (lmfFreq), value (lmfQ), value (lmfGain)); sum += b.magDb (sr, f);
            b.peak (sr, std::min ((double) value (hmfFreq), sr * 0.45), value (hmfQ), value (hmfGain)); sum += b.magDb (sr, f);
            const double hff = std::min ((double) value (hfFreq), sr * 0.45);
            if (on (hfBell)) b.peak (sr, hff, 0.9, value (hfGain)); else b.highShelf (sr, hff, 0.7071, value (hfGain));
            sum += b.magDb (sr, f);
        }
        return (float) sum;
    }

    float responseDb (float f) const { return responseDb ([this] (int i) { return v[(size_t) i]; }, sr, f); }

    void process (float* L, float* R, int n, int nch, const float*, const float*) override
    {
        if (nch == 0 || n == 0) return;
        if (nch < 2) R = L;

        // Auto-Trim-Messung: Eingangspegel vor dem Trim (nur Blöcke mit Signal)
        {
            double e = 0;
            for (int i = 0; i < n; ++i) e += 0.5 * (L[i] * L[i] + R[i] * R[i]);
            if (e / n > 1.0e-6) { disp[4] += (float) e; disp[5] += (float) n; }
        }

        if (on (bypass))
        {
            float pk = 0;
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
            inPk = outPk = pk; grMin = 0; detPk = 0;
            return;
        }

        updateFilters();

        // --- Trim ---
        const float trimG = dsp::db2a (v[trim]);
        for (int i = 0; i < n; ++i) { L[i] *= trimG; if (nch > 1) R[i] *= trimG; }

        // Pegel nach dem Trim für Meter und Auto-Gain
        float inMax = 0;
        for (int i = 0; i < n; ++i) inMax = std::max (inMax, std::max (std::abs (L[i]), std::abs (R[i])));
        // Auto-Gain: Lautheit des getrimmten Eingangs (Zeitkonstante 1,5 s)
        const bool ag = on (autoGain);
        const float aMs = dsp::coef (1500.0f, sr), aSm = dsp::coef (50.0f, sr);
        if (ag)
            for (int i = 0; i < n; ++i) autoIn = aMs * autoIn + (1 - aMs) * 0.5f * (L[i] * L[i] + R[i] * R[i]);

        // --- Drive: weiche Sättigung mit leichter Asymmetrie, 2x überabgetastet (läuft immer, wegen gleicher Latenz) ---
        {
            const float dr = v[drive] * 0.01f;
            const bool shape = dr > 0.001f;
            const float k = 1.0f + 9.0f * dr * dr, bias = 0.15f * dr;
            const float tb = std::tanh (bias), norm = 1.0f / (k * (1 - tb * tb));
            for (int c = 0; c < nch; ++c)
            {
                float* d = c == 0 ? L : R;
                auto& o = os[c];
                for (int i = 0; i < n; ++i)
                {
                    float a, b;
                    o.up (d[i], a, b);
                    if (shape)
                    {
                        a = (std::tanh (k * a + bias) - tb) * norm;
                        b = (std::tanh (k * b + bias) - tb) * norm;
                    }
                    d[i] = o.down (a, b);
                }
            }
        }

        // --- Parameter für die Schleife ---
        const bool gOn = on (gateOn), eOn = on (eqOn), cOn = on (compOn), cPre = on (compPre), dOn = on (dsOn);
        const bool driveOn = v[drive] > 0.1f;
        const float gThr = v[gateThr], gRange = v[gateRange];
        const float gRel = dsp::coef (v[gateRel], sr), gAtt = dsp::coef (0.3f, sr), gDet = dsp::coef (20.0f, sr);
        const int gHoldN = (int) (0.015 * sr);
        const float cThr = v[compThr], cRatio = std::max (1.0f, v[compRatio]), cKnee = 6.0f;
        const float cAtt = dsp::coef (v[compAtt], sr), cRel = dsp::coef (v[compRel], sr), cMix = v[compMix] * 0.01f;
        const float dThr = v[dsThr], dRange = v[dsRange], dAtt = dsp::coef (1.0f, sr), dRel = dsp::coef (60.0f, sr);
        const float outG = dsp::db2a (v[out]);
        float gMin = 0, cMin = 0, dMin = 0, outMax = 0;

        auto eq = [&] (float& l, float& r) {
            l = hf.process (hmf.process (lmf.process (lf.process (l, 0), 0), 0), 0);
            r = hf.process (hmf.process (lmf.process (lf.process (r, 1), 1), 1), 1);
        };
        auto comp = [&] (float& l, float& r) {
            const float xdb = dsp::a2db (std::max (std::abs (l), std::abs (r)));
            const float d = xdb - cThr;
            float y = xdb;
            if (2 * d >= -cKnee)
                y = (2 * std::abs (d) <= cKnee) ? xdb + (1 / cRatio - 1) * (d + cKnee * 0.5f) * (d + cKnee * 0.5f) / (2 * cKnee)
                                               : cThr + d / cRatio;
            const float gr = y - xdb;
            compEnv = gr < compEnv ? cAtt * compEnv + (1 - cAtt) * gr : cRel * compEnv + (1 - cRel) * gr;
            const float g = cMix * dsp::db2a (compEnv) + 1 - cMix;
            l *= g; r *= g;
            cMin = std::min (cMin, compEnv);
        };

        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (driveOn) { l = dcBlock.process (l, 0); r = dcBlock.process (r, 1); }

            // Filter
            if (hpOn) { l = hp2.process (hp1.process (l, 0), 0); r = hp2.process (hp1.process (r, 1), 1); }
            if (lpOn) { l = lp.process (l, 0); r = lp.process (r, 1); }

            // Gate / Expander (1:3 unter der Schwelle, begrenzt auf Range, mit Hold)
            if (gOn)
            {
                const float lvl = dsp::a2db (std::max (std::abs (l), std::abs (r)));
                gateEnv = lvl > gateEnv ? lvl : gDet * gateEnv + (1 - gDet) * lvl;
                float target = 0;
                if (gateEnv >= gThr) gateHold = gHoldN;
                else if (gateHold > 0) --gateHold;
                else target = std::max (-gRange, (gateEnv - gThr) * 2.0f);
                gateGain = target > gateGain ? gAtt * gateGain + (1 - gAtt) * target : gRel * gateGain + (1 - gRel) * target;
                const float g = dsp::db2a (gateGain);
                l *= g; r *= g;
                gMin = std::min (gMin, gateGain);
            }

            if (cPre && cOn) comp (l, r);
            if (eOn) eq (l, r);
            if (! cPre && cOn) comp (l, r);

            // De-Esser: nur der Bereich über der Frequenz wird abgesenkt
            if (dOn)
            {
                const float hl = dsHi2.process (dsHi1.process (l, 0), 0), hr = dsHi2.process (dsHi1.process (r, 1), 1);
                const float ll = dsLo2.process (dsLo1.process (l, 0), 0), lr = dsLo2.process (dsLo1.process (r, 1), 1);
                const float lvl = dsp::a2db (std::max (std::abs (hl), std::abs (hr)));
                const float target = lvl > dThr ? std::max (-dRange, -(lvl - dThr) * 0.75f) : 0.0f;
                dsEnv = target < dsEnv ? dAtt * dsEnv + (1 - dAtt) * target : dRel * dsEnv + (1 - dRel) * target;
                const float g = dsp::db2a (dsEnv);
                l = ll + g * hl; r = lr + g * hr;
                dMin = std::min (dMin, dsEnv);
            }

            // Auto-Gain: Ausgang auf die Lautheit des (getrimmten) Eingangs angleichen
            if (ag)
            {
                autoOut = aMs * autoOut + (1 - aMs) * 0.5f * (l * l + r * r);
                if (autoIn > 1.0e-7f && autoOut > 1.0e-9f)
                {
                    const float t = std::clamp (std::sqrt (autoIn / autoOut), 0.25f, 4.0f);
                    autoG = aSm * autoG + (1 - aSm) * t;
                }
                l *= autoG; r *= autoG;
            }
            else autoG = 1.0f;

            l *= outG; r *= outG;
            L[i] = l;
            if (nch > 1) R[i] = r;
            outMax = std::max (outMax, std::max (std::abs (l), std::abs (r)));
        }

        inPk = inMax; outPk = outMax; grMin = cMin; detPk = 0;
        disp[0] = std::min (disp[0], gMin);
        disp[1] = std::min (disp[1], cMin);
        disp[2] = std::min (disp[2], dMin);
        disp[3] = dsp::a2db (autoG);
    }

private:
    bool on (int i) const { return v[(size_t) i] > 0.5f; }

    void updateFilters()
    {
        const float hpfV = v[hpf], lpfV = v[lpf];
        hpOn = hpfV > 16.5f;
        lpOn = lpfV < 21500.0f;
        // Hochpass 18 dB/Okt (Butterworth 3. Ordnung), Tiefpass 12 dB/Okt
        hp1.highPass1 (sr, hpfV);
        hp2.highPass (sr, hpfV, 1.0);
        lp.lowPass (sr, std::min ((double) lpfV, sr * 0.45), 0.7071);
        lf.lowShelf (sr, v[lfFreq], 0.7071, v[lfGain]);
        if (on (lfBell)) lf.peak (sr, v[lfFreq], 0.9, v[lfGain]);
        lmf.peak (sr, v[lmfFreq], v[lmfQ], v[lmfGain]);
        hmf.peak (sr, std::min ((double) v[hmfFreq], sr * 0.45), v[hmfQ], v[hmfGain]);
        hf.highShelf (sr, std::min ((double) v[hfFreq], sr * 0.45), 0.7071, v[hfGain]);
        if (on (hfBell)) hf.peak (sr, std::min ((double) v[hfFreq], sr * 0.45), 0.9, v[hfGain]);
        // Linkwitz-Riley-Weiche 24 dB/Okt: beide Hälften summieren sich ohne Pegelfehler
        const double dsf = std::min ((double) v[dsFreq], sr * 0.45);
        dsLo1.lowPass (sr, dsf, 0.7071); dsLo2.lowPass (sr, dsf, 0.7071);
        dsHi1.highPass (sr, dsf, 0.7071); dsHi2.highPass (sr, dsf, 0.7071);
    }

    double sr = 48000.0;
    CS1Oversampler os[2];
    dsp::Biquad hp1, hp2, lp, lf, lmf, hmf, hf, dsLo1, dsLo2, dsHi1, dsHi2, dcBlock;
    bool hpOn = false, lpOn = false;

    float gateEnv = -120.0f, gateGain = 0.0f; int gateHold = 0;
    float compEnv = 0.0f;
    float dsEnv = 0.0f;
    float autoIn = 0.0f, autoOut = 0.0f, autoG = 1.0f;
};
} // namespace onno::core
