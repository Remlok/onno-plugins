#pragma once
#include "Common/ModernProcessor.h"
#include "Common/Analog.h"

// Onno VC-1: VCA-Kompressor (Feed-forward, Stereo-gekoppelt, Soft-Knee, Parallel-Blend)
// Teil der Suite: gemeinsame Kopfzeile (Presets, A/B, Rückgängig, MATCH, Bypass) und Analog-Färbung
class VC1Processor : public onno::ModernProcessor
{
public:
    VC1Processor();

    juce::AudioProcessorEditor* createEditor() override;
    std::vector<FactoryPreset> factoryPresets() const override;

    // Kennlinie in dB (für DSP und Anzeige)
    static float gainComputer (float x, float thr, float ratio, float knee);

    // Parameter-IDs in Reglerreihenfolge
    static constexpr int numKnobs = 7;
    static const char* const knobIds[numKnobs];

protected:
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float env = 0.0f;
    double sr = 48000.0;
    onno::Analog analog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VC1Processor)
};
