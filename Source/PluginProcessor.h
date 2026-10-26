#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/MusicChain.h"
#include "dsp/NoiseGenerator.h"
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

private:
    ddr::Settings readSettings() const;

    ddr::Settings smoothed, lastApplied;
    ddr::MusicChain music;
    ddr::NoiseGenerator noise;

    juce::AudioBuffer<float> dry, wet, noiseBuf;

    double sr = 44100.0;
    double inPower = 0.0, wetPower = 0.0, powerCoef = 0.0;
    float matchGain = 1.0f, matchTarget = 1.0f;
    float carMix = 1.0f, carMixStep = 0.0f;
    float outGain = 1.0f;

    std::array<ddr::Biquad, 4> musicBands, noiseBands; // low, mid(hp), mid(lp), high
    std::array<double, 3> musicBandPow {}, noiseBandPow {};

    int currentProgram = 2;

    // cached raw parameter pointers (read on the audio thread)
    struct Raw
    {
        std::atomic<float> *carMode, *car, *speed, *road, *output, *mono, *levelMatch,
                           *engine, *window, *fan, *noiseAmount, *noiseOnly,
                           *boom, *resonance, *reflections, *seat, *rhd,
                           *lowCut, *distortion, *worn, *source, *fader,
                           *volume, *loudness, *svc, *bass, *treble, *limiter;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DDRAudioProcessor)
};
