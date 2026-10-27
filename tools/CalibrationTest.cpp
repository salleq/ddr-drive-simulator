// End-to-end test of "My Car": a simulated car and phone record the test signal,
// DDR analyses the recordings and fits a profile, and the result is compared with the truth.
// Usage: ddr_caltest                      run the test (exit code 1 if anything is off)
//        ddr_caltest --write <file.wav>   write the test signal
//        ddr_caltest --analyse <file>     analyse a recording and print the curve

#include <juce_audio_formats/juce_audio_formats.h>
#include "calibration/Calibration.h"
#include "dsp/Biquad.h"

using namespace ddr;

namespace
{
    constexpr double sr = 48000.0;

    struct Filter
    {
        std::vector<Biquad> stages;
        int echoDelay = 0;
        float echoGain = 0.0f;

        std::vector<float> run (const std::vector<float>& in) const
        {
            auto s = stages;
            std::vector<float> out (in.size());
            for (size_t i = 0; i < in.size(); ++i)
            {
                float x = in[i];
                for (auto& b : s) x = b.process (x);
                out[i] = x;
            }
            if (echoDelay > 0)
                for (size_t i = out.size(); i-- > (size_t) echoDelay;)
                    out[i] += echoGain * out[i - (size_t) echoDelay];
            return out;
        }
    };

    Biquad hp (double f, double q) { Biquad b; b.setHighPass (sr, f, q); return b; }
    Biquad lp (double f, double q) { Biquad b; b.setLowPass (sr, f, q); return b; }
    Biquad peak (double f, double q, double g) { Biquad b; b.setPeak (sr, f, q, g); return b; }
    Biquad lowShelf (double f, double g) { Biquad b; b.setLowShelf (sr, f, g); return b; }
    Biquad highShelf (double f, double g) { Biquad b; b.setHighShelf (sr, f, g); return b; }

    /** A stock family car at the driver's seat, roughly. */
    Filter car()
    {
        Filter f;
        f.stages = { hp (72.0, 1.0), hp (50.0, 0.5), lowShelf (58.0, 9.0), peak (74.0, 6.0, 5.0), peak (121.0, 7.0, -6.0),
                     peak (230.0, 1.5, 3.0), peak (1600.0, 1.8, 4.0), peak (3200.0, 3.0, -6.0), lp (14000.0, 0.707) };
        f.echoDelay = 110; // a window reflection, 2.3 ms
        f.echoGain = 0.25f;
        return f;
    }

    /** A phone microphone: bass cut, a presence bump, a dull top. */
    Filter phone() { Filter f; f.stages = { hp (85.0, 0.7), peak (5000.0, 2.0, 3.0), highShelf (9000.0, -4.0) }; return f; }

    /** Small studio monitors at the mix position. */
    Filter monitors() { Filter f; f.stages = { hp (48.0, 0.8), peak (2000.0, 1.0, 1.5), peak (90.0, 4.0, 4.0) }; return f; }

    std::vector<float> chain (const std::vector<float>& x, std::initializer_list<Filter> filters)
    {
        auto y = x;
        for (auto& f : filters) y = f.run (y);
        return y;
    }

    /** Plays the test signal through the filters and records it, with a late start, background noise
        and a playback clock that runs a little fast. */
    std::vector<float> record (std::initializer_list<Filter> filters, double leadSeconds, float noiseDb, int seed)
    {
        const auto signal = cal::makeTestSignal (sr * (1.0 + 80.0e-6));
        std::vector<float> x ((size_t) (leadSeconds * sr), 0.0f);
        x.insert (x.end(), signal.getReadPointer (0), signal.getReadPointer (0) + signal.getNumSamples());
        x.resize (x.size() + (size_t) (2.0 * sr), 0.0f);
        auto y = chain (x, filters);

        juce::Random rng (seed);
        const float g = std::pow (10.0f, noiseDb / 20.0f);
        float lpState = 0.0f;
        for (auto& v : y)
        {
            lpState = 0.98f * lpState + 0.02f * (rng.nextFloat() * 2.0f - 1.0f); // rumbly, like a car park
            v += g * (lpState * 8.0f + 0.2f * (rng.nextFloat() * 2.0f - 1.0f));
        }
        return y;
    }

