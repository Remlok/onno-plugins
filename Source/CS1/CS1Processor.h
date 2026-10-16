#pragma once
#include "Common/ModernProcessor.h"
#include "Common/OnnoDSP.h"
#include "Core/CS1Core.h"
#include <atomic>

// Onno CS-1: Channel Strip
// Kette: Trim → Drive (2x Oversampling) → Hoch-/Tiefpass → Gate → EQ ⇄ Kompressor → De-Esser → Output (+ Auto-Gain)
// Der Klang steckt im Kern (Core/CS1Core.h, auch im Browser); hier bleiben Auto-Trim und das Preset-Verhalten.
// Workflow: Quell-Presets mit einem Klick, Auto-Trim auf -18 dBFS RMS, Auto-Gain; A/B, Rückgängig und MATCH aus der Suite.
class CS1Processor : public onno::ModernProcessor
{
public:
    CS1Processor();

    juce::AudioProcessorEditor* createEditor() override;

    // --- Workflow (nur vom Message-Thread aufrufen) ---
    // Quell-Presets: Werkspresets 1..10 der gemeinsamen Preset-Liste
    static constexpr int numPresets = onno::core::CS1Core::numPresets;
    static constexpr const char* const* presetNames = onno::core::CS1Core::presetNames;
    std::vector<FactoryPreset> factoryPresets() const override { return fromCore (core.factoryPresets()); }
    bool keepOnPreset (const juce::String& id) const override { return id == "trim" || id == "out" || id == "autoGain"; }
    void applyPreset (int index) { if (index >= 0 && index < numPresets) loadPreset (1 + index); }
    int getSlot() const { return slot(); }

    // Auto-Trim: GUI setzt armed, Audio-Thread misst 3 s Signal und meldet das Ergebnis
    std::atomic<bool> autoTrimArmed { false }, autoTrimDone { false };
    std::atomic<float> autoTrimResult { 0.0f }, autoTrimProgress { 0.0f };

    // Pegel für die Anzeige (Ein-/Ausgang: inPeak/outPeak der Basis)
    std::atomic<float> gateGr { 0 }, compGr { 0 }, dsGr { 0 }, autoGainDb { 0 };

    // EQ-Kurve für die Anzeige (Betrag in dB, inklusive Filter)
    float eqResponseDb (float freq) const;

protected:
    void prepare (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void migrateState (juce::ValueTree&) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    double sr = 48000.0;
    onno::core::CS1Core core;
    onno::CoreLink link;
    int reportedLatency = -1;
    double trimSum = 0.0; long trimCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CS1Processor)
};
