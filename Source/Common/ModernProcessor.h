#pragma once
// Gemeinsame Basis der modernen Onno-Suite: Parameterbaum, Zustand speichern/laden,
// Bus-Layouts (Stereo/Mono, optionaler Sidechain) und kleine Helfer für den Audio-Thread.
// Dazu das Bedien-Gerüst, das alle Plugins teilen: Rückgängig/Wiederholen, A/B-Vergleich,
// Presets (Werk + eigene Dateien) und Lautheitsausgleich (MATCH).
#include <juce_audio_processors/juce_audio_processors.h>
#include "Common/OnnoDSP.h"
#include "Core/Core.h"
#include <atomic>

namespace onno
{
// Logarithmischer Bereich: normierter Wert = Reglerstellung
inline juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float n) { return a * std::exp (n * std::log (b / a)); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float, float, float v) { return v; } };
}

inline void atomicMax (std::atomic<float>& a, float v)
{
    float cur = a.load();
    while (v > cur && ! a.compare_exchange_weak (cur, v)) {}
}

inline void atomicMin (std::atomic<float>& a, float v)
{
    float cur = a.load();
    while (v < cur && ! a.compare_exchange_weak (cur, v)) {}
}

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

// Verlauf für Anzeigen: Ring mit festen Einträgen (nur GUI-Thread)
template <int N>
struct History
{
    std::array<float, N> v {};
    int pos = 0;
    History (float init = -90.0f) { v.fill (init); }
    void push (float x) { v[(size_t) pos] = x; pos = (pos + 1) % N; }
    float operator[] (int i) const { return v[(size_t) ((pos + i) % N)]; }   // 0 = ältester Eintrag
    static constexpr int size() { return N; }
};

// Kleine Bausteine für Parameterlisten
namespace prm
{
    using Layout = std::vector<std::unique_ptr<juce::RangedAudioParameter>>;

    inline void fl (Layout& p, const juce::String& id, const juce::String& name, juce::NormalisableRange<float> r, float def, const juce::String& unit = {})
    {
        p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                                                                  juce::AudioParameterFloatAttributes().withLabel (unit)));
    }

    inline void lin (Layout& p, const juce::String& id, const juce::String& name, float lo, float hi, float def, const juce::String& unit = {})
    {
        fl (p, id, name, juce::NormalisableRange<float> (lo, hi), def, unit);
    }

    inline void lg (Layout& p, const juce::String& id, const juce::String& name, float lo, float hi, float def, const juce::String& unit = {})
    {
        fl (p, id, name, logRange (lo, hi), def, unit);
    }

    inline void bl (Layout& p, const juce::String& id, const juce::String& name, bool def)
    {
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    }

    inline void ch (Layout& p, const juce::String& id, const juce::String& name, const juce::StringArray& items, int def, bool automatable = true)
    {
        p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def,
                                                                   juce::AudioParameterChoiceAttributes().withAutomatable (automatable)));
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout done (Layout& p) { return { p.begin(), p.end() }; }

    // Parameter aus der Tabelle eines Klang-Kerns anlegen (gleiche IDs, Bereiche und Standardwerte)
    inline void fromCore (Layout& p, const core::ParamList& defs)
    {
        for (auto& d : defs)
        {
            const juce::String id (d.id), name (d.name), unit (d.unit);
            switch (d.kind)
            {
                case core::ParamDef::lin: lin (p, id, name, d.min, d.max, d.def, unit); break;
                case core::ParamDef::log: lg (p, id, name, d.min, d.max, d.def, unit); break;
                case core::ParamDef::toggle: bl (p, id, name, d.def > 0.5f); break;
                case core::ParamDef::choice:
                {
                    juce::StringArray items;
                    for (auto& c : d.choices) items.add (juce::String (c));
                    ch (p, id, name, items, (int) d.def, d.automatable);
                    break;
                }
            }
        }
    }
}

