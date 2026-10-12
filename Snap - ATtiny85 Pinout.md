# Snap — ATtiny85 Pinout & Wiring (one-page reference)

**MCU:** ATtiny85 (DIP-8) · **Core:** `attiny:avr` (damellis) · **Clock:** 8 MHz internal (`clock=internal8`)
**Applies to:** Phase 2 — the full build (`Phase2_LedMusicBox`: music + LED strip). Phase-1-only notes at the bottom.

## Pin table

| Phys # | Port | Arduino D | Arduino A | Function | Connects to |
| :----: | :--: | :-------: | :-------: | -------- | ----------- |
| 1 | PB5 | D5 | A0 | RESET / ISP | ISP RESET line (programming only) |
| 2 | PB3 | D3 | A3 | Touch pad 2 — **EFFECT** (ADC3) | capacitive pad: copper tape / bare wire ≥ 1 cm² |
| 3 | PB4 | D4 | A2 | **NeoPixel data out** (WS2812B) | strip DIN through **470 Ω** |
| 4 | GND | — | — | Ground | common ground (supply, speaker driver, strip) |
| 5 | PB0 | D0 | — | Beat / touch **LED** | LED anode + **330 Ω** → GND (active HIGH) |
| 6 | PB1 | D1 | — | **Audio PWM** out (OC1A) | 1 kΩ → NPN base; speaker +5 V ↔ collector; 10–47 µF series |
| 7 | PB2 | D2 | A1 | Touch pad 1 — **SONG** (ADC1) | capacitive pad: copper tape / bare wire ≥ 1 cm² |
| 8 | VCC | — | — | +5 V supply | 5 V; 100 nF + 100 µF decoupling nearby |

DIP-8, top view (dot / dimple = pin 1):

```
        +-----------+
 PB5 ---|1         8|--- VCC   (5 V)
 PB3 ---|2         7|--- PB2   (TOUCH SONG)
 PB4 ---|3         6|--- PB1   (AUDIO PWM)
 GND ---|4         5|--- PB0   (LED)
        +-----------+
```

## Wiring notes

- **Audio:** `PB1 → 1 kΩ → 2N3904 base`; speaker between +5 V and collector;
  10–47 µF in series with the speaker (DC block). A piezo can be driven directly
  through ~100 Ω instead.
- **LED strip:** `PB4 → 470 Ω → strip DIN`; strip +5 V / GND with a **100 µF**
  bulk capacitor. Firmware caps brightness (`NP_BRIGHTNESS`); use a **≥ 1 A** 5 V
  supply for 8 pixels.
- **Touch pads:** PB2 = **SONG**, PB3 = **EFFECT**. Keep pad wiring away from the
  strip data line; a 100 nF pin→GND capacitor steadies weak pads.
- **Power:** 5 V (USB power bank or 3×AA + boost). ATtiny85 max 5.5 V.
- **ISP programming:** RESET = pin 1, MOSI = PB0 (pin 5), MISO = PB1 (pin 6),
  SCK = PB2 (pin 7). PB0/PB1/PB2 are shared with the LED / audio / touch — unplug
  those loads while flashing, or accept the side effects.

## Phase 1 (music only, no strip)

- PB0 = beat LED · PB1 = audio · PB2 = SONG · PB3 = **PLAY/PAUSE** · PB4 = unused
  (reserved for a pot).
- In Phase 2, **PB3 became the EFFECT pad** (cycles LED effects) and **PB4 drives
  the strip**; the pot is deferred.
