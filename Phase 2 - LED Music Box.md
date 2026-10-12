# Phase 2 — LED Music Box (8-pixel NeoPixel + patterns)

**Status:** implemented · **Build:** `7372 bytes flash (89%)`, `175 bytes RAM (34%)`
**Sketch:** `Phase2_LedMusicBox/Phase2_LedMusicBox.ino` + `Phase2_LedMusicBox/songs.h`

Phase 2 = **Phase 1 plus an 8-pixel WS2812B ("NeoPixel") strip**, with **one LED
pattern per song**. It is a standalone sketch so Phase 1 stays untouched; the
music engine, touch stack, per-song tempo and RLE song format are copied
verbatim from Phase 1.

---

## 1. Pin map (updated)

| ATtiny85 pin | Port | Phase 2 function | Notes |
| :----------: | :--: | ---------------- | ----- |
| 1 | PB5 | RESET (ISP) | reserved — cannot be repurposed without HV programming |
| 2 | PB3 | Touch 2 — PLAY/PAUSE | ADC3 → `ADCTouch` channel 3 |
| 3 | **PB4** | **NeoPixel data out** | WS2812B DIN through ~470 Ω; **was the reserved pot pin** |
| 4 | GND | Ground | common with LED/audio supply |
| 5 | PB0 | Beat / touch LED | plain LED (active HIGH) — kept from Phase 1 |
| 6 | PB1 | Audio PWM (OC1A) | Timer1 + 64 MHz PLL |
| 7 | PB2 | Touch 1 — SONG | ADC1 → `ADCTouch` channel 1 |
| 8 | VCC | 5 V | 100 nF + 100 µF; add 100 µF at the strip |

### Why the strip is on PB4 (the "unused pin" rule)

The audio engine **must** use PB1 (OC1A/Timer1 PLL), the two touch pads **must**
use PB2/PB3 (the only free ADC pins), and PB5 is RESET. That leaves **PB0 and
PB4**.

Phase 1 had reserved **PB4 for a potentiometer**, but the pot is **deferred**, so
PB4 is genuinely unused. Phase 2 therefore moves the strip onto the **unused
PB4** and **keeps PB0 as the beat/touch LED** — i.e. the preferred outcome
rather than repurposing the flashing-LED pin.

> **Consequence for the deferred pot:** PB0 and PB1 are **not** ADC-capable, so
> the pot can never live on those. Re-adding the pot later means either sharing
> PB4 with the strip (not possible) or freeing PB5 (disable RESET / HV
> programming). This is recorded as an open item; it is why the pot was left for
> a separate "Phase 2b" decision.

---

## 2. What is reused from Phase 1 (unchanged)

- **Music engine** — 4-voice DDS + PWM carrier (PB1), 20 kHz Timer0 sample ISR.
- **Songs** — `songs.h` (RLE, one record per chord), `NUM_SONGS = 6`.
- **Per-song tempo** — `SONG_<NAME>_TEMPO` applied by `selectSong()`.
- **Touch** — `ADCTouch` + baseline calibration + edge/`TOUCH_GAP_MS` debounce.
- **EEPROM** — remembers the song, auto-plays at reset.

---

## 3. Controls

| Event | Action |
| ----- | ------ |
| **SONG** touched | next song **and** its pattern; restart; remember in EEPROM |
| **PLAY** touched | toggle play / pause |
| **LED (PB0)** | lit while a pad is held; flashes once per beat while playing |
| **Strip (PB4)** | renders the current song's pattern |

---

## 4. Patterns — one per song

Ported (integer-only) from `LedAttinyTouchx2.ino`; the Adafruit library is **not**
used (it would not fit — see §6). `renderPattern()` selects by `songIndex`.

| # | Song | Pattern | Behaviour |
|:-:|------|---------|-----------|
| 0 | Bluey Theme | **Breathe** | triangle fade on a cool colour |
| 1 | Baby Shark | **Chaser** | one green pixel runs around the strip |
| 2 | Dynamite | **Rainbow** | full-spectrum gradient scrolling |
| 3 | Dance of the Sugar Plum Fairy | **Scanner** | one pixel bounces end to end, colour changes per sweep |
| 4 | Pachelbel's Canon | **Solid** | all pixels one colour, hue drifting slowly |
| 5 | Super Mario Brothers | **Color Wipe** | strip fills one pixel at a time, new colour per pass |

