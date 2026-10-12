/*
 LedAttinyTouchx2 - 30-px WS2812B lamp, dual capacitive touch pads (color + pattern)
 Build: arduino-cli compile --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 <this-folder>
 FQBN options are comma-separated; ATtiny85 = 8 KB flash. Use clock=external8 if the
 board has an external 8 MHz crystal (the default 1 MHz clock breaks Adafruit_NeoPixel).
 */

#include <Adafruit_NeoPixel.h>
#include <ADCTouch.h>
#include <EEPROM.h> // Built-in microcontroller library for non-volatile storage

#define LED_PIN 0 // Data line for WS2812B strip
#define NUM_LEDS 30 // Adjust to your strip length
#define TOUCH_COLOR 2 // Input pin for Color Touch Pad
#define TOUCH_MODE 1 // Input pin for Pattern Touch Pad
#define TOUCH_THRESH 40   // delta above baseline; adjust for pad size
#define TOUCH_DELAY 500
#define CTR_THRESH 16 // Firefly: animation frames per pixel

// EEPROM Storage Addresses
#define ADDR_COLOR 0 // Memory slot for Color index (low byte)
#define ADDR_COLOR_HI 2 // Memory slot for Color index (high byte, white flag)
#define ADDR_PATTERN 1 // Memory slot for Pattern mode

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Baseline for touch
unsigned long mark;
int baseline = 0;

// State Trackers
uint16_t colorIndex = 0; // 0-255 wheel position, 256 (maxColors-1) = white
uint8_t patternMode = 0;
const uint8_t maxPatterns = 8;
const uint16_t maxColors = 257; // 256 wheel colours + white

// Simple Debouncing Flags
bool lastTouchColor = LOW;
bool lastTouchMode = LOW;

// EEPROM Commit Timer Variables (Prevents burnout during rapid tapping)
unsigned long lastInteractionTime = 0;
bool memoryNeedsUpdate = false;
const unsigned long writeDelay = 5000; // Wait 5 seconds of inactivity before saving

/* check button state */
boolean chkTouch(int touchVal) {
   int touchDelta = touchVal - baseline;
   if (touchDelta > TOUCH_THRESH && (millis() - mark) > TOUCH_DELAY) {
       mark = millis();
       return true;
    } 
    else { return false; }
}


void setup() {

// Set ADC prescaler to 64 for a stable 125kHz clock at 8MHz
ADCSRA |= (1 << ADPS2) | (1 << ADPS1);
ADCSRA &= ~(1 << ADPS0);

// Calibrate baseline over 100 readings
long sum = 0;
for (int i = 0; i < 100; i++) {
    sum += ADCTouch.read(TOUCH_COLOR, 10);
    delay(5);
}
baseline = sum / 100;

// 1. Read stored settings from EEPROM on boot up
uint8_t colorHi = EEPROM.read(ADDR_COLOR_HI);
if (colorHi == 0xFF) { colorHi = 0; } // high byte unwritten by older firmware
colorIndex = ((uint16_t)colorHi << 8) | EEPROM.read(ADDR_COLOR);
patternMode = EEPROM.read(ADDR_PATTERN);

// Safety boundaries: If EEPROM is brand new, it reads 255. Reset to 0 if out of range.
if (patternMode >= maxPatterns) {
patternMode = 0;
}
if (colorIndex >= maxColors) {
colorIndex = 0;
}

strip.begin();
strip.show();
//pinMode(TOUCH_COLOR, INPUT);
//pinMode(TOUCH_MODE, INPUT);
}

