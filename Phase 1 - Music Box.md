# Phase 1 — Music Box (touch-controlled)

**Status:** implemented · **Build:** `6614 bytes flash (80%)`, `141 bytes RAM (27%)`
**Sketch:** `Phase1_MusicBox/Phase1_MusicBox.ino` + `Phase1_MusicBox/songs.h`

---

## 0. The two-phase plan

The toy is built in two increments so each can be brought up and tested on its
own hardware before the next is added.

| | Phase 1 — Music box | Phase 2 — LED display |
| --- | --- | --- |
| **Function** | Play the four songs, controlled by touch | Light the 8-pixel strip; patterns driven by the variable resistor |
| **New pins** | PB1 (audio), PB2 + PB3 (touch) | PB4 (NeoPixel strip); pot deferred |
| **Timers** | Timer0 (20 kHz sample + note clock), Timer1+PLL (PWM carrier) | same, plus bit-banged NeoPixel timing |
| **Key risk** | none significant (this phase isolates the audio engine) | NeoPixel `show()` disables interrupts and starves the audio ISR |

Phase 1 deliberately avoids the hardest problem in the whole project — the
NeoPixel-vs-audio interrupt conflict (FSD §11.1) — by leaving the LED work out
entirely. It also keeps every resource the LED phase will need free.

---

## 1. Scope

**In scope**

* Play the four compiled-in songs (`songs.h`) through a mono PWM audio output.
  **Playback starts automatically at reset** (the remembered song), so audio can
  be heard immediately without touching anything.
* **Touch 1 (SONG)** — advance to the next song and start it playing.
* **Touch 2 (PLAY)** — play / pause the current song.
* **LED (PB0)** — lit (held high) while either touch pad is being touched, to
  visibly acknowledge a touch; additionally flashes a short pulse on every beat
  while a song is playing.
* Remember the last song in EEPROM and resume it on power-up.
* Loop the current song when it ends.

**Out of scope (deferred to Phase 2)**

* LED strip / patterns, the variable resistor, and the pattern engine.

---

## 2. Pin map

| ATtiny85 pin | Port | Phase 1 function | Notes |
| :----------: | :--: | ---------------- | ----- |
| 1 | PB5 | RESET (ISP) | reserved |
| 2 | PB3 | **Touch 2 — PLAY/PAUSE** | ADC3 → `ADCTouch` channel 3 |
| 3 | PB4 | *free* | ADC2; **Phase 2 puts the NeoPixel strip here** (pot deferred) |
| 4 | GND | Ground | common with audio/supply |
| 5 | PB0 | **Beat LED** | plain LED + ~330 Ω to GND (active HIGH); Phase 2 keeps it as the beat/touch LED |
| 6 | PB1 | **Audio PWM (OC1A)** | Timer1 + 64 MHz PLL |
| 7 | PB2 | **Touch 1 — SONG** | ADC1 → `ADCTouch` channel 1 |
| 8 | VCC | 5 V | decouple 100 nF + 100 µF |

**Why these pins**

* Only PB5/PB2/PB4/PB3 are ADC-capable, and PB5 is the reset pin, so both touch
  pads must live on **PB2/PB3/PB4**. Using PB2 + PB3 for touch leaves **PB4**
  free (Phase 1 reserved it for the pot; Phase 2 uses it for the NeoPixel strip).
* The music engine (`DigitalMusicBox.c`) drives its PWM from **Timer1 + the
  64 MHz PLL**. On the ATtiny85 Timer1's channel A is **OC1A = PB1**, so audio
  goes there (channel B would be PB4, which touch/carrier need).
* That leaves **PB0**: used here for a plain **beat LED**. Phase 2 keeps this LED
  and puts the NeoPixel strip on the unused **PB4** instead (see Phase 2 doc).

---

## 3. Audio output circuit

PB1 emits a 250 kHz, 8-bit PWM square wave. It must be amplified and
DC-blocked before the speaker:

