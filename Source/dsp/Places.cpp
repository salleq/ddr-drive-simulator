#include "Places.h"
#include <algorithm>
#include <cmath>

namespace ddr
{
namespace
{
    constexpr double twoPi = 6.283185307179586;

    // Background noise level per place in dBFS, for a master at about -12 dBFS RMS.
    // Worked out as (noise you hear) - (how loud you listen) - 12:
    //   phone on a kitchen table: 42 dB room, music at 66 dB    -> -36
    //   earbuds on a bus:         74 dB bus, ~6 dB kept out, 82 -> -26
    //   Bluetooth speaker party:  62 dB chatter, music at 80 dB -> -30
    float ambientDbfs (Place p)
    {
        switch (p)
        {
            case Place::phone:     return -36.0f;
            case Place::earbuds:   return -26.0f;
            case Place::btSpeaker: return -30.0f;
            case Place::car:       break;
        }
        return -200.0f;
    }
}

const char* placeNoiseName (Place p)
{
    switch (p)
    {
        case Place::car:       return "road noise";
        case Place::phone:     return "kitchen noise";
        case Place::earbuds:   return "bus noise";
        case Place::btSpeaker: return "party chatter";
    }
    return "noise";
}

//==============================================================================
void PlaceChain::prepare (double sampleRate)
{
    sr = sampleRate;
    setPlace (place);
    reset();
}

void PlaceChain::reset()
{
    for (auto& s : eq) s.reset();
    keyLp.reset();
    env = 0.0f;
}

void PlaceChain::setPlace (Place p)
{
    place = p;
    for (auto& s : eq) { s.l.setIdentity(); s.r.setIdentity(); }
    auto set = [this] (size_t i, auto&& fn) { fn (eq[i].l); fn (eq[i].r); };

    compOn = false; bassKeyed = false; drive = 0.0f; mono = true;
    float thrDb = 0.0f, attackMs = 5.0f, releaseMs = 150.0f, makeupDb = 0.0f;

    switch (p)
    {
        case Place::phone:
            // A phone lying on a table: tiny speaker, nothing below ~400 Hz, a shouty upper mid
            set (0, [&] (Biquad& b) { b.setHighPass (sr, 450.0, 0.54); });
            set (1, [&] (Biquad& b) { b.setHighPass (sr, 450.0, 1.31); });
            set (2, [&] (Biquad& b) { b.setPeak (sr, 1100.0, 1.0, 2.0); });
            set (3, [&] (Biquad& b) { b.setPeak (sr, 3000.0, 1.4, 5.0); });
            set (4, [&] (Biquad& b) { b.setPeak (sr, 6500.0, 2.0, -3.0); });
            set (5, [&] (Biquad& b) { b.setLowPass (sr, 11000.0, 0.707); });
            compOn = true; thrDb = -24.0f; ratio = 4.0f; attackMs = 3.0f; releaseMs = 120.0f; makeupDb = 5.0f;
            drive = 1.2f;
            break;

        case Place::earbuds:
            // Cheap earbuds with a loose fit: the bass leaks out, a harsh 3-5 kHz peak and sharp esses
            set (0, [&] (Biquad& b) { b.setHighPass (sr, 80.0, 0.707); });
            set (1, [&] (Biquad& b) { b.setLowShelf (sr, 250.0, -5.0); });
            set (2, [&] (Biquad& b) { b.setPeak (sr, 1000.0, 1.0, -1.0); });
            set (3, [&] (Biquad& b) { b.setPeak (sr, 3500.0, 2.0, 6.0); });
            set (4, [&] (Biquad& b) { b.setPeak (sr, 8000.0, 3.0, 4.0); });
            set (5, [&] (Biquad& b) { b.setLowPass (sr, 14000.0, 0.707); });
            mono = false;
            break;

        case Place::btSpeaker:
            // Small mono Bluetooth speaker: a passive-radiator bump at ~110 Hz, nothing below 70 Hz,
            // and a bass-keyed limiter that ducks everything when the kick hits
            set (0, [&] (Biquad& b) { b.setHighPass (sr, 70.0, 0.54); });
            set (1, [&] (Biquad& b) { b.setHighPass (sr, 70.0, 1.31); });
            set (2, [&] (Biquad& b) { b.setPeak (sr, 110.0, 1.4, 6.0); });
            set (3, [&] (Biquad& b) { b.setPeak (sr, 600.0, 1.0, -3.0); });
            set (4, [&] (Biquad& b) { b.setPeak (sr, 2500.0, 1.0, 2.0); });
            set (5, [&] (Biquad& b) { b.setLowPass (sr, 15000.0, 0.707); });
            compOn = true; bassKeyed = true; thrDb = -26.0f; ratio = 5.0f; attackMs = 5.0f; releaseMs = 180.0f; makeupDb = 8.0f;
            keyLp.setLowPass (sr, 200.0, 0.707);
            drive = 0.8f;
            break;

        case Place::car:
            mono = false;
            break;
    }

    threshold = dbToGain (thrDb);
    makeup = dbToGain (makeupDb);
    attack  = (float) std::exp (-1.0 / (attackMs * 0.001 * sr));
    release = (float) std::exp (-1.0 / (releaseMs * 0.001 * sr));
}

void PlaceChain::process (float* left, float* right, int n) noexcept
{
    const float slope = 1.0f - 1.0f / std::max (ratio, 1.0f);

    for (int i = 0; i < n; ++i)
    {
        float l = left[i], r = right[i];
        if (mono)
            l = r = 0.5f * (l + r);

        for (auto& s : eq)
        {
            l = s.l.process (l);
            r = s.r.process (r);
        }

        if (compOn)
        {
            const float key = bassKeyed ? std::abs (keyLp.process (0.5f * (l + r))) * 2.0f
                                        : std::max (std::abs (l), std::abs (r));
            env = key + (key > env ? attack : release) * (env - key);
            const float g = (env > threshold ? std::pow (threshold / env, slope) : 1.0f) * makeup;
            l *= g;
            r *= g;
        }

        if (drive > 0.0f)
        {
            l = std::tanh (drive * l) / drive;
            r = std::tanh (drive * r) / drive;
        }

        left[i] = l;
        right[i] = r;
    }
}

//==============================================================================
void AmbientNoise::Channel::reset()
{
    pink = {};
    for (auto* b : { &bedHp, &bedLp, &rumbleLp, &voiceA, &voiceB, &voiceC })
        b->reset();
    voiceEnv.fill (0.0f);
    voiceTarget.fill (0.0f);
    voiceCountdown.fill (0);
}

void AmbientNoise::configure (Channel& c, Place p) const
{
    switch (p)
    {
        case Place::phone:     c.bedHp.setHighPass (sr, 60.0, 0.707);  c.bedLp.setLowPass (sr, 2500.0, 0.707); break;
        case Place::earbuds:   c.bedHp.setHighPass (sr, 30.0, 0.707);  c.bedLp.setLowPass (sr, 1500.0, 0.707); break;
        case Place::btSpeaker: c.bedHp.setHighPass (sr, 100.0, 0.707); c.bedLp.setLowPass (sr, 5000.0, 0.707); break;
        case Place::car:       c.bedHp.setIdentity(); c.bedLp.setIdentity(); break;
    }
    c.rumbleLp.setLowPass (sr, 120.0, 0.707);
    c.voiceA.setBandPass (sr, 500.0, 1.2);
    c.voiceB.setBandPass (sr, 1200.0, 1.5);
    c.voiceC.setBandPass (sr, 2500.0, 2.0);
}

void AmbientNoise::prepare (double sampleRate)
{
    sr = sampleRate;
    rampCoef = 1.0f - (float) std::exp (-1.0 / (0.05 * sr));
    envCoef  = 1.0f - (float) std::exp (-1.0 / (0.03 * sr));
    ch[0].rng.state = 0x2545F491u;
    ch[1].rng.state = 0x9E3779B1u;
    measureNormalisation();
    reset();
}

/** Runs each part of each place once, so every part can be scaled to a known RMS at any sample rate. */
void AmbientNoise::measureNormalisation()
{
    const int n = (int) std::max (32768.0, sr * 2.0);
    for (int p = 1; p < numPlaces; ++p)
        for (int part = 0; part < 3; ++part)
        {
            Channel c;
            c.rng.state = 0x13579BDFu + (uint32_t) (p * 3 + part);
            c.reset();
            configure (c, (Place) p);
            double sum = 0.0;
            runChannel (c, part, n / 8, sum);   // settle
            sum = 0.0;
            runChannel (c, part, n, sum);
            const double rms = std::sqrt (sum / n);
            norm[(size_t) p][(size_t) part] = rms > 1.0e-9 ? (float) (1.0 / rms) : 0.0f;
        }
}

float AmbientNoise::runChannel (Channel& s, int part, int count, double& sum) noexcept
{
    float x = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const float w = s.rng.next();
        const float p = s.pink.process (w);
        if (part == 0)
            x = s.bedLp.process (s.bedHp.process (p));
        else if (part == 1)
            x = s.rumbleLp.process (p);
        else
        {
            for (size_t v = 0; v < 3; ++v)
            {
                if (--s.voiceCountdown[v] <= 0)
                {
                    const float r = s.rng.next() * 0.5f + 0.5f;
                    s.voiceCountdown[v] = (int) (sr * (0.08 + 0.17 * r));
                    const float t = s.rng.next() * 0.5f + 0.5f;
                    s.voiceTarget[v] = t * t * 1.7f;
                }
                s.voiceEnv[v] += (s.voiceTarget[v] - s.voiceEnv[v]) * envCoef;
            }
            const float w2 = s.rng.next();
            x = s.voiceA.process (w2) * s.voiceEnv[0] + s.voiceB.process (w2) * s.voiceEnv[1]
              + s.voiceC.process (w2) * s.voiceEnv[2];
        }
        sum += (double) x * x;
    }
    return x;
}

