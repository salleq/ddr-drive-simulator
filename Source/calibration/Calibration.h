#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "../Parameters.h"
#include <vector>

/** "My Car": measuring your own car with a phone.

    1. DDR writes a test signal: pink noise (to set the volume), then three slow sine sweeps.
    2. You play it in the car and record it with a phone at the driver's head.
       Optionally you record it the same way at your mixing position in the studio.
    3. DDR finds the sweeps in the recording, turns them into the car's frequency response,
       and (with the studio recording) divides out the phone microphone and your monitors.
    4. The response is fitted with the same filters DDR's car presets use, so the result is a
       normal car profile: the Advanced controls, the noise and the seat still work on it. */
namespace ddr::cal
{
struct TestSignal
{
    static constexpr double f1 = 20.0, f2 = 20000.0;  // sweep range
    static constexpr double sweepSeconds = 10.0;
    static constexpr double period = 12.5;            // sweep start to next sweep start
    static constexpr int    numSweeps = 3;
    static constexpr double leadSilence = 0.5, noiseSeconds = 3.0, preSweepSilence = 1.5, tailSilence = 1.5;
    static constexpr float  sweepLevel = 0.35f;       // about -9 dBFS peak
    static constexpr double sampleRate = 44100.0;     // what every car stereo can play
};

/** The whole test signal, stereo (both channels the same). */
juce::AudioBuffer<float> makeTestSignal (double sampleRate = TestSignal::sampleRate);

/** One sweep with its fades, exactly as it appears in the test signal. */
std::vector<float> makeSweep (double sampleRate);

/** Writes the test signal as a 16-bit WAV file. */
bool writeTestSignal (const juce::File& file);

/** The frequency grid every curve uses: 1/12 octave from 20 Hz to 20 kHz. */
const std::vector<double>& grid();

/** What came out of one recording. */
struct Measurement
{
    bool ok = false;
    juce::String message;            // error, or a short summary for the user
    bool warning = false;            // ok, but something looks off (noisy, clipping, sweeps missing)
    int sweepsFound = 0;
    float snrDb = 0.0f;
    std::vector<float> levelDb;      // per grid point, 0 dB = average of 200 Hz - 4 kHz
    std::vector<float> snrPerPoint;  // per grid point, dB
};

Measurement analyse (const float* mono, int numSamples, double sampleRate);
Measurement analyseFile (const juce::File& file);

/** Smoothed level of an impulse response on the grid, in dB (not normalised). Smoothing in octaves. */
std::vector<float> responseOnGrid (const float* ir, int numSamples, double sampleRate, double smoothingOctaves = 1.0 / 6.0);

/** The curve the car profile is fitted to. Weight 0 = don't trust this frequency. */
struct Target
{
    std::vector<float> db, weight;
    bool relativeToStudio = false;
};

Target makeTarget (const Measurement& car, const Measurement* studio);

/** A fitted car profile, plus what it looks like. */
struct Fit
{
    CarProfile profile {};
    int baseCar = 1;                 // the preset the noise and cabin size come from
    std::vector<float> modelDb;      // DDR's own response with this profile, driver's seat
    float errorDb = 0.0f;            // RMS difference to the target where it is trusted
};

/** Fits DDR's speaker and cabin filters to the target. Takes well under a second. */
Fit fitProfile (const Target& target, int baseCar);

/** Response of DDR's music chain for a profile (default settings, driver's seat), on the grid. */
std::vector<float> chainResponse (const CarProfile& profile);

/** Swaps the base car (noise, cabin size) of an existing fit without refitting the speakers. */
void applyBaseCar (CarProfile& profile, int baseCar);

//==============================================================================
/** Saving the result in the plugin state. */
inline const juce::Identifier myCarTag { "MyCar" };

juce::ValueTree toValueTree (const Fit& fit, const Target& target);
bool fromValueTree (const juce::ValueTree& tree, CarProfile& profile);

/** A stored curve ("target", "weight" or "model"), or empty if missing. */
std::vector<float> curveFromTree (const juce::ValueTree& tree, const char* name);
} // namespace ddr::cal
