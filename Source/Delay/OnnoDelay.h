#pragma once
// Onno Delay: Multi-Tap-Delay im Stil von ReaDelay (3 Taps, Sync, Ping-Pong, Filter im Feedback, Ducking)
#include "Common/ModernProcessor.h"
#include "Common/ModernEditor.h"
#include "Common/OnnoDSP.h"
#include "Common/Analog.h"
#include "Core/DelayCore.h"

class DelayProcessor : public onno::ModernProcessor
{
public:
    static constexpr int numTaps = onno::core::DelayCore::numTaps;
    static constexpr float maxMs = onno::core::DelayCore::maxMs;
    static const juce::StringArray divNames;
    static const float divBeats[];

    DelayProcessor();
    void prepare (double sampleRate, int) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    std::vector<FactoryPreset> factoryPresets() const override { return fromCore (core.factoryPresets()); }

    static juce::String id (int t, const char* p) { return "t" + juce::String (t + 1) + p; }
    // Verzögerung eines Taps in ms (Sync über das Tempo)
    float tapMs (int t, double bpm) const;

    std::atomic<float> bpm { 120.0f }, duckGain { 1.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    onno::core::DelayCore core;
    onno::CoreLink link;
    // für tapMs() im GUI-Thread: Sync, Notenwert, Zeit je Tap
    std::array<std::array<std::atomic<float>*, 3>, numTaps> tapP {};
};

class DelayEditor : public onno::ModernEditor
{
public:
    explicit DelayEditor (DelayProcessor&);

private:
    void selectTap (int t);
    void drawBackground (juce::Graphics&) override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    void drawOverlay (juce::Graphics&) override;
    void displayMove (juce::Point<float>) override;
    bool displayDown (juce::Point<float>, const juce::MouseEvent&) override;
    void displayDrag (juce::Point<float>, const juce::MouseEvent&) override;
    void displayUp (juce::Point<float>) override;
    void tick() override;

    float spanMs() const;
    float msToX (float ms) const;
    float xToMs (float x) const;
    float levelToY (float db) const;
    juce::Point<float> headPos (int t) const;
    int hitHead (juce::Point<float>) const;

    DelayProcessor& proc;
    int sel = 0, hoverTap = -1, dragTap = -1;
    bool stripReady = false;
    bool selSync = false;
    float span = 2000;
};
