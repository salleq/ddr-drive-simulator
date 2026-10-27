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
            p (ParamID::mono), p (ParamID::levelMatch),
            p (ParamID::engine), p (ParamID::window), p (ParamID::fan), p (ParamID::noiseAmount), p (ParamID::noiseOnly),
            p (ParamID::boom), p (ParamID::resonance), p (ParamID::reflections), p (ParamID::seat), p (ParamID::rhd),
            p (ParamID::lowCut), p (ParamID::distortion), p (ParamID::worn), p (ParamID::source), p (ParamID::fader),
            p (ParamID::volume), p (ParamID::loudness), p (ParamID::svc), p (ParamID::bass), p (ParamID::treble),
            p (ParamID::limiter) };

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

    music.process (wet.getWritePointer (0), wet.getWritePointer (1), n);
    noise.process (noiseBuf.getWritePointer (0), noiseBuf.getWritePointer (1), n);

    // Level match: keep the car version as loud as the input, so A/B is fair.
    // The noise gets the same gain, so turning the car volume up still lifts the music above the noise.
    auto meanSquare = [n] (const juce::AudioBuffer<float>& b)
    {
        double sum = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            const float* d = b.getReadPointer (c);
            for (int i = 0; i < n; ++i) sum += (double) d[i] * d[i];
        }
        return sum / (2.0 * n);
    };

    const double blockSeconds = n / sr;
    powerCoef = std::exp (-blockSeconds / 3.0);
    inPower  = meanSquare (dry) + powerCoef * (inPower  - meanSquare (dry));
    wetPower = meanSquare (wet) + powerCoef * (wetPower - meanSquare (wet));

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
    const float noiseTarget = playing ? 1.0f : 0.0f;
    const float noiseStep = (float) (1.0 / (0.25 * sr));
    const float smooth = 0.9995f;

    float* dl = dry.getWritePointer (0);  float* dr = dry.getWritePointer (1);
    float* wl = wet.getWritePointer (0);  float* wr = wet.getWritePointer (1);
    float* nl = noiseBuf.getWritePointer (0); float* nr = noiseBuf.getWritePointer (1);

    double mPow[3] {}, nPow[3] {};

    for (int i = 0; i < n; ++i)
    {
        matchGain = matchTarget + smooth * (matchGain - matchTarget);
        outGain   = outTarget + smooth * (outGain - outTarget);
        carMix    = carTarget > carMix ? juce::jmin (carTarget, carMix + carMixStep)
                                       : juce::jmax (carTarget, carMix - carMixStep);

        const float ml = wl[i] * matchGain, mr = wr[i] * matchGain;
        noiseFade = noiseTarget > noiseFade ? juce::jmin (noiseTarget, noiseFade + noiseStep)
                                            : juce::jmax (noiseTarget, noiseFade - noiseStep);
        const float xl = nl[i] * matchGain * noiseFade, xr = nr[i] * matchGain * noiseFade;

        // meters (mono sums)
        const float mm = 0.5f * (ml + mr), nm = 0.5f * (xl + xr);
        const float mLow = musicBands[0].process (mm), mMid = musicBands[2].process (musicBands[1].process (mm)), mHigh = musicBands[3].process (mm);
        const float nLow = noiseBands[0].process (nm), nMid = noiseBands[2].process (noiseBands[1].process (nm)), nHigh = noiseBands[3].process (nm);
        mPow[0] += mLow * mLow; mPow[1] += mMid * mMid; mPow[2] += mHigh * mHigh;
        nPow[0] += nLow * nLow; nPow[1] += nMid * nMid; nPow[2] += nHigh * nHigh;

        float carL = musicOn * ml + xl, carR = musicOn * mr + xr;
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
            apvts.replaceState (state);
            loadMyCar();
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
