# My Car: measure your own car with a phone

DDR can learn the sound of **your** car. You play a test signal in the car, record it with your phone, and DDR turns the recording into a car type called **My Car**. No measurement microphone needed.

It takes about 10 minutes: 2 in the car, 2 in the studio, the rest walking between them.

![The My Car screen](ddr-mycar.png)

## What you need

- **Your phone** with a voice recorder app (it records).
- **The test signal**: in DDR click **MEASURE MY CAR… → 1 SAVE TEST SIGNAL**, or download [DDR-test-signal.mp3](my-car/DDR-test-signal.mp3). It is 43 seconds long: 3 seconds of noise, then three slow sweeps from 20 Hz to 20 kHz.
- **A way to play it in the car that is not the recording phone.** Pick one:
  - a **USB stick** with the MP3 or WAV on it (best: no Bluetooth compression), or a CD,
  - a **second phone, tablet or laptop** over Bluetooth or an AUX cable.

> The recording phone can't play the signal at the same time: recorder apps stop other audio, and Bluetooth switches to "hands-free" mode with the car's own microphone.

## Set up the recorder app

Turn off everything that "improves" the sound:

- **iPhone – Voice Memos:** Settings → Apps → Voice Memos → Audio Quality: **Lossless**. In the recording's options, keep **Enhance Recording off**. To get the file: Share → Save to Files (or AirDrop to your Mac).
- **Android:** use a recorder that can record in **high quality or WAV**, and switch off noise reduction, "speech" or "voice" modes.

DDR reads **WAV, AIFF, FLAC and MP3** on every computer, and **M4A** (iPhone) on a Mac. On Windows, drag an M4A into your DAW and export it as WAV first.

## In the car (about 2 minutes)

1. **Park somewhere quiet.** Engine **off** but the stereo on, fan **off**, windows and doors **shut**.
2. **Stereo settings:** bass and treble at 0, balance and fader in the middle, sound effects off. Leave on whatever you can't switch off.
3. **Sit in the driver's seat.** Hold the phone at your head height, about 10 cm in front of your face, its bottom edge (where the microphone is) pointing forward. Keep still and quiet.
4. **Start recording, then start the test signal.**
5. While the **noise** plays (first 3 seconds), set the volume **loud but comfortable**, like you'd play a favourite song. If you change the volume, just let the signal play to the end – the sweeps after the noise are what counts.
6. Wait until the last sweep ends, then stop the recording. Maybe make a second recording as a spare.

## In the studio (recommended, about 2 minutes)

This cancels out your phone's microphone and your monitors, so the result is "how the car differs from your studio" – exactly what you need when you listen to DDR on those monitors. Without it, the bass of My Car can come out too weak, because phone microphones lose the lowest bass.

1. Put the test signal WAV on a track in your DAW. **Bypass DDR** and everything on the master bus.
2. Sit at your mixing position and play it at your normal mixing level.
3. Record it with the **same phone, app and grip** as in the car.

## In DDR

1. Click **MEASURE MY CAR…** (under the car type).
2. **2 LOAD CAR RECORDING** – pick the car recording. DDR finds the sweeps and shows the curve. You want **3 of 3 sweeps** and **signal-to-noise above 30 dB**.
3. **3 LOAD STUDIO RECORDING** – pick the studio recording (optional, recommended).
4. **NOISE AND SIZE LIKE** – pick the preset closest to your car. Road noise, engine and cabin size come from it; the speakers and cabin sound come from your measurement.
5. Click **USE AS MY CAR**. The car type switches to **My Car**.

My Car is saved in your DAW project, and every new DDR instance on this computer starts with it too. All the other controls (speed, road, seat, volume, the Advanced knobs) work on My Car just like on the presets.

## The curve

- **Orange** is what was measured (gaps = too noisy to trust).
- **White** is what DDR will play. It is built from the same speaker and cabin filters as the presets, so it follows the broad shape and the big bumps and dips, not every tiny wiggle.
- **"DDR follows the measurement within X dB"** – under about 2 dB is a good fit.

## If something goes wrong

| DDR says | Do this |
| --- | --- |
| *The test signal was not found* | Wrong file, or the recording missed the sweeps. Record the whole signal, from the noise to the end. |
| *noisy* | Volume up a bit, engine and fan off, find a quieter spot. |
| *The recording clips* | Volume down a little and record again. |
| *Some sweeps are missing* | A noise covered one; the rest are used. Fine unless it keeps happening. |
| *DDR can't open …* | Export the recording as WAV from your DAW and load that. |
| *sample rate is too low* | Choose a higher quality setting in the recorder app. |

## What it can and can't do

- It measures **one spot** (where the phone was) with **both front speakers playing together**. The seat control still moves you around, using DDR's own model.
- The lowest bass (under ~40 Hz) and the top octave (above ~16 kHz) are hard for phones; DDR doesn't trust those parts much.
- It measures the **sound system and cabin**, not the road noise. The noise comes from the preset you pick.

## How it works

The test signal contains exponential sine sweeps. DDR deconvolves the recording with the inverse sweep, which turns each sweep into the car's impulse response. It cuts out the first 250 ms of each, averages the three, smooths the response to 1/6 octave and (if you gave it one) divides it by the studio measurement. Then it fits DDR's own car filters – speaker low cut, cabin boom, up to 15 bands – so that DDR's whole chain at the driver's seat matches the measured curve. The code is in [`Source/calibration`](../Source/calibration), and `ddr_caltest` tests it end to end with a simulated car and phone.
