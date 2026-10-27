#include "Calibration.h"
#include "../dsp/MusicChain.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

namespace ddr::cal
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double designRate = 48000.0;   // the fit is done at this rate
    constexpr double maxSeconds = 180.0;     // only this much of a recording is read

    using cd = std::complex<double>;

    /** A plain radix-2 FFT. Self-contained, so the result is the same on every platform. */
    struct FFT
    {
        explicit FFT (int size) : n (size), twiddle ((size_t) size / 2), reversed ((size_t) size)
        {
            for (int k = 0; k < n / 2; ++k)
                twiddle[(size_t) k] = std::polar (1.0, -2.0 * pi * k / n);
            int bits = 0;
            while ((1 << bits) < n) ++bits;
            for (int i = 0; i < n; ++i)
            {
                int r = 0;
                for (int b = 0; b < bits; ++b)
                    if (i & (1 << b)) r |= 1 << (bits - 1 - b);
                reversed[(size_t) i] = r;
            }
        }

        void run (std::vector<cd>& a, bool inverse) const
        {
            for (int i = 0; i < n; ++i)
                if (i < reversed[(size_t) i])
                    std::swap (a[(size_t) i], a[(size_t) reversed[(size_t) i]]);

            for (int len = 2; len <= n; len <<= 1)
            {
                const int half = len / 2, step = n / len;
                for (int i = 0; i < n; i += len)
                    for (int j = 0; j < half; ++j)
                    {
                        auto w = twiddle[(size_t) (j * step)];
                        if (inverse) w = std::conj (w);
                        const auto u = a[(size_t) (i + j)];
                        const auto v = a[(size_t) (i + j + half)] * w;
                        a[(size_t) (i + j)] = u + v;
                        a[(size_t) (i + j + half)] = u - v;
                    }
            }

            if (inverse)
                for (auto& v : a) v /= (double) n;
        }

        int n;
        std::vector<cd> twiddle;
        std::vector<int> reversed;
    };

    int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

    /** Linear convolution of a long signal with a long filter (overlap-save). */
    std::vector<float> convolve (const float* x, int nx, const std::vector<float>& h)
    {
        const int nh = (int) h.size();
        const int nOut = nx + nh - 1;
        const int n = std::max (4096, nextPow2 (2 * nh));
        const int hop = n - nh + 1;
        FFT fft (n);

        std::vector<cd> H ((size_t) n, 0.0);
        for (int i = 0; i < nh; ++i) H[(size_t) i] = h[(size_t) i];
        fft.run (H, false);

        std::vector<float> y ((size_t) nOut, 0.0f);
        std::vector<cd> buf ((size_t) n);
        for (int start = 0; start < nOut; start += hop)
        {
            for (int i = 0; i < n; ++i)
            {
                const int idx = start - (nh - 1) + i;
                buf[(size_t) i] = (idx >= 0 && idx < nx) ? (double) x[idx] : 0.0;
            }
            fft.run (buf, false);
            for (int i = 0; i < n; ++i) buf[(size_t) i] *= H[(size_t) i];
            fft.run (buf, true);
            for (int i = 0; i < hop && start + i < nOut; ++i)
                y[(size_t) (start + i)] = (float) buf[(size_t) (nh - 1 + i)].real();
        }
        return y;
    }

    double sweepRate() { return TestSignal::sweepSeconds / std::log (TestSignal::f2 / TestSignal::f1); }

    /** Power on the grid of a windowed slice, as linear power per grid point. */
    std::vector<double> gridPower (const float* ir, int n, double sr, double smoothingOctaves)
    {
        const int size = std::max (32768, 2 * nextPow2 (n));
        FFT fft (size);
        std::vector<cd> buf ((size_t) size, 0.0);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = (double) ir[i];
        fft.run (buf, false);

        const double binHz = sr / size;
        const auto& g = grid();
        std::vector<double> out (g.size(), 1.0e-30);
        const double half = std::pow (2.0, smoothingOctaves * 0.5);
        for (size_t k = 0; k < g.size(); ++k)
        {
            const double f = g[k];
            if (f >= sr * 0.49)
            {
                out[k] = k > 0 ? out[k - 1] : 1.0e-30;
                continue;
            }
            int lo = (int) std::ceil (f / half / binHz), hi = (int) std::floor (f * half / binHz);
            lo = std::max (1, lo);
            hi = std::min (size / 2 - 1, hi);
            if (hi < lo) lo = hi = std::max (1, (int) std::lround (f / binHz));
            double sum = 0.0;
            for (int b = lo; b <= hi; ++b) sum += std::norm (buf[(size_t) b]);
            out[k] = sum / (hi - lo + 1) + 1.0e-30;
        }
        return out;
    }

    std::vector<float> toDb (const std::vector<double>& p)
    {
        std::vector<float> d (p.size());
        for (size_t i = 0; i < p.size(); ++i) d[i] = (float) (10.0 * std::log10 (p[i] + 1.0e-30));
        return d;
    }

    /** Cuts a slice out of a long signal and gives it a short fade-in and a long fade-out. */
    std::vector<float> windowed (const std::vector<float>& y, int peak, int pre, int post)
    {
        std::vector<float> w ((size_t) (pre + post), 0.0f);
        const int fadeOut = post * 2 / 5;
        for (int i = 0; i < pre + post; ++i)
        {
            const int idx = peak - pre + i;
            if (idx < 0 || idx >= (int) y.size()) continue;
            double g = 1.0;
            if (i < pre) g = 0.5 - 0.5 * std::cos (pi * i / pre);
            else if (i >= pre + post - fadeOut) g = 0.5 + 0.5 * std::cos (pi * (i - (pre + post - fadeOut)) / fadeOut);
            w[(size_t) i] = (float) (y[(size_t) idx] * g);
        }
        return w;
    }

    double energy (const std::vector<float>& v)
    {
        double e = 0.0;
        for (auto s : v) e += (double) s * s;
        return e;
    }

    /** Mean of a curve over a frequency range, weighted. */
    double meanOver (const std::vector<float>& db, const std::vector<float>& weight, double from, double to)
    {
        const auto& g = grid();
        double sum = 0.0, wsum = 0.0;
        for (size_t k = 0; k < g.size(); ++k)
            if (g[k] >= from && g[k] <= to && weight[k] > 0.0f)
            {
                sum += weight[k] * db[k];
                wsum += weight[k];
            }
        return wsum > 0.0 ? sum / wsum : 0.0;
    }

    std::vector<float> movingAverage (const std::vector<float>& v, int radius)
    {
        std::vector<float> out (v.size());
        for (int i = 0; i < (int) v.size(); ++i)
        {
            double sum = 0.0;
            int count = 0;
            for (int j = std::max (0, i - radius); j <= std::min ((int) v.size() - 1, i + radius); ++j)
            {
                sum += v[(size_t) j];
                ++count;
            }
            out[(size_t) i] = (float) (sum / count);
        }
        return out;
    }

    juce::String toCsv (const std::vector<float>& v)
    {
        juce::StringArray s;
        for (auto x : v) s.add (juce::String (x, 2));
        return s.joinIntoString (",");
    }

    std::vector<float> fromCsv (const juce::String& text)
    {
        std::vector<float> v;
        for (auto& t : juce::StringArray::fromTokens (text, ",", ""))
            if (t.isNotEmpty()) v.push_back (t.getFloatValue());
        return v;
    }

    template <size_t N>
    juce::String bandsToString (const std::array<Band, N>& bands)
    {
        juce::StringArray s;
        for (auto& b : bands) s.add (juce::String (b.freq, 1) + " " + juce::String (b.q, 3) + " " + juce::String (b.gainDb, 2));
        return s.joinIntoString (";");
    }

    template <size_t N>
    bool bandsFromString (const juce::String& text, std::array<Band, N>& bands)
    {
        auto items = juce::StringArray::fromTokens (text, ";", "");
        if (items.size() != (int) N) return false;
        for (size_t i = 0; i < N; ++i)
        {
            auto parts = juce::StringArray::fromTokens (items[(int) i], " ", "");
            if (parts.size() != 3) return false;
            bands[i] = { parts[0].getFloatValue(), parts[1].getFloatValue(), parts[2].getFloatValue() };
            if (! (bands[i].freq >= 10.0f && bands[i].freq <= 24000.0f && bands[i].q > 0.05f && bands[i].q < 50.0f
                   && std::abs (bands[i].gainDb) <= 30.0f))
                return false;
        }
        return true;
    }
}

