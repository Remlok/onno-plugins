#pragma once
// Onno Tame, Klang-Kern: spektraler Resonanz-Zähmer. Findet Frequenzen, die über die eigene spektrale
// Hüllkurve hinausragen, und drückt sie dynamisch, Bin für Bin (STFT mit 75 % Überlappung, sqrt-Hann).
// Metal-Profile setzen Bereich, Schärfe und Empfindlichkeitskurve passend zur Quelle.
// Fenster N = 1024 bei <= 50 kHz (2048 bis 100 kHz, sonst 4096), Hop N/4, Latenz N Samples.
#include "Core/Core.h"
#include <array>
#include <complex>

namespace onno::core
{
// ---------------------------------------------------------------- reelle FFT ohne JUCE (könnte nach Common/OnnoDSP.h wandern)
// Gleiche Konvention wie juce::dsp::FFT: forward ohne Skalierung, X[k] = sum x[n] e^(-2 pi i k n / N), Bins 0..N/2
// verschachtelt (re, im); inverse rechnet aus den Bins 0..N/2 (hermitesch ergänzt) wieder N reelle Werte, mit 1/N.
// Intern: komplexe Radix-2-FFT der halben Länge (gerade/ungerade Samples als Real-/Imaginärteil) plus Entflechtung.
class TameFFT
{
public:
    using cf = std::complex<float>;

