#include "MusicChain.h"
#include <algorithm>
#include <cmath>

namespace ddr
{
namespace
{
    constexpr float speedOfSound = 343.0f;

    /** Factory amplifier running out of headroom: clean below 0.8, rounds off to 1.0. */
    inline float ampClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.8f)
            return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.0f ? -y : y;
    }

    struct EarPosition { float x, forward, down; };

    /** Where the listener's head is, relative to the midpoint between the front door speakers. */
    EarPosition earFor (Seat seat, bool rhd, float width)
    {
        const float half = width * 0.5f;
        EarPosition p { 0.0f, 0.65f, 0.45f };

        switch (seat)
        {
            case Seat::driver:    p.x = -half + 0.42f; break;
            case Seat::passenger: p.x =  half - 0.42f; break;
            case Seat::backSeat:  p.x = -half + 0.42f; p.forward = 1.55f; break;
            case Seat::sweetSpot: p.x = 0.0f; break;
        }

        if (rhd)
            p.x = -p.x;

        return p;
    }
}

void MusicChain::prepare (double sampleRate, int)
{
    sr = sampleRate;

    int size = 1;
    while (size < (int) (sr * 0.03))
        size <<= 1;
    delayL.assign ((size_t) size, 0.0f);
    delayR.assign ((size_t) size, 0.0f);
    delayMask = size - 1;

    limAttack  = (float) std::exp (-1.0 / (0.0005 * sr));
    limRelease = (float) std::exp (-1.0 / (0.15 * sr));

    reflLpL.setCutoff (sr, 5000.0);
    reflLpR.setCutoff (sr, 5000.0);
    reset();
}

void MusicChain::reset()
{
    for (auto* f : { &srcLp1, &srcLp2, &srcShelf, &huBass, &huTreble, &huLoudLow, &huLoudHigh,
                     &spkHp1, &spkHp2, &spkTop, &spkWorn, &spkLowBand, &cabBoom, &cabSeatShelf })
        f->reset();
    for (auto& f : spkBands) f.reset();
    for (auto& f : spkExtra) f.reset();
    for (auto& f : cabModes) f.reset();

    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    writePos = 0;
    limEnv = 1.0f;
    huGainSmoothed = huGain;
    reflLpL.reset();
    reflLpR.reset();
    rearLpL.reset();
    rearLpR.reset();
}