void AmbientNoise::reset()
{
    for (auto& c : ch)
    {
        c.reset();
        configure (c, place);
    }
    humPhase = 0.0;
    level = levelTarget;
}

void AmbientNoise::setPlace (Place p, float amount)
{
    if (p != place)
    {
        place = p;
        for (auto& c : ch)
            configure (c, place);
    }

    // Relative levels of the parts (dB), then everything is scaled to the place's level.
    float bedDb = -200.0f, rumbleDb = -200.0f, voiceDb = -200.0f, humDb = -200.0f;
    switch (p)
    {
        case Place::phone:     bedDb = 0.0f;  humDb = -12.0f;  voiceDb = -20.0f; humHz = 50.0; break; // fridge, distant radio
        case Place::earbuds:   rumbleDb = 0.0f; bedDb = -4.0f; humDb = -6.0f; voiceDb = -14.0f; humHz = 36.0; break; // engine
        case Place::btSpeaker: voiceDb = 0.0f; bedDb = -6.0f;  rumbleDb = -15.0f; break;
        case Place::car:       break;
    }
    auto power = [] (float db) { return db < -150.0f ? 0.0f : std::pow (10.0f, db * 0.1f); };
    const float total = power (bedDb) + power (rumbleDb) + power (voiceDb) + power (humDb);
    const float scale = total > 0.0f ? dbToGain (ambientDbfs (p)) / std::sqrt (total) : 0.0f;

    const auto& k = norm[(size_t) p];
    bedGain    = dbToGain (bedDb) * scale * k[0];
    rumbleGain = dbToGain (rumbleDb) * scale * k[1];
    voiceGain  = dbToGain (voiceDb) * scale * k[2];
    humGain    = dbToGain (humDb) * scale;
    if (bedDb < -150.0f)    bedGain = 0.0f;
    if (rumbleDb < -150.0f) rumbleGain = 0.0f;
    if (voiceDb < -150.0f)  voiceGain = 0.0f;
    if (humDb < -150.0f)    humGain = 0.0f;
    levelTarget = std::max (0.0f, amount);
}