    void init (int size)
    {
        N = size; M = size / 2;
        buf.assign ((size_t) M, cf());
        tw.resize ((size_t) std::max (1, M / 2));
        for (int j = 0; j < M / 2; ++j)
        {
            const double a = -2.0 * 3.14159265358979323846 * j / M;
            tw[(size_t) j] = cf ((float) std::cos (a), (float) std::sin (a));
        }
        post.resize ((size_t) M + 1);
        for (int k = 0; k <= M; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / N;
            post[(size_t) k] = cf ((float) std::cos (a), (float) std::sin (a));
        }
        rev.resize ((size_t) M);
        int bits = 0;
        while ((1 << bits) < M) ++bits;
        for (int i = 0; i < M; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    int size() const { return N; }

    // in: N reelle Werte; out: 2 * (N/2 + 1) Werte (re, im) für die Bins 0..N/2
    void forward (const float* in, float* out)
    {
        for (int n = 0; n < M; ++n) buf[(size_t) rev[(size_t) n]] = cf (in[2 * n], in[2 * n + 1]);
        transform (false);
        for (int k = 0; k <= M; ++k)
        {
            const cf a = buf[(size_t) (k % M)], b = std::conj (buf[(size_t) ((M - k) % M)]);
            const cf e = 0.5f * (a + b);
            const cf o = cf (0.0f, -0.5f) * (a - b);
            const cf x = e + post[(size_t) k] * o;
            out[2 * k] = x.real(); out[2 * k + 1] = x.imag();
        }
    }

    // in: 2 * (N/2 + 1) Werte (Bins 0..N/2), out: N reelle Werte. Imaginärteil von DC und Nyquist zählt nicht (wie JUCE).
    void inverse (const float* in, float* out)
    {
        auto bin = [in, this] (int k) { return (k == 0 || k == M) ? cf (in[2 * k], 0.0f) : cf (in[2 * k], in[2 * k + 1]); };
        for (int k = 0; k < M; ++k)
        {
            const cf a = bin (k), b = std::conj (bin (M - k));
            const cf e = 0.5f * (a + b);
            const cf o = 0.5f * (a - b) * std::conj (post[(size_t) k]);
            buf[(size_t) rev[(size_t) k]] = e + cf (0.0f, 1.0f) * o;
        }
        transform (true);
        const float s = 1.0f / (float) M;
        for (int n = 0; n < M; ++n) { out[2 * n] = buf[(size_t) n].real() * s; out[2 * n + 1] = buf[(size_t) n].imag() * s; }
    }

private:
    // iterative Radix-2-FFT auf buf (Eingang bereits bit-umgekehrt)
    void transform (bool inv)
    {
        for (int len = 2; len <= M; len <<= 1)
        {
            const int half = len / 2, step = M / len;
            for (int i = 0; i < M; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const cf w = inv ? std::conj (tw[(size_t) (j * step)]) : tw[(size_t) (j * step)];
                    const cf u = buf[(size_t) (i + j)], t = w * buf[(size_t) (i + j + half)];
                    buf[(size_t) (i + j)] = u + t;
                    buf[(size_t) (i + j + half)] = u - t;
                }
        }
    }

    int N = 0, M = 0;
    std::vector<cf> buf, tw, post;
    std::vector<int> rev;
};

class TameCore : public PluginCore
{
public:
    static constexpr int numNodes = 4, numDisplayBands = 160;

    static const std::vector<std::string>& profileNames()
    {
        static const std::vector<std::string> n { "Custom", "Rhythm Gtr", "Lead Gtr", "Scream", "Clean Vox", "Snare", "Overheads", "Bass", "Mix Bus" };
        return n;
    }

    static std::string nid (int n, const char* p) { return "n" + std::to_string (n + 1) + p; }

    static ParamList makeParams()
    {
        static const float defNodeF[numNodes] = { 250, 1000, 3500, 8000 };
        ParamList l;
        p::lin (l, "depth", "Depth", 0, 100, 50, "%");
        p::lin (l, "sharp", "Sharpness", 0, 100, 50, "%");
        p::lin (l, "sel", "Selectivity", 0, 20, 4, "dB");
        p::lg (l, "attack", "Attack", 1, 100, 5, "ms");
        p::lg (l, "release", "Release", 10, 500, 80, "ms");
        p::lg (l, "low", "Range Low", 20, 2000, 120, "Hz");
        p::lg (l, "high", "Range High", 1000, 20000, 16000, "Hz");
        p::lin (l, "mix", "Mix", 0, 100, 100, "%");
        p::lin (l, "output", "Output", -12, 12, 0, "dB");
        for (int n = 0; n < numNodes; ++n)
        {
            p::lg (l, nid (n, "freq"), "Node " + std::to_string (n + 1) + " Freq", 20, 20000, defNodeF[n], "Hz");
            p::lin (l, nid (n, "gain"), "Node " + std::to_string (n + 1) + " Sensitivity", -12, 12, 0, "dB");
        }
        p::ch (l, "profile", "Profile", profileNames(), 0, false);
        p::bl (l, "delta", "Delta", false);
        p::bl (l, "link", "Stereo Link", true);
        p::bl (l, "bypass", "Bypass", false);
        return l;
    }

    enum { depth, sharp, sel, attack, release, low, high, mix, output, node0 /* n1freq, n1gain, n2freq, ... */,
           profile = node0 + 2 * numNodes, delta, link, bypass };

    // Profile 1..8 (Metal-Quellen): Werte aller Regler, die ein Profil setzt. Profil 0 (Custom) = Standardwerte.
    struct Profile { float depth, sharp, sel, att, rel, low, high; float nf[numNodes], ng[numNodes]; };
    static const Profile& profileData (int pr)
    {
        static const Profile profiles[] = {
            { 50, 50, 4, 5, 80, 120, 16000, { 250, 1000, 3500, 8000 }, { 0, 0, 0, 0 } },          // Custom (Standard)
            { 45, 55, 3, 5, 60, 150, 9000, { 280, 1000, 3800, 7000 }, { 4, -3, 5, 2 } },         // Rhythmusgitarre: Mulm und Fizz
            { 40, 65, 3, 4, 70, 400, 11000, { 700, 1500, 2800, 5500 }, { -2, 0, 4, 3 } },       // Leadgitarre: harte Präsenz
            { 55, 45, 3, 3, 50, 700, 13000, { 900, 2500, 4500, 8000 }, { -2, 5, 3, 4 } },       // Scream: Schärfe und Zischen
            { 35, 50, 4, 5, 80, 180, 13000, { 320, 1200, 3000, 7500 }, { 2, -1, 3, 3 } },       // Clean-Gesang: Boxiness, Härte, S
            { 50, 75, 3, 2, 60, 150, 10000, { 220, 900, 3000, 6000 }, { 4, 3, 0, 1 } },         // Snare: Ringen der Felle
            { 45, 50, 3, 3, 90, 1500, 16000, { 2500, 4500, 8000, 12000 }, { 1, 3, 4, 2 } },     // Overheads: Becken-Schärfe
            { 35, 60, 4, 8, 100, 40, 4000, { 90, 250, 800, 2000 }, { 2, 0, 3, 1 } },            // Bass: Dröhnen und Klacken
            { 20, 50, 5, 8, 120, 100, 16000, { 250, 1000, 3500, 8000 }, { 0, 0, 2, 1 } },       // Summe: sanft
        };
        return profiles[std::clamp (pr, 0, (int) (sizeof (profiles) / sizeof (profiles[0])) - 1)];
    }

    // Werte eines Profils als (id, Wert)-Liste, inklusive "profile"
    static std::vector<std::pair<std::string, float>> profileValues (int pr)
    {
        pr = std::clamp (pr, 0, (int) profileNames().size() - 1);
        const auto& P = profileData (pr);
        std::vector<std::pair<std::string, float>> r { { "depth", P.depth }, { "sharp", P.sharp }, { "sel", P.sel }, { "attack", P.att },
                                                       { "release", P.rel }, { "low", P.low }, { "high", P.high } };
        for (int n = 0; n < numNodes; ++n) { r.push_back ({ nid (n, "freq"), P.nf[n] }); r.push_back ({ nid (n, "gain"), P.ng[n] }); }
        r.push_back ({ "profile", (float) pr });
        return r;
    }

    // Werkspresets = Profile 1..8 (setzen dieselben Regler wie die Profil-Knöpfe)
    bool keepOnPreset (const std::string& id) const override { return id == "mix" || id == "output" || id == "delta" || id == "link"; }

    std::vector<Preset> factoryPresets() const override
    {
        std::vector<Preset> l;
        for (int i = 1; i < (int) profileNames().size(); ++i) l.push_back ({ profileNames()[(size_t) i], {}, profileValues (i) });
        return l;
    }

    // Empfindlichkeit (dB) bei Frequenz f aus den vier Knoten (Frequenz in Hz, Empfindlichkeit in dB)
    static float sensitivity (float f, const float* nodeF, const float* nodeG)
    {
        float s = 0;
        for (int n = 0; n < numNodes; ++n)
        {
            const float d = std::log2 (std::max (f, 1.0f) / nodeF[n]) / 0.6f;
            s += nodeG[n] * std::exp (-0.5f * d * d);
        }
        return s;
    }
    float sensitivityAt (float f) const
    {
        float nf[numNodes], ng[numNodes];
        for (int n = 0; n < numNodes; ++n) { nf[n] = v[(size_t) (node0 + 2 * n)]; ng[n] = v[(size_t) (node0 + 2 * n + 1)]; }
        return sensitivity (f, nf, ng);
    }

    // Mitte des Anzeigebands b (log. 20 Hz .. 20 kHz)
    static float displayBandFreq (int b) { return 20.0f * std::pow (1000.0f, (b + 0.5f) / numDisplayBands); }

    // disp (2 * numDisplayBands Werte, Bänder b = 0..159 logarithmisch 20 Hz .. 20 kHz, Mitte = displayBandFreq (b)):
    //   disp[b]        stärkste Reduktion im Band (dB, <= 0), Minimum seit dem letzten resetDisplay()
    //   disp[160 + b]  Eingangsspektrum (Mono-Summe) im Band in dB: 0 dB = Vollaussteuerungs-Sinus,
    //                  +3 dB/Oktave Neigung um 1 kHz wie Analyzer.h; Maximum seit dem letzten resetDisplay() (sonst -120)
    TameCore() : PluginCore (makeParams()) { disp.assign (2 * numDisplayBands, 0.0f); resetDisplay(); }

    int latency() const override { return N; }

    void resetDisplay() override
    {
        std::fill (disp.begin(), disp.begin() + numDisplayBands, 0.0f);
        std::fill (disp.begin() + numDisplayBands, disp.end(), -120.0f);
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        const int order = sr <= 50000 ? 10 : (sr <= 100000 ? 11 : 12);
        N = 1 << order; hop = N / 4;
        fft.init (N);
        win.resize ((size_t) N);
        double wsum = 0;
        for (int i = 0; i < N; ++i)
        {
            win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * 3.14159265358979f * i / N));
            wsum += win[(size_t) i];
        }
        const size_t K = (size_t) N / 2 + 1;
        for (int c = 0; c < 2; ++c)
        {
            inRing[c].assign ((size_t) N, 0.0f);
            outAcc[c].assign ((size_t) N, 0.0f);
            spec[c].assign (K * 2, 0.0f);
            pdb[c].assign (K, -120.0f);
            gdb[c].assign (K, 0.0f);
            state[c].assign (K, 0.0f);
            det[c].assign (K, 0.0f);
        }
        frame.assign ((size_t) N, 0.0f);
        sens.assign (K, 0.0f);
        prefix.assign (K + 1, 0.0f);
        lo.assign (K, 0); hi.assign (K, 0);
        lastWidth = -1;
        pos = hopCount = 0;
        mixS.reset (sr, 0.03); mixS.setCurrentAndTargetValue (v[mix] * 0.01f);
        outS.reset (sr, 0.03); outS.setCurrentAndTargetValue (dsp::db2a (v[output]));

        // Anzeige: Bin -> Band für die Reduktion, Band -> Bins für das Spektrum
        const float binHz = (float) sr / N;
        binBand.assign (K, -1);
        for (int k = 1; k < (int) K; ++k)
        {
            const float f = k * binHz;
            if (f < 20.0f || f >= 20000.0f) continue;
            binBand[(size_t) k] = std::clamp ((int) (std::log (f / 20.0f) / std::log (1000.0f) * numDisplayBands), 0, numDisplayBands - 1);
        }
        for (int b = 0; b < numDisplayBands; ++b)
        {
            auto& s = bandSpec[(size_t) b];
            const float f0 = 20.0f * std::pow (1000.0f, (float) b / numDisplayBands), f1 = 20.0f * std::pow (1000.0f, (b + 1.0f) / numDisplayBands);
            s.tilt = 3.0f * std::log2 (displayBandFreq (b) / 1000.0f);
            if ((f1 - f0) / binHz < 1.0f)   // schmaler als ein Bin: zwischen Nachbar-Bins interpolieren
            {
                const float kb = std::clamp (0.5f * (f0 + f1) / binHz, 1.0f, (float) K - 2.0f);
                s.k0 = (int) kb; s.k1 = -1; s.frac = kb - (float) s.k0;
            }
            else
            {
                s.k0 = std::clamp ((int) std::floor (f0 / binHz), 1, (int) K - 2);
                s.k1 = std::clamp ((int) std::ceil (f1 / binHz), s.k0, (int) K - 2);
                s.frac = 0;
            }
        }
        // Betrag -> Sinus-Amplitude: |X| * 2 / Summe(Fenster)
        specScale = (float) (4.0 / (wsum * wsum));
        resetDisplay();
    }

