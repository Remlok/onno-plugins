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

    // ---- Analog-genaue Filter ("matched", nach Vicanek): Pole wie das analoge Vorbild (impulsinvariant),
    // Betrag exakt bei 0 Hz, Nyquist und der Eckfrequenz. Dadurch keine Stauchung in den Höhen wie beim
    // bilinearen RBJ-Entwurf (Glocke 12 kHz +12 dB: bei 16 kHz +8,6 dB wie analog statt +5,3 dB).
    enum class Kind { peak, lowShelf, highShelf, lowPass };

    // analoger Betrag im Quadrat, s = j f/f0
    static double analogMag2 (Kind k, double f, double f0, double gdb, double q)
    {
        const double A = std::pow (10.0, gdb / 40.0), w = f / f0, w2 = w * w, sa = std::sqrt (A);
        auto quad = [] (double re, double im) { return re * re + im * im; };
        switch (k)
        {
            case Kind::peak:      return quad (1 - w2, w * A / q) / quad (1 - w2, w / (A * q));
            case Kind::lowShelf:  return A * A * quad (A - w2, w * sa / q) / quad (1 - A * w2, w * sa / q);
            case Kind::highShelf: return A * A * quad (1 - A * w2, w * sa / q) / quad (A - w2, w * sa / q);
            case Kind::lowPass:   return 1.0 / quad (1 - w2, w / q);
        }
        return 1.0;
    }

    bool matchedRaw (Kind k, double sr, double f0, double gdb, double q)
    {
        const double pi = twoPi * 0.5, A = std::pow (10.0, gdb / 40.0);
        double fp = f0, qp = q;
        if (k == Kind::peak) qp = A * q;
        else if (k == Kind::lowShelf) fp = f0 / std::sqrt (A);
        else if (k == Kind::highShelf) fp = f0 * std::sqrt (A);
        const double w = twoPi * std::min (fp, 0.499 * sr) / sr, z = 1.0 / (2 * qp);
        const double na1 = z <= 1 ? -2 * std::exp (-z * w) * std::cos (std::sqrt (1 - z * z) * w)
                                  : -2 * std::exp (-z * w) * std::cosh (std::sqrt (z * z - 1) * w);
        const double na2 = std::exp (-2 * z * w);
        const double A0 = (1 + na1 + na2) * (1 + na1 + na2), A1 = (1 - na1 + na2) * (1 - na1 + na2), A2 = -4 * na2;
        const double fm = std::min (f0, 0.45 * sr), wm = twoPi * fm / sr;
        const double p1 = std::sin (wm / 2) * std::sin (wm / 2), p0 = 1 - p1, p2 = 4 * p0 * p1;
        const double B0 = A0 * analogMag2 (k, 1.0e-3 * f0, f0, gdb, q), B1 = A1 * analogMag2 (k, sr * 0.5, f0, gdb, q);
        const double B2 = (analogMag2 (k, fm, f0, gdb, q) * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / p2;
        const double s0 = std::sqrt (B0), s1 = std::sqrt (B1), W = 0.5 * (s0 + s1), disc = W * W + B2;
        if (! (disc >= 0) || ! std::isfinite (disc)) return false;
        const double nb0 = 0.5 * (W + std::sqrt (disc)), nb1 = 0.5 * (s0 - s1), nb2 = -B2 / (4 * nb0);
        if (! std::isfinite (nb2) || nb0 <= 0) return false;
        b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2;
        return true;
    }

    // Kehrwert (Absenkung = umgekehrte Anhebung); Nullstellen sind minimalphasig, daher stabil
    void invert()
    {
        const double ob0 = b0, ob1 = b1, ob2 = b2, oa1 = a1, oa2 = a2;
        b0 = 1 / ob0; b1 = oa1 / ob0; b2 = oa2 / ob0; a1 = ob1 / ob0; a2 = ob2 / ob0;
    }

    // Glocke/Shelf/Tiefpass analog-genau; jeweils die Variante mit den tieferen Polen entwerfen
    // (Glocke und Low-Shelf als Anhebung, High-Shelf als Absenkung) und bei Bedarf umkehren
    void matched (Kind k, double sr, double f0, double q, double gdb = 0)
    {
        const bool flip = (k == Kind::peak || k == Kind::lowShelf) ? gdb < 0 : (k == Kind::highShelf ? gdb > 0 : false);
        const double g = flip ? -gdb : gdb;
        if (matchedRaw (k, sr, f0, g, q)) { if (flip) invert(); return; }
        switch (k)   // Rückfall (sollte nicht vorkommen): bilinear
        {
            case Kind::peak:      peak (sr, f0, q, gdb); break;
            case Kind::lowShelf:  lowShelf (sr, f0, q, gdb); break;
            case Kind::highShelf: highShelf (sr, f0, q, gdb); break;
            case Kind::lowPass:   lowPass (sr, f0, q); break;
        }
    }

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

// ---- Halbband-Allpass-Filter (Polyphasen-IIR nach L. de Soras / HIIR) für 2-fache Über-/Unterabtastung.
// Koeffizienten werden aus Anzahl und Übergangsbreite berechnet (elliptisches Halbband).
template <int NC>
struct HalfbandCoefs
{
    double c[NC] {};
    explicit HalfbandCoefs (double transition)
    {
        double k = std::tan ((1 - 2 * transition) * twoPi / 8); k *= k;
        const double kk = std::pow (1 - k * k, 0.25), e = 0.5 * (1 - kk) / (1 + kk), e4 = e * e * e * e;
        const double q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)));
        const int order = NC * 2 + 1;
        const double pi = twoPi * 0.5;
        for (int idx = 0; idx < NC; ++idx)
        {
            const int cc = idx + 1;
            double num = 0, den = 0, t;
            int i = 0, j = 1;
            do { t = std::pow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * cc * pi / order) * j; num += t; j = -j; ++i; } while (std::abs (t) > 1e-100);
            i = 1; j = -1;
            do { t = std::pow (q, i * i) * std::cos (i * 2 * cc * pi / order) * j; den += t; j = -j; ++i; } while (std::abs (t) > 1e-100);
            const double ww = num * std::pow (q, 0.25) / (den + 0.5), w2 = ww * ww;
            const double x = std::sqrt ((1 - w2 * k) * (1 - w2 / k)) / (1 + w2);
            c[idx] = (1 - x) / (1 + x);
        }
    }
};

