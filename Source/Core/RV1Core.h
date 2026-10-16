#pragma once
// Onno RV-1, Klang-Kern: algorithmischer Hall (8-Kanal-Feedback-Delay-Netz) mit 32 Programmen
// Kette (Hallanteil): Pre-Delay → Early Reflections + Diffusion → FDN mit Modulation und Dämpfung
//                     → Gate (Gated-Programme) → Ducking → Low/High Cut → Breite → Mix
#include "Core/Core.h"
#include <array>

namespace onno::core
{
class RV1Core : public PluginCore
{
public:
    // --- Algorithmen und Programme ---
    enum Algo { hall, plate, room, chamber, ambience, chorus, gated, numAlgos };
    static constexpr const char* algoNames[numAlgos] = { "HALL", "PLATE", "ROOM", "CHAMBER", "AMBIENCE", "CHORUS", "GATED" };
    static constexpr int numCategories = 8;
    static constexpr const char* categoryNames[numCategories] = { "HALL", "PLATE", "ROOM", "CHAMBER", "AMBIENCE", "CHORUS", "GATED", "BIG" };

    // Nachhall nach dem Ende des Signals (für den Host): höchstens Pre-Delay + 20 s Decay
    static constexpr double maxTailSeconds = 20.0;

    struct Program
    {
        const char* name; int category; int algo;
        float predelay, decay, size, depth, movement, character, width, duck, lowCut, highCut;
    };

    static const std::vector<Program>& programs()
    {
        //  Name, Kategorie, Algorithmus, Pre-Delay, Decay, Size, Depth, Movement, Character, Width, Duck, Low Cut, High Cut
        static const std::vector<Program> list = {
            { "Concert Hall", 0, hall, 30, 2.4f, 60, 55, 30, 50, 100, 0, 80, 12000 },
            { "Big Hall", 0, hall, 45, 3.8f, 85, 65, 35, 45, 120, 0, 100, 10000 },
            { "Vocal Hall", 0, hall, 70, 2.2f, 55, 45, 25, 58, 100, 45, 150, 11000 },
            { "Dark Hall", 0, hall, 25, 3.0f, 70, 70, 30, 25, 110, 0, 100, 6000 },
            { "Bright Plate", 1, plate, 10, 1.8f, 50, 30, 20, 75, 100, 0, 150, 16000 },
            { "Vocal Plate", 1, plate, 60, 1.6f, 45, 35, 20, 62, 100, 50, 180, 12000 },
            { "Snare Plate", 1, plate, 5, 1.2f, 40, 25, 15, 68, 110, 0, 200, 14000 },
            { "Gold Plate", 1, plate, 20, 2.6f, 60, 40, 25, 55, 100, 0, 120, 10000 },
            { "Small Room", 2, room, 0, 0.6f, 30, 30, 10, 50, 90, 0, 60, 14000 },
            { "Drum Room", 2, room, 5, 0.9f, 50, 40, 15, 55, 110, 0, 80, 12000 },
            { "Live Room", 2, room, 10, 1.2f, 65, 50, 20, 52, 100, 0, 70, 13000 },
            { "Studio A", 2, room, 8, 0.8f, 45, 35, 12, 48, 100, 0, 90, 11000 },
            { "Echo Chamber", 3, chamber, 20, 1.8f, 55, 50, 20, 50, 100, 0, 100, 10000 },
            { "Stone Chamber", 3, chamber, 15, 2.4f, 65, 55, 15, 60, 100, 0, 120, 13000 },
            { "Tight Chamber", 3, chamber, 10, 1.1f, 40, 40, 15, 52, 90, 0, 120, 12000 },
            { "Wide Chamber", 3, chamber, 25, 2.0f, 60, 50, 25, 50, 140, 0, 100, 11000 },
            { "Air", 4, ambience, 0, 0.5f, 30, 20, 20, 70, 120, 0, 300, 18000 },
            { "Close Mic", 4, ambience, 0, 0.35f, 20, 15, 10, 50, 80, 0, 150, 12000 },
            { "Short Space", 4, ambience, 5, 0.7f, 40, 30, 15, 50, 100, 0, 120, 12000 },
            { "Glue", 4, ambience, 0, 0.9f, 45, 40, 20, 45, 100, 0, 100, 9000 },
            { "Chorus Space", 5, chorus, 20, 2.0f, 60, 50, 60, 55, 130, 0, 120, 11000 },
            { "Lush Verb", 5, chorus, 35, 3.2f, 75, 60, 70, 50, 140, 0, 150, 10000 },
            { "Wobble Hall", 5, chorus, 40, 4.0f, 80, 65, 90, 45, 130, 0, 100, 9000 },
            { "Ensemble", 5, chorus, 15, 1.4f, 45, 40, 55, 60, 150, 0, 200, 13000 },
            { "80s Snare Gate", 6, gated, 0, 1.6f, 60, 30, 20, 65, 120, 0, 150, 14000 },
            { "Tom Gate", 6, gated, 0, 2.0f, 70, 35, 15, 50, 120, 0, 80, 10000 },
            { "Short Gate", 6, gated, 0, 1.0f, 50, 25, 15, 60, 110, 0, 150, 13000 },
            { "Kick Gate", 6, gated, 0, 1.2f, 55, 30, 10, 40, 100, 0, 40, 8000 },
            { "Arena 84", 7, hall, 30, 4.5f, 95, 70, 40, 62, 140, 0, 120, 13000 },
            { "Cathedral", 7, hall, 60, 7.0f, 100, 80, 35, 45, 130, 0, 100, 8000 },
            { "Doom Hall", 7, hall, 80, 9.0f, 100, 85, 45, 30, 140, 30, 60, 5000 },
            { "Lead Wash", 7, chorus, 90, 5.0f, 90, 70, 70, 55, 150, 60, 250, 11000 },
        };
        return list;
    }