void MusicChain::update (const Settings& s)
{
    const auto& car = *s.car;
    auto keepState = [] (Stereo& st, auto&& fn)
    {
        // update coefficients but keep the filter memory, so changes do not click
        Biquad tmp = st.l; fn (tmp);
        st.l.b0 = st.r.b0 = tmp.b0; st.l.b1 = st.r.b1 = tmp.b1; st.l.b2 = st.r.b2 = tmp.b2;
        st.l.a1 = st.r.a1 = tmp.a1; st.l.a2 = st.r.a2 = tmp.a2;
    };

    // ---- Source -------------------------------------------------------------
    switch (s.source)
    {
        case Source::usb:
            srcFilters = 0; fmWidth = 1.0f; fmHiss = 0.0f;
            break;
        case Source::bluetooth:
            srcFilters = 2; fmWidth = 1.0f; fmHiss = 0.0f;
            keepState (srcLp1,   [&] (Biquad& b) { b.setLowPass (sr, 16000.0, 0.707); });
            keepState (srcShelf, [&] (Biquad& b) { b.setHighShelf (sr, 11000.0, -1.5); });
            keepState (srcLp2,   [&] (Biquad& b) { b.setIdentity(); });
            break;
        case Source::fm:
            srcFilters = 3; fmWidth = 0.7f; fmHiss = dbToGain (-64.0f);
            keepState (srcLp1,   [&] (Biquad& b) { b.setLowPass (sr, 15000.0, 0.541); });
            keepState (srcLp2,   [&] (Biquad& b) { b.setLowPass (sr, 15000.0, 1.307); });
            keepState (srcShelf, [&] (Biquad& b) { b.setPeak (sr, 3000.0, 0.8, 1.0); });
            break;
    }

    // ---- Head unit ----------------------------------------------------------
    keepState (huBass,   [&] (Biquad& b) { b.setLowShelf (sr, 100.0, s.bassDb); });
    keepState (huTreble, [&] (Biquad& b) { b.setHighShelf (sr, 10000.0, s.trebleDb); });

    const float loudLow = s.loudness ? juce::jlimit (0.0f, 10.0f, (24.0f - (float) s.volume) * 0.6f) : 0.0f;
    keepState (huLoudLow,  [&] (Biquad& b) { b.setLowShelf (sr, 90.0, loudLow); });
    keepState (huLoudHigh, [&] (Biquad& b) { b.setHighShelf (sr, 10000.0, loudLow * 0.5); });

    const float svcPerKmh = s.svc == 0 ? 0.0f : (s.svc == 1 ? 0.035f : 0.07f);
    huGain = dbToGain (((float) s.volume - 20.0f) * 0.75f + svcPerKmh * s.speedKmh);
    limiterOn = s.limiter;

    // ---- Speakers -----------------------------------------------------------
    const bool worn = s.worn || car.worn;
    const double lowCut = car.lowCutHz * std::pow (2.0, (double) s.lowCutOct) * (worn ? 1.2 : 1.0);
    const double lowQ   = car.lowCutQ + (worn ? 0.25 : 0.0);
    keepState (spkHp1, [&] (Biquad& b) { b.setHighPass (sr, lowCut, lowQ); });
    keepState (spkHp2, [&] (Biquad& b) { b.setHighPass (sr, lowCut * 0.7, 0.5); });
    for (size_t i = 0; i < spkBands.size(); ++i)
    {
        const auto& band = car.speakerBands[i];
        keepState (spkBands[i], [&] (Biquad& b) { b.setPeak (sr, band.freq, band.q, band.gainDb); });
    }
    numExtra = 0;
    for (size_t i = 0; i < spkExtra.size(); ++i)
    {
        const auto& band = car.extraBands[i];
        if (std::abs (band.gainDb) > 0.01f)
            numExtra = (int) i + 1;
        keepState (spkExtra[i], [&] (Biquad& b) { if (std::abs (band.gainDb) > 0.01f) b.setPeak (sr, band.freq, band.q, band.gainDb); else b.setIdentity(); });
    }
    keepState (spkTop,  [&] (Biquad& b) { b.setLowPass (sr, car.topHz * (worn ? 0.85 : 1.0), 0.707); });
    keepState (spkWorn, [&] (Biquad& b) { if (worn) b.setHighShelf (sr, 5000.0, -3.0); else b.setIdentity(); });
    keepState (spkLowBand, [&] (Biquad& b) { b.setLowPass (sr, 250.0, 0.707); });

    coneMix = juce::jlimit (0.0f, 1.0f, s.distortion);
    rattleAmount = 1.5f * s.distortion * car.coneDrive * (worn ? 1.6f : 1.0f);
    rattleThreshold = 0.22f;
    rattleBpL.setBandPass (sr, 1900.0, 1.8);
    rattleBpR.setBandPass (sr, 2300.0, 1.8);
    coneK   = 1.0f + 5.0f * car.coneDrive * s.distortion * (worn ? 1.4f : 1.0f);

    // ---- Cabin --------------------------------------------------------------
    const bool back = s.seat == Seat::backSeat;
    keepState (cabBoom, [&] (Biquad& b) { b.setLowShelf (sr, car.boomHz, car.boomDb * s.boom + (back ? 2.0 : 0.0)); });
    for (size_t i = 0; i < cabModes.size(); ++i)
    {
        const auto& m = car.modes[i];
        keepState (cabModes[i], [&] (Biquad& b) { b.setPeak (sr, m.freq, m.q, m.gainDb * s.resonance); });
    }
    keepState (cabSeatShelf, [&] (Biquad& b) { if (back) b.setHighShelf (sr, 2500.0, -2.0); else b.setIdentity(); });

    // ---- Seat: arrival time and level from each front speaker ---------------
    const auto ear = earFor (s.seat, s.rhd, car.widthMetres);
    const float half = car.widthMetres * 0.5f;
    constexpr float rearPlane = 1.6f; // rear door speakers, metres behind the front ones
    auto dist = [&] (float speakerX, float planeOffset)
    {
        const float dx = ear.x - speakerX, dy = ear.forward - planeOffset;
        return std::sqrt (dx * dx + dy * dy + ear.down * ear.down);
    };
    const bool rear = car.rearSpeakers;
    const float dL = dist (-half, 0.0f), dR = dist (half, 0.0f);
    const float rL = dist (-half, rearPlane), rR = dist (half, rearPlane);
    const float dMin = rear ? std::min ({ dL, dR, rL, rR }) : std::min (dL, dR);
    auto delayFor = [&] (float d) { return (int) std::lround ((d - dMin) / speedOfSound * sr); };
    // a car cabin is small and reflective, so the level drops slower than 1/r
    auto gainFor  = [&] (float d) { return std::pow (dMin / d, 0.7f); };

    const float fader = juce::jlimit (-1.0f, 1.0f, s.fader);
    frontLevel = fader > 0.0f ? 1.0f - fader : 1.0f;
    const float rearLevel = rear ? (fader < 0.0f ? 1.0f + fader : 1.0f) * 0.55f : 0.0f; // rear pair is smaller and further away

    seatDelayL = delayFor (dL);  seatDelayR = delayFor (dR);
    seatGainL  = gainFor (dL);   seatGainR  = gainFor (dR);
    rearDelayL = rear ? delayFor (rL) : 0;
    rearDelayR = rear ? delayFor (rR) : 0;
    rearGainL  = rear ? gainFor (rL) * rearLevel : 0.0f;
    rearGainR  = rear ? gainFor (rR) * rearLevel : 0.0f;
    rearLpL.setCutoff (sr, back ? 12000.0 : 4500.0);
    rearLpR.setCutoff (sr, back ? 12000.0 : 4500.0);
    crossfeed = s.seat == Seat::sweetSpot ? 0.35f : 0.45f;

    // ---- Early reflections from glass and dashboard -------------------------
    static constexpr float msL[numTaps]  { 1.1f, 1.9f, 2.8f, 4.1f, 5.6f };
    static constexpr float msR[numTaps]  { 1.3f, 2.2f, 3.1f, 4.4f, 6.0f };
    static constexpr float gain[numTaps] { 0.42f, 0.34f, 0.27f, 0.20f, 0.14f };
    for (int i = 0; i < numTaps; ++i)
    {
        tapL[(size_t) i] = (int) (msL[i] * 0.001 * sr);
        tapR[(size_t) i] = (int) (msR[i] * 0.001 * sr);
        tapGain[(size_t) i] = gain[i];
    }
    reflectionGain = juce::jlimit (0.0f, 1.6f, car.reflections * s.reflections);
}