//==============================================================================
const std::vector<double>& grid()
{
    static const std::vector<double> g = []
    {
        std::vector<double> v;
        for (int k = 0; k <= 120; ++k)
            v.push_back (20.0 * std::pow (2.0, k / 12.0));
        return v;
    }();
    return g;
}

std::vector<float> makeSweep (double sr)
{
    const int n = (int) std::lround (TestSignal::sweepSeconds * sr);
    const double L = sweepRate();
    const double fadeIn = 0.1, fadeOut = 0.02;
    const double topF = std::min (TestSignal::f2, sr * 0.45);
    std::vector<float> s ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f = TestSignal::f1 * std::exp (t / L);
        double g = TestSignal::sweepLevel;
        if (t < fadeIn) g *= 0.5 - 0.5 * std::cos (pi * t / fadeIn);
        const double toEnd = TestSignal::sweepSeconds - t;
        if (toEnd < fadeOut) g *= 0.5 - 0.5 * std::cos (pi * std::max (0.0, toEnd) / fadeOut);
        if (f > topF) g = 0.0; // only happens at low sample rates: nothing can be measured up there anyway
        s[(size_t) i] = (float) (g * std::sin (2.0 * pi * TestSignal::f1 * L * (std::exp (t / L) - 1.0)));
    }
    return s;
}

