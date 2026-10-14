#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Common/OnnoDSP.h"
#include <atomic>
#include <array>

// Onno RV-1: algorithmischer Hall (8-Kanal-Feedback-Delay-Netz) mit Programm-Browser
// Kette (Hallanteil): Pre-Delay → Early Reflections + Diffusion → FDN mit Modulation und Dämpfung
//                     → Gate (Gated-Programme) → Ducking → Low/High Cut → Breite → Mix
class RV1Processor : public juce::AudioProcessor
{
public:
    RV1Processor();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Onno RV-1"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- Algorithmen und Programme ---
    enum Algo { hall, plate, room, chamber, ambience, chorus, gated, numAlgos };
    static const char* const algoNames[numAlgos];
    static constexpr int numCategories = 8;
    static const char* const categoryNames[numCategories];

    struct Program
    {
        const char* name; int category; int algo;
        float predelay, decay, size, depth, movement, character, width, duck, lowCut, highCut;
    };
    static const std::vector<Program>& programs();
    void loadProgram (int index);                      // nur Message-Thread; Mix, 100 % Wet und Bypass bleiben
    int currentProgram() const { return (int) apvts.getRawParameterValue ("program")->load(); }
    static int firstOfCategory (int cat);
    void setParam (const juce::String& id, float value);

    // Anzeige
    std::atomic<float> inPeak { 0 }, wetPeak { 0 }, duckDb { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool on (const char* id) const { return p (id) > 0.5f; }

    struct Line
    {
        std::vector<float> buf; int w = 0;
        void init (int n) { buf.assign ((size_t) n, 0.0f); w = 0; }
        void write (float x) { buf[(size_t) w] = x; if (++w == (int) buf.size()) w = 0; }
        float read (float delay) const   // linear interpoliert, delay in Samples (>= 1)
        {
            const int n = (int) buf.size();
            const int di = (int) delay;
            const float fr = delay - (float) di;
            // Ganzzahlige Indizes, damit Rundung nie über das Pufferende hinaus liest
            int i1 = w - di; if (i1 < 0) i1 += n;          // Verzögerung di
            int i0 = i1 - 1; if (i0 < 0) i0 += n;          // Verzögerung di + 1
            return buf[(size_t) i1] + fr * (buf[(size_t) i0] - buf[(size_t) i1]);
        }
    };

    struct Allpass
    {
        Line line; float len = 100;
        float process (float x, float g)
        {
            const float d = line.read (len);
            const float v = x + g * d;
            line.write (v);
            return d - g * v;
        }
    };

    static constexpr int N = 8;
    double sr = 48000.0;
    Line pre[2];
    std::array<Allpass, 4> diff[2];
    std::array<Line, N> tank;
    std::array<float, N> damp {}, lfoPhase {};
    onno::dsp::Biquad lowCutF, highCutF;
    float duckEnv = -120.0f, gateEnv = -120.0f, gateGain = 0.0f, gateTimer = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RV1Processor)
};
