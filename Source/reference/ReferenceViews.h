#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ReferencePlayer.h"

namespace ddr
{
/** Draws a reference's waveform between two times, with its loop and playhead. */
void drawRefWaveform (juce::Graphics&, juce::Rectangle<float> area, const RefTrack&, double viewStart, double viewEnd,
                      ReferencePlayer::Loop loop, double position, bool playing);

/** The small reference card: play/pause, seek by clicking the waveform, open the big view
    by clicking the name. An empty card loads a file (click, or drop one on it). */
class RefCard : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    RefCard (ReferencePlayer&, int slot);

    std::function<void (int slot)> onOpen;    // the name or info was clicked
    std::function<void (int slot)> onLoad;    // asks for a file

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    juce::Rectangle<int> playArea() const   { return { 10, 9, 22, 22 }; }
    juce::Rectangle<int> closeArea() const  { return { getWidth() - 30, 8, 22, 22 }; }
    juce::Rectangle<int> infoArea() const   { return { 36, 6, getWidth() - 70, 44 }; }
    juce::Rectangle<int> waveArea() const   { return { 10, 52, getWidth() - 20, getHeight() - 58 }; }

private:
    enum class Zone { none, play, close, info, wave, load };
    Zone zoneAt (juce::Point<int>) const;
    void seekTo (int x);

    ReferencePlayer& player;
    const int slot;
    Zone hover = Zone::none, pressed = Zone::none;
    bool dragOver = false;
};

/** The big view of one reference: choose a loop by dragging, fine-tune its edges, play and pause. */
class RefDetailPanel : public juce::Component, private juce::Timer
{
public:
    RefDetailPanel (ReferencePlayer&, int slot);

    std::function<void()> onClose;
    void setSlot (int newSlot);
    int  getSlot() const noexcept { return slot; }

    void paint (juce::Graphics&) override;
    void resized() override;

    class Wave : public juce::Component
    {
    public:
        Wave (ReferencePlayer& p, int& s) : player (p), slot (s) {}
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        juce::Rectangle<float> waveBounds() const { return getLocalBounds().toFloat().withTrimmedBottom (18.0f); }
        double timeAt (float x) const;
        float  xOf (double t) const;

    private:
        enum class Drag { none, pending, newLoop, start, end };
        ReferencePlayer& player;
        int& slot;
        Drag drag = Drag::none;
        double anchor = 0.0;
        float downX = 0.0f;
    };

private:
    void timerCallback() override;
    void update();

    ReferencePlayer& player;
    int slot;
    int lastChange = -1;
    Wave wave;
    juce::TextButton playButton { "PLAY" }, loopButton { "LOOP" }, clearLoopButton { "CLEAR LOOP" },
                     otherButton { "REF 2" }, closeButton { "BACK" };
    juce::Label hint, loopText;
};

/** The two reference cards under the meter, with the match switch. */
class ReferenceStrip : public juce::Component, private juce::Timer
{
public:
    explicit ReferenceStrip (ReferencePlayer&);

    std::function<void (int slot)> onOpen;
    std::function<float()> mixLoudness;   // LUFS, or below -90 when the mix hasn't played yet

    void resized() override;
    void paint (juce::Graphics&) override;
    juce::String getStatusText() const;

    /** Updates the text and cards now instead of on the next timer tick. */
    void refresh() { timerCallback(); }

    /** Asks for a file and loads it into a slot. */
    void chooseFile (int slot);

private:
    void timerCallback() override;

    ReferencePlayer& player;
    std::array<std::unique_ptr<RefCard>, ReferencePlayer::numSlots> cards;
    juce::TextButton matchButton { "MATCH TO MIX" };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastChange = -1;
    bool wasPlaying = false;
};
} // namespace ddr
