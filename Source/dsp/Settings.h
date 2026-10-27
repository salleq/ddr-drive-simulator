#pragma once

#include "../Parameters.h"

namespace ddr
{
/** A snapshot of every parameter, read once per block on the audio thread. */
struct Settings
{
    const CarProfile* car = nullptr;

    float  speedKmh    = 100.0f;
    Road   road        = Road::rough;
    Engine engine      = Engine::petrol;
    bool   window      = false;
    int    fan         = 2;
    float  noiseAmount = 1.0f;   // 0..1.5
    bool   noiseOnly   = false;

    float boom        = 1.0f;    // 0..2, scales the car's own value
    float resonance   = 1.0f;    // 0..2
    float reflections = 1.0f;    // 0..2
    Seat  seat        = Seat::driver;
    bool  rhd         = false;

    float  lowCutOct  = 0.0f;    // -1..+1 octave shift of the speaker roll-off
    float  distortion = 1.0f;    // 0..2
    bool   worn       = false;
    Source source     = Source::usb;
    float  fader      = 0.0f;    // -1 front only .. +1 rear only

    int   volume   = 20;         // 0..40, car volume steps
    bool  loudness = true;
    int   svc      = 1;          // 0 off, 1 low, 2 high
    float bassDb   = 0.0f;
    float trebleDb = 0.0f;
    bool  limiter  = true;

    // Exact comparison on purpose: it only decides whether the filters need recomputing.
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
    bool operator== (const Settings& o) const noexcept
    {
        return car == o.car && speedKmh == o.speedKmh && road == o.road && engine == o.engine
            && window == o.window && fan == o.fan && noiseAmount == o.noiseAmount && noiseOnly == o.noiseOnly
            && boom == o.boom && resonance == o.resonance && reflections == o.reflections && seat == o.seat
            && rhd == o.rhd && lowCutOct == o.lowCutOct && distortion == o.distortion && worn == o.worn
            && source == o.source && fader == o.fader && volume == o.volume && loudness == o.loudness && svc == o.svc
            && bassDb == o.bassDb && trebleDb == o.trebleDb && limiter == o.limiter;
    }
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    bool operator!= (const Settings& o) const noexcept { return ! (*this == o); }
};
} // namespace ddr