```
        +5V
         |
     [ SPEAKER 8Ω ]
         |
  PB1 --[1k]--|<  NPN (2N3904)
              |\e
               |
         [10–47µF]---/
               |
              GND
```

* `1k` base resistor keeps the ATtiny output within its current limits.
* The series capacitor blocks DC so the speaker cone doesn't sit off-centre.
* A **piezo buzzer** also works — drive it directly (through ~100 Ω) instead of
  the transistor stage.

> The output is intentionally quiet; it is a child's toy. A small speaker plus
> the transistor stage is ample.

---

## 4. Software design

### 4.1 Engines reused

* **Audio** — `DigitalMusicBox.c` (David Johnson-Davies, CC BY 4.0): a 4-voice
  DDS oscillator. Each voice is a square wave built from a 16-bit phase
  accumulator; the four voices are summed and written to the PWM duty register.
  The 32-entry `Scale[]` and the note decay are unchanged.
* **Touch** — the `ADCTouch` method from `LedAttinyTouchx2.ino`: read the pad's
  capacitance through the ADC, compare against a baseline measured at start-up,
  and debounce on the edge.
* **Songs** — `songs.h`, generated from the `MusicBoxPunchSheets` PDFs by
  `tools/convert_punchsheet.py` (see `Punch Sheet Conversion.md`): six tunes
  (Bluey, Baby Shark, Dynamite, Sugar Plum Fairy, Pachelbel, Super Mario Bros.),
  each punch column becoming one slot with chords for stacked notes. The songs
  are stored **run-length encoded** so all six fit flash; the engine expands each
  record in `advanceNote()`.

### 4.2 Timer / interrupt map

| Peripheral | Role | Configuration |
| ---------- | ---- | ------------- |
| **Timer0** | 20 kHz sample interrupt that mixes the voices → `OCR1A`; also builds the 1 kHz `g_ms` time base and drives the note clock (per-song tempo) | Fast PWM, `TOP=OCR0A=49`, prescaler 8 → `8 MHz / (8·50) = 20 kHz`, `OCIE0A` |
| **Timer1 + PLL** | PWM audio carrier on OC1A (PB1) | `PLLCSR = PLLE/PCKE` (64 MHz), `CS10` (÷1), `PWM1A`, `COM1A=clear-on-match`, `OCR1C=0xFF` (TOP 255) |

> **Note clock.** The note slots are advanced from inside the 20 kHz Timer0
> sample ISR: the same ISR divides 20 kHz down to 1 kHz (`g_ms`), and a second
> divider fires `advanceNote()` every `1000 / tempo` ms, where `tempo` is the
> current song's slots-per-second value (default 8 → 125 ms, 8 slots/s). This
> replaces the original watchdog-timer clock, which did not fire reliably on this
> build (leaving the voices silent and the beat LED dark). Running the note clock
> serial with the mixer also removes any need to lock the `Freq`/`Amp`/`Tune`
> state. `advanceNote()` also decodes the run-length song stream: one
> `[gap][chord]` record per chord, with `gap` rest slots in front.
> **Tempo.** Each song carries its own `SONG_<NAME>_TEMPO` macro in `songs.h`
> (slots/s; `DEFAULT_TEMPO = 8`); `selectSong()` copies it into `tempo`.

**Clock budget.** The engine needs a **20 kHz** sample rate so the `Scale[]`
tuning is exact. At the 8 MHz internal clock used by the touch stack, that is
`TOP = 49` with a ÷8 prescaler (the reference used `TOP = 99` at 16 MHz for the
same 20 kHz). An `#error` guards against building at the wrong clock.

### 4.3 No `millis()`

Re-configuring Timer0 for the sample clock removes the Arduino core's
millisecond time base, so `millis()`/`delay()` must not be used. The sketch
builds its own time base from the sample interrupt:

```cpp
if (++msDiv >= 20) { msDiv = 0; g_ms++; }   // 20 kHz / 20 = 1 kHz
```

