#include "ReferenceViews.h"
#include "../PluginEditor.h"

namespace ddr
{
namespace
{
    const juce::Colour refColour { 0xffb48cff };   // lilac, so references never look like the amber car controls
    const char* audioWildcard = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf";

    juce::String formatTime (double seconds, bool tenths = false)
    {
        seconds = juce::jmax (0.0, seconds);
        const int m = (int) (seconds / 60.0);
        const double s = seconds - m * 60.0;
        if (tenths)
            return juce::String (m) + ":" + juce::String (s, 1).paddedLeft ('0', 4);
        return juce::String (m) + ":" + juce::String ((int) s).paddedLeft ('0', 2);
    }

    juce::FontOptions mono (float size)
    {
        return juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain);
    }

    bool isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf");
    }

    void drawPlayIcon (juce::Graphics& g, juce::Rectangle<float> r, bool playing, bool hover)
    {
        g.setColour (hover ? refColour.brighter (0.25f) : refColour);
        g.fillEllipse (r);
        g.setColour (Colours::background);
        auto c = r.getCentre();
        const float s = r.getWidth() * 0.22f;
        if (playing)
        {
            g.fillRect (c.x - s * 0.95f, c.y - s, s * 0.7f, s * 2.0f);
            g.fillRect (c.x + s * 0.25f, c.y - s, s * 0.7f, s * 2.0f);
        }
        else
        {
            juce::Path p;
            p.addTriangle (c.x - s * 0.7f, c.y - s * 1.1f, c.x - s * 0.7f, c.y + s * 1.1f, c.x + s * 1.2f, c.y);
            g.fillPath (p);
        }
    }
}

//==============================================================================
void drawRefWaveform (juce::Graphics& g, juce::Rectangle<float> area, const RefTrack& t, double viewStart, double viewEnd,
                      ReferencePlayer::Loop loop, double position, bool playing)
{
    if (area.getWidth() < 2.0f || t.lengthSeconds <= 0.0 || viewEnd <= viewStart)
        return;

    auto xOf = [&] (double sec) { return area.getX() + (float) ((sec - viewStart) / (viewEnd - viewStart)) * area.getWidth(); };

    if (loop.exists())
    {
        const float x0 = juce::jmax (area.getX(), xOf (loop.start)), x1 = juce::jmin (area.getRight(), xOf (loop.end));
        if (x1 > x0)
        {
            g.setColour (loop.on ? refColour.withAlpha (0.22f) : Colours::dim.withAlpha (0.12f));
            g.fillRect (x0, area.getY(), x1 - x0, area.getHeight());
            g.setColour (loop.on ? refColour : Colours::dim);
            g.fillRect (x0, area.getY(), 1.5f, area.getHeight());
            g.fillRect (x1 - 1.5f, area.getY(), 1.5f, area.getHeight());
        }
    }

    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
    const int cols = (int) t.peak.size();
    const float playX = xOf (position);
    for (int px = 0; px < (int) area.getWidth(); ++px)
    {
        const double a = viewStart + (viewEnd - viewStart) * px / area.getWidth();
        const double b = viewStart + (viewEnd - viewStart) * (px + 1) / area.getWidth();
        int c0 = (int) (a / t.lengthSeconds * cols), c1 = (int) (b / t.lengthSeconds * cols);
        c0 = juce::jlimit (0, cols - 1, c0);
        c1 = juce::jlimit (c0 + 1, cols, c1);
        float pk = 0.0f, rm = 0.0f;
        for (int c = c0; c < c1; ++c)
        {
            pk = juce::jmax (pk, t.peak[(size_t) c]);
            rm = juce::jmax (rm, t.rms[(size_t) c]);
        }
        if (a >= t.lengthSeconds)
            break;
        const float x = area.getX() + (float) px;
        const bool played = x < playX;
        const juce::Colour col = played ? refColour.brighter (0.3f) : refColour.withAlpha (0.75f);
        g.setColour (col.withMultipliedAlpha (0.45f));
        g.fillRect (x, mid - pk * half, 1.0f, juce::jmax (1.0f, 2.0f * pk * half));
        g.setColour (col);
        g.fillRect (x, mid - rm * half, 1.0f, juce::jmax (1.0f, 2.0f * rm * half));
    }

    if (position >= viewStart && position <= viewEnd)
    {
        g.setColour (playing ? Colours::text : Colours::text.withAlpha (0.6f));
        g.fillRect (playX - 0.75f, area.getY() - 2.0f, 1.5f, area.getHeight() + 4.0f);
    }
}

