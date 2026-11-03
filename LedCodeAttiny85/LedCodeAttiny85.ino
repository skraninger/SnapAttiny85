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

#define NUM_PIXELS       4       // match your strip (max 16)
#define NEO_PIN          0        // PB0
#define TOUCH_PIN        2        // PB2

const int touchThreshold = 1000;
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

// Function to read capacitance on PB2 (A1) without a library
int readCapacitivePin() {
  // 1. Charge the internal capacitor by setting PB2 high
  ADMUX = (1 << MUX0); // Select ADC1 (PB2)
  pinMode(TOUCH_PIN, OUTPUT);
  digitalWrite(TOUCH_PIN, HIGH);
  delayMicroseconds(10);

  // 2. Set PB2 as input to isolate it
  pinMode(TOUCH_PIN, INPUT);
  
  // 3. Start ADC conversion to read how much charge remains
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC)); // Wait for conversion to finish
  
  int result = ADC;
  
  // 4. Discharge the pin to reset it for the next cycle
  pinMode(TOUCH_PIN, OUTPUT);
  digitalWrite(TOUCH_PIN, LOW);

  //don't need to delay, frame delay provides enough delay
  //delayMicroseconds(10);
  
  return result;
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
    static uint32_t touchWasLow = 0;
    static uint32_t lastTouch = 0;

    //if (touchRead() > TOUCH_THRESHOLD) {
    if (readCapacitivePin() > touchThreshold) {
        if (touchWasLow > 3) {
            touchWasLow = 0;

            // advance pattern
            pattern = (pattern + 1) % 5;
            hue     = 0;
            frame   = 0;
            strip.clear();
            strip.show();

        }
    } else {
        touchWasLow = touchWasLow+1;
    }

    // ── Render current pattern ──
    if ((frame % 90) == 0) {
        switch (pattern) {
            case 0: patternSolid();   break;
            case 1: patternRainbow(); break;
            case 2: patternBreathe(); break;
            case 3: patternChase();   break;
            case 4: patternTwinkle(); break;
        }
    }

    strip.show();
    frame++;
    delayMicroseconds(FRAME_DELAY_US);
}