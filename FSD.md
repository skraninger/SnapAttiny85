# Functional Specification Document (FSD)

## Snap — ATtiny85 Music & Light Toy

| Field            | Value                                              |
| ---------------- | -------------------------------------------------- |
| **Product name** | Snap                                               |
| **Document**     | Functional Specification Document (FSD)            |
| **Version**      | 1.0 (Draft)                                        |
| **Date**         | 2026-10-10                                         |
| **Status**       | Draft for review                                   |
| **Target HW**    | Microchip ATtiny85 (DIP-8 / SOIC-8), 8 KB flash    |
| **Source of idea** | `Project Idea.md`                                |
| **Reference docs** | `ATtiny85 NeoPixel + Capacitive Touch.md`, `LedAttinyTouchx2.ino`, `DigitalMusicBox.c` |

### Revision history

| Ver | Date       | Author | Change                         |
| --- | ---------- | ------ | ------------------------------ |
| 1.0 | 2026-10-10 | —      | Initial FSD derived from idea  |

---

## 1. Overview

**Snap** is a small, self-contained electronic toy for a young child. It plays
short, recognisable tunes through a small speaker while lighting an 8-pixel
WS2812B ("NeoPixel") strip with a variety of animated patterns. The child
interacts entirely through two large capacitive touch pads and one physical
rotary knob (variable resistor). There is no screen, no network, and no
configuration UI — every control is a single, discoverable physical gesture.

The design is built around a single **ATtiny85** microcontroller. All firmware
runs from the chip's on-board 8 KB flash and 512 B SRAM; the only passive
support components are a data-line resistor, a bulk capacitor, an audio driver,
and the pads/knob.

### 1.1 Purpose of this document

This FSD defines **what** Snap must do (functions and behaviour) and the
constraints it must satisfy. It is the agreement between the stakeholder
(the parent/specifier) and the implementer. It does not prescribe final code,
but it fixes the external behaviour, interfaces, data formats, and acceptance
criteria so the firmware can be implemented and verified against it.

### 1.2 Design lineage

The implementation reuses and adapts three proven building blocks supplied with
the idea:

| Block                        | Source file                              | Reused for                                  |
| ---------------------------- | ---------------------------------------- | ------------------------------------------- |
| Capacitive touch (ADC method) | `LedAttinyTouchx2.ino` (`ADCTouch`)     | Touch pads + debounce + baseline calibration |
| LED animation patterns        | `LedAttinyTouchx2.ino`                  | Pattern engine (8 patterns)                 |
| Polyphonic tone synthesis     | `DigitalMusicBox.c`                     | Music engine (DDS + PWM audio)              |

---

## 2. Scope

### 2.1 In scope (v1)

- Firmware for ATtiny85 that drives LED patterns and plays music.
- Two capacitive touch inputs (Song select, Pattern select).
- One analog variable-resistor input (knob).
- Eight-pixel NeoPixel output.
- Audio output to a small speaker via a simple driver.
- At least four built-in songs and at least eight LED patterns.
- Persistence of last-used pattern/song across power cycles.
- A **music conversion pipeline** (script/tool) that turns note data into the
  firmware's compact song format.
- A wiring diagram, bill of materials, and build/flash instructions.

### 2.2 Out of scope (v1)

- Enclosures, mechanical CAD, or manufacturing tooling.
- Bluetooth/Wi-Fi/App connectivity.
- Rechargeable battery management / charging circuit.
- Runtime firmware update/user code loading.
- Recording or user-supplied songs at runtime (songs are compiled in).
- Multi-device sync.

### 2.3 Assumptions

- A single child (approx. 3–6 years) is the primary user; an adult may replace
  batteries and re-flash firmware.
- The toy is powered from a fixed 5 V supply (USB power bank or 3 × AA with a
  boost, see §10.3).
- A suitable programmer (USBasp / USBtinyISP) is available for flashing.

---

## 3. Definitions & acronyms

