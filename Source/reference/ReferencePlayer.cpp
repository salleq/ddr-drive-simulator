#include "ReferencePlayer.h"
#include "dsp/Biquad.h"

namespace ddr
{
//==============================================================================
float RefTrack::sampleAt (int channel, double frame) const noexcept
{
    // 4-point Hermite: smooth enough for playing a file at another sample rate
    const int n = audio.getNumSamples();
    const float* d = audio.getReadPointer (channel);
    const int i1 = (int) frame;
    const float f = (float) (frame - i1);
    auto at = [d, n] (int k) { return d[juce::jlimit (0, n - 1, k)]; };
    const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
    if (f < 1.0e-7f)
        return y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

std::shared_ptr<RefTrack> RefTrack::fromBuffer (const juce::AudioBuffer<float>& source, double rate)
{
    auto t = std::make_shared<RefTrack>();
    const int n = source.getNumSamples();
    t->rate = rate;
    t->audio.setSize (2, juce::jmax (1, n));
    t->audio.clear();
    for (int c = 0; c < 2; ++c)
        if (n > 0 && source.getNumChannels() > 0)
            t->audio.copyFrom (c, 0, source, juce::jmin (c, source.getNumChannels() - 1), 0, n);
    t->lengthSeconds = n / rate;

    // Integrated loudness as in ITU-R BS.1770: K-weighting, 400 ms blocks every 100 ms,
    // an absolute gate at -70 LUFS and a relative gate 10 LU under the ungated mean.
    {
        std::array<std::array<Biquad, 2>, 2> k;
        for (auto& c : k)
        {
            c[0].setHighShelf (rate, 1682.0, 4.0);
            c[1].setHighPass (rate, 38.0, 0.5);
        }
        const int hop = juce::jmax (1, (int) (0.1 * rate));
        std::vector<double> hops;
        double acc = 0.0;
        int count = 0;
        for (int i = 0; i < n; ++i)
        {
            for (size_t c = 0; c < 2; ++c)
            {
                const float y = k[c][1].process (k[c][0].process (t->audio.getSample ((int) c, i)));
                acc += (double) y * y;
            }
            if (++count == hop)
            {
                hops.push_back (acc / hop);
                acc = 0.0;
                count = 0;
            }
        }
        std::vector<double> blocks;
        for (size_t i = 3; i < hops.size(); ++i)
            blocks.push_back (0.25 * (hops[i] + hops[i - 1] + hops[i - 2] + hops[i - 3]));
        if (blocks.empty() && ! hops.empty())   // shorter than 400 ms: one block
        {
            double sum = 0.0;
            for (auto h : hops) sum += h;
            blocks.push_back (sum / (double) hops.size());
        }

        auto toLufs = [] (double p) { return -0.691 + 10.0 * std::log10 (p + 1.0e-20); };
        double sum = 0.0;
        int num = 0;
        for (auto b : blocks)
            if (toLufs (b) > -70.0) { sum += b; ++num; }
        if (num > 0)
        {
            const double gate = toLufs (sum / num) - 10.0;
            double sum2 = 0.0;
            int num2 = 0;
            for (auto b : blocks)
                if (toLufs (b) > -70.0 && toLufs (b) > gate) { sum2 += b; ++num2; }
            t->lufs = (float) toLufs (sum2 / juce::jmax (1, num2));
        }
    }

    // Waveform overview
    t->peak.assign ((size_t) numColumns, 0.0f);
    t->rms.assign ((size_t) numColumns, 0.0f);
    for (int col = 0; col < numColumns && n > 0; ++col)
    {
        const int a = (int) ((juce::int64) col * n / numColumns);
        const int b = juce::jmax (a + 1, (int) ((juce::int64) (col + 1) * n / numColumns));
        float pk = 0.0f;
        double sq = 0.0;
        for (int i = a; i < juce::jmin (b, n); ++i)
        {
            const float l = t->audio.getSample (0, i), r = t->audio.getSample (1, i);
            pk = juce::jmax (pk, std::abs (l), std::abs (r));
            sq += 0.5 * ((double) l * l + (double) r * r);
        }
        t->peak[(size_t) col] = juce::jmin (1.0f, pk);
        t->rms[(size_t) col]  = juce::jmin (1.0f, (float) std::sqrt (sq / juce::jmax (1, b - a)));
    }
    return t;
}

std::shared_ptr<RefTrack> RefTrack::load (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = file.existsAsFile() ? "Can't read this file type" : "File not found";
        return nullptr;
    }
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "The file is empty";
        return nullptr;
    }

    const auto maxFrames = (juce::int64) (maxSeconds * reader->sampleRate);
    const int frames = (int) juce::jmin (reader->lengthInSamples, maxFrames);
    juce::AudioBuffer<float> buffer (2, frames);
    if (! reader->read (&buffer, 0, frames, 0, true, true))
    {
        error = "Can't decode the file";
        return nullptr;
    }
    if (reader->numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, frames);

