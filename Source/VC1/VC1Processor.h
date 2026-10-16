#pragma once
#include "Common/ModernProcessor.h"
#include "Common/Analog.h"
#include "Core/VC1Core.h"

// Onno VC-1: VCA-Kompressor (Feed-forward, Stereo-gekoppelt, Soft-Knee, Parallel-Blend)
// Teil der Suite: gemeinsame Kopfzeile (Presets, A/B, Rückgängig, MATCH, Bypass) und Analog-Färbung
class VC1Processor : public onno::ModernProcessor
{
public:
    VC1Processor();

    juce::AudioProcessorEditor* createEditor() override;
    std::vector<FactoryPreset> factoryPresets() const override { return fromCore (core.factoryPresets()); }

    // Kennlinie in dB (für DSP und Anzeige)
    static float gainComputer (float x, float thr, float ratio, float knee) { return onno::core::VC1Core::gainComputer (x, thr, ratio, knee); }

    // Parameter-IDs in Reglerreihenfolge
    static constexpr int numKnobs = 7;
    static const char* const knobIds[numKnobs];

protected:
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    onno::core::VC1Core core;
    onno::CoreLink link;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VC1Processor)
};
