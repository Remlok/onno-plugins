#pragma once
// Onno Limit: Brickwall-Limiter im Stil von ReaLimit (Lookahead 1,5 ms, keine Übersteuerung über die Ceiling).
// TRUE PEAK: erkennt auch Spitzen zwischen den Samples (8-fach interpoliert), damit nach dem
// Umwandeln (MP3/AAC, D/A-Wandler) nichts über die Ceiling geht.
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/OnnoDSP.h"
#include "Common/Analog.h"

class LimitProcessor : public onno::ModernProcessor
{
public:
    LimitProcessor();
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    std::atomic<float> drivenPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::atomic<float> *pThr, *pCeil, *pRel, *pBypass, *pTp;
    static constexpr int tpTaps = 24, tpPhases = 8, tpDelay = 12;
    float tpH[tpPhases][tpTaps] {};
    float hist[2][tpTaps] {};
    int histPos = 0;
    float truePeak (int ch) const;
    double sr = 48000;
    int L = 72;                               // Fensterlänge in Samples
    std::vector<float> dl[2], box, dqv;
    std::vector<long long> dqi;
    int dlPos = 0, boxPos = 0, dqHead = 0, dqSize = 0;
    long long t = 0;
    double boxSum = 0;
    float env = 1;
    juce::SmoothedValue<float> driveS, ceilS;
    onno::Analog analog;
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