    static int firstOfCategory (int cat)
    {
        const auto& l = programs();
        for (int i = 0; i < (int) l.size(); ++i) if (l[(size_t) i].category == cat) return i;
        return 0;
    }

    // Index des Programms mit diesem Namen oder -1
    static int programIndex (const std::string& name)
    {
        const auto& l = programs();
        for (int i = 0; i < (int) l.size(); ++i) if (name == l[(size_t) i].name) return i;
        return -1;
    }

    // Reihenfolge, IDs, Bereiche und Standardwerte wie im alten layout() des VST3.
    // Ausnahme: Pre-Delay ist im VST3 schief (Mitte bei 60 ms, siehe RV1Processor::createLayout); hier linear
    // eingetragen, weil ParamDef keinen Skew kennt. Die Werte (ms) sind dieselben.
    static ParamList makeParams()
    {
        ParamList l;
        p::lin (l, "predelay", "Pre-Delay", 0, 250, 30, "ms");
        p::lg (l, "decay", "Decay", 0.2f, 20, 2.4f, "s");
        p::lin (l, "size", "Size", 0, 100, 60, "%");
        p::lin (l, "depth", "Depth", 0, 100, 55, "%");
        p::lin (l, "movement", "Movement", 0, 100, 30, "%");
        p::lin (l, "character", "Character", 0, 100, 50, "");
        p::lin (l, "width", "Width", 0, 150, 100, "%");
        p::lin (l, "duck", "Duck", 0, 100, 0, "%");
        p::lg (l, "lowCut", "Low Cut", 20, 1000, 80, "Hz");
        p::lg (l, "highCut", "High Cut", 1000, 20000, 12000, "Hz");
        p::lin (l, "mix", "Mix", 0, 100, 30, "%");
        p::bl (l, "send", "100% Wet", false);
        p::bl (l, "freeze", "Freeze", false);
        p::bl (l, "bypass", "Bypass", false);
        p::ch (l, "algo", "Algorithmus", std::vector<std::string> (algoNames, algoNames + numAlgos), 0);
        std::vector<std::string> progNames;
        for (auto& pr : programs()) progNames.push_back (pr.name);
        p::ch (l, "program", "Programm", progNames, 0, false);     // im VST3 ein AudioParameterInt 0..31
        p::ch (l, "view", "Ansicht", { "Konsole", "Modern" }, 0, false);
        return l;
    }