    auto t = fromBuffer (buffer, reader->sampleRate);
    t->truncated = reader->lengthInSamples > maxFrames;
    return t;
}

//==============================================================================
ReferencePlayer::ReferencePlayer() = default;

ReferencePlayer::~ReferencePlayer()
{
    *alive = false;
    pool.removeAllJobs (true, 10000);
}

void ReferencePlayer::load (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    auto& s = slots[(size_t) slot];
    if (wantSlot.load() == slot)
        pause();
    s.file = file;
    s.status = Status::loading;
    s.error = {};
    const int gen = ++s.generation;
    bump();

    std::weak_ptr<bool> weak = alive;
    pool.addJob ([this, weak, slot, file, gen]
    {
        juce::String error;
        auto track = RefTrack::load (file, error);
        juce::MessageManager::callAsync ([this, weak, slot, file, gen, track, error]
        {
            auto ok = weak.lock();
            if (ok == nullptr || ! *ok || slots[(size_t) slot].generation != gen)
                return;
            if (track != nullptr)
                setTrack (slot, track, file);
            else
            {
                auto& st = slots[(size_t) slot];
                st.status = file.existsAsFile() ? Status::failed : Status::missing;
                st.error = error;
                bump();
            }
        });
    });
}

void ReferencePlayer::loadNow (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    ++slots[(size_t) slot].generation;
    juce::String error;
    if (auto track = RefTrack::load (file, error))
        setTrack (slot, track, file);
    else
    {
        auto& s = slots[(size_t) slot];
        s.file = file;
        s.status = file.existsAsFile() ? Status::failed : Status::missing;
        s.error = error;
        bump();
    }
}

void ReferencePlayer::setTrack (int slot, std::shared_ptr<RefTrack> track, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, numSlots) || track == nullptr)
        return;
    auto& s = slots[(size_t) slot];
    if (wantSlot.load() == slot)
        pause();

    std::shared_ptr<RefTrack> old;
    {
        const juce::SpinLock::ScopedLockType lock (trackLock);
        s.live = track.get();
        old = std::move (s.owned);
        s.owned = track;
    }
    s.file = file;
    s.status = Status::ready;
    s.error = {};

    // a loop saved with the project must still fit the file
    const double len = track->lengthSeconds;
    if (s.loopEnd.load() > len)
        s.loopEnd.store (len);
    if (s.loopStart.load() > len)
        s.loopStart.store (0.0);
    s.pendingSeek.store (getLoop (slot).exists() && s.loopOn.load() ? s.loopStart.load() : 0.0);
    bump();
}

void ReferencePlayer::clear (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    auto& s = slots[(size_t) slot];
    if (wantSlot.load() == slot)
        pause();
    std::shared_ptr<RefTrack> old;
    {
        const juce::SpinLock::ScopedLockType lock (trackLock);
        s.live = nullptr;
        old = std::move (s.owned);
    }
    ++s.generation;
    s.file = juce::File();
    s.status = Status::empty;
    s.error = {};
    s.loopStart.store (0.0);
    s.loopEnd.store (0.0);
    s.loopOn.store (false);
    s.pendingSeek.store (0.0);
    s.shownPos.store (0.0);
    bump();
}

ReferencePlayer::Status ReferencePlayer::getStatus (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].status : Status::empty;
}

juce::File ReferencePlayer::getFile (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].file : juce::File();
}

juce::String ReferencePlayer::getError (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].error : juce::String();
}

std::shared_ptr<const RefTrack> ReferencePlayer::getTrack (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].owned : nullptr;
}

void ReferencePlayer::play (int slot)
{
    if (getStatus (slot) != Status::ready)
        return;
    wantSlot.store (slot);
    bump();
}

void ReferencePlayer::pause()
{
    wantSlot.store (-1);
    bump();
}

void ReferencePlayer::togglePlay (int slot)
{
    if (isPlaying (slot))
        pause();
    else
        play (slot);
}

void ReferencePlayer::seek (int slot, double seconds)
{
    auto track = getTrack (slot);
    if (track == nullptr)
        return;
    seconds = juce::jlimit (0.0, track->lengthSeconds, seconds);
    const auto loop = getLoop (slot);
    if (loop.on && loop.exists() && (seconds < loop.start || seconds >= loop.end))
        seconds = loop.start;
    slots[(size_t) slot].pendingSeek.store (seconds);
    bump();
}

double ReferencePlayer::getPosition (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return 0.0;
    const auto& s = slots[(size_t) slot];
    const double pending = s.pendingSeek.load();
    return pending >= 0.0 ? pending : s.shownPos.load();
}