template <int NC>
struct Halfband
{
    const double* c = nullptr;
    double x[NC] {}, y[NC] {};
    void reset() { for (int i = 0; i < NC; ++i) x[i] = y[i] = 0; }

    // ein Eingangssample -> zwei Ausgangssamples
    void up (float in, float& o0, float& o1)
    {
        double ev = in, od = in;
        run (ev, od);
        o0 = (float) ev; o1 = (float) od;
    }

    // zwei Eingangssamples -> ein Ausgangssample
    float down (float i0, float i1)
    {
        double ev = i1, od = i0;
        run (ev, od);
        return (float) (0.5 * (ev + od));
    }

private:
    void run (double& ev, double& od)
    {
        for (int i = 0; i < NC; i += 2)
        {
            const double t0 = (ev - y[i]) * c[i] + x[i];
            x[i] = ev; y[i] = t0; ev = t0;
            if (i + 1 < NC)
            {
                const double t1 = (od - y[i + 1]) * c[i + 1] + x[i + 1];
                x[i + 1] = od; y[i + 1] = t1; od = t1;
            }
        }
    }
};

// 4-fache Überabtastung pro Sample: up() liefert 4 Werte, down() nimmt 4 Werte zurück.
// Sperrdämpfung ca. 100 dB, Durchlass flach bis 0,46 fs.
struct Oversampler4
{
    static const HalfbandCoefs<8>& c1() { static const HalfbandCoefs<8> c (0.04); return c; }
    static const HalfbandCoefs<4>& c2() { static const HalfbandCoefs<4> c (0.25); return c; }
    Halfband<8> u1, d1;     // 1x <-> 2x
    Halfband<4> u2, d2;     // 2x <-> 4x (ein Filter, läuft mit doppelter Rate)

    Oversampler4() { u1.c = d1.c = c1().c; u2.c = d2.c = c2().c; }
    void reset() { u1.reset(); d1.reset(); u2.reset(); d2.reset(); }

    void up (float x, float* o)
    {
        float a, b;
        u1.up (x, a, b);
        u2.up (a, o[0], o[1]);
        u2.up (b, o[2], o[3]);
    }

    float down (const float* in)
    {
        const float a = d2.down (in[0], in[1]);
        const float b = d2.down (in[2], in[3]);
        return d1.down (a, b);
    }
};
} // namespace onno::dsp
