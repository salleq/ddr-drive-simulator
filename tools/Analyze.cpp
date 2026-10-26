// Prints the magnitude response of the music chain for every car type.
// Usage: ddr_analyze

#include <juce_dsp/juce_dsp.h>
#include "dsp/MusicChain.h"

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
    return 0;
}
