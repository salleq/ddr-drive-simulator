#include "NoiseGenerator.h"
#include <algorithm>
#include <cmath>

namespace ddr
{
namespace
{
    constexpr double twoPi = 6.283185307179586;
    constexpr float silent = -200.0f;

    inline float splToGain (float spl) noexcept
    {
        return spl <= silent ? 0.0f : dbToGain (spl - NoiseGenerator::splAtZeroDbfs);
    }

    struct SurfaceLevels { float rumbleDb, tyreDb, tyreHz, hissSpl; bool hissIsSpray; bool gravel; };

    /** Offsets relative to rough-ish average asphalt at 100 km/h. */
    SurfaceLevels surfaceFor (Road road)
    {
        switch (road)
        {
            case Road::smooth:  return { 0.0f, -2.0f,  750.0f, silent, false, false };
            case Road::rough:   return { 3.0f,  2.0f,  900.0f, silent, false, false };
            case Road::studded: return { 2.0f,  3.0f, 1000.0f, 64.0f,  false, false };
            case Road::gravel:  return { 7.0f,  4.0f,  650.0f, silent, false, true };
            case Road::wet:     return { 1.0f,  1.0f,  850.0f, 62.0f,  true,  false };
        }
        return { 0.0f, 0.0f, 900.0f, silent, false, false };
    }

