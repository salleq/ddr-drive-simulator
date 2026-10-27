#include "CalibrationPanel.h"
#include "../PluginEditor.h"

namespace ddr
{
namespace
{
    constexpr const char* helpUrl = "https://github.com/salleq/ddr-drive-simulator/blob/main/docs/MY-CAR.md";

    void styleText (juce::Label& l, float size = 13.0f)
    {
        l.setFont (juce::FontOptions (size));
        l.setColour (juce::Label::textColourId, Colours::dim);
        l.setJustificationType (juce::Justification::topLeft);
        l.setMinimumHorizontalScale (1.0f);
    }
}

//==============================================================================
void CurveView::paint (juce::Graphics& g)
{
    constexpr float topDb = 18.0f, bottomDb = -24.0f;
    const auto& f = cal::grid();
    auto area = getLocalBounds().toFloat();

    g.setColour (Colours::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto plot = area.reduced (34.0f, 12.0f).withTrimmedBottom (14.0f).withTrimmedLeft (-8.0f);
    auto xOf = [&] (double hz) { return plot.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * plot.getWidth(); };
    auto yOf = [&] (float db) { return plot.getY() + (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb) * plot.getHeight(); };

    g.setFont (juce::FontOptions (10.0f));
    for (float db = bottomDb; db <= topDb; db += 6.0f)
    {
        g.setColour (Colours::panelEdge.withAlpha (std::abs (db) < 0.1f ? 1.0f : 0.6f));
        g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
        g.setColour (Colours::dim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (area.getX() + 2.0f, yOf (db) - 6.0f, 28.0f, 12.0f),
                    juce::Justification::centredRight);
    }
    const std::pair<double, const char*> ticks[] { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" },
                                                   { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } };
    for (auto& [hz, name] : ticks)
    {
        g.setColour (Colours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine ((int) xOf (hz), plot.getY(), plot.getBottom());
        g.setColour (Colours::dim);
        g.drawText (name, juce::Rectangle<float> (xOf (hz) - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
    }

    auto drawCurve = [&] (const std::vector<float>& v, const std::vector<float>* w, juce::Colour c, float thickness)
    {
        if (v.size() != f.size()) return;
        juce::Path p;
        bool drawing = false;
        for (size_t k = 0; k < f.size(); ++k)
        {
            const bool use = (w == nullptr || (w->size() == f.size() && (*w)[k] > 0.05f)) && f[k] >= 25.0 && f[k] <= 18000.0;
            if (! use) { drawing = false; continue; }
            const juce::Point<float> pt (xOf (f[k]), yOf (v[k]));
            if (drawing) p.lineTo (pt); else p.startNewSubPath (pt);
            drawing = true;
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    drawCurve (target, &weight, Colours::accent, 2.0f);
    drawCurve (model, nullptr, Colours::text, 1.5f);

    // legend
    auto legend = juce::Rectangle<float> (plot.getRight() - 230.0f, plot.getY() + 4.0f, 230.0f, 14.0f);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (Colours::accent);
    g.fillRect (legend.getX(), legend.getCentreY() - 1.0f, 14.0f, 2.0f);
    g.drawText ("MEASURED", legend.withTrimmedLeft (18.0f), juce::Justification::centredLeft);
    g.setColour (Colours::text);
    g.fillRect (legend.getX() + 100.0f, legend.getCentreY() - 1.0f, 14.0f, 2.0f);
    g.drawText ("DDR MY CAR", legend.withTrimmedLeft (118.0f), juce::Justification::centredLeft);

    if (target.empty() && model.empty())
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (caption.isNotEmpty() ? caption : "Your car's frequency response appears here.", plot, juce::Justification::centred);
    }
}

//==============================================================================
CalibrationPanel::CalibrationPanel (DDRAudioProcessor& p) : processor (p)
{
    for (auto* b : { &closeButton, &helpButton, &saveButton, &carButton, &studioButton, &useButton })
        addAndMakeVisible (*b);

    closeButton.onClick  = [this] { if (onClose != nullptr) onClose(); };
    helpButton.onClick   = [] { juce::URL (helpUrl).launchInDefaultBrowser(); };
    saveButton.onClick   = [this] { saveTestSignal(); };
    carButton.onClick    = [this] { chooseRecording (Slot::car); };
    studioButton.onClick = [this] { chooseRecording (Slot::studio); };
    useButton.onClick    = [this] { useAsMyCar(); };
    useButton.setClickingTogglesState (false);

    helpButton.setTooltip ("Step-by-step instructions on the web.");
    saveButton.setTooltip ("Saves a 43-second WAV file: 3 s of noise to set the volume, then three sweeps from 20 Hz to 20 kHz.");
    carButton.setTooltip ("The phone recording made in the car. WAV, AIFF, FLAC, MP3 (and M4A on a Mac).");
    studioButton.setTooltip ("Optional: the same test made at your mixing position with the same phone.");
    baseBox.setTooltip ("Road noise, engine and cabin size come from this car type. The sound of the speakers comes from your measurement.");

    for (auto* l : { &saveText, &carText, &studioText, &resultText })
    {
        styleText (*l);
        addAndMakeVisible (*l);
    }
    saveText.setText ("Play it in the car from a USB stick, or from a second device over Bluetooth or AUX.", juce::dontSendNotification);

    baseLabel.setText ("NOISE AND SIZE LIKE", juce::dontSendNotification);
    baseLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    baseLabel.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (baseLabel);

    for (int i = 0; i < (int) carProfiles().size(); ++i)
        baseBox.addItem (carProfiles()[(size_t) i].name, i + 1);
    baseBox.setSelectedId ((int) processor.getMyCar().getProperty ("base", 1) + 1, juce::dontSendNotification);
    baseBox.onChange = [this] { if (carM.has_value() && carM->ok) refit(); };
    addAndMakeVisible (baseBox);

    addAndMakeVisible (curve);
    showStored();
    updateStatus();
}

CalibrationPanel::~CalibrationPanel()
{
    pool.removeAllJobs (true, 10000);
}

void CalibrationPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    auto area = getLocalBounds().reduced (16);
    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("MY CAR", area.getX(), area.getY(), 130, 32, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Measure your own car with a phone. No measurement microphone needed.",
                area.getX() + 128, area.getY() + 2, 460, 30, juce::Justification::centredLeft);
}

void CalibrationPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto header = area.removeFromTop (32);
    closeButton.setBounds (header.removeFromRight (90).reduced (0, 2));
    header.removeFromRight (8);
    helpButton.setBounds (header.removeFromRight (150).reduced (0, 2));
    area.removeFromTop (14);

    auto left = area.removeFromLeft (330);
    area.removeFromLeft (16);

    auto step = [&] (juce::TextButton& b, juce::Label& text, int textHeight)
    {
        b.setBounds (left.removeFromTop (30));
        left.removeFromTop (4);
        text.setBounds (left.removeFromTop (textHeight));
        left.removeFromTop (8);
    };
    step (saveButton, saveText, 34);
    step (carButton, carText, 64);
    step (studioButton, studioText, 64);
    baseLabel.setBounds (left.removeFromTop (18));
    baseBox.setBounds (left.removeFromTop (28));

    auto bottom = area.removeFromBottom (36);
    useButton.setBounds (bottom.removeFromRight (170));
    bottom.removeFromRight (12);
    resultText.setBounds (bottom);
    area.removeFromBottom (8);
    curve.setBounds (area);
}

void CalibrationPanel::saveTestSignal()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the DDR test signal",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("DDR test signal.wav"), "*.wav");
    juce::Component::SafePointer<CalibrationPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
    {
        auto* self = safe.getComponent();
        auto file = fc.getResult();
        if (self == nullptr || file == juce::File())
            return;
        if (! file.hasFileExtension ("wav"))
            file = file.withFileExtension ("wav");
        const bool ok = cal::writeTestSignal (file);
        self->saveText.setText (ok ? "Saved " + file.getFileName() + ". Play it in the car from a USB stick, or from a second device."
                                   : "Could not write " + file.getFullPathName(), juce::dontSendNotification);
        self->saveText.setColour (juce::Label::textColourId, ok ? Colours::good : Colours::bad);
    });
}

void CalibrationPanel::chooseRecording (Slot slot)
{
    chooser = std::make_unique<juce::FileChooser> (slot == Slot::car ? "Open the car recording" : "Open the studio recording",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg;*.caf");
    juce::Component::SafePointer<CalibrationPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, slot] (const juce::FileChooser& fc)
    {
        if (auto* self = safe.getComponent())
            if (fc.getResult().existsAsFile())
                self->analyseInBackground (slot, fc.getResult());
    });
}

void CalibrationPanel::analyseInBackground (Slot slot, const juce::File& file)
{
    (slot == Slot::car ? busyCar : busyStudio) = true;
    updateStatus();

    juce::Component::SafePointer<CalibrationPanel> safe (this);
    pool.addJob (std::function<void()> ([safe, slot, file]
    {
        auto m = cal::analyseFile (file);
        juce::MessageManager::callAsync ([safe, slot, m]
        {
            auto* self = safe.getComponent();
            if (self == nullptr)
                return;
            (slot == Slot::car ? self->busyCar : self->busyStudio) = false;
            (slot == Slot::car ? self->carM : self->studioM) = m;
            self->refit();
        });
    }));
}

void CalibrationPanel::refit()
{
    if (! carM.has_value() || ! carM->ok)
    {
        updateStatus();
        return;
    }

    const auto t = cal::makeTarget (*carM, (studioM.has_value() && studioM->ok) ? &*studioM : nullptr);
    const int base = juce::jmax (0, baseBox.getSelectedId() - 1);
    const int gen = ++generation;
    busyFit = true;
    updateStatus();

    juce::Component::SafePointer<CalibrationPanel> safe (this);
    pool.addJob (std::function<void()> ([safe, t, base, gen]
    {
        auto f = cal::fitProfile (t, base);
        juce::MessageManager::callAsync ([safe, t, f, gen]
        {
            auto* self = safe.getComponent();
            if (self == nullptr || gen != self->generation)
                return;
            self->busyFit = false;
            self->showFit (f, t);
        });
    }));
}

void CalibrationPanel::showFit (const cal::Fit& f, const cal::Target& t)
{
    fit = f;
    target = t;
    curve.target = t.db;
    curve.weight = t.weight;
    curve.model = f.modelDb;
    curve.repaint();
    updateStatus();
}

void CalibrationPanel::showForTesting (const cal::Measurement& car, const cal::Measurement* studio)
{
    carM = car;
    if (studio != nullptr) studioM = *studio;
    const auto t = cal::makeTarget (car, studio);
    showFit (cal::fitProfile (t, juce::jmax (0, baseBox.getSelectedId() - 1)), t);
}

void CalibrationPanel::showStored()
{
    const auto tree = processor.getMyCar();
    curve.target = cal::curveFromTree (tree, "target");
    curve.weight = cal::curveFromTree (tree, "weight");
    curve.model  = cal::curveFromTree (tree, "model");
    curve.repaint();
}

void CalibrationPanel::useAsMyCar()
{
    if (! fit.has_value() || ! target.has_value())
        return;

    processor.setMyCar (cal::toValueTree (*fit, *target));
    if (auto* param = processor.apvts.getParameter (ParamID::car))
        param->setValueNotifyingHost (param->convertTo0to1 ((float) myCarIndex));

    resultText.setText ("Done: My Car is selected. It is saved with this project, and new DDR instances start with it too.",
                        juce::dontSendNotification);
    resultText.setColour (juce::Label::textColourId, Colours::good);
}

void CalibrationPanel::updateStatus()
{
    auto show = [] (juce::Label& l, const juce::String& text, juce::Colour c)
    {
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, c);
    };
    auto measurementColour = [] (const cal::Measurement& m) { return ! m.ok ? Colours::bad : (m.warning ? Colours::warn : Colours::good); };

