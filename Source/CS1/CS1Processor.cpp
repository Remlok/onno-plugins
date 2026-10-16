#include "CS1Processor.h"
#include "CS1Editor.h"

using namespace onno::dsp;

juce::AudioProcessorValueTreeState::ParameterLayout CS1Processor::createLayout()
{
    onno::prm::Layout p;
    onno::prm::fromCore (p, onno::core::CS1Core::makeParams());
    return onno::prm::done (p);
}

CS1Processor::CS1Processor() : ModernProcessor ("Onno CS-1", "CS1", stereoBuses(), createLayout())
{
    link.attach (apvts, core);
}

void CS1Processor::prepare (double sampleRate, int blockSize)
{
    sr = sampleRate;
    link.pull (core);
    core.prepare (sampleRate, blockSize);
    reportedLatency = core.latency();
    setLatencySamples (reportedLatency);
}

float CS1Processor::eqResponseDb (float f) const
{
    // Werte direkt aus den Parametern (GUI-Thread), gleiche Rechnung wie im Kern
    return onno::core::CS1Core::responseDb ([this] (int i) { return link.raw[(size_t) i]->load(); }, sr, f);
}

void CS1Processor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = std::min (2, getMainBusNumInputChannels()), n = buf.getNumSamples();
    if (nch == 0 || n == 0) return;
    auto* L = buf.getWritePointer (0);
    auto* R = buf.getWritePointer (nch > 1 ? 1 : 0);

    // --- Auto-Trim: Eingangspegel vor dem Trim messen (nur Passagen mit Signal) ---
    if (autoTrimArmed.load())
    {
        double e = 0;
        for (int i = 0; i < n; ++i) e += 0.5 * (L[i] * L[i] + R[i] * R[i]);
        if (e / n > 1.0e-6)  // über -60 dBFS
        {
            trimSum += e; trimCount += n;
            autoTrimProgress = (float) std::min (1.0, trimCount / (3.0 * sr));
            if (trimCount >= (long) (3.0 * sr))
            {
                const float rmsDb = 10.0f * std::log10 ((float) (trimSum / trimCount));
                autoTrimResult = juce::jlimit (-24.0f, 24.0f, -18.0f - rmsDb);
                autoTrimArmed = false;
                autoTrimDone = true;
                trimSum = 0; trimCount = 0;
            }
        }
    }
    else { trimSum = 0; trimCount = 0; }

    link.pull (core);
    core.process (L, R, n, nch, nullptr, nullptr);
    if (core.latency() != reportedLatency) { reportedLatency = core.latency(); setLatencySamples (reportedLatency); }

    // Anzeige (siehe disp-Belegung in CS1Core.h); im Bypass bleiben GR und Auto-Gain stehen
    onno::atomicMax (inPeak, core.inPk);
    onno::atomicMax (outPeak, core.outPk);
    if (core.v[(size_t) onno::core::CS1Core::bypass] <= 0.5f)
    {
        onno::atomicMin (gateGr, core.disp[0]);
        onno::atomicMin (compGr, core.disp[1]);
        onno::atomicMin (dsGr, core.disp[2]);
        autoGainDb = core.disp[3];
    }
    core.resetDisplay();
}

// Zustände von vor der Suite: eigener A/B-Speicher und Preset-Name in Großbuchstaben
void CS1Processor::migrateState (juce::ValueTree& st)
{
    st.removeChild (st.getChildWithName ("AB"), nullptr);
    const auto old = st.getProperty ("preset").toString();
    if (old.isNotEmpty())
        for (int i = 0; i < numPresets; ++i)
            if (old.equalsIgnoreCase (presetNames[i])) setPresetName (presetNames[i]);
    st.removeProperty ("preset", nullptr);
    st.removeProperty ("slot", nullptr);
}

juce::AudioProcessorEditor* CS1Processor::createEditor() { return new CS1Editor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CS1Processor(); }
#endif
