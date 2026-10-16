#pragma once
// Onno Limit: Brickwall-Limiter im Stil von ReaLimit (Lookahead 1,5 ms, keine Übersteuerung über die Ceiling).
// TRUE PEAK: erkennt auch Spitzen zwischen den Samples (8-fach interpoliert), damit nach dem
// Umwandeln (MP3/AAC, D/A-Wandler) nichts über die Ceiling geht.
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/OnnoDSP.h"
#include "Common/Analog.h"
#include "Core/LimitCore.h"

class LimitProcessor : public onno::ModernProcessor
{
public:
    LimitProcessor();
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    std::vector<FactoryPreset> factoryPresets() const override { return fromCore (core.factoryPresets()); }

    std::atomic<float> drivenPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    onno::core::LimitCore core;
    onno::CoreLink link;
    int reportedLatency = -1;
};

class LimitEditor : public onno::ModernEditor
{
public:
    explicit LimitEditor (LimitProcessor&);

private:
    void drawBackground (juce::Graphics&) override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    void tick() override;

    LimitProcessor& proc;
    onno::History<300> inH, outH, grH { 0.0f };
    float inD = -90, outD = -90, grD = 0;
};