    if (busyCar) show (carText, "Analysing...", Colours::text);
    else if (carM.has_value()) show (carText, carM->message, measurementColour (*carM));
    else show (carText, "Phone at the driver's head, engine off, doors shut. Record the whole signal.", Colours::dim);

    if (busyStudio) show (studioText, "Analysing...", Colours::text);
    else if (studioM.has_value()) show (studioText, studioM->message, measurementColour (*studioM));
    else show (studioText, "Optional, recommended: the same recording at your mixing position. "
                           "It cancels out your phone's microphone and your monitors.", Colours::dim);

    const auto stored = processor.getMyCar();
    if (busyFit)
        show (resultText, "Fitting DDR's filters to the measurement...", Colours::text);
    else if (fit.has_value())
    {
        juce::String s = "DDR follows the measurement within " + juce::String (fit->errorDb, 1) + " dB RMS";
        s << (target.has_value() && target->relativeToStudio ? ", relative to your studio." : ". Load a studio recording too for a truer bass.");
        show (resultText, s, Colours::text);
    }
    else if (stored.isValid())
        show (resultText, "Current My Car: measured " + stored.getProperty ("date").toString()
                              + ((bool) stored.getProperty ("studio", false) ? ", relative to the studio." : "."),
              Colours::dim);
    else
        show (resultText, "Load a car recording to see the result here.", Colours::dim);

    useButton.setEnabled (fit.has_value() && ! busyFit);
    carButton.setEnabled (! busyCar);
    studioButton.setEnabled (! busyStudio);
}
} // namespace ddr
