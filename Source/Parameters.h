#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace ddr
{
namespace ParamID
{
    // Basic
    inline constexpr const char* carMode     = "carMode";
    inline constexpr const char* car         = "car";
    inline constexpr const char* speed       = "speed";
    inline constexpr const char* road        = "road";
    inline constexpr const char* output      = "output";
    inline constexpr const char* mono        = "mono";
    inline constexpr const char* levelMatch  = "levelMatch";

    // Advanced: driving & noise
    inline constexpr const char* engine      = "engine";
    inline constexpr const char* window      = "window";
    inline constexpr const char* fan         = "fan";
    inline constexpr const char* noiseAmount = "noiseAmount";
    inline constexpr const char* noiseOnly   = "noiseOnly";

    // Advanced: cabin
    inline constexpr const char* boom        = "boom";
    inline constexpr const char* resonance   = "resonance";
    inline constexpr const char* reflections = "reflections";
    inline constexpr const char* seat        = "seat";
    inline constexpr const char* rhd         = "rhd";

    // Advanced: speakers
    inline constexpr const char* lowCut      = "lowCut";
    inline constexpr const char* distortion  = "distortion";
    inline constexpr const char* worn        = "worn";
    inline constexpr const char* source      = "source";
    inline constexpr const char* fader       = "fader";

    // Advanced: head unit
    inline constexpr const char* volume      = "volume";
    inline constexpr const char* loudness    = "loudness";
    inline constexpr const char* svc         = "svc";
    inline constexpr const char* bass        = "bass";
    inline constexpr const char* treble      = "treble";
    inline constexpr const char* limiter     = "limiter";
}

enum class Engine { petrol, diesel, electric };
enum class Road   { smooth, rough, studded, gravel, wet };
enum class Seat   { driver, passenger, backSeat, sweetSpot };
enum class Source { usb, bluetooth, fm };

struct Band { float freq, q, gainDb; };

/** Everything that defines one car type. The Advanced knobs scale or offset these. */
struct CarProfile
{
    const char* name;
    // speakers
    float lowCutHz, lowCutQ;
    std::array<Band, 4> speakerBands;
    float topHz;
    float coneDrive;       // 0..1 how easily the cones distort
    bool  worn;
    // cabin
    float boomHz, boomDb;
    std::array<Band, 3> modes;
    float reflections;     // 0..1
    float widthMetres;     // distance between the front door speakers
    // noise
    float insulationDb;    // + = louder cabin
    bool  rearSpeakers;    // rear door speakers as well as front ones
};

const std::array<CarProfile, 6>& carProfiles();
juce::StringArray carNames();
juce::StringArray roadNames();
juce::StringArray engineNames();
juce::StringArray seatNames();
juce::StringArray sourceNames();
juce::StringArray svcNames();

/** Situation presets, exposed to the host as programs. */
struct Situation
{
    const char* name;
    float speed;
    Road road;
    int fan;
    bool window;
    Seat seat;
};

const std::array<Situation, 6>& situations();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace ddr
