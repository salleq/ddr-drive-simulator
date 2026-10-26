#include "PluginEditor.h"
#include "BinaryData.h"

using namespace ddr;

namespace
{
    constexpr int editorWidth    = 880;
    constexpr int basicHeight    = 430;
    constexpr int advancedHeight = 712;
    constexpr int margin         = 16;

    const char* carDescriptions[] {
        "Newer family estate. Door woofers, tweeters in the A-pillars and a long, boomy rear.",
        "Mid-size hatchback with the basic 4-6 speaker system most people have.",
        "Fifteen years old. Tired speaker cones, more engine and wind noise.",
        "Small and loud. Little real bass, a strong boom and lots of road noise.",
        "Big, tall cabin. Lower boom and more wind noise at speed.",
        "The reference: subwoofer, DSP and a quiet cabin. Your mix should shine here."
    };

    const char* roadDescriptions[] {
        "New, smooth asphalt. A low, even hum from the tyres.",
        "Worn, coarse asphalt. The classic motorway roar.",
        "Winter tyres with studs. A hissing, rattling top end on top of the roar.",
        "Gravel road. Heavy rumble and stones hitting the wheel arches.",
        "Wet road. Spray hiss from the tyres all the way up the spectrum."
    };

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, bool retro)
    {
        // On the Retro skin the panels are see-through glass with a neon edge
        g.setColour (retro ? Colours::panel.withAlpha (0.66f) : Colours::panel);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (retro ? juce::Colour (0xffff4fd8).withAlpha (0.5f) : Colours::panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title, r.reduced (14, 8).removeFromTop (16), juce::Justification::centredLeft);
    }
}

//==============================================================================
DashLookAndFeel::DashLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, Colours::background);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::panelEdge);
    setColour (juce::ComboBox::arrowColourId, Colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::background);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, Colours::dim);
    setColour (juce::TextButton::textColourOnId, Colours::background);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panel);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::TooltipWindow::outlineColourId, Colours::accent);
}

void DashLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const bool speedo = slider.getName() == "speed";
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (speedo ? 10.0f : 4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float track = speedo ? 9.0f : 4.0f;
    const float arcRadius = radius - track * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::panelEdge);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (Colours::accent);
    g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (speedo)
    {
        // speedometer ticks and numbers every 20 km/h
        const double maxV = slider.getMaximum();
        for (int kmh = 0; kmh <= (int) maxV; kmh += 10)
        {
            const float a = startAngle + (float) (kmh / maxV) * (endAngle - startAngle);
            const bool major = kmh % 20 == 0;
            const float r1 = arcRadius - track - 4.0f, r2 = r1 - (major ? 12.0f : 6.0f);
            const auto p1 = centre.getPointOnCircumference (r1, a), p2 = centre.getPointOnCircumference (r2, a);
            g.setColour (major ? Colours::text : Colours::dim);
            g.drawLine ({ p1, p2 }, major ? 2.0f : 1.0f);
            if (major)
            {
                const auto tp = centre.getPointOnCircumference (r2 - 14.0f, a);
                g.setFont (juce::FontOptions (12.0f));
                g.drawText (juce::String (kmh), juce::Rectangle<float> (30.0f, 16.0f).withCentre (tp), juce::Justification::centred);
            }
        }

        // needle
        const auto tip = centre.getPointOnCircumference (arcRadius - track - 8.0f, angle);
        g.setColour (Colours::accent);
        g.drawLine ({ centre, tip }, 3.0f);
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (centre));

        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
        const auto readout = juce::Rectangle<float> (100.0f, 36.0f).withCentre (centre.translated (0.0f, radius * 0.74f));
        g.drawText (juce::String (juce::roundToInt (slider.getValue())), readout, juce::Justification::centred);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("km/h", readout.translated (0.0f, 26.0f), juce::Justification::centred);
        return;
    }

    // small knob pointer
    const auto p1 = centre.getPointOnCircumference (arcRadius * 0.25f, angle);
    const auto p2 = centre.getPointOnCircumference (arcRadius - track - 2.0f, angle);
    g.setColour (Colours::text);
    g.drawLine ({ p1, p2 }, 2.0f);
}

void DashLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? Colours::accent : Colours::background);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? Colours::accent : (over ? Colours::dim : Colours::panelEdge));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
}

void DashLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? Colours::background : Colours::text);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

