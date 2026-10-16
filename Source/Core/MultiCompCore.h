#pragma once
// Onno MultiComp, Klang-Kern: 4-Band-Kompressor im Stil von ReaXcomp (Linkwitz-Riley-Weichen LR4, Solo, Band-Bypass)
// Der Spektrum-Analyzer gehört zur Oberfläche (VST3: Common/Analyzer.h, Browser: AnalyserNode), nicht in den Kern.
#include "Core/Core.h"
#include "Core/AnalogCore.h"
#include <array>

namespace onno::core
{
class MultiCompCore : public PluginCore
{
public:
    static constexpr int numBands = 4;
    // Parameter je Band, Reihenfolge wie in der Tabelle: b<n>thr, b<n>ratio, ... b<n>byp
    enum { pThr, pRatio, pAtt, pRel, pGain, pSolo, pByp, numP };

    static std::string id (int b, const char* p) { return "b" + std::to_string (b + 1) + p; }

    static ParamList makeParams()
    {
        ParamList l;
        const float att[] = { 20, 10, 5, 3 }, rel[] = { 200, 150, 100, 80 };
        for (int b = 0; b < numBands; ++b)
        {
            const std::string n = "Band " + std::to_string (b + 1) + " ";
            p::lin (l, id (b, "thr"), n + "Threshold", -60, 0, -18, "dB");
            p::lg (l, id (b, "ratio"), n + "Ratio", 1, 20, 2, ":1");
            p::lg (l, id (b, "attack"), n + "Attack", 0.1f, 200, att[b], "ms");
            p::lg (l, id (b, "release"), n + "Release", 10, 2000, rel[b], "ms");
            p::lin (l, id (b, "gain"), n + "Gain", -24, 24, 0, "dB");
            p::bl (l, id (b, "solo"), n + "Solo", false);
            p::bl (l, id (b, "byp"), n + "Bypass", false);
        }
        p::lg (l, "x1", "Crossover Low", 20, 1000, 120, "Hz");
        p::lg (l, "x2", "Crossover Mid", 150, 6000, 1000, "Hz");
        p::lg (l, "x3", "Crossover High", 1000, 16000, 5000, "Hz");
        p::lin (l, "mix", "Mix", 0, 100, 100, "%");
        p::lin (l, "output", "Output", -24, 24, 0, "dB");
        AnalogCore::addParams (l);
        p::bl (l, "bypass", "Bypass", false);
        return l;
    }

    static constexpr int bp (int b, int k) { return b * numP + k; }
    enum { x1 = numBands * numP, x2, x3, mix, output, analogModel, analogDrive, bypass };

    // disp[0..3]: tiefste Gain Reduction je Band (dB, <= 0; LOW, LOW-MID, HIGH-MID, HIGH) seit dem letzten Abholen
    MultiCompCore() : PluginCore (makeParams()) { disp.assign (numBands, 0.0f); }

    void resetDisplay() override { std::fill (disp.begin(), disp.end(), 0.0f); }

    // Weichenfrequenzen mit Mindestabstand (auch für die Anzeige)
    static void crossovers (double sampleRate, float v1, float v2, float v3, float& f1, float& f2, float& f3)
    {
        const float ny = (float) sampleRate * 0.45f;
        f1 = std::min (v1, ny / 2.0f);
        f2 = std::clamp (v2, f1 * 1.25f, ny / 1.5f);
        f3 = std::clamp (v3, f2 * 1.25f, ny);
    }
    void crossovers (float& f1, float& f2, float& f3) const { crossovers (sr, v[x1], v[x2], v[x3], f1, f2, f3); }
    double sampleRate() const { return sr; }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        fx1 = fx2 = fx3 = -1;
        for (auto* f : { &lp2[0], &lp2[1], &hp2[0], &hp2[1], &ap3, &lp1[0], &lp1[1], &hp1[0], &hp1[1], &ap1, &lp3[0], &lp3[1], &hp3[0], &hp3[1] })
            f->reset();
        env.fill (0.0f);
        analog.prepare (sr);
        for (int b = 0; b < numBands; ++b)
        {
            makeup[(size_t) b].reset (sr, 0.03);
            makeup[(size_t) b].setCurrentAndTargetValue (dsp::db2a (v[(size_t) bp (b, pGain)]));
        }
        outS.reset (sr, 0.03); outS.setCurrentAndTargetValue (dsp::db2a (v[output]));
        mixS.reset (sr, 0.03); mixS.setCurrentAndTargetValue (v[mix] * 0.01f);
    }

