#pragma once
#include "CS1Processor.h"
#include "Common/ModernEditor.h"

// Oberfläche des CS-1: Kopfzeile der Suite, darunter die Module nebeneinander wie ein Konsolenkanal
// und die Workflow-Leiste (Anzeige, Quell-Presets). Ansicht Konsole oder Modern.
class CS1Editor : public onno::ModernEditor
{
public:
    explicit CS1Editor (CS1Processor&);

    static constexpr float W0 = 1100.0f, H0 = 426.0f;
    static constexpr float top = 46.0f;      // Raster des Kanals beginnt unter der Kopfzeile

private:
    void syncView() override;
    void tick() override;
    void drawBackground (juce::Graphics&) override;
    void drawOverlay (juce::Graphics&) override;
    void drawKnob (juce::Graphics&, int i) override;

    void buildConsole();
    void buildModern();
    void addWorkflowButtons (bool modern);
    void rebuildSprites (float ps);
    void drawConsole (juce::Graphics&);
    void drawModern (juce::Graphics&);
    void drawConsoleKnob (juce::Graphics&, int i);
    void drawScale (juce::Graphics&, int i, float rOuter, juce::Colour c);
    void drawLadder (juce::Graphics&, float x, float y, float h, float db, bool isGr, bool modern);
    void drawReadout (juce::Graphics&, juce::Rectangle<float> r, bool modern);
    void drawEqCurve (juce::Graphics&, juce::Rectangle<float> r);

    juce::String formatValue (int i, float v) const;
    juce::String formatScale (int i, float v) const;

    CS1Processor& proc;
    int builtView = -1;
    std::vector<onno::Sprite> knobSprites;
    std::array<onno::Sprite, 2> toggleSprites;
    float builtPs = 0.0f;

    float inDisp = -90, outDisp = -90, gateDisp = 0, compDisp = 0, dsDisp = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CS1Editor)
};
