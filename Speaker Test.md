# Speaker Test — audio-path bring-up

**Status:** test sketch · **Build:** `972 bytes flash (11%)`, `28 bytes RAM (5%)`
**Sketch:** `SpeakerTest/SpeakerTest.ino` (self-contained, no libraries)

---

## 1. Purpose

`SpeakerTest` is the smallest program that exercises the **whole audio output
path** of the Snap toy. It plays one square-wave voice that alternates between
two clearly separated notes and flashes a plain LED whenever the note changes.

Use it to verify, before wiring up the touch pads / NeoPixel:

* the **Timer1 + 64 MHz PLL** PWM carrier is configured correctly,
* the **PB1 (OC1A)** audio pin and the transistor/speaker driver work,
* the **8 MHz internal clock** and the `Scale[]`/DDS maths produce the expected
  pitch (the same engine used by `Phase1_MusicBox.ino`),
* a **second (plain) LED** is wired and flashes on PB0.

It is a stripped-down build of the Phase 1 engine: same timers, same `Scale[]`
table and sample ISR, but **one voice at a constant amplitude** and **no touch,
songs, or EEPROM**.

### 1.1 What it does

| Behaviour | Detail |
| --------- | ------ |
| Tone A | **A4 ≈ 440 Hz** (`Scale[13]` = 1442) |
| Tone B | **A5 ≈ 880 Hz** (`Scale[25]` = 2884) — one octave up |
| Alternation | switch note every **500 ms** (`TOGGLE_MS`) |
| LED | flashes on **PB0** for **120 ms** (`LED_FLASH_MS`) at each change |
| Loop | runs forever, no interaction required |

So the speaker should go *low–high–low–high…* twice a second, and the LED
should give a short blink exactly in step with each change.

---

## 2. Hardware

### 2.1 Pin map

| ATtiny85 pin | Port | Function | Notes |
| :----------: | :--: | -------- | ----- |
| 1 | PB5 | RESET (ISP) | reserved |
| 2 | PB3 | *free* | — |
| 3 | PB4 | *free* | — |
| 4 | GND | Ground | common with supply/audio |
| 5 | PB0 | **LED** | plain LED + ~330 Ω to GND, active HIGH |
| 6 | PB1 | **Audio PWM (OC1A)** | Timer1 + 64 MHz PLL → speaker driver |
| 7 | PB2 | *free* | — |
| 8 | VCC | 5 V | decouple 100 nF + 100 µF |

PB0 is deliberately chosen because it is the pin Phase 2 reserves for the
NeoPixel data line; PB1 is the audio pin shared with Phase 1, so the test
exercises the real output stage.

### 2.2 Wiring

```
        +5V                       +5V
         |                         |
     [ SPEAKER 8Ω ]            [330R]
         |                         |
  PB1 --[1k]--|<  NPN (2N3904)  PB0 --|>|--+
              |\e                        LED
               |                           |
         [10–47µF]---/                    GND
               |
              GND
```

* The **1 kΩ base resistor** keeps the ATtiny85 output within its current limit.
* The **10–47 µF series capacitor** DC-blocks the speaker so its cone rests
  centred. A **piezo buzzer** driven through ~100 Ω also works.
* The **LED** is an ordinary indicator LED; keep it modest (~10 mA) so it does
  not load the port. It is independent of the audio path.

---

## 3. Build & flash

Using `arduino-cli` with the **damellis `attiny` core** (same as Phase 1):

```bash
# compile
arduino-cli compile --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 SpeakerTest

# upload (replace COMx with your programmer's port)
arduino-cli upload  --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 -p COMx SpeakerTest
```

No external libraries are required (the touch library is not used).

> **8 MHz only.** The sample rate (and therefore the exact pitch) assumes
> `clock=internal8`. The sketch has an `#error` that refuses to build at any
> other clock.

---

## 4. Expected result / pass criteria

