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

    /** "Records" the DDR test signal through one of DDR's own cars, like a phone in that car would. */
    ddr::cal::Measurement measureThroughDDR (const ddr::CarProfile& car)
    {
        const double rate = 48000.0;
        auto signal = ddr::cal::makeTestSignal (rate);
        ddr::Settings s;
        s.car = &car;
        ddr::MusicChain chain;
        chain.prepare (rate, 512);
        chain.update (s);
        chain.reset();
        std::vector<float> mono ((size_t) signal.getNumSamples());
        juce::Random rng (7);
        for (int start = 0; start < signal.getNumSamples(); start += 512)
        {
            const int n = juce::jmin (512, signal.getNumSamples() - start);
            chain.process (signal.getWritePointer (0, start), signal.getWritePointer (1, start), n);
        }
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.2f * (signal.getSample (0, (int) i) + signal.getSample (1, (int) i)) + 0.0005f * (rng.nextFloat() - 0.5f);
        return ddr::cal::analyse (mono.data(), (int) mono.size(), rate);
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
    DDRAudioProcessor::useMyCarFile = false; // never touch the real "My Car" of this computer
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
    for (int carIdx = 0; carIdx <= ddr::myCarIndex; ++carIdx)
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
    // ...and every listening place, switched while the music plays
    for (int placeIdx : { 1, 2, 3, 0, 3, 1 })
    {
        setParam (processor, ddr::ParamID::place, (float) placeIdx);
        runAudio (processor, sr, 40, pos, nullptr, &finite);
    }
    setParam (processor, ddr::ParamID::place, 0.0f);
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

    // My Car: measure the Family Wagon through DDR itself, fit it, and play it back
    ddr::cal::Measurement myCarMeasurement;
    {
        myCarMeasurement = measureThroughDDR (ddr::carProfiles()[0]);
        const auto target = ddr::cal::makeTarget (myCarMeasurement, nullptr);
        const auto fit = ddr::cal::fitProfile (target, 0);
        std::printf ("My Car from the Family Wagon: %s fit error %.2f dB\n", myCarMeasurement.message.toRawUTF8(), (double) fit.errorDb);

        DDRAudioProcessor p6;
        p6.setPlayConfigDetails (2, 2, sr, 512);
        p6.prepareToPlay (sr, 512);
        p6.setMyCar (ddr::cal::toValueTree (fit, target));
        setParam (p6, ddr::ParamID::car, (float) ddr::myCarIndex);
        bool myCarOk = myCarMeasurement.ok && fit.errorDb < 2.0f && p6.hasMyCar();
        int64_t at = 0;
        runAudio (p6, sr, 100, at, nullptr, &myCarOk);

        juce::MemoryBlock saved;
        p6.getStateInformation (saved);
        DDRAudioProcessor p7;
        p7.setStateInformation (saved.getData(), (int) saved.getSize());
        myCarOk = myCarOk && p7.hasMyCar();
        std::printf ("My Car plays and survives a save and load: %s\n", myCarOk ? "yes" : "NO");
        finite = finite && myCarOk;
    }

    // Reference tracks: they replace the mix, match its loudness, loop, stop at the end and are saved
    const auto refDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ddr-refs");
    refDir.createDirectory();
    auto writeRef = [&] (const juce::String& name, double rate, float gain, double seconds)
    {
        juce::AudioBuffer<float> song (2, (int) (rate * seconds));
        fillTestSong (song, rate, 12345);
        song.applyGain (gain);
        auto file = refDir.getChildFile (name);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 2, 24, {}, 0));
        writer->writeFromAudioSampleBuffer (song, 0, song.getNumSamples());
        return file;
    };
    const auto refFile  = writeRef ("Favourite club track.wav", 44100.0, 0.5f, 20.0);
    const auto refFile2 = writeRef ("Radio hit.wav", 48000.0, 0.8f, 30.0);
    {
        DDRAudioProcessor p8;
        p8.setPlayConfigDetails (2, 2, sr, 512);
        p8.prepareToPlay (sr, 512);
        setParam (p8, ddr::ParamID::carMode, 0.0f);
        int64_t at = 0;
        double mixRms = 0.0;
        runAudio (p8, sr, 600, at, &mixRms);   // the mix plays first, so its loudness is known

        auto& refs = p8.references;
        refs.loadNow (0, refFile);
        const auto track = refs.getTrack (0);
        bool ok = track != nullptr && refs.getStatus (0) == ddr::ReferencePlayer::Status::ready
                  && std::abs (track->lengthSeconds - 20.0) < 0.01;
        std::printf ("reference loaded: %s, %.1f LUFS, %.1f s, mix %.1f LUFS\n", ok ? "yes" : "NO",
                     track != nullptr ? (double) track->lufs : -100.0, track != nullptr ? track->lengthSeconds : 0.0,
                     (double) p8.getMixLoudness());

        // play it while the host sends silence: what comes out is the reference, as loud as the mix
        auto runSilence = [&] (int blocks, double& rms)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> all (2, blocks * 512);
            for (int k = 0; k < blocks; ++k)
            {
                buf.clear();
                p8.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    all.copyFrom (c, k * 512, buf, c, 0, 512);
            }
            rms = rmsDb (all);
        };
        double refRms = 0.0, quiet = 0.0;
        refs.play (0);
        runSilence (200, refRms);
        const bool matched = std::abs (refRms - mixRms) < 1.5;
        std::printf ("reference plays as loud as the mix: %s (mix %.1f dBFS, ref %.1f dBFS, gain %+.1f dB)\n",
                     matched ? "yes" : "NO", mixRms, refRms, (double) refs.getGainDb());

        refs.setMatchToMix (false);
        double unmatched = 0.0;
        runSilence (200, unmatched);
        const bool raw = std::abs ((mixRms - unmatched) - 6.0) < 1.5;   // the file is 6 dB quieter than the mix
        std::printf ("match off plays it as it is: %s (%.1f dBFS)\n", raw ? "yes" : "NO", unmatched);
        refs.setMatchToMix (true);

        // a loop keeps the playhead inside it
        refs.setLoop (0, 5.0, 6.0);
        refs.setLoopOn (0, true);
        double dummy = 0.0, lo = 1.0e9, hi = 0.0;
        for (int k = 0; k < 30; ++k)
        {
            runSilence (10, dummy);
            lo = juce::jmin (lo, refs.getPosition (0));
            hi = juce::jmax (hi, refs.getPosition (0));
        }
        const bool looped = lo >= 4.99 && hi <= 6.01 && refs.isPlaying (0);
        std::printf ("loop 5-6 s holds the playhead: %s (%.2f-%.2f s)\n", looped ? "yes" : "NO", lo, hi);

        // the loop and the file survive a save and load
        juce::MemoryBlock saved;
        p8.getStateInformation (saved);
        DDRAudioProcessor p9;
        p9.setStateInformation (saved.getData(), (int) saved.getSize());
        ddr::ReferencePlayer fresh;
        fresh.fromState (p8.references.toState(), false);
        const auto loop9 = p9.references.getLoop (0);
        const bool restored = p9.references.getFile (0) == refFile && fresh.getStatus (0) == ddr::ReferencePlayer::Status::ready
                              && fresh.getStatus (1) == ddr::ReferencePlayer::Status::empty
                              && std::abs (loop9.start - 5.0) < 1.0e-6 && std::abs (loop9.end - 6.0) < 1.0e-6 && loop9.on
                              && std::abs (fresh.getLoop (0).end - 6.0) < 1.0e-6;
        std::printf ("reference and loop survive a save and load: %s\n", restored ? "yes" : "NO");

        // pause: back to the mix (silence here)
        refs.pause();
        runSilence (5, quiet);    // the fade out
        runSilence (50, quiet);
        const bool paused = quiet < -120.0;
        std::printf ("pause returns to the mix: %s (%.0f dBFS)\n", paused ? "yes" : "NO", quiet);

        // without the loop it stops at the end and rewinds
        refs.setLoopOn (0, false);
        refs.seek (0, 19.5);
        refs.play (0);
        runSilence (100, dummy);
        const bool stopped = ! refs.isPlaying (0) && refs.getPosition (0) < 0.01;
        std::printf ("stops and rewinds at the end: %s\n", stopped ? "yes" : "NO");

        // a second reference at another sample rate, switched while playing, in the car
        refs.loadNow (1, refFile2);
        setParam (p8, ddr::ParamID::carMode, 1.0f);
        refs.play (0);
        bool fin = true;
        runAudio (p8, sr, 50, at, nullptr, &fin);
        refs.play (1);
        runAudio (p8, sr, 50, at, nullptr, &fin);
        refs.clear (1);   // removed while playing
        runAudio (p8, sr, 50, at, nullptr, &fin);
        std::printf ("switching and removing while playing: %s\n", fin && refs.getPlayingSlot() == -1 ? "ok" : "NOT OK");

        finite = finite && ok && matched && raw && looped && restored && paused && stopped && fin;
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

    // Mix hints and level match, in the car at a few speeds and on the other devices
    {
        ddr::HintEngine hints;
        auto printHints = [&] (const char* what)
        {
            const auto h = processor.getHintLevels();
            std::printf ("%s: matched %+.1f dB | dry/music/noise dB:", what, (double) processor.matchDb.load());
            static const char* bandNames[] { "sub", "bass", "mid", "vocal", "air" };
            for (size_t b = 0; b < (size_t) ddr::numHintBands; ++b)
                std::printf (" %s %.0f/%.0f/%.0f", bandNames[b], (double) h.dry[b], (double) h.music[b], (double) h.noise[b]);
            std::printf ("\n");
            for (auto& hint : hints.make (h, processor.readSettings(), processor.getPlace()))
                std::printf ("    [%d] %s\n", hint.severity, hint.text.toRawUTF8());
        };
        for (float kmh : { 50.0f, 100.0f, 130.0f })
        {
            setParam (processor, ddr::ParamID::speed, kmh);
            runAudio (processor, sr, 400, pos);
            printHints ((juce::String ("car at ") + juce::String (kmh, 0) + " km/h").toRawUTF8());
        }
        {
            // the same mix with the vocal and the bass 12 dB quieter: now they should go under at some speed
            auto quiet = processor.getHintLevels();
            quiet.music[ddr::vocalBand] -= 12.0f;
            quiet.music[ddr::bassBand] -= 12.0f;
            auto s = processor.readSettings();
            for (float kmh : { 60.0f, 130.0f })
            {
                s.speedKmh = kmh;
                std::printf ("quieter vocal and bass at %.0f km/h:\n", (double) kmh);
                for (auto& hint : hints.make (quiet, s, ddr::Place::car))
                    std::printf ("    [%d] %s\n", hint.severity, hint.text.toRawUTF8());
            }
        }
        const auto curve = hints.noiseBySpeed();
        std::printf ("noise by speed (vocal band): 0 %.0f, 50 %.0f, 100 %.0f, 140 %.0f dBFS\n",
                     curve[0][ddr::vocalBand], curve[5][ddr::vocalBand], curve[10][ddr::vocalBand], curve[14][ddr::vocalBand]);

        setParam (processor, ddr::ParamID::speed, 100.0f);
        for (int placeIdx = 1; placeIdx < ddr::numPlaces; ++placeIdx)
        {
            setParam (processor, ddr::ParamID::place, (float) placeIdx);
            double out = 0.0;
            runAudio (processor, sr, 400, pos, &out);
            printHints ((ddr::placeNames()[placeIdx] + ", output " + juce::String (out, 1) + " dBFS").toRawUTF8());
            setParam (processor, ddr::ParamID::noiseOnly, 1.0f);
            runAudio (processor, sr, 200, pos, &out);
            std::printf ("    background noise alone: %.1f dBFS\n", out);
            setParam (processor, ddr::ParamID::noiseOnly, 0.0f);
        }
        setParam (processor, ddr::ParamID::place, 0.0f);
        runAudio (processor, sr, 100, pos);
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

    // two references in the screenshots: the first one plays its loop
    processor.references.loadNow (0, refFile);
    processor.references.loadNow (1, refFile2);
    processor.references.setLoop (0, 8.0, 12.5);
    processor.references.setLoopOn (0, true);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* ddrEditor = dynamic_cast<DDRAudioProcessorEditor*> (editor.get());

    for (bool advanced : { false, true })
    {
        ddrEditor->setAdvanced (advanced);
        runAudio (processor, sr, 20, pos);
        ddrEditor->getPanel().refreshNow();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        auto file = outDir.getChildFile (advanced ? "ddr-advanced.png" : "ddr-basic.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }

    {
        // a device place, so its look (dimmed car controls) is checked too
        ddrEditor->setAdvanced (false);
        setParam (processor, ddr::ParamID::place, 3.0f);
        runAudio (processor, sr, 200, pos);
        ddrEditor->getPanel().refreshNow();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        auto file = outDir.getChildFile ("ddr-places.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
        setParam (processor, ddr::ParamID::place, 0.0f);
        runAudio (processor, sr, 50, pos);
    }

    {
        // the big view of a reference, playing its loop
        ddrEditor->setAdvanced (false);
        processor.references.play (0);
        runAudio (processor, sr, 120, pos);
        auto& panel = ddrEditor->getPanel();
        panel.showReference (0);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        auto file = outDir.getChildFile ("ddr-reference.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
        panel.showReference (-1);

        // and the small cards while it plays
        panel.refreshNow();
        auto cards = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        auto cardFile = outDir.getChildFile ("ddr-reference-cards.png");
        cardFile.deleteFile();
        juce::FileOutputStream cardStream (cardFile);
        juce::PNGImageFormat().writeImageToStream (cards, cardStream);
        std::printf ("wrote %s\n", cardFile.getFullPathName().toRawUTF8());
        processor.references.pause();
        runAudio (processor, sr, 20, pos);
    }

    {
        ddrEditor->setAdvanced (false);
        auto& panel = ddrEditor->getPanel();
        panel.showMyCar (true);
        panel.getMyCarPanel()->showForTesting (myCarMeasurement, nullptr);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        auto file = outDir.getChildFile ("ddr-mycar.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
        panel.showMyCar (false);
    }

    editor.reset();
    return finite ? 0 : 1;
}
