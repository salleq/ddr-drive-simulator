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

    int   volume   = 20;         // 0..40, car volume steps
    bool  loudness = true;
    int   svc      = 1;          // 0 off, 1 low, 2 high
    float bassDb   = 0.0f;
    float trebleDb = 0.0f;
    bool  limiter  = true;
};
} // namespace ddr