//==============================================================================
RefCard::RefCard (ReferencePlayer& p, int s) : player (p), slot (s)
{
    setRepaintsOnMouseActivity (false);
}

RefCard::Zone RefCard::zoneAt (juce::Point<int> pt) const
{
    const auto status = player.getStatus (slot);
    if (status == ReferencePlayer::Status::empty)
        return Zone::load;
    if (closeArea().expanded (2).contains (pt))
        return Zone::close;
    if (status != ReferencePlayer::Status::ready)
        return status == ReferencePlayer::Status::loading ? Zone::none : Zone::load;
    if (playArea().expanded (3).contains (pt))
        return Zone::play;
    if (waveArea().expanded (0, 3).contains (pt))
        return Zone::wave;
    if (infoArea().contains (pt))
        return Zone::info;
    return Zone::none;
}

void RefCard::paint (juce::Graphics& g)
{
    const auto status = player.getStatus (slot);
    const bool playing = player.isPlaying (slot);
    auto r = getLocalBounds().toFloat().reduced (0.75f);

    g.setColour (Colours::background.withAlpha (0.82f));
    g.fillRoundedRectangle (r, 10.0f);

    if (status == ReferencePlayer::Status::empty)
    {
        juce::Path outline;
        outline.addRoundedRectangle (r.reduced (1.0f), 10.0f);
        juce::Path dashed;
        const float dashes[] { 6.0f, 5.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (dragOver || hover == Zone::load ? refColour : Colours::dim.withAlpha (0.6f));
        g.fillPath (dashed);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("+  LOAD REFERENCE " + juce::String (slot + 1), r.withTrimmedBottom (r.getHeight() * 0.35f),
                    juce::Justification::centredBottom);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Click, or drop an audio file here. It plays through DDR in place of your mix.",
                    r.withTrimmedTop (r.getHeight() * 0.62f).reduced (8.0f, 0.0f), juce::Justification::centredTop);
        return;
    }

    g.setColour (playing ? refColour : (dragOver ? refColour.withAlpha (0.7f) : Colours::panelEdge.brighter (0.25f)));
    g.drawRoundedRectangle (r, 10.0f, playing ? 2.0f : 1.2f);

    // close
    {
        auto c = closeArea().toFloat().reduced (6.0f);
        g.setColour (hover == Zone::close ? Colours::text : Colours::dim);
        g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.6f);
        g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.6f);
    }

    const auto file = player.getFile (slot);
    const juce::String title = "Ref " + juce::String (slot + 1) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + file.getFileName();
    auto info = infoArea().toFloat();
    g.setColour (hover == Zone::info ? Colours::text.brighter() : Colours::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawFittedText (title, info.removeFromTop (24.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    g.setFont (mono (12.0f));
    if (status == ReferencePlayer::Status::loading)
    {
        g.setColour (Colours::dim);
        g.drawText ("loading and measuring...", info.toNearestInt(), juce::Justification::centredLeft);
        return;
    }
    if (status != ReferencePlayer::Status::ready)
    {
        g.setColour (Colours::warn);
        g.drawText (player.getError (slot) + dot + "click to choose it again", info.toNearestInt(), juce::Justification::centredLeft);
        return;
    }

    auto track = player.getTrack (slot);
    if (track == nullptr)
        return;

    drawPlayIcon (g, playArea().toFloat(), playing, hover == Zone::play);

    const auto loop = player.getLoop (slot);
    juce::String text = juce::String (track->lufs, 1) + " LUFS" + dot + formatTime (player.getPosition (slot))
                        + " / " + formatTime (track->lengthSeconds);
    if (loop.exists())
        text << dot << "loop " << formatTime (loop.start) << "-" << formatTime (loop.end) << (loop.on ? "" : " (off)");
    else
        text << dot << "click name to loop";
    g.setColour (Colours::dim);
    g.drawFittedText (text, info.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

    drawRefWaveform (g, waveArea().toFloat(), *track, 0.0, track->lengthSeconds, loop, player.getPosition (slot), playing);
}

void RefCard::seekTo (int x)
{
    if (auto track = player.getTrack (slot))
    {
        const auto w = waveArea();
        player.seek (slot, juce::jlimit (0.0, 1.0, (double) (x - w.getX()) / juce::jmax (1, w.getWidth())) * track->lengthSeconds);
        repaint();
    }
}

void RefCard::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.getPosition());
    if (z != hover)
    {
        hover = z;
        setMouseCursor (z == Zone::none || z == Zone::wave ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        setTooltip (z == Zone::play  ? "Play or pause this reference. It plays in place of your mix, through the car."
                  : z == Zone::wave  ? "Click or drag to jump. With the loop on, playback stays in the loop."
                  : z == Zone::info  ? "Open the big view to choose a loop."
                  : z == Zone::close ? "Remove this reference."
                  : z == Zone::load  ? "Load a reference track: WAV, AIFF, FLAC, MP3 or OGG."
                                     : "");
        repaint();
    }
}

void RefCard::mouseExit (const juce::MouseEvent&)
{
    hover = Zone::none;
    repaint();
}

void RefCard::mouseDown (const juce::MouseEvent& e)
{
    pressed = zoneAt (e.getPosition());
    switch (pressed)
    {
        case Zone::play:  player.togglePlay (slot); repaint(); break;
        case Zone::wave:  seekTo (e.x); break;
        case Zone::close: player.clear (slot); hover = Zone::none; repaint(); break;
        case Zone::info:  if (onOpen != nullptr) onOpen (slot); break;
        case Zone::load:  if (onLoad != nullptr) onLoad (slot); break;
        case Zone::none:  break;
    }
}

void RefCard::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed == Zone::wave)
        seekTo (e.x);
}