    enum { predelay, decay, size, depth, movement, character, width, duck, lowCut, highCut, mix, send, freeze, bypass, algo, program, view };

    // disp[0]: Spitze des Hallanteils (linear, höchster Wert seit dem letzten Abholen)
    // disp[1]: Ducking in dB (<= 0, tiefster Wert seit dem letzten Abholen; 0 = kein Ducking)
    // Außerdem: inPk = Eingang, outPk = Ausgang, grMin = Ducking (dB, dieser Block), detPk = Hallanteil (dieser Block)
    RV1Core() : PluginCore (makeParams()) { disp.assign (2, 0.0f); }

    void resetDisplay() override { disp[0] = 0; disp[1] = 0; }

    // Programme sind die Werkspresets 1..32; Mix, 100 % Wet und Freeze bleiben beim Laden stehen (keepOnPreset im VST3)
    std::vector<Preset> factoryPresets() const override
    {
        std::vector<Preset> r;
        const auto& progs = programs();
        for (int i = 0; i < (int) progs.size(); ++i)
        {
            const auto& pr = progs[(size_t) i];
            r.push_back ({ pr.name, categoryNames[pr.category],
                           { { "program", (float) i }, { "algo", (float) pr.algo }, { "predelay", pr.predelay }, { "decay", pr.decay },
                             { "size", pr.size }, { "depth", pr.depth }, { "movement", pr.movement }, { "character", pr.character },
                             { "width", pr.width }, { "duck", pr.duck }, { "lowCut", pr.lowCut }, { "highCut", pr.highCut } } });
        }
        return r;
    }

    // ungefähre Länge des Nachhalls mit den aktuellen Werten (s); Freeze = maxTailSeconds
    bool keepOnPreset (const std::string& id) const override { return id == "mix" || id == "send" || id == "freeze"; }

    double tailSeconds() const override
    {
        if (v[freeze] > 0.5f) return maxTailSeconds;
        const double t = (double) v[predelay] * 0.001 + ((int) v[algo] == gated ? 1.3 : (double) v[decay] * 1.12);
        return std::min (maxTailSeconds, t);
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& l : pre) l.init ((int) (0.45 * sr) + 4);
        for (int c = 0; c < 2; ++c)
            for (auto& a : diff[c]) a.line.init ((int) (0.03 * sr) + 4);
        for (auto& l : tank) l.init ((int) (0.13 * sr) + 8);
        damp.fill (0); lfoPhase.fill (0);
        for (int i = 0; i < N; ++i) lfoPhase[(size_t) i] = (float) i * 0.785f;
        lowCutF.reset(); highCutF.reset();
        duckEnv = gateEnv = -120; gateGain = 0; gateTimer = 0;
    }