void ReferencePlayer::setLoop (int slot, double start, double end)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    if (end < start)
        std::swap (start, end);
    if (auto t = getTrack (slot))
    {
        start = juce::jlimit (0.0, t->lengthSeconds, start);
        end   = juce::jlimit (0.0, t->lengthSeconds, end);
    }
    auto& s = slots[(size_t) slot];
    s.loopStart.store (juce::jmax (0.0, start));
    s.loopEnd.store (juce::jmax (0.0, end));
    bump();
}

void ReferencePlayer::setLoopOn (int slot, bool on)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    slots[(size_t) slot].loopOn.store (on);
    bump();
}

void ReferencePlayer::clearLoop (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    auto& s = slots[(size_t) slot];
    s.loopStart.store (0.0);
    s.loopEnd.store (0.0);
    s.loopOn.store (false);
    bump();
}

ReferencePlayer::Loop ReferencePlayer::getLoop (int slot) const
{
    Loop l;
    if (juce::isPositiveAndBelow (slot, numSlots))
    {
        const auto& s = slots[(size_t) slot];
        l.start = s.loopStart.load();
        l.end = s.loopEnd.load();
        l.on = s.loopOn.load();
    }
    return l;
}

juce::ValueTree ReferencePlayer::toState() const
{
    juce::ValueTree tree (stateTag);
    tree.setProperty ("match", matchToMix.load(), nullptr);
    for (int i = 0; i < numSlots; ++i)
    {
        const auto& s = slots[(size_t) i];
        if (s.status == Status::empty || s.file == juce::File())
            continue;
        juce::ValueTree ref ("Ref");
        ref.setProperty ("slot", i, nullptr);
        ref.setProperty ("path", s.file.getFullPathName(), nullptr);
        ref.setProperty ("loopStart", s.loopStart.load(), nullptr);
        ref.setProperty ("loopEnd", s.loopEnd.load(), nullptr);
        ref.setProperty ("loopOn", s.loopOn.load(), nullptr);
        tree.addChild (ref, -1, nullptr);
    }
    return tree;
}

void ReferencePlayer::fromState (const juce::ValueTree& tree, bool loadInBackground)
{
    pause();
    matchToMix.store ((bool) tree.getProperty ("match", true));

    std::array<juce::ValueTree, numSlots> refs;
    for (auto child : tree)
        if (child.hasType ("Ref"))
        {
            const int i = (int) child.getProperty ("slot", -1);
            if (juce::isPositiveAndBelow (i, numSlots))
                refs[(size_t) i] = child;
        }

    for (int i = 0; i < numSlots; ++i)
    {
        const auto& ref = refs[(size_t) i];
        const juce::String path = ref.isValid() ? ref.getProperty ("path").toString() : juce::String();
        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
        {
            clear (i);
            continue;
        }
        // the same file already loaded (e.g. the host restores the state twice): keep it
        const juce::File file (path);
        auto& s = slots[(size_t) i];
        const bool same = s.file == file && (s.status == Status::ready || s.status == Status::loading);
        if (! same)
            clear (i);
        s.loopStart.store ((double) ref.getProperty ("loopStart", 0.0));
        s.loopEnd.store ((double) ref.getProperty ("loopEnd", 0.0));
        s.loopOn.store ((bool) ref.getProperty ("loopOn", false));
        if (same)
            continue;
        if (loadInBackground)
            load (i, file);
        else
            loadNow (i, file);
    }
    bump();
}

//==============================================================================
void ReferencePlayer::prepare (double sampleRate)
{
    sr = sampleRate;
    envStep = (float) (1.0 / (0.008 * sr));
    current = -1;
    refEnv = 0.0f;
    mixEnv = 1.0f;
}

double ReferencePlayer::loopStartFrames (const SlotState& s, const RefTrack& t, bool& active) const noexcept
{
    const double ls = s.loopStart.load(), le = s.loopEnd.load();
    active = s.loopOn.load() && le - ls >= minLoopSeconds && ls < t.lengthSeconds;
    return ls * t.rate;
}

