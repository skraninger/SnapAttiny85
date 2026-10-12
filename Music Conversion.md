# Music Conversion Notes

**How to turn a song into data the ATtiny85 music player can play.**

This document gathers the information needed to convert simplified melodies
(the Bluey theme, Baby Shark, *Can't Stop the Feeling*, *Dynamite*) into the
compact format used by the music engine from `DigitalMusicBox.c`. It records
the target format (verified by decoding the reference tune), the note table,
the timing, the source transcriptions used, and the tooling that automates the
conversion.

Related files:

| File | Purpose |
| ---- | ------- |
| `DigitalMusicBox.c` | Reference music engine (David Johnson-Davies, CC BY 4.0) |
| `tools/convert_song.py` | Melody → chord-word converter (this project) |
| `tools/convert_punchsheet.py` | Punch-sheet PDF → chord-word converter (this project) |
| `Punch Sheet Conversion.md` | How the K15/GI20 punch sheets are translated |
| `MusicBoxPunchSheets/*.pdf` | musicboxmaniacs.com punch sheets (image-only) |
| `songs/*.song` | Simplified melodies in the converter's text format |
| `songs.h` | Generated C `PROGMEM` arrays ready to `#include` |
| `FSD.md` §9 | Where this format fits in the overall design |

> **Two sources.** `songs.h` is now generated from the **punch sheets** (full
> arrangements, with chords) by `tools/convert_punchsheet.py`; see
> `Punch Sheet Conversion.md`. The `.song` melody path below is kept for songs
> without a sheet (currently *Can't Stop the Feeling*) and for hand-written
> experiments.

---

## 1. The target format (verified)

The reference tune in `DigitalMusicBox.c` was decoded to confirm every detail
below.

* A tune is a `const uint32_t[]` living in **PROGMEM**.
* **Each element is one time slot.** Slots advance at a fixed rate set by the
  watchdog timer (see §3).
* Within a 32-bit slot word, **bit _n_ selects note `Scale[n]`**:

  | bit position | index | note  |
  | :----------: | :---: | :---: |
  | 31 (MSB, `0x80000000`) | 0  | G#3 |
  | …            | …     | …     |
  | 0 (LSB, `0x00000001`)  | 31 | D#6 |

  In the ISR the word is read MSB-first, so **leftmost bit = lowest note**.
* A slot word of **`0x00000000` is a rest**.
* The tune ends with the sentinel word **`0x000000FF`**. The ISR does
  `if (Chord == 0xFF) return;` and waits there (the firmware must reset the
  pointer if it wants a loop).
* Setting several bits in one word plays those notes **together** (a chord).

Reference example (start of "Happy Birthday"):

```c
0b00000000000000000001000000000000,  // D#5
0b00000000000000000001000000000000,  // D#5
0b10000000000000000000010000000000,  // G#3 + F5 (bass + melody)
```

> **Storage note.** The engine still plays the 32-bit slot word described here,
> but `songs.h` now stores the songs **run-length encoded** (to fit flash) and the
> Phase 1 firmware expands each record at play time — see §6.

---

## 2. The note table (`Scale[]`)

The engine plays a **chromatic scale of 32 notes from G#3 to D#6**
(2 octaves + 4 semitones). The player's `Scale[]` values are DDS phase
increments, not frequencies; the audible frequency is

```
f(note) = Scale[index] * sampleRate / 65536      // sampleRate = 20 000 Hz
```

Decoding `Scale[]` against that formula shows it is chromatic and equal-tempered,
with **A4 = 440 Hz at index 13**:

| idx | note | Scale |  f (Hz) | idx | note | Scale |  f (Hz) |
| :-: | :--: | :---: | ------: | :-: | :--: | :---: | ------: |
| 0  | G#3 | 680  | 207.5 | 16 | C5  | 1715 | 523.4 |
| 1  | A3  | 721  | 220.0 | 17 | C#5 | 1817 | 554.5 |
| 2  | A#3 | 764  | 233.2 | 18 | D5  | 1925 | 587.5 |
| 3  | B3  | 809  | 246.9 | 19 | D#5 | 2039 | 622.3 |
| 4  | C4  | 857  | 261.5 | 20 | E5  | 2160 | 659.2 |
| 5  | C#4 | 908  | 277.1 | 21 | F5  | 2289 | 698.5 |
| 6  | D4  | 962  | 293.6 | 22 | F#5 | 2425 | 740.1 |
| 7  | D#4 | 1020 | 311.3 | 23 | G5  | 2569 | 784.0 |
| 8  | E4  | 1080 | 329.6 | 24 | G#5 | 2722 | 830.7 |
| 9  | F4  | 1144 | 349.1 | 25 | A5  | 2884 | 880.1 |
| 10 | F#4 | 1212 | 369.9 | 26 | A#5 | 3055 | 932.3 |
| 11 | G4  | 1284 | 391.8 | 27 | B5  | 3237 | 987.9 |
| 12 | G#4 | 1361 | 415.3 | 28 | C6  | 3429 | 1046.4 |
| 13 | A4  | 1442 | 440.1 | 29 | C#6 | 3633 | 1108.7 |
| 14 | A#4 | 1528 | 466.3 | 30 | D6  | 3849 | 1174.6 |
| 15 | B4  | 1618 | 493.8 | 31 | D#6 | 4078 | 1244.5 |

**Consequences for transcription**

* Any melody must be **transposed into G#3 … D#6**; the converter rejects
  out-of-range notes with a clear error.
* 3.5 octaves is plenty for children's songs — most sit naturally in
  C4 … C6.

---

## 3. Timing (tempo / slot rate)

The slot rate comes from the watchdog timer:

```c
WDTCR = 1 << WDIE | Tempo << WDP0;   // one ISR per slot
```

The `Tempo` value selects the watchdog prescaler (`WDP`), giving the slot rate:

| `Tempo` | WDP | slot period | slots / second |
| :-----: | :-: | :---------: | :------------: |
| 0 | 0 | 16 ms   | 62.5 |
| 1 | 1 | 32 ms   | 31.25 |
| 2 | 2 | 64 ms   | 15.6 |
| 3 | 3 | 0.125 s | 8 |
| **4** | **4** | **0.25 s** | **4** ← reference default |
| 5 | 5 | 0.5 s   | 2 |
| 6 | 6 | 1.0 s   | 1 |

* The pieces are written for **`Tempo = 4`** (one slot = 0.25 s). At that rate
  a **quarter note = 2 slots** and an **eighth note = 1 slot**, i.e. ~120 BPM.
* For faster passages (16th-note runs) you can rebuild with `Tempo = 3`
  (0.125 s per slot) — the note data does not change, only the playback rate.
* The converter's `tempo:` line documents which `Tempo` a song expects; set the
  firmware `Tempo` to match.

> **Snap firmware (Phase 1).** The toy no longer uses the watchdog; it drives the
> note clock from Timer0 and takes its slot rate from the **per-song
> `SONG_<NAME>_TEMPO`** macro in `songs.h` (slots/second, `DEFAULT_TEMPO = 8`
> → 0.125 s per slot). The table above documents the reference `DigitalMusicBox.c`
> only.

A note that should last longer than one slot is written as **the note bit
repeated across consecutive slot words** (this re-triggers the sound each slot,
matching how the reference renders its long notes). Rests are zero words.

---

## 4. Source transcriptions located

Letter-note transcriptions were located online and simplified to a single
monophonic melody line (the toy's engine is at its best with one tune at a
time). Every note was then transposed into the G#3…D#6 range.

| Song | Excerpt | Source | Notes taken |
| ---- | ------- | ------ | ----------- |
| **Bluey Theme** | main theme | [pianoletternotes.blogspot.com – Bluey Theme](https://pianoletternotes.blogspot.com/2022/08/bluey-theme-song-ludo-studios-bluey.html) | `G#5 F#5 E5 D#5 … C#5 … E5 C#5` opening (their uppercase = sharp) |
| **Baby Shark** | the "doo doo" hook | [noobnotes.net – Baby Shark](https://noobnotes.net/baby-shark-pinkfong/) | `D E G G G G G G G`, ending `G G F#` |
| **Can't Stop the Feeling** | chorus | [pianokeyboardnotes.com – Can't Stop the Feeling](https://www.pianokeyboardnotes.com/cant-stop-the-feeling-piano.html) | C/D-driven chorus hook: "Nothing I can see but you / dance, dance, dance / I can't stop the feeling" |
| **Dynamite (BTS)** | verse 1 | [mintmusic.co.uk – Dynamite](https://www.mintmusic.co.uk/2020/09/dynamite-bts.html) | `Db E E E E Db E …` the "Shoes on, get up in the morn'" verse |

> ⚠ The published transcriptions vary between sources and none of them is an
> official score. These simplified lines are **starting points meant to be
> corrected by ear**. The Baby Shark and Dynamite sources line up very cleanly;
> the Bluey and *Can't Stop the Feeling* reductions are approximations and are
> the first candidates for manual tuning.

---

## 5. The converter tool

`tools/convert_song.py` implements the whole pipeline: text notes →
`Scale[]` indices → slot words → a C `PROGMEM` array.

### Input format (`songs/*.song`)

```
name:          Baby Shark
tempo:         4          # must match the firmware's Tempo
slots_per_beat: 2         # 2 -> eighth note = 1 slot (quarter = 2 slots)
melody:
    D4/0.5 E4/0.5 G4/0.5 G4/0.5     # NOTE:beats  (quarter = 1 beat)
    C4/1+E4/1+G4/1                  # '+' = simultaneous notes (chord)
    R/1                             # rest
```

* pitch: `C C# Db D D# Eb E F F# Gb G G# Ab A A# Bb B` (case-insensitive)
* octave: optional digit, default 4 (`C4` = middle C)
* duration: beats after `/` or `:` (fractions allowed)
* `R` = rest, `+` = chord

### Usage

```bash
# generate a combined header (list files explicitly so the song index
# order is stable and matches the table in section 6)
python tools/convert_song.py songs/bluey_theme.song songs/baby_shark.song \
    songs/cant_stop_the_feeling.song songs/dynamite.song -o songs.h

# inspect the decoded slot map of one song (great for verification)
python tools/convert_song.py --decode songs/baby_shark.song
```

> A shell glob (`songs/*.song`) also works, but file order is then
> alphabetical, which changes the song indices — fix the order explicitly when
> the `songIndex` values matter.

Worked example — `D4/0.5` becomes one slot with bit 25 set:

```
index = MIDI(D4) - 56 = 62 - 56 = 6  ->  bit (31-6) = 25
0b00000010000000000000000000000000   // D4
```

---

## 6. Generated songs

`songs.h` is generated by `tools/convert_punchsheet.py`, which reads the
MusicBoxPunchSheets PDFs (chord arrangements) and stores the six songs in a
compact run-length form (see below and `Punch Sheet Conversion.md`). The
`.song` melody path (`tools/convert_song.py`) is kept for hand-written
experiments.

| # | Array (`PROGMEM`) | Source sheet | Slots | RLE bytes |
| :-: | ----------------- | ------------ | :---: | :-------: |
| 0 | `SONG_BLUEY_THEME` | `bluey…GI20` | 60 | 185 |
| 1 | `SONG_BABY_SHARK` | `baby-shark…K15` | 314 | 815 |
| 2 | `SONG_DYNAMITE` | `dynamite…GI20` | 126 | 375 |
| 3 | `SONG_DANCE_OF_THE_SUGAR_PLUM_FAIRY` | `dance-of-the-sugar-plum-fairy…GI20` | 314 | 545 |
| 4 | `SONG_PACHELBELS_CANON` | `pachabels-canon…GI20` | 139 | 585 |
| 5 | `SONG_SUPER_MARIO_BROTHERS` | `super-mario-brothers…GI20` | 923 | 1490 |
|   | **Total** |  | **1876** | **3995** |

Each slot is a chord; the slot length is `1 / tempo` seconds (per-song tempo,
default `8` → 0.125 s) and the punch columns are eighth notes, i.e. one column =
one slot. Storing one `uint32_t` per slot would need 7504 bytes (over flash); the
RLE form below fits.

**Storage format.** A song is a `const uint8_t[]` stream of 5-byte records,
`[gap][chord_lo][chord_mid][chord_hi][chord_MSB]` (`gap` = rest slots before the
chord, chord little-endian), terminated by a record whose chord is
`0x000000FF` (its `gap` is a tail rest the firmware waits out before looping).
The engine expands each record back into the 32-bit slot word at play time
(Phase 1 firmware, `advanceNote()`).

**Tempo.** Each song also gets a `SONG_<NAME>_TEMPO` macro (plus a global
`DEFAULT_TEMPO = 8`) in `songs.h`, in **slots per second** (one slot = one
eighth note, so quarter-note BPM = `30 × tempo`). The firmware applies it in
`selectSong()`; the beat LED blinks at half that tempo.

```bash
python tools/convert_punchsheet.py -o Phase1_MusicBox/songs.h   # regenerate all
python tools/convert_punchsheet.py --print                      # decode & inspect
```


### Using the arrays in the sketch

The slot *word* (bit 31 = `Scale[0]` = G#3, bit 0 = `Scale[31]` = D#6) is the
same as before; only the on-flash encoding is run-length. The Phase 1 firmware
decodes it in `advanceNote()`:

```cpp
const uint8_t *volatile Tune = SONG_BABY_SHARK;   // RLE byte stream (PROGMEM)
volatile uint16_t TunePtr = 0;                    // byte cursor
volatile uint8_t  restSlots = 0;                  // rests left before the chord
volatile bool     hasPending = false;
volatile unsigned long pendingChord = 0;

static void advanceNote() {
    if (restSlots) { restSlots--; return; }
    if (hasPending) { playChord(pendingChord); hasPending = false; return; }
    uint8_t gap = pgm_read_byte(&Tune[TunePtr++]);
    unsigned long chord = pgm_read_dword(&Tune[TunePtr]); TunePtr += 4;
    if (chord == 0x000000FFUL) { TunePtr = 0; return; }      // loop to start
    if (gap) { restSlots = gap - 1; pendingChord = chord; hasPending = true; return; }
    playChord(chord);
}
```

Song switching on the **SONG** touch pad:

```cpp
switch (songIndex) {
    case 0: Tune = SONG_BLUEY_THEME;                 break;
    case 1: Tune = SONG_BABY_SHARK;                  break;
    case 2: Tune = SONG_DYNAMITE;                    break;
    case 3: Tune = SONG_DANCE_OF_THE_SUGAR_PLUM_FAIRY; break;
    case 4: Tune = SONG_PACHELBELS_CANON;            break;
    default: Tune = SONG_SUPER_MARIO_BROTHERS;       break;
}
TunePtr = 0;
```

---

## 7. How the reference tune was decoded (validation)

To be certain of the format, the "Happy Birthday" table from
`DigitalMusicBox.c` was decoded with the `Scale[]` from §2. The melody came out
as the expected tune in A♭ major:

```
slots 0,1 : D#5 D#5      ("Hap-py")
slot  2   : F5           ("birth")
slot  4   : D#5          ("day")
slot  6   : G#5          ("to")
slot  8   : G5           ("you")
```

with bass/harmony notes packed into the same words (e.g. slot 2 = `G#3 + F5`).
This confirmed the bit order, the rest encoding, the chord behaviour, and the
`f = Scale·20000/65536` tuning. The same decoder is available via
`convert_song.py --decode`.

---

## 8. Transcription checklist (for adding/tuning a song)

1. Pick the melody and **simplify to one line** (drop harmony/arpeggios).
2. Note the key; **transpose into G#3…D#6** (roughly C4…C6 is comfortable).
3. Write it in a `.song` file: `name`, `tempo`, `slots_per_beat`, then the
   `melody` as `PITCH+OCTAVE/duration_in_beats`.
4. Run `python tools/convert_song.py <file> -o songs.h` (or use `--decode`
   alone to check the slot map).
5. Set the firmware `Tempo` to the song's `tempo:` value.
6. **Listen and tune** — adjust pitches and durations until it is recognisable,
   then re-run the generator.

---

## 9. Caveats

* **Copyright.** All four are copyrighted works. Keeping short excerpts for a
  private child's toy is fine; redistributing the compiled firmware publicly
  requires licensing.
* **Monophonic.** The engine supports 4 voices and the format supports chords,
  but these songs are transcribed as a single melody line for clarity and to
  save flash. Add `+` chords for bass if wanted.
* **Timbre.** Voices are square waves with a ~0.8 s decay — bright and
  chiptune-like, which suits a toy but is not hi-fi.
* **Exactness.** The Bluey and *Can't Stop the Feeling* reductions are
  approximations (see §4). Treat all four as editable starting points.
