#pragma once
// Onno MultiComp: 4-Band-Kompressor im Stil von ReaXcomp (Linkwitz-Riley-Weichen, Solo, Analyzer)
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/Analyzer.h"
#include "Common/OnnoDSP.h"
#include "Common/Analog.h"
#include "Core/MultiCompCore.h"

class MultiCompProcessor : public onno::ModernProcessor
{
public:
    static constexpr int numBands = onno::core::MultiCompCore::numBands;
    static const char* const bandNames[numBands];

    MultiCompProcessor();
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    std::vector<FactoryPreset> factoryPresets() const override { return fromCore (core.factoryPresets()); }

    static juce::String id (int b, const char* p) { return "b" + juce::String (b + 1) + p; }
    // Weichenfrequenzen mit Mindestabstand
    void crossovers (float& x1, float& x2, float& x3) const;

    std::array<std::atomic<float>, numBands> bandGr;
    onno::Analyzer analyzer;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::atomic<float> *pX1, *pX2, *pX3;   // nur für crossovers() (GUI-Thread)
    std::atomic<double> sr { 48000.0 };
    onno::core::MultiCompCore core;
    onno::CoreLink link;
};

class MultiCompEditor : public onno::ModernEditor
{
public:
    explicit MultiCompEditor (MultiCompProcessor&);

private:
    void selectBand (int b);
    void drawBackground (juce::Graphics&) override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    void drawOverlay (juce::Graphics&) override;
    void displayMove (juce::Point<float>) override;
    bool displayDown (juce::Point<float>, const juce::MouseEvent&) override;
    void displayDrag (juce::Point<float>, const juce::MouseEvent&) override;
    void displayUp (juce::Point<float>) override;
    void tick() override;

    int hitXover (juce::Point<float>) const;
    float xoverX (int k) const;

    MultiCompProcessor& proc;
    int sel = 0, hoverX = -1, dragX = -1;
    bool stripReady = false;
    std::array<float, MultiCompProcessor::numBands> gr {};
};
