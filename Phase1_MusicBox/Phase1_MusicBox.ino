/*
 * Snap - Phase 1: Music Box with capacitive touch controls
 * ---------------------------------------------------------
 * A single ATtiny85 that plays the compiled-in songs (songs.h) through a
 * small speaker via PWM, with two capacitive touch pads for control:
 *
 *   Touch 1 (SONG)  - advance to the next song and start playing it
 *   Touch 2 (PLAY)  - play / pause the current song
 *   LED     (PB0)   - plain LED: lit while a touch pad is being touched, and
 *                     flashed once per beat while playing
 *
 * The audio engine and the 32-note Scale[] table are taken from David
 * Johnson-Davies' "Digital Music Box v2" (CC BY 4.0), adapted here for an 8 MHz
 * ATtiny85 and the pins this board uses.  The songs are stored in a compact
 * run-length form (see songs.h / advanceNote()) so all of them fit in 8 KB.
 * The touch stack mirrors the proven "LedAttinyTouchx2" sketch (ADCTouch +
 * baseline calibration).
 *
 * PIN MAP (Phase 1)
 *   1  PB5  RESET (ISP)
 *   2  PB3  Touch 2  - PLAY/PAUSE   (ADC3 -> ADCTouch channel 3)
 *   3  PB4  reserved for Phase 2 potentiometer (ADC2)
 *   4  GND
 *   5  PB0  LED (beat flash + touch indicator; Phase 2 reuses this pin)
 *   6  PB1  Audio PWM out (OC1A, Timer1 + 64 MHz PLL)
 *   7  PB2  Touch 1  - SONG        (ADC1 -> ADCTouch channel 1)
 *   8  VCC
 *
 * BUILD (arduino-cli, damellis "attiny" core)
 *   arduino-cli compile --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 Phase1_MusicBox
 *   arduino-cli upload  --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 -p <PORT> Phase1_MusicBox
 *
 * Audio output circuit (see Phase 1 doc): PB1 -> 1k -> NPN base; speaker
 * between +5V and collector; 10-47 uF DC-block in series with the speaker.
 */

#include <ADCTouch.h>
#include <EEPROM.h>
#include "songs.h"

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

#if F_CPU != 8000000
#error "Phase 1 targets 8 MHz (clock=internal8): the 20 kHz sample rate depends on it."
#endif

#define TOUCH_SONG_CH   1        // ADC1 = PB2  (Song pad)
#define TOUCH_PLAY_CH   3        // ADC3 = PB3  (Play/Pause pad)
#define TOUCH_THRESHOLD 40       // counts above the calibrated baseline
#define TOUCH_GAP_MS    250      // minimum gap between two accepted touches
#define TOUCH_CAL_N     100      // startup baseline samples per pad (FR-07)

// --- Beat / touch indicator LED (PB0) --------------------------------
#define LED_PIN         0        // PB0 - plain LED (active HIGH)
#define SLOTS_PER_BEAT  2        // 1 slot = one eighth note, so a beat is 2 slots;
                                 //   the LED therefore blinks at HALF the song
                                 //   tempo - on for one slot, off for the next

// --- Tempo -----------------------------------------------------------
// Per-song speed lives in songs.h: each song has a SONG_<NAME>_TEMPO macro,
// in SLOTS PER SECOND (1 slot = one eighth note, so quarter-note BPM = 30*tempo).
// DEFAULT_TEMPO (also in songs.h) is the fallback.  Both default to 8
// (0.125 s/slot = 240 BPM), i.e. twice the original 4.
#ifndef DEFAULT_TEMPO
#define DEFAULT_TEMPO   8        // fallback if songs.h doesn't define one
#endif

// --- Synth voices / output level -------------------------------------
// A voice's envelope starts at 1 << (DECAY + AMP_SHIFT) and steps down by 1 per
// sample, so peak loudness = 1 << AMP_SHIFT and the decay lasts
// (1 << (DECAY + AMP_SHIFT)) / 20000 seconds.  Keep DECAY + AMP_SHIFT <= 15.
#define DECAY           9        // decay curve / length (from DigitalMusicBox)
#define AMP_SHIFT       5        // voice peak = 1 << AMP_SHIFT;
                                 //   raise to 6 for ~2x louder (and a longer decay)