| # | Check | Expected |
| :-: | ----- | -------- |
| 1 | Compile | `972 bytes (11%) flash`, `28 bytes (5%) RAM` |
| 2 | Power on | low note (440 Hz) sounds immediately |
| 3 | Listen | note alternates up/down every ~0.5 s, no gaps or clicks |
| 4 | Watch LED | short blink on PB0 at each note change, in step with the audio |
| 5 | Run 1 minute | rhythm stays steady (no drift, no lock-up) |

If the timing is wrong, the alternation sounds uneven; if the pitch is wrong
(e.g. everything an octave off), the clock/build option is likely wrong.

---

## 5. Tuning the sketch

All knobs are `#define`s near the top of `SpeakerTest/SpeakerTest.ino`:

| Define | Default | Meaning |
| ------ | :-----: | ------- |
| `LED_PIN` | `0` | Plain-LED pin (PB0) |
| `TOGGLE_MS` | `500` | Time each note is held before switching |
| `LED_FLASH_MS` | `120` | LED on-time after each change |
| `TONE_LEVEL` | `100` | Square-wave amplitude, `1..127` (loudness) |
| `DECAY` | `9` | Envelope shift, kept equal to the music engine |
| `TONE_LOW` / `TONE_HIGH` | `Scale[13]` / `Scale[25]` | The two notes (change the indices to pick others) |

`Scale[]` is the 0-based 32-note table shared with `Reference/DigitalMusicBox.c` /
`Phase1_MusicBox.ino`, so any index from 0 to 31 is a valid note.

---

## 6. How it works

1. **Timer1 + PLL** generates a 250 kHz, 8-bit PWM carrier on OC1A (PB1);
   `OCR1A = 128` is the mid-rail (silence) duty.
2. **Timer0** fires a **20 kHz** sample interrupt (`OCR0A = 49`, prescaler 8 at
   8 MHz). Each interrupt advances a 16-bit phase accumulator by `Freq` and
   writes the resulting square-wave sample to `OCR1A`:

   ```
   audible frequency = Freq × (20000 / 65536) Hz    // Freq=1442 → 440 Hz
   ```

3. The same interrupt divides 20 kHz down to a **1 kHz millisecond counter**
   (`g_ms`), because reconfiguring Timer0 removes the Arduino `millis()` base.
4. `loop()` compares the millisecond count against `TOGGLE_MS`, flips the note
   via `setFreq()`, and starts a short LED pulse.

There is **no `delay()`/`millis()`** in the sketch — only `snapMillis()`, which
reads `g_ms` with interrupts masked.

---

## 7. Troubleshooting

| Symptom | Likely cause / fix |
| ------- | ------------------ |
| No sound at all | Check the transistor orientation, the 1 kΩ base resistor, and that PB1 (pin **6**) is the wire being driven. Confirm 5 V at the speaker driver. |
| Sound but wrong pitch | Build option is not `clock=internal8`, or the wrong `#error`-forcing clock. Re-flash with the exact FQBN above. |
| Sketch appears to hang on power-up | PLL did not lock (`while (!(PLLCSR & PLOCK))`). Verify VCC is a clean 5 V and the chip is a genuine ATtiny85. |
| LED never flashes | LED reversed or no series resistor; confirm the LED is on **PB0 (pin 5)** and active HIGH. |
| LED flashes but no audio (or vice-versa) | The two outputs are independent — this cleanly isolates a wiring fault to one path. |
| Faint high whine | Expected PWM carrier artefact (FSD §11.4); add a simple RC low-pass before the speaker if it is objectionable. |
| Too loud / harsh | Lower `TONE_LEVEL`. |

If pads are the next thing to bring up, tune touch response as described in
`Phase 1 - Music Box.md` §6.

---

## 8. Files

| File | Role |
| ---- | ---- |
| `SpeakerTest/SpeakerTest.ino` | This test sketch |
| `Speaker Test.md` | This document |
| `Phase1_MusicBox/Phase1_MusicBox.ino` | Full Phase 1 engine the sketch is derived from |
| `Reference/DigitalMusicBox.c` | Original 4-voice DDS engine (CC BY 4.0) |
| `Phase 1 - Music Box.md` | Phase 1 design & bring-up notes |