    void process (float* L, float* R, int n, int nch, const float* = nullptr, const float* = nullptr) override
    {
        if (nch == 0 || n == 0) return;
        if (nch < 2) R = L;

        float inMax = 0;
        for (int i = 0; i < n; ++i) inMax = std::max (inMax, std::max (std::abs (L[i]), std::abs (R[i])));
        inPk = inMax;
        if (v[bypass] > 0.5f) { outPk = inMax; grMin = 0; detPk = 0; return; }

        using dsp::twoPi;
        const auto& ad = algos()[std::clamp ((int) v[algo], 0, numAlgos - 1)];
        const bool frz = v[freeze] > 0.5f;
        const float sizeMul = 0.5f + v[size] * 0.01f;
        const float dep = v[depth] * 0.01f, move = v[movement] * 0.01f;
        const float dec = ad.gate ? 3.0f : v[decay];
        const float gateTime = std::clamp (v[decay] * 0.25f, 0.06f, 1.2f);
        const float wid = v[width] * 0.01f;
        const float dk = v[duck] * 0.01f;
        const bool snd = v[send] > 0.5f;
        const float mx = snd ? 1.0f : v[mix] * 0.01f;
        const float dryG = snd ? 0.0f : 1.0f - mx;

        // Dämpfung: Character 0 = dunkel, 100 = hell; mehr Tiefe klingt entfernter und dunkler
        const float fc = std::clamp (1200.0f * std::pow (15.0f, (v[character] + ad.bright) / 100.0f) * (1.0f - 0.35f * dep), 1200.0f, 18000.0f);
        const float dampA = frz ? 0.0f : (float) std::exp (-twoPi * fc / sr);

        float len[N], fb[N], modS[N], inc[N];
        for (int i = 0; i < N; ++i)
        {
            len[i] = lineMs[i] * 0.001f * ad.scale * sizeMul * (float) sr;
            // 1,12: gleicht die Verluste durch Dämpfung und Interpolation aus, gemessen per Impulsantwort
            fb[i] = frz ? 1.0f : (float) std::pow (10.0, -3.0 * len[i] / (dec * 1.12 * sr));
            modS[i] = move * 0.0009f * (float) sr * ad.modDepth;
            inc[i] = (float) (twoPi * lfoHz[i] * ad.modRate * (0.4f + move) / sr);
        }
        float diffLen[2][4];
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
                diffLen[c][k] = diffMs[c][k] * 0.001f * std::sqrt (ad.scale * sizeMul) * (float) sr;
        const float diffG = ad.diffusion;
        const float erGain = frz ? 0.0f : ad.er * (1.0f - 0.85f * dep) * 0.5f;
        const float lateGain = 0.6f + 0.4f * dep;
        const float tankIn = frz ? 0.0f : 0.5f;
        const float pdS = std::max (1.0f, v[predelay] * 0.001f * (float) sr);
        float erD[2][8];
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 8; ++k)
                erD[c][k] = pdS + erTaps[c][k].ms * 0.001f * ad.erScale * sizeMul * (float) sr;

        const float lc = v[lowCut], hc = v[highCut];
        const bool lcOn = lc > 20.5f, hcOn = hc < 19500.0f;
        lowCutF.highPass (sr, lc, 0.7071);
        highCutF.lowPass (sr, std::min ((double) hc, sr * 0.45), 0.7071);

        const float dAtt = dsp::coef (5.0f, sr), dRel = dsp::coef (300.0f, sr);
        const float gDet = dsp::coef (10.0f, sr), gOpen = dsp::coef (1.0f, sr), gClose = dsp::coef (8.0f, sr);
        const float invSqrtN = 1.0f / std::sqrt ((float) N);
        float wetPk = 0, duckMin = 0, outMax = 0;

        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            pre[0].write (xl); pre[1].write (xr);

            // frühe Reflexionen
            float er[2] = { 0, 0 };
            if (erGain > 0)
                for (int c = 0; c < 2; ++c)
                    for (int k = 0; k < 8; ++k) er[c] += erTaps[c][k].g * pre[c].read (erD[c][k]);