bool ReferencePlayer::process (float* l, float* r, int n) noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (trackLock);
    if (! lock.isLocked())
        return current >= 0 || mixEnv < 1.0f;   // the message thread is swapping a track: skip one block

    // Seeks on slots that aren't playing apply at once; the playing one fades first
    double heldSeek = -1.0;
    for (int i = 0; i < numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        const double ps = s.pendingSeek.exchange (-1.0);
        if (ps < 0.0)
            continue;
        if (i == current)
            heldSeek = ps;
        else
        {
            s.pos = s.live != nullptr ? juce::jlimit (0.0, (double) s.live->audio.getNumSamples() - 1.0, ps * s.live->rate) : 0.0;
            s.shownPos.store (ps);
        }
    }

    if (current >= 0 && slots[(size_t) current].live == nullptr)
    {
        current = -1;   // cleared while playing
        refEnv = 0.0f;
    }

    int want = wantSlot.load();
    if (want >= 0 && slots[(size_t) want].live == nullptr)
    {
        wantSlot.compare_exchange_strong (want, -1);
        want = -1;
    }

    if (current < 0 && want < 0 && mixEnv >= 1.0f)
        return false;

    const RefTrack* t = nullptr;
    bool loopActive = false;
    double ls = 0.0, le = 0.0, xf = 1.0, step = 1.0, len = 0.0;
    float gainTarget = 1.0f;

    auto setup = [&]
    {
        t = current >= 0 ? slots[(size_t) current].live : nullptr;
        if (t == nullptr)
            return;
        auto& s = slots[(size_t) current];
        ls = loopStartFrames (s, *t, loopActive);
        len = (double) t->audio.getNumSamples();
        le = juce::jmin (len, s.loopEnd.load() * t->rate);
        xf = juce::jmax (1.0, juce::jmin (0.01 * t->rate, (le - ls) * 0.25));
        step = t->rate / sr;
        float db = 0.0f;
        const float mix = mixLufs.load();
        if (matchToMix.load() && mix > -90.0f && t->lufs > -90.0f)
            db = juce::jlimit (-30.0f, 12.0f, mix - t->lufs);
        gainDbShown.store (db);
        gainTarget = std::pow (10.0f, db * 0.05f);
    };

    auto applySeek = [&] (int slot, double seconds)
    {
        auto& s = slots[(size_t) slot];
        if (s.live == nullptr)
            return;
        s.pos = juce::jlimit (0.0, (double) s.live->audio.getNumSamples() - 1.0, seconds * s.live->rate);
    };

    setup();
    if (t != nullptr && heldSeek < 0.0)
    {
        // the loop moved away from the playhead, or a new file is shorter: jump with a fade
        const double p = slots[(size_t) current].pos;
        if (p >= len || (loopActive && (p < ls - 0.5 || p >= le)))
            heldSeek = loopActive ? ls / t->rate : 0.0;
    }

    const float mixTarget = want >= 0 ? 0.0f : 1.0f;
    const float gainCoef = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));

    for (int i = 0; i < n; ++i)
    {
        if (current != want || heldSeek >= 0.0)
        {
            refEnv = juce::jmax (0.0f, refEnv - envStep);
            if (refEnv <= 0.0f)
            {
                if (current >= 0 && heldSeek >= 0.0)
                    applySeek (current, heldSeek);
                heldSeek = -1.0;
                current = want;
                setup();
                if (t != nullptr)
                {
                    // starting outside the loop: start at the loop
                    auto& p = slots[(size_t) current].pos;
                    if (p >= len)
                        p = 0.0;
                    if (loopActive && (p < ls - 0.5 || p >= le))
                        p = ls;
                    gain = gainTarget;
                }
            }
        }
        else if (current >= 0)
        {
            refEnv = juce::jmin (1.0f, refEnv + envStep);
        }

        mixEnv = mixTarget > mixEnv ? juce::jmin (mixTarget, mixEnv + envStep)
                                    : juce::jmax (mixTarget, mixEnv - envStep);

        float rl = 0.0f, rr = 0.0f;
        if (t != nullptr && refEnv > 0.0f)
        {
            auto& p = slots[(size_t) current].pos;
            rl = t->sampleAt (0, p);
            rr = t->sampleAt (1, p);
            if (loopActive && p >= le - xf)
            {
                // crossfade the loop end into the audio just before the loop start
                const double into = p - (le - xf);
                const float w = (float) juce::jlimit (0.0, 1.0, into / xf);
                const double head = ls - xf + into;
                const float hl = head >= 0.0 ? t->sampleAt (0, head) : 0.0f;
                const float hr = head >= 0.0 ? t->sampleAt (1, head) : 0.0f;
                const float a = std::sqrt (1.0f - w), b = std::sqrt (w);
                rl = a * rl + b * hl;
                rr = a * rr + b * hr;
            }
            p += step;
            if (loopActive && p >= le)
                p = ls + (p - le);
            else if (p >= len)
            {
                // the end of the track: stop and rewind
                int playing = current;
                wantSlot.compare_exchange_strong (playing, -1);
                want = wantSlot.load();
                p = 0.0;
                slots[(size_t) current].shownPos.store (0.0);
                current = -1;
                refEnv = 0.0f;
                t = nullptr;
                bump();
            }
        }

        gain += (gainTarget - gain) * gainCoef;
        const float g = refEnv * gain;
        l[i] = l[i] * mixEnv + rl * g;
        r[i] = r[i] * mixEnv + rr * g;
    }

    if (current >= 0 && t != nullptr)
        slots[(size_t) current].shownPos.store (slots[(size_t) current].pos / t->rate);

    return current >= 0 || mixEnv < 1.0f;
}
} // namespace ddr