juce::Font DashLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions (juce::jmin (13.0f, buttonHeight * 0.48f), juce::Font::bold);
}

void DashLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    g.setColour (Colours::background);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 5.0f, ay - 3.0f, ax + 5.0f, ay - 3.0f, ax, ay + 3.5f);
    g.setColour (Colours::accent);
    g.fillPath (arrow);
}

juce::Font DashLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::FontOptions (juce::jmin (17.0f, box.getHeight() * 0.45f));
}

void DashLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Label* DashLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setFont (juce::FontOptions (12.0f));
    return l;
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& caption, bool big)
{
    slider.setName (paramId);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (big ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (big ? 13.0f : 12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

Choice::Choice (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& caption)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);

    label.setText (caption, juce::dontSendNotification);
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    box.setBounds (r.removeFromTop (juce::jmin (r.getHeight(), 40)));
}

Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text)
{
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, button);
}

//==============================================================================
void MaskingMeter::paint (juce::Graphics& g)
{
    static const char* names[] { "LOW  < 250 Hz", "MID  250 Hz - 4 kHz", "HIGH  > 4 kHz" };
    constexpr float floorDb = -60.0f;

    auto r = getLocalBounds();
    const int rowH = r.getHeight() / 3;

    for (int b = 0; b < 3; ++b)
    {
        auto row = r.removeFromTop (rowH).reduced (0, 3);
        const float m = processor.meters[(size_t) b].music.load();
        const float n = processor.meters[(size_t) b].noise.load();

        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (names[b], row.removeFromLeft (150), juce::Justification::centredLeft);

        auto status = row.removeFromRight (170);
        auto bars = row.reduced (8, 0);

        auto widthFor = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb) * (float) bars.getWidth(); };
        auto musicBar = bars.removeFromTop (bars.getHeight() / 2).toFloat().reduced (0.0f, 1.5f);
        auto noiseBar = bars.toFloat().reduced (0.0f, 1.5f);

        g.setColour (Colours::background);
        g.fillRoundedRectangle (musicBar, 3.0f);
        g.fillRoundedRectangle (noiseBar, 3.0f);
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (musicBar.withWidth (widthFor (m)), 3.0f);
        g.setColour (Colours::noise);
        g.fillRoundedRectangle (noiseBar.withWidth (widthFor (n)), 3.0f);

        juce::String word = "-";
        auto colour = Colours::dim;
        if (m > -80.0f)
        {
            const float diff = m - n;
            // The meter compares the whole mix with the noise. Single parts of the mix are
            // quieter than the whole, so they need a good margin to stay audible.
            if (diff >= 12.0f)     { word = "CLEAR";    colour = Colours::good; }
            else if (diff >= 3.0f) { word = "FIGHTING"; colour = Colours::warn; }
            else                   { word = "MASKED";   colour = Colours::bad; }
        }
        g.setColour (colour);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (word, status, juce::Justification::centredRight);

        if (m > -80.0f)
        {
            // the margin in dB, next to the word
            g.setColour (Colours::dim);
            g.setFont (juce::FontOptions (11.0f));
            const auto diffDb = juce::roundToInt (m - n);
            g.drawText ((diffDb > 0 ? "+" : "") + juce::String (diffDb) + " dB", status.withTrimmedRight (90),
                        juce::Justification::centredRight);
        }
    }
}