`snapMillis()` reads it atomically (interrupts masked for the 32-bit load) and is
used only for the touch debounce gap.

### 4.4 Control behaviour

| Event | Action |
| ----- | ------ |
| **SONG** touched | `songIndex = (last+1) % NUM_SONGS`; load the song **and its tempo**, reset the cursor, start playing, `EEPROM.update` the index |
| **PLAY** touched | toggle `playing`; on pause, zero all voice amplitudes for instant silence |
| song end (RLE record with chord `0x000000FF`) | reset the cursor to `0` → loop the song |
| each beat | the note clock counts `SLOTS_PER_BEAT` slots → `g_beats`; `loop()` blinks the PB0 LED |
| power-up | read `songIndex` from EEPROM (validated), auto-play it |

Song switching pauses the note scheduler, rewrites the `Tune` pointer and
voices with interrupts masked, then resumes — so it can never read a
half-updated pointer.

### 4.5 EEPROM

| Address | Field | Notes |
| :-----: | ----- | ----- |
| 0x00 | magic `0x53` | initialises the block on first boot |
| 0x01 | song index | range-checked against `NUM_SONGS`; written with `EEPROM.update()` |

### 4.6 Beat / touch LED

The note clock (Timer0 time base) counts slots and bumps the 8-bit `g_beats`
counter once every `SLOTS_PER_BEAT` slots. Because one slot is an eighth note,
that is one beat per quarter note, so the LED **blinks at half the song tempo**.
`loop()` polls the counter and lights the LED for one slot per beat (on for one
slot, off for the next → 50% duty):

```cpp
if (g_beats != lastBeat) {                       // a new beat
    lastBeat = g_beats;
    ledOffAt = snapMillis() + (1000 / tempo);    // lit for one slot
    ledOn    = true;
}
```

The physical LED is the OR of the beat and the touch state, so a finger on a pad
is visible and it drops back to blinking on release:

```cpp
bool touchHeld = lastTouch[0] || lastTouch[1];   // pad currently touched
digitalWrite(LED_PIN, (touchHeld || ledOn) ? HIGH : LOW);
```

* One blink per beat: the blink rate is **half the song tempo** (`tempo = 8`
  → 4 blinks/s).
* Touching **either** pad holds the LED solid on while the finger is present;
  on release it resumes blinking.
* The LED stays dark while paused (`playing == false` doesn't advance the
  counter).
* `g_beats` is 8-bit, so the polled read in `loop()` is atomic.
* `LED_PIN` and `SLOTS_PER_BEAT` are `#define`s near the top of the sketch; the
  per-song tempo macros live in `songs.h`.

---

## 5. Build & flash

```bash
# compile
arduino-cli compile --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 Phase1_MusicBox

# upload (replace COMx with your programmer's port)
arduino-cli upload  --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 -p COMx Phase1_MusicBox
```

Required: the **attiny** core (`attiny:avr` 1.0.2, damellis) and the
**ADCTouch** library (`ADCTouch` 1.0.3). Both are already installed in the dev
environment.

If `songs.h` is regenerated, copy/point it into `Phase1_MusicBox/`:

```bash
# from the punch sheets (chords); falls back to the .song for Cant Stop
python tools/convert_punchsheet.py -o Phase1_MusicBox/songs.h
copy Phase1_MusicBox\songs.h songs.h
```

---

## 6. Verification / bring-up

