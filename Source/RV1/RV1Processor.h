#pragma once
#include "Common/ModernProcessor.h"
#include "Common/OnnoDSP.h"
#include "Core/RV1Core.h"
#include <atomic>
#include <array>

// Onno RV-1: algorithmischer Hall (8-Kanal-Feedback-Delay-Netz) mit Programm-Browser
// Der Klang steckt im Kern (Core/RV1Core.h, gleich im Browser); hier nur Parameter, Programme und Anzeige.
class RV1Processor : public onno::ModernProcessor
{
public:
    using Core = onno::core::RV1Core;
    RV1Processor();

    juce::AudioProcessorEditor* createEditor() override;

    // --- Algorithmen und Programme (aus dem Kern) ---
    enum Algo { hall = Core::hall, plate = Core::plate, room = Core::room, chamber = Core::chamber,
                ambience = Core::ambience, chorus = Core::chorus, gated = Core::gated, numAlgos = Core::numAlgos };
    static constexpr const char* const (&algoNames)[numAlgos] = Core::algoNames;
    static constexpr int numCategories = Core::numCategories;
    static constexpr const char* const (&categoryNames)[numCategories] = Core::categoryNames;

    using Program = Core::Program;
    static const std::vector<Program>& programs() { return Core::programs(); }
    // Programme sind die Werkspresets 1..32 der gemeinsamen Preset-Liste; Mix, 100 % Wet und Freeze bleiben stehen
    std::vector<FactoryPreset> factoryPresets() const override { return fromCore (core.factoryPresets()); }
    bool keepOnPreset (const juce::String& id) const override { return id == "mix" || id == "send" || id == "freeze"; }
    void loadProgram (int index);                      // nur Message-Thread
    int currentProgram() const { return (int) apvts.getRawParameterValue ("program")->load(); }
    // Index des geladenen Werkspresets (0..31) oder -1 bei Init/eigenen Presets
    int factoryProgram() const { return Core::programIndex (presetName.toStdString()); }
    static int firstOfCategory (int cat) { return Core::firstOfCategory (cat); }

    // Anzeige (Eingang: inPeak der Basis)
    std::atomic<float> wetPeak { 0 }, duckDb { 0 };

protected:
    void prepare (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void migrateState (juce::ValueTree&) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    Core core;
    onno::CoreLink link;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RV1Processor)
};
