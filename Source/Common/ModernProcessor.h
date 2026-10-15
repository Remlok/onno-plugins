#pragma once
// Gemeinsame Basis der modernen Onno-Suite: Parameterbaum, Zustand speichern/laden,
// Bus-Layouts (Stereo/Mono, optionaler Sidechain) und kleine Helfer für den Audio-Thread.
#include <juce_audio_processors/juce_audio_processors.h>
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
}

class ModernProcessor : public juce::AudioProcessor
{
public:
    ModernProcessor (const juce::String& name, const juce::Identifier& stateId, const BusesProperties& buses,
                     juce::AudioProcessorValueTreeState::ParameterLayout layout, double tailSeconds = 0.0)
        : AudioProcessor (buses), apvts (*this, nullptr, stateId, std::move (layout)), pluginName (name), tail (tailSeconds)
    {
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
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
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

    juce::AudioProcessorValueTreeState apvts;

    // Anzeige-Pegel (Audio-Thread schreibt, GUI holt mit exchange ab)
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, grPeak { 0.0f };

private:
    juce::String pluginName;
    double tail;
};
} // namespace onno
