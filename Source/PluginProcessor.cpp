#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace ddr;
namespace cal = ddr::cal;

DDRAudioProcessor::DDRAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DDR", createParameterLayout())
{
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw = { p (ParamID::carMode), p (ParamID::car), p (ParamID::speed), p (ParamID::road), p (ParamID::output),
            p (ParamID::mono), p (ParamID::levelMatch), p (ParamID::place),
            p (ParamID::engine), p (ParamID::window), p (ParamID::fan), p (ParamID::noiseAmount), p (ParamID::noiseOnly),
            p (ParamID::boom), p (ParamID::resonance), p (ParamID::reflections), p (ParamID::seat), p (ParamID::rhd),
            p (ParamID::lowCut), p (ParamID::distortion), p (ParamID::worn), p (ParamID::source), p (ParamID::fader),
            p (ParamID::volume), p (ParamID::loudness), p (ParamID::svc), p (ParamID::bass), p (ParamID::treble),
            p (ParamID::limiter) };

    for (size_t b = 0; b < (size_t) numHintBands; ++b)
    {
        hintDry[b].store (-100.0f);
        hintMusic[b].store (-100.0f);
        hintNoise[b].store (-100.0f);
    }

    apvts.state.setProperty ("advanced", false, nullptr);
    loadMyCar();
}

juce::File DDRAudioProcessor::myCarFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("DDR Drive Simulator").getChildFile ("MyCar.xml");
}

void DDRAudioProcessor::setMyCar (const juce::ValueTree& tree)
{
    apvts.state.removeChild (apvts.state.getChildWithName (cal::myCarTag), nullptr);
    if (tree.isValid())
    {
        apvts.state.addChild (tree.createCopy(), -1, nullptr);
        if (auto xml = tree.createXml(); xml != nullptr && useMyCarFile)
        {
            myCarFile().getParentDirectory().createDirectory();
            xml->writeTo (myCarFile());
        }
    }
    loadMyCar();
}

void DDRAudioProcessor::loadMyCar()
{
    // The project's own measurement wins; a project without one gets the last car measured on this computer
    auto tree = apvts.state.getChildWithName (cal::myCarTag);
    if (! tree.isValid() && useMyCarFile)
        if (auto xml = juce::XmlDocument::parse (myCarFile()))
        {
            auto fromFile = juce::ValueTree::fromXml (*xml);
            ddr::CarProfile check;
            if (cal::fromValueTree (fromFile, check))
            {
                apvts.state.addChild (fromFile, -1, nullptr);
                tree = fromFile;
            }
        }

    const int next = 1 - myCarSlot.load();
    if (cal::fromValueTree (tree, myCarSlots[(size_t) next]))
    {
        myCarSlot.store (next);
        myCarVersion.fetch_add (1);
        myCarValid.store (true);
    }
    else
    {
        myCarValid.store (false);
    }
}

bool DDRAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void DDRAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const int n = juce::jmax (1, samplesPerBlock);
    dry.setSize (2, n);
    wet.setSize (2, n);
    noiseBuf.setSize (2, n);

    music.prepare (sr, n);
    noise.prepare (sr);
    const auto s = readSettings();
    smoothed = s;
    lastApplied = s;
    music.update (s);
    noise.update (s);
    music.reset();
    noise.reset();

    activePlace.store (juce::jlimit (0, numPlaces - 1, (int) raw.place->load()));
    placeChain.prepare (sr);
    placeChain.setPlace ((Place) activePlace.load());
    placeChain.reset();
    ambient.prepare (sr);
    ambient.setPlace ((Place) activePlace.load(), s.noiseAmount);
    ambient.reset();
    switchGain = 1.0f;
    switchStep = (float) (1.0 / (0.01 * sr));
    fastMatchSamples = 0;

    for (auto* k : { &kDry, &kWet })
        for (auto& c : *k)
        {
            c[0].setHighShelf (sr, 1682.0, 4.0);
            c[1].setHighPass (sr, 38.0, 0.5);
            for (auto& b : c) b.reset();
        }

    for (auto* f : { &hintDryF, &hintMusicF, &hintNoiseF })
        f->prepare (sr);
    hintDryPow.fill (0.0); hintMusicPow.fill (0.0); hintNoisePow.fill (0.0);
    hintDryTotal = hintMusicTotal = 0.0;

    inPower = wetPower = 0.0;
    matchGain = matchTarget = 1.0f;
    carMix = raw.carMode->load() > 0.5f ? 1.0f : 0.0f;
    carMixStep = (float) (1.0 / (0.02 * sr));
    outGain = dbToGain (raw.output->load());

    for (auto* bands : { &musicBands, &noiseBands })
    {
        (*bands)[0].setLowPass (sr, 250.0, 0.707);
        (*bands)[1].setHighPass (sr, 250.0, 0.707);
        (*bands)[2].setLowPass (sr, 4000.0, 0.707);
        (*bands)[3].setHighPass (sr, 4000.0, 0.707);
        for (auto& b : *bands) b.reset();
    }
    musicBandPow.fill (0.0);
    noiseBandPow.fill (0.0);

    references.prepare (sr);
    refWasActive = false;
}