void loop() {
    // Read touch sensors (assuming digital active-HIGH signals like TTP223)
    bool touchColor = chkTouch(ADCTouch.read(TOUCH_COLOR, 10));
    bool touchMode = chkTouch(ADCTouch.read(TOUCH_MODE, 10));

    // 2. Check Color Button
    if (touchColor == HIGH && lastTouchColor == LOW) {
        colorIndex += 15; // Cycle forward around the wheel + white
        if (colorIndex >= maxColors) { colorIndex -= maxColors; }
        lastInteractionTime = millis();
        memoryNeedsUpdate = true;
        delay(50); // Hardware debounce limit
    }
    lastTouchColor = touchColor;

    // 3. Check Pattern Button
    if (touchMode == HIGH && lastTouchMode == LOW) {
        patternMode = (patternMode + 1) % maxPatterns;
        strip.clear(); // Flush layout memory before pattern change
        lastInteractionTime = millis();
        memoryNeedsUpdate = true;
        delay(50);
    }
    lastTouchMode = touchMode;

    // 4. Smart EEPROM Write Wear Protection
    // Only writes if a value changed AND the user hasn't touched a button for 2 seconds
    if (memoryNeedsUpdate && (millis() - lastInteractionTime >= writeDelay)) {
        EEPROM.update(ADDR_COLOR, (uint8_t)(colorIndex & 0xFF)); // update() skips writing if data is identical
        EEPROM.update(ADDR_COLOR_HI, (uint8_t)(colorIndex >> 8));
        EEPROM.update(ADDR_PATTERN, patternMode);
        memoryNeedsUpdate = false; // Reset update flag
    }

    // 5. Render Current Pattern State
    switch (patternMode) {
        case 0: // Solid Static Color
        setSolidColor(baseColor());
        break;
        case 1: // Breathing Effect
        breatheEffect();
        break;
        case 2: // Chaser / Marquee
        chaserEffect(baseColor());
        break;
        case 3: // Rainbow Overwrite (Overrides base color selection)
        rainbowCycle(20);
        break;
        case 4: // Wave - hue oscillates between 200 and 240 (Overrides base color selection)
        waveyFrame();
        break;
        case 5: // Scanner - one pixel bounces end to end through 3 colours (Overrides base color selection)
        scannerFrame();
        break;
        case 6: // Rainbow Firefly - one random pixel at a time (Overrides base color selection)
        colorFirefly();
        break;
        case 7: // Color Wipe - random RGB, new colour each pass (Overrides base color selection)
        colorWipeFrame();
        break;
    }
}

// Global Color Wheel Generator (0 - 255 Hue Range)
uint32_t colorWheel(byte WheelPos) {
    WheelPos = 255 - WheelPos;
    if(WheelPos < 85) {
        return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    }
    if(WheelPos < 170) {
        WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
    }
        WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

// Base colour for the current colorIndex. Positions 0-255 come from the wheel;
// index 256 (maxColors - 1) is white, which sits one step past the wheel.
uint32_t baseColor() {
    if (colorIndex == maxColors - 1) {
        return strip.Color(255, 255, 255); // White
    }
    return colorWheel((byte)colorIndex);
}

void setSolidColor(uint32_t c) {
    for(int i=0; i<strip.numPixels(); i++) {
        strip.setPixelColor(i, c);
    }
    strip.show();
}

// Chaser with a number of pixels
void chaserEffect(uint32_t c) {
    static int currentPixel = 0;
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 60) {
        strip.setPixelColor(currentPixel, strip.Color(0,0,0)); // Clear previous
        currentPixel = (currentPixel + 1) % strip.numPixels();
        strip.setPixelColor(currentPixel, c);
        strip.show();
        lastUpdate = millis();
    }
}

void breatheEffect() {
    static int brightness = 0;
    static int fadeAmount = 2;
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 15) {
        brightness += fadeAmount;
        if (brightness <= 0 || brightness >= 150) { fadeAmount = -fadeAmount; }
        strip.setBrightness(constrain(brightness, 0, 255));
        setSolidColor(baseColor());
        lastUpdate = millis();
    }
}

void rainbowCycle(uint8_t wait) {
    static uint16_t j = 0;
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > wait) {
        j = (j + 1) % 256;
        for(uint16_t i=0; i<strip.numPixels(); i++) {
            strip.setPixelColor(i, colorWheel(((i * 256 / strip.numPixels()) + j) & 255));
        }
        strip.setBrightness(255);
        strip.show();
        lastUpdate = millis();
    }
}

// Wave - hue oscillates between 200 and 240 with a phase offset per pixel
void waveyFrame() {
    static float waveIn = 0.0;
    static unsigned long lastUpdate = 0;
    uint8_t rgb[3];
    if (millis() - lastUpdate > 0) {
        int diff = 240 - 200;
        for(uint16_t i=0; i<strip.numPixels(); i++) {
            float out = sin(waveIn + i * (6.283 / strip.numPixels())) * diff + 200;
            HSVtoRGB(out, 255, 255, rgb);
            strip.setPixelColor(i, rgb[0], rgb[1], rgb[2]);
        }
        waveIn += 0.06;
        if (waveIn >= 6.283) { waveIn = 0; }
        strip.setBrightness(255);
        strip.show();
        lastUpdate = millis();
    }
}