| Term        | Meaning                                                                 |
| ----------- | ----------------------------------------------------------------------- |
| **FSD**     | Functional Specification Document                                       |
| **DDS**     | Direct Digital Synthesis — integer phase-accumulator tone generation    |
| **WS2812B** | Addressable RGB LED used in the "NeoPixel" strip                        |
| **NeoPixel**| Adafruit's driver/library for WS2812B                                   |
| **PWM**     | Pulse-Width Modulation (used to synthesise the analog audio waveform)   |
| **ADC**     | Analog-to-Digital Converter (used for touch sensing and the knob)       |
| **Baseline**| Calibrated resting reading of a touch pad with no finger present        |
| **Pattern** | One animated LED effect (e.g. Rainbow, Breathe)                         |
| **Song**    | One compiled-in tune                                                    |
| **Chord word** | One 32-bit time slot of the song format (1 bit per note)             |
| **MoSCoW**  | Prioritisation: Must / Should / Could / Won't                           |

---

## 4. Target users & use cases

| ID  | Actor   | Use case                                                                 |
| --- | ------- | ------------------------------------------------------------------------ |
| UC1 | Child   | Turn the toy on and immediately see lights and hear music.               |
| UC2 | Child   | Tap the **Song** pad to change to a different tune.                      |
| UC3 | Child   | Tap the **Pattern** pad to change the light animation.                   |
| UC4 | Child   | Turn the **knob** and see/hear the effect change smoothly.               |
| UC5 | Child   | Leave the toy; tapping it again behaves predictably (state is remembered).|
| UC6 | Adult   | Flash new firmware and re-convert the song set.                          |

---

## 5. System overview & architecture

```
                        +---------------------------+
   5V supply  --------->|  VCC (pin 8)   ATtiny85    |
                        |                            |
   Touch pad "SONG" --->| PB2 (pin 7, ADC1)          |
   Touch pad "PLAY" --->| PB3 (pin 2, ADC3)          |
   Knob (pot) wiper --->| PB4 (pin 3, ADC2)          |
                        |                            |
                        | PB0 (pin 5) ---[470R]---> WS2812B DIN  (8 px)
                        | PB1 (pin 6, OC1A) ------> Audio driver --> Speaker
                        | PB5 (pin 1) = RESET (ISP)  |
   GND  <--------------|  GND (pin 4)               |
                        +---------------------------+
```

Logical software layers:

```
+-----------------------------------------------------------+
| Application / state machine                               |
|   - song selection, pattern selection, knob mapping       |
+-----------------------------------------------------------+
| Pattern engine          |  Music engine (DDS + PWM)        |
|   - 8 effects, frame t  |  - 4-voice, 32-note scale        |
+-----------------------------------------------------------+
| Input services          |  Persistence (EEPROM)            |
|   - ADCTouch reads      |  - last pattern/song             |
|   - baseline + debounce |  - wear-protected writes         |
+-----------------------------------------------------------+
| HAL: pinMode/digitalWrite/analogRead/timers/interrupts     |
+-----------------------------------------------------------+
```

---

## 6. Hardware specification

### 6.1 Pin map (fixed by this spec)

| ATtiny85 Pin | Port | Function                | Direction | Notes                                         |
| :----------: | :--: | ----------------------- | :-------: | --------------------------------------------- |
| 1            | PB5  | RESET (ISP)             | —         | Left as reset; not repurposed in v1           |
| 2            | PB3  | Touch pad 2 — **PLAY**  | Input     | ADC3 → `ADCTouch` channel 3                   |
| 3            | PB4  | NeoPixel data out       | Output    | WS2812B DIN via 470 Ω (Phase 2); pot deferred  |
| 4            | GND  | Ground                  | —         | Must be common with LED/audio supply          |
| 5            | PB0  | Beat / touch LED        | Output    | Active HIGH (Phase 1); retained in Phase 2    |
| 6            | PB1  | Audio PWM out (OC1A)    | Output    | Timer1 + 64 MHz PLL → speaker driver          |
| 7            | PB2  | Touch pad 1 — **SONG**  | Input     | ADC1 → `ADCTouch` channel 1                   |
| 8            | VCC  | 5 V (MCU + logic)       | —         | Decoupled with 100 nF + 100 µF                 |

