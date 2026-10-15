// WebAssembly-Schnittstelle der Onno-Suite: dieselben Klang-Kerne wie im VST3, als C-Funktionen für ein AudioWorklet.
// Ein Handle = eine Plugin-Instanz. Audio läuft über Puffer im Wasm-Speicher (onno_buffer), Parameter über onno_set.
#include "Core/Core.h"
#include "Core/EQCore.h"
#include "Core/CompCore.h"
#include <memory>
#include <string>
#include <vector>

#define EXPORT extern "C" __attribute__ ((used, visibility ("default")))

using namespace onno::core;

namespace
{
enum Kind { kEQ = 0, kComp, numKinds };
const char* const kindIds[numKinds] = { "eq", "comp" };

std::unique_ptr<PluginCore> make (int kind)
{
    switch (kind)
    {
        case kEQ:   return std::make_unique<EQCore>();
        case kComp: return std::make_unique<CompCore>();
        default:    return nullptr;
    }
}

struct Instance
{
    int kind = 0;
    std::unique_ptr<PluginCore> core;
    LoudnessMatch match;
    std::vector<float> buf[4];   // L, R, Sidechain L, Sidechain R
    int bypass = -1;
};

std::vector<std::unique_ptr<Instance>> instances;
std::string text;

void jsonString (std::string& o, const std::string& s)
{
    o += '"';
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    o += '"';
}
}

EXPORT int onno_kinds() { return numKinds; }
EXPORT const char* onno_kind_id (int kind) { return kind >= 0 && kind < numKinds ? kindIds[kind] : ""; }

// Parameter-Tabelle eines Plugins als JSON (für die Oberfläche)
EXPORT const char* onno_params_json (int kind)
{
    auto c = make (kind);
    text = "[";
    if (c)
    {
        static const char* const kinds[] = { "lin", "log", "choice", "toggle" };
        bool first = true;
        for (auto& d : c->params())
        {
            if (! first) text += ',';
            first = false;
            text += "{\"id\":"; jsonString (text, d.id);
            text += ",\"name\":"; jsonString (text, d.name);
            text += ",\"kind\":\""; text += kinds[d.kind]; text += '"';
            text += ",\"min\":" + std::to_string (d.min) + ",\"max\":" + std::to_string (d.max) + ",\"def\":" + std::to_string (d.def);
            text += ",\"unit\":"; jsonString (text, d.unit);
            text += ",\"choices\":[";
            for (size_t i = 0; i < d.choices.size(); ++i) { if (i) text += ','; jsonString (text, d.choices[i]); }
            text += "],\"automatable\":"; text += d.automatable ? "true" : "false";
            text += '}';
        }
    }
    text += "]";
    return text.c_str();
}

EXPORT int onno_create (int kind, double sampleRate, int maxBlock)
{
    auto c = make (kind);
    if (! c) return -1;
    auto in = std::make_unique<Instance>();
    in->kind = kind;
    in->core = std::move (c);
    in->bypass = in->core->param ("bypass");
    for (auto& b : in->buf) b.assign ((size_t) maxBlock, 0.0f);
    in->core->prepare (sampleRate, maxBlock);
    in->match.prepare (sampleRate);
    for (size_t i = 0; i < instances.size(); ++i)
        if (! instances[i]) { instances[i] = std::move (in); return (int) i; }
    instances.push_back (std::move (in));
    return (int) instances.size() - 1;
}

static Instance* get (int h) { return h >= 0 && h < (int) instances.size() ? instances[(size_t) h].get() : nullptr; }

EXPORT void onno_destroy (int h) { if (get (h)) instances[(size_t) h].reset(); }
EXPORT int onno_param_count (int h) { auto* in = get (h); return in ? (int) in->core->v.size() : 0; }
EXPORT void onno_set (int h, int i, float value) { if (auto* in = get (h); in && i >= 0 && i < (int) in->core->v.size()) in->core->v[(size_t) i] = value; }
EXPORT float onno_get (int h, int i) { auto* in = get (h); return in && i >= 0 && i < (int) in->core->v.size() ? in->core->v[(size_t) i] : 0.0f; }
EXPORT float* onno_buffer (int h, int ch) { auto* in = get (h); return in && ch >= 0 && ch < 4 ? in->buf[ch].data() : nullptr; }
EXPORT int onno_latency (int h) { auto* in = get (h); return in ? in->core->latency() : 0; }

// verarbeitet n Samples in den Puffern; matchOn/slot: Lautheitsausgleich wie in der Kopfzeile; sidechain: Puffer 2/3 nutzen
EXPORT void onno_process (int h, int n, int nch, int matchOn, int slot, int sidechain)
{
    auto* in = get (h);
    if (! in || n <= 0) return;
    n = std::min (n, (int) in->buf[0].size());
    float* ch[2] = { in->buf[0].data(), in->buf[1].data() };
    in->match.measureIn (ch, nch, n);
    in->core->process (ch[0], nch > 1 ? ch[1] : ch[0], n, nch, sidechain ? in->buf[2].data() : nullptr, sidechain ? in->buf[3].data() : nullptr);
    const bool byp = in->bypass >= 0 && in->core->v[(size_t) in->bypass] > 0.5f;
    in->match.apply (ch, nch, n, matchOn != 0, byp, slot);
}

// Anzeige: 0 Eingangsspitze, 1 Ausgangsspitze (linear), 2 Gain Reduction (dB), 3 Detektor (linear), 4 MATCH-Ausgleich (dB)
EXPORT float onno_meter (int h, int k)
{
    auto* in = get (h);
    if (! in) return 0;
    switch (k)
    {
        case 0: return in->core->inPk;
        case 1: return in->core->outPk;
        case 2: return in->core->grMin;
        case 3: return in->core->detPk;
        case 4: return in->match.currentGainDb();
        default: return 0;
    }
}

// EQ-Kurve in dB bei Frequenz f (nur Onno EQ)
EXPORT float onno_eq_response (int h, float f)
{
    auto* in = get (h);
    if (! in || in->kind != kEQ) return 0;
    return static_cast<EQCore*> (in->core.get())->responseDb (f);
}
