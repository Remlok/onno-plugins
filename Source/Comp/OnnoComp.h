#pragma once
// Onno Comp: Kompressor im Stil von ReaComp (Knee, Lookahead, RMS, Sidechain mit Filter, Parallel-Mix)
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/OnnoDSP.h"
#include "Common/Analog.h"
#include "Core/CompCore.h"

class CompProcessor : public onno::ModernProcessor
{
public:
    CompProcessor();
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    std::vector<FactoryPreset> factoryPresets() const override { return fromCore (core.factoryPresets()); }

    // höchster Detektorpegel im Block (dB), für den Punkt auf der Kennlinie
    std::atomic<float> detPeak { 0.0f };
    float autoMakeup() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    onno::core::CompCore core;
    onno::CoreLink link;
    std::atomic<float> *pThr, *pRatio, *pKnee;
    int reportedLatency = -1;
};

class CompEditor : public onno::ModernEditor
{
public:
    explicit CompEditor (CompProcessor&);

private:
    void drawBackground (juce::Graphics&) override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    bool displayDown (juce::Point<float>, const juce::MouseEvent&) override;
    void displayDrag (juce::Point<float>, const juce::MouseEvent&) override;
    void displayUp (juce::Point<float>) override;
    void displayWheel (juce::Point<float>, float, const juce::MouseEvent&) override;
    void tick() override;

    CompProcessor& proc;
    onno::History<300> inH, outH, grH { 0.0f };
    float det = -90, gr = 0;
};