#define VOICES          4        // DDS voices (max notes per chord)

#define EEPROM_MAGIC    0x53     // 'S'
#define ADDR_MAGIC      0
#define ADDR_SONG       1

/* ------------------------------------------------------------------ */
/*  Music engine (David Johnson-Davies, CC BY 4.0)                     */
/* ------------------------------------------------------------------ */

// 32 chromatic notes, G#3 .. D#6.  Sample rate is 20 kHz, so the audible
// frequency of Scale[n] is Scale[n] * 20000 / 65536 Hz.
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

// RLE decode state: after a record's `gap` rest slots the chord is played.
volatile uint8_t      restSlots = 0;   // rest slots still to wait
volatile bool         hasPending = false;
volatile unsigned long pendingChord = 0;

// Millisecond time base built from the 20 kHz sample interrupt.
volatile uint32_t g_ms    = 0;
static volatile uint8_t  msDiv   = 0;
static volatile uint16_t noteDiv = 0;   // counts ms toward the next note slot

// Playback tempo (slots per second) for the current song.  Set by selectSong()
// from the song's SONG_<NAME>_TEMPO macro; read by the note clock in the ISR.
volatile uint8_t tempo = DEFAULT_TEMPO;

// Beat counter: the note clock counts slots and sets one beat every
// SLOTS_PER_BEAT slots. g_beats is polled by loop() to flash the beat LED;
// 8-bit so the read in loop() is atomic on AVR.
volatile uint8_t g_beats = 0;
static volatile uint8_t beatDiv = 0;   // slots counted toward the next beat

// Trigger every note set in a chord word (bit 31 = Scale[0] = G#3).
static void playChord(unsigned long chord) {
    for (uint8_t note = 0; note < 32; note++) {
        if (chord & 0x80000000UL) {
            Freq[Chan] = Scale[note];
            Amp[Chan]  = 1 << (DECAY + AMP_SHIFT);   // peak = 1 << AMP_SHIFT
            Chan = (Chan + 1) & (VOICES - 1);
        }
        chord <<= 1;
    }
}

// Note clock: advance one slot.  The tune is a run-length byte stream of
// [gap][chord LE x4] records - `gap` rest slots, then the chord.  A chord of
// 0x000000FF ends the tune, so the cursor loops to the start.  Serial with the
// mixer, so no locking is required; only called while playing.
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