void MusicChain::process (float* left, float* right, int n) noexcept
{
    const float smooth = 0.999f;

    for (int i = 0; i < n; ++i)
    {
        float l = left[i], r = right[i];

        // Source
        if (fmWidth < 1.0f)
        {
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * fmWidth;
            l = mid + side;
            r = mid - side;
        }
        if (srcFilters > 0)
        {
            l = srcLp1.l.process (l);  r = srcLp1.r.process (r);
            l = srcShelf.l.process (l); r = srcShelf.r.process (r);
            if (srcFilters > 2) { l = srcLp2.l.process (l); r = srcLp2.r.process (r); }
        }
        if (fmHiss > 0.0f)
        {
            l += fmHiss * nextNoise();
            r += fmHiss * nextNoise();
        }

        // Head unit
        l = huBass.l.process (l);     r = huBass.r.process (r);
        l = huTreble.l.process (l);   r = huTreble.r.process (r);
        l = huLoudLow.l.process (l);  r = huLoudLow.r.process (r);
        l = huLoudHigh.l.process (l); r = huLoudHigh.r.process (r);

        huGainSmoothed = huGain + smooth * (huGainSmoothed - huGain);
        l *= huGainSmoothed;
        r *= huGainSmoothed;

        if (limiterOn)
        {
            const float threshold = 0.79f; // about -2 dBFS
            const float peak = std::max (std::abs (l), std::abs (r));
            const float target = peak > threshold ? threshold / peak : 1.0f;
            const float coef = target < limEnv ? limAttack : limRelease;
            limEnv = target + coef * (limEnv - target);
            l *= limEnv;
            r *= limEnv;
        }
        l = ampClip (l);
        r = ampClip (r);

        // Speakers
        l = spkHp1.l.process (l); r = spkHp1.r.process (r);
        l = spkHp2.l.process (l); r = spkHp2.r.process (r);
        for (auto& b : spkBands) { l = b.l.process (l); r = b.r.process (r); }
        for (int e = 0; e < numExtra; ++e) { l = spkExtra[(size_t) e].l.process (l); r = spkExtra[(size_t) e].r.process (r); }
        l = spkTop.l.process (l);  r = spkTop.r.process (r);
        l = spkWorn.l.process (l); r = spkWorn.r.process (r);

        if (coneMix > 0.0f)
        {
            const float lowL = spkLowBand.l.process (l), lowR = spkLowBand.r.process (r);
            l += coneMix * (std::tanh (coneK * lowL) / coneK - lowL);
            r += coneMix * (std::tanh (coneK * lowR) / coneK - lowR);

            // Door panel rattle: strong bass makes the trim buzz. The buzz follows the
            // bass waveform above a threshold, coloured like a plastic panel.
            if (rattleAmount > 0.0f)
            {
                const float push = std::max (0.0f, std::abs (lowL + lowR) * 0.5f - rattleThreshold);
                const float buzz = push * nextNoise();
                l += rattleAmount * rattleBpL.process (buzz);
                r += rattleAmount * rattleBpR.process (buzz);
            }
        }

        // Cabin: direct sound from both speakers to the listener, then reflections
        delayL[(size_t) writePos] = l;
        delayR[(size_t) writePos] = r;
        auto readL = [&] (int d) { return delayL[(size_t) ((writePos - d) & delayMask)]; };
        auto readR = [&] (int d) { return delayR[(size_t) ((writePos - d) & delayMask)]; };

        const float fromL = frontLevel * seatGainL * readL (seatDelayL);
        const float fromR = frontLevel * seatGainR * readR (seatDelayR);
        float outL = fromL + crossfeed * fromR;
        float outR = fromR + crossfeed * fromL;

        if (rearGainL > 0.0f || rearGainR > 0.0f)
        {
            const float backL = rearLpL.process (rearGainL * readL (rearDelayL));
            const float backR = rearLpR.process (rearGainR * readR (rearDelayR));
            outL += backL + crossfeed * backR;
            outR += backR + crossfeed * backL;
        }

        if (reflectionGain > 0.0f)
        {
            float reflL = 0.0f, reflR = 0.0f;
            for (size_t t = 0; t < (size_t) numTaps; ++t)
            {
                const bool same = (t % 2) == 0;
                reflL += tapGain[t] * (same ? readL (seatDelayL + tapL[t]) : readR (seatDelayR + tapL[t]));
                reflR += tapGain[t] * (same ? readR (seatDelayR + tapR[t]) : readL (seatDelayL + tapR[t]));
            }
            outL += reflectionGain * reflLpL.process (reflL);
            outR += reflectionGain * reflLpR.process (reflR);
        }
        writePos = (writePos + 1) & delayMask;

        outL = cabBoom.l.process (outL); outR = cabBoom.r.process (outR);
        for (auto& m : cabModes) { outL = m.l.process (outL); outR = m.r.process (outR); }
        outL = cabSeatShelf.l.process (outL); outR = cabSeatShelf.r.process (outR);

        left[i]  = outL;
        right[i] = outR;
    }
}
} // namespace ddr