> **Correction on ADC pins:** on the ATtiny85 only **PB5 (ADC0), PB2 (ADC1),
> PB4 (ADC2)** and **PB3 (ADC3)** are ADC-capable; **PB0 and PB1 are not**.
> The touch pads therefore sit on PB2/PB3; the audio PWM lives on **PB1 (OC1A)**
> (the music engine needs Timer1's PLL-clocked PWM); and because the pot is
> deferred, the Phase 2 NeoPixel uses the otherwise-unused **PB4**, leaving
> **PB0** as the beat/touch LED.

### 6.2 Bill of materials

| Qty | Part                                   | Purpose                         |
| :-: | -------------------------------------- | ------------------------------- |
| 1   | ATtiny85 (DIP-8 or SOIC-8)             | MCU                             |
| 1   | WS2812B strip, 8 pixels                | Light output                    |
| 1   | 470 Ω resistor                         | NeoPixel data-line protection   |
| 1   | 100 µF electrolytic capacitor          | Bulk decoupling at strip        |
| 2   | Copper tape / bare wire pads (≥1 cm²)  | Capacitive touch electrodes     |
| 1   | 10 kΩ linear potentiometer + knob      | Variable resistor control       |
| 1   | NPN transistor (e.g. 2N3904) + 100 Ω   | Audio driver                    |
| 1   | 10–47 µF capacitor (audio coupling)    | DC-block to speaker             |
| 1   | 8 Ω / 0.5 W small speaker              | Sound output                    |
| 1   | 5 V / ≥2 A supply (or 3×AA + boost)    | Power                           |
| —   | USBasp/USBtinyISP, wires, protoboard   | Programming & assembly          |

### 6.3 Clock

- **Baseline:** 8 MHz internal RC (`clock=internal8`), matching the working
  `LedAttinyTouchx2` build and reliable `ADCTouch` behaviour.
- **Optional:** 16 MHz (internal PLL) where higher audio-carrier frequency is
  desired (see §11.1). If used, NeoPixel timing and the audio sample rate MUST
  be re-derived.

---

## 7. Functional requirements

Priorities: **M** = Must (v1), **S** = Should, **C** = Could, **W** = Won't (v1).

### 7.1 Startup & operation

| ID    | Requirement                                                                                  | Pri | Verify |
| ----- | -------------------------------------------------------------------------------------------- | :-: | :----: |
| FR-01 | On power-up the device shall begin operating within 500 ms and show the remembered pattern.   | M   | T-01   |
| FR-02 | The LED strip shall continuously render the currently selected pattern.                       | M   | T-02   |
| FR-03 | The music engine shall play the selected song through the speaker.                            | M   | T-03   |
| FR-04 | On first boot (unwritten EEPROM) the device shall default to pattern 0 and song 0.            | M   | T-04   |

### 7.2 Touch controls

| ID    | Requirement                                                                                       | Pri | Verify |
| ----- | ------------------------------------------------------------------------------------------------- | :-: | :----: |
| FR-05 | Touch pad **SONG** shall advance to the next song and begin playing it immediately (wrapping).    | M   | T-05   |
| FR-06 | Touch pad **PATTERN** shall advance to the next LED pattern (wrapping after the last).             | M   | T-06   |
| FR-07 | Touch input shall be baseline-calibrated at startup (≈100 samples) to reject ambient drift.        | M   | T-02   |
| FR-08 | Touch input shall be debounced so one physical touch produces exactly one action.                  | M   | T-07   |
| FR-09 | A touch shall be registered only when the reading exceeds the calibrated baseline by a threshold.  | M   | T-08   |
| FR-10 | Touch shall remain functional while music is playing and while LEDs animate.                       | M   | T-09   |
| FR-11 | (Optional) Long-press on **SONG** (≥1 s) shall play/pause the current song.                        | C   | T-10   |

### 7.3 Variable-resistor (knob) control

| ID    | Requirement                                                                                       | Pri | Verify |
| ----- | ------------------------------------------------------------------------------------------------- | :-: | :----: |
| FR-12 | The knob shall be sampled via the ADC and mapped to a continuous output parameter.                 | M   | T-11   |
| FR-13 | **Default mapping:** the knob shall control LED **brightness** (0–255) with smoothing so changes are gradual. | M | T-11 |
| FR-14 | The knob mapping shall be a compile-time option, selectable among: brightness, music volume, LED animation speed. | S | T-12 |
| FR-15 | Knob changes shall take effect within 100 ms.                                                      | M   | T-11   |

> **Design note:** the pot role is configurable via a single `#define POT_ROLE`
> constant (see §11.5) so the behaviour can be changed without a rewrite. The
> FSD commits to *brightness* as the shipped default.

### 7.4 LED patterns

| ID    | Requirement                                                                                    | Pri | Verify |
| ----- | ---------------------------------------------------------------------------------------------- | :-: | :----: |
| FR-16 | The firmware shall provide at least eight distinct patterns (see §8.1).                        | M   | T-13   |
| FR-17 | Patterns shall animate at 20–60 fps and loop indefinitely.                                     | M   | T-13   |
| FR-18 | Changing pattern shall clear residual pixel state before rendering the new pattern.            | M   | T-06   |
| FR-19 | Patterns shall respect the knob-controlled parameter.                                          | M   | T-11   |

### 7.5 Music

| ID    | Requirement                                                                                    | Pri | Verify |
| ----- | ---------------------------------------------------------------------------------------------- | :-: | :----: |
| FR-20 | The firmware shall include at least four (4) songs, defined in §8.2.                           | M   | T-03   |
| FR-21 | Songs shall be stored as compact PROGMEM note data in the format of §9.1.                      | M   | T-03   |
| FR-22 | The engine shall be polyphonic (≥2 simultaneous voices) with note decay.                       | S   | T-03   |
| FR-23 | When a song finishes, the device shall either loop it or advance per the configured policy.    | M   | T-14   |
| FR-24 | Music shall play at a fixed, child-safe output level unless the volume mapping is selected.     | M   | T-15   |

### 7.6 Persistence

| ID    | Requirement                                                                                     | Pri | Verify |
| ----- | ----------------------------------------------------------------------------------------------- | :-: | :----: |
| FR-25 | The device shall remember the last selected pattern and song across power cycles.               | S   | T-16   |
| FR-26 | EEPROM writes shall be deferred until 5 s after the last interaction to limit flash wear.       | S   | T-16   |
| FR-27 | EEPROM values shall be range-checked on read; out-of-range values reset to defaults.            | M   | T-04   |

---

## 8. Content specification

### 8.1 LED patterns (v1 set)

Adapted from `LedAttinyTouchx2.ino`; each pattern is indexed 0..N-1 and cycled
by the **PATTERN** pad.

| # | Name            | Behaviour                                                   | Knob-dependent |
| :-: | --------------- | ----------------------------------------------------------- | :-: |
| 0 | Solid           | All pixels one colour (hue drifts slowly)                   | Brightness |
| 1 | Breathe         | Triangle-wave brightness fade in/out on base colour         | Brightness |
| 2 | Chaser          | Single bright pixel runs around the strip                   | Brightness |
| 3 | Rainbow         | Full-spectrum gradient scrolling across the strip           | Speed |
| 4 | Wave            | Sinusoidal hue wave across pixels                           | Speed |
| 5 | Scanner         | One pixel bounces end-to-end, colour changes per sweep      | Brightness |
| 6 | Firefly         | One pseudo-random pixel pulses through the rainbow          | Brightness |
| 7 | Color Wipe      | Strip fills pixel-by-pixel with a new random colour per pass| Speed |

> The v1 set is fixed. A future revision may add a **color** selector driven by
> the remaining pin (PB5) if the RESET fuse is repurposed (§13.3).

### 8.2 Songs (v1 set)

| # | Title                                   | Excerpt           |
| :-: | ------------------------------------- | ----------------- |
| 0 | Bluey Theme                             | Main theme        |
| 1 | Baby Shark                              | Chorus            |
| 2 | Can't Stop the Feeling (Justin Timberlake) | Chorus         |
| 3 | Dynamite (BTS)                          | One verse         |

> **Licensing note:** These are copyrighted works. They are included only as
> short excerpts for **private, non-commercial** use in a personal toy. Any
> public distribution of the compiled firmware must address licensing.

---

## 9. Data model

### 9.1 Song format (chord-word table)

Compatible with the format in `DigitalMusicBox.c`:

- The tune is a `const uint32_t[]` array in `PROGMEM`.
- Each element is a **chord word**: one time slot. Bit *b* set means the note at
  `Scale[b]` is retriggered in that slot (MSB = note 0).
- Slots advance at the configured tempo (default **4 slots/second**).
- The sequence terminates with the sentinel chord word `0x000000FF`.
- Frequencies come from a 32-entry `Scale[]` table (see `DigitalMusicBox.c`).

```
Tune[] = {
  0b00000000000000000001000000000000,   // slot 0: note 14
  0b10000000000000000000010000000000,   // slot 1: notes 0 and 18
  ...
  0xFF                                  // end-of-tune sentinel
};
```

**Size guidance:** 4 bytes/slot × 4 slots/s = 16 bytes/s. A 30 s excerpt ≈
480 bytes. Four songs ≈ 1.9 KB of flash (plus code), which must fit in 8 KB
alongside the NeoPixel and touch libraries (§11.2).

### 9.2 EEPROM map

| Address | Size | Field          | Values / notes                    |
| :-----: | :--: | -------------- | --------------------------------- |
| 0x00    | 1    | Magic/version  | 0x53 = 'S'; else re-init defaults |
| 0x01    | 1    | patternIndex   | 0 .. (numPatterns-1)              |
| 0x02    | 1    | songIndex      | 0 .. (numSongs-1)                 |
| 0x03    | 1    | reserved       | (future: brightness/color)        |

Writes use `EEPROM.update()` and are performed only after 5 s of inactivity
(FR-26).

### 9.3 Music conversion pipeline (deliverable)

| Item     | Detail                                                                 |
| -------- | ---------------------------------------------------------------------- |
| Input    | Note sequence (text/MIDI subset) per song                              |
| Tool     | Script (e.g. Python) that maps note names → `Scale[]` indices and emits the `uint32_t[]` PROGMEM array |
| Output   | Ready-to-paste C array per song, plus a byte-size report               |
| Verify   | Generated array compiles and plays the intended melody (T-17)          |

> **Implemented in this repo:** `tools/convert_song.py` (the converter),
> `songs/*.song` (the simplified melodies), and `songs.h` (generated PROGMEM
> arrays for the four v1 songs). The note table, sources, and the full
> conversion procedure are documented in `Music Conversion.md`.

---

## 10. Non-functional requirements

| ID     | Category       | Requirement                                                                                   |
| ------ | -------------- | --------------------------------------------------------------------------------------------- |
| NFR-01 | Footprint      | Firmware shall fit within 8 KB flash and 512 B SRAM of the ATtiny85.                           |
| NFR-02 | Responsiveness | Touch-to-action latency ≤ 100 ms; knob-to-effect latency ≤ 100 ms.                             |
| NFR-03 | Smoothness     | LED animation runs at 20–60 fps; no visible flicker or tears.                                  |
| NFR-04 | Audio quality  | Audio shall be intelligible with no sustained background whine; brief artifacts per §11.1 are acceptable if < 20 ms. |
| NFR-05 | Power          | Typical consumption ≤ 600 mA at 5 V with 8 pixels at moderate brightness; peak documented per §10.3. |
| NFR-06 | Reliability    | Touch shall be stable with ≤ 1 false trigger per hour of normal use.                           |
| NFR-07 | Safety (child)| Low voltage (≤ 5 V) only; no exposed sharp edges in the finished build; output volume limited (adult-config) to protect hearing. |
| NFR-08 | Maintainability| All tunables exposed as named `#define`s; code documented; build reproducible via `arduino-cli`. |
| NFR-09 | Testability    | Behaviour verifiable via the test cases in §12 without specialised equipment.                 |

### 10.3 Power budget

| Load                        | Typical | Peak  |
| --------------------------- | :-----: | :---: |
| ATtiny85                    | 10 mA   | 20 mA |
| WS2812B × 8 (mixed colour)  | 160 mA  | 480 mA (full white) |
| Speaker (small, audio)      | 50 mA   | 150 mA |
| **Total**                   | ~220 mA | ~650 mA |

Recommend a **5 V / ≥ 2 A** supply; add a 100 µF bulk capacitor across the
strip supply. Keep `setBrightness ≤ 80` for 8 pixels on a 1 A supply.

---

## 11. Technical design notes, constraints & risks

### 11.1 Interrupt & timer contention (highest risk)

The two output subsystems compete for the CPU:

- **NeoPixel (WS2812B)** requires a bit-banged 800 kHz signal with **interrupts
  disabled** for the duration of `strip.show()` (≈ 240 µs for 8 pixels).
- **Audio (DDS + PWM)** needs a periodic sample interrupt (≈ 20–60 kHz) to
  update the PWM duty register.

A `strip.show()` therefore starves the audio sample interrupt, which can
produce a short click/buzz each LED frame.

**Mitigations (in priority order):**
1. Only call `strip.show()` when the frame actually changes, not every loop.
2. Throttle LED frame rate (e.g. ≤ 30 fps) to bound the artifact duty cycle.
3. Keep the number of pixels small (8) to shorten the interrupt-off window.
4. Use a higher audio carrier (16 MHz PLL → 62.5 kHz Timer0 PWM) to reduce the
   perceived artifact.
5. Accept residual artifacts; document them as a known limitation of the toy.

### 11.2 Flash / SRAM budget

| Component                     | Est. flash | Est. SRAM |
| ----------------------------- | :--------: | :-------: |
| Arduino core + HAL            | ~1.5 KB    | ~32 B     |
| Adafruit_NeoPixel (+t85 fork) | ~1.5 KB    | 24 B (8 px) |
| ADCTouch                      | ~0.3 KB    | ~8 B      |
| Pattern engine (8 patterns)   | ~1.5 KB    | ~64 B     |
| Music engine (DDS ISR + Scale)| ~1.5 KB    | ~40 B     |
| Song data (4 excerpts)        | ~2.0 KB    | 0 (PROGMEM) |
| **Total**                     | **~8.3 KB** (tight) | ~170 B |

**Risk:** the estimate slightly exceeds 8 KB. Mitigations: shorten excerpts,
restrict to 16 notes/slot (halves song size), drop to 3 voices, and/or reduce
patterns. Recommend `-Os` and avoiding `float`/`sin()` (use integer LUTs).

### 11.3 Touch under LED/audio noise

WS2812 data edges and PWM audio are EMI sources that can destabilise capacitive
touch. Mitigations: route touch wires away from the data line; add 100 nF from
each touch pin to GND; re-baseline periodically; require the reading to clear a
delta threshold (as in `chkTouch`).

### 11.4 Audio PWM carrier

An 8-bit PWM carrier at ~31 kHz (8 MHz clock) may be faintly audible as a high
whine on sensitive speakers. Prefer a higher carrier (16 MHz PLL) or add a
simple RC/LC low-pass before the speaker.

### 11.5 Configuration constants (implementation contract)

| Constant         | Default | Meaning                                   |
| ---------------- | :-----: | ----------------------------------------- |
| `NUM_LEDS`       | 8       | Strip length                              |
| `LED_PIN`        | 4 (PB4) | NeoPixel data (Phase 2); PB0 = beat LED   |
| `TOUCH_SONG`     | 2 (PB2) | Song pad (`ADCTouch` channel 1)           |
| `TOUCH_PLAY`     | 3 (PB3) | Play/Pause pad (`ADCTouch` channel 3)     |
| `POT_PIN`        | — (deferred) | Knob ADC input; PB4 reused by the strip |
| `AUDIO_PIN`      | 1 (PB1) | PWM audio out (OC1A)                      |
| `POT_ROLE`       | `BRIGHTNESS` | `BRIGHTNESS` \| `VOLUME` \| `SPEED`  |
| `TOUCH_THRESH`   | 40      | Touch delta threshold                     |
| `TOUCH_DELAY`    | 500 ms  | Touch debounce interval                   |
| `NUM_PATTERNS`   | 8       | Pattern count                             |
| `NUM_SONGS`      | 4       | Song count                                |
| `TEMPO`          | 4 /s    | Slots per second                          |
| `WRITE_DELAY`    | 5000 ms | EEPROM deferral                           |

---

## 12. Acceptance test cases

| ID   | Test                                    | Expected result                                                        |
| ---- | --------------------------------------- | --------------------------------------------------------------------- |
| T-01 | Power on with valid EEPROM              | Lights + (audio) active within 500 ms; remembered pattern shown.        |
| T-02 | Leave idle 10 minutes                   | Pattern keeps running; no spurious pattern/song change.                |
| T-03 | Play each song                          | All 4 songs audible and recognisable; end behaviour per FR-23.         |
| T-04 | Erase EEPROM and boot                   | Defaults to pattern 0 / song 0; no crash.                              |
| T-05 | Tap SONG repeatedly through the cycle   | Songs advance 0→1→2→3→0, each starts immediately.                      |
| T-06 | Tap PATTERN repeatedly                  | Patterns advance 0…7→0; strip clears between patterns.                 |
| T-07 | Rapid double-tap a pad                  | Exactly one action per deliberate touch.                              |
| T-08 | Vary finger pressure/position           | Touch registers reliably above threshold; no trigger when near but not touching. |
| T-09 | Touch pads while music plays            | Inputs still respond; no lock-up.                                      |
| T-10 | Long-press SONG (if implemented)        | Play/pause toggles.                                                    |
| T-11 | Sweep knob                              | Chosen parameter changes smoothly; effect visible/audible ≤ 100 ms.    |
| T-12 | Rebuild with each `POT_ROLE`            | Mapping changes as configured.                                        |
| T-13 | Observe each pattern 60 s               | Animates smoothly at 20–60 fps, no flicker/gaps.                       |
| T-14 | Let a song finish                       | Loops or advances per policy; no glitch.                               |
| T-15 | Measure output level                    | Fixed child-safe level (or knob-limited).                              |
| T-16 | Set pattern/song, power cycle           | Same pattern/song restored; EEPROM written only after 5 s idle.        |
| T-17 | Run conversion script on a known tune   | Emitted array compiles and plays correct melody and size.              |
| T-18 | Listen during LED refresh               | No sustained whine; artifacts (if any) < 20 ms and intermittent.       |

---

## 13. Open questions / decisions needed

| #   | Question                                                          | Proposed default |
| --- | ----------------------------------------------------------------- | ---------------- |
| Q1  | Knob role: brightness vs volume vs speed?                         | **Brightness** (configurable) |
| Q2  | Should music auto-play on power-up, or stay silent until SONG tap?| **Auto-play** last song |
| Q3  | Song end policy: loop, or advance to next?                        | **Loop** current |
| Q4  | Add a third touch pad for colour (PB5, disable RESET)?            | **No** in v1 |
| Q5  | Exact excerpt lengths to fit flash?                               | ~30 s each (tune at build) |
| Q6  | Power source: USB power bank vs 3×AA + boost?                     | USB 5 V for v1 |

---

## 14. Deliverables checklist

- [ ] `Snap.ino` (or equivalent multi-file sketch) implementing this FSD.
- [ ] `songs.h` with converted PROGMEM song tables.
- [ ] `tools/convert_song.py` music conversion script.
- [ ] Wiring diagram / schematic (`FSD` §5–6 as baseline).
- [ ] Build & flash instructions for `arduino-cli` and the Arduino IDE.
- [ ] This FSD kept current with implemented behaviour.

---

## Appendix A — ATtiny85 DIP-8 pinout reference

```
        +-----------+
  PB5 --|1         8|-- VCC
  PB3 --|2         7|-- PB2
  PB4 --|3         6|-- PB1
  GND --|4         5|-- PB0
        +-----------+
```

| ADC channel | Pin | Port |
| :---------: | :-: | :--: |
| ADC0        | 5   | PB0  |
| ADC1        | 7   | PB2  |
| ADC2        | 3   | PB4  |
| ADC3        | 2   | PB3  |

## Appendix B — References

- `Project Idea.md` — origin of the product concept.
- `ATtiny85 NeoPixel + Capacitive Touch.md` — NeoPixel + touch wiring, patterns, power.
- `LedAttinyTouchx2.ino` — dual-touch LED lamp: `ADCTouch`, calibration, debounce, EEPROM, 8 patterns.
- `DigitalMusicBox.c` — David Johnson-Davies, 4-voice DDS music engine (CC BY 4.0).
