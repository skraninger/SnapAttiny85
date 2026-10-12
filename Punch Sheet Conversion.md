# Punch Sheet Conversion — K15 / GI20 → song bit patterns

**How to read a musicboxmaniacs.com punch-sheet PDF and turn it into a
multi-note (chord) song for the Phase 1 music box.**

This is the companion to `Music Conversion.md`: that document covers the
hand-written melody (`.song`) path; this one covers the **punched** sheets under
`MusicBoxPunchSheets/`, which are full arrangements with chords. The whole
process is automated by `tools/convert_punchsheet.py`, but this guide records
every rule so a new sheet can be translated (by the tool, or by hand if the
exporter ever changes).

---

## 1. What a punch-sheet PDF actually is

Each PDF is an **export for a physical music box** and is **image-only** — no
text, no vector graphics (`get_text()` is empty), so it must be read as a
picture. Every band of the image is a grid:

```
                 time ---->
            +--+--+--+--+--+--+--+--+--+
   A (high) |  |  |  |X |  |  |  |  |  |
   G        |  |  |  |  |  |  |  |  |  |
   F        |  |  |X |  |  |X |  |  |  |
   ...      |  |  |  |  |  |  |  |  |  |
   C (low)  |  |  |  |  |  |  |  |  |  |
            +--+--+--+--+--+--+--+--+--+
```

* **One column = one time slot.** It is the smallest punchable step = an
  **eighth note** (see §4 for how that maps onto the firmware).
* **One row = one note of the comb**, **highest pitch at the top**.
* An **X at (column, row)** means that note sounds in that slot.
* **Several X's in one column = a chord** (this is the polyphony the toy wants).
* A **red X** is the *same* note repeated on the very next column. A mechanical
  comb cannot re-strike a tine that fast, so the exporter flags it red; a
  digital player *can* play it, and we do.
* Each page stacks several **bands**, one per ~8-bar section of the song,
  labelled `"<song> (1 of M)"`, `"(2 of M)"`, … top to bottom. All bands of one
  song must be read in order.

> The blue triangle in the top-left corner of each band is the site's "play"
> button, **not** a note — the detector skips it (any mark whose x ≤ 140 px).

---

## 2. The two combs (`K15` and `GI20`)

Both combs used here are **diatonic — the white keys only** (no sharps/flats).
The number in the name is the number of tines (rows). Pick the pattern by the
**row count**:

| Comb | Tines (rows) | Range | Left-edge letters (top → bottom) |
| :--: | :----------: | :---: | :------------------------------- |
| **K15**  | 15 | C … C (2 octaves)      | `C B A G F E D C B A G F E D C` |
| **GI20** | 20 | A … C (2.5 octaves)    | `A G F E D C B A G F E D C B A G F E D C` |

(The 30-note combs are **chromatic** and use a different layout — out of scope.)

The letters repeat every **7 rows** (C-major octave). The octave boundary is
where the descent goes **C → B** (the top of one octave down to the leading tone
of the next). Note the two combs are **the same ladder**; GI20 simply has 5 more
rows above the K15 top:

```
K15 row  i   ==   GI20 row  i + 5        (e.g. K15 row0 C == GI20 row5 C)
```

Cross-checked on *Dynamite*, which ships in **both** combs: the two sheets
encode the same melody, and GI20 additionally doubles the top line an octave up.

### Absolute pitch

The sheet labels have **no octave digits**, so the sheet does not encode an
absolute register. We adopt a fixed canonical register and then **transpose the
whole song into the toy's range** (§5). Canonical MIDI per row (before shifting):

**K15** (`row 0` = C5):

| row | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
| :-- | -:| -:| -:| -:| -:| -:| -:| -:| -:| -:| --:| --:| --:| --:| --:|
| note | C5 | B4 | A4 | G4 | F4 | E4 | D4 | C4 | B3 | A3 | G3 | F3 | E3 | D3 | C3 |
| MIDI | 72 | 71 | 69 | 67 | 65 | 64 | 62 | 60 | 59 | 57 | 55 | 53 | 52 | 50 | 48 |

**GI20** (`row 0` = A5):

| row | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 |
| :-- | -:| -:| -:| -:| -:| -:| -:| -:| -:| -:| --:| --:| --:| --:| --:| --:| --:| --:| --:| --:|
| note | A5 | G5 | F5 | E5 | D5 | C5 | B4 | A4 | G4 | F4 | E4 | D4 | C4 | B3 | A3 | G3 | F3 | E3 | D3 | C3 |
| MIDI | 81 | 79 | 77 | 76 | 74 | 72 | 71 | 69 | 67 | 65 | 64 | 62 | 60 | 59 | 57 | 55 | 53 | 52 | 50 | 48 |

A row's MIDI is just the sum of **descending diatonic steps** from the row-0
note, counting `C→B` as 1 semitone and every other adjacent pair as 2:

```
step sizes (descending, C major):  C→B 1, B→A 2, A→G 2, G→F 2, F→E 1, E→D 2, D→C 2, then repeat
```

---

## 3. Reading the grid from the image

The tool uses Pillow + a tiny amount of pixel logic (`analyse_section`):