    std::vector<float> truth (std::initializer_list<Filter> filters)
    {
        std::vector<float> impulse (1 << 15, 0.0f);
        impulse[0] = 1.0f;
        const auto ir = chain (impulse, filters);
        auto db = cal::responseOnGrid (ir.data(), (int) ir.size(), sr);
        return db;
    }

    /** RMS difference between two curves from 40 Hz to 12 kHz, after matching their levels. */
    double rmsDiff (const std::vector<float>& a, const std::vector<float>& b, double from = 40.0, double to = 12000.0)
    {
        const auto& g = cal::grid();
        double sum = 0.0, sumSq = 0.0;
        int n = 0;
        for (size_t k = 0; k < g.size(); ++k)
            if (g[k] >= from && g[k] <= to) { sum += a[k] - b[k]; ++n; }
        const double off = sum / n;
        for (size_t k = 0; k < g.size(); ++k)
            if (g[k] >= from && g[k] <= to) { const double d = a[k] - b[k] - off; sumSq += d * d; }
        return std::sqrt (sumSq / n);
    }

    std::vector<float> minus (std::vector<float> a, const std::vector<float>& b)
    {
        for (size_t i = 0; i < a.size(); ++i) a[i] -= b[i];
        return a;
    }

    void printCurve (const char* name, const std::vector<float>& v)
    {
        const auto& g = cal::grid();
        std::printf ("%-10s", name);
        for (size_t k = 0; k < g.size(); k += 6) std::printf ("%6.1f", v[k]);
        std::printf ("\n");
    }

    bool check (const char* what, bool ok)
    {
        std::printf ("%-56s %s\n", what, ok ? "ok" : "FAILED");
        return ok;
    }
}

