#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <memory>

namespace ddr
{
/** A loaded reference track. Never changes after loading, so the audio thread can read it freely. */
struct RefTrack
{
    juce::AudioBuffer<float> audio;   // always two channels, at the file's own sample rate
    double rate = 44100.0;
    double lengthSeconds = 0.0;
    float  lufs = -100.0f;            // integrated loudness, as played (mono files count as two channels)
    bool   truncated = false;         // longer than maxSeconds: only the start was loaded
    std::vector<float> peak, rms;     // waveform overview, numColumns values each (0..1)

    static constexpr int numColumns = 1200;
    static constexpr double maxSeconds = 12.0 * 60.0;

    /** Decodes and analyses a file (any thread). Returns nullptr and an error message on failure. */
    static std::shared_ptr<RefTrack> load (const juce::File&, juce::String& error);
    /** Builds a track from audio already in memory (the test tools use this). */
    static std::shared_ptr<RefTrack> fromBuffer (const juce::AudioBuffer<float>&, double rate);

    float sampleAt (int channel, double frame) const noexcept;
};

/** Plays the reference tracks in place of the mix.

    The message thread loads, clears, plays, pauses, seeks and sets loops; the audio thread
    calls process(). Switching between the mix and a reference, seeking and pausing all fade
    for a few milliseconds, and a loop crossfades at its end, so nothing clicks. */
class ReferencePlayer
{
public:
    static constexpr int numSlots = 2;
    static constexpr double minLoopSeconds = 0.1;

    enum class Status { empty, loading, ready, missing, failed };

    struct Loop
    {
        double start = 0.0, end = 0.0;   // seconds; no loop when end <= start
        bool on = false;
        bool exists() const noexcept { return end - start >= minLoopSeconds; }
    };

    ReferencePlayer();
    ~ReferencePlayer();

    //== message thread =========================================================
    void load (int slot, const juce::File&);          // in the background
    void loadNow (int slot, const juce::File&);       // synchronously (tests, no message loop)
    void setTrack (int slot, std::shared_ptr<RefTrack>, const juce::File& file = {});
    void clear (int slot);

    Status getStatus (int slot) const;
    juce::File getFile (int slot) const;
    juce::String getError (int slot) const;
    std::shared_ptr<const RefTrack> getTrack (int slot) const;

    void play (int slot);
    void pause();
    void togglePlay (int slot);
    int  getPlayingSlot() const noexcept { return wantSlot.load(); }   // -1 = the mix is playing
    bool isPlaying (int slot) const noexcept { return wantSlot.load() == slot; }

    /** Jumps to a time in seconds. With the loop on, a time outside it jumps to the loop start. */
    void seek (int slot, double seconds);
    double getPosition (int slot) const;              // seconds

    void setLoop (int slot, double start, double end); // keeps the loop on/off state
    void setLoopOn (int slot, bool on);
    void clearLoop (int slot);
    Loop getLoop (int slot) const;

    void setMatchToMix (bool on) { matchToMix.store (on); bump(); }
    bool getMatchToMix() const noexcept { return matchToMix.load(); }

    /** The level the reference is played at, in dB (what the match does). */
    float getGainDb() const noexcept { return gainDbShown.load(); }

    /** Counts every change, so a view can tell when to redraw. */
    int getChangeCount() const noexcept { return changes.load(); }

    juce::ValueTree toState() const;
    void fromState (const juce::ValueTree&, bool loadInBackground = true);
    static inline const juce::Identifier stateTag { "References" };

    //== audio thread ===========================================================
    void prepare (double sampleRate);

    /** The mix loudness in LUFS (or below -90 when unknown); the reference is matched to it. */
    void setMixLoudness (float lufs) noexcept { mixLufs.store (lufs); }

    /** Replaces (or crossfades) the mix in l and r with the playing reference.
        Returns true while any of the output comes from a reference. */
    bool process (float* l, float* r, int n) noexcept;

private:
    struct SlotState
    {
        // message thread
        std::shared_ptr<RefTrack> owned;
        juce::File file;
        juce::String error;
        Status status = Status::empty;
        int generation = 0;

        // shared
        RefTrack* live = nullptr;                 // guarded by trackLock
        std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };
        std::atomic<bool>   loopOn { false };
        std::atomic<double> pendingSeek { -1.0 };  // seconds, -1 = none
        std::atomic<double> shownPos { 0.0 };      // seconds, published by the audio thread

        // audio thread
        double pos = 0.0;                          // in frames of the file
    };

    void bump() noexcept { changes.fetch_add (1); }
    double loopStartFrames (const SlotState&, const RefTrack&, bool& active) const noexcept;

    std::array<SlotState, numSlots> slots;
    juce::SpinLock trackLock;
    std::atomic<int> wantSlot { -1 };
    std::atomic<bool> matchToMix { true };
    std::atomic<float> mixLufs { -100.0f }, gainDbShown { 0.0f };
    std::atomic<int> changes { 0 };

    // audio thread
    double sr = 44100.0;
    int current = -1;
    float refEnv = 0.0f, mixEnv = 1.0f, envStep = 0.01f;
    float gain = 1.0f;

    juce::ThreadPool pool { 1 };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};
} // namespace ddr
