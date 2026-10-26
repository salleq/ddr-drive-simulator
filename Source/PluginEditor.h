#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace ddr
{
namespace Colours
{
    inline const juce::Colour background { 0xff121418 };
    inline const juce::Colour panel      { 0xff1c1f26 };
    inline const juce::Colour panelEdge  { 0xff2a2e38 };
    inline const juce::Colour accent     { 0xffffa21a }; // dashboard amber
    inline const juce::Colour text       { 0xffe9eaee };
    inline const juce::Colour dim        { 0xff8b909b };
    inline const juce::Colour good       { 0xff4cc38a };
    inline const juce::Colour warn       { 0xffffc23d };
    inline const juce::Colour bad        { 0xffff5a4e };
    inline const juce::Colour noise      { 0xff6b7280 };
}

class DashLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DashLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

/** A knob with its caption above and its value below. */
struct Knob : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& caption, bool big = false);
    void resized() override;

    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A labelled drop-down bound to a choice parameter. */
struct Choice : public juce::Component
{
    Choice (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& caption);
    void resized() override;

    juce::ComboBox box;
    juce::Label    label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** An on/off pill bound to a bool parameter. */
struct Toggle : public juce::Component
{
    Toggle (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text);
    void resized() override { button.setBounds (getLocalBounds()); }

    juce::TextButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Shows, per band, whether the music is still heard over the noise. */
class MaskingMeter : public juce::Component
{
public:
    explicit MaskingMeter (DDRAudioProcessor& p) : processor (p) {}
    void paint (juce::Graphics&) override;

private:
    DDRAudioProcessor& processor;
};
} // namespace ddr

/** Everything you see, drawn at 100 % size. The editor scales it for the zoom setting. */
class DDRPanel : public juce::Component, private juce::Timer
{
public:
    explicit DDRPanel (DDRAudioProcessor&);
    ~DDRPanel() override;

    std::function<void()> onSizeChanged, onZoomChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Switches between the Basic and Advanced layouts. */
    void setAdvanced (bool shouldBeAdvanced);

private:
    void timerCallback() override;
    void updateCarModeButton();

    DDRAudioProcessor& processor;
    ddr::DashLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // header
    juce::TextButton basicButton { "BASIC" }, advancedButton { "ADVANCED" };
    juce::TextButton carModeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> carModeAttachment;
    juce::ComboBox situationBox, zoomBox;

    // basic strip
    ddr::Choice car, road;
    ddr::Knob   speed, output;
    ddr::Toggle mono, levelMatch;
    juce::Label carDescription, roadDescription;
    ddr::MaskingMeter meter;

    // advanced: driving
    ddr::Choice engine;
    ddr::Knob   fan, noiseAmount;
    ddr::Toggle window, noiseOnly;
    // advanced: cabin
    ddr::Knob   boom, resonance, reflections;
    ddr::Choice seat;
    ddr::Toggle rhd;
    // advanced: speakers
    ddr::Knob   lowCut, distortion, fader;
    ddr::Choice source;
    ddr::Toggle worn;
    // advanced: head unit
    ddr::Knob   volume, bass, treble;
    ddr::Choice svc;
    ddr::Toggle loudness, limiter;

    bool advanced = false;
    juce::Rectangle<int> basicPanel, meterPanel;
    std::array<juce::Rectangle<int>, 4> groupPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DDRPanel)
};

class DDRAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DDRAudioProcessorEditor (DDRAudioProcessor&);
    void resized() override {}
    void setAdvanced (bool shouldBeAdvanced) { panel.setAdvanced (shouldBeAdvanced); }

    static constexpr float zoomLevels[] { 0.75f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f };

private:
    void applyZoom();

    DDRAudioProcessor& processor;
    DDRPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DDRAudioProcessorEditor)
};