1. **Rows** — count pixels that are "gridline grey" (`max−min < 40 and max < 175`)
   per image row. The ~20 (or ~15) image rows whose count exceeds **half the
   width** are the note lines; drop the page border at `y≈0` and the URL text at
   the bottom by keeping only `20 < y < height−20`. Their spacing is uniform
   (`row_step`).
2. **Columns** — the same test per image column. Keep only the **regular
   lattice** (median spacing, ±4 px) so the right page border and any stray line
   are discarded. All sheets share the same geometry: first line at **x = 163 px**,
   spacing **36 px**, giving **59 mark columns (0 … 58)**.
3. **Marks** — flood-fill connected **coloured** pixels (blue: `b>120 and b−r>50`;
   red: `r>120 and r−g>40 and r−b>40`), keep blobs ≥ 40 px, and skip blobs at
   x ≤ 140 (the play arrow). Each blob centroid is snapped to the nearest
   `(col, row)`: `col = round((x − x₀)/col_step)`, `row = round((y − y₀)/row_step)`.
4. **Row count** = number of detected row lines → picks the K15/GI20 pattern.

Colour (blue vs red) is recorded but does **not** change the output — both are
notes.

---

## 4. Stitching the bands into one song

The x-axis of every band is labelled in **absolute bar numbers**, e.g. band 1
shows `3.0 … 8.0`, band 2 shows `10.0 … 15.0`, and the last Baby Shark band
shows `39.0 … 44.0`. Two facts fall out:

* **1 bar = 8 columns** (4/4, eighth-note grid): the labels are 288 px apart and
  columns are 36 px apart → `288 / 36 = 8`.
* Consecutive bands are offset by exactly **58 columns** (7.25 bars):
  `band k column 0  ==  band k−1 column 58` — the boundary column is drawn in
  both bands, and they match exactly.

So a whole song is rebuilt by **unioning** each band's marks at absolute column
counts:

```
absolute_column = 58 * band_index + column        # band_index = 0,1,2,…
```

The result is one long sequence of columns, each holding a set of rows (the
chord for that slot). (A quicker sanity check: the phrase repeats every 64
columns, so consecutive bands align at a shift of `58 − 64 = −6`, which is
exactly what a cross-correlation of the mark sets returns.)

---

## 5. Turning columns into the toy's bit patterns

The engine plays one `uint32_t` **per time slot**; a punch column is an eighth
note and maps 1:1 onto a slot, so:

> **one punch column = exactly one firmware slot.**

The slot length is set by the song tempo (slots/second): 0.125 s at the default
`tempo = 8`. Each song carries a `SONG_<NAME>_TEMPO` macro in `songs.h`
(`DEFAULT_TEMPO = 8`); `selectSong()` applies it, and the beat LED blinks at half
that tempo.

Mapping a note to its bit:

```
MIDI 56 = G#3 = Scale[0];   MIDI 87 = D#6 = Scale[31]
note_index = MIDI − 56                     # 0 … 31, must be in range
bit        = 31 − note_index = 87 − MIDI   # bit 31 = MSB = G#3 … bit 0 = D#6
word(column) = OR of (1 << bit) for every note in that column
```

* An empty column → `0x00000000` (rest).
* The end of a tune is the sentinel chord `0x000000FF`.
* The firmware has **`VOICES = 4`** DDS voices; a column may contain at most 4
  notes (the sheets here top out at exactly 4).

### Storage: run-length encoding (RLE)

An uncompressed `uint32_t` per slot needs **7504 bytes** for the six songs — too
much for the ATtiny85's 8 KB flash once the firmware is included. Because most
slots are rests (e.g. Super Mario Bros. has 922 slots but only 297 sound),
`songs.h` stores each song as a byte stream of

```
[gap][chord0][chord1][chord2][chord3]      // gap = rest slots before the chord
```

read little-endian (`gap` = one byte, chord = four bytes little-endian). A
record costs **5 bytes** and stands for `gap + 1` slots; a final record with
chord `0x000000FF` ends the tune, and its `gap` is extra rest slots the firmware
waits out before looping (a song with no tail rest has `gap = 0`). The firmware's
`advanceNote()` (`playChord()` + RLE walk) decodes this on the fly: one byte
gap, then a 32-bit chord, holding the pending chord while its rest gap elapses
and looping at the END record. This shrinks the songs to **3995 bytes** and the
whole sketch to **6614 bytes (80%)**.

> To add a pause before a song repeats, set an entry in `TAIL_RESTS` in
> `tools/convert_punchsheet.py` — e.g. `{"Bluey Theme": 1}` appends one extra
> silent slot at the end of Bluey.

### Transposition to the playable range

Because the sheet has no absolute octave, each song is shifted to fit G#3…D#6
(MIDI 56…87): an **octave shift (keeps the key)** is preferred, else the shift
that **drops the fewest note occurrences**. Applied shifts in the current build:

