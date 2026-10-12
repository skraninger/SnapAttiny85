/*
 * Snap - Phase 2: Music box + 8-pixel WS2812B ("NeoPixel") strip
 * ---------------------------------------------------------------------------
 * Phase 2 is Phase 1 (touch music box: RLE songs + per-song tempo, ADCTouch,
 * PWM audio) plus an 8-pixel LED strip that shows ONE animated pattern per
 * song.  It is a standalone sketch so Phase 1 stays untouched.  The patterns
 * are hand-ported (integer-only) from LedAttinyTouchx2.ino; the Adafruit
 * NeoPixel library is NOT used because it would not fit in the ATtiny85's 8 KB.
 *
 * CONTROLS
 *   Touch 1 (SONG)  - advance to the next song AND cycle the LED effect
 *   Touch 2 (EFFECT)- cycle to the next LED effect
 *   LED     (PB0)   - plain LED: touch indicator + one flash per beat (Phase 1)
 *   Strip   (PB4)   - WS2812B data for the 8-pixel strip
 *
 * PIN MAP (Phase 2)
 *   1  PB5  RESET (ISP)                     reserved
 *   2  PB3  Touch 2 - EFFECT               ADC3 -> ADCTouch channel 3
 *   3  PB4  NeoPixel data out (WS2812B)     was "Phase 2 pot"; pot deferred
 *   4  GND                                  common with strip/audio supply
 *   5  PB0  Beat / touch LED (active HIGH)  Phase 1's LED kept here
 *   6  PB1  Audio PWM out (OC1A)            Timer1 + 64 MHz PLL
 *   7  PB2  Touch 1 - SONG                  ADC1 -> ADCTouch channel 1
 *   8  VCC                                  5 V
 *
 * WHY PB4 FOR THE STRIP (unused-pin rule)
 *   PB1 = audio, PB2/PB3 = touch, and PB5 = RESET cannot be used.  That leaves
 *   PB0 and PB4.  Phase 1 reserved PB4 for a potentiometer, but the pot is
 *   deferred, so Phase 2 spends the otherwise-unused PB4 on the strip and KEEPS
 *   PB0 as the beat/touch LED.  (Reminder: PB0 and PB1 are not ADC-capable, so
 *   the pot can never move to PB0/PB1; re-adding the pot later needs a
 *   different plan - see "Phase 2 - LED Music Box.md".)
 *
 * LED REFRESH IS TIED TO THE SONG TEMPO
 *   WS2812 bit-banging must disable interrupts for ~275 us per frame.  The audio
 *   sample ISR runs every 50 us, so each frame briefly stalls audio (a small
 *   click).  To keep those stalls rare and on note boundaries, the strip is
 *   refreshed on the note clock, once every LED_FRAME_SLOTS slots, so the rate
 *   follows the song tempo.  LED_FRAME_SLOTS = 1 gives a frame per slot (8 fps at
 *   the default tempo = 8 slots/s), i.e. the LED effects run 2x faster than a
 *   once-per-beat refresh.  See loop().
 *
 * BUILD (arduino-cli, damellis "attiny" core)
 *   arduino-cli compile --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 Phase2_LedMusicBox
 *   arduino-cli upload  --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 -p <PORT> Phase2_LedMusicBox
 *
 * AUDIO OUTPUT: PB1 -> 1k -> NPN base; speaker +5V..collector; 10-47 uF series.
 * LED  OUTPUT: PB4 -> 470R -> strip DIN; 100 uF across strip 5V/GND.
 */

#include <ADCTouch.h>
#include <EEPROM.h>
#include <avr/io.h>
#include "songs.h"

/* ------------------------------------------------------------------ */
/*  Configuration -- everything worth hand-tuning                      */
/* ------------------------------------------------------------------ */

#if F_CPU != 8000000
#error "Phase 2 targets 8 MHz (clock=internal8): the audio sample rate and the WS2812 bit timing both depend on it."
#endif

// --- Touch pads (PB2 / PB3) -----------------------------------------
#define TOUCH_SONG_CH   1        // ADC1 = PB2  (Song pad: next song + next effect)
#define TOUCH_EFFECT_CH 3        // ADC3 = PB3  (Effect pad: next LED effect)
#define TOUCH_THRESHOLD 40       // counts above the calibrated baseline
#define TOUCH_GAP_MS    250      // minimum gap between two accepted touches
#define TOUCH_CAL_N     100      // startup baseline samples per pad (FR-07)