            // Diffusion des verzögerten Signals
            float d[2] = { pre[0].read (pdS), pre[1].read (pdS) };
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 4; ++k) { diff[c][(size_t) k].len = diffLen[c][k]; d[c] = diff[c][(size_t) k].process (d[c], diffG); }

            // Netz: lesen (moduliert), dämpfen, mischen (Hadamard), zurückschreiben
            float y[N], x[N];
            for (int k = 0; k < N; ++k)
            {
                lfoPhase[(size_t) k] += inc[k];
                if (lfoPhase[(size_t) k] > 6.2831853f) lfoPhase[(size_t) k] -= 6.2831853f;
                y[k] = tank[(size_t) k].read (len[k] + modS[k] * (1.0f + std::sin (lfoPhase[(size_t) k])) + 1.0f);
                damp[(size_t) k] = flush (y[k] * (1 - dampA) + damp[(size_t) k] * dampA);
                x[k] = damp[(size_t) k] * fb[k];
            }
            for (int h = 1; h < N; h <<= 1)
                for (int a = 0; a < N; a += 2 * h)
                    for (int b = a; b < a + h; ++b) { const float u = x[b], w = x[b + h]; x[b] = u + w; x[b + h] = u - w; }
            for (int k = 0; k < N; ++k)
                tank[(size_t) k].write (flush (x[k] * invSqrtN + (k % 2 ? d[1] : d[0]) * injSign[k] * tankIn));

            float wl = lateGain * 0.5f * (y[0] - y[2] + y[4] - y[6]) + erGain * er[0];
            float wr = lateGain * 0.5f * (y[1] - y[3] + y[5] - y[7]) + erGain * er[1];

            // Gated: Hall öffnet mit dem Signal und schließt nach der Gate-Zeit hart
            if (ad.gate)
            {
                const float lvl = dsp::a2db (std::max (std::abs (xl), std::abs (xr)));
                gateEnv = lvl > gateEnv ? lvl : gDet * gateEnv + (1 - gDet) * lvl;
                if (gateEnv > -45.0f) gateTimer = gateTime; else gateTimer -= (float) (1.0 / sr);
                const float target = gateTimer > 0 ? 1.0f : 0.0f;
                gateGain = target > gateGain ? gOpen * gateGain + (1 - gOpen) * target : gClose * gateGain + (1 - gClose) * target;
                if (target == 0.0f && gateGain < 1.0e-4f) gateGain = 0.0f;   // ganz zu, kein Restrauschen
                wl *= gateGain; wr *= gateGain;
            }

            // Ducking: Hall wird leiser, solange das Signal spielt, und blüht in den Pausen auf
            if (dk > 0)
            {
                const float lvl = dsp::a2db (std::max (std::abs (xl), std::abs (xr)));
                duckEnv = lvl > duckEnv ? dAtt * duckEnv + (1 - dAtt) * lvl : dRel * duckEnv + (1 - dRel) * lvl;
                const float red = -24.0f * dk * std::clamp ((duckEnv + 45.0f) / 35.0f, 0.0f, 1.0f);
                const float g = dsp::db2a (red);
                wl *= g; wr *= g;
                duckMin = std::min (duckMin, red);
            }

            if (lcOn) { wl = lowCutF.process (wl, 0); wr = lowCutF.process (wr, 1); }
            if (hcOn) { wl = highCutF.process (wl, 0); wr = highCutF.process (wr, 1); }

            // Breite über Mitte/Seite
            const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * wid;
            wl = m + s; wr = m - s;
            wetPk = std::max (wetPk, std::max (std::abs (wl), std::abs (wr)));

            const float ol = xl * dryG + wl * mx, orr = xr * dryG + wr * mx;
            L[i] = ol;
            if (nch > 1) R[i] = orr;
            outMax = std::max (outMax, std::max (std::abs (ol), std::abs (nch > 1 ? orr : ol)));
        }

        outPk = outMax; grMin = duckMin; detPk = wetPk;
        disp[0] = std::max (disp[0], wetPk);
        disp[1] = std::min (disp[1], duckMin);
    }

