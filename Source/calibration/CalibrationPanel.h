#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Calibration.h"
#include <optional>

class DDRAudioProcessor;

namespace ddr
{
/** Draws a measured curve and DDR's fitted version of it. */
class CurveView : public juce::Component
{
public:
    void paint (juce::Graphics&) override;

    std::vector<float> target, weight, model;
    juce::String caption;
};

/** The "My Car" screen: save the test signal, load the phone recordings, use the result. */
class CalibrationPanel : public juce::Component
{
public:
    explicit CalibrationPanel (DDRAudioProcessor&);
    ~CalibrationPanel() override;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** For the screenshot tool: show these measurements as if they were loaded (fits synchronously). */
    void showForTesting (const cal::Measurement& car, const cal::Measurement* studio);

private:
    enum class Slot { car, studio };

    void saveTestSignal();
    void chooseRecording (Slot);
    void analyseInBackground (Slot, const juce::File&);
    void refit();
    void showFit (const cal::Fit&, const cal::Target&);
    void showStored();
    void useAsMyCar();
    void updateStatus();

    DDRAudioProcessor& processor;

    juce::TextButton closeButton { "CLOSE" }, helpButton { "HOW TO MEASURE" };
    juce::TextButton saveButton { "1  SAVE TEST SIGNAL" }, carButton { "2  LOAD CAR RECORDING" },
                     studioButton { "3  LOAD STUDIO RECORDING" }, useButton { "USE AS MY CAR" };
    juce::Label saveText, carText, studioText, baseLabel, resultText;
    juce::ComboBox baseBox;
    CurveView curve;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool pool { 1 };
    int generation = 0;
    bool busyCar = false, busyStudio = false, busyFit = false;

    std::optional<cal::Measurement> carM, studioM;
    std::optional<cal::Fit> fit;
    std::optional<cal::Target> target;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalibrationPanel)
};
} // namespace ddr
