#pragma once
// Klang-Kern der Onno-Suite ohne JUCE: läuft im VST3/AU (über JUCE) und als WebAssembly im Browser.
// Ein Kern kennt seine Parameter (Tabelle), bekommt pro Block die aktuellen Werte und rechnet auf float-Kanälen.
#include "Common/OnnoDSP.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace onno::core
{
// ---------------------------------------------------------------- Parameter
struct ParamDef
{
    enum Kind { lin, log, choice, toggle };
    std::string id, name;
    Kind kind = lin;
    float min = 0, max = 1, def = 0;
    std::string unit;
    std::vector<std::string> choices;   // nur choice
    bool automatable = true;
};

using ParamList = std::vector<ParamDef>;

namespace p
{
    inline void lin (ParamList& l, std::string id, std::string name, float lo, float hi, float def, std::string unit = {})
    { l.push_back ({ std::move (id), std::move (name), ParamDef::lin, lo, hi, def, std::move (unit), {}, true }); }
    inline void lg (ParamList& l, std::string id, std::string name, float lo, float hi, float def, std::string unit = {})
    { l.push_back ({ std::move (id), std::move (name), ParamDef::log, lo, hi, def, std::move (unit), {}, true }); }
    inline void ch (ParamList& l, std::string id, std::string name, std::vector<std::string> items, int def, bool automatable = true)
    { const float hi = (float) items.size() - 1; l.push_back ({ std::move (id), std::move (name), ParamDef::choice, 0, hi, (float) def, {}, std::move (items), automatable }); }
    inline void bl (ParamList& l, std::string id, std::string name, bool def)
    { l.push_back ({ std::move (id), std::move (name), ParamDef::toggle, 0, 1, def ? 1.0f : 0.0f, {}, {}, true }); }
}

inline int indexOf (const ParamList& l, const std::string& id)
{
    for (size_t i = 0; i < l.size(); ++i) if (l[i].id == id) return (int) i;
    return -1;
}

// ---------------------------------------------------------------- Glättung (wie juce::SmoothedValue, linear)
class Smoothed
{
public:
    void reset (double sr, double seconds) { steps = (int) std::floor (seconds * sr); setCurrentAndTargetValue (target); }
    void setCurrentAndTargetValue (float v) { target = current = v; countdown = 0; }
    void setTargetValue (float v)
    {
        if (v == target) return;
        if (steps <= 0) { setCurrentAndTargetValue (v); return; }
        target = v; countdown = steps; step = (target - current) / (float) countdown;
    }
    float getNextValue()
    {
        if (countdown <= 0) return target;
        --countdown;
        if (countdown > 0) current += step; else current = target;
        return current;
    }
    float getTargetValue() const { return target; }

private:
    float current = 0, target = 0, step = 0;
    int steps = 0, countdown = 0;
};

// Kennlinie eines Kompressors in dB (Soft-Knee); ratio >= 99 gilt als unendlich
inline float gainComputer (float x, float thr, float ratio, float knee)
{
    const float slope = ratio >= 99.0f ? 0.0f : 1.0f / ratio;
    const float d = x - thr;
    if (2 * d < -knee) return x;
    if (knee > 0 && 2 * std::abs (d) <= knee)
        return x + (slope - 1) * (d + knee * 0.5f) * (d + knee * 0.5f) / (2 * knee);
    return thr + d * slope;
}

// ---------------------------------------------------------------- Werkspresets
// Name, optionale Kategorie (Untermenü) und Abweichungen vom Standard in Einheiten des Parameters
struct Preset
{
    std::string name, category;
    std::vector<std::pair<std::string, float>> values;
};

// ---------------------------------------------------------------- Kern
class PluginCore
{
public:
    explicit PluginCore (ParamList list) : defs (std::move (list))
    {
        v.resize (defs.size());
        for (size_t i = 0; i < defs.size(); ++i) v[i] = defs[i].def;
    }
    virtual ~PluginCore() = default;

    const ParamList& params() const { return defs; }
    int param (const std::string& id) const { return indexOf (defs, id); }

    virtual void prepare (double sampleRate, int maxBlock) = 0;
    // L/R: Kanäle (R == L bei Mono), nch 1 oder 2; Sidechain optional
    virtual void process (float* L, float* R, int n, int nch, const float* scL = nullptr, const float* scR = nullptr) = 0;
    virtual int latency() const { return 0; }
    virtual std::vector<Preset> factoryPresets() const { return {}; }
    // Tempo des Hosts (BPM) für tempo-synchrone Plugins
    virtual void setTempo (double) {}
    // Parameter, die beim Laden eines Werkspresets bleiben (z. B. Mix, Ausgang)
    virtual bool keepOnPreset (const std::string&) const { return false; }
    // Nachklang in Sekunden bei den aktuellen Werten (für Mixdown-Länge)
    virtual double tailSeconds() const { return 0.0; }

    std::vector<float> v;   // aktuelle Werte in Einheiten des Parameters, Reihenfolge wie params()
    // Anzeige: Spitzen des letzten Blocks (linear) und tiefste Gain Reduction (dB)
    float inPk = 0, outPk = 0, grMin = 0, detPk = 0;
    // weitere Anzeigewerte, je Plugin festgelegt (z. B. GR pro Band, Spektrum). Ein Kern darf Werte über
    // mehrere Blöcke sammeln (Spitzen halten), bis die Anzeige sie abholt und resetDisplay() aufruft.
    std::vector<float> disp;
    virtual void resetDisplay() {}

protected:
    ParamList defs;
};

// ---------------------------------------------------------------- Lautheitsausgleich (MATCH)
// misst Ein- und Ausgang K-gewichtet (~1 s) und gleicht den Ausgang an die Eingangslautheit an.
// Pro A/B-Platz wird das gemessene Verhältnis gemerkt, damit ein Umschalten sofort richtig sitzt.
class LoudnessMatch
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto* f : { &inShelf, &outShelf }) f->highShelf (sr, std::min (1500.0, sr * 0.4), 0.7, 4.0);
        for (auto* f : { &inHp, &outHp }) f->highPass (sr, 60.0, 0.5);
        for (auto* f : { &inShelf, &outShelf, &inHp, &outHp }) f->reset();
        msIn = msOut = 0;
        gain = 1.0f;
    }

    void measureIn (const float* const* ch, int nch, int n) { blockIn = energy (ch, nch, n, inShelf, inHp); }

    // slot = aktiver A/B-Platz; frozen = Bypass aktiv (dann nichts messen, Verstärkung 1)
    void apply (float* const* ch, int nch, int n, bool on, bool frozen, int slot)
    {
        if (n == 0 || nch == 0) return;
        const double blockOut = energy (ch, nch, n, outShelf, outHp);
        const bool jump = slot != lastSlot || frozen != lastFrozen;     // Umschalten: sofort richtig, nicht gleiten
        lastFrozen = frozen;
        if (slot != lastSlot)
        {
            lastSlot = slot;
            const float r = ratio[slot & 1];
            if (r > 0 && msIn > 0) msOut = msIn / ((double) r * r);
        }
        if (! frozen)
        {
            const double a = std::exp (-(double) n / (sr * 1.0));
            msIn = a * msIn + (1 - a) * blockIn;
            msOut = a * msOut + (1 - a) * blockOut;
        }
        float target = 1.0f;
        if (msIn > 1.0e-7 && msOut > 1.0e-10)      // unter -70 dBFS nichts nachführen, Wert halten
        {
            const float r = (float) std::clamp (std::sqrt (msIn / msOut), 0.063, 15.85);   // ±24 dB
            if (! frozen) ratio[slot & 1] = r;
            target = r;
        }
        else if (ratio[slot & 1] > 0) target = ratio[slot & 1];
        if (! on || frozen) target = 1.0f;
        const float g0 = gain;
        gain = jump ? target : target + (gain - target) * (float) std::exp (-(double) n / (sr * 0.08));
        if (std::abs (g0 - 1.0f) < 1.0e-6f && std::abs (gain - 1.0f) < 1.0e-6f) return;
        // Rampe wie AudioBuffer::applyGainRamp
        const float inc = (gain - g0) / (float) n;
        for (int c = 0; c < nch; ++c)
        {
            float g = g0;
            for (int i = 0; i < n; ++i) { ch[c][i] *= g; g += inc; }
        }
    }

    float currentGainDb() const { return dsp::a2db (gain); }
    float ratio[2] { 0.0f, 0.0f };

private:
    double energy (const float* const* ch, int nch, int n, dsp::Biquad& shelf, dsp::Biquad& hp)
    {
        double e = 0;
        const int m = std::min (nch, 2);
        for (int c = 0; c < m; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float y = hp.process (shelf.process (ch[c][i], c), c);
                e += (double) y * y;
            }
        return n > 0 ? e / ((double) n * std::max (1, m)) : 0.0;
    }

    double sr = 48000, msIn = 0, msOut = 0, blockIn = 0;
    float gain = 1.0f;
    int lastSlot = 0;
    bool lastFrozen = false;
    dsp::Biquad inShelf, outShelf, inHp, outHp;
};
} // namespace onno::core