Settings DDRAudioProcessor::readSettings() const
{
    Settings s;
    const auto& cars = carProfiles();
    const int carIndex = (int) raw.car->load();
    if (carIndex == myCarIndex)
    {
        s.car = myCarValid.load() ? &myCarSlots[(size_t) myCarSlot.load()] : &cars[1]; // not measured yet: the hatchback
        s.carVersion = myCarVersion.load();
    }
    else
        s.car = &cars[(size_t) juce::jlimit (0, (int) cars.size() - 1, carIndex)];
    s.speedKmh    = raw.speed->load();
    s.road        = (Road) juce::jlimit (0, 4, (int) raw.road->load());
    s.engine      = (Engine) juce::jlimit (0, 2, (int) raw.engine->load());
    s.window      = raw.window->load() > 0.5f;
    s.fan         = (int) raw.fan->load();
    s.noiseAmount = raw.noiseAmount->load() * 0.01f;
    s.noiseOnly   = raw.noiseOnly->load() > 0.5f;
    s.boom        = raw.boom->load() * 0.01f;
    s.resonance   = raw.resonance->load() * 0.01f;
    s.reflections = raw.reflections->load() * 0.01f;
    s.seat        = (Seat) juce::jlimit (0, 3, (int) raw.seat->load());
    s.rhd         = raw.rhd->load() > 0.5f;
    s.lowCutOct   = raw.lowCut->load();
    s.distortion  = raw.distortion->load() * 0.01f;
    s.worn        = raw.worn->load() > 0.5f;
    s.source      = (Source) juce::jlimit (0, 2, (int) raw.source->load());
    s.fader       = raw.fader->load() * 0.01f;
    s.volume      = (int) raw.volume->load();
    s.loudness    = raw.loudness->load() > 0.5f;
    s.svc         = (int) raw.svc->load();
    s.bassDb      = raw.bass->load();
    s.trebleDb    = raw.treble->load();
    s.limiter     = raw.limiter->load() > 0.5f;
    return s;
}

void DDRAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    if (n == 0 || numCh == 0)
        return;

    if (n > dry.getNumSamples())
    {
        // hosts should never do this, but stay safe if they do
        dry.setSize (2, n, false, false, true);
        wet.setSize (2, n, false, false, true);
        noiseBuf.setSize (2, n, false, false, true);
    }

    // Glide the continuous controls towards their targets, so turning a knob
    // never steps the filters hard enough to click.
    auto s = readSettings();
    {
        const float k = 1.0f - (float) std::exp (-(double) n / (0.03 * sr));
        auto glide = [k] (float& current, float target)
        {
            current += (target - current) * k;
            if (std::abs (target - current) < 1.0e-4f)
                current = target; // settle exactly, so unchanged settings can skip the filter update
            return current;
        };
        s.speedKmh    = glide (smoothed.speedKmh, s.speedKmh);
        s.boom        = glide (smoothed.boom, s.boom);
        s.resonance   = glide (smoothed.resonance, s.resonance);
        s.reflections = glide (smoothed.reflections, s.reflections);
        s.lowCutOct   = glide (smoothed.lowCutOct, s.lowCutOct);
        s.distortion  = glide (smoothed.distortion, s.distortion);
        s.bassDb      = glide (smoothed.bassDb, s.bassDb);
        s.trebleDb    = glide (smoothed.trebleDb, s.trebleDb);
        s.noiseAmount = glide (smoothed.noiseAmount, s.noiseAmount);
        s.fader       = glide (smoothed.fader, s.fader);
    }
    if (s != lastApplied)
    {
        music.update (s);
        noise.update (s);
        lastApplied = s;
    }

    // input -> dry, upmixing mono to two channels
    for (int c = 0; c < 2; ++c)
    {
        dry.copyFrom (c, 0, buffer, juce::jmin (c, numCh - 1), 0, n);
        wet.copyFrom (c, 0, buffer, juce::jmin (c, numCh - 1), 0, n);
    }

    // A playing reference takes the mix's place, so everything after this treats it as the music
    references.setMixLoudness (mixLufs.load());
    const bool refActive = references.process (dry.getWritePointer (0), dry.getWritePointer (1), n);
    if (refActive || refWasActive)
    {
        wet.copyFrom (0, 0, dry, 0, 0, n);
        wet.copyFrom (1, 0, dry, 1, 0, n);
    }
    if (refActive != refWasActive)
        fastMatchSamples = juce::jmax (fastMatchSamples, (int) (1.5 * sr));   // a new song: let the level match catch up
    const bool refBlock = refActive || refWasActive;
    refWasActive = refActive;

    // Listening place: switch only while the old one is faded out, so the change never clicks
    const int wantedPlace = juce::jlimit (0, numPlaces - 1, (int) raw.place->load());
    if (wantedPlace != activePlace.load() && switchGain <= 0.0f)
    {
        activePlace.store (wantedPlace);
        placeChain.setPlace ((Place) wantedPlace);
        placeChain.reset();
        ambient.setPlace ((Place) wantedPlace, s.noiseAmount);
        ambient.reset();
        music.reset();
        noise.reset();
        fastMatchSamples = (int) (1.5 * sr);
    }
    const auto place = (Place) activePlace.load();
    const float switchTarget = wantedPlace == (int) place ? 1.0f : 0.0f;
    const bool inCar = place == Place::car;

    if (place == Place::car)
    {
        music.process (wet.getWritePointer (0), wet.getWritePointer (1), n);
        noise.process (noiseBuf.getWritePointer (0), noiseBuf.getWritePointer (1), n);
    }
    else
    {
        placeChain.process (wet.getWritePointer (0), wet.getWritePointer (1), n);
        ambient.setPlace (place, s.noiseAmount);
        ambient.process (noiseBuf.getWritePointer (0), noiseBuf.getWritePointer (1), n);
    }

    // Level match: keep the car version as loud as the input, so A/B is fair. Both sides are
    // K-weighted like a loudness meter, so a car that boosts the bass doesn't get turned down for it.
    // The noise gets the same gain, so turning the car volume up still lifts the music above the noise.
    auto kWeightedMeanSquare = [n] (const juce::AudioBuffer<float>& b, std::array<std::array<Biquad, 2>, 2>& k)
    {
        double sum = 0.0;
        for (size_t c = 0; c < 2; ++c)
        {
            const float* d = b.getReadPointer ((int) c);
            for (int i = 0; i < n; ++i)
            {
                const float y = k[c][1].process (k[c][0].process (d[i]));
                sum += (double) y * y;
            }
        }
        return sum / (2.0 * n);
    };

    const double blockSeconds = n / sr;
    const double dryMs = kWeightedMeanSquare (dry, kDry), wetMs = kWeightedMeanSquare (wet, kWet);
    const bool fast = fastMatchSamples > 0;
    fastMatchSamples = juce::jmax (0, fastMatchSamples - n);
    powerCoef = std::exp (-blockSeconds / (fast ? 0.4 : 3.0));
    if (dryMs > 1.0e-7)   // ignore silence (gaps between songs), so the match holds its value
    {
        inPower  = dryMs + powerCoef * (inPower  - dryMs);
        wetPower = wetMs + powerCoef * (wetPower - wetMs);
    }

    // The mix's loudness (10 s average, quiet parts left out), for matching the references to it
    if (! refBlock && dryMs > 1.0e-6)
    {
        // a plain average over the first 10 s, then a moving one, so one loud first hit can't skew it
        mixSeconds = juce::jmin (10.0, mixSeconds + blockSeconds);
        mixPower += (dryMs - mixPower) * (blockSeconds / mixSeconds);
        mixLufs.store ((float) (-0.691 + 10.0 * std::log10 (2.0 * mixPower + 1.0e-20)));
    }

    if (raw.levelMatch->load() > 0.5f)
    {
        if (inPower > 1.0e-8 && wetPower > 1.0e-10)
            matchTarget = (float) juce::jlimit (0.06, 16.0, std::sqrt (inPower / wetPower));
    }
    else
    {
        matchTarget = 1.0f;
    }

    const float carTarget = raw.carMode->load() > 0.5f ? 1.0f : 0.0f;
    const float outTarget = dbToGain (raw.output->load());
    const bool  mono = raw.mono->load() > 0.5f;
    const float musicOn = s.noiseOnly ? 0.0f : 1.0f;

    // The car "parks" when the transport stops: the noise fades out instead of droning on.
    // Without transport information (standalone, some hosts) it keeps running.
    bool playing = true;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
            playing = pos->getIsPlaying();
    playing = playing || refBlock;   // a reference plays even while the host is stopped
    const float noiseTarget = playing ? 1.0f : 0.0f;
    const float noiseStep = (float) (1.0 / (0.25 * sr));
    const float smooth = fast ? 0.998f : 0.9995f;

    float* dl = dry.getWritePointer (0);  float* dr = dry.getWritePointer (1);
    float* wl = wet.getWritePointer (0);  float* wr = wet.getWritePointer (1);
    float* nl = noiseBuf.getWritePointer (0); float* nr = noiseBuf.getWritePointer (1);

    double mPow[3] {}, nPow[3] {};
    std::array<double, numHintBands> hDry {}, hMusic {}, hNoise {};
    double hDryTotal = 0.0, hMusicTotal = 0.0;

    for (int i = 0; i < n; ++i)
    {
        matchGain = matchTarget + smooth * (matchGain - matchTarget);
        outGain   = outTarget + smooth * (outGain - outTarget);
        carMix    = carTarget > carMix ? juce::jmin (carTarget, carMix + carMixStep)
                                       : juce::jmax (carTarget, carMix - carMixStep);
        switchGain = switchTarget > switchGain ? juce::jmin (switchTarget, switchGain + switchStep)
                                               : juce::jmax (switchTarget, switchGain - switchStep);

        // mix hints look at the music and noise before level matching
        const float dm = 0.5f * (dl[i] + dr[i]), wm = 0.5f * (wl[i] + wr[i]), xm = 0.5f * (nl[i] + nr[i]);
        hintDryF.process (dm, hDry);
        hintMusicF.process (wm, hMusic);
        // in a device place the background noise is not level matched (see below), so compare it
        // with the music as if it were
        hintNoiseF.process (inCar ? xm : xm / matchGain, hNoise);
        hDryTotal += (double) dm * dm;
        hMusicTotal += (double) wm * wm;

        const float ml = wl[i] * matchGain, mr = wr[i] * matchGain;
        noiseFade = noiseTarget > noiseFade ? juce::jmin (noiseTarget, noiseFade + noiseStep)
                                            : juce::jmax (noiseTarget, noiseFade - noiseStep);
        // In the car the noise follows the level match, so the car volume still decides music vs noise.
        // A device is turned up until the song is as loud as in the studio: its background noise stays put.
        const float noiseGain = (inCar ? matchGain : 1.0f) * noiseFade;
        const float xl = nl[i] * noiseGain, xr = nr[i] * noiseGain;

        // meters (mono sums)
        const float mm = 0.5f * (ml + mr), nm = 0.5f * (xl + xr);
        const float mLow = musicBands[0].process (mm), mMid = musicBands[2].process (musicBands[1].process (mm)), mHigh = musicBands[3].process (mm);
        const float nLow = noiseBands[0].process (nm), nMid = noiseBands[2].process (noiseBands[1].process (nm)), nHigh = noiseBands[3].process (nm);
        mPow[0] += mLow * mLow; mPow[1] += mMid * mMid; mPow[2] += mHigh * mHigh;
        nPow[0] += nLow * nLow; nPow[1] += nMid * nMid; nPow[2] += nHigh * nHigh;

        float carL = (musicOn * ml + xl) * switchGain, carR = (musicOn * mr + xr) * switchGain;
        float l = carMix * carL + (1.0f - carMix) * dl[i];
        float r = carMix * carR + (1.0f - carMix) * dr[i];

        if (mono)
            l = r = 0.5f * (l + r);

        wl[i] = l * outGain;
        wr[i] = r * outGain;
    }

    const double meterCoef = std::exp (-blockSeconds / 0.3);
    for (size_t b = 0; b < 3; ++b)
    {
        musicBandPow[b] = mPow[b] / n + meterCoef * (musicBandPow[b] - mPow[b] / n);
        noiseBandPow[b] = nPow[b] / n + meterCoef * (noiseBandPow[b] - nPow[b] / n);
        meters[b].music.store ((float) (10.0 * std::log10 (musicBandPow[b] + 1.0e-12)));
        meters[b].noise.store ((float) (10.0 * std::log10 (noiseBandPow[b] + 1.0e-12)));
    }
    matchDb.store (20.0f * std::log10 (juce::jmax (1.0e-6f, matchGain)));

    // The hints average over a couple of seconds, and hold still while the music is silent
    if (hDryTotal / n > 1.0e-8)
    {
        const double hintCoef = std::exp (-blockSeconds / (fast ? 0.4 : 2.0));
        auto avg = [hintCoef, n] (double& state, double blockSum)
        {
            state = blockSum / n + hintCoef * (state - blockSum / n);
            return (float) (10.0 * std::log10 (state + 1.0e-12));
        };
        for (size_t b = 0; b < (size_t) numHintBands; ++b)
        {
            hintDry[b].store (avg (hintDryPow[b], hDry[b]));
            hintMusic[b].store (avg (hintMusicPow[b], hMusic[b]));
            hintNoise[b].store (avg (hintNoisePow[b], hNoise[b]));
        }
        hintDryTotalDb.store (avg (hintDryTotal, hDryTotal));
        hintMusicTotalDb.store (avg (hintMusicTotal, hMusicTotal));
    }

    if (numCh == 1)
    {
        for (int i = 0; i < n; ++i)
            wl[i] = 0.5f * (wl[i] + wr[i]);
        buffer.copyFrom (0, 0, wet, 0, 0, n);
    }
    else
    {
        buffer.copyFrom (0, 0, wet, 0, 0, n);
        buffer.copyFrom (1, 0, wet, 1, 0, n);
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
}

