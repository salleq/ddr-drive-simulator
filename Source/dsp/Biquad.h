#pragma once

#include <cmath>

namespace ddr
{
/** A plain RBJ-cookbook biquad. Coefficient updates never allocate, so they are
    safe to call from the audio thread once per block. */
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setIdentity() noexcept { b0 = 1.0f; b1 = b2 = a1 = a2 = 0.0f; }

    void setLowPass (double sr, double f, double q) noexcept
    {
        const auto w = omega (sr, f), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setHighPass (double sr, double f, double q) noexcept
    {
        const auto w = omega (sr, f), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    /** Constant 0 dB peak gain band-pass. */
    void setBandPass (double sr, double f, double q) noexcept
    {
        const auto w = omega (sr, f), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setPeak (double sr, double f, double q, double gainDb) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = omega (sr, f), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }

    void setLowShelf (double sr, double f, double gainDb, double slope = 1.0) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = omega (sr, f), c = std::cos (w), s = std::sin (w);
        const auto alpha = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const auto k = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1.0) - (A - 1.0) * c + k),
             2.0 * A * ((A - 1.0) - (A + 1.0) * c),
             A * ((A + 1.0) - (A - 1.0) * c - k),
             (A + 1.0) + (A - 1.0) * c + k,
             -2.0 * ((A - 1.0) + (A + 1.0) * c),
             (A + 1.0) + (A - 1.0) * c - k);
    }

    void setHighShelf (double sr, double f, double gainDb, double slope = 1.0) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = omega (sr, f), c = std::cos (w), s = std::sin (w);
        const auto alpha = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const auto k = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1.0) + (A - 1.0) * c + k),
             -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
             A * ((A + 1.0) + (A - 1.0) * c - k),
             (A + 1.0) - (A - 1.0) * c + k,
             2.0 * ((A - 1.0) - (A + 1.0) * c),
             (A + 1.0) - (A - 1.0) * c - k);
    }

private:
    static double omega (double sr, double f) noexcept
    {
        // keep the corner safely below Nyquist
        const auto fc = std::fmin (std::fmax (f, 5.0), sr * 0.45);
        return 2.0 * 3.14159265358979323846 * fc / sr;
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = (float) (nb0 / na0);
        b1 = (float) (nb1 / na0);
        b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0);
        a2 = (float) (na2 / na0);
    }
};

/** One-pole low-pass, used for smoothing and gentle absorption. */
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (double sr, double f) noexcept { a = (float) std::exp (-2.0 * 3.14159265358979323846 * f / sr); }
    void reset() noexcept { z = 0.0f; }
    inline float process (float x) noexcept { z = x + a * (z - x); return z; }
};

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
} // namespace ddr
