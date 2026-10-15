#pragma once
// Onno EQ: 8-Band-Equalizer im Stil von ReaEQ mit Kurve zum Anfassen und Analyzer
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/Analyzer.h"
#include "Common/Analog.h"
#include "Common/OnnoDSP.h"
#include "Core/EQCore.h"

class EQProcessor : public onno::ModernProcessor
{
public:
    using Core = onno::core::EQCore;
    static constexpr int numBands = Core::numBands;
    enum Type { lowCut = Core::lowCut, lowShelf, bell, notch, highShelf, highCut, bandPass };
    static const juce::StringArray typeNames, typeShort;
    using Band = Core::Band;

    EQProcessor();
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::String id (int b, const char* p) { return juce::String (Core::id (b, p)); }
    Band band (int b) const;
    static bool hasGain (int type) { return Core::hasGain (type); }
    static bool isCut (int type) { return Core::isCut (type); }
    // Filterstufen eines Bandes entwerfen (Rückgabe: Anzahl Stufen)
    static int design (const Band& b, double sr, onno::dsp::Biquad* st) { return Core::design (b, sr, st); }

    onno::Analyzer analyzer;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    enum { pOn, pType, pFreq, pGain, pQ, pSlope };
    std::array<std::array<std::atomic<float>*, 6>, numBands> bp {};
    std::atomic<float> *pAnalyzer;
    Core core;
    onno::CoreLink link;
};

class EQEditor : public onno::ModernEditor
{
public:
    explicit EQEditor (EQProcessor&);

private:
    void selectBand (int b);
    void drawBackground (juce::Graphics&) override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    void drawOverlay (juce::Graphics&) override;
    void displayMove (juce::Point<float>) override;
    bool displayDown (juce::Point<float>, const juce::MouseEvent&) override;
    void displayDrag (juce::Point<float>, const juce::MouseEvent&) override;
    void displayUp (juce::Point<float>) override;
    void displayDoubleClick (juce::Point<float>) override;
    void displayWheel (juce::Point<float>, float, const juce::MouseEvent&) override;
    void tick() override;

    juce::Point<float> nodePos (int b) const;
    int hitNode (juce::Point<float>) const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    double rate() const;

    EQProcessor& proc;
    int sel = 2, hoverNode = -1, dragNode = -1;
    bool stripReady = false;
    static constexpr float range = 18.0f;
};
