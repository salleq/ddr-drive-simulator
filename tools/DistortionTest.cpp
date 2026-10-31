// Measures what the speaker distortion adds to a steady bass tone, for every car type.
// Usage: ddr_disttest
//   low   = harmonics 100 Hz - 1 kHz      (warm, "the speaker is working hard")
//   fizz  = harmonics above 2 kHz         (harsh, "broken speaker")
//   noise = everything that is not a harmonic of the tone (rattle / scratch)
// All in dB relative to the tone itself.

#include <juce_dsp/juce_dsp.h>
#include "dsp/MusicChain.h"

int main()
{
    const double sr = 48000.0;
    const int order = 15, n = 1 << order;
    const int toneBin = 68;                          // 99.6 Hz, exactly on an FFT bin
    const double toneHz = toneBin * sr / n;

    auto measure = [&] (const ddr::CarProfile& car, float distortion, bool worn, float amp, int volume)
    {
        ddr::Settings s;
        s.car = &car;
        s.seat = ddr::Seat::sweetSpot;
        s.distortion = distortion;
        s.worn = worn;
        s.loudness = false;
        s.svc = 0;
        s.limiter = false;
        s.volume = volume;
        s.reflections = 0.0f;

        ddr::MusicChain chain;
        chain.prepare (sr, n);
        chain.update (s);
        chain.reset();

        std::vector<float> l ((size_t) n * 2), r ((size_t) n * 2);
        for (int pass = 0; pass < 2; ++pass)     // second pass = steady state
        {
            for (int i = 0; i < n; ++i)
                l[(size_t) i] = r[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * toneHz * i / sr);
            chain.process (l.data(), r.data(), n);
        }

        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) n * 2, 0.0f);
        std::copy (l.begin(), l.begin() + n, data.begin());
        fft.performFrequencyOnlyForwardTransform (data.data());

        double fund = 0.0, low = 0.0, fizz = 0.0, noise = 0.0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double e = (double) data[(size_t) k] * data[(size_t) k];
            const double f = k * sr / n;
            if (k == toneBin)          fund += e;
            else if (k % toneBin == 0) { if (f >= 100.0 && f <= 1000.0) low += e; else if (f >= 2000.0) fizz += e; }
            else if (f > 20.0)         noise += e;
        }
        auto db = [&] (double e) { return 10.0 * std::log10 (e / fund + 1.0e-20); };
        std::printf ("  %6.1f %6.1f %6.1f", db (low), db (fizz), db (noise));
    };

    for (int volume : { 20, 28 })
    {
        const float amp = 0.5f;
        std::printf ("\nBass tone %.0f Hz at %.1f, volume %d   dist 0%%               dist 100%%             dist 100%% + worn      dist 200%%\n", toneHz, amp, volume);
        std::printf ("%-18s     low   fizz  noise    low   fizz  noise    low   fizz  noise    low   fizz  noise\n", "");
        for (const auto& car : ddr::carProfiles())
        {
            std::printf ("%-18s", car.name);
            measure (car, 0.0f, false, amp, volume);
            measure (car, 1.0f, false, amp, volume);
            measure (car, 1.0f, true, amp, volume);
            measure (car, 2.0f, false, amp, volume);
            std::printf ("\n");
        }
    }
}