void RefCard::mouseUp (const juce::MouseEvent&)
{
    pressed = Zone::none;
}

bool RefCard::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (f))
            return true;
    return false;
}

void RefCard::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (isAudioFile (f))
        {
            player.clearLoop (slot);
            player.load (slot, juce::File (f));
            break;
        }
    repaint();
}

//==============================================================================
double RefDetailPanel::Wave::timeAt (float x) const
{
    auto track = player.getTrack (slot);
    if (track == nullptr)
        return 0.0;
    const auto w = waveBounds();
    return juce::jlimit (0.0, 1.0, (double) ((x - w.getX()) / w.getWidth())) * track->lengthSeconds;
}

float RefDetailPanel::Wave::xOf (double t) const
{
    auto track = player.getTrack (slot);
    const auto w = waveBounds();
    if (track == nullptr || track->lengthSeconds <= 0.0)
        return w.getX();
    return w.getX() + (float) (t / track->lengthSeconds) * w.getWidth();
}

void RefDetailPanel::Wave::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Colours::background.withAlpha (0.9f));
    g.fillRoundedRectangle (bounds.withTrimmedBottom (18.0f).expanded (0.0f, 6.0f).reduced (0.0f, 6.0f), 8.0f);

    auto track = player.getTrack (slot);
    if (track == nullptr)
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Nothing loaded in this slot.", bounds, juce::Justification::centred);
        return;
    }

    const auto w = waveBounds().reduced (0.0f, 8.0f);
    g.setColour (Colours::panelEdge);
    g.fillRect (w.getX(), w.getCentreY(), w.getWidth(), 1.0f);
    drawRefWaveform (g, w, *track, 0.0, track->lengthSeconds, player.getLoop (slot), player.getPosition (slot), player.isPlaying (slot));

    // the loop's edges get handles, so it's clear they can be dragged
    const auto loop = player.getLoop (slot);
    if (loop.exists())
        for (double t : { loop.start, loop.end })
        {
            const float x = xOf (t);
            g.setColour (loop.on ? refColour : Colours::dim);
            g.fillRoundedRectangle (x - 4.0f, w.getY() - 6.0f, 8.0f, 12.0f, 2.0f);
            g.fillRoundedRectangle (x - 4.0f, w.getBottom() - 6.0f, 8.0f, 12.0f, 2.0f);
        }

    // time ruler
    const double len = track->lengthSeconds;
    double stepSec = 1.0;
    for (double s : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0 })
    {
        stepSec = s;
        if (len / s <= 14.0)
            break;
    }
    g.setFont (mono (11.0f));
    const float rulerY = bounds.getBottom() - 16.0f;
    for (double t = 0.0; t <= len + 1.0e-6; t += stepSec)
    {
        const float x = xOf (t);
        g.setColour (Colours::panelEdge.brighter (0.4f));
        g.fillRect (x, rulerY - 2.0f, 1.0f, 4.0f);
        g.setColour (Colours::dim);
        g.drawText (formatTime (t), juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ x, rulerY + 8.0f })
                                        .constrainedWithin (bounds), juce::Justification::centred);
    }
}

