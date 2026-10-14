#pragma once
#include "RV1Processor.h"
#include "Common/OnnoLook.h"

// Oberfläche des RV-1: links Programm-Browser mit Display, Mitte die acht Klangregler, rechts Mix und Filter
class RV1Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RV1Editor (RV1Processor&);
    ~RV1Editor() override = default;

    static constexpr float W = 1000.0f, H = 340.0f;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // für Tests
    void tickMeters() { timerCallback(); }
    void setHot (int k) { hot = k; }

private:
    void timerCallback() override;

    juce::RangedAudioParameter* param (const char* id) const { return proc.apvts.getParameter (id); }
    float value (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    bool flag (const char* id) const { return value (id) > 0.5f; }
    void toggle (const char* id) { proc.setParam (id, flag (id) ? 0.0f : 1.0f); repaint(); }
    bool isModern() const { return flag ("view"); }

    juce::Point<float> toBase (juce::Point<float> p) const { return { (p.x - ox) / s, (p.y - oy) / s }; }
    int hitKnob (juce::Point<float> b) const;

    void rebuild (float ps);
    void drawConsole (juce::Graphics&);
    void drawModern (juce::Graphics&);
    void drawKnob (juce::Graphics&, int i);
    void drawFlatKnob (juce::Graphics&, int i);
    void drawScale (juce::Graphics&, int i, float rOuter, juce::Colour c);
    void drawDisplay (juce::Graphics&, juce::Rectangle<float> r, bool modern);
    void drawBtn (juce::Graphics&, juce::Rectangle<float> r, bool on, const juce::String& label, bool modern);

    juce::String formatValue (int i, float v) const;
    juce::String formatScale (int i, float v) const;

    RV1Processor& proc;
    std::vector<onno::Sprite> knobSprites;
    std::array<onno::Sprite, 2> toggleSprites;
    float builtPs = 0.0f;

    float s = 1.0f, ox = 0.0f, oy = 0.0f;
    juce::Point<float> mouseBase { -1000.0f, -1000.0f };
    int hot = -1, drag = -1;
    float dragNorm = 0.0f, lastY = 0.0f;
    float inDisp = -90, wetDisp = -90, duckDisp = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RV1Editor)
};