    /** Rough gearbox: picks the highest gear that keeps the engine above 1400 rpm. */
    float rpmForSpeed (float kmh)
    {
        if (kmh < 1.0f)
            return 800.0f;

        static constexpr float rpmPerKmh[] { 120.0f, 70.0f, 46.0f, 35.0f, 28.0f, 23.0f };
        for (int g = 5; g >= 0; --g)
        {
            const float rpm = kmh * rpmPerKmh[g];
            if (rpm >= 1400.0f)
                return std::min (rpm, 4500.0f);
        }
        return std::max (kmh * rpmPerKmh[0], 1000.0f);
    }
}

void NoiseGenerator::Channel::reset()
{
    pinkA = {}; pinkB = {}; pinkC = {};
    for (auto* b : { &rumbleLp, &rumbleHp, &tyreBp, &hissA, &hissB, &windBp, &rushBp, &engBp, &fanBp, &crackleBp })
        b->reset();
}

void NoiseGenerator::configureFilters (Channel& c, Road r) const
{
    const auto surf = surfaceFor (r);
    c.rumbleLp.setLowPass (sr, 160.0, 0.707);
    c.rumbleHp.setHighPass (sr, 28.0, 0.707);
    c.tyreBp.setBandPass (sr, surf.tyreHz, 0.8);

    if (surf.hissIsSpray)
    {
        c.hissA.setHighPass (sr, 2500.0, 0.707);
        c.hissB.setLowPass (sr, 9000.0, 0.707);
    }
    else
    {
        c.hissA.setBandPass (sr, 4500.0, 0.9);
        c.hissB.setIdentity();
    }

    c.windBp.setBandPass (sr, 350.0, 0.5);
    c.rushBp.setBandPass (sr, 1300.0, 0.7);
    c.engBp.setLowPass (sr, 300.0, 0.707);
    c.fanBp.setBandPass (sr, 900.0, 0.4);
    c.crackleBp.setBandPass (sr, 1800.0, 3.0);
}

void NoiseGenerator::prepare (double sampleRate)
{
    sr = sampleRate;
    rampCoef  = 1.0f - (float) std::exp (-1.0 / (0.05 * sr));
    rpmSmooth = (float) std::exp (-1.0 / (0.4 * sr));
    textureCoef = 1.0f - (float) std::exp (-1.0 / (0.35 * sr));
    jointDecay  = (float) std::exp (-1.0 / (0.07 * sr));
    ch[0].rng.state = 0x9E3779B9u;
    ch[1].rng.state = 0x7F4A7C15u;
    measureNormalisation();
    reset();
}

void NoiseGenerator::reset()
{
    for (auto& c : ch)
    {
        c.reset();
        configureFilters (c, road);
    }
    gain.fill (0.0f);
    phase = whinePhase = buffetPhaseA = buffetPhaseB = 0.0;
    rpm = rpmTarget;
    engineToneGain = engineToneTarget;
    roadChanged = false;
    texture = textureTarget = 1.0f;
    textureCountdown = 0;
    jointEnv = 0.0f;
    jointSecondHit = -1;
    jointCountdown = nextJointInterval();
}

/** Runs each noise colour through its filters once so that every component can be
    scaled to a known RMS level, whatever the sample rate. */
void NoiseGenerator::measureNormalisation()
{
    const int n = (int) std::max (32768.0, sr * 1.5);
    const float crackleRef = (float) (240.0 / sr);

    for (int r = 0; r < 5; ++r)
    {
        Channel c;
        c.rng.state = 0xA5A5A5A5u + (uint32_t) r;
        c.reset();
        configureFilters (c, (Road) r);
        Rng ctrl { 0x1234567u };

        std::array<double, numComponents> sum {};
        for (int i = 0; i < n; ++i)
        {
            const float w1 = c.rng.next(), w2 = c.rng.next(), w3 = c.rng.next(), w4 = c.rng.next();
            const float pa = c.pinkA.process (w1), pb = c.pinkB.process (w2), pc = c.pinkC.process (w3);
            const float impulse = (ctrl.next() * 0.5f + 0.5f) < crackleRef ? ctrl.next() : 0.0f;

            const float v[numComponents] {
                c.rumbleHp.process (c.rumbleLp.process (pa)),
                c.tyreBp.process (pb),
                c.hissB.process (c.hissA.process (w4)),
                c.windBp.process (pc),
                c.rushBp.process (pc),
                c.engBp.process (pb),
                c.fanBp.process (pa),
                c.crackleBp.process (impulse)
            };

            if (i > n / 8) // skip the filters' settling time
                for (int k = 0; k < numComponents; ++k)
                    sum[(size_t) k] += (double) v[k] * v[k];
        }

        const double count = n - n / 8;
        for (int k = 0; k < numComponents; ++k)
        {
            const double rms = std::sqrt (sum[(size_t) k] / count);
            norm[(size_t) r][(size_t) k] = rms > 1.0e-9 ? (float) (1.0 / rms) : 0.0f;
        }
    }
}

int NoiseGenerator::nextJointInterval() noexcept
{
    // a joint or patch every 3 to 9 seconds
    return (int) (sr * (3.0 + 6.0 * (ctrlRng.next() * 0.5 + 0.5)));
}

void NoiseGenerator::update (const Settings& s)
{
    if (s.road != road)
    {
        road = s.road;
        roadChanged = true;
    }

    const float v     = s.speedKmh;
    const float vEff  = std::max (v, 10.0f);
    const float fade  = std::min (1.0f, v / 10.0f);
    const float ins   = s.car->insulationDb;
    const float amt   = s.noiseAmount;
    const auto  surf  = surfaceFor (road);
    const auto& nrm   = norm[(size_t) road];
    const float lv    = std::log10 (vEff / 100.0f);

    auto set = [&] (Component k, float spl, float extra = 1.0f)
    { target[(size_t) k] = splToGain (spl) * nrm[(size_t) k] * amt * extra; };

    // road and tyres
    set (rumble, 76.0f + 30.0f * lv + surf.rumbleDb + ins, fade);
    set (tyre,   66.0f + 35.0f * lv + surf.tyreDb + ins, fade);
    set (hiss,   surf.hissSpl <= silent ? silent : surf.hissSpl + 40.0f * lv + ins, fade);
    set (crackle, surf.gravel ? 70.0f + 20.0f * lv + ins : silent, fade);
    crackleRate = surf.gravel ? (float) (120.0 * (v / 50.0) / sr) : 0.0f;

    // joints only make sense on paved roads at speed
    jointsEnabled = ! surf.gravel && v > 20.0f;
    jointGain = jointsEnabled ? splToGain (78.0f + 10.0f * lv + ins) * amt : 0.0f;
    wheelbaseDelaySec = v > 1.0f ? 2.65f / (v / 3.6f) : 0.0f;

    // wind grows very fast with speed; an open window is much louder and buffets
    window = s.window;
    const float windFade = std::min (1.0f, v / 20.0f);
    const float windSpl  = 63.0f + 55.0f * std::log10 (std::max (v, 20.0f) / 100.0f) + ins + (window ? 12.0f : 0.0f);
    set (wind, windSpl, windFade);
    set (rush, windSpl + (window ? 2.0f : -10.0f), windFade);

    // engine
    engine = s.engine;
    const float load = std::min (v / 100.0f, 1.3f);
    float engineSpl = 54.0f + 8.0f * load + ins;
    if (engine == Engine::diesel)   engineSpl += 4.0f;
    if (engine == Engine::electric) engineSpl = 35.0f + ins;
    set (engineNoise, engineSpl - 3.0f);
    rpmTarget = rpmForSpeed (v);
    engineToneTarget = engine == Engine::electric ? 0.0f : splToGain (engineSpl) * amt;

    if (engine == Engine::diesel)
        harmonics = { 1.0f, 0.7f, 0.5f, 0.4f, 0.3f, 0.25f };
    else
        harmonics = { 1.0f, 0.5f, 0.3f, 0.15f, 0.1f, 0.05f };

    float power = 0.0f;
    for (auto h : harmonics) power += h * h * 0.5f;
    const float scale = 1.0f / std::sqrt (power);
    for (auto& h : harmonics) h *= scale;

    clatter = engine == Engine::diesel ? 0.35f : 0.0f;
    whine   = engine == Engine::electric ? splToGain (42.0f + 10.0f * load + ins) * amt * 1.414f : 0.0f;

    // ventilation fan
    static constexpr float fanSpl[] { silent, 47.0f, 52.0f, 57.0f, 62.0f };
    set (fan, fanSpl[juce::jlimit (0, 4, s.fan)]);
}

void NoiseGenerator::process (float* left, float* right, int n) noexcept
{
    if (roadChanged)
    {
        for (auto& c : ch)
            configureFilters (c, road);
        roadChanged = false;
    }

    const double buffetIncA = twoPi * 1.9 / sr, buffetIncB = twoPi * 0.31 / sr;

    for (int i = 0; i < n; ++i)
    {
        for (size_t k = 0; k < (size_t) numComponents; ++k)
            gain[k] += (target[k] - gain[k]) * rampCoef;
        engineToneGain += (engineToneTarget - engineToneGain) * rampCoef;

        // engine tone, shared by both ears
        rpm = rpmTarget + rpmSmooth * (rpm - rpmTarget);
        const double firingHz = rpm / 60.0 * 2.0; // four cylinders: two firings per revolution
        phase += twoPi * firingHz / sr;
        if (phase > twoPi) phase -= twoPi;

        float tone = 0.0f;
        for (size_t h = 0; h < harmonics.size(); ++h)
            tone += harmonics[h] * (float) std::sin (phase * (double) (h + 1));
        tone *= engineToneGain;

        float clatterEnv = 0.0f;
        if (clatter > 0.0f)
        {
            const float sp = (float) std::sin (phase);
            clatterEnv = sp > 0.0f ? sp * sp * sp * sp * sp * sp * sp * sp : 0.0f;
        }

        float whineSample = 0.0f;
        if (whine > 0.0f)
        {
            whinePhase += twoPi * (300.0 + 12.0 * (rpmTarget > 800.0f ? rpm / 23.0 : 0.0)) / sr;
            if (whinePhase > twoPi) whinePhase -= twoPi;
            whineSample = whine * (float) std::sin (whinePhase);
        }

        // road texture: a new random level every half second, gently smoothed
        if (--textureCountdown <= 0)
        {
            textureCountdown = (int) (sr * 0.5);
            textureTarget = dbToGain (1.5f * ctrlRng.next());
        }
        texture += (textureTarget - texture) * textureCoef;

        // expansion joints: thump from the front axle, then the rear one
        float thump = 0.0f;
        if (jointsEnabled)
        {
            if (--jointCountdown <= 0)
            {
                jointCountdown = nextJointInterval();
                jointEnv = 1.0f;
                jointSecondHit = (int) (wheelbaseDelaySec * sr);
            }
            if (jointSecondHit >= 0 && jointSecondHit-- == 0)
                jointEnv = std::max (jointEnv, 0.8f);
        }
        if (jointEnv > 1.0e-4f)
        {
            jointPhase += twoPi * 48.0 / sr;
            if (jointPhase > twoPi) jointPhase -= twoPi;
            thump = jointGain * jointEnv * (float) std::sin (jointPhase) * 1.414f;
            jointEnv *= jointDecay;
        }

        float buffet = 1.0f;
        if (window)
        {
            buffetPhaseA += buffetIncA; buffetPhaseB += buffetIncB;
            if (buffetPhaseA > twoPi) buffetPhaseA -= twoPi;
            if (buffetPhaseB > twoPi) buffetPhaseB -= twoPi;
            buffet = 1.0f + 0.5f * (float) (std::sin (buffetPhaseA) * std::sin (buffetPhaseB));
        }

        for (size_t c = 0; c < 2; ++c)
        {
            auto& chn = ch[c];
            const float w1 = chn.rng.next(), w2 = chn.rng.next(), w3 = chn.rng.next(), w4 = chn.rng.next();
            const float pa = chn.pinkA.process (w1), pb = chn.pinkB.process (w2), pc = chn.pinkC.process (w3);

            float impulse = 0.0f;
            if (crackleRate > 0.0f && (ctrlRng.next() * 0.5f + 0.5f) < crackleRate)
                impulse = ctrlRng.next();

            float out = texture * (gain[rumble] * chn.rumbleHp.process (chn.rumbleLp.process (pa))
                                 + gain[tyre]   * chn.tyreBp.process (pb)
                                 + gain[hiss]   * chn.hissB.process (chn.hissA.process (w4)))
                      + gain[wind]        * buffet * chn.windBp.process (pc)
                      + gain[rush]        * buffet * chn.rushBp.process (pc)
                      + gain[engineNoise] * chn.engBp.process (pb)
                      + gain[fan]         * chn.fanBp.process (pa)
                      + gain[crackle]     * chn.crackleBp.process (impulse);

            out += tone + whineSample + thump + clatter * engineToneGain * clatterEnv * w4;

            (c == 0 ? left : right)[i] = out;
        }
    }
}
} // namespace ddr