// --- Beat / touch indicator LED (PB0) --------------------------------
#define LED_PIN         0        // PB0 - plain LED (active HIGH)
#define SLOTS_PER_BEAT  2        // 1 slot = one eighth note, so a beat = 2 slots
                                 //   (this is the PB0 flash rate)

// --- NeoPixel strip (PB4) --------------------------------------------
#define NP_PIN          4        // PB4 - WS2812B data line (through ~470R)
#define NUM_LEDS        8        // strip length
#define NP_BRIGHTNESS   48       // 0..255 master scale applied to every pixel
                                 //   (keeps 8 px within a small supply's budget)
#define LED_FRAME_SLOTS 1        // note slots per strip refresh.  2 = once per
                                 //   beat (4 fps at tempo 8); 1 = a frame every
                                 //   slot, i.e. LED effects run 2x faster (8 fps)
#define NUM_PATTERNS    6        // LED effects in the cycle (see renderPattern)

// --- Tempo -----------------------------------------------------------
// Per-song speed lives in songs.h (SONG_<NAME>_TEMPO, SLOTS PER SECOND; 1 slot
// = one eighth note, so quarter-note BPM = 30*tempo).  DEFAULT_TEMPO is the
// fallback.  Both default to 8.
#ifndef DEFAULT_TEMPO
#define DEFAULT_TEMPO   8
#endif

// --- Synth voices / output level -------------------------------------
// Voice peak = 1 << AMP_SHIFT; decay lasts (1 << (DECAY + AMP_SHIFT)) / 20000 s.
// Keep DECAY + AMP_SHIFT <= 15.
#define DECAY           9
#define AMP_SHIFT       5
#define VOICES          4

#define EEPROM_MAGIC    0x53     // 'S'
#define ADDR_MAGIC      0
#define ADDR_SONG       1
#define ADDR_PATTERN    2

/* ------------------------------------------------------------------ */
/*  Music engine (David Johnson-Davies, CC BY 4.0)                     */
/* ------------------------------------------------------------------ */

// 32 chromatic notes, G#3 .. D#6.  f = Scale[n] * 20000 / 65536 Hz.
const int Scale[] = {
    680,  721,  764,  809,  857,  908,  962, 1020, 1080, 1144, 1212, 1284,
   1361, 1442, 1528, 1618, 1715, 1817, 1925, 2039, 2160, 2289, 2425, 2569,
   2722, 2884, 3055, 3237, 3429, 3633, 3849, 4078 };

volatile unsigned int Acc[VOICES];
volatile unsigned int Freq[VOICES];
volatile unsigned int Amp[VOICES];

const uint8_t *volatile Tune = 0;    // RLE byte stream in PROGMEM
volatile uint16_t TunePtr = 0;       // byte cursor into Tune
volatile uint8_t  Chan    = 0;
volatile bool     playing = false;
uint8_t           songIndex = 0;     // current song (also mirrored to EEPROM)
uint8_t           patternIndex = 0;  // current LED effect (also mirrored to EEPROM)

// RLE decode state: after a record's `gap` rest slots the chord is played.
volatile uint8_t      restSlots = 0;
volatile bool         hasPending = false;
volatile unsigned long pendingChord = 0;

// Millisecond time base built from the 20 kHz sample interrupt.
volatile uint32_t g_ms    = 0;
static volatile uint8_t  msDiv   = 0;
static volatile uint16_t noteDiv = 0;   // counts ms toward the next note slot

// Playback tempo (slots per second) for the current song.
volatile uint8_t tempo = DEFAULT_TEMPO;

// Beat counter: one beat every SLOTS_PER_BEAT slots; drives the PB0 flash.
// 8-bit so the read in loop() is atomic.
volatile uint8_t g_beats = 0;
static volatile uint8_t beatDiv = 0;

// Strip frame counter: one frame every LED_FRAME_SLOTS slots.  loop() renders
// the LED pattern when this changes; it is independent of the beat so the LED
// effects can run at their own (2x) rate.
volatile uint8_t g_frames = 0;
static volatile uint8_t ledDiv = 0;

