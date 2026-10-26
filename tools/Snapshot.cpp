// Renders the DDR editor to PNG files, and prints a few level measurements.
// Usage: ddr_snapshot <output-folder>

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    /** A crude stand-in for a mastered song: bass line, kick, a mid-range "vocal" and hats. */
    void fillTestSong (juce::AudioBuffer<float>& b, double sr, int64_t start)
    {
        juce::Random rng ((juce::int64) start);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = (double) (start + i) / sr;
            const double beat = std::fmod (t, 0.5);
            const double kick = std::exp (-beat * 30.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 80.0 * std::exp (-beat * 40.0)) * beat);
            const double bass = 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * t);
            const double vocal = 0.12 * std::sin (2.0 * juce::MathConstants<double>::pi * (440.0 + 20.0 * std::sin (t * 5.0)) * t)
                               + 0.06 * std::sin (2.0 * juce::MathConstants<double>::pi * 1320.0 * t);
            const double hat = std::fmod (t, 0.25) < 0.03 ? 0.05 * (rng.nextFloat() * 2.0 - 1.0) : 0.0;
            const float l = (float) (0.5 * kick + bass + vocal + hat);
            const float r = (float) (0.5 * kick + bass + 0.8 * vocal + hat);
            b.setSample (0, i, l);
            b.setSample (1, i, r);
        }
    }

    double rmsDb (const juce::AudioBuffer<float>& b)
    {
        double sum = 0.0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                sum += (double) b.getSample (c, i) * b.getSample (c, i);
        return 10.0 * std::log10 (sum / (b.getNumChannels() * b.getNumSamples()) + 1.0e-20);
    }

    void runAudio (DDRAudioProcessor& p, double sr, int blocks, int64_t& pos, double* lastRms = nullptr, bool* finite = nullptr)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int k = 0; k < blocks; ++k)
        {
            fillTestSong (buf, sr, pos);
            pos += buf.getNumSamples();
            p.processBlock (buf, midi);
            if (finite != nullptr)
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        if (! std::isfinite (buf.getSample (c, i)) || std::abs (buf.getSample (c, i)) > 8.0f)
                            *finite = false;
        }
        if (lastRms != nullptr)
            *lastRms = rmsDb (buf);
    }

    void setParam (DDRAudioProcessor& p, const char* id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    const double sr = 48000.0;
    DDRAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, sr, 512);
    processor.prepareToPlay (sr, 512);

    // Sanity sweep: every car, road, seat and source at a few speeds must stay finite.
    bool finite = true;
    int64_t pos = 0;
    for (int carIdx = 0; carIdx < 6; ++carIdx)
        for (int roadIdx = 0; roadIdx < 5; ++roadIdx)
        {
            setParam (processor, ddr::ParamID::car, (float) carIdx);
            setParam (processor, ddr::ParamID::road, (float) roadIdx);
            setParam (processor, ddr::ParamID::seat, (float) ((carIdx + roadIdx) % 4));
            setParam (processor, ddr::ParamID::source, (float) (roadIdx % 3));
            setParam (processor, ddr::ParamID::engine, (float) (carIdx % 3));
            setParam (processor, ddr::ParamID::speed, (float) (roadIdx * 35));
            setParam (processor, ddr::ParamID::window, (float) (carIdx % 2));
            runAudio (processor, sr, 20, pos, nullptr, &finite);
        }
    std::printf ("sweep finite: %s\n", finite ? "yes" : "NO");

    // Robustness: other sample rates, odd block sizes, and CPU cost
    for (double rate : { 44100.0, 96000.0, 192000.0 })
        for (int block : { 1, 33, 4096 })
        {
            DDRAudioProcessor p2;
            p2.setPlayConfigDetails (2, 2, rate, block);
            p2.prepareToPlay (rate, block);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            bool ok = true;
            const int total = (int) rate * 2;
            int64_t at = 0;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            while (at < total)
            {
                fillTestSong (buf, rate, at);
                at += block;
                p2.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i)
                        if (! std::isfinite (buf.getSample (c, i))) ok = false;
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::printf ("rate %6.0f block %4d: %s, CPU %.1f %% of real time\n", rate, block, ok ? "ok" : "NOT FINITE",
                         100.0 * ms / 2000.0);
            finite = finite && ok;
        }

    // Mono in / mono out
    {
        DDRAudioProcessor p3;
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        const bool supported = p3.setBusesLayout (mono);
        p3.setPlayConfigDetails (1, 1, sr, 512);
        p3.prepareToPlay (sr, 512);
        juce::AudioBuffer<float> buf (1, 512);
        juce::MidiBuffer midi;
        bool ok = true;
        for (int k = 0; k < 200; ++k)
        {
            for (int i = 0; i < 512; ++i) buf.setSample (0, i, 0.3f * std::sin ((float) (k * 512 + i) * 0.01f));
            p3.processBlock (buf, midi);
            for (int i = 0; i < 512; ++i) if (! std::isfinite (buf.getSample (0, i))) ok = false;
        }
        std::printf ("mono layout supported: %s, output %s\n", supported ? "yes" : "no", ok ? "ok" : "NOT FINITE");
        finite = finite && ok;
    }

    // STUDIO must pass the mix through untouched, and the state must survive a save and load.
    {
        DDRAudioProcessor p4;
        p4.setPlayConfigDetails (2, 2, sr, 512);
        p4.prepareToPlay (sr, 512);
        setParam (p4, ddr::ParamID::carMode, 0.0f);
        juce::AudioBuffer<float> buf (2, 512), ref (2, 512);
        juce::MidiBuffer midi;
        int64_t at = 0;
        float worst = 0.0f;
        for (int k = 0; k < 200; ++k)
        {
            fillTestSong (buf, sr, at);
            ref.makeCopyOf (buf);
            at += 512;
            p4.processBlock (buf, midi);
            if (k >= 100)
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        worst = juce::jmax (worst, std::abs (buf.getSample (c, i) - ref.getSample (c, i)));
        }
        const bool transparent = worst < 1.0e-5f;
        std::printf ("STUDIO transparent: %s (max difference %.2g)\n", transparent ? "yes" : "NO", (double) worst);

        setParam (p4, ddr::ParamID::car, 4.0f);
        setParam (p4, ddr::ParamID::speed, 77.0f);
        setParam (p4, ddr::ParamID::road, 3.0f);
        juce::MemoryBlock saved;
        p4.getStateInformation (saved);
        DDRAudioProcessor p5;
        p5.setStateInformation (saved.getData(), (int) saved.getSize());
        bool same = true;
        for (auto* id : { ddr::ParamID::carMode, ddr::ParamID::car, ddr::ParamID::speed, ddr::ParamID::road })
            same = same && std::abs (p4.apvts.getRawParameterValue (id)->load() - p5.apvts.getRawParameterValue (id)->load()) < 1.0e-4f;
        std::printf ("state round trip: %s\n", same ? "ok" : "MISMATCH");
        finite = finite && transparent && same;
    }

    // Reset to defaults for the measurements and the screenshot
    for (auto* param : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    juce::AudioBuffer<float> in (2, 48000);
    fillTestSong (in, sr, 0);
    std::printf ("input rms: %.1f dBFS\n", rmsDb (in));

    for (float kmh : { 0.0f, 50.0f, 100.0f, 130.0f })
    {
        setParam (processor, ddr::ParamID::speed, kmh);
        double out = 0.0;
        runAudio (processor, sr, 400, pos, &out);
        std::printf ("speed %3.0f km/h: output %.1f dBFS | low music %.1f noise %.1f | mid music %.1f noise %.1f | high music %.1f noise %.1f\n",
                     kmh, out,
                     processor.meters[0].music.load(), processor.meters[0].noise.load(),
                     processor.meters[1].music.load(), processor.meters[1].noise.load(),
                     processor.meters[2].music.load(), processor.meters[2].noise.load());
    }

    // Noise only at 100 km/h, so the level of the "mökä" itself is visible
    setParam (processor, ddr::ParamID::speed, 100.0f);
    setParam (processor, ddr::ParamID::noiseOnly, 1.0f);
    double noiseOnly = 0.0;
    runAudio (processor, sr, 200, pos, &noiseOnly);
    std::printf ("noise only at 100 km/h: %.1f dBFS\n", noiseOnly);
    setParam (processor, ddr::ParamID::noiseOnly, 0.0f);
    runAudio (processor, sr, 200, pos);

    {
        // the Retro skin artwork on its own, as JUCE renders it
        auto bg = juce::Drawable::createFromImageData (BinaryData::background_svg, BinaryData::background_svgSize);
        juce::Image img (juce::Image::RGB, 880, 712, true);
        juce::Graphics g (img);
        bg->draw (g, 1.0f);
        auto file = outDir.getChildFile ("background.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (img, stream);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* ddrEditor = dynamic_cast<DDRAudioProcessorEditor*> (editor.get());

    for (bool advanced : { false, true })
    {
        ddrEditor->setAdvanced (advanced);
        runAudio (processor, sr, 20, pos);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        auto file = outDir.getChildFile (advanced ? "ddr-advanced.png" : "ddr-basic.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }

    editor.reset();
    return finite ? 0 : 1;
}