juce::AudioBuffer<float> makeTestSignal (double sr)
{
    const double firstSweep = TestSignal::leadSilence + TestSignal::noiseSeconds + TestSignal::preSweepSilence;
    const double total = firstSweep + (TestSignal::numSweeps - 1) * TestSignal::period + TestSignal::sweepSeconds
                       + TestSignal::tailSilence;
    juce::AudioBuffer<float> out (2, (int) std::lround (total * sr));
    out.clear();
    float* d = out.getWritePointer (0);

    // Pink noise, so you can set a sensible volume before the sweeps start (Paul Kellet's filter)
    {
        juce::Random rng (1234);
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        const int start = (int) (TestSignal::leadSilence * sr), len = (int) (TestSignal::noiseSeconds * sr);
        const int fade = (int) (0.05 * sr);
        for (int i = 0; i < len; ++i)
        {
            const double w = rng.nextDouble() * 2.0 - 1.0;
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            const double pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11;
            b6 = w * 0.115926;
            double g = 0.6; // about -18 dBFS RMS
            if (i < fade) g *= (double) i / fade;
            if (len - i < fade) g *= (double) (len - i) / fade;
            d[start + i] = (float) (pink * g);
        }
    }

    const auto sweep = makeSweep (sr);
    for (int k = 0; k < TestSignal::numSweeps; ++k)
    {
        const int start = (int) std::lround ((firstSweep + k * TestSignal::period) * sr);
        for (size_t i = 0; i < sweep.size() && start + (int) i < out.getNumSamples(); ++i)
            d[start + (int) i] = sweep[i];
    }

    out.copyFrom (1, 0, out, 0, 0, out.getNumSamples());
    return out;
}

bool writeTestSignal (const juce::File& file)
{
    const auto signal = makeTestSignal();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), TestSignal::sampleRate, 2, 16, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // the writer owns it now
    return writer->writeFromAudioSampleBuffer (signal, 0, signal.getNumSamples());
}

std::vector<float> responseOnGrid (const float* ir, int n, double sr, double smoothingOctaves)
{
    return toDb (gridPower (ir, n, sr, smoothingOctaves));
}

//==============================================================================
Measurement analyse (const float* input, int numSamples, double sr)
{
    Measurement m;
    if (sr < 32000.0)
    {
        m.message = "The recording's sample rate is too low (" + juce::String (juce::roundToInt (sr))
                  + " Hz). Choose a higher quality setting in the recorder app.";
        return m;
    }
    if (numSamples < (int) ((TestSignal::sweepSeconds + 1.0) * sr))
    {
        m.message = "The recording is too short. Record the whole test signal, from the noise to the end.";
        return m;
    }
    numSamples = std::min (numSamples, (int) (maxSeconds * sr));

    // remove DC, and look for clipping
    std::vector<float> x (input, input + numSamples);
    const double mean = std::accumulate (x.begin(), x.end(), 0.0) / numSamples;
    int clipped = 0;
    for (auto& v : x)
    {
        if (std::abs (v) > 0.985f) ++clipped;
        v = (float) (v - mean);
    }

    // The inverse sweep: time reversed, and tilted +6 dB per octave so the result comes out flat
    const auto sweep = makeSweep (sr);
    const int ns = (int) sweep.size();
    const double L = sweepRate();
    std::vector<float> inverse ((size_t) ns);
    for (int i = 0; i < ns; ++i)
    {
        const double tOrig = (ns - 1 - i) / sr;
        inverse[(size_t) i] = (float) (sweep[(size_t) (ns - 1 - i)] * std::exp (tOrig / L) * TestSignal::f1 / TestSignal::f2);
    }

    const auto y = convolve (x.data(), numSamples, inverse);
    const auto ref = convolve (sweep.data(), ns, inverse);

    // The strongest impulse is one sweep; the others sit one period before or after it
    auto absAt = [&] (int i) { return std::abs (y[(size_t) i]); };
    int best = 0;
    for (int i = 1; i < (int) y.size(); ++i)
        if (absAt (i) > absAt (best)) best = i;
    const float bestValue = absAt (best);

    std::vector<int> peaks;
    const int period = (int) std::lround (TestSignal::period * sr);
    const int search = (int) (0.1 * sr);
    for (int k = -4; k <= 4; ++k)
    {
        const int centre = best + k * period;
        int at = -1;
        for (int i = std::max (0, centre - search); i <= std::min ((int) y.size() - 1, centre + search); ++i)
            if (at < 0 || absAt (i) > absAt (at)) at = i;
        if (at >= 0 && absAt (at) >= 0.3f * bestValue)
            peaks.push_back (at);
    }

    const int pre = (int) (0.005 * sr), post = (int) (0.25 * sr);
    const int noiseOffset = (int) (0.6 * sr);
    std::vector<double> sig (grid().size(), 0.0), noise (grid().size(), 0.0);
    double sigEnergy = 0.0, noiseEnergy = 0.0;
    int used = 0;
    for (int p : peaks)
    {
        if (p + noiseOffset + post >= (int) y.size())
            continue;
        const auto s = windowed (y, p, pre, post);
        const auto n = windowed (y, p + noiseOffset, pre, post);
        const auto ps = gridPower (s.data(), (int) s.size(), sr, 1.0 / 6.0);
        const auto pn = gridPower (n.data(), (int) n.size(), sr, 1.0 / 6.0);
        for (size_t k = 0; k < sig.size(); ++k) { sig[k] += ps[k]; noise[k] += pn[k]; }
        sigEnergy += energy (s);
        noiseEnergy += energy (n);
        ++used;
    }

    m.snrDb = used > 0 ? (float) (10.0 * std::log10 (sigEnergy / (noiseEnergy + 1.0e-30))) : 0.0f;
    if (used == 0 || m.snrDb < 6.0f)
    {
        m.message = "The test signal was not found in this recording. Check that it is the right file, "
                    "and that the whole test signal was recorded.";
        return m;
    }

    int refPeak = 0;
    for (int i = 1; i < (int) ref.size(); ++i)
        if (std::abs (ref[(size_t) i]) > std::abs (ref[(size_t) refPeak])) refPeak = i;
    const auto r = windowed (ref, refPeak, pre, post);
    const auto pr = gridPower (r.data(), (int) r.size(), sr, 1.0 / 6.0);

    const auto& g = grid();
    m.levelDb.resize (g.size());
    m.snrPerPoint.resize (g.size());
    std::vector<float> trusted (g.size());
    for (size_t k = 0; k < g.size(); ++k)
    {
        m.levelDb[k] = (float) (10.0 * std::log10 (sig[k] / used / pr[k]));
        m.snrPerPoint[k] = (float) (10.0 * std::log10 (sig[k] / (noise[k] + 1.0e-30)));
        trusted[k] = (m.snrPerPoint[k] > 10.0f && g[k] < sr * 0.45) ? 1.0f : 0.0f;
    }
    const auto level = meanOver (m.levelDb, trusted, 200.0, 4000.0);
    for (auto& v : m.levelDb) v = (float) (v - level);

    m.ok = true;
    m.sweepsFound = std::min (used, TestSignal::numSweeps);
    juce::String quality = m.snrDb >= 30.0f ? "good" : (m.snrDb >= 20.0f ? "OK" : "noisy");
    m.message = juce::String (m.sweepsFound) + " of " + juce::String (TestSignal::numSweeps) + " sweeps found, signal-to-noise "
              + juce::String (juce::roundToInt (m.snrDb)) + " dB (" + quality + ").";
    if (m.snrDb < 20.0f)
    {
        m.warning = true;
        m.message << " Turn the volume up, switch the engine and fan off, and record again for a better result.";
    }
    if (clipped > 50)
    {
        m.warning = true;
        m.message << " The recording clips: turn the volume down a little and record again.";
    }
    if (m.sweepsFound < TestSignal::numSweeps && m.snrDb >= 20.0f)
    {
        m.warning = true;
        m.message << " Some sweeps are missing or were drowned by a noise; the rest are used.";
    }
    return m;
}

