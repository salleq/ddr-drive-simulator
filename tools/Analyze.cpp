// Prints the magnitude response of the music chain for every car type.
// Usage: ddr_analyze

#include <juce_dsp/juce_dsp.h>
#include "dsp/MusicChain.h"
#include "dsp/NoiseGenerator.h"

int main()
{
    const double sr = 48000.0;
    const int order = 15, n = 1 << order;
    const double freqs[] { 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                           1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };

    std::printf ("%-18s", "Hz");
    for (auto f : freqs) std::printf ("%6.0f", f);
    std::printf ("\n");

    for (const auto& car : ddr::carProfiles())
    {
        ddr::Settings s;
        s.car = &car;
        s.seat = ddr::Seat::sweetSpot;
        s.distortion = 0.0f;
        s.loudness = false;
        s.svc = 0;
        s.limiter = false;
        s.volume = 20;
        s.reflections = 0.0f; // keep the comb filtering out of the tonal picture

        ddr::MusicChain chain;
        chain.prepare (sr, n);
        chain.update (s);
        chain.reset();

        std::vector<float> l ((size_t) n * 2, 0.0f), r ((size_t) n * 2, 0.0f);
        l[0] = r[0] = 0.01f; // small impulse, so nothing clips
        chain.process (l.data(), r.data(), n);

        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) n * 2, 0.0f);
        for (int i = 0; i < n; ++i) data[(size_t) i] = l[(size_t) i] * 100.0f;
        fft.performFrequencyOnlyForwardTransform (data.data());

        auto magAt = [&] (double f)
        {
            // average a third of an octave around f
            const int lo = (int) (f / 1.12 / sr * n), hi = std::max (lo + 1, (int) (f * 1.12 / sr * n));
            double sum = 0.0;
            for (int k = lo; k <= hi; ++k) sum += (double) data[(size_t) k] * data[(size_t) k];
            return 10.0 * std::log10 (sum / (hi - lo + 1) + 1.0e-20);
        };

        const double ref = magAt (1000.0);
        std::printf ("%-18s", car.name);
        for (auto f : freqs) std::printf ("%6.1f", magAt (f) - ref);
        std::printf ("\n");
    }
    // Noise spectrum per road surface at 100 km/h, as dB SPL per third octave (95 dB SPL = 0 dBFS)
    std::printf ("\nNoise at 100 km/h, dB SPL per third octave, Family Hatchback\n%-18s", "Hz");
    for (auto f : freqs) std::printf ("%6.0f", f);
    std::printf ("  total\n");
    for (int road = 0; road < 5; ++road)
    {
        ddr::Settings s;
        s.car = &ddr::carProfiles()[1];
        s.road = (ddr::Road) road;
        s.speedKmh = 100.0f;
        ddr::NoiseGenerator gen;
        gen.prepare (sr);
        gen.update (s);
        gen.reset();
        std::vector<float> l ((size_t) n * 2), r ((size_t) n * 2);
        gen.process (l.data(), r.data(), n); // let gains settle
        gen.process (l.data(), r.data(), n);

        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> win ((size_t) n, juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<float> data ((size_t) n * 2, 0.0f);
        double total = 0.0;
        for (int i = 0; i < n; ++i) { data[(size_t) i] = l[(size_t) i]; total += (double) l[(size_t) i] * l[(size_t) i]; }
        win.multiplyWithWindowingTable (data.data(), (size_t) n);
        fft.performFrequencyOnlyForwardTransform (data.data());

        // power per bin, scaled so that the bins sum to the signal's mean square (Hann power gain 0.375)
        const double scale = 2.0 / ((double) n * (double) n * 0.375);
        std::printf ("%-18s", ddr::roadNames()[road].toRawUTF8());
        for (auto f : freqs)
        {
            const int lo = (int) (f / 1.122 / sr * n), hi = std::max (lo, (int) (f * 1.122 / sr * n) - 1);
            double p = 0.0;
            for (int k = lo; k <= hi; ++k) p += (double) data[(size_t) k] * data[(size_t) k] * scale;
            std::printf ("%6.1f", 10.0 * std::log10 (p + 1.0e-20) + 95.0);
        }
        std::printf ("  %5.1f\n", 10.0 * std::log10 (total / n) + 95.0);
    }
    return 0;
}