int main (int argc, char* argv[])
{
    if (argc == 3 && juce::String (argv[1]) == "--write")
        return cal::writeTestSignal (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])) ? 0 : 1;

    if (argc == 3 && juce::String (argv[1]) == "--analyse")
    {
        const auto m = cal::analyseFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        std::printf ("%s\n", m.message.toRawUTF8());
        if (m.ok)
            for (size_t k = 0; k < cal::grid().size(); ++k)
                std::printf ("%8.1f Hz %6.1f dB  (signal-to-noise %4.0f dB)\n", cal::grid()[k], m.levelDb[k], m.snrPerPoint[k]);
        return m.ok ? 0 : 1;
    }

    bool ok = true;
    const auto& g = cal::grid();
    std::printf ("%-10s", "Hz");
    for (size_t k = 0; k < g.size(); k += 6) std::printf ("%6.0f", g[k]);
    std::printf ("\n");

    // 1. The test signal itself, written to a file and read back, must measure flat.
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ddr-caltest-signal.wav");
        ok &= check ("test signal written", cal::writeTestSignal (file));
        const auto m = cal::analyseFile (file);
        file.deleteFile();
        printCurve ("loopback", m.levelDb);
        const std::vector<float> flat (g.size(), 0.0f);
        ok &= check ("loopback finds 3 sweeps", m.ok && m.sweepsFound == 3);
        ok &= check ("loopback is flat (< 0.3 dB RMS, 25 Hz - 16 kHz)", m.ok && rmsDiff (m.levelDb, flat, 25.0, 16000.0) < 0.3);
    }

    // 2. Car recording on its own: car and phone together
    const auto carRec = record ({ car(), phone() }, 2.3, -52.0f, 1);
    const auto tA = juce::Time::getMillisecondCounterHiRes();
    const auto carM = cal::analyse (carRec.data(), (int) carRec.size(), sr);
    std::printf ("analysis of %.0f s took %.0f ms\n", carRec.size() / sr, juce::Time::getMillisecondCounterHiRes() - tA);
    std::printf ("car: %s\n", carM.message.toRawUTF8());
    const auto carTruth = truth ({ car(), phone() });
    printCurve ("car+phone", carTruth);
    printCurve ("measured", carM.levelDb);
    const double e1 = carM.ok ? rmsDiff (carM.levelDb, carTruth) : 99.0;
    std::printf ("car measurement error: %.2f dB RMS\n", e1);
    ok &= check ("car recording analysed, 3 sweeps", carM.ok && carM.sweepsFound == 3);
    ok &= check ("car measurement within 1 dB RMS", e1 < 1.0);

    // 3. With the studio reference: the phone and the monitors cancel out
    const auto studioRec = record ({ monitors(), phone() }, 0.8, -60.0f, 2);
    const auto studioM = cal::analyse (studioRec.data(), (int) studioRec.size(), sr);
    std::printf ("studio: %s\n", studioM.message.toRawUTF8());
    const auto target = cal::makeTarget (carM, &studioM);
    const auto relTruth = minus (truth ({ car() }), truth ({ monitors() }));
    printCurve ("car/studio", relTruth);
    printCurve ("target", target.db);
    const double e2 = rmsDiff (target.db, relTruth);
    std::printf ("car relative to studio error: %.2f dB RMS\n", e2);
    ok &= check ("studio recording analysed", studioM.ok);
    ok &= check ("car relative to studio within 1 dB RMS", e2 < 1.0);

    // 4. The fit: DDR's own chain with the fitted profile must follow the target
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto fit = cal::fitProfile (target, 1);
    std::printf ("fit took %.0f ms, error %.2f dB RMS\n", juce::Time::getMillisecondCounterHiRes() - t0, (double) fit.errorDb);
    printCurve ("DDR", fit.modelDb);
    const auto& p = fit.profile;
    std::printf ("low cut %.0f Hz Q %.2f, boom %.0f Hz %+.1f dB, top %.0f Hz\n", p.lowCutHz, p.lowCutQ, p.boomHz, p.boomDb, p.topHz);
    for (auto* set : { &p.modes })
        for (auto& b : *set) std::printf ("  mode %7.0f Hz Q %5.2f %+5.1f dB\n", b.freq, b.q, b.gainDb);
    for (auto& b : p.speakerBands) std::printf ("  band %7.0f Hz Q %5.2f %+5.1f dB\n", b.freq, b.q, b.gainDb);
    for (auto& b : p.extraBands) if (std::abs (b.gainDb) > 0.0f) std::printf ("  extra %6.0f Hz Q %5.2f %+5.1f dB\n", b.freq, b.q, b.gainDb);
    ok &= check ("fit within 1.5 dB RMS", fit.errorDb < 1.5f);
    ok &= check ("DDR with the profile within 1.5 dB RMS of the truth", rmsDiff (fit.modelDb, relTruth, 40.0, 12000.0) < 1.5);

    // 5. Saved and loaded with the plugin state
    {
        const auto tree = cal::toValueTree (fit, target);
        CarProfile back {};
        const bool loaded = cal::fromValueTree (juce::ValueTree::fromXml (*tree.createXml()), back);
        ok &= check ("profile survives a save and load", loaded && std::abs (back.lowCutHz - p.lowCutHz) < 0.1f
                                                          && std::abs (back.modes[0].gainDb - p.modes[0].gainDb) < 0.01f
                                                          && cal::curveFromTree (tree, "model").size() == g.size());
    }

    // 6. Things that must be refused politely
    {
        juce::Random rng (3);
        std::vector<float> noise ((size_t) (40.0 * sr));
        for (auto& v : noise) v = 0.1f * (rng.nextFloat() * 2.0f - 1.0f);
        const auto m = cal::analyse (noise.data(), (int) noise.size(), sr);
        std::printf ("noise only: %s\n", m.message.toRawUTF8());
        ok &= check ("a recording without the test signal is refused", ! m.ok);

        const auto m2 = cal::analyse (noise.data(), (int) (3.0 * sr), sr);
        ok &= check ("a too short recording is refused", ! m2.ok);
    }

    // 7. A noisy recording still works, with a warning
    {
        const auto rec = record ({ car(), phone() }, 1.0, -30.0f, 4);
        const auto m = cal::analyse (rec.data(), (int) rec.size(), sr);
        std::printf ("noisy car: %s\n", m.message.toRawUTF8());
        ok &= check ("noisy recording analysed", m.ok);
    }

    std::printf ("%s\n", ok ? "ALL OK" : "SOMETHING FAILED");
    return ok ? 0 : 1;
}