Measurement analyseFile (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        Measurement m;
        m.message = "DDR can't open " + file.getFileName() + ". Drag it into your DAW, export it as a WAV file "
                    "and load that instead.";
        return m;
    }

    const auto sr = reader->sampleRate;
    const int n = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (maxSeconds * sr));
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> buf (channels, std::max (1, n));
    buf.clear();
    reader->read (&buf, 0, n, 0, true, channels > 1);

    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < channels; ++c) s += buf.getSample (c, i);
        mono[(size_t) i] = s / (float) channels;
    }
    auto m = analyse (mono.data(), n, sr);
    if (! m.ok && reader->lengthInSamples > n)
        m.message << " Only the first 3 minutes are read: trim the recording so the test signal is in them.";
    return m;
}

//==============================================================================
Target makeTarget (const Measurement& car, const Measurement* studio)
{
    const auto& g = grid();
    Target t;
    t.relativeToStudio = studio != nullptr && studio->ok;
    t.db.resize (g.size());
    t.weight.resize (g.size());

    auto trust = [] (float snr) { return juce::jlimit (0.0f, 1.0f, (snr - 8.0f) / 7.0f); };

    for (size_t k = 0; k < g.size(); ++k)
    {
        const double f = g[k];
        // Phone microphones are not to be trusted at the very ends of the range
        float w = f < 28.0 ? 0.0f : f < 40.0 ? 0.4f : f <= 12000.0 ? 1.0f : f <= 16000.0 ? 0.4f : 0.0f;
        w *= trust (car.snrPerPoint[k]);
        float db = car.levelDb[k];
        if (t.relativeToStudio)
        {
            w *= trust (studio->snrPerPoint[k]);
            db -= studio->levelDb[k];
        }
        t.db[k] = juce::jlimit (-20.0f, 20.0f, db);
        t.weight[k] = w;
    }
    const auto level = meanOver (t.db, t.weight, 200.0, 4000.0);
    for (auto& v : t.db) v = (float) (v - level);
    return t;
}

