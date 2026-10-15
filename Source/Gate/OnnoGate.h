#pragma once
// Onno Gate: Noise-Gate im Stil von ReaGate (Hysterese, Hold, Range, Lookahead, Sidechain mit Filtern)
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/OnnoDSP.h"

class GateProcessor : public onno::ModernProcessor
{
public:
    GateProcessor();
    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    std::atomic<float> detPeak { 0.0f }, openPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::atomic<float> *pThr, *pHyst, *pAtt, *pHold, *pRel, *pRange, *pLa, *pHpf, *pLpf, *pSrc, *pListen, *pBypass;
    double sr = 48000;
    std::vector<float> dl[2];
    int dlPos = 0, la = -1, holdCnt = 0;
    float det = 0, env = 0, hpfF = -1, lpfF = -1;
    bool open = false;
    onno::dsp::Biquad hpf, lpf;
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
