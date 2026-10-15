#pragma once
// Analoge Klangfarbe für die moderne Suite: Sättigung plus leichte Färbung im Frequenzgang.
// Modelle: Clean (aus), Brit (Übertrager, Neve-artig), Console (SSL-artig, sauber mit Glanz),
// Tube (Röhre, geradzahlige Obertöne), FET (1176-artig, bissig).
// Die Kennlinie läuft 4-fach überabgetastet und zusätzlich mit Antiderivative-Antialiasing (ADAA),
// damit Obertöne über 20 kHz nicht als falsche Töne zurückspiegeln (gemessen unter -90 dBc).
#include "Common/ModernProcessor.h"
#include "Common/OnnoDSP.h"

namespace onno
{
class Analog
{
public:
    enum Model { clean = 0, brit, console, tube, fet, numModels };

    static const juce::StringArray& names()
    {
        static const juce::StringArray n { "Clean", "Brit", "Console", "Tube", "FET" };
        return n;
    }

    static void addParams (prm::Layout& p)
    {
        prm::ch (p, "analog", "Analog Model", names(), clean);
        prm::lin (p, "drive", "Analog Drive", 0, 100, 35, "%");
    }

    void attach (const ModernProcessor& proc)
    {
        pModel = proc.raw ("analog");
        pDrive = proc.raw ("drive");
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        model = -1;
        kS.reset (sr, 0.05);
        kS.setCurrentAndTargetValue (1.0f);
        reset();
    }

    void reset()
    {
        u1[0] = u1[1] = x1[0] = x1[1] = 0;
        os[0].reset(); os[1].reset();
        for (auto& f : eq) f.reset();
        dc.reset();
    }

    // einmal pro Block: Modell und Drive übernehmen
    void beginBlock()
    {
        const int m = pModel ? juce::jlimit (0, numModels - 1, (int) pModel->load()) : clean;
        if (m != model) setModel (m);
        const float drive = pDrive ? pDrive->load() * 0.01f : 0.35f;
        kS.setTargetValue (dsp::db2a (spec.baseDb + drive * spec.maxDb));
    }

    bool active() const { return model != clean; }

    // pro Sample einmal aufrufen, danach tick() je Kanal
    void advance() { k = kS.getNextValue(); }

    float tick (float x, int ch, bool withEq = true)
    {
        if (model == clean) return x;
        float up[4];
        os[ch].up (x, up);
        for (auto& v : up) v = shape (v, ch);
        float out = os[ch].down (up);
        if (spec.bias != 0.0f) out = dc.process (out, ch);
        if (withEq) for (int i = 0; i < nEq; ++i) out = eq[i].process (out, ch);
        return out;
    }

    void process (float* L, float* R, int n, int nch)
    {
        beginBlock();
        if (model == clean) return;
        for (int i = 0; i < n; ++i)
        {
            advance();
            L[i] = tick (L[i], 0);
            if (nch > 1) R[i] = tick (R[i], 1);
        }
    }

private:
    struct Spec { float bias, mix, baseDb, maxDb; };

    void setModel (int m)
    {
        model = m;
        static const Spec specs[] = { { 0, 0, 0, 0 }, { 0.25f, 1, -10, 24 }, { 0.03f, 0.55f, -10, 21 }, { 0.45f, 1, -10, 21 }, { 0.12f, 1, -7, 27 } };
        spec = specs[m];
        tb = std::tanh (spec.bias);
        slope0 = 1.0f - tb * tb;
        nEq = 0;
        switch (m)
        {
            case brit:    eq[0].lowShelf (sr, 90, 0.7, 0.8);  eq[1].highShelf (sr, std::min (16000.0, sr * 0.4), 0.7, -0.8); nEq = 2; break;
            case console: eq[0].highShelf (sr, std::min (9000.0, sr * 0.4), 0.7, 0.6); nEq = 1; break;
            case tube:    eq[0].lowShelf (sr, 120, 0.7, 0.5); eq[1].highShelf (sr, std::min (12000.0, sr * 0.4), 0.7, -1.2); nEq = 2; break;
            case fet:     eq[0].peak (sr, 2500, 0.7, 0.8);    eq[1].highShelf (sr, std::min (10000.0, sr * 0.4), 0.7, 0.5); nEq = 2; break;
            default: break;
        }
        dc.highPass1 (sr, 8.0);
        reset();
    }

    // ein Sample der Kennlinie (mit ADAA) auf der 4-fachen Rate; Dry-Anteil mit gleicher halber Verzögerung
    float shape (float x, int ch)
    {
        const float u = k * x;
        const float du = u - u1[ch];
        float y;
        if (std::abs (du) > 1.0e-4f) y = (float) ((F (u) - F (u1[ch])) / du);
        else y = f (0.5f * (u + u1[ch]));
        u1[ch] = u;
        const float xd = 0.5f * (x + x1[ch]);
        x1[ch] = x;
        return (1.0f - spec.mix) * xd + spec.mix * y / (k * slope0);
    }

    // Kennlinie und ihre Stammfunktion (log cosh numerisch stabil)
    float f (float u) const { return std::tanh (u + spec.bias) - tb; }
    double F (float u) const
    {
        const double v = std::abs ((double) u + spec.bias);
        return v + std::log1p (std::exp (-2.0 * v)) - 0.6931471805599453 - (double) u * tb;
    }

    std::atomic<float>* pModel = nullptr;
    std::atomic<float>* pDrive = nullptr;
    double sr = 48000;
    int model = -1, nEq = 0;
    Spec spec { 0, 0, 0, 0 };
    float tb = 0, slope0 = 1, k = 1;
    float u1[2] {}, x1[2] {};
    dsp::Biquad eq[2], dc;
    dsp::Oversampler4 os[2];
    juce::SmoothedValue<float> kS;
};
} // namespace onno
