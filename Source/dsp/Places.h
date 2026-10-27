#pragma once

#include "Biquad.h"
#include <array>
#include <cstdint>

namespace ddr
{
/** Where you listen. "Car" uses the full car simulation, the others are small devices. */
enum class Place { car, phone, earbuds, btSpeaker };
inline constexpr int numPlaces = 4;

/** Music through a small device: a phone speaker, cheap earbuds or a mono Bluetooth speaker. */
class PlaceChain
{
public:
    void prepare (double sampleRate);
    void reset();
    void setPlace (Place p);

    void process (float* left, float* right, int numSamples) noexcept;

private:
    struct Stereo { Biquad l, r; void reset() { l.reset(); r.reset(); } };

    Place place = Place::phone;
    std::array<Stereo, 7> eq;   // unused stages are set to identity
    bool  mono = true;

    // compressor (phone DSP, Bluetooth bass ducking)
    bool  compOn = false, bassKeyed = false;
    float threshold = 1.0f, ratio = 1.0f, makeup = 1.0f;
    float env = 0.0f, attack = 0.0f, release = 0.0f;
    Biquad keyLp;

    float drive = 0.0f;         // small-driver distortion, 0 = clean
    double sr = 44100.0;
};

/** Background noise for the device places: a kitchen, a bus, a party. */
class AmbientNoise
{
public:
    void prepare (double sampleRate);
    void reset();
    void setPlace (Place p, float amount);

    /** Writes (does not add) the noise into the two buffers. */
    void process (float* left, float* right, int numSamples) noexcept;

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
    struct Channel
    {
        Rng rng;
        Pink pink;
        Biquad bedHp, bedLp, rumbleLp, voiceA, voiceB, voiceC;
        std::array<float, 3> voiceEnv {}, voiceTarget {};
        std::array<int, 3> voiceCountdown {};
        void reset();
    };

    void configure (Channel& c, Place p) const;
    float runChannel (Channel& c, int part, int numSamples, double& sum) noexcept;  // test signal for the normalisation
    void measureNormalisation();

    std::array<Channel, 2> ch;
    Place place = Place::phone;
    float bedGain = 0.0f, rumbleGain = 0.0f, voiceGain = 0.0f, humGain = 0.0f;
    float level = 0.0f, levelTarget = 0.0f, rampCoef = 0.0f, envCoef = 0.0f;
    double humPhase = 0.0, humHz = 50.0;
    std::array<std::array<float, 3>, numPlaces> norm {};   // bed, rumble, voices: gain for unit RMS
    double sr = 44100.0;
};

const char* placeNoiseName (Place p);   // what the background noise is, for the hints
} // namespace ddr