//==============================================================================
std::vector<float> chainResponse (const CarProfile& profile)
{
    Settings s;
    s.car = &profile;
    s.seat = Seat::driver;

    const int n = 1 << 15;
    MusicChain chain;
    chain.prepare (designRate, n);
    chain.update (s);
    chain.reset();

    std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
    l[0] = r[0] = 0.01f;
    chain.process (l.data(), r.data(), n);

    // a phone in the middle of the head hears both ears' worth: average the power
    const auto pl = gridPower (l.data(), n, designRate, 1.0 / 6.0);
    const auto pr = gridPower (r.data(), n, designRate, 1.0 / 6.0);
    std::vector<double> p (pl.size());
    for (size_t k = 0; k < p.size(); ++k) p[k] = 0.5 * (pl[k] + pr[k]) * 1.0e4;
    return toDb (p);
}

void applyBaseCar (CarProfile& profile, int baseCar)
{
    const auto& base = carProfiles()[(size_t) juce::jlimit (0, (int) carProfiles().size() - 1, baseCar)];
    profile.name = "My Car";
    profile.coneDrive = base.coneDrive;
    profile.worn = false;
    profile.reflections = base.reflections;
    profile.widthMetres = base.widthMetres;
    profile.insulationDb = base.insulationDb;
    profile.rearSpeakers = base.rearSpeakers;
}

namespace
{
    /** The filters of a car profile that the fit can move. Same filter types as MusicChain uses. */
    struct Model
    {
        double hp = 70.0, hpQ = 0.9, top = 16000.0, boomHz = 60.0, boomDb = 6.0;
        std::vector<Band> peaks;
    };

    std::vector<double> hpVector (double hz, double q)
    {
        Biquad a, b;
        a.setHighPass (designRate, hz, q);
        b.setHighPass (designRate, hz * 0.7, 0.5);
        std::vector<double> v;
        for (auto f : grid()) v.push_back (a.magnitudeDb (f, designRate) + b.magnitudeDb (f, designRate));
        return v;
    }

    std::vector<double> shelfVector (double hz, double db)
    {
        Biquad a;
        a.setLowShelf (designRate, hz, db);
        std::vector<double> v;
        for (auto f : grid()) v.push_back (a.magnitudeDb (f, designRate));
        return v;
    }

    std::vector<double> topVector (double hz)
    {
        Biquad a;
        a.setLowPass (designRate, hz, 0.707);
        std::vector<double> v;
        for (auto f : grid()) v.push_back (a.magnitudeDb (f, designRate));
        return v;
    }

    std::vector<double> peakVector (const Band& band)
    {
        Biquad a;
        a.setPeak (designRate, band.freq, band.q, band.gainDb);
        std::vector<double> v;
        for (auto f : grid()) v.push_back (a.magnitudeDb (f, designRate));
        return v;
    }

    /** Weighted squared error after removing the best level offset. */
    double errorOf (const std::vector<double>& target, const std::vector<double>& weight, const std::vector<double>& model)
    {
        double sw = 0.0, swd = 0.0, swdd = 0.0;
        for (size_t k = 0; k < target.size(); ++k)
        {
            const double d = target[k] - model[k];
            sw += weight[k]; swd += weight[k] * d; swdd += weight[k] * d * d;
        }
        return sw > 0.0 ? (swdd - swd * swd / sw) / sw : 0.0;
    }

    double offsetOf (const std::vector<double>& target, const std::vector<double>& weight, const std::vector<double>& model)
    {
        double sw = 0.0, swd = 0.0;
        for (size_t k = 0; k < target.size(); ++k) { sw += weight[k]; swd += weight[k] * (target[k] - model[k]); }
        return sw > 0.0 ? swd / sw : 0.0;
    }

    class Fitter
    {
    public:
        Fitter (std::vector<double> t, std::vector<double> w) : target (std::move (t)), weight (std::move (w)) {}

        Model model;