void RefDetailPanel::Wave::mouseMove (const juce::MouseEvent& e)
{
    const auto loop = player.getLoop (slot);
    const bool nearEdge = loop.exists() && (std::abs (e.position.x - xOf (loop.start)) < 7.0f || std::abs (e.position.x - xOf (loop.end)) < 7.0f);
    setMouseCursor (nearEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::IBeamCursor);
}

void RefDetailPanel::Wave::mouseDown (const juce::MouseEvent& e)
{
    const auto loop = player.getLoop (slot);
    downX = e.position.x;
    anchor = timeAt (e.position.x);
    drag = Drag::pending;
    if (loop.exists())
    {
        const float ds = std::abs (e.position.x - xOf (loop.start)), de = std::abs (e.position.x - xOf (loop.end));
        if (juce::jmin (ds, de) < 7.0f)
            drag = ds <= de ? Drag::start : Drag::end;
    }
}

void RefDetailPanel::Wave::mouseDrag (const juce::MouseEvent& e)
{
    const double t = timeAt (e.position.x);
    const auto loop = player.getLoop (slot);
    switch (drag)
    {
        case Drag::pending:
            if (std::abs (e.position.x - downX) < 4.0f)
                return;
            drag = Drag::newLoop;
            player.setLoopOn (slot, true);
            [[fallthrough]];
        case Drag::newLoop:
            player.setLoop (slot, juce::jmin (anchor, t), juce::jmax (anchor, t));
            break;
        case Drag::start:
            if (t > loop.end) { drag = Drag::end; player.setLoop (slot, loop.end, t); }
            else player.setLoop (slot, t, loop.end);
            break;
        case Drag::end:
            if (t < loop.start) { drag = Drag::start; player.setLoop (slot, t, loop.start); }
            else player.setLoop (slot, loop.start, t);
            break;
        case Drag::none:
            break;
    }
    repaint();
}

void RefDetailPanel::Wave::mouseUp (const juce::MouseEvent&)
{
    const auto loop = player.getLoop (slot);
    if (drag == Drag::pending)
    {
        // a click: jump there. Outside the loop that means you want to hear something else, so the loop goes off.
        if (loop.on && loop.exists() && (anchor < loop.start || anchor >= loop.end))
            player.setLoopOn (slot, false);
        player.seek (slot, anchor);
    }
    else if (drag != Drag::none && ! loop.exists())
    {
        player.clearLoop (slot);   // too short to be a loop
    }
    else if (drag == Drag::newLoop && ! player.isPlaying (slot))
    {
        player.seek (slot, loop.start);   // a new loop: be ready to play it from the start
    }
    drag = Drag::none;
    repaint();
}

