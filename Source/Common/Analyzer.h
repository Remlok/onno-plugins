#pragma once
// Spektrum-Analyzer: der Audio-Thread schreibt lockfrei in einen Ring, die GUI rechnet die FFT.
#include <juce_dsp/juce_dsp.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <atomic>

namespace onno
{
class Analyzer
{
public:
    static constexpr int order = 12, fftSize = 1 << order, ringSize = fftSize * 2;

    Analyzer()
    {
        for (auto& s : ring) s.store (0.0f, std::memory_order_relaxed);
        spec.fill (-120.0f);
    }

    void prepare (double sr) { sampleRate.store ((float) sr); }

    // Audio-Thread: Mono-Summe einspeisen
    void push (const float* l, const float* r, int n) noexcept
    {
        int w = wpos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) w].store (r != nullptr ? 0.5f * (l[i] + r[i]) : l[i], std::memory_order_relaxed);
            w = (w + 1) & (ringSize - 1);
        }
        wpos.store (w, std::memory_order_release);
    }

    // GUI-Thread: neues Spektrum rechnen (fällt mit fallDb pro Aufruf)
    void update (float fallDb = 1.2f)
    {
        const int w = wpos.load (std::memory_order_acquire);
        for (int i = 0; i < fftSize; ++i)
            work[(size_t) i] = ring[(size_t) ((w - fftSize + i) & (ringSize - 1))].load (std::memory_order_relaxed);
        std::fill (work.begin() + fftSize, work.end(), 0.0f);
        window.multiplyWithWindowingTable (work.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (work.data());
        const float sr = sampleRate.load();
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const float f = (float) k * sr / (float) fftSize;
            // +3 dB/Oktave Neigung um 1 kHz, damit ein Mix optisch flach liegt
            float d = 20.0f * std::log10 (std::max (1.0e-9f, work[(size_t) k] * 4.0f / (float) fftSize))
                    + 3.0f * std::log2 (std::max (f, 1.0f) / 1000.0f);
            auto& s = spec[(size_t) k];
            s = d > s ? d : std::max (d, s - fallDb);
        }
    }

    // Pegel (dB) bei Frequenz f, Maximum über die Bins zwischen f0 und f1
    float level (float f0, float f1) const
    {
        const float sr = sampleRate.load();
        // schmaler als ein Bin: zwischen den Nachbar-Bins interpolieren
        if ((f1 - f0) * fftSize / sr < 1.0f)
        {
            const float kb = juce::jlimit (1.0f, fftSize / 2 - 2.0f, 0.5f * (f0 + f1) * fftSize / sr);
            const int k = (int) kb; const float fr = kb - (float) k;
            return spec[(size_t) k] + fr * (spec[(size_t) k + 1] - spec[(size_t) k]);
        }
        int k0 = (int) std::floor (f0 * fftSize / sr), k1 = (int) std::ceil (f1 * fftSize / sr);
        k0 = juce::jlimit (1, fftSize / 2 - 1, k0);
        k1 = juce::jlimit (k0, fftSize / 2 - 1, k1);
        float m = -120.0f;
        for (int k = k0; k <= k1; ++k) m = std::max (m, spec[(size_t) k]);
        return m;
    }

    // Kurve über das Rechteck; xToFreq bildet x auf Hz ab, dbToY dB auf y
    template <typename XF, typename YF>
    juce::Path path (juce::Rectangle<float> r, XF xToFreq, YF dbToY, bool closed) const
    {
        juce::Path p;
        const float step = 2.0f;
        bool first = true;
        for (float x = r.getX(); x <= r.getRight() + 0.01f; x += step)
        {
            const float y = juce::jlimit (r.getY(), r.getBottom(), dbToY (level (xToFreq (x - step * 0.5f), xToFreq (x + step * 0.5f))));
            if (first) { if (closed) { p.startNewSubPath (x, r.getBottom()); p.lineTo (x, y); } else p.startNewSubPath (x, y); first = false; }
            else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (r.getRight(), r.getBottom()); p.closeSubPath(); }
        return p;
    }

private:
    std::array<std::atomic<float>, ringSize> ring;
    std::atomic<int> wpos { 0 };
    std::atomic<float> sampleRate { 48000.0f };
    juce::dsp::FFT fft { order };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, fftSize * 2> work {};
    std::array<float, fftSize / 2> spec {};
};
} // namespace onno