        void fitBase()
        {
            // A broad version of the target, so the base filters follow the overall shape, not the peaks
            std::vector<double> broad (target.size());
            for (int i = 0; i < (int) target.size(); ++i)
            {
                double s = 0.0, sw = 0.0;
                for (int j = std::max (0, i - 3); j <= std::min ((int) target.size() - 1, i + 3); ++j)
                {
                    s += weight[(size_t) j] * target[(size_t) j];
                    sw += weight[(size_t) j];
                }
                broad[(size_t) i] = sw > 0.0 ? s / sw : target[(size_t) i];
            }

            std::vector<double> hpHz, hpQs { 0.5, 0.7, 0.9, 1.2, 1.6 }, boomHz, boomDb, topHz;
            for (int k = 0; k <= 40; ++k) hpHz.push_back (20.0 * std::pow (2.0, k / 12.0));
            for (int k = 0; k <= 12; ++k) boomHz.push_back (35.0 * std::pow (2.0, k / 6.0));
            for (int db = -3; db <= 15; ++db) boomDb.push_back (db);
            for (int k = 0; k <= 10; ++k) topHz.push_back (6000.0 * std::pow (2.0, k / 6.0));

            std::vector<std::vector<double>> hpV, shelfV, topV;
            for (auto hz : hpHz) for (auto q : hpQs) hpV.push_back (hpVector (hz, q));
            for (auto hz : boomHz) for (auto db : boomDb) shelfV.push_back (shelfVector (hz, db));
            for (auto hz : topHz) topV.push_back (topVector (hz));

            double bestErr = 1.0e30;
            size_t bh = 0, bs = 0, bt = 0;
            std::vector<double> m (target.size());
            for (size_t h = 0; h < hpV.size(); ++h)
                for (size_t s = 0; s < shelfV.size(); ++s)
                    for (size_t t = 0; t < topV.size(); ++t)
                    {
                        for (size_t k = 0; k < m.size(); ++k) m[k] = hpV[h][k] + shelfV[s][k] + topV[t][k];
                        const double e = errorOf (broad, weight, m);
                        if (e < bestErr) { bestErr = e; bh = h; bs = s; bt = t; }
                    }

            model.hp = hpHz[bh / hpQs.size()];
            model.hpQ = hpQs[bh % hpQs.size()];
            model.boomHz = boomHz[bs / boomDb.size()];
            model.boomDb = boomDb[bs % boomDb.size()];
            model.top = topHz[bt];
            rebuild();
        }

        /** Adds peaking filters one at a time where the error is biggest, refining everything as it goes. */
        void fitPeaks (int maxPeaks)
        {
            const auto& g = grid();
            while ((int) model.peaks.size() < maxPeaks)
            {
                const auto m = total();
                const double off = offsetOf (target, weight, m);
                std::vector<double> r (target.size());
                for (size_t k = 0; k < r.size(); ++k) r[k] = target[k] - m[k] - off;

                int at = -1;
                double worst = 0.0;
                for (int k = 1; k + 1 < (int) r.size(); ++k)
                {
                    if (weight[(size_t) k] < 0.5 || g[(size_t) k] < 25.0 || g[(size_t) k] > 16000.0) continue;
                    const double v = (r[(size_t) k - 1] + 2.0 * r[(size_t) k] + r[(size_t) k + 1]) * 0.25;
                    if (std::abs (v) > worst) { worst = std::abs (v); at = k; }
                }
                if (at < 0 || worst < 0.75)
                    break;

                // width at half the height, on the same side of zero
                const double peak = r[(size_t) at];
                int lo = at, hi = at;
                while (lo > 0 && r[(size_t) lo - 1] * peak > 0.0 && std::abs (r[(size_t) lo - 1]) > std::abs (peak) * 0.5) --lo;
                while (hi + 1 < (int) r.size() && r[(size_t) hi + 1] * peak > 0.0 && std::abs (r[(size_t) hi + 1]) > std::abs (peak) * 0.5) ++hi;
                const double bw = std::max (1.0, (double) (hi - lo + 1)) / 12.0;
                const double q = std::sqrt (std::pow (2.0, bw)) / (std::pow (2.0, bw) - 1.0);

                model.peaks.push_back ({ (float) g[(size_t) at], (float) juce::jlimit (0.5, 8.0, q),
                                         (float) juce::jlimit (-15.0, 10.0, peak) });
                peakV.push_back (peakVector (model.peaks.back()));
                refine (12);
            }
            refine (40);

            // drop the ones that ended up doing nothing
            std::vector<Band> keep;
            for (auto& p : model.peaks)
                if (std::abs (p.gainDb) >= 0.3f) keep.push_back (p);
            model.peaks = keep;
            rebuild();
        }

        /** Squared error, plus a small cost per dB of peak gain, so pairs of peaks don't fight each other. */
        double error() const
        {
            double cost = 0.0;
            for (auto& p : model.peaks) cost += (double) p.gainDb * p.gainDb;
            return errorOf (target, weight, total()) + 0.0005 * cost;
        }

    private:
        std::vector<double> target, weight;
        std::vector<double> baseV;
        std::vector<std::vector<double>> peakV;

        void rebuildBase()
        {
            const auto h = hpVector (model.hp, model.hpQ), s = shelfVector (model.boomHz, model.boomDb), t = topVector (model.top);
            baseV.resize (h.size());
            for (size_t k = 0; k < h.size(); ++k) baseV[k] = h[k] + s[k] + t[k];
        }

        void rebuild()
        {
            rebuildBase();
            peakV.clear();
            for (auto& p : model.peaks) peakV.push_back (peakVector (p));
        }

        std::vector<double> total() const
        {
            auto m = baseV;
            for (auto& v : peakV)
                for (size_t k = 0; k < m.size(); ++k) m[k] += v[k];
            return m;
        }