| Step | Check | Expected |
| :--: | ----- | -------- |
| 1 | Compile | `6614 bytes (80%) flash`, `141 bytes (27%) RAM` |
| 2 | Power on | remembered song starts playing immediately |
| 3 | Tap **SONG** | next song starts; order cycles 0→1→2→3→0 |
| 4 | Tap **PLAY** | music pauses; tap again resumes from the same spot |
| 5 | Let a song end | it loops |
| 6 | Power cycle | the last-used song is restored |
| 7 | Touch a pad once firmly | exactly one action (edge + 250 ms gap debounce) |
| 8 | Hold a pad | does **not** repeat |
| 9 | Touch and hold a pad | LED on PB0 is lit while the pad is held (bring-up confirmation that touch is detected) |
| 10 | Watch the beat LED | PB0 blinks at **half the song tempo** (e.g. 4 blinks/s at `tempo = 8`), in time with the tune |
| 11 | Tap **PLAY** to pause | LED stops flashing; resumes in step with the music when re-enabled |
| 12 | Press reset | the remembered song starts playing immediately, with no touch required |

**Touch tuning.** If pads are unresponsive, lower `TOUCH_THRESHOLD`; if they
false-trigger, raise it. Enlarge the pad (≥ 1 cm²) or add 100 nF from the pin
to GND to steady the reading.

---

## 7. Known characteristics & limitations

* **Polyphonic chord arrangements.** The engine is 4-voice and the punch-sheet
  songs use that: a column with several notes becomes a chord (the sheets peak
  at 4 notes). All six songs are punch-sheet arrangements, stored run-length
  encoded to fit flash. See `Punch Sheet Conversion.md`.
* **Per-song tempo.** Each song has a `SONG_<NAME>_TEMPO` macro in `songs.h`
  (slots/second; `DEFAULT_TEMPO = 8` → 0.125 s per slot = 240 BPM), applied by
  `selectSong()`. Edit the macros (or `TEMPO_OVERRIDES` in
  `tools/convert_punchsheet.py`) to retune speed; the beat LED blinks at half
  that tempo.
* **Chiptune timbre.** Square waves with a ~0.8 s decay — bright, toy-like.
* **No volume control in Phase 1.** Output level is fixed by the driver; a volume
  pot is deferred (its former pin, PB4, is now the Phase 2 strip data line).
* **Beat LED is PB0.** Phase 2 keeps this LED and drives the NeoPixel strip from
  the unused **PB4** — see `Phase 2 - LED Music Box.md`.
* **8 MHz required.** Building at another clock changes the sample rate and
  detunes everything; the `#error` catches this.
* **No `delay()`/`millis()`.** Timer0 is repurposed; use `snapMillis()`.

---

## 8. Phase 2 (implemented) — 8-pixel NeoPixel + patterns

Implemented as a standalone sketch in `Phase2_LedMusicBox/`; see
`Phase 2 - LED Music Box.md`. Summary:

* **Strip on PB4** — the pin Phase 1 had reserved for the pot. Because the pot is
  deferred, the strip uses this otherwise-unused pin and **PB0 stays the
  beat/touch LED** (no pin is lost).
* **LED effects are cycled** by the second touch pad (Wave, Chaser, Rainbow,
  Scanner, Solid, Color Wipe) and also advance on a song change; ported
  integer-only from `LedAttinyTouchx2.ino`. The Adafruit NeoPixel library does
  not fit in flash. (Phase 1's PLAY/pause is gone — that pad now cycles effects.)
* **Refresh tied to the tempo** — the strip updates from the note clock (once
  per note slot), so the interrupts-off WS2812 frame (~275 µs) stays bounded.
* Build: `7554 bytes (92%) flash`, `177 bytes RAM`.

---

## 9. Files

| File | Role |
| ---- | ---- |
| `Phase1_MusicBox/Phase1_MusicBox.ino` | Phase 1 firmware |
| `Phase1_MusicBox/songs.h` | generated song data (compiled in) |
| `tools/convert_punchsheet.py` | punch-sheet PDF converter (main path) |
| `tools/convert_song.py` | `.song` melody converter (fallback) |
| `MusicBoxPunchSheets/*.pdf` | punch-sheet sources (K15 / GI20) |
| `songs/*.song` | song source notation |
| `Punch Sheet Conversion.md` | punch-sheet translation reference |
| `Music Conversion.md` | slot format & `.song` conversion reference |
| `FSD.md` | overall functional specification |
