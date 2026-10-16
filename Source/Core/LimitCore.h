#pragma once
// Onno Limit, Klang-Kern: Brickwall-Limiter im Stil von ReaLimit (Lookahead 1,5 ms, keine Übersteuerung über die Ceiling).
// TRUE PEAK: erkennt auch Spitzen zwischen den Samples (8-fach interpoliert), damit nach dem
// Umwandeln (MP3/AAC, D/A-Wandler) nichts über die Ceiling geht. Die Erkennung läuft immer tpDelay
// Samples hinterher, darum ist die Latenz in beiden Modi gleich (1,5 ms + 12 Samples).
#include "Core/Core.h"
#include "Core/AnalogCore.h"

namespace onno::core
{
class LimitCore : public PluginCore
{
public:
    static ParamList makeParams()
    {
        ParamList l;
        p::lin (l, "thr", "Threshold", -24, 0, 0, "dB");
        p::lin (l, "ceiling", "Ceiling", -12, 0, -0.3f, "dB");
        p::lg (l, "release", "Release", 1, 1000, 60, "ms");
        p::bl (l, "tp", "True Peak", true);
        AnalogCore::addParams (l);
        p::bl (l, "bypass", "Bypass", false);
        return l;
    }

    enum { thr, ceiling, release, tp, analogModel, analogDrive, bypass };

    // keine weiteren Anzeigewerte: detPk = Spitze nach Threshold (und Sättigung), wie sie der Limiter sieht
    LimitCore() : PluginCore (makeParams())
    {
        // Interpolationsfilter: gefensterter Sinc, Phase p liegt p/8 Sample hinter dem Sample tpDelay
        const double pi = dsp::twoPi * 0.5;
        for (int ph = 1; ph < tpPhases; ++ph)
        {
            double sum = 0;
            for (int k = 0; k < tpTaps; ++k)
            {
                const double d = (tpTaps - 1 - tpDelay) + ph / (double) tpPhases - k;   // Abstand zum Stützpunkt
                const double x = pi * d;
                const double sinc = std::abs (d) < 1e-9 ? 1.0 : std::sin (x) / x;
                const double w = 0.5 + 0.5 * std::cos (pi * d / (tpTaps * 0.5 + 0.5));
                tpH[ph][k] = (float) (sinc * w);
                sum += sinc * w;
            }
            for (int k = 0; k < tpTaps; ++k) tpH[ph][k] = (float) (tpH[ph][k] / sum);
        }
    }

    int latency() const override { return L - 1 + tpDelay; }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        L = std::max (2, (int) std::round (0.0015 * sr));
        for (auto& d : dl) d.assign ((size_t) (L + 1 + tpDelay), 0.0f);
        for (auto& h : hist) std::fill (std::begin (h), std::end (h), 0.0f);
        histPos = 0;
        box.assign ((size_t) L, 1.0f);
        boxSum = L;
        dqv.assign ((size_t) L + 2, 1.0f);
        dqi.assign ((size_t) L + 2, 0);
        dlPos = boxPos = dqHead = dqSize = 0;
        t = 0; env = 1;
        analog.prepare (sr);
        driveS.reset (sr, 0.02); driveS.setCurrentAndTargetValue (dsp::db2a (-v[thr]));
        ceilS.reset (sr, 0.02); ceilS.setCurrentAndTargetValue (dsp::db2a (v[ceiling]));
    }

    void process (float* Lp, float* Rp, int n, int nch, const float*, const float*) override
    {
        if (nch == 0) return;
        if (nch < 2) Rp = Lp;

        const bool byp = v[bypass] > 0.5f;
        driveS.setTargetValue (dsp::db2a (-v[thr]));
        ceilS.setTargetValue (dsp::db2a (v[ceiling]));
        const float relC = dsp::coef (v[release], sr);
        analog.beginBlock (v[analogModel], v[analogDrive]);
        const bool sat = analog.active() && ! byp;
        const bool usTp = v[tp] > 0.5f;
        const int cap = (int) dqv.size(), D = (int) dl[0].size();
        float inMax = 0, drvPk = 0, outMax = 0, gMin = 1;

        for (int i = 0; i < n; ++i, ++t)
        {
            const float xl = Lp[i], xr = Rp[i];
            float drive = driveS.getNextValue();
            const float cl = ceilS.getNextValue();
            // Sättigung vor dem Limiter (nach dem Threshold-Gain), danach ohne weiteres Gain
            float sl = xl, sr2 = xr;
            if (sat) { analog.advance(); sl = analog.tick (xl * drive, 0); sr2 = analog.tick (xr * drive, 1); drive = 1.0f; }
            // Verlauf für die Spitzenerkennung (Erkennung läuft tpDelay Samples hinterher, Audio entsprechend länger verzögert)
            hist[0][histPos] = sl * drive; hist[1][histPos] = sr2 * drive;
            histPos = (histPos + 1) % tpTaps;
            const float pk = usTp ? std::max (truePeak (0), truePeak (1))
                                  : std::max (std::abs (hist[0][(histPos + tpTaps - 1 - tpDelay) % tpTaps]),
                                              std::abs (hist[1][(histPos + tpTaps - 1 - tpDelay) % tpTaps]));
            const float r = pk > cl ? cl / pk : 1.0f;

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
            int rp = dlPos - (L - 1 + tpDelay); if (rp < 0) rp += D;
            const float yl = dl[0][(size_t) rp], yr = dl[1][(size_t) rp];
            if (++dlPos >= D) dlPos = 0;

            float ol = yl, orr = yr;
            if (! byp)
            {
                ol = std::clamp (yl * drive * g, -cl, cl);
                orr = std::clamp (yr * drive * g, -cl, cl);
                gMin = std::min (gMin, g);
            }
            Lp[i] = ol;
            if (nch > 1) Rp[i] = orr;
            inMax = std::max (inMax, std::max (std::abs (xl), std::abs (xr)));
            drvPk = std::max (drvPk, pk);
            outMax = std::max (outMax, std::max (std::abs (ol), std::abs (orr)));
        }
        inPk = inMax; outPk = outMax; grMin = dsp::a2db (gMin); detPk = drvPk;
    }

private:
    static constexpr int tpTaps = 24, tpPhases = 8, tpDelay = 12;

    // größter Betrag zwischen Sample (n - tpDelay) und dem nächsten, aus den letzten tpTaps Samples interpoliert
    float truePeak (int ch) const
    {
        const float* h = hist[ch];
        float pk = std::abs (h[(histPos + tpTaps - 1 - tpDelay) % tpTaps]);
        for (int ph = 1; ph < tpPhases; ++ph)
        {
            float acc = 0;
            for (int k = 0; k < tpTaps; ++k) acc += tpH[ph][k] * h[(histPos + k) % tpTaps];   // histPos = ältester Wert
            pk = std::max (pk, std::abs (acc));
        }
        return pk;
    }

    float tpH[tpPhases][tpTaps] {};
    float hist[2][tpTaps] {};
    int histPos = 0;
    double sr = 48000;
    int L = 72;                               // Fensterlänge in Samples
    std::vector<float> dl[2], box, dqv;
    std::vector<long long> dqi;
    int dlPos = 0, boxPos = 0, dqHead = 0, dqSize = 0;
    long long t = 0;
    double boxSum = 0;
    float env = 1;
    Smoothed driveS, ceilS;
    AnalogCore analog;
};
} // namespace onno::core