| Song | Comb | Shift | Resulting MIDI | Notes |
| :--- | :--: | :---: | :------------- | :---- |
| Bluey Theme | GI20 | +12 | 60 … 81 | |
| Baby Shark | K15 | +12 | 60 … 84 | |
| Dynamite | GI20 | +4 | 56 … 85 | span 30 semitones, no octave shift fits |
| Dance of the Sugar Plum Fairy | GI20 | +8 | 56 … 84 | |
| Pachelbels Canon | GI20 | +12 | 60 … 79 | |
| Super Mario Brothers | GI20 | +8 | 56 … 87 | uses all 20 rows; 10 top-note hits dropped |

*(Can't Stop the Feeling was removed — it had no punch sheet.)*

---

## 6. Using the tool

```bash
pip install pymupdf pillow          # only needed for the punch-sheet path

# Rebuild the four Phase 1 songs (order = playback/EEPROM order):
python tools/convert_punchsheet.py -o Phase1_MusicBox/songs.h
copy Phase1_MusicBox\songs.h songs.h

# Decode/inspect without writing anything (great for verification):
python tools/convert_punchsheet.py --print

# Convert one arbitrary new sheet:
python tools/convert_punchsheet.py --pdf "MusicBoxPunchSheets/my_song_GI20_us-letter.pdf" \
    --name "My Song" --print
```

The tool prints one line per song to stderr, e.g.
`Baby Shark  314 slots  815 bytes RLE  6 sheets, 15-note comb, shift +12, MIDI 60..84, max chord 4`.

---

## 7. Adding a new sheet — step by step

1. **Drop the exported PDF** into `MusicBoxPunchSheets/`, keeping the
   `…_K15_…` / `…_GI20_…` part of the filename (it identifies the comb).
2. **Open it** and check the title(s). Confirm every `(N of M)` section is
   present and that `M` sections exist; the tool reads them top-to-bottom.
3. **Identify the comb** by counting the rows / reading the left-edge letters:
   15 → K15, 20 → GI20. (The tool does this automatically and errors on anything
   else.)
4. **Dry-run decode** and read the slot map:
   ```bash
   python tools/convert_punchsheet.py --pdf MusicBoxPunchSheets/NEW.pdf --name "New" --print
   ```
   Sanity-check: the melody/rhythm should be humanly recognisable, chords should
   land on strong beats, and no note should read as out-of-range.
5. **Register the song** in `SONG_PLAN` in `tools/convert_punchsheet.py`
   (position defines the `SONG_*` array and playback order — keep it stable so
   EEPROM `songIndex` keeps meaning the same thing).
6. **Regenerate + copy** `songs.h`, then **compile**:
   ```bash
   python tools/convert_punchsheet.py -o Phase1_MusicBox/songs.h
   copy Phase1_MusicBox\songs.h songs.h
   arduino-cli compile --fqbn attiny:avr:ATtiny85:cpu=attiny85,clock=internal8 Phase1_MusicBox
   ```
7. **Listen and tune** if needed (the sheet is the source of truth, but the
   comb's exact tuning is approximated by `Scale[]`).

---

## 8. Gotchas & limitations

* **Red repeats.** We treat every X as a note, so an `A A` (second one red)
  becomes two adjacent `A` slots. If you would rather hear a single held note,
  merge a red mark into the previous column instead of emitting it.
* **No absolute octave.** The sheet doesn't say which octave the comb is in, so
  every song is (re)tuned to fit the toy; this only changes the key/register,
  never the melody or the intervals.
* **4-voice ceiling.** A column with >4 notes would overwrite voices. The current
  sheets peak at 4; a richer arrangement would need `VOICES` raised in the
  firmware (more RAM/CPU) or the extra notes dropped.
* **Flash.** The RLE storage above is what lets all six songs fit the ATtiny85's
  8 KB. A longer added sheet can still overflow; watch the `Sketch uses …`
  bytes line on compile.
* **Exporter assumptions.** The pixel thresholds (`is_gridline`, `is_mark`, the
  `>0.5·width/height` line test, `36 px` column spacing, `58`-column stride) fit
  the musicboxmaniacs export used here. A different exporter (different colours,
  spacing, or a chromatic 30-note sheet) needs those constants revisited.
* **Copyright.** These are copyrighted tunes; short excerpts in a private toy are
  fine, but redistributing the compiled firmware publicly needs licensing.

---

## 9. Reference

| Item | Value |
| ---- | ----- |
| Source | musicboxmaniacs.com exported punch-sheet PDFs |
| Combs | K15 = 15-tone diatonic (C…C); GI20 = 20-tone diatonic (A…C) |
| Grid | 1 column = 1 eighth note; 1 row = 1 comb note (top = highest) |
| Columns per band | 59 (indices 0…58); band stride = **58** columns |
| Bars per band | 7.25 (1 bar = 8 columns) |
| Firmware slot | 1 column = 1 slot; slot length = `1 / tempo` s (default `tempo = 8` ⇒ 0.125 s) |
| Bit mapping | `bit = 87 − MIDI`; rest = `0x00000000`; end = `0x000000FF` |
| Storage | RLE `[gap][chord LE ×4]`, 5 bytes/record, END = `0x000000FF` |
| Voices | 4 (`VOICES` in the sketch) |
| Tool | `tools/convert_punchsheet.py` |
