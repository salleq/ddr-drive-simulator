# DDR – (Dreaded) Drive Simulator

**A free VST3 plugin that lets you hear your mix the way it sounds in an ordinary family car** – with the stock speakers, the cabin boom and the road noise. Put it last on your master bus, pick a car, set the speed and check whether the vocal, the bass and the snare still make it through.

![DDR basic view](docs/ddr-basic.png)

## Why songs sound worse in the car

| What happens | What it does to the mix |
| --- | --- |
| **Road, tyre, wind and engine noise** – roughly 65–72 dB(A) at 100 km/h, mostly below 500 Hz | Masks the bass line, the low end of the kick and the quiet details |
| **Cabin boom** – a small closed space lifts everything below ~80 Hz and adds strong resonances at 60–200 Hz | Bass gets thick and single notes jump out |
| **Cheap door speakers** – no real bass below 70–100 Hz, peaks and dips around the crossover, distortion when loud | Vocal and snare clarity depends on a bumpy 1–4 kHz range |
| **You sit to one side** – the near speaker arrives ~1–2 ms earlier and louder | The centre image moves, comb filtering colours the mids |
| **Head unit tricks** – loudness, speed-dependent volume, limiter | Dynamics get squashed or pumped |

DDR simulates all of these.

## Controls

**Basic** – everything most people need:

- **Car type** – Family Wagon, Family Hatchback, Old Family Car, City Car, Family SUV, Premium Sedan (reference)
- **Speed** – 0–140 km/h. Raises road, wind and engine noise like a real drive
- **Road** – Smooth Asphalt, Rough Asphalt, Studded Tyres, Gravel, Wet Road
- **CAR / STUDIO** – level-matched A/B switch
- **Situation** – Parked, City, Motorway, Winter Motorway, Windows Down, Back Seat
- **Output**, **Mono**, **Level Match**
- **Can you still hear it?** – shows per band (low / mid / high) if your music is CLEAR, FIGHTING or MASKED by the noise

**Advanced** – everything else:

![DDR advanced view](docs/ddr-advanced.png)

| Group | Controls |
| --- | --- |
| Driving & noise | Fan (0–4), Noise amount, Engine (Petrol / Diesel / Electric), Window open, Noise only |
| Cabin & seat | Boom, Resonance, Reflections, Seat (Driver / Front Passenger / Back Seat / Sweet Spot), Right-hand drive |
| Speakers | Low cut shift, Cone distortion, Worn speakers, Source (USB / Bluetooth / FM radio) |
| Head unit | Volume (0–40), Bass, Treble, Speed volume (Off / Low / High), Loudness, Limiter |

The car presets are educated guesses at typical stock systems, not measurements of any specific car.

## Download and install

Get the latest zip for Windows or macOS from the [Releases page](https://github.com/salleq/ddr-drive-simulator/releases). Install instructions are in `INSTALL.txt` inside the zip ([read it here](docs/INSTALL.txt)).

macOS users: the plugin is not notarized yet, so you need to run one Terminal command once (see INSTALL.txt).

## Build it yourself

You need CMake 3.22+ and a C++17 compiler (Xcode on macOS, Visual Studio 2022 on Windows). JUCE is downloaded automatically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target DDR_VST3
```

Developer extras:

- `-DJUCE_DIR=/path/to/JUCE` uses a local JUCE checkout instead of downloading it
- `-DDDR_BUILD_TOOLS=ON` builds `ddr_snapshot`, which renders the UI to PNG and runs a quick level and stability check

GitHub Actions builds and validates (pluginval) both platforms on every push. Pushing a tag like `v0.1.0` publishes a release with the zips.

## How it works

```
mix ─► source ─► head unit ─► speakers ─► cabin + seat ─► (+) ─► level match ─► out
       BT/FM      loudness,     low cut,     boom, modes,    ▲
                  SVC, limiter  EQ, cone     reflections,    │
                                distortion   seat delays     │
                                                     noise generator
                                       road, tyres, wind, engine, fan
```

The noise is added after the car stereo, because it reaches your ears directly. Noise levels are set in dB SPL and mapped so that a typical master plays at a car-like ~83 dB SPL. Level match keeps CAR mode as loud as your input, and gives the noise the same gain, so turning the car volume up lifts the music above the noise just like in a real car.

Built with [JUCE](https://juce.com). Licensed under the GNU AGPL v3.
