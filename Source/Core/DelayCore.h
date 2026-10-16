#pragma once
// Onno Delay, Klang-Kern: Multi-Tap-Delay im Stil von ReaDelay (3 Taps, Sync, Ping-Pong, Filter im Feedback, Ducking)
#include "Core/Core.h"
#include "Core/AnalogCore.h"
#include <array>

namespace onno::core
{
class DelayCore : public PluginCore
{
public:
    static constexpr int numTaps = 3;
    static constexpr float maxMs = 4000.0f;
    static constexpr int numDivs = 13;

    static const std::vector<std::string>& divNames()
    {
        static const std::vector<std::string> n { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1/1" };
        return n;
    }
    // Länge der Notenwerte in Vierteln
    static float divBeats (int d)
    {
        static const float b[numDivs] = { 0.125f, 1.0f / 6, 0.25f, 0.375f, 1.0f / 3, 0.5f, 0.75f, 2.0f / 3, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
        return b[std::clamp (d, 0, numDivs - 1)];
    }

    static std::string id (int t, const char* p) { return "t" + std::to_string (t + 1) + p; }

    // Parameter je Tap (Reihenfolge innerhalb eines Taps), danach dry, wet, duck, analog, drive, bypass
    enum { pOn, pSync, pTime, pDiv, pFb, pLevel, pPan, pHp, pLp, pPp, numP };
    enum { dry = numTaps * numP, wet, duck, analogModel, analogDrive, bypass };

    static ParamList makeParams()
    {
        ParamList l;
        const float times[] = { 250, 375, 500 };
        const int divs[] = { 8, 6, 10 };
        const float pans[] = { 0, -60, 60 };
        for (int t = 0; t < numTaps; ++t)
        {
            const std::string n = "Tap " + std::to_string (t + 1) + " ";
            p::bl (l, id (t, "on"), n + "On", t == 0);
            p::bl (l, id (t, "sync"), n + "Sync", true);
            p::lg (l, id (t, "time"), n + "Time", 1, maxMs, times[t], "ms");
            p::ch (l, id (t, "div"), n + "Division", divNames(), divs[t]);
            p::lin (l, id (t, "fb"), n + "Feedback", 0, 98, t == 0 ? 35.0f : 20.0f, "%");
            p::lin (l, id (t, "level"), n + "Level", -60, 6, t == 0 ? 0.0f : -6.0f, "dB");
            p::lin (l, id (t, "pan"), n + "Pan", -100, 100, pans[t]);
            p::lg (l, id (t, "hp"), n + "Low Cut", 20, 2000, 20, "Hz");
            p::lg (l, id (t, "lp"), n + "High Cut", 500, 20000, 20000, "Hz");
            p::bl (l, id (t, "pp"), n + "Ping-Pong", false);
        }
        p::lin (l, "dry", "Dry", -60, 6, 0, "dB");
        p::lin (l, "wet", "Wet", -60, 6, -6, "dB");
        p::lin (l, "duck", "Ducking", 0, 100, 0, "%");
        AnalogCore::addParams (l);
        p::bl (l, "bypass", "Bypass", false);
        return l;
    }

    // disp[0]: kleinste Ducking-Verstärkung seit dem letzten Abholen (linear, 1 = kein Ducking)
    // disp[1]: Tempo, mit dem der Kern rechnet (BPM)
    DelayCore() : PluginCore (makeParams()) { disp.assign (2, 0.0f); resetDisplay(); }

    void resetDisplay() override { disp[0] = 1.0f; disp[1] = (float) tempo; }

    // Songtempo (vom Host); ohne Host bleibt es bei 120 BPM
    void setTempo (double bpm) override { tempo = bpm; disp[1] = (float) bpm; }
    double getTempo() const { return tempo; }

    // Verzögerung in ms aus Sync, Notenwert, Zeit und Tempo
    static float tapMs (bool sync, float div, float timeMs, double bpm)
    {
        if (sync)
        {
            const int d = std::clamp ((int) div, 0, numDivs - 1);
            return (float) std::min ((double) maxMs, divBeats (d) * 60000.0 / std::max (20.0, bpm));
        }
        return timeMs;
    }
    float tapMs (int t) const
    {
        const float* p = &v[(size_t) (t * numP)];
        return tapMs (p[pSync] > 0.5f, p[pDiv], p[pTime], tempo);
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& t : taps)
        {
            for (auto& b : t.buf) b.assign ((size_t) (maxMs * 0.001 * sr) + 8, 0.0f);
            t.w = 0; t.cur = -1; t.hpF = t.lpF = -1;
            t.hp.reset(); t.lp.reset();
            t.analog.prepare (sr);
        }
        duckEnv = 0; duckS = 1;
        dryS.reset (sr, 0.03); dryS.setCurrentAndTargetValue (v[dry] <= -59.9f ? 0.0f : dsp::db2a (v[dry]));
        wetS.reset (sr, 0.03); wetS.setCurrentAndTargetValue (v[wet] <= -59.9f ? 0.0f : dsp::db2a (v[wet]));
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

        dryS.setTargetValue (v[dry] <= -59.9f ? 0.0f : dsp::db2a (v[dry]));
        wetS.setTargetValue (v[wet] <= -59.9f ? 0.0f : dsp::db2a (v[wet]));
        const float dk = v[duck] * 0.01f;
        const float dAtt = dsp::coef (5.0f, sr), dRel = dsp::coef (250.0f, sr), glide = 1.0f - dsp::coef (60.0f, sr);

        struct TapNow { bool on, pp; float target, fb, gl, gr; };
        std::array<TapNow, numTaps> tn;
        for (int t = 0; t < numTaps; ++t)
        {
            auto& tp = taps[(size_t) t];
            const float* p = &v[(size_t) (t * numP)];
            auto& x = tn[(size_t) t];
            x.on = p[pOn] > 0.5f;
            x.pp = p[pPp] > 0.5f;
            x.target = std::clamp (tapMs (t) * 0.001f * (float) sr, 1.0f, (float) tp.buf[0].size() - 4.0f);
            if (tp.cur < 0) tp.cur = x.target;
            x.fb = p[pFb] * 0.01f;
            const float lv = p[pLevel] <= -59.9f ? 0.0f : dsp::db2a (p[pLevel]);
            const float pan = p[pPan] * 0.01f;
            x.gl = lv * (pan <= 0 ? 1.0f : 1.0f - pan);
            x.gr = lv * (pan >= 0 ? 1.0f : 1.0f + pan);
            const float hf = p[pHp], lf = p[pLp];
            if (hf != tp.hpF) { tp.hpF = hf; tp.hp.highPass (sr, std::min ((double) hf, sr * 0.45), 0.7071); }
            if (lf != tp.lpF) { tp.lpF = lf; if (lf < 19900.0f && lf < sr * 0.45) tp.lp.lowPass (sr, lf, 0.7071); else tp.lp.identity(); }
            tp.analog.beginBlock (v[analogModel], v[analogDrive]);
            if (! x.on && tp.wasOn) { for (auto& b : tp.buf) std::fill (b.begin(), b.end(), 0.0f); tp.hp.reset(); tp.lp.reset(); }
            tp.wasOn = x.on;
        }

        float inMax = 0, outMax = 0, duckMin = 1, envMax = 0;
        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            float wl = 0, wr = 0;
            for (int t = 0; t < numTaps; ++t)
            {
                const auto& x = tn[(size_t) t];
                if (! x.on) continue;
                auto& tp = taps[(size_t) t];
                tp.cur += (x.target - tp.cur) * glide;
                const int len = (int) tp.buf[0].size();
                const int di = (int) tp.cur; const float fr = tp.cur - (float) di;
                int i1 = tp.w - di; if (i1 < 0) i1 += len;
                int i0 = i1 - 1; if (i0 < 0) i0 += len;
                float yl = tp.buf[0][(size_t) i1] + fr * (tp.buf[0][(size_t) i0] - tp.buf[0][(size_t) i1]);
                float yr = tp.buf[1][(size_t) i1] + fr * (tp.buf[1][(size_t) i0] - tp.buf[1][(size_t) i1]);
                if (tp.hpF > 20.5f) { yl = tp.hp.process (yl, 0); yr = tp.hp.process (yr, 1); }
                yl = tp.lp.process (yl, 0); yr = tp.lp.process (yr, 1);
                if (tp.analog.active())
                {
                    // wie ein Bandecho: jede Wiederholung läuft erneut durch die Sättigung
                    tp.analog.advance();
                    yl = tp.analog.tick (yl, 0, false); yr = tp.analog.tick (yr, 1, false);
                }
                if (x.pp)
                {
                    tp.buf[0][(size_t) tp.w] = 0.5f * (xl + xr) + x.fb * yr;
                    tp.buf[1][(size_t) tp.w] = x.fb * yl;
                }
                else
                {
                    tp.buf[0][(size_t) tp.w] = xl + x.fb * yl;
                    tp.buf[1][(size_t) tp.w] = xr + x.fb * yr;
                }
                if (++tp.w >= len) tp.w = 0;
                wl += yl * x.gl; wr += yr * x.gr;
            }
            // Ducking: Echo leiser, solange das Signal spielt
            const float in = std::max (std::abs (xl), std::abs (xr));
            duckEnv = in > duckEnv ? dAtt * duckEnv + (1 - dAtt) * in : dRel * duckEnv + (1 - dRel) * in;
            const float target = 1.0f - dk * std::clamp ((dsp::a2db (duckEnv) + 42.0f) / 30.0f, 0.0f, 1.0f);
            duckS += (target - duckS) * 0.002f;
            duckMin = std::min (duckMin, duckS);
            envMax = std::max (envMax, duckEnv);
            const float dg = dryS.getNextValue(), wg = wetS.getNextValue() * duckS;
            const float ol = xl * dg + wl * wg, orr = xr * dg + wr * wg;
            L[i] = ol;
            if (nch > 1) R[i] = orr;
            else L[i] = 0.5f * (ol + orr);
            inMax = std::max (inMax, in);
            outMax = std::max (outMax, std::max (std::abs (ol), std::abs (orr)));
        }
        inPk = inMax; outPk = outMax; grMin = 0; detPk = envMax;     // Ducking-Verstärkung steht in disp[0]
        disp[0] = std::min (disp[0], duckMin);
        disp[1] = (float) tempo;
    }

private:
    struct Tap
    {
        std::vector<float> buf[2];
        int w = 0;
        float cur = -1, hpF = -1, lpF = -1;
        bool wasOn = false;
        dsp::Biquad hp, lp;
        AnalogCore analog;
    };
    std::array<Tap, numTaps> taps;
    double sr = 48000, tempo = 120.0;
    float duckEnv = 0, duckS = 1;
    Smoothed dryS, wetS;
};
} // namespace onno::core
