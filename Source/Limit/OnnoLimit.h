#pragma once
// Onno Limit: Brickwall-Limiter im Stil von ReaLimit (Lookahead 1,5 ms, keine Übersteuerung über die Ceiling)
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/OnnoDSP.h"

class LimitProcessor : public onno::ModernProcessor
{
public:
    LimitProcessor();
    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    std::atomic<float> drivenPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::atomic<float> *pThr, *pCeil, *pRel, *pBypass;
    double sr = 48000;
    int L = 72;                               // Fensterlänge in Samples
    std::vector<float> dl[2], box, dqv;
    std::vector<long long> dqi;
    int dlPos = 0, boxPos = 0, dqHead = 0, dqSize = 0;
    long long t = 0;
    double boxSum = 0;
    float env = 1;
    juce::SmoothedValue<float> driveS, ceilS;
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
