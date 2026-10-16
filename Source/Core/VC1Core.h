#pragma once
// Onno VC-1, Klang-Kern: VCA-Kompressor (Feed-forward, Stereo-gekoppelt, Soft-Knee, Parallel-Blend)
#include "Core/Core.h"
#include "Core/AnalogCore.h"

namespace onno::core
{
class VC1Core : public PluginCore
{
public:
    static ParamList makeParams()
    {
        ParamList l;
        p::lin (l, "thr", "Threshold", -60, 0, -18, "dB");
        p::lg (l, "ratio", "Ratio", 1, 20, 4, ":1");
        p::lg (l, "attack", "Attack", 0.1f, 100, 10, "ms");
        p::lg (l, "release", "Release", 10, 1000, 120, "ms");
        p::lin (l, "knee", "Knee", 0, 24, 6, "dB");
        p::lin (l, "output", "Output Gain", -12, 24, 0, "dB");
        p::lin (l, "blend", "Blend", 0, 100, 100, "%");
        p::ch (l, "meter", "Meter", { "Input", "Output", "G.C." }, 2);
        AnalogCore::addParams (l);
        p::bl (l, "bypass", "Bypass", false);
        // nur für die native Oberfläche (Konsole/Modern); der Browser zeigt immer Modern
        p::ch (l, "view", "Ansicht", { "Konsole", "Modern" }, 0, false);
        return l;
    }

    enum { thr, ratio, attack, release, knee, output, blend, meter, analogModel, analogDrive, bypass, view };

    // disp: leer. Die Anzeige kommt mit inPk/outPk (linear) und grMin (dB, <= 0) aus.
    VC1Core() : PluginCore (makeParams()) {}

    // Kennlinie in dB (für DSP und Anzeige); ratio höchstens 20, also nie "unendlich"
    static float gainComputer (float x, float th, float ra, float kn)
    {
        const float d = x - th;
        if (2 * d < -kn) return x;
        if (kn > 0 && 2 * std::abs (d) <= kn)
            return x + (1 / ra - 1) * (d + kn * 0.5f) * (d + kn * 0.5f) / (2 * kn);
        return th + d / ra;
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        env = 0.0f;
        analog.prepare (sr);
    }

    void process (float* L, float* R, int n, int nch, const float*, const float*) override
    {
        if (nch == 0) return;
        const float th = v[thr];
        const float ra = std::max (1.0f, v[ratio]);
        const float kn = std::max (0.0f, v[knee]);
        const float att = (float) std::exp (-1.0 / (std::max (0.01f, v[attack]) * 0.001 * sr));
        const float rel = (float) std::exp (-1.0 / (std::max (1.0f, v[release]) * 0.001 * sr));
        const float makeup = dsp::db2a (v[output]);
        const float mix = v[blend] * 0.01f;
        const bool byp = v[bypass] > 0.5f;
        if (nch < 2) R = L;

        analog.beginBlock (v[analogModel], v[analogDrive]);
        float inMax = 0, outMax = 0, grPk = 0;

        for (int i = 0; i < n; ++i)
        {
            const float lvl = std::max (std::abs (L[i]), std::abs (R[i]));
            const float xdb = 8.685889638f * std::log (std::max (lvl, 1.0e-9f));
            const float gr = gainComputer (xdb, th, ra, kn) - xdb;
            env = gr < env ? att * env + (1 - att) * gr : rel * env + (1 - rel) * gr;
            float wet = 1.0f;
            if (! byp)
            {
                const float g = std::exp (env * 0.11512925f) * makeup;
                wet = mix * g + 1 - mix;
                grPk = std::min (grPk, env);
            }
            float ol = L[i] * wet, orr = R[i] * wet;
            if (! byp && analog.active())
            {
                // Färbung nach Kompression und Blend
                analog.advance();
                ol = analog.tick (ol, 0); orr = analog.tick (orr, 1);
            }
            L[i] = ol;
            if (nch > 1) R[i] = orr;
            inMax = std::max (inMax, lvl);
            outMax = std::max (outMax, std::max (std::abs (L[i]), std::abs (R[i])));
        }
        inPk = inMax; outPk = outMax; grMin = grPk; detPk = inMax;
    }

    // Startpunkte für Metal-Produktionen (Analog: 0 Clean, 1 Brit, 2 Console, 3 Tube, 4 FET)
    std::vector<Preset> factoryPresets() const override
    {
        return {
            { "Drum Bus Glue", {}, { { "thr", -20 }, { "ratio", 4 }, { "attack", 10 }, { "release", 100 }, { "knee", 6 }, { "blend", 60 }, { "output", 2 }, { "analog", 1 }, { "drive", 30 } } },
            { "Kick Punch", {}, { { "thr", -18 }, { "ratio", 4 }, { "attack", 30 }, { "release", 80 }, { "knee", 3 }, { "output", 2 } } },
            { "Snare Smack", {}, { { "thr", -22 }, { "ratio", 6 }, { "attack", 3 }, { "release", 60 }, { "blend", 70 }, { "output", 2 }, { "analog", 4 }, { "drive", 25 } } },
            { "Room Crush", {}, { { "thr", -40 }, { "ratio", 20 }, { "attack", 0.3f }, { "release", 50 }, { "knee", 0 }, { "blend", 30 }, { "output", 6 }, { "analog", 4 }, { "drive", 50 } } },
            { "Bass Steady", {}, { { "thr", -24 }, { "ratio", 6 }, { "attack", 5 }, { "release", 120 }, { "output", 4 }, { "analog", 3 }, { "drive", 30 } } },
            { "Vocal Ride", {}, { { "thr", -20 }, { "ratio", 3 }, { "attack", 5 }, { "release", 150 }, { "knee", 12 }, { "output", 3 } } },
            { "Mix Bus 2:1", {}, { { "thr", -12 }, { "ratio", 2 }, { "attack", 30 }, { "release", 300 }, { "knee", 12 }, { "output", 1 }, { "analog", 2 }, { "drive", 20 } } },
        };
    }

private:
    double sr = 48000.0;
    float env = 0.0f;
    AnalogCore analog;
};
} // namespace onno::core
