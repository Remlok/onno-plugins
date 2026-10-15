#pragma once
// Analoge Klangfarbe für die moderne Suite (Rechnung im Klang-Kern Core/AnalogCore.h).
// Diese Hülle verbindet sie mit den Parametern "analog" und "drive" eines Plugins.
#include "Common/ModernProcessor.h"
#include "Core/AnalogCore.h"

namespace onno
{
class Analog : public core::AnalogCore
{
public:
    static const juce::StringArray& names()
    {
        static const juce::StringArray n = [] { juce::StringArray a; for (auto& s : AnalogCore::names()) a.add (s); return a; }();
        return n;
    }

    static void addParams (prm::Layout& p)
    {
        prm::ch (p, "analog", "Analog Model", names(), clean);
        prm::lin (p, "drive", "Analog Drive", 0, 100, 35, "%");
    }

    void attach (const ModernProcessor& proc)
    {
        pModel = proc.raw ("analog");
        pDrive = proc.raw ("drive");
    }

    // einmal pro Block: Modell und Drive übernehmen
    void beginBlock() { AnalogCore::beginBlock (pModel ? pModel->load() : 0.0f, pDrive ? pDrive->load() : 35.0f); }

    void process (float* L, float* R, int n, int nch)
    {
        AnalogCore::process (L, R, n, nch, pModel ? pModel->load() : 0.0f, pDrive ? pDrive->load() : 35.0f);
    }

private:
    std::atomic<float>* pModel = nullptr;
    std::atomic<float>* pDrive = nullptr;
};
} // namespace onno
