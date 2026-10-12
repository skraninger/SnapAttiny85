Here's the full document. Copy everything between the `---` lines into a file called `attiny85_neopixel_touch.md`, then run:

```bash
pandoc attiny85_neopixel_touch.md -o attiny85_neopixel_touch.pdf --toc
```

Or open it in **Typora** → *File → Export → PDF*.

---

# ATtiny85 NeoPixel + Capacitive Touch

**A single-board LED controller with touch-activated lighting patterns**

---

## Table of Contents

1. [Overview](#1-overview)
2. [Parts & Bill of Materials](#2-parts--bill-of-materials)
3. [Wiring](#3-wiring)
4. [Library Installation](#4-library-installation)
5. [Arduino Sketch](#5-arduino-sketch)
6. [Build & Flash](#6-build--flash)
7. [Lighting Patterns](#7-lighting-patterns)
8. [How the Touch Sensor Works](#8-how-the-touch-sensor-works)
9. [Tuning & Configuration](#9-tuning--configuration)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Overview

This project pairs an **ATtiny85** microcontroller with a **WS2812B NeoPixel** strip and a **capacitive touch pad**. Each touch of the pad cycles through five lighting patterns:

| #    | Pattern | Description                                |
| ---- | ------- | ------------------------------------------ |
| 0    | Solid   | All pixels one colour, slowly rotating hue |
| 1    | Rainbow | Full-spectrum gradient across the strip    |
| 2    | Breathe | Triangle-wave brightness fade in/out       |
| 3    | Chase   | Two bright pixels running around the strip |
| 4    | Twinkle | Pseudo-random per-pixel colour/brightness  |

The entire project fits in the ATtiny85's **8 KB flash** and **512 B SRAM**.

---

## 2. Parts & Bill of Materials

| Qty  | Part                               | Notes                                                    |
| ---- | ---------------------------------- | -------------------------------------------------------- |
| 1    | ATtiny85 (SMD or DIP-8)            | Pre-flashed with 1 MHz internal or 16 MHz external clock |
| 1    | WS2812B NeoPixel strip             | 8 pixels shown; max 16 for this design                   |
| 1    | 470 Ω – 1 kΩ resistor              | Series on data line                                      |
| 1    | Copper tape or bare wire (5–10 cm) | Capacitive touch pad                                     |
| 1    | 5 V / 3 A power supply             | For the NeoPixel strip (separate from MCU)               |
| 1    | 100 µF electrolytic cap            | Across strip VDD/GND                                     |
| 1    | USBasp or USBtinyISP programmer    | For flashing the ATtiny85                                |
| —    | Jumper wires, breadboard or PCB    |                                                          |

---

## 3. Wiring

```
  ATtiny85 (DIP-8)              NeoPixel Strip
  ┌─────────────┐
  │ 8  VCC  1  │── 5V ──────────► VDD
  │ 7  PB3  2  │
  │ 6  PB4  3  │
  │ 5  GND  4  │── GND ─────────► GND
  │            │
  │ 4  GND  5  │
  │ 3  PB4  6  │
  │ 2  PB3  7  │
  │ 1  VCC  8  │
  └─────────────┘

  Pin 0 (PB0) ──► [470 Ω] ──► DIN
  Pin 1 (PB1) ──► Touch pad (copper tape / bare wire)

  ⚠  Shared GND between MCU and strip supply is REQUIRED.
  ⚠  Keep the touch pad wire away from the NeoPixel data line.
```

**Pin summary:**

| ATtiny85 Pin | Function         | Direction |
| :----------: | ---------------- | :-------: |
| Pin 0 / PB0  | NeoPixel DIN     |  Output   |
| Pin 1 / PB1  | Capacitive touch |   Input   |
| Pin 4 / GND  | Ground           |     —     |
| Pin 8 / VCC  | 5 V (MCU only)   |     —     |

---

## 4. Library Installation

1. **Arduino IDE** → *Tools → Board → Boards Manager* → install **Arduino AVR Boards** (usually pre-installed).
2. *Sketch → Include Library → Manage Libraries* → search **"Adafruit NeoPixel"** → **Install**.
3. For the ATtiny85-specific timing patch, clone the fork:

   ```bash
   git clone https://github.com/adafruit/Adafruit_NeoPixel_ATTiny85.git \
     ~/Arduino/libraries/Adafruit_NeoPixel_ATTiny85
   ```

   > If you use the standard `Adafruit_NeoPixel` library and target the ATtiny85 board, it works for ≤ 16 pixels. The fork simply tightens the `show()` loop for reliability at low clock speeds.

4. Select the board:
   - **Tools → Board → ATtiny → ATtiny85**
   - **Tools → Clock → 1 MHz (Internal)** *(or 16 MHz (External) if you have a crystal on PB4/PB3)*
   - **Tools → Port →** *(your programmer)*

---

## 5. Arduino Sketch

Save as `touch_npx.ino`.

```cpp
/*
 * ATtiny85 – NeoPixel + Capacitive Touch
 * Uses Adafruit_NeoPixel (ATtiny85 variant)
 *
 * Pin map:
 *   Pin 0 (PB0)  NeoPixel data out
 *   Pin 1 (PB1)  Capacitive touch pad
 *
 * Touch → cycle lighting pattern
 *
 * Board:    ATtiny85
 * Clock:    1 MHz (internal) or 16 MHz (external)
 * Max pixels: 16  (48 B buffer, well within 512 B SRAM)
 */

#include <Adafruit_NeoPixel.h>

// ─── Configuration ────────────────────────────────────────────────

#define NUM_PIXELS       8        // match your strip (max 16)
#define NEO_PIN          0        // PB0
#define TOUCH_PIN        1        // PB1

#define TOUCH_THRESHOLD  300      // tune: higher = less sensitive
                                 // (at 1 MHz counts are higher than at 16 MHz)
#define FRAME_DELAY_US   33000    // ~30 fps

// ─── Globals ──────────────────────────────────────────────────────

Adafruit_NeoPixel strip(NUM_PIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);

uint8_t  pattern = 0;
uint8_t  hue     = 0;
uint16_t frame   = 0;

// ─── HSV → RGB (8-bit, no floats) ─────────────────────────────────

void hsvToRGB(uint8_t h, uint8_t s, uint8_t v,
              uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = *g = *b = v; return; }

    uint8_t region, rem, p, q, t;

    h *= 3;                        // 0..511
    region = h / 84;               // 0..5
    rem    = (h - region * 84) * 4;// 0..83

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * rem)        >> 8))) >> 8;
    t = (v * (255 - ((s * (84 - rem)) >> 8))) >> 8;

    switch (region) {
        case 0:  *r=v; *g=t; *b=p; break;
        case 1:  *r=q; *g=v; *b=p; break;
        case 2:  *r=p; *g=v; *b=t; break;
        case 3:  *r=p; *g=q; *b=v; break;
        case 4:  *r=t; *g=p; *b=v; break;
        default: *r=v; *g=p; *b=q; break;
    }
}

// ─── Lighting patterns ────────────────────────────────────────────

void patternSolid()
{
    uint8_t r, g, b;
    hue = (hue + 1) & 0xFF;
    hsvToRGB(hue, 255, 180, &r, &g, &b);
    strip.fill(strip.Color(r, g, b));
}

void patternRainbow()
{
    uint8_t r, g, b, h;
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        h = (hue + i * (255 / NUM_PIXELS)) & 0xFF;
        hsvToRGB(h, 255, 220, &r, &g, &b);
        strip.setPixelColor(i, strip.Color(r, g, b));
    }
    hue = (hue + 3) & 0xFF;
}

void patternBreathe()
{
    uint8_t r, g, b, v;
    // triangle wave 0→255→0 over 128 frames
    v = (frame < 64) ? (frame * 4) : (255 - (frame - 64) * 4);
    hsvToRGB(hue, 200, v, &r, &g, &b);
    strip.fill(strip.Color(r, g, b));
    hue = (hue + 1) & 0xFF;
}

void patternChase()
{
    uint8_t r, g, b, offset;
    offset = frame % NUM_PIXELS;
    for (uint8_t i = 0; i < NUM_PIXELS; i++) {
        if (i == offset || i == (offset + 1) % NUM_PIXELS) {
            hsvToRGB(hue, 255, 255, &r, &g, &b);
        } else {
            hsvToRGB(hue, 255, 15, &r, &g, &b);
        }
        strip.setPixelColor(i, strip.Color(r, g, b));
    }
    hue = (hue + 2) & 0xFF;
}

void patternTwinkle()
{
    // re-randomise every 30 frames
    if (frame % 30 == 0) {
        for (uint8_t i = 0; i < NUM_PIXELS; i++) {
            frame = (frame * 7 + i * 13 + 1) & 0xFFFF;
            uint8_t r, g, b;
            hsvToRGB((i * 37 + frame) & 0xFF, 220,
                     (frame >> 4) & 0xFF, &r, &g, &b);
            strip.setPixelColor(i, strip.Color(r, g, b));
        }
    }
}

// ─── Capacitive touch (RC charge-time) ────────────────────────────
// 1. Discharge  → OUTPUT LOW
// 2. Charge     → INPUT + internal pull-up (~20 kΩ)
// 3. Count tight loops until pin reads HIGH.
//    Finger adds ~10 pF → slower charge → higher count.

uint16_t touchRead()
{
    uint16_t count = 0;

    // discharge
    pinMode(TOUCH_PIN, OUTPUT);
    digitalWrite(TOUCH_PIN, LOW);
    delayMicroseconds(5);

    // charge via internal pull-up
    pinMode(TOUCH_PIN, INPUT);
    digitalWrite(TOUCH_PIN, HIGH);   // enable pull-up

    while (digitalRead(TOUCH_PIN) == LOW) {
        if (++count > 5000) break;   // safety timeout
    }

    // restore: input, pull-up off
    digitalWrite(TOUCH_PIN, LOW);
    pinMode(TOUCH_PIN, INPUT);

    return count;
}

// ─── Setup / Loop ─────────────────────────────────────────────────

void setup()
{
    pinMode(TOUCH_PIN, INPUT);
    digitalWrite(TOUCH_PIN, LOW);

    strip.begin();
    strip.setBrightness(60);   // 0-255, keep low to limit power
    strip.clear();
    strip.show();
}

void loop()
{
    // ── Touch (debounced) ──
    static bool touchWasLow = true;
    static uint32_t lastTouch = 0;

    if (touchRead() > TOUCH_THRESHOLD) {
        if (touchWasLow && (millis() - lastTouch > 300)) {
            touchWasLow = false;
            lastTouch   = millis();

            // advance pattern
            pattern = (pattern + 1) % 5;
            hue     = 0;
            frame   = 0;
            strip.clear();
            strip.show();

            // brief hold so the user doesn't double-trigger
            delay(150);
        }
    } else {
        touchWasLow = true;
    }

    // ── Render current pattern ──
    switch (pattern) {
        case 0: patternSolid();   break;
        case 1: patternRainbow(); break;
        case 2: patternBreathe(); break;
        case 3: patternChase();   break;
        case 4: patternTwinkle(); break;
    }

    strip.show();
    frame++;
    delayMicroseconds(FRAME_DELAY_US);
}
```

---

## 6. Build & Flash

### Arduino IDE (GUI)

1. Open `touch_npx.ino`.
2. **Tools → Board → ATtiny85**
3. **Tools → Clock → 1 MHz (Internal)** *(or 16 MHz External)*
4. **Tools → Port →** *(USBasp / USBtinyISP / etc.)*
5. Click **Upload**.

### Command line (avrdude)

```bash
# Compile (if not using Arduino IDE)
avr-gcc -mmcu=attiny85 -DF_CPU=1000000UL -Os -Wall \
        -I ~/Arduino/libraries/Adafruit_NeoPixel_ATTiny85 \
        -o touch_npx.elf touch_npx.c

# Generate hex
avr-objcopy -O ihex -j .text -j .data touch_npx.elf touch_npx.hex

# Flash
avrdude -c usbasp -p t85 -U flash:w:touch_npx.hex
```

> If using the Arduino IDE, it handles all of the above automatically.

---

## 7. Lighting Patterns

| Index | Name        | Behaviour                                                    |
| :---: | ----------- | ------------------------------------------------------------ |
|   0   | **Solid**   | All pixels the same colour; hue rotates 1 step/frame         |
|   1   | **Rainbow** | 256° hue spread across the strip; whole gradient scrolls     |
|   2   | **Breathe** | Brightness follows a triangle wave (0 → 255 → 0) over ~4 s; hue drifts |
|   3   | **Chase**   | Two adjacent bright pixels "run" around the ring; dim trail  |
|   4   | **Twinkle** | Every 30 frames each pixel gets a new pseudo-random colour & brightness |

Each touch advances to the next pattern (wrapping after 4 → 0).

---

## 8. How the Touch Sensor Works

No dedicated capacitive-touch IC is needed. The ATtiny85's **internal pull-up resistor** (~20 kΩ) and the **stray capacitance** of the pad form a simple RC circuit.

```
        Internal pull-up (~20 kΩ)
   VCC ──┤├──┬──► PB1 (read)
         │  │
         │  C_pad  (~5 pF bare, ~15 pF with finger)
         │  │
        GND
```

**Procedure (each sample):**

1. **Discharge** – Drive PB1 as output LOW for ~5 µs.
2. **Charge** – Switch PB1 to input with pull-up enabled.
3. **Count** – Loop counting cycles until PB1 reads HIGH.

| Condition     | Pad capacitance | Charge time (1 MHz) | Count (approx.) |
| ------------- | :-------------: | :-----------------: | :-------------: |
| No finger     |      ~5 pF      |      ~0.02 µs       |      ~200       |
| Finger on pad |     ~15 pF      |      ~0.06 µs       |      ~600       |

The `TOUCH_THRESHOLD` (default 300) sits between these two values. A larger pad or longer wire increases the baseline capacitance and widens the gap, improving reliability.

---

## 9. Tuning & Configuration

All user-facing settings are at the top of the sketch:

| Parameter               | Default | Range / Notes                                                |
| ----------------------- | :-----: | ------------------------------------------------------------ |
| `NUM_PIXELS`            |    8    | 1 – 16 (SRAM limit)                                          |
| `NEO_PIN`               |    0    | Must be PB0 (pin 1) for this wiring                          |
| `TOUCH_PIN`             |    1    | Must be PB1 (pin 2) for this wiring                          |
| `TOUCH_THRESHOLD`       |   300   | Raise if false-triggering; lower if unresponsive. At 16 MHz use ~400. |
| `FRAME_DELAY_US`        |  33000  | 33 000 µs ≈ 30 fps. Lower = faster animation.                |
| `strip.setBrightness()` |   60    | 0 – 255. Keep ≤ 80 for 8 px on a 1 A supply.                 |

### Power budget (8 × WS2812B)

| Colour          | Per pixel | 8 pixels |
| --------------- | :-------: | :------: |
| Full white      |   60 mA   |  480 mA  |
| Full red        |   20 mA   |  160 mA  |
| Full green      |   15 mA   |  120 mA  |
| Full blue       |   4 mA    |  32 mA   |
| Mixed (typical) |  ~20 mA   | ~160 mA  |

Use a **5 V / 3 A** supply for the strip. The ATtiny85 itself draws < 20 mA.

---

## 10. Troubleshooting

| Symptom                            | Likely cause                                           | Fix                                                          |
| ---------------------------------- | ------------------------------------------------------ | ------------------------------------------------------------ |
| Strip is dark, no pixels light     | Wrong clock speed in IDE vs. actual crystal            | Match **Tools → Clock** to your hardware.                    |
| First pixel garbled / flickers     | No series resistor or long data wire                   | Add 470 Ω between PB0 and DIN; shorten wire.                 |
| Touch never triggers               | Threshold too high or pad too small                    | Lower `TOUCH_THRESHOLD`; enlarge pad to ≥ 1 cm².             |
| Touch triggers randomly            | Threshold too low; EMI from NeoPixel data line         | Raise `TOUCH_THRESHOLD`; route touch wire away from data wire; add 100 nF cap on touch pin to GND. |
| Strip resets / drops out mid-frame | Insufficient power or no bulk cap                      | Use a 3 A 5 V supply; add 100 µF electrolytic across VDD/GND at the strip. |
| `show()` timing is off at 1 MHz    | Using standard Adafruit_NeoPixel without the t85 patch | Install the **Adafruit_NeoPixel_ATTiny85** fork.             |
| Upload fails                       | Fuse / bootloader mismatch                             | Verify fuses: `avrdude -c usbasp -p t85 -U lfuse:w:0x62:m -U hfuse:w:0x9F:m -U efuse:w:0xFF:m` (1 MHz internal). |

---

## License

This sketch is provided under the **MIT License**. Use it, modify it, ship it — no attribution required, but a link back is appreciated.

---

*Document generated for the ATtiny85 + NeoPixel + Capacitive Touch project.*