        /** Coordinate descent: nudge each value up and down, keep what helps, then take smaller steps. */
        void refine (int rounds)
        {
            double err = error();
            double freqStep = std::pow (2.0, 1.0 / 12.0), qStep = 1.25, gainStep = 1.0;

            for (int round = 0; round < rounds; ++round)
            {
                bool improved = false;

                auto tryValue = [&] (double& value, double candidate, double lo, double hi, bool isBase, size_t peakIndex)
                {
                    candidate = juce::jlimit (lo, hi, candidate);
                    if (std::abs (candidate - value) < 1.0e-9) return;
                    const double old = value;
                    value = candidate;
                    if (isBase) rebuildBase();
                    else
                    {
                        auto& p = model.peaks[peakIndex];
                        p = { (float) peakFreq[peakIndex], (float) peakQ[peakIndex], (float) peakGain[peakIndex] };
                        peakV[peakIndex] = peakVector (p);
                    }
                    const double e = error();
                    if (e < err - 1.0e-7) { err = e; improved = true; return; }
                    value = old;
                    if (isBase) rebuildBase();
                    else
                    {
                        auto& p = model.peaks[peakIndex];
                        p = { (float) peakFreq[peakIndex], (float) peakQ[peakIndex], (float) peakGain[peakIndex] };
                        peakV[peakIndex] = peakVector (p);
                    }
                };

                peakFreq.clear(); peakQ.clear(); peakGain.clear();
                for (auto& p : model.peaks) { peakFreq.push_back (p.freq); peakQ.push_back (p.q); peakGain.push_back (p.gainDb); }

                for (int dir : { 1, -1 })
                {
                    const double fm = dir > 0 ? freqStep : 1.0 / freqStep;
                    const double qm = dir > 0 ? qStep : 1.0 / qStep;
                    tryValue (model.hp, model.hp * fm, 15.0, 250.0, true, 0);
                    tryValue (model.hpQ, model.hpQ * qm, 0.4, 2.0, true, 0);
                    tryValue (model.boomHz, model.boomHz * fm, 30.0, 200.0, true, 0);
                    tryValue (model.boomDb, model.boomDb + dir * gainStep, -6.0, 18.0, true, 0);
                    tryValue (model.top, model.top * fm, 4000.0, 20000.0, true, 0);

                    for (size_t i = 0; i < model.peaks.size(); ++i)
                    {
                        tryValue (peakFreq[i], peakFreq[i] * fm, 25.0, 16000.0, false, i);
                        tryValue (peakQ[i], peakQ[i] * qm, 0.3, 12.0, false, i);
                        tryValue (peakGain[i], peakGain[i] + dir * gainStep, -15.0, 10.0, false, i);
                    }
                }

                if (! improved)
                {
                    freqStep = std::sqrt (freqStep);
                    qStep = std::sqrt (qStep);
                    gainStep *= 0.5;
                    if (gainStep < 0.05)
                        break;
                }
            }
        }

        std::vector<double> peakFreq, peakQ, peakGain;
    };

    /** A profile with flat speakers and cabin: what the rest of DDR's chain adds on its own. */
    CarProfile flatProfile (int baseCar)
    {
        CarProfile p {};
        p.lowCutHz = 5.0f; p.lowCutQ = 0.5f;
        for (auto& b : p.speakerBands) b = { 1000.0f, 1.0f, 0.0f };
        p.topHz = 24000.0f;
        p.boomHz = 60.0f; p.boomDb = 0.0f;
        for (auto& b : p.modes) b = { 100.0f, 1.0f, 0.0f };
        for (auto& b : p.extraBands) b = { 1000.0f, 1.0f, 0.0f };
        applyBaseCar (p, baseCar);
        return p;
    }

    std::vector<double> modelOnly (const CarProfile& p)
    {
        auto v = hpVector (p.lowCutHz, p.lowCutQ);
        const auto s = shelfVector (p.boomHz, p.boomDb), t = topVector (p.topHz);
        for (size_t k = 0; k < v.size(); ++k) v[k] += s[k] + t[k];
        return v;
    }
}

