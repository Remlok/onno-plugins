#pragma once
#include "RV1Processor.h"
#include "Common/ModernEditor.h"

// Oberfläche des RV-1: Kopfzeile der Suite, darunter links der Programm-Browser mit Display,
// in der Mitte die acht Klangregler, rechts Mix und Filter. Ansicht Konsole oder Modern.
class RV1Editor : public onno::ModernEditor
{
public:
    explicit RV1Editor (RV1Processor&);

    static constexpr float W0 = 1000.0f, H0 = 386.0f;
    static constexpr float top = 46.0f;

private:
    void syncView() override;
    void tick() override;
    void drawBackground (juce::Graphics&) override;
    void drawKnob (juce::Graphics&, int i) override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    bool displayDown (juce::Point<float>, const juce::MouseEvent&) override;
    void displayWheel (juce::Point<float>, float, const juce::MouseEvent&) override;

    void buildConsole();
    void buildModern();
    void addBrowserButtons (bool modern);
    void stepProgram (int dir);
    int category() const;
    void rebuildSprites (float ps);
    void drawConsole (juce::Graphics&);
    void drawModern (juce::Graphics&);
    void drawConsoleKnob (juce::Graphics&, int i);
    void drawScale (juce::Graphics&, int i, float rOuter, juce::Colour c);
    void drawProgram (juce::Graphics&, juce::Rectangle<float> r, bool modern);

    juce::String formatValue (int i, float v) const;
    juce::String formatScale (int i, float v) const;

    RV1Processor& proc;
    int builtView = -1;
    std::vector<onno::Sprite> knobSprites;
    std::array<onno::Sprite, 2> toggleSprites;
    float builtPs = 0.0f;
    float inDisp = -90, wetDisp = -90, duckDisp = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RV1Editor)
};