//==============================================================================
DDRPanel::DDRPanel (DDRAudioProcessor& p)
    : processor (p),
      car (p.apvts, ParamID::car, "CAR TYPE"),
      road (p.apvts, ParamID::road, "ROAD"),
      speed (p.apvts, ParamID::speed, "SPEED", true),
      output (p.apvts, ParamID::output, "OUTPUT"),
      mono (p.apvts, ParamID::mono, "MONO"),
      levelMatch (p.apvts, ParamID::levelMatch, "LEVEL MATCH"),
      meter (p),
      engine (p.apvts, ParamID::engine, "ENGINE"),
      fan (p.apvts, ParamID::fan, "FAN"),
      noiseAmount (p.apvts, ParamID::noiseAmount, "NOISE"),
      window (p.apvts, ParamID::window, "WINDOW"),
      noiseOnly (p.apvts, ParamID::noiseOnly, "NOISE ONLY"),
      boom (p.apvts, ParamID::boom, "BOOM"),
      resonance (p.apvts, ParamID::resonance, "RESONANCE"),
      reflections (p.apvts, ParamID::reflections, "REFLECT."),
      seat (p.apvts, ParamID::seat, "SEAT"),
      rhd (p.apvts, ParamID::rhd, "RIGHT-HAND DRIVE"),
      lowCut (p.apvts, ParamID::lowCut, "LOW CUT"),
      distortion (p.apvts, ParamID::distortion, "DISTORTION"),
      fader (p.apvts, ParamID::fader, "FADER"),
      source (p.apvts, ParamID::source, "SOURCE"),
      worn (p.apvts, ParamID::worn, "WORN SPEAKERS"),
      volume (p.apvts, ParamID::volume, "VOLUME"),
      bass (p.apvts, ParamID::bass, "BASS"),
      treble (p.apvts, ParamID::treble, "TREBLE"),
      svc (p.apvts, ParamID::svc, "SPEED VOLUME"),
      loudness (p.apvts, ParamID::loudness, "LOUDNESS"),
      limiter (p.apvts, ParamID::limiter, "LIMITER")
{
    setLookAndFeel (&lnf);

    for (auto* b : { &basicButton, &advancedButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    basicButton.onClick    = [this] { setAdvanced (false); };
    advancedButton.onClick = [this] { setAdvanced (true); };

    carModeButton.setClickingTogglesState (true);
    carModeButton.onStateChange = [this] { updateCarModeButton(); };
    carModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamID::carMode, carModeButton);
    addAndMakeVisible (carModeButton);
    updateCarModeButton();

    situationBox.setTextWhenNothingSelected ("Situation...");
    int id = 1;
    for (auto& s : situations())
        situationBox.addItem (s.name, id++);
    situationBox.setSelectedId (p.getCurrentProgram() + 1, juce::dontSendNotification);
    situationBox.onChange = [this]
    {
        if (situationBox.getSelectedId() > 0)
            processor.setCurrentProgram (situationBox.getSelectedId() - 1);
    };
    addAndMakeVisible (situationBox);

    int zoomId = 1;
    for (auto z : DDRAudioProcessorEditor::zoomLevels)
        zoomBox.addItem (juce::String (juce::roundToInt (z * 100.0f)) + " %", zoomId++);
    const float currentZoom = (float) (double) p.apvts.state.getProperty ("zoom", 1.0);
    for (int i = 0; i < (int) std::size (DDRAudioProcessorEditor::zoomLevels); ++i)
        if (std::abs (DDRAudioProcessorEditor::zoomLevels[i] - currentZoom) < 0.01f)
            zoomBox.setSelectedId (i + 1, juce::dontSendNotification);
    zoomBox.setTooltip ("Window size");
    zoomBox.onChange = [this]
    {
        const int idx = zoomBox.getSelectedId() - 1;
        if (idx >= 0)
        {
            processor.apvts.state.setProperty ("zoom", (double) DDRAudioProcessorEditor::zoomLevels[idx], nullptr);
            if (onZoomChanged != nullptr)
                onZoomChanged();
        }
    };
    addAndMakeVisible (zoomBox);

    background = juce::Drawable::createFromImageData (BinaryData::background_svg, BinaryData::background_svgSize);
    skinBox.addItem ("Retro skin", 1);
    skinBox.addItem ("Plain skin", 2);
    skinBox.setSelectedId ((int) p.apvts.state.getProperty ("skin", 0) + 1, juce::dontSendNotification);
    skinBox.setTooltip ("Retro: night drive artwork behind the controls. Plain: no picture.");
    skinBox.onChange = [this]
    {
        processor.apvts.state.setProperty ("skin", skinBox.getSelectedId() - 1, nullptr);
        repaint();
    };
    addAndMakeVisible (skinBox);

    carDescription.setFont (juce::FontOptions (13.0f));
    carDescription.setColour (juce::Label::textColourId, Colours::dim);
    carDescription.setJustificationType (juce::Justification::topLeft);
    carDescription.setMinimumHorizontalScale (1.0f);
    roadDescription.setFont (juce::FontOptions (13.0f));
    roadDescription.setColour (juce::Label::textColourId, Colours::dim);
    roadDescription.setJustificationType (juce::Justification::topLeft);
    roadDescription.setMinimumHorizontalScale (1.0f);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &car, &road, &speed, &output, &mono, &levelMatch, &carDescription, &roadDescription, &meter,
             &engine, &fan, &noiseAmount, &window, &noiseOnly,
             &boom, &resonance, &reflections, &seat, &rhd,
             &lowCut, &distortion, &fader, &source, &worn,
             &volume, &bass, &treble, &svc, &loudness, &limiter })
        addAndMakeVisible (c);

    // Tooltips: hover any control to see what it does
    car.box.setTooltip ("The kind of car and its stock audio system.");
    speed.slider.setTooltip ("Driving speed. Road, tyre, wind and engine noise grow with it.");
    road.box.setTooltip ("Road surface. Changes the colour and level of the tyre noise.");
    output.slider.setTooltip ("Final output level.");
    mono.button.setTooltip ("Sum to mono, like many Bluetooth speakers and back-seat listeners hear it.");
    levelMatch.button.setTooltip ("Keeps CAR mode as loud as STUDIO, so the comparison is fair.");
    carModeButton.setTooltip ("CAR: hear the simulation. STUDIO: hear your mix untouched. Bypass DDR before you bounce!");
    situationBox.setTooltip ("Quick driving situations: sets speed, road, fan, window and seat.");
    engine.box.setTooltip ("Engine sound. Petrol hums, diesel clatters, electric whines quietly.");
    fan.slider.setTooltip ("Ventilation fan speed. 0 is off.");
    noiseAmount.slider.setTooltip ("Scales all the driving noise. 100 % is a typical car.");
    window.button.setTooltip ("Driver's window open: much more wind and buffeting.");
    noiseOnly.button.setTooltip ("Listen to the noise alone.");
    boom.slider.setTooltip ("Cabin bass build-up below about 80 Hz, relative to the chosen car.");
    resonance.slider.setTooltip ("Strength of the cabin's narrow bass resonances and dips (60-200 Hz).");
    reflections.slider.setTooltip ("Early reflections from the windows and dashboard.");
    seat.box.setTooltip ("Where you sit. Off-centre seats hear the near speaker first and louder.");
    rhd.button.setTooltip ("Mirror the car for right-hand drive countries.");
    lowCut.slider.setTooltip ("Moves the speakers' bass roll-off down (bigger) or up (smaller) in octaves.");
    distortion.slider.setTooltip ("How much the door speakers distort when the bass gets loud.");
    source.box.setTooltip ("How the music reaches the car: USB/CD, Bluetooth or FM radio.");
    fader.slider.setTooltip ("Front/rear balance, like the car's own fader. The City Car has no rear speakers.");
    worn.button.setTooltip ("Old, tired speaker cones: less bass, duller top, more distortion.");
    volume.slider.setTooltip ("Car stereo volume. Louder lifts the music over the noise but pushes the limiter and speakers.");
    bass.slider.setTooltip ("The car stereo's bass tone control.");
    treble.slider.setTooltip ("The car stereo's treble tone control.");
    svc.box.setTooltip ("Speed-dependent volume: the car turns the music up as you drive faster.");
    loudness.button.setTooltip ("Boosts bass and treble at low volume, as many car stereos do.");
    limiter.button.setTooltip ("The head unit's built-in limiter that stops the speakers from overloading.");

    advanced = (bool) p.apvts.state.getProperty ("advanced", false);
    setAdvanced (advanced);
    timerCallback();
    startTimerHz (30);
}