    void process (float* L, float* R, int n, int nch, const float*, const float*) override
    {
        if (nch == 0) return;
        if (nch < 2) R = L;

        if (v[bypass] > 0.5f)
        {
            float pk = 0;
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
            inPk = outPk = pk; grMin = 0; detPk = 0;
            return;
        }

        float f1, f2, f3;
        crossovers (f1, f2, f3);
        if (f1 != fx1) { fx1 = f1; for (auto& f : lp1) f.lowPass (sr, f1, 0.7071); for (auto& f : hp1) f.highPass (sr, f1, 0.7071); ap1.allPass (sr, f1, 0.7071); }
        if (f2 != fx2) { fx2 = f2; for (auto& f : lp2) f.lowPass (sr, f2, 0.7071); for (auto& f : hp2) f.highPass (sr, f2, 0.7071); }
        if (f3 != fx3) { fx3 = f3; for (auto& f : lp3) f.lowPass (sr, f3, 0.7071); for (auto& f : hp3) f.highPass (sr, f3, 0.7071); ap3.allPass (sr, f3, 0.7071); }

        struct B { float thr, ratio, att, rel; bool solo, byp; };
        std::array<B, numBands> bs;
        bool anySolo = false;
        for (int b = 0; b < numBands; ++b)
        {
            auto p = [&] (int k) { return v[(size_t) bp (b, k)]; };
            bs[(size_t) b] = { p (pThr), std::max (1.0f, p (pRatio)), dsp::coef (p (pAtt), sr), dsp::coef (p (pRel), sr),
                               p (pSolo) > 0.5f, p (pByp) > 0.5f };
            anySolo = anySolo || bs[(size_t) b].solo;
            makeup[(size_t) b].setTargetValue (dsp::db2a (p (pGain)));
        }
        outS.setTargetValue (dsp::db2a (v[output]));
        mixS.setTargetValue (v[mix] * 0.01f);
        analog.beginBlock (v[analogModel], v[analogDrive]);

        std::array<float, numBands> grLow {};
        float inMax = 0, outMax = 0;
        for (int i = 0; i < n; ++i)
        {
            float band[numBands][2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = c == 0 ? L[i] : R[i];
                inMax = std::max (inMax, std::abs (x));
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
                grLow[(size_t) b] = std::min (grLow[(size_t) b], e);
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
            outMax = std::max (outMax, std::max (std::abs (ol), std::abs (orr)));
        }
        float gAll = 0;
        for (int b = 0; b < numBands; ++b)
        {
            disp[(size_t) b] = std::min (disp[(size_t) b], grLow[(size_t) b]);
            gAll = std::min (gAll, grLow[(size_t) b]);
        }
        inPk = inMax; outPk = outMax; grMin = gAll; detPk = inMax;
    }

private:
    double sr = 48000;
    float fx1 = -1, fx2 = -1, fx3 = -1;
    // Weichen: LR4 = zwei Butterworth-Stufen
    dsp::Biquad lp2[2], hp2[2], ap3, lp1[2], hp1[2], ap1, lp3[2], hp3[2];
    std::array<float, numBands> env {};
    std::array<Smoothed, numBands> makeup;
    Smoothed outS, mixS;
    AnalogCore analog;
};
} // namespace onno::core
