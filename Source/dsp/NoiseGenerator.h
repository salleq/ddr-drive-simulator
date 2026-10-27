#pragma once

#include "Biquad.h"
#include "Settings.h"
#include <array>

namespace ddr
{
/** Synthesises what you hear in the cabin besides the music: road rumble, tyre noise,
    surface hiss or gravel, wind, engine and the ventilation fan.

    Levels are set in "dB SPL" and mapped to digital level with 95 dB SPL = 0 dBFS,
    which puts a typical master (about -12 dBFS RMS) at a car-like 83 dB SPL. */
class NoiseGenerator
{
public:
    void prepare (double sampleRate);
    void reset();
    void update (const Settings& s);

    /** Writes (does not add) the noise into the two buffers. */
    void process (float* left, float* right, int numSamples) noexcept;

    static constexpr float splAtZeroDbfs = 95.0f;

private:
    struct Rng
    {
        uint32_t state;
        inline float next() noexcept
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            return (float) (int32_t) state * (1.0f / 2147483648.0f);
        }
    };

    struct Pink
    {
        float b0 = 0, b1 = 0, b2 = 0;
        inline float process (float w) noexcept
        {
            b0 = 0.99765f * b0 + w * 0.0990460f;
            b1 = 0.96300f * b1 + w * 0.2965164f;
            b2 = 0.57000f * b2 + w * 1.0526913f;
            return (b0 + b1 + b2 + w * 0.1848f) * 0.2f;
        }
    };

    enum Component { rumble, tyre, hiss, wind, rush, engineNoise, fan, crackle, numComponents };

    struct Channel
    {
        Rng rng;
        Pink pinkA, pinkB, pinkC;
        Biquad rumbleLp, rumbleHp, tyreBp, hissA, hissB, windBp, rushBp, engBp, fanBp, crackleBp;
        void reset();
    };

    void configureFilters (Channel& c, Road road) const;
    void measureNormalisation();

    std::array<Channel, 2> ch;
    Rng ctrlRng { 0xC0FFEEu };

    // per component: normalisation so that gain 1 = 0 dBFS RMS, and the current/target gains
    std::array<std::array<float, numComponents>, 5> norm {};   // by road surface
    std::array<float, numComponents> gain {}, target {};

    Road road = Road::rough;
    bool roadChanged = true;

    // engine
    Engine engine = Engine::petrol;
    float rpmTarget = 800.0f, rpm = 800.0f, rpmSmooth = 0.0f;
    double phase = 0.0;
    float engineToneGain = 0.0f, engineToneTarget = 0.0f;
    std::array<float, 6> harmonics {};
    float clatter = 0.0f;
    float whine = 0.0f;
    double whinePhase = 0.0;

    // window buffeting
    bool window = false;
    double buffetPhaseA = 0.0, buffetPhaseB = 0.0;

    // gravel stones
    float crackleRate = 0.0f;   // impulses per sample

    // slow changes in the road texture, so the noise is never a static hiss
    float textureTarget = 1.0f, texture = 1.0f, textureCoef = 0.0f;
    int   textureCountdown = 0;

    // expansion joints and patches: a double thump, front axle then rear axle
    bool  jointsEnabled = false;
    float jointGain = 0.0f;
    int   jointCountdown = 0;
    int   jointSecondHit = -1;      // samples until the rear axle hits, -1 = none pending
    float jointEnv = 0.0f, jointDecay = 0.0f;
    double jointPhase = 0.0;
    float wheelbaseDelaySec = 0.0f;
    int nextJointInterval() noexcept;

    float wander = 1.0f, wanderTarget = 1.0f;
    int   wanderCountdown = 0;

    float rampCoef = 0.0f;
    double sr = 44100.0;
};
} // namespace ddr