//==============================================================================
RefDetailPanel::RefDetailPanel (ReferencePlayer& p, int s) : player (p), slot (s), wave (p, slot)
{
    addAndMakeVisible (wave);
    for (auto* b : { &playButton, &loopButton, &clearLoopButton, &otherButton, &closeButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    playButton.onClick      = [this] { player.togglePlay (slot); update(); };
    loopButton.onClick      = [this] { player.setLoopOn (slot, ! player.getLoop (slot).on); update(); };
    clearLoopButton.onClick = [this] { player.clearLoop (slot); update(); };
    otherButton.onClick     = [this] { setSlot (1 - slot); };
    closeButton.onClick     = [this] { if (onClose != nullptr) onClose(); };

    playButton.setTooltip ("Play or pause. Plays the loop when the loop is on.");
    loopButton.setTooltip ("Loop on: playback repeats the marked part. The loop stays when you go back.");
    clearLoopButton.setTooltip ("Remove the loop.");
    closeButton.setTooltip ("Back to DDR. The loop and the playback keep going.");

    for (auto* l : { &hint, &loopText })
    {
        l->setColour (juce::Label::textColourId, Colours::dim);
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }
    hint.setFont (juce::FontOptions (13.0f));
    hint.setText ("Drag across the waveform to choose a loop. Drag its edges to fine-tune. Click to jump.",
                  juce::dontSendNotification);
    loopText.setFont (mono (13.0f));

    update();
    startTimerHz (30);
}

void RefDetailPanel::setSlot (int newSlot)
{
    slot = juce::jlimit (0, ReferencePlayer::numSlots - 1, newSlot);
    lastChange = -1;
    update();
    repaint();
}

void RefDetailPanel::update()
{
    const bool ready = player.getStatus (slot) == ReferencePlayer::Status::ready;
    const auto loop = player.getLoop (slot);
    playButton.setButtonText (player.isPlaying (slot) ? "PAUSE" : (loop.on && loop.exists() ? "PLAY LOOP" : "PLAY"));
    playButton.setToggleState (player.isPlaying (slot), juce::dontSendNotification);
    playButton.setEnabled (ready);
    loopButton.setButtonText (loop.on ? "LOOP ON" : "LOOP OFF");
    loopButton.setToggleState (loop.on && loop.exists(), juce::dontSendNotification);
    loopButton.setEnabled (ready && loop.exists());
    clearLoopButton.setEnabled (ready && loop.exists());

    const int other = 1 - slot;
    otherButton.setButtonText ("REF " + juce::String (other + 1));
    otherButton.setVisible (player.getStatus (other) == ReferencePlayer::Status::ready);

    loopText.setText (loop.exists() ? "Loop " + formatTime (loop.start, true) + " - " + formatTime (loop.end, true)
                                          + "  (" + juce::String (loop.end - loop.start, 1) + " s)" + (loop.on ? "" : "  off")
                                    : juce::String ("No loop: the whole track plays"),
                      juce::dontSendNotification);
}

void RefDetailPanel::timerCallback()
{
    const int c = player.getChangeCount();
    if (c != lastChange || player.isPlaying (slot))
    {
        lastChange = c;
        update();
        wave.repaint();
        repaint (0, 0, getWidth(), 110);
    }
}

void RefDetailPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    auto area = getLocalBounds().reduced (16);
    g.setColour (refColour);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("REFERENCE " + juce::String (slot + 1), area.getX(), area.getY(), 220, 32, juce::Justification::centredLeft);

    auto track = player.getTrack (slot);
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawFittedText (player.getFile (slot).getFileName(), area.getX(), area.getY() + 44, area.getWidth(), 22,
                      juce::Justification::centredLeft, 1, 0.8f);
    if (track != nullptr)
    {
        juce::String info = juce::String (track->lufs, 1) + " LUFS" + dot + formatTime (player.getPosition (slot), true)
                            + " / " + formatTime (track->lengthSeconds) + dot + juce::String (track->rate / 1000.0, 1) + " kHz";
        if (track->truncated)
            info << dot << "only the first " << (int) (RefTrack::maxSeconds / 60.0) << " min loaded";
        g.setColour (Colours::dim);
        g.setFont (mono (13.0f));
        g.drawText (info, area.getX(), area.getY() + 68, area.getWidth(), 18, juce::Justification::centredLeft);
    }
}

void RefDetailPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto header = area.removeFromTop (32);
    closeButton.setBounds (header.removeFromRight (100).reduced (0, 2));
    header.removeFromRight (8);
    otherButton.setBounds (header.removeFromRight (80).reduced (0, 2));
    area.removeFromTop (64);

    auto bottom = area.removeFromBottom (24);
    hint.setBounds (bottom);
    area.removeFromBottom (10);
    auto controls = area.removeFromBottom (32);
    playButton.setBounds (controls.removeFromLeft (120));
    controls.removeFromLeft (8);
    loopButton.setBounds (controls.removeFromLeft (110));
    controls.removeFromLeft (8);
    clearLoopButton.setBounds (controls.removeFromLeft (120));
    controls.removeFromLeft (16);
    loopText.setBounds (controls);
    area.removeFromBottom (12);
    wave.setBounds (area);
}