// Trigger every note set in a chord word (bit 31 = Scale[0] = G#3).
static void playChord(unsigned long chord) {
    for (uint8_t note = 0; note < 32; note++) {
        if (chord & 0x80000000UL) {
            Freq[Chan] = Scale[note];
            Amp[Chan]  = 1 << (DECAY + AMP_SHIFT);
            Chan = (Chan + 1) & (VOICES - 1);
        }
        chord <<= 1;
    }
}

// Note clock: advance one slot.  RLE byte stream of [gap][chord LE x4] records;
// chord 0x000000FF ends the tune and its gap = trailing rests before looping.
static void advanceNote() {
    if (restSlots) { restSlots--; return; }
    if (hasPending) { playChord(pendingChord); hasPending = false; return; }

    uint8_t gap = pgm_read_byte(&Tune[TunePtr]);
    TunePtr++;
    unsigned long chord = pgm_read_dword(&Tune[TunePtr]);
    TunePtr += 4;

    if (chord == 0x000000FFUL) {              // end-of-tune
        TunePtr = 0;                          // loop to the start
        restSlots = gap;                      // wait any trailing rests first
        return;
    }
    if (gap) { restSlots = gap - 1; pendingChord = chord; hasPending = true; return; }
    playChord(chord);
}

// 20 kHz sample interrupt: mix voices into the PWM duty register, build the
// millisecond time base, and drive the note + beat clocks.  Serial with the
// mixer, so the RLE/tune state needs no locking.
ISR(TIMER0_COMPA_vect) {
    signed char Temp, Mask, Env, Note, Sum = 0;
    for (uint8_t c = 0; c < VOICES; c++) {
        Acc[c] = Acc[c] + Freq[c];
        Amp[c] = Amp[c] - (Amp[c] != 0);
        Temp = Acc[c] >> 8;
        Mask = Temp >> 7;
        Env  = Amp[c] >> DECAY;
        Note = (Env ^ Mask) + (Mask & 1);
        Sum  = Sum + Note;
    }
    OCR1A = Sum + 128;

    if (++msDiv >= 20) {                      // 20 kHz / 20 = 1 kHz
        msDiv = 0;
        g_ms++;
        if (++noteDiv >= (1000 / tempo)) {    // one slot every 1000/tempo ms
            noteDiv = 0;
            if (playing) {
                advanceNote();
                if (++ledDiv >= LED_FRAME_SLOTS) {     // strip frame clock (2x)
                    ledDiv = 0;
                    g_frames++;
                }
                if (++beatDiv >= SLOTS_PER_BEAT) {     // PB0 beat clock
                    beatDiv = 0;
                    g_beats++;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  NeoPixel strip: minimal WS2812B bit-bang driver (no library)       */
/* ------------------------------------------------------------------ */
/*
 * WS2812B bit timing at 8 MHz (1 cycle = 125 ns).  Per the part's data sheet:
 *   bit '0': high ~0.4 us, bit '1': high ~0.8 us, both ~1.25 us total.
 * The loop below emits 3-cycle-high for a '0' bit and 7-cycle-high for a '1'
 * bit (0.375 us / 0.875 us), period 11-12 cycles (1.4-1.5 us) - inside the
 * WS2812B tolerances.  Byte is sent MSB-first, and each pixel is GRB.
 */
static uint8_t pix[NUM_LEDS * 3];    // wire order: G,R,B per pixel

// Send one byte, MSB first, on PORTB/NP_PIN.  Interrupts are disabled by the
// caller for the whole frame; this routine is pure cycle-counted output.
static void wsSendByte(uint8_t b) {
    uint8_t cnt = 8;
    __asm__ __volatile__(
        "1:            \n\t"
        "lsl  %0       \n\t"   // 1 cyc: MSB -> carry
        "sbi  %2, %3   \n\t"   // 2 cyc: data line HIGH
        "brcs 2f       \n\t"   // 1/2 cyc
        "cbi  %2, %3   \n\t"   // 2 cyc: LOW  ('0' => 3-cycle high)
        "rjmp 3f       \n\t"   // 2 cyc
        "2:            \n\t"
        "nop           \n\t"
        "nop           \n\t"
        "cbi  %2, %3   \n\t"   // ('1' => 7-cycle high)
        "3:            \n\t"
        "dec  %1       \n\t"   // 1 cyc
        "brne 1b       \n\t"   // 2/1 cyc
        : "+r" (b), "+r" (cnt)
        : "I" (_SFR_IO_ADDR(PORTB)), "I" (NP_PIN)
    );
}

// Push the whole buffer out.  Interrupts are masked for ~240 us (8 px x 24 bit
// x ~1.4 us).  The line is left LOW, which the strip latches after >50 us.
static void wsShow() {
    uint8_t sreg = SREG;
    cli();
    for (uint8_t i = 0; i < NUM_LEDS * 3; i++) wsSendByte(pix[i]);
    SREG = sreg;
}

// Store one pixel as GRB (WS2812 wire order), scaled by NP_BRIGHTNESS.
static inline void setPx(uint8_t i, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *p = &pix[i * 3];
    p[0] = (uint8_t)(((uint16_t)g * NP_BRIGHTNESS) >> 8);
    p[1] = (uint8_t)(((uint16_t)r * NP_BRIGHTNESS) >> 8);
    p[2] = (uint8_t)(((uint16_t)b * NP_BRIGHTNESS) >> 8);
}

static void clearPix() {
    for (uint8_t i = 0; i < NUM_LEDS * 3; i++) pix[i] = 0;
}

/* ------------------------------------------------------------------ */
/*  LED patterns (integer-only, ported from LedAttinyTouchx2.ino)      */
/*  One pattern per song; renderPattern() picks by songIndex.          */
/* ------------------------------------------------------------------ */

// hue 0..255 -> RGB (three 85-step ramp segments)
static void wheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (pos < 85) {
        *r = 255 - pos * 3; *g = pos * 3;        *b = 0;
    } else if (pos < 170) {
        pos -= 85;          *r = 0;              *g = 255 - pos * 3; *b = pos * 3;
    } else {
        pos -= 170;         *r = pos * 3;        *g = 0;             *b = 255 - pos * 3;
    }
}

// Integer sine table: sin(2*pi*k/32) scaled to +-127 (no float/sin() needed).
// Stored in flash (PROGMEM) so it costs no SRAM.
static const int8_t sineTab[32] PROGMEM = {
      0,  25,  49,  71,  90, 106, 117, 125,
    127, 125, 117, 106,  90,  71,  49,  25,
      0, -25, -49, -71, -90,-106,-117,-125,
   -127,-125,-117,-106, -90, -71, -49, -25 };

// 0) Bluey Theme - Wave: a sinusoidal hue wave travels across the strip.  Hue
//    oscillates 160..240 (blue -> purple -> red), matching LedAttinyTouchx2's
//    waveyFrame(), but computed with the integer sine table (no float).
static void patWave() {
    static uint8_t phase = 0;
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        int8_t s = (int8_t)pgm_read_byte(&sineTab[(i * 4 + phase) & 31]);  // 45 deg/px
        uint8_t r, g, b;
        wheel((uint8_t)(200 + (s * 40) / 127), &r, &g, &b);
        setPx(i, r, g, b);
    }
    phase = (phase + 2) & 31;                          // wave travels (per frame)
}

// 1) Baby Shark - Chaser: one bright green pixel runs around the strip.
static void patChaser() {
    static uint8_t pos = 0;
    clearPix();
    setPx(pos, 0, 255, 0);
    pos = (pos + 1) % NUM_LEDS;
}

// 2) Dynamite - Rainbow: full-spectrum gradient scrolling across the strip.
static void patRainbow() {
    static uint8_t hue = 0;
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        uint8_t r, g, b;
        wheel((uint8_t)(hue + i * 32), &r, &g, &b);
        setPx(i, r, g, b);
    }
    hue += 4;
}

// 3) Dance of the Sugar Plum Fairy - Scanner: one pixel bounces end to end;
//    the colour advances after each sweep.
static void patScanner() {
    static uint8_t pos = 0;
    static bool    fwd = true;
    static uint8_t col = 0;
    uint8_t r, g, b;
    wheel((uint8_t)(col * 85), &r, &g, &b);
    clearPix();
    setPx(pos, r, g, b);
    if (fwd) {
        if (pos >= NUM_LEDS - 1) fwd = false; else pos++;
    } else {
        if (pos == 0) { fwd = true; col = (col + 1) % 3; } else pos--;
    }
}

// 4) Pachelbel's Canon - Solid: all pixels one colour, hue drifting slowly.
static void patSolid() {
    static uint8_t hue = 0;
    uint8_t r, g, b;
    wheel(hue, &r, &g, &b);
    for (uint8_t i = 0; i < NUM_LEDS; i++) setPx(i, r, g, b);
    hue += 2;
}

// 5) Super Mario Brothers - Color Wipe: fill the strip one pixel at a time,
//    new colour each pass.
static void patWipe() {
    static uint8_t pos = 0;
    static uint8_t hue = 0;
    uint8_t r, g, b;
    if (pos == 0) clearPix();
    wheel(hue, &r, &g, &b);
    setPx(pos, r, g, b);
    if (++pos >= NUM_LEDS) { pos = 0; hue += 64; }
}

// LED effects, cycled by the EFFECT pad (index 0..NUM_PATTERNS-1).  The same
// cycle also advances when the song changes, so a new song always brings a new
// effect.  (A subset of LedAttinyTouchx2.ino's patterns, integer-only.)
static void renderPattern() {
    switch (patternIndex) {
        case 0: patWave();    break;   // blue/purple/red hue wave
        case 1: patChaser();  break;   // one pixel runs around
        case 2: patRainbow(); break;   // scrolling rainbow
        case 3: patScanner(); break;   // bouncing pixel, colour per sweep
        case 4: patSolid();   break;   // solid colour, hue drifts
        default: patWipe();   break;   // fill one pixel at a time
    }
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static uint32_t snapMillis() {
    uint32_t m;
    uint8_t  sreg = SREG;
    cli();
    m = g_ms;
    SREG = sreg;
    return m;
}

static void silenceVoices() {
    for (uint8_t c = 0; c < VOICES; c++) Amp[c] = 0;
}

static void selectSong(uint8_t idx) {
    uint8_t sreg = SREG;
    cli();
    playing = false;
    songIndex = idx;
    switch (idx) {
        case 0:  Tune = SONG_BLUEY_THEME;                 tempo = SONG_BLUEY_THEME_TEMPO;                 break;
        case 1:  Tune = SONG_BABY_SHARK;                  tempo = SONG_BABY_SHARK_TEMPO;                  break;
        case 2:  Tune = SONG_DYNAMITE;                    tempo = SONG_DYNAMITE_TEMPO;                    break;
        case 3:  Tune = SONG_DANCE_OF_THE_SUGAR_PLUM_FAIRY; tempo = SONG_DANCE_OF_THE_SUGAR_PLUM_FAIRY_TEMPO; break;
        case 4:  Tune = SONG_PACHELBELS_CANON;            tempo = SONG_PACHELBELS_CANON_TEMPO;            break;
        default: Tune = SONG_SUPER_MARIO_BROTHERS;        tempo = SONG_SUPER_MARIO_BROTHERS_TEMPO;        break;
    }
    if (tempo == 0) tempo = DEFAULT_TEMPO;   // guard: tempo is a divisor in the ISR
    TunePtr = 0;
    restSlots = 0;
    hasPending = false;
    Chan = 0;
    silenceVoices();
    playing = true;
    SREG = sreg;

    // Immediately show the new song's first pattern frame.
    clearPix();
    renderPattern();
    wsShow();
}

/* ------------------------------------------------------------------ */
/*  Touch                                                              */
/* ------------------------------------------------------------------ */

int  baseline[2]  = { 0, 0 };
bool lastTouch[2] = { false, false };
uint32_t lastFire[2] = { 0, 0 };

static bool touchFired(uint8_t pad, uint8_t channel) {
    int  value = ADCTouch.read(channel, 10);
    bool now   = (value - baseline[pad]) > TOUCH_THRESHOLD;
    bool fired = false;

    if (now && !lastTouch[pad] &&
        (snapMillis() - lastFire[pad]) > TOUCH_GAP_MS) {
        lastFire[pad] = snapMillis();
        fired = true;
    }
    lastTouch[pad] = now;
    return fired;
}

/* ------------------------------------------------------------------ */
/*  Setup / loop                                                       */
/* ------------------------------------------------------------------ */

void setup() {
    // --- Indicator LED (PB0)
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // --- NeoPixel data pin (PB4) and a blank first frame
    pinMode(NP_PIN, OUTPUT);
    digitalWrite(NP_PIN, LOW);
    clearPix();
    wsShow();

    // ADC prescaler /64 -> 125 kHz at 8 MHz (matches the touch sketch)
    ADCSRA |= (1 << ADPS2) | (1 << ADPS1);
    ADCSRA &= ~(1 << ADPS0);

    // --- Audio PWM carrier: Timer1 from the 64 MHz PLL on OC1A (PB1).
    PLLCSR = 1 << PLLE;
    while (!(PLLCSR & (1 << PLOCK))) { /* wait for PLL lock */ }
    PLLCSR |= 1 << PCKE;
    TCCR1  = 1 << CS10 | 1 << PWM1A | 2 << COM1A0;  // PLL/1, PWM A, clear on match
    OCR1C  = 0xFF;                                  // TOP = 255 (8-bit)
    OCR1A  = 128;                                   // 50% = silence
    DDRB  |= 1 << DDB1;                             // PB1 output

    for (uint8_t c = 0; c < VOICES; c++) { Acc[c] = 0; Freq[c] = 0; Amp[c] = 0; }

    // --- EEPROM: restore the last song and LED effect
    if (EEPROM.read(ADDR_MAGIC) != EEPROM_MAGIC) {
        EEPROM.write(ADDR_MAGIC, EEPROM_MAGIC);
        EEPROM.write(ADDR_SONG, 0);
        EEPROM.write(ADDR_PATTERN, 0);
    }
    uint8_t song = EEPROM.read(ADDR_SONG);
    if (song >= NUM_SONGS) song = 0;
    patternIndex = EEPROM.read(ADDR_PATTERN);
    if (patternIndex >= NUM_PATTERNS) patternIndex = 0;

    // --- Touch calibration (average resting reads to set the baseline)
    long sumSong = 0, sumEffect = 0;
    for (uint8_t i = 0; i < TOUCH_CAL_N; i++) {
        sumSong   += ADCTouch.read(TOUCH_SONG_CH, 10);
        sumEffect += ADCTouch.read(TOUCH_EFFECT_CH, 10);
    }
    baseline[0] = sumSong   / TOUCH_CAL_N;
    baseline[1] = sumEffect / TOUCH_CAL_N;

    // --- 20 kHz sample clock: Timer0 fast PWM, TOP=OCR0A=49, prescaler 8
    TCCR0A = 3 << WGM00;
    TCCR0B = 1 << WGM02 | 2 << CS00;
    OCR0A  = 49;
    TIMSK  = 1 << OCIE0A;

    // --- Auto-play the remembered song (and show its pattern) at reset
    selectSong(song);
}

void loop() {
    // SONG pad: next song, and cycle the LED effect as well; restart; remember
    if (touchFired(0, TOUCH_SONG_CH)) {
        uint8_t next = (songIndex + 1) % NUM_SONGS;
        patternIndex = (patternIndex + 1) % NUM_PATTERNS;
        selectSong(next);                       // renders the new effect at once
        EEPROM.update(ADDR_SONG, next);
        EEPROM.update(ADDR_PATTERN, patternIndex);
    }

    // EFFECT pad: cycle to the next LED effect and show it immediately
    if (touchFired(1, TOUCH_EFFECT_CH)) {
        patternIndex = (patternIndex + 1) % NUM_PATTERNS;
        EEPROM.update(ADDR_PATTERN, patternIndex);
        renderPattern();
        wsShow();
    }

    // PB0 (beat/touch LED): flash once per beat; lit while a pad is held.
    // g_beats is 8-bit so the read here is atomic on AVR.
    static uint8_t  lastBeat = 0;
    static uint32_t ledOffAt = 0;
    static bool     ledOn    = false;
    if (g_beats != lastBeat) {
        lastBeat = g_beats;
        ledOffAt = snapMillis() + (1000 / tempo);   // LED lit for one slot
        ledOn    = true;
    }
    if (ledOn && (int32_t)(snapMillis() - ledOffAt) >= 0) {
        ledOn = false;
    }

    // Strip: render one frame every LED_FRAME_SLOTS slots (still tied to the
    // tempo).  g_frames is 8-bit so the read here is atomic on AVR.
    static uint8_t lastFrame = 0;
    if (g_frames != lastFrame) {
        lastFrame = g_frames;
        renderPattern();
        wsShow();                                   // ~275 us with IRQs off
    }

    bool touchHeld = lastTouch[0] || lastTouch[1];
    digitalWrite(LED_PIN, (touchHeld || ledOn) ? HIGH : LOW);
}
