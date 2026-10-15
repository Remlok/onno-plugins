#pragma once
// Onno Comp, Klang-Kern: Kompressor im Stil von ReaComp (Knee, Lookahead, RMS, Sidechain mit Filter, Parallel-Mix)
#include "Core/Core.h"
#include "Core/AnalogCore.h"

namespace onno::core
{
class CompCore : public PluginCore
{
public:
    static ParamList makeParams()
    {
        ParamList l;
        p::lin (l, "thr", "Threshold", -60, 0, -18, "dB");
        p::lg (l, "ratio", "Ratio", 1, 100, 4, ":1");
        p::lin (l, "knee", "Knee", 0, 24, 3, "dB");
        p::lg (l, "attack", "Attack", 0.1f, 200, 5, "ms");
        p::lg (l, "release", "Release", 5, 2000, 100, "ms");
        p::lin (l, "lookahead", "Lookahead", 0, 20, 0, "ms");
        p::lin (l, "rms", "RMS Size", 0, 100, 0, "ms");
        p::lg (l, "scHpf", "Detector HPF", 20, 1000, 20, "Hz");
        p::lin (l, "mix", "Mix", 0, 100, 100, "%");
        p::lin (l, "output", "Output", -24, 24, 0, "dB");
        p::ch (l, "source", "Detector Input", { "Main", "Sidechain" }, 0);
        p::bl (l, "autoGain", "Auto Makeup", false);
        p::bl (l, "limit", "Limit Output", false);
        AnalogCore::addParams (l);
        p::bl (l, "bypass", "Bypass", false);
        return l;
    }

    enum { thr, ratio, knee, attack, release, lookahead, rms, scHpf, mix, output, source, autoGain, limit, analogModel, analogDrive, bypass };

    CompCore() : PluginCore (makeParams()) {}

    int latency() const override { return la < 0 ? 0 : la; }

    float autoMakeup() const
    {
        // die halbe Reduktion bei 0 dBFS ausgleichen
        return -0.5f * gainComputer (0.0f, v[thr], std::max (1.0f, v[ratio]), v[knee]);
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : dl) d.assign ((size_t) (0.021 * sr) + 2, 0.0f);
        dlPos = 0; env = 0; ms = 0; hpfFreq = -1;
        hpf.reset();
        analog.prepare (sr);
        la = (int) std::round (v[lookahead] * 0.001 * sr);
        outGain.reset (sr, 0.03); outGain.setCurrentAndTargetValue (dsp::db2a (v[output]));
        mixS.reset (sr, 0.03); mixS.setCurrentAndTargetValue (v[mix] * 0.01f);
    }

    void process (float* L, float* R, int n, int nch, const float* scL, const float* scR) override
    {
        if (nch == 0) return;
        la = std::clamp ((int) std::round (v[lookahead] * 0.001 * sr), 0, (int) dl[0].size() - 2);

        const float th = v[thr], rt = std::max (1.0f, v[ratio]), kn = v[knee];
        const float att = dsp::coef (v[attack], sr), rel = dsp::coef (v[release], sr);
        const float rmsMs = v[rms];
        const float rmsC = rmsMs > 0.05f ? dsp::coef (rmsMs, sr) : 0.0f;
        const float hf = v[scHpf];
        if (std::abs (hf - hpfFreq) > 0.01f) { hpfFreq = hf; hpf.highPass (sr, std::min ((double) hf, sr * 0.45), 0.7071); }
        const bool useHpf = hf > 20.5f;
        const bool byp = v[bypass] > 0.5f;
        const float makeup = v[autoGain] > 0.5f ? autoMakeup() : 0.0f;
        const bool lim = v[limit] > 0.5f;
        outGain.setTargetValue (dsp::db2a (v[output]));
        mixS.setTargetValue (v[mix] * 0.01f);
        analog.beginBlock (v[analogModel], v[analogDrive]);

        if (v[source] < 0.5f) scL = scR = nullptr;
        if (nch < 2) R = L;
        const int len = (int) dl[0].size();
        float inMax = 0, outMax = 0, grLow = 0, detMax = 0;

        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            float dL = scL ? scL[i] : xl, dR = scR ? scR[i] : xr;
            if (useHpf) { dL = hpf.process (dL, 0); dR = hpf.process (dR, 1); }
            float lvl = std::max (std::abs (dL), std::abs (dR));
            if (rmsC > 0) { ms = rmsC * ms + (1 - rmsC) * lvl * lvl; lvl = std::sqrt (ms); }
            detMax = std::max (detMax, lvl);
            const float xdb = dsp::a2db (lvl);
            const float target = gainComputer (xdb, th, rt, kn) - xdb;
            env = target < env ? att * env + (1 - att) * target : rel * env + (1 - rel) * target;

            // Lookahead: Audio verzögern
            dl[0][(size_t) dlPos] = xl; dl[1][(size_t) dlPos] = xr;
            int rp = dlPos - la; if (rp < 0) rp += len;
            const float yl = dl[0][(size_t) rp], yr = dl[1][(size_t) rp];
            if (++dlPos >= len) dlPos = 0;

            float g = 1.0f;
            const float og = outGain.getNextValue(), mx = mixS.getNextValue();
            if (! byp)
            {
                g = (mx * dsp::db2a (env + makeup) + (1 - mx)) * og;
                grLow = std::min (grLow, env);
            }
            float ol = yl * g, orr = yr * g;
            if (! byp && analog.active())
            {
                // Färbung nach Kompression und Mix, vor dem Limit
                analog.advance();
                ol = analog.tick (ol, 0); orr = analog.tick (orr, 1);
            }
            if (lim && ! byp) { ol = std::clamp (ol, -1.0f, 1.0f); orr = std::clamp (orr, -1.0f, 1.0f); }
            L[i] = ol;
            if (nch > 1) R[i] = orr;
            inMax = std::max (inMax, std::max (std::abs (xl), std::abs (xr)));
            outMax = std::max (outMax, std::max (std::abs (ol), std::abs (orr)));
        }
        inPk = inMax; outPk = outMax; grMin = grLow; detPk = detMax;
    }

private:
    double sr = 48000;
    std::vector<float> dl[2];
    int dlPos = 0, la = -1;
    float env = 0, ms = 0, hpfFreq = -1;
    dsp::Biquad hpf;
    Smoothed outGain, mixS;
    AnalogCore analog;
};
} // namespace onno::core