//==============================================================================
ReferenceStrip::ReferenceStrip (ReferencePlayer& p) : player (p)
{
    for (int i = 0; i < ReferencePlayer::numSlots; ++i)
    {
        cards[(size_t) i] = std::make_unique<RefCard> (player, i);
        cards[(size_t) i]->onOpen = [this] (int s) { if (onOpen != nullptr) onOpen (s); };
        cards[(size_t) i]->onLoad = [this] (int s) { chooseFile (s); };
        addAndMakeVisible (*cards[(size_t) i]);
    }

    matchButton.setClickingTogglesState (false);
    matchButton.onClick = [this] { player.setMatchToMix (! player.getMatchToMix()); };
    matchButton.setTooltip ("Plays the references as loud as your mix, so you compare the sound, not the level.");
    addAndMakeVisible (matchButton);

    status.setFont (juce::FontOptions (12.0f));
    status.setColour (juce::Label::textColourId, Colours::dim);
    status.setJustificationType (juce::Justification::centredRight);
    status.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (status);

    timerCallback();
    startTimerHz (30);
}

juce::String ReferenceStrip::getStatusText() const
{
    const int playing = player.getPlayingSlot();
    const float mix = mixLoudness != nullptr ? mixLoudness() : -100.0f;
    juce::String text;
    if (playing >= 0)
        text << "Ref " << (playing + 1) << " plays instead of your mix";
    if (player.getMatchToMix())
    {
        if (! text.isEmpty()) text << ", ";
        if (mix > -90.0f)
        {
            if (playing >= 0)
            {
                const float db = player.getGainDb();
                text << "turned " << (db >= 0.0f ? "up " : "down ") << juce::String (std::abs (db), 1) << " dB to your mix";
            }
            else
                text << "matched to your mix (" << juce::String (mix, 1) << " LUFS)";
        }
        else
            text << (playing >= 0 ? "not matched yet: play your mix first" : "play your mix first, then refs match its level");
    }
    return text;
}

void ReferenceStrip::chooseFile (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose reference track " + juce::String (slot + 1),
        juce::File::getSpecialLocation (juce::File::userMusicDirectory), audioWildcard);
    juce::Component::SafePointer<ReferenceStrip> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, slot] (const juce::FileChooser& fc)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || ! fc.getResult().existsAsFile())
            return;
        self->player.clearLoop (slot);
        self->player.load (slot, fc.getResult());
    });
}

void ReferenceStrip::timerCallback()
{
    const int c = player.getChangeCount();
    const bool playing = player.getPlayingSlot() >= 0;
    if (c != lastChange || playing || wasPlaying)
    {
        lastChange = c;
        for (auto& card : cards)
            card->repaint();
    }
    wasPlaying = playing;
    matchButton.setToggleState (player.getMatchToMix(), juce::dontSendNotification);
    status.setText (getStatusText(), juce::dontSendNotification);
}

void ReferenceStrip::paint (juce::Graphics&) {}

void ReferenceStrip::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (20);
    matchButton.setBounds (top.removeFromRight (124));
    top.removeFromRight (10);
    status.setBounds (top.withTrimmedLeft (150));
    r.removeFromTop (8);
    const int gap = 12;
    const int w = (r.getWidth() - gap * (ReferencePlayer::numSlots - 1)) / ReferencePlayer::numSlots;
    for (auto& card : cards)
    {
        card->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (gap);
    }
}
} // namespace ddr