// Verbindet die Parameter eines Plugins mit den Werten eines Klang-Kerns (Zeiger einmal holen, pro Block kopieren)
struct CoreLink
{
    std::vector<std::atomic<float>*> raw;
    void attach (juce::AudioProcessorValueTreeState& s, const core::PluginCore& c)
    {
        for (auto& d : c.params()) raw.push_back (s.getRawParameterValue (juce::String (d.id)));
    }
    void pull (core::PluginCore& c) const
    {
        for (size_t i = 0; i < raw.size(); ++i) c.v[i] = raw[i]->load();
    }
};

// Lautheitsausgleich (MATCH): im Klang-Kern, damit VST und Browser gleich rechnen
using LoudnessMatch = core::LoudnessMatch;

class ModernProcessor : public juce::AudioProcessor
{
public:
    // Werkspreset: Name und Werte (in Einheiten des Parameters); alles andere bleibt auf Standard
    struct FactoryPreset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
        juce::String category;      // optional: Untermenü im Preset-Menü
    };
    using Snapshot = juce::NamedValueSet;

    ModernProcessor (const juce::String& name, const juce::Identifier& stateId, const BusesProperties& buses,
                     juce::AudioProcessorValueTreeState::ParameterLayout layout, double tailSeconds = 0.0)
        : AudioProcessor (buses), apvts (*this, nullptr, stateId, std::move (layout)), pluginName (name), tail (tailSeconds)
    {
        pBypassBase = apvts.getRawParameterValue ("bypass");
    }

    static BusesProperties stereoBuses (bool sidechain = false)
    {
        auto b = BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
        if (sidechain) b = b.withInput ("Sidechain", juce::AudioChannelSet::stereo(), false);
        return b;
    }

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto out = l.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
        if (l.getMainInputChannelSet() != out) return false;
        if (l.inputBuses.size() > 1)
        {
            const auto sc = l.getChannelSet (true, 1);
            if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
        }
        return true;
    }

    // Die Plugins implementieren prepare() und process(); die Basis legt den Lautheitsausgleich darum
    void prepareToPlay (double sampleRate, int blockSize) final
    {
        match.prepare (sampleRate);
        prepare (sampleRate, blockSize);
    }

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) final
    {
        const int nch = std::min (2, getMainBusNumOutputChannels());
        match.measureIn (buf.getArrayOfReadPointers(), nch, buf.getNumSamples());
        process (buf, midi);
        match.apply (buf.getArrayOfWritePointers(), nch, buf.getNumSamples(), matchOn.load(), pBypassBase != nullptr && pBypassBase->load() > 0.5f, abSlot.load());
    }

    void releaseResources() override {}
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return pluginName; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return tail; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        auto st = apvts.copyState();
        st.setProperty ("onnoMatch", matchOn.load(), nullptr);
        st.setProperty ("onnoSlot", abSlot.load(), nullptr);
        st.setProperty ("onnoPreset", presetName, nullptr);
        for (int i = 0; i < 2; ++i)
            if (slotValid[i]) st.appendChild (snapshotToTree (slots[i], i == 0 ? "OnnoSlotA" : "OnnoSlotB"), nullptr);
        if (auto xml = st.createXml())
            copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
            {
                auto st = juce::ValueTree::fromXml (*xml);
                matchOn = (bool) st.getProperty ("onnoMatch", false);
                abSlot = juce::jlimit (0, 1, (int) st.getProperty ("onnoSlot", 0));
                presetName = st.getProperty ("onnoPreset", "Init").toString();
                for (int i = 0; i < 2; ++i)
                {
                    auto t = st.getChildWithName (i == 0 ? "OnnoSlotA" : "OnnoSlotB");
                    slotValid[i] = t.isValid();
                    if (t.isValid()) { slots[i] = treeToSnapshot (t); st.removeChild (t, nullptr); }
                }
                for (auto id : { "onnoMatch", "onnoSlot", "onnoPreset" }) st.removeProperty (id, nullptr);
                migrateState (st);
                apvts.replaceState (st);
            }
    }

    // Werte über den Namen (allokiert, nur außerhalb von processBlock nutzen)
    float v (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    // Zeiger einmal im Konstruktor holen, im Audio-Thread nur noch laden
    std::atomic<float>* raw (const juce::String& id) const { return apvts.getRawParameterValue (id); }
    bool on (const juce::String& id) const { return v (id) > 0.5f; }

    // Sidechain-Kanäle (nullptr, wenn kein Sidechain anliegt)
    const float* sidechain (juce::AudioBuffer<float>& buf, int ch)
    {
        if (getBusCount (true) < 2) return nullptr;
        auto* bus = getBus (true, 1);
        if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() == 0) return nullptr;
        auto sc = getBusBuffer (buf, true, 1);
        if (sc.getNumChannels() == 0) return nullptr;
        return sc.getReadPointer (std::min (ch, sc.getNumChannels() - 1));
    }

    // ------------------------------------------------------------ Bedien-Gerüst (nur GUI-Thread)
    // Parameter der Bedienung (Bypass, Ansicht): nicht in Rückgängig, A/B und Presets
    virtual bool isUiParam (const juce::String& id) const { return id == "bypass" || id == "view"; }
    // bleiben beim Laden eines Werkspresets stehen (z. B. Pegel, Mix)
    virtual bool keepOnPreset (const juce::String&) const { return false; }

    // Momentaufnahme aller Klang-Parameter (normierte Werte)
    Snapshot capture() const
    {
        Snapshot s;
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! isUiParam (rp->getParameterID()))
                    s.set (rp->getParameterID(), rp->getValue());
        return s;
    }

    void restore (const Snapshot& s)
    {
        for (auto& nv : s)
            if (auto* p = apvts.getParameter (nv.name.toString()))
            {
                const float v = (float) nv.value;
                if (std::abs (p->getValue() - v) < 1.0e-7f) continue;
                p->beginChangeGesture();
                p->setValueNotifyingHost (v);
                p->endChangeGesture();
            }
    }

    // vor jeder Bedienung aufrufen: merkt den Zustand davor
    void pushUndo()
    {
        auto s = capture();
        if (! undoStack.empty() && undoStack.back() == s) return;
        undoStack.push_back (std::move (s));
        if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
        redoStack.clear();
    }

    bool undo()
    {
        const auto cur = capture();
        while (! undoStack.empty() && undoStack.back() == cur) undoStack.pop_back();   // Schritte ohne Änderung
        if (undoStack.empty()) return false;
        redoStack.push_back (cur);
        restore (undoStack.back());
        undoStack.pop_back();
        return true;
    }

    bool redo()
    {
        if (redoStack.empty()) return false;
        undoStack.push_back (capture());
        restore (redoStack.back());
        redoStack.pop_back();
        return true;
    }

    bool canUndo() const { return ! undoStack.empty() && ! (undoStack.size() == 1 && undoStack.back() == capture()); }
    bool canRedo() const { return ! redoStack.empty(); }

    // A/B: zwei Speicherplätze; der aktive folgt immer den Reglern
    int slot() const { return abSlot.load(); }
    void selectSlot (int s)
    {
        s = juce::jlimit (0, 1, s);
        if (s == abSlot.load()) return;
        pushUndo();
        slots[abSlot.load()] = capture(); slotValid[abSlot.load()] = true;
        abSlot = s;
        if (slotValid[s]) restore (slots[s]);
        else { slots[s] = capture(); slotValid[s] = true; }
    }
    void copyToOtherSlot() { const int o = 1 - abSlot.load(); slots[o] = capture(); slotValid[o] = true; }

    // Presets: 0 = Init (Standardwerte), dann Werkspresets, dann eigene Dateien
    virtual std::vector<FactoryPreset> factoryPresets() const { return {}; }

    juce::File userPresetDir() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Onno").getChildFile ("Presets").getChildFile (pluginName);
    }

    juce::Array<juce::File> userPresets() const
    {
        auto files = userPresetDir().findChildFiles (juce::File::findFiles, false, "*.onnopreset");
        files.sort();
        return files;
    }

    juce::StringArray presetNames() const
    {
        juce::StringArray n { "Init" };
        for (auto& f : factoryPresets()) n.add (f.name);
        for (auto& f : userPresets()) n.add (f.getFileNameWithoutExtension());
        return n;
    }

    int numFactoryPresets() const { return (int) factoryPresets().size(); }
    int presetIndex() const { return presetNames().indexOf (presetName); }

    void loadPreset (int index)
    {
        const auto names = presetNames();
        if (! juce::isPositiveAndBelow (index, names.size())) return;
        pushUndo();
        Snapshot s;
        const auto fac = factoryPresets();
        const bool factory = index >= 1 && index <= (int) fac.size();
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto id = rp->getParameterID();
                if (isUiParam (id)) continue;
                s.set (id, factory && keepOnPreset (id) ? rp->getValue() : rp->getDefaultValue());
            }
        if (factory)
        {
            for (auto& [id, val] : fac[(size_t) index - 1].values)
                if (auto* p = apvts.getParameter (id)) s.set (id, p->convertTo0to1 (val));
        }
        else if (index > (int) fac.size())
        {
            if (auto xml = juce::XmlDocument::parse (userPresets()[index - 1 - (int) fac.size()]))
                for (auto* e : xml->getChildWithTagNameIterator ("P"))
                    if (auto* p = apvts.getParameter (e->getStringAttribute ("id")))
                        if (! isUiParam (p->getParameterID()))
                            s.set (p->getParameterID(), p->convertTo0to1 ((float) e->getDoubleAttribute ("v")));
        }
        restore (s);
        presetName = names[index];
    }

    void stepPreset (int dir)
    {
        const int n = presetNames().size();
        const int i = presetIndex();
        loadPreset (i < 0 ? 0 : (i + dir + n) % n);
    }

    bool saveUserPreset (const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName.trim());
        if (name.isEmpty()) return false;
        juce::XmlElement xml ("OnnoPreset");
        xml.setAttribute ("plugin", pluginName);
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! isUiParam (rp->getParameterID()))
                {
                    auto* e = xml.createNewChildElement ("P");
                    e->setAttribute ("id", rp->getParameterID());
                    e->setAttribute ("v", rp->convertFrom0to1 (rp->getValue()));
                }
        auto dir = userPresetDir();
        if (! dir.createDirectory()) return false;
        if (! xml.writeTo (dir.getChildFile (name + ".onnopreset"))) return false;
        presetName = name;
        return true;
    }

    juce::AudioProcessorValueTreeState apvts;

    // Anzeige-Pegel (Audio-Thread schreibt, GUI holt mit exchange ab)
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, grPeak { 0.0f };

    std::atomic<bool> matchOn { false };
    LoudnessMatch match;
    juce::String presetName { "Init" };

    // Name des Presets aus der Preset-Liste übernehmen (z. B. nach Laden eines alten Zustands)
    void setPresetName (const juce::String& n) { presetName = n; }

protected:
    virtual void prepare (double sampleRate, int blockSize) = 0;
    // ältere Zustände anpassen, bevor sie geladen werden
    virtual void migrateState (juce::ValueTree&) {}
    virtual void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;

private:
    static juce::ValueTree snapshotToTree (const Snapshot& s, const juce::Identifier& type)
    {
        juce::ValueTree t (type);
        for (auto& nv : s) t.setProperty (nv.name, nv.value, nullptr);
        return t;
    }

    static Snapshot treeToSnapshot (const juce::ValueTree& t)
    {
        Snapshot s;
        for (int i = 0; i < t.getNumProperties(); ++i) s.set (t.getPropertyName (i), t.getProperty (t.getPropertyName (i)));
        return s;
    }

    juce::String pluginName;
    double tail;
    std::atomic<float>* pBypassBase = nullptr;
    std::vector<Snapshot> undoStack, redoStack;
    Snapshot slots[2];
    bool slotValid[2] { false, false };
    std::atomic<int> abSlot { 0 };
};
} // namespace onno
