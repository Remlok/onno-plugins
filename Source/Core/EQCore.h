#pragma once
// Onno EQ, Klang-Kern: 8 Bänder (Low/High Cut 12/24/48 dB, Shelf, Bell, Notch, Band Pass), Ausgang, Analog-Färbung
#include "Core/Core.h"
#include "Core/AnalogCore.h"
#include <array>

namespace onno::core
{
class EQCore : public PluginCore
{
public:
    static constexpr int numBands = 8;
    enum Type { lowCut = 0, lowShelf, bell, notch, highShelf, highCut, bandPass };

    static const std::vector<std::string>& typeNames()
    {
        static const std::vector<std::string> n { "Low Cut", "Low Shelf", "Bell", "Notch", "High Shelf", "High Cut", "Band Pass" };
        return n;
    }

    struct Band
    {
        bool on = false; int type = bell; float freq = 1000, gain = 0, q = 1; int slope = 0;
        bool operator!= (const Band& o) const { return on != o.on || type != o.type || freq != o.freq || gain != o.gain || q != o.q || slope != o.slope; }
    };

    static std::string id (int b, const char* p) { return "b" + std::to_string (b + 1) + p; }
    static bool hasGain (int type) { return type == lowShelf || type == bell || type == highShelf; }
    static bool isCut (int type) { return type == lowCut || type == highCut; }

