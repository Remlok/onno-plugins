#pragma once
// Onno Tame: spektraler Resonanz-Zähmer. Findet Frequenzen, die über die eigene spektrale
// Hüllkurve hinausragen, und drückt sie dynamisch, Bin für Bin (STFT mit 75 % Überlappung).
// Metal-Profile setzen Bereich, Schärfe und Empfindlichkeitskurve passend zur Quelle.
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/Analyzer.h"
#include "Core/TameCore.h"

class TameProcessor : public onno::ModernProcessor
{
public:
    static constexpr int numNodes = onno::core::TameCore::numNodes, numDisplayBands = onno::core::TameCore::numDisplayBands;
    static const juce::StringArray profileNames;

    TameProcessor();
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    // Werkspresets = Profile (wie die Profil-Knöpfe); Mix, Output, Delta und Link bleiben dabei stehen
    std::vector<FactoryPreset> factoryPresets() const override { return fromCore (core.factoryPresets()); }
    bool keepOnPreset (const juce::String& id) const override { return id == "mix" || id == "output" || id == "delta" || id == "link"; }

    void applyProfile (int p);
    static juce::String nid (int n, const char* p) { return "n" + juce::String (n + 1) + p; }
    // Empfindlichkeit (dB) bei Frequenz f aus den vier Knoten
    float sensitivityAt (float f) const;
    static float displayBandFreq (int b) { return onno::core::TameCore::displayBandFreq (b); }

    onno::Analyzer analyzer;
    std::array<std::atomic<float>, numDisplayBands> grDisplay;   // Reduktion je Anzeigeband (dB, min)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    onno::core::TameCore core;
    onno::CoreLink link;
    std::array<std::atomic<float>*, numNodes> pNodeF {}, pNodeG {};
    int reportedLatency = -1;
};

class TameEditor : public onno::ModernEditor
{
public:
    explicit TameEditor (TameProcessor&);

private:
    void drawBackground (juce::Graphics&) override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    void drawOverlay (juce::Graphics&) override;
    void displayMove (juce::Point<float>) override;
    bool displayDown (juce::Point<float>, const juce::MouseEvent&) override;
    void displayDrag (juce::Point<float>, const juce::MouseEvent&) override;
    void displayUp (juce::Point<float>) override;
    void displayDoubleClick (juce::Point<float>) override;
    void tick() override;

    // Ziehbare Objekte: 0..3 Knoten, 4 = untere, 5 = obere Bereichsgrenze
    int hitObject (juce::Point<float>) const;
    juce::Point<float> nodePos (int n) const;
    float sensToY (float db) const;

    TameProcessor& proc;
    int hoverObj = -1, dragObj = -1;
    std::array<float, TameProcessor::numDisplayBands> gr {};
};