void AmbientNoise::process (float* left, float* right, int n) noexcept
{
    const double humInc = twoPi * humHz / sr;
    float* out[2] { left, right };

    for (int i = 0; i < n; ++i)
    {
        level += (levelTarget - level) * rampCoef;
        humPhase += humInc;
        if (humPhase > twoPi) humPhase -= twoPi;
        const float hum = (float) (std::sin (humPhase) + 0.4 * std::sin (2.0 * humPhase)) * humGain * 1.313f; // unit RMS

        for (size_t c = 0; c < 2; ++c)
        {
            auto& s = ch[c];
            const float w = s.rng.next();
            const float p = s.pink.process (w);
            float x = s.bedLp.process (s.bedHp.process (p)) * bedGain
                    + s.rumbleLp.process (p) * rumbleGain + hum;

            if (voiceGain > 0.0f)
            {
                // a few people talking: each voice comes and goes in syllables of 80-250 ms
                for (size_t v = 0; v < 3; ++v)
                {
                    if (--s.voiceCountdown[v] <= 0)
                    {
                        const float r = s.rng.next() * 0.5f + 0.5f;
                        s.voiceCountdown[v] = (int) (sr * (0.08 + 0.17 * r));
                        const float t = s.rng.next() * 0.5f + 0.5f;
                        s.voiceTarget[v] = t * t * 1.7f;
                    }
                    s.voiceEnv[v] += (s.voiceTarget[v] - s.voiceEnv[v]) * envCoef;
                }
                const float w2 = s.rng.next();
                x += (s.voiceA.process (w2) * s.voiceEnv[0] + s.voiceB.process (w2) * s.voiceEnv[1]
                      + s.voiceC.process (w2) * s.voiceEnv[2]) * voiceGain;
            }

            out[c][i] = x * level;
        }
    }
}
} // namespace ddr
