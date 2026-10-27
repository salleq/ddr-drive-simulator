#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/MusicChain.h"
#include "dsp/NoiseGenerator.h"
#include "calibration/Calibration.h"
#include "dsp/Places.h"
#include "MixHints.h"
#include "reference/ReferencePlayer.h"
#include <atomic>

class DDRAudioProcessor : public juce::AudioProcessor
{
public:
    DDRAudioProcessor();
    ~DDRAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DDR Drive Simulator"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Music and noise level per band (low, mid, high) in dBFS, for the editor's meter. */
    struct BandMeter { std::atomic<float> music { -100.0f }, noise { -100.0f }; };
    std::array<BandMeter, 3> meters;

    /** How much level matching turns the car (or device) up or down right now, in dB. */
    std::atomic<float> matchDb { 0.0f };

    /** Band levels for the mix hints, before level matching. */
    ddr::HintLevels getHintLevels() const;

    /** The listening place that is playing now. */
    ddr::Place getPlace() const noexcept { return (ddr::Place) activePlace.load(); }

    /** A snapshot of all parameters (any thread). */
    ddr::Settings readSettings() const;

    /** "My Car" (message thread only): installs a measured car, stores it in the project and as the
        default for new DDR instances. An invalid tree removes it from this project. */
    void setMyCar (const juce::ValueTree& tree);
    bool hasMyCar() const noexcept { return myCarValid.load(); }
    juce::ValueTree getMyCar() const { return apvts.state.getChildWithName (ddr::cal::myCarTag); }

    /** Where the last measured car is kept for new projects. */
    static juce::File myCarFile();
    static inline bool useMyCarFile = true;   // the test tools switch this off

    /** Reference tracks: they play through DDR in place of the mix. */
    ddr::ReferencePlayer references;

    /** The mix's loudness (LUFS, about the last 10 s it played), or below -90 when not heard yet. */
    float getMixLoudness() const noexcept { return mixLufs.load(); }

private:
    void loadMyCar();

    // The measured car, double buffered: the audio thread reads one slot while the other is rewritten
    std::array<ddr::CarProfile, 2> myCarSlots {};
    std::atomic<int>  myCarSlot { 0 };
    std::atomic<bool> myCarValid { false };
    std::atomic<int>  myCarVersion { 0 };

    ddr::Settings smoothed, lastApplied;
    ddr::MusicChain music;
    ddr::NoiseGenerator noise;
    ddr::PlaceChain placeChain;
    ddr::AmbientNoise ambient;
    std::atomic<int> activePlace { 0 };
    float switchGain = 1.0f, switchStep = 0.0f;
    int fastMatchSamples = 0;   // after a place switch the level match adapts quickly

    // K-weighting (as in loudness meters) for the level match, per channel: dry and wet
    std::array<std::array<ddr::Biquad, 2>, 2> kDry, kWet;

    // mix hints
    ddr::HintFilters hintDryF, hintMusicF, hintNoiseF;
    std::array<double, ddr::numHintBands> hintDryPow {}, hintMusicPow {}, hintNoisePow {};
    double hintDryTotal = 0.0, hintMusicTotal = 0.0;
    std::array<std::atomic<float>, ddr::numHintBands> hintDry, hintMusic, hintNoise;
    std::atomic<float> hintDryTotalDb { -100.0f }, hintMusicTotalDb { -100.0f };

    juce::AudioBuffer<float> dry, wet, noiseBuf;

    // the mix's own loudness, measured while no reference plays
    double mixPower = 0.0, mixSeconds = 0.0;
    std::atomic<float> mixLufs { -100.0f };
    bool refWasActive = false;

    double sr = 44100.0;
    double inPower = 0.0, wetPower = 0.0, powerCoef = 0.0;
    float matchGain = 1.0f, matchTarget = 1.0f;
    float carMix = 1.0f, carMixStep = 0.0f;
    float noiseFade = 1.0f;
    float outGain = 1.0f;

    std::array<ddr::Biquad, 4> musicBands, noiseBands; // low, mid(hp), mid(lp), high
    std::array<double, 3> musicBandPow {}, noiseBandPow {};

    int currentProgram = 2;

    // cached raw parameter pointers (read on the audio thread)
    struct Raw
    {
        std::atomic<float> *carMode, *car, *speed, *road, *output, *mono, *levelMatch, *place,
                           *engine, *window, *fan, *noiseAmount, *noiseOnly,
                           *boom, *resonance, *reflections, *seat, *rhd,
                           *lowCut, *distortion, *worn, *source, *fader,
                           *volume, *loudness, *svc, *bass, *treble, *limiter;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DDRAudioProcessor)
};
