/*
 * Snap - Speaker (audio path) bring-up test
 * -----------------------------------------
 * The simplest possible program that proves the audio output chain works:
 * it plays a single square-wave "voice" that alternates between two clear
 * notes (A4 = 440 Hz and A5 = 880 Hz) once every TOGGLE_MS, and flashes a
 * plain LED on LED_PIN for a short pulse each time the tone changes.
 *
 * If you hear the two notes alternating and see the LED blink in time with
 * the changes, then the Timer1 + 64 MHz PLL PWM carrier, the PB1 pin, and the
 * transistor/speaker driver are all wired correctly and the 8 MHz clock/timer
 * maths of the real music engine are sound.
 *
 * This is deliberately a stripped-down build of the Phase 1 engine
 * (Phase1_MusicBox.ino): same timers, same Scale[] table, same DDS square
 * wave, but one voice at a constant amplitude and no touch / songs / EEPROM.
 *
 * PIN MAP
 *   1  PB5  RESET (ISP)
 *   2  PB3  free
 *   3  PB4  free
 *   4  GND
 *   5  PB0  LED  (plain LED + ~330R to GND, active HIGH)
 *   6  PB1  Audio PWM out (OC1A, Timer1 + 64 MHz PLL) -> speaker driver
 *   7  PB2  free
 *   8  VCC
 *
 * BUILD (arduino-cli, damellis "attiny" core)
 *   arduino-cli compile --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 SpeakerTest
 *   arduino-cli upload  --fqbn attiny:avr:ATtinyX5:cpu=attiny85,clock=internal8 -p <PORT> SpeakerTest
 *
 * IMPORTANT: 8 MHz only. Timer0 is repurposed as the 20 kHz sample tick, which
 * fixes the 440/880 Hz tuning and removes the Arduino millis()/delay() time
 * base - the sketch counts milliseconds itself (see snapMillis()).
 */

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

#if F_CPU != 8000000
#error "SpeakerTest targets 8 MHz (clock=internal8): the 20 kHz sample rate depends on it."
#endif

#define LED_PIN        0        // PB0 - plain LED (active HIGH)
#define TOGGLE_MS      500      // switch between the two notes every 500 ms
#define LED_FLASH_MS   120      // LED on-time after each tone change

#define DECAY          9        // matches the music engine's envelope shift
#define TONE_LEVEL    100       // square-wave amplitude, 1..127 (loudness)

/* ------------------------------------------------------------------ */
/*  Audio engine (single voice, from David Johnson-Davies - CC BY 4.0) */
/* ------------------------------------------------------------------ */

// 32 chromatic notes, G#3 .. D#6.  Sample rate is 20 kHz, so the audible
// frequency of Scale[n] is Scale[n] * 20000 / 65536 Hz.
const int Scale[] = {
    680,  721,  764,  809,  857,  908,  962, 1020, 1080, 1144, 1212, 1284,
   1361, 1442, 1528, 1618, 1715, 1817, 1925, 2039, 2160, 2289, 2425, 2569,
   2722, 2884, 3055, 3237, 3429, 3633, 3849, 4078 };

#define TONE_LOW   Scale[13]    // 1442 -> ~440 Hz (A4)
#define TONE_HIGH  Scale[25]    // 2884 -> ~880 Hz (A5)

volatile unsigned int Acc  = 0;          // phase accumulator
volatile unsigned int Freq = TONE_LOW;   // current note increment

// Millisecond time base built from the 20 kHz sample interrupt.
volatile uint32_t g_ms    = 0;
static volatile uint8_t msDiv = 0;

// 20 kHz sample interrupt: one square-wave voice -> PWM duty register.
ISR(TIMER0_COMPA_vect) {
    Acc += Freq;
    signed char temp = Acc >> 8;             // high byte of the accumulator
    signed char mask = temp >> 7;            // 0 or -1 -> square-wave sign
    signed char env  = TONE_LEVEL;           // constant amplitude (no decay)
    signed char note = (env ^ mask) + (mask & 1);
    OCR1A = note + 128;                      // 128 = silence / mid-rail

    if (++msDiv >= 20) {                     // 20000 / 20 = 1000 Hz
        msDiv = 0;
        g_ms++;
    }
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

// Read the interrupt-driven millisecond counter without a torn 32-bit load.
static uint32_t snapMillis() {
    uint32_t m;
    uint8_t  sreg = SREG;
    cli();
    m = g_ms;
    SREG = sreg;
    return m;
}

// Set the note increment atomically (the ISR reads it every 50 us).
static void setFreq(unsigned int f) {
    uint8_t sreg = SREG;
    cli();
    Freq = f;
    SREG = sreg;
}

/* ------------------------------------------------------------------ */
/*  Setup / loop                                                       */
/* ------------------------------------------------------------------ */

void setup() {
    // LED output (plain digital pin).
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // --- Audio PWM carrier: Timer1 clocked from the 64 MHz PLL, output on
    //     OC1A (PB1).  64 MHz / 256 = 250 kHz carrier, 8-bit duty.
    PLLCSR = 1 << PLLE;
    while (!(PLLCSR & (1 << PLOCK))) { /* wait for PLL lock */ }
    PLLCSR |= 1 << PCKE;

    TCCR1  = 1 << CS10 | 1 << PWM1A | 2 << COM1A0; // PLL/1, PWM A, clear on match
    OCR1C  = 0xFF;                                 // TOP = 255 (8-bit)
    OCR1A  = 128;                                  // 50% = silence
    DDRB  |= 1 << DDB1;                            // PB1 output

    // --- 20 kHz sample clock: Timer0 fast PWM, TOP=OCR0A, prescaler 8.
    //     8 MHz / (8 * (49 + 1)) = 20 kHz.
    TCCR0A = 3 << WGM00;
    TCCR0B = 1 << WGM02 | 2 << CS00;
    OCR0A  = 49;
    TIMSK  = 1 << OCIE0A;          // enable compare-match A (sample ISR)

    // Start on the low note; the first change happens after TOGGLE_MS.
    Acc = 0;
    Freq = TONE_LOW;
    g_ms = 0;
}

void loop() {
    static uint32_t lastToggle = 0;
    static uint32_t ledOffAt    = 0;
    static bool     toneHigh    = false;
    static bool     ledOn       = false;

    uint32_t now = snapMillis();

    // Alternate the tone; flash the LED on every change.
    if (now - lastToggle >= TOGGLE_MS) {
        lastToggle += TOGGLE_MS;
        toneHigh = !toneHigh;
        setFreq(toneHigh ? TONE_HIGH : TONE_LOW);

        digitalWrite(LED_PIN, HIGH);
        ledOffAt = now + LED_FLASH_MS;
        ledOn    = true;
    }

    // Turn the LED back off after the flash.
    if (ledOn && (int32_t)(now - ledOffAt) >= 0) {
        digitalWrite(LED_PIN, LOW);
        ledOn = false;
    }
}
