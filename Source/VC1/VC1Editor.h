#pragma once
#include "VC1Processor.h"
#include "Common/OnnoLook.h"

// Oberfläche des VC-1 mit zwei Ansichten: Konsole (Rack-Look) und Modern (Studio-Look)
class VC1Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VC1Editor (VC1Processor&);
    ~VC1Editor() override = default;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // für Tests: Anzeigewerte direkt setzen
    void tickMeters() { timerCallback(); }
    void setHot (int k) { hot = k; }

private:
    void timerCallback() override;

    juce::RangedAudioParameter* knobParam (int i) const;
    juce::RangedAudioParameter* param (const char* id) const;
    float knobValue (int i) const;
    float paramValue (const char* id) const;
    void setChoice (const char* id, int index);
    bool isModern() const { return paramValue ("view") > 0.5f; }

    juce::Point<float> toBase (juce::Point<float> p) const;
    int hitKnob (juce::Point<float> b) const;

    void rebuild (float ps);
    void drawConsole (juce::Graphics&);
    void drawModern (juce::Graphics&);
    void drawKnob (juce::Graphics&, int i);
    void drawModernKnob (juce::Graphics&, int i);
    void drawMeterBar (juce::Graphics&, float x, const juce::String& label, float db, bool isGr);

    juce::String formatValue (int i, float v) const;
    juce::String formatScale (int i, float v) const;

    VC1Processor& proc;

    std::array<onno::Sprite, VC1Processor::numKnobs> knobSprites;
    std::array<onno::Sprite, 2> toggleSprites;
    float builtPs = 0.0f;

    float s = 1.0f, ox = 0.0f, oy = 0.0f;
    juce::Point<float> mouseBase { -1000.0f, -1000.0f };
    int hot = -1, drag = -1;
    float dragNorm = 0.0f, lastY = 0.0f;

    float inDisp = -90.0f, outDisp = -90.0f, grDisp = 0.0f, needle = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VC1Editor)
};
