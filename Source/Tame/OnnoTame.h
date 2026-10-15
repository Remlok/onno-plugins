#pragma once
// Onno Tame: spektraler Resonanz-Zähmer. Findet Frequenzen, die über die eigene spektrale
// Hüllkurve hinausragen, und drückt sie dynamisch, Bin für Bin (STFT mit 75 % Überlappung).
// Metal-Profile setzen Bereich, Schärfe und Empfindlichkeitskurve passend zur Quelle.
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/Analyzer.h"
#include <juce_dsp/juce_dsp.h>

class TameProcessor : public onno::ModernProcessor
{
public:
    static constexpr int numNodes = 4, numDisplayBands = 160;
    static const juce::StringArray profileNames;

    TameProcessor();
    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    void applyProfile (int p);
    static juce::String nid (int n, const char* p) { return "n" + juce::String (n + 1) + p; }
    // Empfindlichkeit (dB) bei Frequenz f aus den vier Knoten
    float sensitivityAt (float f) const;
    static float displayBandFreq (int b) { return 20.0f * std::pow (1000.0f, (b + 0.5f) / numDisplayBands); }

    onno::Analyzer analyzer;
    std::array<std::atomic<float>, numDisplayBands> grDisplay;   // Reduktion je Anzeigeband (dB, min)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void processFrame();
    void computeGains (const float* powerDb, float* gainDb, std::vector<float>& state);

    std::atomic<float> *pDepth, *pSharp, *pSel, *pAtt, *pRel, *pLow, *pHigh, *pMix, *pOut, *pDelta, *pLink, *pBypass;
    std::array<std::atomic<float>*, numNodes> pNodeF {}, pNodeG {};

    double sr = 48000;
    int N = 1024, hop = 256, order = 10, pos = 0, hopCount = 0, nch = 2;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, inRing[2], outAcc[2], dry[2], work[2], pdb[2], gdb[2], state[2], det[2], sens, prefix;
    void detect (int c, const std::vector<float>& power);
    std::vector<int> lo, hi;
    float lastWidth = -1;
    std::array<float, numDisplayBands> dispMin {};
    juce::SmoothedValue<float> mixS, outS;
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
