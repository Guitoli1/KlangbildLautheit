#pragma once

// Klangbild Lautheit – Lautheitsmessung nach ITU-R BS.1770-4 / EBU R128 / EBU Tech 3341 + 3342
//   K-Filter -> mittlere Leistung in 100-ms-Abschnitten
//   Momentan  = 400 ms, Kurzzeit = 3 s (gleitend, alle 100 ms neu)
//   Integriert: 400-ms-Blöcke (75 % Überlappung), Gate absolut −70 LUFS, relativ −10 LU
//   LRA:        Kurzzeitwerte, Gate absolut −70 LUFS, relativ −20 LU, Spanne 10.–95. Perzentil

#include <cmath>
#include <array>
#include <vector>
#include <algorithm>

namespace loudness
{

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

// K-Bewertung für beliebige Abtastraten (Herleitung wie libebur128)
inline void makeKFilter (double sr, Biquad& shelf, Biquad& hp)
{
    constexpr double pi = 3.14159265358979323846;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
        hp.a1 = 2.0 * (K * K - 1.0) / a0;
        hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
}

inline double energyToLufs (double e) { return e > 0.0 ? -0.691 + 10.0 * std::log10 (e) : -200.0; }
inline double lufsToEnergy (double l) { return std::pow (10.0, (l + 0.691) / 10.0); }

// Histogramm mit 0,1-LU-Fächern: speichert Anzahl und Energie -> exaktes Gating ohne alle Blöcke aufzuheben
class Histogram
{
public:
    static constexpr double lo = -70.0, hi = 10.0, step = 0.1;
    static constexpr int bins = (int) ((hi - lo) / step) + 1;

    void reset() { count.fill (0); energy.fill (0.0); total = 0; }

    void add (double lufs)
    {
        if (lufs < lo) return;                                   // absolutes Gate
        const int b = std::clamp ((int) ((lufs - lo) / step), 0, bins - 1);
        ++count[(size_t) b]; energy[(size_t) b] += lufsToEnergy (lufs); ++total;
    }

    long long size() const { return total; }

    // relatives Gate: Mittelwert aller Blöcke über −70, dann `relGate` LU darunter
    double relativeThreshold (double relGate) const
    {
        double e = 0; long long n = 0;
        for (int b = 0; b < bins; ++b) { e += energy[(size_t) b]; n += count[(size_t) b]; }
        return n > 0 ? energyToLufs (e / (double) n) + relGate : -200.0;
    }

    double gatedMean (double threshold) const
    {
        double e = 0; long long n = 0;
        for (int b = binFor (threshold); b < bins; ++b) { e += energy[(size_t) b]; n += count[(size_t) b]; }
        return n > 0 ? energyToLufs (e / (double) n) : -200.0;
    }

    // Perzentil der Werte oberhalb der Schwelle (Fach-Mitte)
    double percentile (double threshold, double p) const
    {
        const int b0 = binFor (threshold);
        long long n = 0; for (int b = b0; b < bins; ++b) n += count[(size_t) b];
        if (n == 0) return -200.0;
        const long long target = (long long) std::ceil (p * (double) n);
        long long acc = 0;
        for (int b = b0; b < bins; ++b)
        {
            acc += count[(size_t) b];
            if (acc >= std::max (1LL, target)) return lo + (b + 0.5) * step;
        }
        return hi;
    }

private:
    static int binFor (double lufs)
    {
        // Werte genau auf der Schwelle zählen mit (">" laut Norm, Fachbreite 0,1 LU)
        return std::clamp ((int) std::ceil ((lufs - lo) / step - 1e-9), 0, bins);
    }
    std::array<long long, bins> count {};
    std::array<double, bins> energy {};
    long long total = 0;
};

class Meter
{
public:
    static constexpr int historyLen = 600;   // Kurzzeit-Verlauf: 60 s in 100-ms-Schritten

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = std::clamp (numChannels, 1, 2);
        for (int c = 0; c < 2; ++c) makeKFilter (sr, shelf[(size_t) c], hp[(size_t) c]);
        stepLen = (int) std::lround (sr * 0.1);
        reset();
    }

    void reset()
    {
        for (auto& f : shelf) f.reset();
        for (auto& f : hp) f.reset();
        resetIntegrated();
    }

    // neue Messung: auch die gleitenden Fenster leeren, damit nichts Altes hineinwirkt
    void resetIntegrated()
    {
        blocks.fill (0.0); blockCount = 0; acc = 0; accN = 0;
        momentary = shortTerm = -200.0;
        integratedHist.reset(); lraHist.reset();
        integrated = -200.0; lra = 0.0; maxMomentary = maxShort = -200.0;
        stepsGated = 0;
    }

    // ein Stereo-Frame (Mono: r ignoriert). `gated` = in die Langzeitwerte aufnehmen
    // gibt true zurück, wenn ein 100-ms-Abschnitt fertig wurde (neue Werte verfügbar)
    bool process (float l, float r, bool gated)
    {
        double e = 0;
        const double yl = hp[0].process (shelf[0].process ((double) l));
        e += yl * yl;
        if (channels == 2)
        {
            const double yr = hp[1].process (shelf[1].process ((double) r));
            e += yr * yr;
        }
        acc += e; ++accN;
        if (accN < stepLen) return false;

        blocks[(size_t) (blockCount % 30)] = acc / (double) accN;
        ++blockCount; acc = 0; accN = 0;

        momentary = meanOf (4);
        shortTerm = meanOf (30);
        if (gated)
        {
            ++stepsGated;
            if (blockCount >= 4) { integratedHist.add (momentary); maxMomentary = std::max (maxMomentary, momentary); }
            if (blockCount >= 30) { lraHist.add (shortTerm); maxShort = std::max (maxShort, shortTerm); }
            if (integratedHist.size() > 0)
                integrated = integratedHist.gatedMean (integratedHist.relativeThreshold (-10.0));
            if (lraHist.size() > 0)
            {
                const double t = lraHist.relativeThreshold (-20.0);
                lra = std::max (0.0, lraHist.percentile (t, 0.95) - lraHist.percentile (t, 0.10));
            }
        }
        return true;
    }

    double momentary = -200.0, shortTerm = -200.0, integrated = -200.0, lra = 0.0;
    double maxMomentary = -200.0, maxShort = -200.0;
    long long stepsGated = 0;    // gemessene Zeit in 100-ms-Schritten

private:
    double meanOf (int n) const
    {
        if (blockCount < n) n = (int) std::max (1LL, blockCount);
        double s = 0;
        for (int i = 0; i < n; ++i) s += blocks[(size_t) ((blockCount - 1 - i) % 30)];
        return energyToLufs (s / n);
    }

    double sr = 48000.0;
    int channels = 2, stepLen = 4800;
    std::array<Biquad, 2> shelf, hp;
    std::array<double, 30> blocks {};
    long long blockCount = 0;
    double acc = 0; int accN = 0;
    Histogram integratedHist, lraHist;
};

} // namespace loudness
