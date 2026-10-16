#pragma once
// Onno Gate: Noise-Gate im Stil von ReaGate (Hysterese, Hold, Range, Lookahead, Sidechain mit Filtern)
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/OnnoDSP.h"
#include "Common/Analog.h"
#include "Core/GateCore.h"

class GateProcessor : public onno::ModernProcessor
{
public:
    GateProcessor();
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    std::vector<FactoryPreset> factoryPresets() const override { return fromCore (core.factoryPresets()); }

    std::atomic<float> detPeak { 0.0f }, openPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    onno::core::GateCore core;
    onno::CoreLink link;
    int reportedLatency = -1;
};

class GateEditor : public onno::ModernEditor
{
public:
    explicit GateEditor (GateProcessor&);

private:
    void drawBackground (juce::Graphics&) override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    bool displayDown (juce::Point<float>, const juce::MouseEvent&) override;
    void displayDrag (juce::Point<float>, const juce::MouseEvent&) override;
    void displayUp (juce::Point<float>) override;
    void tick() override;

    GateProcessor& proc;
    onno::History<300> detH, outH, openH { 0.0f };
    bool dragHyst = false;
};