// 20 kHz sample interrupt: mix the voices into the PWM duty register. It also
// builds the millisecond time base and drives the note clock from it, so the
// music no longer depends on the ATtiny85 watchdog timer (which did not fire
// reliably here - that left the voices silent and the beat LED dark).
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

    // 20 kHz / 20 = 1 kHz time base.
    if (++msDiv >= 20) {
        msDiv = 0;
        g_ms++;
        // One slot every (1000 / tempo) ms  =>  `tempo` slots per second.
        if (++noteDiv >= (1000 / tempo)) {
            noteDiv = 0;
            if (playing) {
                advanceNote();                        // advance one slot
                if (++beatDiv >= SLOTS_PER_BEAT) {    // a beat = SLOTS_PER_BEAT slots
                    beatDiv = 0;
                    g_beats++;                        // polled by loop() for the LED
                }
            }
        }
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
    // Pause scheduling while the pointer/voices are rewritten.
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
    // --- Beat LED output (plain digital pin)
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // ADC prescaler /64 -> 125 kHz at 8 MHz (matches the working touch sketch)
    ADCSRA |= (1 << ADPS2) | (1 << ADPS1);
    ADCSRA &= ~(1 << ADPS0);

    // --- Audio PWM carrier: Timer1 clocked from the 64 MHz PLL, output on
    //     OC1A (PB1).  64 MHz / 256 = 250 kHz carrier, 8-bit duty.
    PLLCSR = 1 << PLLE;
    while (!(PLLCSR & (1 << PLOCK))) { /* wait for PLL lock */ }
    PLLCSR |= 1 << PCKE;

    TCCR1  = 1 << CS10 | 1 << PWM1A | 2 << COM1A0; // PLL/1, PWM A, clear on match
    OCR1C  = 0xFF;                                 // TOP = 255 (8-bit)
    OCR1A  = 128;                                  // 50% = silence
    DDRB  |= 1 << DDB1;                            // PB1 output

    // voices off
    for (uint8_t c = 0; c < VOICES; c++) {
        Acc[c] = 0; Freq[c] = 0; Amp[c] = 0;
    }

    // --- EEPROM: restore the last song
    if (EEPROM.read(ADDR_MAGIC) != EEPROM_MAGIC) {
        EEPROM.write(ADDR_MAGIC, EEPROM_MAGIC);
        EEPROM.write(ADDR_SONG, 0);
    }
    uint8_t song = EEPROM.read(ADDR_SONG);
    if (song >= NUM_SONGS) song = 0;

    // --- Touch calibration: average TOUCH_CAL_N resting reads per pad so the
    //     threshold (TOUCH_THRESHOLD counts above baseline) rejects ambient
    //     drift.  ADCTouch.read() itself takes time, so no extra delay is used.
    long sumSong = 0, sumPlay = 0;
    for (uint8_t i = 0; i < TOUCH_CAL_N; i++) {
        sumSong += ADCTouch.read(TOUCH_SONG_CH, 10);
        sumPlay += ADCTouch.read(TOUCH_PLAY_CH, 10);
    }
    baseline[0] = sumSong / TOUCH_CAL_N;
    baseline[1] = sumPlay / TOUCH_CAL_N;

    // --- 20 kHz sample clock: Timer0 fast PWM, TOP=OCR0A, prescaler 8.
    //     8 MHz / (8 * (49 + 1)) = 20 kHz.
    TCCR0A = 3 << WGM00;
    TCCR0B = 1 << WGM02 | 2 << CS00;
    OCR0A  = 49;
    TIMSK  = 1 << OCIE0A;          // enable compare-match A (sample ISR)

    // --- Start playing the remembered song immediately on reset. The note
    //     clock that advances the tune is driven from the Timer0 sample ISR
    //     time base, so no watchdog timer is needed (FR-01/FR-03/FR-04).
    selectSong(song);
}

void loop() {
    // SONG pad: next song, restart playback
    if (touchFired(0, TOUCH_SONG_CH)) {
        uint8_t next = (songIndex + 1) % NUM_SONGS;
        selectSong(next);
        EEPROM.update(ADDR_SONG, next);
    }

    // PLAY pad: toggle play / pause
    if (touchFired(1, TOUCH_PLAY_CH)) {
        if (playing) {
            playing = false;
            silenceVoices();
        } else {
            playing = true;
        }
    }

    // Beat LED: blink at HALF the song tempo - one beat every SLOTS_PER_BEAT
    // slots.  The LED is lit for one slot per beat (on for one slot, off for the
    // next = 50% duty), so the blink rate is half the tempo.
    // Touch LED: force PB0 on while either pad is held, so a finger is visibly
    // acknowledged; on release it drops straight back to blinking.
    // g_beats is 8-bit, so the read here is atomic on AVR.
    static uint8_t  lastBeat = 0;
    static uint32_t ledOffAt = 0;
    static bool     ledOn    = false;
    if (g_beats != lastBeat) {
        lastBeat = g_beats;
        ledOffAt = snapMillis() + (1000 / tempo);   // on for one slot
        ledOn    = true;
    }
    if (ledOn && (int32_t)(snapMillis() - ledOffAt) >= 0) {
        ledOn = false;
    }

    bool touchHeld = lastTouch[0] || lastTouch[1];
    digitalWrite(LED_PIN, (touchHeld || ledOn) ? HIGH : LOW);
}