Fit fitProfile (const Target& target, int baseCar)
{
    const auto& g = grid();

    // What DDR adds by itself at the driver's seat (both speakers, reflections, loudness),
    // smoothed: the measurement already contains the real car's version of all that.
    const auto flat = flatProfile (baseCar);
    const auto flatChain = chainResponse (flat);
    const auto flatModel = modelOnly (flat);
    std::vector<float> extra (g.size());
    for (size_t k = 0; k < g.size(); ++k) extra[k] = (float) (flatChain[k] - flatModel[k]);
    extra = movingAverage (extra, 2);

    // Fit a slightly smoother curve than was measured: the fine ripple above a few hundred Hz
    // depends on exactly where the phone was, so it is not worth copying with filters.
    std::vector<double> t (g.size()), w (g.size());
    for (int k = 0; k < (int) g.size(); ++k)
    {
        const int radius = g[(size_t) k] < 300.0 ? 1 : 2;
        double sum = 0.0, sw = 0.0;
        for (int j = std::max (0, k - radius); j <= std::min ((int) g.size() - 1, k + radius); ++j)
        {
            sum += (target.weight[(size_t) j] + 1.0e-6) * target.db[(size_t) j];
            sw += target.weight[(size_t) j] + 1.0e-6;
        }
        t[(size_t) k] = sum / sw - extra[(size_t) k];
        w[(size_t) k] = target.weight[(size_t) k];
    }

    Fitter fitter (t, w);
    fitter.fitBase();
    fitter.fitPeaks (15);
    const auto& m = fitter.model;

    Fit fit;
    fit.baseCar = baseCar;
    auto& p = fit.profile;
    p = flat;
    p.lowCutHz = (float) m.hp;
    p.lowCutQ = (float) m.hpQ;
    p.topHz = (float) m.top;
    p.boomHz = (float) m.boomHz;
    p.boomDb = (float) m.boomDb;

    // The narrow low peaks become the cabin modes (so the Resonance knob scales them), the rest speaker bands
    auto peaks = m.peaks;
    std::sort (peaks.begin(), peaks.end(), [] (const Band& a, const Band& b) { return std::abs (a.gainDb) > std::abs (b.gainDb); });
    size_t modes = 0, bands = 0, extras = 0;
    for (auto& b : peaks)
    {
        if (b.freq < 250.0f && modes < p.modes.size()) p.modes[modes++] = b;
        else if (bands < p.speakerBands.size()) p.speakerBands[bands++] = b;
        else if (extras < p.extraBands.size()) p.extraBands[extras++] = b;
    }

    // Check the result through the real chain
    const auto chain = chainResponse (p);
    double sw = 0.0, swd = 0.0;
    for (size_t k = 0; k < g.size(); ++k) { sw += w[k]; swd += w[k] * (chain[k] - target.db[k]); }
    const double offset = sw > 0.0 ? swd / sw : 0.0;
    double se = 0.0;
    fit.modelDb.resize (g.size());
    for (size_t k = 0; k < g.size(); ++k)
    {
        fit.modelDb[k] = (float) (chain[k] - offset);
        const double d = chain[k] - offset - target.db[k];
        se += w[k] * d * d;
    }
    fit.errorDb = sw > 0.0 ? (float) std::sqrt (se / sw) : 0.0f;
    return fit;
}

//==============================================================================
juce::ValueTree toValueTree (const Fit& fit, const Target& target)
{
    const auto& p = fit.profile;
    juce::ValueTree t (myCarTag);
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("base", fit.baseCar, nullptr);
    t.setProperty ("lowCutHz", p.lowCutHz, nullptr);
    t.setProperty ("lowCutQ", p.lowCutQ, nullptr);
    t.setProperty ("topHz", p.topHz, nullptr);
    t.setProperty ("boomHz", p.boomHz, nullptr);
    t.setProperty ("boomDb", p.boomDb, nullptr);
    t.setProperty ("speakerBands", bandsToString (p.speakerBands), nullptr);
    t.setProperty ("modes", bandsToString (p.modes), nullptr);
    t.setProperty ("extraBands", bandsToString (p.extraBands), nullptr);
    t.setProperty ("target", toCsv (target.db), nullptr);
    t.setProperty ("weight", toCsv (target.weight), nullptr);
    t.setProperty ("model", toCsv (fit.modelDb), nullptr);
    t.setProperty ("errorDb", fit.errorDb, nullptr);
    t.setProperty ("studio", target.relativeToStudio, nullptr);
    t.setProperty ("date", juce::Time::getCurrentTime().formatted ("%Y-%m-%d"), nullptr);
    return t;
}

bool fromValueTree (const juce::ValueTree& t, CarProfile& p)
{
    if (! t.isValid() || ! t.hasType (myCarTag) || (int) t.getProperty ("version", 0) != 1)
        return false;

    CarProfile q {};
    q.lowCutHz = (float) t.getProperty ("lowCutHz", 70.0);
    q.lowCutQ  = (float) t.getProperty ("lowCutQ", 0.9);
    q.topHz    = (float) t.getProperty ("topHz", 16000.0);
    q.boomHz   = (float) t.getProperty ("boomHz", 60.0);
    q.boomDb   = (float) t.getProperty ("boomDb", 6.0);
    if (! (q.lowCutHz >= 5.0f && q.lowCutHz <= 400.0f && q.lowCutQ > 0.1f && q.lowCutQ < 5.0f
           && q.topHz >= 2000.0f && q.topHz <= 24000.0f && q.boomHz >= 20.0f && q.boomHz <= 300.0f
           && std::abs (q.boomDb) <= 30.0f))
        return false;
    if (! bandsFromString (t.getProperty ("speakerBands").toString(), q.speakerBands)
        || ! bandsFromString (t.getProperty ("modes").toString(), q.modes)
        || ! bandsFromString (t.getProperty ("extraBands").toString(), q.extraBands))
        return false;

    applyBaseCar (q, (int) t.getProperty ("base", 1));
    p = q;
    return true;
}

std::vector<float> curveFromTree (const juce::ValueTree& t, const char* name)
{
    auto v = fromCsv (t.getProperty (name).toString());
    if (v.size() != grid().size()) v.clear();
    return v;
}
} // namespace ddr::cal