HintLevels DDRAudioProcessor::getHintLevels() const
{
    HintLevels h;
    for (size_t b = 0; b < (size_t) numHintBands; ++b)
    {
        h.dry[b]   = hintDry[b].load();
        h.music[b] = hintMusic[b].load();
        h.noise[b] = hintNoise[b].load();
    }
    h.dryTotal   = hintDryTotalDb.load();
    h.musicTotal = hintMusicTotalDb.load();
    return h;
}

int DDRAudioProcessor::getNumPrograms() { return (int) situations().size(); }

const juce::String DDRAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) situations().size()))
        return situations()[(size_t) index].name;
    return {};
}

void DDRAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) situations().size()))
        return;

    currentProgram = index;
    const auto& sit = situations()[(size_t) index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set (ParamID::speed, sit.speed);
    set (ParamID::road, (float) sit.road);
    set (ParamID::fan, (float) sit.fan);
    set (ParamID::window, sit.window ? 1.0f : 0.0f);
    set (ParamID::seat, (float) sit.seat);
}

void DDRAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.removeChild (state.getChildWithName (ddr::ReferencePlayer::stateTag), nullptr);
    state.addChild (references.toState(), -1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DDRAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) state.getProperty ("program", 2);
            const auto refs = state.getChildWithName (ddr::ReferencePlayer::stateTag);
            state.removeChild (refs, nullptr);
            apvts.replaceState (state);
            loadMyCar();
            references.fromState (refs, juce::MessageManager::getInstanceWithoutCreating() != nullptr
                                            && juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());
        }
}

juce::AudioProcessorEditor* DDRAudioProcessor::createEditor()
{
    return new DDRAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DDRAudioProcessor();
}
