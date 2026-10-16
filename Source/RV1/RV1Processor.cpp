#include "RV1Processor.h"
#include "RV1Editor.h"

// Parameter aus der Tabelle des Kerns (gleiche Reihenfolge und IDs wie früher).
// Zwei Ausnahmen behalten ihren alten JUCE-Typ, damit Sessions und Automation unverändert bleiben:
// Pre-Delay mit Mitte bei 60 ms und das Programm als AudioParameterInt.
juce::AudioProcessorValueTreeState::ParameterLayout RV1Processor::createLayout()
{
    onno::prm::Layout p;
    for (auto& d : Core::makeParams())
    {
        if (d.id == "predelay")
        {
            juce::NormalisableRange<float> pd (d.min, d.max); pd.setSkewForCentre (60);
            onno::prm::fl (p, d.id, d.name, pd, d.def, d.unit);
        }
        else if (d.id == "program")
            p.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { d.id, 1 }, d.name, (int) d.min, (int) d.max, (int) d.def,
                                                                    juce::AudioParameterIntAttributes().withAutomatable (false)));
        else
            onno::prm::fromCore (p, { d });
    }
    return onno::prm::done (p);
}

RV1Processor::RV1Processor() : ModernProcessor ("Onno RV-1", "RV1", stereoBuses(), createLayout(), Core::maxTailSeconds)
{
    link.attach (apvts, core);
}

void RV1Processor::prepare (double sampleRate, int maxBlock)
{
    link.pull (core);
    core.prepare (sampleRate, maxBlock);
}

void RV1Processor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = std::min (2, getMainBusNumInputChannels()), n = buf.getNumSamples();
    if (nch == 0 || n == 0) return;
    link.pull (core);
    core.process (buf.getWritePointer (0), buf.getWritePointer (nch > 1 ? 1 : 0), n, nch);
    onno::atomicMax (inPeak, core.inPk);
    if (core.v[Core::bypass] <= 0.5f)       // bei Bypass bleiben Hall- und Duck-Anzeige stehen wie früher
    {
        onno::atomicMax (wetPeak, core.disp[0]);
        duckDb = core.disp[1];
    }
    core.resetDisplay();
}

// ----------------------------------------------------------------- Programme
void RV1Processor::loadProgram (int index)
{
    const int n = (int) programs().size();
    loadPreset (1 + (index % n + n) % n);
}

// Zustände von vor der Suite: Programmname aus dem Programm-Parameter übernehmen
void RV1Processor::migrateState (juce::ValueTree& st)
{
    if (presetName != "Init") return;
    for (auto c : st)
        if (c.getProperty ("id").toString() == "program")
        {
            const int i = juce::jlimit (0, (int) programs().size() - 1, (int) c.getProperty ("value"));
            if (i > 0) setPresetName (programs()[(size_t) i].name);
        }
}

juce::AudioProcessorEditor* RV1Processor::createEditor() { return new RV1Editor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RV1Processor(); }
#endif