DDRPanel::~DDRPanel()
{
    setLookAndFeel (nullptr);
}

void DDRPanel::updateCarModeButton()
{
    carModeButton.setButtonText (carModeButton.getToggleState() ? "CAR" : "STUDIO");
    repaint (0, 0, getWidth(), 64);
}

void DDRPanel::setAdvanced (bool shouldBeAdvanced)
{
    advanced = shouldBeAdvanced;
    processor.apvts.state.setProperty ("advanced", advanced, nullptr);
    basicButton.setToggleState (! advanced, juce::dontSendNotification);
    advancedButton.setToggleState (advanced, juce::dontSendNotification);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &engine, &fan, &noiseAmount, &window, &noiseOnly,
             &boom, &resonance, &reflections, &seat, &rhd,
             &lowCut, &distortion, &fader, &source, &worn,
             &volume, &bass, &treble, &svc, &loudness, &limiter })
        c->setVisible (advanced);

    setSize (editorWidth, advanced ? advancedHeight : basicHeight);
    resized();
    repaint();
    if (onSizeChanged != nullptr)
        onSizeChanged();
}

void DDRPanel::timerCallback()
{
    const int carIndex = juce::jlimit (0, 5, car.box.getSelectedItemIndex());
    carDescription.setText (carDescriptions[carIndex], juce::dontSendNotification);
    const int roadIndex = juce::jlimit (0, 4, road.box.getSelectedItemIndex());
    roadDescription.setText (roadDescriptions[roadIndex], juce::dontSendNotification);

    // Show the situation only while the controls still match it
    int matching = 0;
    auto value = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load(); };
    for (size_t i = 0; i < situations().size(); ++i)
    {
        const auto& sit = situations()[i];
        if (std::abs (value (ParamID::speed) - sit.speed) < 0.5f
            && (int) value (ParamID::road) == (int) sit.road
            && (int) value (ParamID::fan) == sit.fan
            && (value (ParamID::window) > 0.5f) == sit.window
            && (int) value (ParamID::seat) == (int) sit.seat)
        {
            matching = (int) i + 1;
            break;
        }
    }
    if (situationBox.getSelectedId() != matching)
        situationBox.setSelectedId (matching, juce::dontSendNotification);
    meter.repaint();
}

void DDRPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    const bool retro = isRetroSkin();
    if (retro)
    {
        const int w = getWidth(), h = getHeight();
        if (backgroundImage.getWidth() != w * 2 || backgroundImage.getHeight() != h * 2)
        {
            // Render the vector artwork once per size, at 2x so it stays sharp when zoomed
            backgroundImage = juce::Image (juce::Image::RGB, w * 2, h * 2, true);
            juce::Graphics ig (backgroundImage);
            ig.addTransform (juce::AffineTransform::scale (2.0f));
            if (background != nullptr)
            {
                // the artwork is drawn in an 880 x 712 canvas; parts of the road run past its edges
                const juce::Rectangle<float> artwork (0.0f, 0.0f, 880.0f, 712.0f);
                const juce::RectanglePlacement cover (juce::RectanglePlacement::xMid | juce::RectanglePlacement::yMid
                                                      | juce::RectanglePlacement::fillDestination);
                ig.reduceClipRegion (getLocalBounds());
                background->draw (ig, 1.0f, cover.getTransformToFit (artwork, getLocalBounds().toFloat()));
            }
        }
        g.drawImage (backgroundImage, getLocalBounds().toFloat());

        // darken behind the header so the title stays readable over the sunset
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.85f), 0.0f, 0.0f,
                                                 juce::Colours::black.withAlpha (0.0f), 0.0f, 110.0f, false));
        g.fillRect (0, 0, w, 110);
    }

    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (32.0f, juce::Font::bold));
    g.drawText ("DDR", margin, 12, 90, 40, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("(DREADED) DRIVE SIMULATOR", margin + 84, 20, 220, 16, juce::Justification::centredLeft);
   #ifdef JucePlugin_VersionString
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("v" JucePlugin_VersionString, margin + 268, 20, 60, 16, juce::Justification::centredLeft);
   #endif

    if (carModeButton.getToggleState())
    {
        g.setColour (Colours::accent.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Car mode is on. Bypass DDR before you bounce!", margin + 84, 36, 300, 14, juce::Justification::centredLeft);
    }

    drawPanel (g, basicPanel, "THE DRIVE", retro);
    drawPanel (g, meterPanel, "CAN YOU STILL HEAR IT?  music vs. noise", retro);

    if (advanced)
    {
        static const char* titles[] { "DRIVING & NOISE", "CABIN & SEAT", "SPEAKERS", "HEAD UNIT" };
        for (size_t i = 0; i < groupPanels.size(); ++i)
            drawPanel (g, groupPanels[i], titles[i], retro);
    }
}

void DDRPanel::resized()
{
    auto area = getLocalBounds().reduced (margin, 0);

    // header
    auto header = area.removeFromTop (64).withTrimmedTop (14).withTrimmedBottom (10);
    carModeButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (12);
    situationBox.setBounds (header.removeFromRight (170).reduced (0, 2));
    header.removeFromRight (12);
    advancedButton.setBounds (header.removeFromRight (104));
    header.removeFromRight (6);
    basicButton.setBounds (header.removeFromRight (80));

    // the drive: car, speed, road, output
    basicPanel = area.removeFromTop (232);
    auto inner = basicPanel.reduced (14, 10).withTrimmedTop (22);

    auto carCol = inner.removeFromLeft (250);
    car.setBounds (carCol.removeFromTop (60));
    carCol.removeFromTop (10);
    carDescription.setBounds (carCol.removeFromTop (70));

    inner.removeFromLeft (16);
    auto outCol = inner.removeFromRight (130);
    output.setBounds (outCol.removeFromTop (118));
    outCol.removeFromTop (8);
    mono.setBounds (outCol.removeFromTop (28));
    outCol.removeFromTop (8);
    levelMatch.setBounds (outCol.removeFromTop (28));

    inner.removeFromRight (16);
    auto roadCol = inner.removeFromRight (200);
    road.setBounds (roadCol.removeFromTop (60));
    roadCol.removeFromTop (10);
    roadDescription.setBounds (roadCol.removeFromTop (70));

    speed.setBounds (inner.withSizeKeepingCentre (juce::jmin (inner.getWidth(), 230), inner.getHeight() + 10).translated (0, -6));

    area.removeFromTop (10);

    // advanced groups
    if (advanced)
    {
        auto groups = area.removeFromTop (270);
        const int gw = (groups.getWidth() - 3 * 10) / 4;
        for (size_t i = 0; i < groupPanels.size(); ++i)
        {
            groupPanels[i] = groups.removeFromLeft (gw);
            groups.removeFromLeft (10);
        }

        auto layoutGroup = [] (juce::Rectangle<int> panel, std::initializer_list<Knob*> knobs,
                               Choice* choice, std::initializer_list<Toggle*> toggles)
        {
            auto r = panel.reduced (12, 10).withTrimmedTop (24);
            auto knobRow = r.removeFromTop (96);
            const int kw = knobRow.getWidth() / 3;
            if (knobs.size() == 2)
                knobRow = knobRow.withSizeKeepingCentre (kw * 2, knobRow.getHeight());
            for (auto* k : knobs)
                k->setBounds (knobRow.removeFromLeft (kw));
            r.removeFromTop (8);
            choice->setBounds (r.removeFromTop (58));
            r.removeFromTop (12);
            auto toggleRow = r.removeFromTop (28);
            if (toggles.size() == 1)
                (*toggles.begin())->setBounds (toggleRow);
            else
            {
                const int tw = (toggleRow.getWidth() - 8) / 2;
                auto it = toggles.begin();
                (*it)->setBounds (toggleRow.removeFromLeft (tw));
                toggleRow.removeFromLeft (8);
                (*++it)->setBounds (toggleRow);
            }
        };

        layoutGroup (groupPanels[0], { &fan, &noiseAmount }, &engine, { &window, &noiseOnly });
        layoutGroup (groupPanels[1], { &boom, &resonance, &reflections }, &seat, { &rhd });
        layoutGroup (groupPanels[2], { &lowCut, &distortion, &fader }, &source, { &worn });
        layoutGroup (groupPanels[3], { &volume, &bass, &treble }, &svc, { &loudness, &limiter });

        area.removeFromTop (10);
    }

    meterPanel = area.removeFromTop (110);
    meter.setBounds (meterPanel.reduced (14, 8).withTrimmedTop (22));
    zoomBox.setBounds (meterPanel.getRight() - 14 - 84, meterPanel.getY() + 6, 84, 22);
    skinBox.setBounds (zoomBox.getX() - 8 - 110, zoomBox.getY(), 110, 22);
}

//==============================================================================
DDRAudioProcessorEditor::DDRAudioProcessorEditor (DDRAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.onSizeChanged = [this] { applyZoom(); };
    panel.onZoomChanged = [this] { applyZoom(); };
    applyZoom();
}

void DDRAudioProcessorEditor::applyZoom()
{
    const float zoom = juce::jlimit (0.5f, 2.0f, (float) (double) processor.apvts.state.getProperty ("zoom", 1.0));
    panel.setTopLeftPosition (0, 0);
    panel.setTransform (juce::AffineTransform::scale (zoom));
    setSize (juce::roundToInt ((float) panel.getWidth() * zoom), juce::roundToInt ((float) panel.getHeight() * zoom));
}
