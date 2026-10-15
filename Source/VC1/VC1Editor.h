#pragma once
#include "VC1Processor.h"
#include "Common/ModernEditor.h"

// Oberfläche des VC-1: Kopf- und Fußzeile der Suite, dazwischen wahlweise
// die Konsole (Rack-Gerät mit VU) oder die moderne Ansicht (Kennlinie und Verlauf)
class VC1Editor : public onno::ModernEditor
{
public:
    explicit VC1Editor (VC1Processor&);

    static constexpr float W0 = 800.0f, H0 = 396.0f;
    static constexpr float top = 46.0f;      // Konsole: das Gerät beginnt unter der Kopfzeile

private:
    void syncView() override;
    void tick() override;
    void drawBackground (juce::Graphics&) override;
    void drawKnob (juce::Graphics&, int i) override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    bool displayDown (juce::Point<float>, const juce::MouseEvent&) override;
    void displayDrag (juce::Point<float>, const juce::MouseEvent&) override;
    void displayUp (juce::Point<float>) override;
    void displayWheel (juce::Point<float>, float, const juce::MouseEvent&) override;

    void buildConsole();
    void buildModern();
    void rebuildSprites (float ps);
    void drawConsole (juce::Graphics&);
    void drawConsoleKnob (juce::Graphics&, int i);
    juce::String formatValue (int i, float v) const;
    juce::String formatScale (int i, float v) const;
    float knobValue (int i) const { return value (VC1Processor::knobIds[i]); }

    VC1Processor& proc;
    int builtView = -1;
    std::array<onno::Sprite, VC1Processor::numKnobs> knobSprites;
    std::array<onno::Sprite, 2> toggleSprites;
    float builtPs = 0.0f;

    float inDisp = -90.0f, outDisp = -90.0f, grDisp = 0.0f, needle = 0.0f;
    onno::History<300> inH, outH, grH { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VC1Editor)
};