    void process (float* L, float* R, int n, int nch_, const float*, const float*) override
    {
        nch = std::min (2, nch_);
        if (nch <= 0) return;
        float* ch[2] = { L, nch > 1 ? R : L };
        const bool byp = v[bypass] > 0.5f, dlt = v[delta] > 0.5f;
        mixS.setTargetValue (v[mix] * 0.01f);
        outS.setTargetValue (dsp::db2a (v[output]));
        float inMax = 0, outMax = 0;
        blockGr = 0;

        for (int i = 0; i < n; ++i)
        {
            const float m = mixS.getNextValue(), og = outS.getNextValue();
            for (int c = 0; c < nch; ++c)
            {
                float* d = ch[c];
                const float dryD = inRing[c][(size_t) pos];      // Eingang von vor N Samples = Latenz
                inMax = std::max (inMax, std::abs (d[i]));
                inRing[c][(size_t) pos] = d[i];
                const float wet = outAcc[c][(size_t) pos];
                outAcc[c][(size_t) pos] = 0;
                float y;
                if (byp) y = dryD;
                else if (dlt) y = (dryD - wet) * og;
                else y = (m * wet + (1 - m) * dryD) * og;
                d[i] = y;
                outMax = std::max (outMax, std::abs (y));
            }
            if (++pos >= N) pos = 0;
            if (++hopCount >= hop) { hopCount = 0; processFrame(); }
        }
        inPk = inMax; outPk = outMax; grMin = blockGr;
    }

private:
    void computeGains (const float* p, float* g, std::vector<float>& st)
    {
        const int K = N / 2 + 1;
        const float binHz = (float) sr / N;

        // Breite der Hüllkurve: weich = breite Hüllkurve (fängt auch breite Buckel), scharf = nur schmale Spitzen
        const float width = 3.0f - 2.5f * v[sharp] * 0.01f;
        if (std::abs (width - lastWidth) > 1.0e-3f)
        {
            lastWidth = width;
            const float r = std::pow (2.0f, width * 0.5f);
            for (int k = 0; k < K; ++k)
            {
                lo[(size_t) k] = std::clamp ((int) std::floor ((float) k / r), 1, K - 1);
                hi[(size_t) k] = std::clamp ((int) std::ceil ((float) k * r), 1, K - 1);
                if (hi[(size_t) k] - lo[(size_t) k] < 2) hi[(size_t) k] = std::min (K - 1, lo[(size_t) k] + 2);
            }
        }

        // Pegel unterhalb der Rauschgrenze zählen nicht (sonst "findet" das Plugin Resonanzen in der Stille)
        const float floorDb = 20.0f * std::log10 (N * 0.25f) - 80.0f;
        prefix[0] = 0;
        for (int k = 0; k < K; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + std::max (p[k], floorDb);

        const float d = v[depth] * 0.01f, selDb = v[sel];
        const float amount = std::min (1.0f, 1.5f * d), maxRed = 36.0f * d;
        const float lowF = v[low], highF = v[high];
        const float frameSec = (float) hop / (float) sr;
        const float att = std::exp (-frameSec / (v[attack] * 0.001f)), rel = std::exp (-frameSec / (v[release] * 0.001f));

        for (int k = 1; k < K; ++k)
        {
            const float f = k * binHz;
            float w = 1.0f;
            if (f < lowF) w = std::clamp (1.0f - 3.0f * std::log2 (lowF / f), 0.0f, 1.0f);
            else if (f > highF) w = std::clamp (1.0f - 3.0f * std::log2 (f / highF), 0.0f, 1.0f);
            float target = 0;
            if (w > 0 && p[k] > floorDb)
            {
                const float env = (prefix[(size_t) hi[(size_t) k] + 1] - prefix[(size_t) lo[(size_t) k]]) / (float) (hi[(size_t) k] - lo[(size_t) k] + 1);
                const float excess = p[k] - env - selDb + sens[(size_t) k];
                if (excess > 0) target = -std::min (maxRed, excess * amount) * w;
            }
            auto& s = st[(size_t) k];
            s = target < s ? att * s + (1 - att) * target : rel * s + (1 - rel) * target;
        }
        // leicht über die Frequenz glätten (gegen "musikalisches Rauschen")
        g[0] = 0;
        for (int k = 1; k < K - 1; ++k) g[k] = 0.25f * st[(size_t) k - 1] + 0.5f * st[(size_t) k] + 0.25f * st[(size_t) k + 1];
        g[K - 1] = st[(size_t) K - 1];
    }

    // Erkennungsspektrum: Leistung je Bin leicht über Frequenz und Zeit (~12 ms) geglättet, dann in dB.
    // Ohne Glättung schwanken einzelne Bins bei Rauschen so stark, dass Zufallsspitzen als Resonanz gelten.
    void detect (int c, std::vector<float>& power)
    {
        const int K = N / 2 + 1;
        const float a = std::exp (-(float) hop / ((float) sr * 0.012f));
        auto& dt = det[c];
        float prev = power[0];
        for (int k = 0; k < K; ++k)
        {
            const float cur = power[(size_t) k], next = k + 1 < K ? power[(size_t) k + 1] : cur;
            const float sm = 0.25f * prev + 0.5f * cur + 0.25f * next;
            prev = cur;
            dt[(size_t) k] = a * dt[(size_t) k] + (1 - a) * sm;
        }
        for (int k = 0; k < K; ++k) power[(size_t) k] = 10.0f * std::log10 (dt[(size_t) k] + 1.0e-12f);
    }

    void processFrame()
    {
        const int K = N / 2 + 1;
        const float binHz = (float) sr / N;
        float nf[numNodes], ng[numNodes];
        for (int n = 0; n < numNodes; ++n) { nf[n] = v[(size_t) (node0 + 2 * n)]; ng[n] = v[(size_t) (node0 + 2 * n + 1)]; }
        for (int k = 0; k < K; ++k) sens[(size_t) k] = sensitivity (k * binHz, nf, ng);

        for (int c = 0; c < nch; ++c)
        {
            for (int i = 0; i < N; ++i) frame[(size_t) i] = inRing[c][(size_t) ((pos + i) % N)] * win[(size_t) i];
            auto& w = spec[c];
            fft.forward (frame.data(), w.data());
            for (int k = 0; k < K; ++k)
            {
                const float re = w[(size_t) (2 * k)], im = w[(size_t) (2 * k + 1)];
                pdb[c][(size_t) k] = re * re + im * im;
            }
        }

        // Anzeige: Eingangsspektrum der Mono-Summe (wie Analyzer.h), Spitzen halten bis resetDisplay()
        {
            auto pw = [this] (int k)
            {
                float re = spec[0][(size_t) (2 * k)], im = spec[0][(size_t) (2 * k + 1)];
                if (nch > 1) { re = 0.5f * (re + spec[1][(size_t) (2 * k)]); im = 0.5f * (im + spec[1][(size_t) (2 * k + 1)]); }
                return 10.0f * std::log10 (std::max (1.0e-18f, (re * re + im * im) * specScale));
            };
            for (int b = 0; b < numDisplayBands; ++b)
            {
                const auto& s = bandSpec[(size_t) b];
                float db;
                if (s.k1 < 0) { const float a = pw (s.k0), c = pw (s.k0 + 1); db = a + s.frac * (c - a); }
                else { db = -180.0f; for (int k = s.k0; k <= s.k1; ++k) db = std::max (db, pw (k)); }
                auto& o = disp[(size_t) (numDisplayBands + b)];
                o = std::max (o, db + s.tilt);
            }
        }

        const bool lnk = v[link] > 0.5f || nch < 2;
        if (lnk)
        {
            if (nch > 1)
                for (int k = 0; k < K; ++k) pdb[0][(size_t) k] = 0.5f * (pdb[0][(size_t) k] + pdb[1][(size_t) k]);
            detect (0, pdb[0]);
            computeGains (pdb[0].data(), gdb[0].data(), state[0]);
            if (nch > 1) gdb[1] = gdb[0];
        }
        else
            for (int c = 0; c < nch; ++c)
            {
                detect (c, pdb[c]);
                computeGains (pdb[c].data(), gdb[c].data(), state[c]);
            }

        // Anzeige: stärkste Reduktion je Band
        for (int k = 1; k < K; ++k)
        {
            const int b = binBand[(size_t) k];
            if (b < 0) continue;
            const float g = std::min (gdb[0][(size_t) k], nch > 1 ? gdb[1][(size_t) k] : 0.0f);
            auto& o = disp[(size_t) b];
            o = std::min (o, g);
            blockGr = std::min (blockGr, g);
        }

        // Verstärkung anwenden, zurücktransformieren, überlappend addieren
        for (int c = 0; c < nch; ++c)
        {
            auto& w = spec[c];
            for (int k = 0; k < K; ++k)
            {
                const float gl = dsp::db2a (gdb[c][(size_t) k]);
                w[(size_t) (2 * k)] *= gl; w[(size_t) (2 * k + 1)] *= gl;
            }
            fft.inverse (w.data(), frame.data());
            // sqrt-Hann zweimal = Hann; bei 75 % Überlappung summiert sich Hann zu 2
            for (int i = 0; i < N; ++i)
                outAcc[c][(size_t) ((pos + i) % N)] += frame[(size_t) i] * win[(size_t) i] * 0.5f;
        }
    }

    struct BandSpec { int k0 = 1, k1 = -1; float frac = 0, tilt = 0; };

    double sr = 48000;
    int N = 1024, hop = 256, pos = 0, hopCount = 0, nch = 2;
    TameFFT fft;
    std::vector<float> win, frame, inRing[2], outAcc[2], spec[2], pdb[2], gdb[2], state[2], det[2], sens, prefix;
    std::vector<int> lo, hi, binBand;
    std::array<BandSpec, numDisplayBands> bandSpec {};
    float lastWidth = -1, specScale = 1, blockGr = 0;
    Smoothed mixS, outS;
};
} // namespace onno::core