private:
    // Eigenschaften der Algorithmen
    struct AlgoDef { float scale, diffusion, er, erScale, modRate, modDepth, bright; bool gate; };
    static const AlgoDef* algos()
    {
        static const AlgoDef a[numAlgos] = {
            { 1.00f, 0.62f, 0.60f, 1.00f, 1.0f, 1.0f, 0, false },    // Hall
            { 0.55f, 0.75f, 0.00f, 0.30f, 1.3f, 0.6f, 12, false },   // Plate: dicht, hell, keine frühen Reflexionen
            { 0.35f, 0.55f, 1.00f, 0.45f, 0.8f, 0.4f, 0, false },    // Room
            { 0.60f, 0.65f, 0.80f, 0.70f, 1.0f, 0.8f, 0, false },    // Chamber
            { 0.30f, 0.60f, 1.00f, 0.35f, 1.0f, 0.6f, 0, false },    // Ambience
            { 0.90f, 0.60f, 0.40f, 0.90f, 3.0f, 3.0f, 0, false },    // Chorus: starke Modulation
            { 0.70f, 0.70f, 0.80f, 0.60f, 1.0f, 0.6f, 0, true },     // Gated
        };
        return a;
    }

    // Basislängen des Netzes in ms (teilerfremd verteilt)
    static constexpr float lineMs[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.0f, 66.3f };
    static constexpr float lfoHz[8] = { 0.11f, 0.17f, 0.23f, 0.29f, 0.37f, 0.43f, 0.53f, 0.61f };
    static constexpr float injSign[8] = { 1, 1, -1, 1, 1, -1, -1, -1 };
    static constexpr float diffMs[2][4] = { { 4.77f, 3.59f, 12.73f, 9.30f }, { 4.97f, 3.83f, 12.11f, 9.81f } };
    struct Tap { float ms, g; };
    static constexpr Tap erTaps[2][8] = {
        { { 4.3f, 0.84f }, { 9.7f, -0.71f }, { 14.1f, 0.62f }, { 21.3f, -0.53f }, { 28.9f, 0.45f }, { 37.4f, -0.38f }, { 47.2f, 0.31f }, { 58.6f, -0.24f } },
        { { 5.9f, -0.81f }, { 11.2f, 0.69f }, { 17.8f, -0.60f }, { 24.6f, 0.51f }, { 32.7f, -0.43f }, { 41.9f, 0.35f }, { 52.3f, -0.28f }, { 63.1f, 0.22f } },
    };

    // Ausklingender Hall endet sonst in Denormals (im Browser gibt es kein Flush-to-Zero); weit unter -400 dB, unhörbar
    static float flush (float x) { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

    struct Line
    {
        std::vector<float> buf; int w = 0;
        void init (int n) { buf.assign ((size_t) n, 0.0f); w = 0; }
        void write (float x) { buf[(size_t) w] = x; if (++w == (int) buf.size()) w = 0; }
        float read (float delay) const   // linear interpoliert, delay in Samples (>= 1)
        {
            const int n = (int) buf.size();
            const int di = (int) delay;
            const float fr = delay - (float) di;
            // Ganzzahlige Indizes, damit Rundung nie über das Pufferende hinaus liest
            int i1 = w - di; if (i1 < 0) i1 += n;          // Verzögerung di
            int i0 = i1 - 1; if (i0 < 0) i0 += n;          // Verzögerung di + 1
            return buf[(size_t) i1] + fr * (buf[(size_t) i0] - buf[(size_t) i1]);
        }
    };

    struct Allpass
    {
        Line line; float len = 100;
        float process (float x, float g)
        {
            const float d = line.read (len);
            const float vv = x + g * d;
            line.write (vv);
            return d - g * vv;
        }
    };

    static constexpr int N = 8;
    double sr = 48000.0;
    Line pre[2];
    std::array<Allpass, 4> diff[2];
    std::array<Line, N> tank;
    std::array<float, N> damp {}, lfoPhase {};
    dsp::Biquad lowCutF, highCutF;
    float duckEnv = -120.0f, gateEnv = -120.0f, gateGain = 0.0f, gateTimer = 0.0f;
};
} // namespace onno::core
