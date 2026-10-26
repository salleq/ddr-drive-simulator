#include "Parameters.h"

namespace ddr
{
// Rough values for typical stock systems. They are educated guesses, not measurements:
// door woofers in a leaky door, a crossover dip around 3 kHz, cabin gain below ~70 Hz
// and a few strong cabin modes between 60 and 200 Hz.
const std::array<CarProfile, 6>& carProfiles()
{
    static const std::array<CarProfile, 6> profiles {{
        { "Family Wagon",
          70.0f, 0.9f, {{ { 180.0f, 1.2f, 2.5f }, { 1900.0f, 2.0f, 3.5f }, { 3600.0f, 3.0f, -5.0f }, { 7000.0f, 1.5f, 2.0f } }},
          16000.0f, 0.40f, false,
          55.0f, 9.0f, {{ { 68.0f, 6.0f, 5.0f }, { 112.0f, 7.0f, -6.0f }, { 155.0f, 6.0f, 4.0f } }},
          0.60f, 1.45f, -1.0f },

        { "Family Hatchback",
          75.0f, 1.0f, {{ { 220.0f, 1.5f, 3.0f }, { 1600.0f, 1.8f, 4.0f }, { 3200.0f, 3.0f, -6.0f }, { 6500.0f, 1.5f, 1.5f } }},
          15000.0f, 0.50f, false,
          62.0f, 8.0f, {{ { 74.0f, 6.0f, 5.0f }, { 120.0f, 7.0f, -6.0f }, { 165.0f, 6.0f, 4.0f } }},
          0.65f, 1.42f, 0.0f },

        { "Old Family Car",
          85.0f, 1.2f, {{ { 250.0f, 1.4f, 3.5f }, { 1400.0f, 1.6f, 5.0f }, { 3000.0f, 2.5f, -7.0f }, { 5500.0f, 1.2f, -3.0f } }},
          12000.0f, 0.75f, true,
          62.0f, 8.0f, {{ { 74.0f, 6.0f, 5.0f }, { 118.0f, 7.0f, -7.0f }, { 160.0f, 6.0f, 5.0f } }},
          0.70f, 1.40f, 3.0f },

        { "City Car",
          100.0f, 1.3f, {{ { 300.0f, 1.4f, 4.0f }, { 1300.0f, 1.6f, 5.0f }, { 2800.0f, 2.5f, -6.0f }, { 6000.0f, 1.5f, 3.0f } }},
          13000.0f, 0.80f, false,
          72.0f, 10.0f, {{ { 85.0f, 6.0f, 6.0f }, { 135.0f, 7.0f, -7.0f }, { 190.0f, 6.0f, 5.0f } }},
          0.80f, 1.30f, 4.0f },

        { "Family SUV",
          65.0f, 0.9f, {{ { 160.0f, 1.2f, 3.0f }, { 2000.0f, 2.0f, 3.0f }, { 3800.0f, 3.0f, -5.0f }, { 7500.0f, 1.5f, 2.0f } }},
          16000.0f, 0.40f, false,
          48.0f, 8.0f, {{ { 58.0f, 6.0f, 4.0f }, { 98.0f, 7.0f, -5.0f }, { 140.0f, 6.0f, 4.0f } }},
          0.50f, 1.55f, 0.0f },

        { "Premium Sedan",
          32.0f, 0.7f, {{ { 90.0f, 1.0f, 2.0f }, { 2500.0f, 1.5f, 1.0f }, { 4500.0f, 2.0f, -1.5f }, { 10000.0f, 1.0f, 1.0f } }},
          19000.0f, 0.15f, false,
          50.0f, 5.0f, {{ { 60.0f, 5.0f, 2.0f }, { 105.0f, 6.0f, -2.0f }, { 150.0f, 5.0f, 1.5f } }},
          0.40f, 1.48f, -6.0f },
    }};
    return profiles;
}

juce::StringArray carNames()
{
    juce::StringArray names;
    for (auto& p : carProfiles())
        names.add (p.name);
    return names;
}

juce::StringArray roadNames()   { return { "Smooth Asphalt", "Rough Asphalt", "Studded Tyres", "Gravel", "Wet Road" }; }
juce::StringArray engineNames() { return { "Petrol", "Diesel", "Electric" }; }
juce::StringArray seatNames()   { return { "Driver", "Front Passenger", "Back Seat", "Sweet Spot" }; }
juce::StringArray sourceNames() { return { "USB / CD", "Bluetooth", "FM Radio" }; }
juce::StringArray svcNames()    { return { "Off", "Low", "High" }; }

const std::array<Situation, 6>& situations()
{
    static const std::array<Situation, 6> s {{
        { "Parked",          0.0f,   Road::smooth,  1, false, Seat::driver },
        { "City",            40.0f,  Road::smooth,  2, false, Seat::driver },
        { "Motorway",        100.0f, Road::rough,   2, false, Seat::driver },
        { "Winter Motorway", 100.0f, Road::studded, 4, false, Seat::driver },
        { "Windows Down",    80.0f,  Road::smooth,  0, true,  Seat::driver },
        { "Back Seat",       80.0f,  Road::rough,   2, false, Seat::backSeat },
    }};
    return s;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v)) + " %"; });
    auto decibels = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (v, 1) + " dB"; });

    auto addBool = [&] (const char* id, const char* name, bool def)
    { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto addChoice = [&] (const char* id, const char* name, const StringArray& items, int def)
    { layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };

    // Basic
    addBool (ParamID::carMode, "Car Mode", true);
    addChoice (ParamID::car, "Car", carNames(), 1);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::speed, 1 }, "Speed", NormalisableRange<float> (0.0f, 140.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " km/h"; })));
    addChoice (ParamID::road, "Road", roadNames(), (int) Road::rough);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, decibels));
    addBool (ParamID::mono, "Mono", false);
    addBool (ParamID::levelMatch, "Level Match", true);

    // Driving & noise
    addChoice (ParamID::engine, "Engine", engineNames(), 0);
    addBool (ParamID::window, "Window Open", false);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamID::fan, 1 }, "Fan", 0, 4, 2));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::noiseAmount, 1 }, "Noise Amount", NormalisableRange<float> (0.0f, 150.0f, 1.0f), 100.0f, percent));
    addBool (ParamID::noiseOnly, "Noise Only", false);

    // Cabin
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::boom, 1 }, "Bass Boom", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::resonance, 1 }, "Resonances", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::reflections, 1 }, "Reflections", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percent));
    addChoice (ParamID::seat, "Seat", seatNames(), (int) Seat::driver);
    addBool (ParamID::rhd, "Right-Hand Drive", false);

    // Speakers
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::lowCut, 1 }, "Speaker Low Cut", NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return (v >= 0.0f ? "+" : "") + String (v, 2) + " oct"; })));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::distortion, 1 }, "Cone Distortion", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percent));
    addBool (ParamID::worn, "Worn Speakers", false);
    addChoice (ParamID::source, "Source", sourceNames(), (int) Source::usb);

    // Head unit
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamID::volume, 1 }, "Volume", 0, 40, 20));
    addBool (ParamID::loudness, "Loudness", true);
    addChoice (ParamID::svc, "Speed Volume", svcNames(), 1);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::bass, 1 }, "Bass", NormalisableRange<float> (-10.0f, 10.0f, 1.0f), 0.0f, decibels));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::treble, 1 }, "Treble", NormalisableRange<float> (-10.0f, 10.0f, 1.0f), 0.0f, decibels));
    addBool (ParamID::limiter, "Factory Limiter", true);

    return layout;
}
} // namespace ddr