    static ParamList makeParams()
    {
        struct Def { bool on; int type; float freq, q; };
        static const Def defaults[numBands] = {
            { false, lowCut, 30, 0.707f }, { true, lowShelf, 100, 0.707f },
            { true, bell, 400, 1.0f },     { true, bell, 1500, 1.0f },
            { true, bell, 4000, 1.0f },    { true, highShelf, 8000, 0.707f },
            { false, highCut, 18000, 0.707f }, { false, bell, 1000, 1.0f },
        };
        ParamList l;
        for (int b = 0; b < numBands; ++b)
        {
            const std::string n = "Band " + std::to_string (b + 1) + " ";
            p::bl (l, id (b, "on"), n + "On", defaults[b].on);
            p::ch (l, id (b, "type"), n + "Type", typeNames(), defaults[b].type);
            p::lg (l, id (b, "freq"), n + "Freq", 20, 20000, defaults[b].freq, "Hz");
            p::lin (l, id (b, "gain"), n + "Gain", -24, 24, 0, "dB");
            p::lg (l, id (b, "q"), n + "Q", 0.1f, 18, defaults[b].q);
            p::ch (l, id (b, "slope"), n + "Slope", { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1);
        }
        p::lin (l, "output", "Output", -24, 24, 0, "dB");
        AnalogCore::addParams (l);
        p::ch (l, "analyzer", "Analyzer", { "Off", "Pre", "Post" }, 2, false);
        p::bl (l, "bypass", "Bypass", false);
        return l;
    }

    EQCore() : PluginCore (makeParams())
    {
        iOutput = param ("output"); iAnalog = param ("analog"); iDrive = param ("drive"); iBypass = param ("bypass");
    }

    // EQ-Typen: 0 Low Cut, 1 Low Shelf, 2 Bell, 3 Notch, 4 High Shelf, 5 High Cut, 6 Band Pass; Analog 0 Clean .. 4 FET
    std::vector<Preset> factoryPresets() const override
    {
        return {
            { "Metal-Gitarre", "Metal", { { "b1on", 1 }, { "b1freq", 90 }, { "b3freq", 420 }, { "b3gain", -3 }, { "b3q", 1.2f }, { "b4freq", 2500 }, { "b4gain", 1.5f }, { "b7on", 1 }, { "b7freq", 9000 } } },
            { "Kick Klick", "Metal", { { "b1on", 1 }, { "b1freq", 30 }, { "b2freq", 60 }, { "b2gain", 3 }, { "b3freq", 350 }, { "b3gain", -4 }, { "b3q", 1.5f }, { "b5freq", 4000 }, { "b5gain", 4 }, { "b5q", 1.2f } } },
            { "Snare K\xc3\xb6rper", "Metal", { { "b1on", 1 }, { "b1freq", 80 }, { "b3freq", 200 }, { "b3gain", 2 }, { "b4freq", 900 }, { "b4gain", -2.5f }, { "b6gain", 3 } } },
            { "Bass DI", "Metal", { { "b1on", 1 }, { "b1freq", 35 }, { "b3freq", 250 }, { "b3gain", -2 }, { "b4freq", 800 }, { "b4gain", 2 }, { "b7on", 1 }, { "b7freq", 5000 } } },
            { "Gesang Luft", "Gesang", { { "b1on", 1 }, { "b1freq", 100 }, { "b3freq", 300 }, { "b3gain", -2 }, { "b5freq", 3000 }, { "b5gain", 1.5f }, { "b6freq", 10000 }, { "b6gain", 3 } } },
            { "Shouts", "Gesang", { { "b1on", 1 }, { "b1freq", 120 }, { "b3freq", 500 }, { "b3gain", -3 }, { "b4freq", 2000 }, { "b4gain", 2 }, { "b5freq", 5500 }, { "b5gain", -2 }, { "b5q", 3 }, { "analog", 4 }, { "drive", 40 } } },
        };
    }

    Band band (int b) const
    {
        const float* q = v.data() + b * 6;
        Band r;
        r.on = q[0] > 0.5f; r.type = (int) q[1]; r.freq = q[2]; r.gain = q[3]; r.q = q[4]; r.slope = (int) q[5];
        return r;
    }

    // Filterstufen eines Bandes entwerfen (Rückgabe: Anzahl Stufen)
    static int design (const Band& bd, double sr, dsp::Biquad* st)
    {
        const double f = std::min ((double) bd.freq, sr * 0.49);
        switch (bd.type)
        {
            // Glocke, Shelves und High Cut analog-genau (keine Stauchung zur Nyquist-Frequenz hin)
            case lowShelf:  st[0].matched (dsp::Biquad::Kind::lowShelf, sr, f, std::clamp (bd.q, 0.3f, 2.0f), bd.gain); return 1;
            case highShelf: st[0].matched (dsp::Biquad::Kind::highShelf, sr, f, std::clamp (bd.q, 0.3f, 2.0f), bd.gain); return 1;
            case notch:     st[0].notch (sr, f, bd.q); return 1;
            case bandPass:  st[0].bandPass (sr, f, bd.q); return 1;
            case lowCut:
            case highCut:
            {
                // Butterworth-Stufen; Q hebt die Resonanz der steilsten Stufe an
                static const double q12[] = { 0.7071 }, q24[] = { 0.5412, 1.3066 }, q48[] = { 0.5098, 0.6013, 0.9000, 2.5629 };
                const double* qs = bd.slope == 0 ? q12 : (bd.slope == 1 ? q24 : q48);
                const int n = bd.slope == 0 ? 1 : (bd.slope == 1 ? 2 : 4);
                for (int i = 0; i < n; ++i)
                {
                    const double q = i == n - 1 ? qs[i] * bd.q / 0.7071 : qs[i];
                    if (bd.type == lowCut) st[i].highPass (sr, f, q); else st[i].matched (dsp::Biquad::Kind::lowPass, sr, f, q);
                }
                return n;
            }
            default: st[0].matched (dsp::Biquad::Kind::peak, sr, f, bd.q, bd.gain); return 1;
        }
    }

    // Betrag der ganzen Kurve in dB (für Anzeigen)
    float responseDb (double f) const
    {
        double sum = 0;
        dsp::Biquad st[4];
        for (int b = 0; b < numBands; ++b)
        {
            const Band bd = band (b);
            if (! bd.on) continue;
            const int n = design (bd, sr, st);
            for (int i = 0; i < n; ++i) sum += st[i].magDb (sr, f);
        }
        return (float) sum + v[(size_t) iOutput];
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        analog.prepare (sr);
        for (int b = 0; b < numBands; ++b)
        {
            cur[(size_t) b] = band (b);
            stages[(size_t) b] = design (cur[(size_t) b], sr, filt[(size_t) b].data());
            for (auto& f : filt[(size_t) b]) f.reset();
        }
        outGain.reset (sr, 0.03);
        outGain.setCurrentAndTargetValue (dsp::db2a (v[(size_t) iOutput]));
    }

    void process (float* L, float* R, int n, int nch, const float*, const float*) override
    {
        float* ch[2] = { L, R };
        if (v[(size_t) iBypass] < 0.5f)
        {
            for (int b = 0; b < numBands; ++b)
            {
                const Band nb = band (b);
                auto& c = cur[(size_t) b];
                if (nb != c)
                {
                    const bool structural = nb.type != c.type || nb.slope != c.slope || nb.on != c.on;
                    c = nb;
                    stages[(size_t) b] = design (c, sr, filt[(size_t) b].data());
                    if (structural) for (auto& f : filt[(size_t) b]) f.reset();
                }
                if (! c.on) continue;
                for (int s = 0; s < stages[(size_t) b]; ++s)
                {
                    auto& f = filt[(size_t) b][(size_t) s];
                    for (int k = 0; k < nch; ++k)
                    {
                        auto* d = ch[k];
                        for (int i = 0; i < n; ++i) d[i] = f.process (d[i], k);
                    }
                }
            }
            analog.process (L, nch > 1 ? R : L, n, nch, v[(size_t) iAnalog], v[(size_t) iDrive]);
            outGain.setTargetValue (dsp::db2a (v[(size_t) iOutput]));
            for (int i = 0; i < n; ++i)
            {
                const float g = outGain.getNextValue();
                for (int k = 0; k < nch; ++k) ch[k][i] *= g;
            }
        }
        float pk = 0;
        for (int k = 0; k < nch; ++k)
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (ch[k][i]));
        outPk = pk;
    }

private:
    double sr = 48000;
    int iOutput = 0, iAnalog = 0, iDrive = 0, iBypass = 0;
    std::array<std::array<dsp::Biquad, 4>, numBands> filt;
    std::array<int, numBands> stages {};
    std::array<Band, numBands> cur;
    Smoothed outGain;
    AnalogCore analog;
};
} // namespace onno::core
