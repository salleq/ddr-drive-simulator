#pragma once

#include <juce_core/juce_core.h>
#include "dsp/Biquad.h"
#include "dsp/NoiseGenerator.h"
#include "dsp/Places.h"
#include "dsp/Settings.h"
#include <array>
#include <vector>

namespace ddr
{
/** The parts of a mix the hints talk about. */
enum HintBand { subBand, bassBand, midBand, vocalBand, airBand, numHintBands };   // mid is the reference for the balance

/** Splits a signal into the hint bands (12 dB/oct on both sides, good enough for a hint). */
struct HintFilters
{
    void prepare (double sr);
    void reset();
    /** Adds each band's squared output to pow. */
    inline void process (float x, std::array<double, numHintBands>& pow) noexcept
    {
        for (size_t b = 0; b < (size_t) numHintBands; ++b)
        {
            const float y = lp[b].process (hp[b].process (x));
            pow[b] += (double) y * y;
        }
    }

    std::array<Biquad, numHintBands> hp, lp;
};

/** What the processor measured, all before level matching, in dBFS. */
struct HintLevels
{
    std::array<float, numHintBands> dry {}, music {}, noise {};
    float dryTotal = -100.0f, musicTotal = -100.0f;
};

struct Hint
{
    int severity;        // 2 = fix this, 1 = worth a look, 0 = good news / info
    juce::String text;
};

/** Turns the band levels into short mix hints, like "Vocals get masked from about 120 km/h".
    Message thread only. In a car it renders the road noise offline at other speeds, so it can
    tell at which speed each part of the mix goes under. */
class HintEngine
{
public:
    std::vector<Hint> make (const HintLevels& levels, const Settings& settings, Place place);

    /** Noise level per band at each speed in speedGrid (dBFS), for the current car and road. */
    const std::array<std::array<float, numHintBands>, 15>& noiseBySpeed() const { return curve; }
    static constexpr float speedStep = 10.0f;

private:
    void updateCurve (const Settings& s);

    NoiseGenerator offline;
    bool prepared = false, curveValid = false;
    Settings curveKey;
    std::array<std::array<float, numHintBands>, 15> curve {};
};
} // namespace ddr