// Scanner - one pixel bounces end to end; when it returns to the start
// the colour advances through three sweep colours
void scannerFrame() {
    static uint8_t scanPos = 0;
    static bool scanFwd = true;
    static uint8_t scanColor = 0;
    static unsigned long lastUpdate = 0;
    static uint32_t scanColors[3] = { strip.Color(255, 0, 0),
                                      strip.Color(200, 0, 100),
                                      strip.Color(64, 0, 200) };
    if (millis() - lastUpdate > 50) {
        strip.clear();
        strip.setPixelColor(scanPos, scanColors[scanColor]);

        if (scanFwd) {
            if (scanPos >= strip.numPixels() - 1) {
                scanFwd = false;
            } else {
                scanPos++;
            }
        } else {
            if (scanPos == 0) {
                scanColor = (scanColor + 1) % 3;
                scanFwd = true;
            } else {
                scanPos--;
            }
        }
        strip.setBrightness(255);
        strip.show();
        lastUpdate = millis();
    }
}

// Rainbow Firefly - one random pixel pulses through the rainbow, then the
// strip clears and a new pixel takes over
void colorFirefly() {
    static uint8_t myPix = 0;
    static uint8_t lastPix = 0;
    static uint8_t counter = 0;
    static uint8_t frameDelay = 60;
    static unsigned long lastUpdate = 0;
    uint8_t rgb[3];
    if (millis() - lastUpdate > frameDelay) {
        if (myPix != lastPix) {
            if (counter < CTR_THRESH) {
                float colorV = sin((6.28 / 30) * (float)(counter)) * 255;
                HSVtoRGB((359 / CTR_THRESH) * counter, 255, colorV, rgb);
                strip.setPixelColor(myPix, rgb[0], rgb[1], rgb[2]);
                counter++;
                frameDelay = 60;
            } else {
                lastPix = myPix;
                counter = 0;
                strip.clear();
                frameDelay = 0;
            }
        } else {
            myPix = random(0, strip.numPixels());
            frameDelay = 0;
        }
        strip.setBrightness(255);
        strip.show();
        lastUpdate = millis();
    }
}

// Color Wipe - fills the strip one pixel at a time with a random colour,
// picking a new colour for each pass
void colorWipeFrame() {
    static uint8_t wipePos = 0;
    static uint32_t wipeColor = strip.Color(random(255), random(255), random(255));
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 50) {
        strip.setPixelColor(wipePos, wipeColor);
        if (++wipePos >= strip.numPixels()) {
            wipePos = 0;
            wipeColor = strip.Color(random(255), random(255), random(255));
        }
        strip.setBrightness(255);
        strip.show();
        lastUpdate = millis();
    }
}

// HSV to RGB colors
// hue: 0-359, sat: 0-255, val (lightness): 0-255
// adapted from http://funkboxing.com/wordpress/?p=1366
void HSVtoRGB(int hue, int sat, int val, uint8_t * colors) {
    int base;
    if (sat == 0) { // Achromatic color (gray).
        colors[0] = val;
        colors[1] = val;
        colors[2] = val;
    } else {
        base = ((255 - sat) * val) >> 8;
        switch (hue / 60) {
        case 0:
            colors[0] = val;
            colors[1] = (((val - base) * hue) / 60) + base;
            colors[2] = base;
            break;
        case 1:
            colors[0] = (((val - base) * (60 - (hue % 60))) / 60) + base;
            colors[1] = val;
            colors[2] = base;
            break;
        case 2:
            colors[0] = base;
            colors[1] = val;
            colors[2] = (((val - base) * (hue % 60)) / 60) + base;
            break;
        case 3:
            colors[0] = base;
            colors[1] = (((val - base) * (60 - (hue % 60))) / 60) + base;
            colors[2] = val;
            break;
        case 4:
            colors[0] = (((val - base) * (hue % 60)) / 60) + base;
            colors[1] = base;
            colors[2] = val;
            break;
        case 5:
            colors[0] = val;
            colors[1] = base;
            colors[2] = (((val - base) * (60 - (hue % 60))) / 60) + base;
            break;
        }

    }
}

