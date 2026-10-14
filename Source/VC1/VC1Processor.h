#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

// Onno VC-1: VCA-Kompressor (Feed-forward, Stereo-gekoppelt, Soft-Knee, Parallel-Blend)
class VC1Processor : public juce::AudioProcessor
{
public:
    VC1Processor();

    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Onno VC-1"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Kennlinie in dB (für DSP und Anzeige)
    static float gainComputer (float x, float thr, float ratio, float knee);

    juce::AudioProcessorValueTreeState apvts;

    // Parameter-IDs in Reglerreihenfolge
    static constexpr int numKnobs = 7;
    static const char* const knobIds[numKnobs];

    // Pegel für die Anzeige: werden vom Audio-Thread hochgesetzt und von der GUI abgeholt
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, grPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float env = 0.0f;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VC1Processor)
};
