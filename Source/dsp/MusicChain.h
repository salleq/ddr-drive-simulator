#pragma once

#include "Biquad.h"
#include "Settings.h"
#include <array>
#include <vector>

namespace ddr
{
/** The music path of the car: source -> head unit -> speakers -> cabin and seat.
    Road noise is added later by the processor, because it does not go through the stereo. */
class MusicChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Recomputes all filters from the settings. Cheap enough to call once per block. */
    void update (const Settings& s);

    void process (float* left, float* right, int numSamples) noexcept;

private:
    struct Stereo { Biquad l, r; void reset() { l.reset(); r.reset(); } };

    // Source
    Stereo srcLp1, srcLp2, srcShelf;
    int    srcFilters = 0;     // how many of the above are active
    float  fmWidth = 1.0f, fmHiss = 0.0f;
    uint32_t rng = 0x12345678u;

    // Head unit
    Stereo huBass, huTreble, huLoudLow, huLoudHigh;
    float  huGain = 1.0f, huGainSmoothed = 1.0f;
    bool   limiterOn = true;
    float  limEnv = 1.0f, limAttack = 0.0f, limRelease = 0.0f;

    // Speakers
    Stereo spkHp1, spkHp2, spkTop, spkWorn;
    std::array<Stereo, 4> spkBands;
    std::array<Stereo, 8> spkExtra;  // only a measured car uses these
    int    numExtra = 0;
    Stereo spkLowBand;         // band that the cone distortion acts on
    float  coneK = 1.0f, coneMix = 0.0f, coneEven = 0.0f;
    OnePole coneLpL1, coneLpL2, coneLpR1, coneLpR2;   // keep the distortion products low, like a real cone
    OnePole coneDcL, coneDcR;                        // removes the offset the even harmonics make
    float  rattleAmount = 0.0f, rattleThreshold = 0.30f;
    Biquad rattleBpL, rattleBpR;
    float  scratchAmount = 0.0f;                     // torn cone, only with "Worn Speakers"
    Biquad scratchBpL, scratchBpR;

    /** What the cone adds to the clean bass: soft compression with a little asymmetry. */
    inline float coneError (float x, OnePole& lp1, OnePole& lp2, OnePole& dc) noexcept
    {
        const float y = std::tanh (coneK * x) / coneK;
        float err = y - x + coneEven * (coneK - 1.0f) * y * y;
        err -= dc.process (err);
        return lp2.process (lp1.process (err));
    }

    // Cabin and seat
    Stereo cabBoom, cabSeatShelf;
    std::array<Stereo, 3> cabModes;
    std::vector<float> delayL, delayR;
    int    writePos = 0, delayMask = 0;
    int    seatDelayL = 0, seatDelayR = 0;
    float  seatGainL = 1.0f, seatGainR = 1.0f, crossfeed = 0.45f;
    int    rearDelayL = 0, rearDelayR = 0;
    float  rearGainL = 0.0f, rearGainR = 0.0f;   // includes the fader; 0 = no rear speakers
    float  frontLevel = 1.0f;
    OnePole rearLpL, rearLpR;                    // rear speakers heard through the seat backs
    static constexpr int numTaps = 5;
    std::array<int, numTaps>   tapL {}, tapR {};
    std::array<float, numTaps> tapGain {};
    float  reflectionGain = 0.0f;
    OnePole reflLpL, reflLpR;

    double sr = 44100.0;

    inline float nextNoise() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (int32_t) rng * (1.0f / 2147483648.0f);
    }
};
} // namespace ddr
