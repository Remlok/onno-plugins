#pragma once
// Onno Gate, Klang-Kern: Noise-Gate im Stil von ReaGate (Hysterese, Hold, Range, Lookahead, Sidechain mit Filtern)
#include "Core/Core.h"
#include "Core/AnalogCore.h"

namespace onno::core
{
class GateCore : public PluginCore
{
public:
    static ParamList makeParams()
    {
        ParamList l;
        p::lin (l, "thr", "Threshold", -80, 0, -40, "dB");
        p::lin (l, "hyst", "Hysteresis", 0, 12, 4, "dB");
        p::lg (l, "attack", "Attack", 0.01f, 50, 0.5f, "ms");
        p::lin (l, "hold", "Hold", 0, 500, 50, "ms");
        p::lg (l, "release", "Release", 5, 2000, 150, "ms");
        p::lin (l, "range", "Range", -80, 0, -80, "dB");
        p::lin (l, "lookahead", "Lookahead", 0, 10, 0, "ms");
        p::lg (l, "scHpf", "Detector HPF", 20, 2000, 20, "Hz");
        p::lg (l, "scLpf", "Detector LPF", 1000, 20000, 20000, "Hz");
        p::ch (l, "source", "Detector Input", { "Main", "Sidechain" }, 0);
        p::bl (l, "listen", "Detector Listen", false);
        AnalogCore::addParams (l);
        p::bl (l, "bypass", "Bypass", false);
        return l;
    }

    enum { thr, hyst, attack, hold, release, range, lookahead, scHpf, scLpf, source, listen, analogModel, analogDrive, bypass };

    // disp[0]: wie weit das Gate offen war (0..1, Spitze seit dem letzten Abholen)
    GateCore() : PluginCore (makeParams()) { disp.assign (1, 0.0f); }

    int latency() const override { return la < 0 ? 0 : la; }
    void resetDisplay() override { disp[0] = 0; }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& d : dl) d.assign ((size_t) (0.011 * sr) + 2, 0.0f);
        dlPos = 0; det = 0; env = 0; open = false; holdCnt = 0; hpfF = lpfF = -1;
        hpf.reset(); lpf.reset();
        analog.prepare (sr);
        la = (int) std::round (v[lookahead] * 0.001 * sr);
    }

    void process (float* L, float* R, int n, int nch, const float* scL, const float* scR) override
    {
        if (nch == 0) return;
        la = std::clamp ((int) std::round (v[lookahead] * 0.001 * sr), 0, (int) dl[0].size() - 2);

        const float th = v[thr], closeThr = th - v[hyst];
        const float attC = dsp::coef (v[attack], sr), relC = dsp::coef (v[release], sr), detC = dsp::coef (8.0f, sr);
        const int holdN = (int) (v[hold] * 0.001 * sr);
        const float rng = v[range] <= -79.9f ? 0.0f : dsp::db2a (v[range]);
        const float hf = v[scHpf], lf = v[scLpf];
        if (hf != hpfF) { hpfF = hf; hpf.highPass (sr, std::min ((double) hf, sr * 0.45), 0.7071); }
        if (lf != lpfF) { lpfF = lf; lpf.lowPass (sr, std::min ((double) lf, sr * 0.45), 0.7071); }
        const bool useH = hf > 20.5f, useL = lf < 19900.0f && lf < sr * 0.45;
        const bool byp = v[bypass] > 0.5f, lis = v[listen] > 0.5f;

        analog.beginBlock (v[analogModel], v[analogDrive]);
        if (v[source] < 0.5f) scL = scR = nullptr;
        if (nch < 2) R = L;
        const int len = (int) dl[0].size();
        float inMax = 0, outMax = 0, detMax = 0, openMax = 0, gMin = 1;

        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            float dL = scL ? scL[i] : xl, dR = scR ? scR[i] : xr;
            if (useH) { dL = hpf.process (dL, 0); dR = hpf.process (dR, 1); }
            if (useL) { dL = lpf.process (dL, 0); dR = lpf.process (dR, 1); }
            const float lvl = std::max (std::abs (dL), std::abs (dR));
            det = lvl > det ? lvl : det * detC;
            const float ddb = dsp::a2db (det);
            if (ddb > th) { open = true; holdCnt = holdN; }
            else if (ddb < closeThr) { if (holdCnt > 0) --holdCnt; else open = false; }
            env = open ? 1.0f - attC * (1.0f - env) : env * relC;
            detMax = std::max (detMax, det);
            openMax = std::max (openMax, env);

            dl[0][(size_t) dlPos] = xl; dl[1][(size_t) dlPos] = xr;
            int rp = dlPos - la; if (rp < 0) rp += len;
            float yl = dl[0][(size_t) rp], yr = dl[1][(size_t) rp];
            if (++dlPos >= len) dlPos = 0;

            if (! byp)
            {
                if (lis) { yl = dL; yr = dR; }
                else
                {
                    const float g = rng + (1.0f - rng) * env;
                    if (analog.active()) { analog.advance(); yl = analog.tick (yl, 0); yr = analog.tick (yr, 1); }
                    yl *= g; yr *= g;
                    gMin = std::min (gMin, g);
                }
            }
            L[i] = yl;
            if (nch > 1) R[i] = yr;
            inMax = std::max (inMax, std::max (std::abs (xl), std::abs (xr)));
            outMax = std::max (outMax, std::max (std::abs (yl), std::abs (yr)));
        }
        inPk = inMax; outPk = outMax; grMin = dsp::a2db (gMin); detPk = detMax;
        disp[0] = std::max (disp[0], openMax);
    }

private:
    double sr = 48000;
    std::vector<float> dl[2];
    int dlPos = 0, la = -1, holdCnt = 0;
    float det = 0, env = 0, hpfF = -1, lpfF = -1;
    bool open = false;
    dsp::Biquad hpf, lpf;
    AnalogCore analog;
};
} // namespace onno::core
