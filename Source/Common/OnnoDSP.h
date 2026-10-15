#pragma once
// Gemeinsame DSP-Bausteine: RBJ-Biquads (Stereo) und kleine Helfer
#include <cmath>
#include <algorithm>

namespace onno::dsp
{
constexpr double twoPi = 6.283185307179586;

inline float db2a (float d) { return std::exp (d * 0.11512925f); }
inline float a2db (float a) { return 8.685889638f * std::log (std::max (a, 1.0e-9f)); }

// Koeffizient eines Einpol-Glätters für eine Zeitkonstante in ms
inline float coef (float ms, double sr) { return (float) std::exp (-1.0 / (std::max (0.001f, ms) * 0.001 * sr)); }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    float process (float x, int ch)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }

    void peak (double sr, double f, double q, double gdb)
    {
        const double A = std::pow (10.0, gdb / 40.0), w = twoPi * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    void lowShelf (double sr, double f, double q, double gdb)
    {
        const double A = std::pow (10.0, gdb / 40.0), w = twoPi * f / sr, c = std::cos (w);
        const double al = std::sin (w) / (2 * q), sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }

    void highShelf (double sr, double f, double q, double gdb)
    {
        const double A = std::pow (10.0, gdb / 40.0), w = twoPi * f / sr, c = std::cos (w);
        const double al = std::sin (w) / (2 * q), sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }

    void highPass (double sr, double f, double q)
    {
        const double w = twoPi * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void lowPass (double sr, double f, double q)
    {
        const double w = twoPi * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }

    // Bandpass mit 0 dB in der Mitte
    void bandPass (double sr, double f, double q)
    {
        const double w = twoPi * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }

    void notch (double sr, double f, double q)
    {
        const double w = twoPi * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set (1, -2 * c, 1, 1 + al, -2 * c, 1 - al);
    }

    void allPass (double sr, double f, double q)
    {
        const double w = twoPi * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        set (1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
    }

    void identity() { set (1, 0, 0, 1, 0, 0); }

    // Hochpass erster Ordnung (bilinear)
    void highPass1 (double sr, double f)
    {
        const double k = std::tan (twoPi * f / sr / 2);
        set (1, -1, 0, 1 + k, k - 1, 0);
    }

    // Betrag in dB bei Frequenz f (für Anzeigen)
    double magDb (double sr, double f) const
    {
        const double w = twoPi * f / sr, c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return 10 * std::log10 ((nr * nr + ni * ni) / std::max (1e-30, dr * dr + di * di));
    }
};
} // namespace onno::dsp
