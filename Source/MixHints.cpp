#include "MixHints.h"
#include <algorithm>
#include <cmath>

namespace ddr
{
namespace
{
    struct BandEdges { double lo, hi; };
    constexpr BandEdges edges[numHintBands] { { 25.0, 60.0 }, { 60.0, 200.0 }, { 250.0, 1000.0 }, { 1000.0, 4000.0 }, { 6000.0, 12000.0 } };

    constexpr float maskedDb = 3.0f;    // a band this close to the noise (or under it) is masked
    constexpr float tightDb  = 8.0f;

    int roundTo5 (float v) { return juce::roundToInt (v / 5.0f) * 5; }

    const char* whereFor (Place p)
    {
        switch (p)
        {
            case Place::car:       return "in this car";
            case Place::phone:     return "on a phone speaker";
            case Place::earbuds:   return "in cheap earbuds";
            case Place::btSpeaker: return "on a Bluetooth speaker";
        }
        return "";
    }
}

void HintFilters::prepare (double sr)
{
    for (size_t b = 0; b < (size_t) numHintBands; ++b)
    {
        hp[b].setHighPass (sr, edges[b].lo, 0.707);
        lp[b].setLowPass (sr, edges[b].hi, 0.707);
    }
    reset();
}

void HintFilters::reset()
{
    for (auto& f : hp) f.reset();
    for (auto& f : lp) f.reset();
}

void HintEngine::updateCurve (const Settings& s)
{
    constexpr double rate = 32000.0;
    if (! prepared)
    {
        offline.prepare (rate);
        prepared = true;
    }

    const int settle = (int) (0.3 * rate), measure = (int) (0.5 * rate), block = 256;
    std::array<float, block> l {}, r {};
    HintFilters filters;

    for (size_t k = 0; k < curve.size(); ++k)
    {
        auto at = s;
        at.speedKmh = (float) k * speedStep;
        at.noiseOnly = false;
        offline.update (at);
        offline.reset();
        filters.prepare (rate);

        std::array<double, numHintBands> pow {};
        for (int done = 0; done < settle + measure; done += block)
        {
            offline.process (l.data(), r.data(), block);
            std::array<double, numHintBands> blockPow {};
            for (int i = 0; i < block; ++i)
                filters.process (0.5f * (l[(size_t) i] + r[(size_t) i]), blockPow);
            if (done >= settle)
                for (size_t b = 0; b < pow.size(); ++b) pow[b] += blockPow[b];
        }
        for (size_t b = 0; b < pow.size(); ++b)
            curve[k][b] = (float) (10.0 * std::log10 (pow[b] / measure + 1.0e-12));
    }

    curveKey = s;
    curveKey.speedKmh = 0.0f;
    curveValid = true;
}

std::vector<Hint> HintEngine::make (const HintLevels& lv, const Settings& s, Place place)
{
    if (lv.musicTotal < -70.0f || lv.dryTotal < -70.0f)
        return { { 0, "Play your mix to get hints." } };

    const bool car = place == Place::car;
    const juce::String where = whereFor (place);
    const juce::String noiseName = placeNoiseName (place);

    if (car)
    {
        auto key = s;
        key.speedKmh = 0.0f;
        if (! curveValid || key != curveKey)
            updateCurve (s);
    }

    // Speed volume turns the music up as the car goes faster; the noise curve does not include it
    const float svcDbPerKmh = s.svc == 0 ? 0.0f : (s.svc == 1 ? 0.035f : 0.07f);
    const float vNow = s.speedKmh;

    auto noiseAt = [this] (size_t b, float v)
    {
        const float x = juce::jlimit (0.0f, (float) (curve.size() - 1), v / speedStep);
        const size_t i = juce::jmin ((size_t) x, curve.size() - 2);
        const float t = x - (float) i;
        return curve[i][b] + t * (curve[i + 1][b] - curve[i][b]);
    };
    auto marginAt = [&] (size_t b, float v)
    {
        if (! car)
            return lv.music[b] - lv.noise[b];
        return lv.music[b] + svcDbPerKmh * (v - vNow) - noiseAt (b, v);
    };
    // the lowest speed where the band goes under, or -1 if it stays clear up to 140 km/h
    auto maskSpeed = [&] (size_t b) -> float
    {
        float prev = marginAt (b, 0.0f);
        if (prev < maskedDb)
            return 0.0f;
        for (size_t k = 1; k < curve.size(); ++k)
        {
            const float v = (float) k * speedStep, m = marginAt (b, v);
            if (m < maskedDb)
                return v - speedStep + speedStep * (prev - maskedDb) / juce::jmax (0.01f, prev - m);
            prev = m;
        }
        return -1.0f;
    };
    // how much the band changes compared with the low mids (250 Hz - 1 kHz)
    auto tilt = [&] (size_t b) { return (lv.music[b] - lv.dry[b]) - (lv.music[midBand] - lv.dry[midBand]); };
    auto present = [&] (size_t b) { return lv.dry[b] > lv.dryTotal - 25.0f; };

    std::vector<Hint> hints;
    auto add = [&hints] (int severity, const juce::String& text) { hints.push_back ({ severity, text }); };

    // masking, the band at a time
    struct Part { size_t band; const char* name; const char* fix; int severity; };
    const Part parts[] {
        { vocalBand, "Vocals (1-4 kHz)", "Lift the vocal or its presence a little.", 2 },
        { bassBand,  "Kick and bass (60-200 Hz)", "Harmonics above 200 Hz help them through.", 2 },
        { airBand,   "Hi-hats and air (6-12 kHz)", "", 1 },
    };
    for (const auto& part : parts)
    {
        if (! present (part.band))
            continue;

        if (car)
        {
            const float from = maskSpeed (part.band);
            if (from >= 0.0f && from <= vNow + 0.5f)
            {
                juce::String t = juce::String (part.name) + " are masked by road noise at " + juce::String (juce::roundToInt (vNow)) + " km/h";
                if (from < vNow - 7.0f)
                    t << " (from about " << roundTo5 (from) << " km/h)";
                t << ".";
                if (*part.fix != 0) t << " " << part.fix;
                add (part.severity, t);
            }
            else if (from >= 0.0f && part.band != airBand)
                add (1, juce::String (part.name) + " get masked from about " + juce::String (roundTo5 (from)) + " km/h.");
            else if (part.band == vocalBand)
                add (0, "Vocals stay clear of the road noise even at 140 km/h.");
        }
        else
        {
            const float m = marginAt (part.band, 0.0f);
            if (m < maskedDb)
            {
                juce::String t = juce::String (part.name) + " are masked by the " + noiseName + ".";
                if (*part.fix != 0) t << " " << part.fix;
                add (part.severity, t);
            }
            else if (m < tightDb && part.band == vocalBand)
                add (1, "Vocals only just make it over the " + noiseName + ".");
        }
    }

    // what the speakers and the cabin do to the balance
    if (present (bassBand) && tilt (bassBand) < -12.0f)
        add (2, "Bass below 200 Hz nearly vanishes " + where + ". Does the bass line still come through its upper harmonics?");
    else if (present (subBand) && tilt (subBand) < -10.0f)
        add (1, "Most of the sub bass below 60 Hz is lost " + where + ". Check the bass line still reads without it.");

    if (present (bassBand) && tilt (bassBand) > 6.0f)
        add (0, juce::String (car ? "The cabin boom" : "The speaker's bass bump") + " lifts kick and bass by about "
                    + juce::String (juce::roundToInt (tilt (bassBand))) + " dB " + where
                    + ". Don't thin out the low end just for this.");

    if (present (vocalBand) && tilt (vocalBand) > 4.0f)
        add (1, "1-4 kHz comes out about " + juce::String (juce::roundToInt (tilt (vocalBand))) + " dB louder " + where
                    + ": listen for harsh vocals and sharp esses.");

    std::stable_sort (hints.begin(), hints.end(), [] (const Hint& a, const Hint& b) { return a.severity > b.severity; });
    if (hints.empty())
        hints.push_back ({ 0, "Everything stays above the " + noiseName + ". Nice!" });
    if (hints.size() > 2)
        hints.resize (2);
    return hints;
}
} // namespace ddr
