#pragma once
// Onno EQ: 8-Band-Equalizer im Stil von ReaEQ mit Kurve zum Anfassen und Analyzer
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/Analyzer.h"
#include "Common/Analog.h"
#include "Common/OnnoDSP.h"

class EQProcessor : public onno::ModernProcessor
{
public:
    static constexpr int numBands = 8;
    enum Type { lowCut = 0, lowShelf, bell, notch, highShelf, highCut, bandPass };
    static const juce::StringArray typeNames, typeShort;

    struct Band
    {
        bool on = false; int type = bell; float freq = 1000, gain = 0, q = 1; int slope = 0;
        bool operator!= (const Band& o) const { return on != o.on || type != o.type || freq != o.freq || gain != o.gain || q != o.q || slope != o.slope; }
    };

    EQProcessor();
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::String id (int b, const char* p) { return "b" + juce::String (b + 1) + p; }
    Band band (int b) const;
    static bool hasGain (int type) { return type == lowShelf || type == bell || type == highShelf; }
    static bool isCut (int type) { return type == lowCut || type == highCut; }
    // Filterstufen eines Bandes entwerfen (Rückgabe: Anzahl Stufen)
    static int design (const Band&, double sr, onno::dsp::Biquad* st);

    onno::Analyzer analyzer;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    enum { pOn, pType, pFreq, pGain, pQ, pSlope };
    std::array<std::array<std::atomic<float>*, 6>, numBands> bp {};
    std::atomic<float> *pOutput, *pAnalyzer, *pBypass;
    std::array<std::array<onno::dsp::Biquad, 4>, numBands> filt;
    std::array<int, numBands> stages {};
    std::array<Band, numBands> cur;
    double sr = 48000;
    juce::SmoothedValue<float> outGain;
    onno::Analog analog;
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