`Wave` and `Firefly` were intentionally left out: `Wave` uses `float`/`sin()`
(pulls in ~1 KB of float code) and both are the heaviest patterns.

---

## 5. LED refresh is tied to the song tempo

WS2812 bit-banging must hold the CPU (interrupts off) for the whole frame. At
8 pixels that is **~275 µs**; the audio sample ISR runs every 50 µs, so a naive
frame steals ~5 audio samples (a short click).

To keep those stalls **rare and masked**, the strip is refreshed **once per
beat** — the same `g_beats` tick that flashes PB0:

```cpp
if (g_beats != lastBeat) {                 // one beat (SLOTS_PER_BEAT slots)
    lastBeat = g_beats;
    renderPattern();                       // advance the pattern one frame
    wsShow();                              // ~275 us with interrupts off
}
```

- Refresh rate = `tempo / SLOTS_PER_BEAT` = **4 fps** at the default tempo 8.
- Stalls land on **note onsets**, where the note attack masks the artifact.
- The strip holds its last frame while paused (no beats ⇒ no refresh).

If a click is ever objectionable, the next step is to interleave a few WS2812
bits inside the audio ISR (zero-stall), but that adds code and complexity.

---

## 6. WS2812 driver (hand-rolled, no library)

`wsSendByte()` is a cycle-counted inline-asm loop. Verified from the build's
disassembly (8 MHz → 1 cycle = 125 ns):

| Bit | high time | total period | WS2812B spec |
| :-: | :-------: | :----------: | :----------- |
| `0` | 3 cyc = 0.375 µs | 11 cyc = 1.375 µs | 0.4 ± 0.15 / 1.25 ± 0.6 µs |
| `1` | 6 cyc = 0.75 µs  | 12 cyc = 1.5 µs   | 0.8 ± 0.15 / 1.25 ± 0.6 µs |

Both are inside the part's tolerances. The frame sends `NUM_LEDS × 24` bits,
MSB-first, GRB order; `setPx()` applies `NP_BRIGHTNESS` (default 48) so 8 pixels
stay within a small supply's budget.

---

## 7. Measured memory

| | Phase 1 | **Phase 2** |
|---|---:|---:|
| Flash | 6614 B (80%) | **7372 B (89%)** |
| RAM | 141 B (27%) | **175 B (34%)** |
| — of which strip pixel buffer | 0 | 24 B (`8 × 3`) |

Fits with **~820 B of flash headroom** and ~337 B of RAM. Phase 2 did **not**
use `Adafruit_NeoPixel` (~1.5 KB) — with it, driver + patterns would have
overflowed. A 7th/8th pattern with a separate selector is possible only if the
heavy patterns stay out or a song is shortened.

---

## 8. Build & verify

```bash
arduino-cli compile --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 Phase2_LedMusicBox
arduino-cli upload  --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 -p COMx Phase2_LedMusicBox
```

Verification:

| Step | Check | Expected |
| :--: | ----- | -------- |
| 1 | Compile | `7372 bytes (89%) flash`, `175 bytes (34%) RAM` |
| 2 | Power on | remembered song plays; its pattern shows immediately |
| 3 | Tap **SONG** | song and pattern advance together (0…5→0) |
| 4 | Tap **PLAY** | music pauses; strip holds its frame; PB0 stops flashing |
| 5 | Watch PB0 | one flash per beat; lit while a pad is held |
| 6 | Watch the strip | pattern animates at ~4 fps (one step per beat) |
| 7 | Listen | music intelligible; at most a faint tick per beat-led refresh |

---

## 9. Files

| File | Role |
| ---- | ---- |
| `Phase2_LedMusicBox/Phase2_LedMusicBox.ino` | Phase 2 firmware |
| `Phase2_LedMusicBox/songs.h` | generated song data (copy of the Phase 1 header) |
| `Phase 2 - LED Music Box.md` | this document |
| `Phase 1 - Music Box.md` | Phase 1 (music only) |
| `LedAttinyTouchx2.ino` | source of the touch stack and the patterns |
| `tools/convert_punchsheet.py` | song generator (shared with Phase 1) |
