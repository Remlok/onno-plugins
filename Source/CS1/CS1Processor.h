#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Common/OnnoDSP.h"
#include <atomic>

// Onno CS-1: Channel Strip
// Kette: Trim → Drive (2x Oversampling) → Hoch-/Tiefpass → Gate → EQ ⇄ Kompressor → De-Esser → Output (+ Auto-Gain)
// Workflow: Quell-Presets mit einem Klick, Auto-Trim auf -18 dBFS RMS, Auto-Gain für faire Vergleiche, A/B-Speicher.
class CS1Processor : public juce::AudioProcessor
{
public:
    CS1Processor();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Onno CS-1"; }
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

    juce::AudioProcessorValueTreeState apvts;

    // --- Workflow (nur vom Message-Thread aufrufen) ---
    static constexpr int numPresets = 10;
    static const char* const presetNames[numPresets];
    void applyPreset (int index);
    void selectSlot (int slot);          // A/B: 0 = A, 1 = B
    int getSlot() const { return slot; }
    juce::String presetName;             // zuletzt geladenes Preset (Anzeige)
    void setParam (const juce::String& id, float value);

    // Auto-Trim: GUI setzt armed, Audio-Thread misst 3 s Signal und meldet das Ergebnis
    std::atomic<bool> autoTrimArmed { false }, autoTrimDone { false };
    std::atomic<float> autoTrimResult { 0.0f }, autoTrimProgress { 0.0f };

    // Pegel für die Anzeige
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, gateGr { 0 }, compGr { 0 }, dsGr { 0 }, autoGainDb { 0 };

    // EQ-Kurve für die Anzeige (Betrag in dB, inklusive Filter)
    float eqResponseDb (float freq) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool on (const char* id) const { return p (id) > 0.5f; }
    void updateFilters();

    double sr = 48000.0;
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };

    onno::dsp::Biquad hp1, hp2, lp, lf, lmf, hmf, hf, dsLo1, dsLo2, dsHi1, dsHi2, dcBlock;
    bool hpOn = false, lpOn = false;

    float gateEnv = -120.0f, gateGain = 0.0f; int gateHold = 0;
    float compEnv = 0.0f;
    float dsEnv = 0.0f;
    float autoIn = 0.0f, autoOut = 0.0f, autoG = 1.0f;
    double trimSum = 0.0; long trimCount = 0;

    std::array<juce::ValueTree, 2> slots;
    int slot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CS1Processor)
};
