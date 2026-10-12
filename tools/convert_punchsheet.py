#!/usr/bin/env python3
"""
convert_punchsheet.py -- Build the ATtiny85 "Digital Music Box" song header
(`songs.h`) from MusicBoxPunchSheets PDFs, so the toy plays the *punched*
multi-note arrangements instead of hand-simplified single lines.

WHAT THE PDFs ARE
-----------------
Each PDF is a musicboxmaniacs.com punch sheet exported for a physical music box
comb.  A sheet is a raster image (no text/vectors) laid out as a grid:

  * one COLUMN = one time slot (the smallest punchable step = an eighth note);
  * one ROW    = one note of the comb, HIGHEST pitch at the top;
  * an "X" mark at (column,row) = that note sounds in that slot;
  * several X's in the same column = a chord (polyphony);
  * a page holds several BANDS, one per 8-bar section of the song
    ("... (1 of 6)", "(2 of 6)" ...), stacked top to bottom.

Two combs appear here, both DIATONIC (white keys only):

  * K15  (Kikkerland 15-note)  : 15 rows, C .. C  (two octaves)
  * GI20 (Grand Illusions 20)  : 20 rows, A .. C  (2.5 octaves)

Cross-checking the Dynamite sheets (available in both combs) shows they encode
the same melody; GI20 additionally doubles the top line an octave up.

HOW SECTIONS STITCH
-------------------
The x-axis of every band is labelled in ABSOLUTE bar numbers (1 bar = 8 columns
= 4/4 at eighth-note resolution).  Consecutive bands of one song are offset by
exactly 58 columns (7.25 bars): band k column 0 == band k-1 column 58.  So a
whole song is reconstructed by placing band k at absolute column offset 58*k
and unioning the one shared boundary column.

OUTPUT
------
`songs.h`: one `const uint8_t SONG_*[] PROGMEM` per song, in a small
**run-length format** the Phase 1 firmware decodes in `advanceNote()`:

    [gap][chord_lo][chord_mid][chord_hi][chord_MSB]   (gap = rest slots before the chord)

A chord is the same 32-bit word as before (bit 87-MIDI selects a note), rest
slots are collapsed into the one-byte `gap`, and a final record with chord
`0x000000FF` ends the song; its `gap` is extra rest slots the firmware waits out
before looping.  This shrinks the six songs from ~7.5 KB to ~4 KB so they fit
the ATtiny85's 8 KB flash.

"Dynamite" is taken from the GI20 sheet (richer); all songs now come from punch
sheets (Can't Stop the Feeling was removed).

DEPENDENCIES
------------
    pip install pymupdf pillow

USAGE
-----
    python tools/convert_punchsheet.py -o Phase1_MusicBox/songs.h
    python tools/convert_punchsheet.py --print        # dump decoded slot maps
"""

from __future__ import annotations

import argparse
import glob
import io
import os
import re
import sys
from collections import Counter

from PIL import Image

try:                      # PyMuPDF >= 1.24 exposes the "pymupdf" name
    import pymupdf as _pdf
except ImportError:       # older releases only expose "fitz"
    import fitz as _pdf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert_song  # noqa: E402  (same directory)

# --- Player constants (mirror the firmware / convert_song.py) ---------------
SCALE_BASE_MIDI = 56           # Scale[0] = G#3 = MIDI 56
N_NOTES = 32                   # Scale[] spans G#3 .. D#6 (MIDI 56..87)
END_MARKER = 0x000000FF

# --- Punch-sheet geometry ---------------------------------------------------
SECTION_STRIDE = 58            # columns between consecutive band origins
ROWS_TO_LETTERS = {            # top row .. bottom row, descending diatonic
    15: ["C", "B", "A", "G", "F", "E", "D"],   # K15
    20: ["A", "G", "F", "E", "D", "C", "B"],   # GI20
}
ROW0_MIDI = {15: 72, 20: 81}   # arbitrary register; songs are transposed to fit
_SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

# --- The four Phase 1 songs, in EEPROM/playback order -----------------------
# Each entry is (display name, source file/glob).  The array + tempo macro are
# generated from the name.  Per-song playback speeds live in TEMPO_OVERRIDES.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PUNCH_DIR = os.path.join(ROOT, "MusicBoxPunchSheets")
SONG_PLAN = [
    ("Bluey Theme",                    os.path.join(PUNCH_DIR, "bluey-song*_GI20_*.pdf")),
    ("Baby Shark",                     os.path.join(PUNCH_DIR, "baby-shark*_K15_*.pdf")),
    ("Dynamite",                       os.path.join(PUNCH_DIR, "dynamite-bts*_GI20_*.pdf")),
    ("Dance of the Sugar Plum Fairy",  os.path.join(PUNCH_DIR, "dance-of-the-sugar-plum-fairy*.pdf")),
    ("Pachelbels Canon",               os.path.join(PUNCH_DIR, "pachabels-canon*.pdf")),
    ("Super Mario Brothers",           os.path.join(PUNCH_DIR, "super-mario-brothers*.pdf")),
]

# Tempo is in SLOTS PER SECOND (one slot = one eighth note, so quarter-note
# BPM = 30 * tempo).  Every song defaults to DEFAULT_TEMPO; add an entry here
# to give a specific song its own speed, e.g.  "Baby Shark": 6,
DEFAULT_TEMPO = 8
TEMPO_OVERRIDES: dict[str, int] = {}

# Extra silent slots appended before a song loops.  The END record already
# contributes one silent slot, so this adds to that; e.g. 1 = a longer pause
# between repeats.  Add per-song entries as needed.
TAIL_RESTS: dict[str, int] = {"Bluey Theme": 1}


# ---------------------------------------------------------------------------
#  PDF -> per-section images (in display order)
# ---------------------------------------------------------------------------

def extract_section_images(pdf_path: str) -> list[Image.Image]:
    """Return each stacked band of a punch-sheet PDF as a PIL image, top first."""
    doc = _pdf.open(pdf_path)
    images: list[Image.Image] = []
    try:
        for pno in range(doc.page_count):
            page = doc[pno]
            placed = []
            for info in page.get_images(full=True):
                xref = info[0]
                rects = page.get_image_rects(xref)
                y0 = rects[0].y0 if rects else 0.0
                x0 = rects[0].x0 if rects else 0.0
                placed.append((y0, x0, xref))
            placed.sort()
            for _, _, xref in placed:
                raw = doc.extract_image(xref)["image"]
                images.append(Image.open(io.BytesIO(raw)).convert("RGB"))
    finally:
        doc.close()
    return images


# ---------------------------------------------------------------------------
#  Image -> grid + marks
# ---------------------------------------------------------------------------

def _is_gridline(px) -> bool:
    r, g, b = px
    return max(r, g, b) - min(r, g, b) < 40 and max(r, g, b) < 175


def _is_mark(px) -> bool:
    r, g, b = px
    blue = b > 120 and b - r > 50
    red = r > 120 and r - g > 40 and r - b > 40
    return blue or red


def _line_positions(profile: list[int], need: float) -> list[float]:
    idx = [i for i, v in enumerate(profile) if v > need]
    groups: list[list[int]] = []
    for i in idx:
        if groups and i - groups[-1][-1] <= 2:
            groups[-1].append(i)
        else:
            groups.append([i])
    return [(g[0] + g[-1]) / 2 for g in groups]


def _regular_lattice(positions: list[float], tol: float = 4.0) -> tuple[list[float], float]:
    """Keep the longest arithmetic run so borders/outliers are dropped."""
    if len(positions) < 2:
        return positions, 1.0
    diffs = sorted(positions[i + 1] - positions[i] for i in range(len(positions) - 1))
    step = diffs[len(diffs) // 2]
    start = positions[0]
    kept = [start]
    for p in positions[1:]:
        if abs((p - start) - step * len(kept)) <= tol:
            kept.append(p)
    return kept, step


def analyse_section(im: Image.Image) -> tuple[int, dict[int, set[int]]]:
    """Return (number_of_rows, {column: {row, ...}})."""
    w, h = im.size
    px = im.load()

    rows_raw = _line_positions(
        [sum(1 for x in range(w) if _is_gridline(px[x, y])) for y in range(h)], w * 0.5)
    rows = [y for y in rows_raw if 20 < y < h - 20]
    row_step = (rows[-1] - rows[0]) / (len(rows) - 1) if len(rows) > 1 else 1.0

    cols_raw = _line_positions(
        [sum(1 for y in range(h) if _is_gridline(px[x, y])) for x in range(w)], h * 0.5)
    cols = [x for x in cols_raw if 100 < x < w - 20]
    cols, col_step = _regular_lattice(cols)
    if len(cols) < 2:
        raise RuntimeError("grid not detected")

    # connected components of mark pixels
    seen = [[False] * w for _ in range(h)]
    cells: dict[int, set[int]] = {}
    for y0 in range(h):
        for x0 in range(w):
            if seen[y0][x0] or not _is_mark(px[x0, y0]):
                continue
            stack = [(x0, y0)]
            seen[y0][x0] = True
            pts = []
            while stack:
                x, y = stack.pop()
                pts.append((x, y))
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx] and _is_mark(px[nx, ny]):
                            seen[ny][nx] = True
                            stack.append((nx, ny))
            if len(pts) < 40 or max(p[0] for p in pts) <= 140:   # skip the play arrow
                continue
            cx = sum(p[0] for p in pts) / len(pts)
            cy = sum(p[1] for p in pts) / len(pts)
            col = round((cx - cols[0]) / col_step)
            row = round((cy - rows[0]) / row_step)
            cells.setdefault(col, set()).add(row)
    return len(rows), cells


# ---------------------------------------------------------------------------
#  Notes
# ---------------------------------------------------------------------------

def _row_offsets(letters: list[str], count: int) -> list[int]:
    off = [0]
    for i in range(1, count):
        drop = (_SEMI[letters[(i - 1) % len(letters)]] - _SEMI[letters[i % len(letters)]]) % 12 or 12
        off.append(off[-1] + drop)
    return off


def row_to_midi(n_rows: int, row: int) -> int:
    letters = ROWS_TO_LETTERS[n_rows]
    return ROW0_MIDI[n_rows] - _row_offsets(letters, n_rows + 1)[row]


def choose_shift(counts: dict[int, int]) -> tuple[int, int]:
    """Pick the transposition that fits the tune into G#3..D#6 (MIDI 56..87).

    Returns (shift, dropped).  The scan minimises the number of note
    *occurrences* pushed out of range (some 20-note combs span a little more
    than the toy's 31 semitones, so the extreme note has to be sacrificed);
    ties prefer a whole-octave shift (keeps the key), then the smallest shift.
    """
    lo_ok, hi_ok = SCALE_BASE_MIDI, SCALE_BASE_MIDI + N_NOTES - 1
    best = None
    for shift in range(-36, 37):
        dropped = sum(c for m, c in counts.items() if not (lo_ok <= m + shift <= hi_ok))
        key = (dropped, 0 if shift % 12 == 0 else 1, abs(shift), shift)
        if best is None or key < best[0]:
            best = (key, shift, dropped)
    return best[1], best[2]


def build_words_from_pdf(pdf_path: str) -> tuple[list[int], dict]:
    sections = extract_section_images(pdf_path)
    absolute: dict[int, set[int]] = {}
    n_rows = 0
    for k, im in enumerate(sections):
        n_rows_s, cells = analyse_section(im)
        n_rows = max(n_rows, n_rows_s)
        for col, rows in cells.items():
            absolute.setdefault(SECTION_STRIDE * k + col, set()).update(rows)

    if n_rows not in ROWS_TO_LETTERS:
        raise RuntimeError(f"unexpected row count {n_rows} for {os.path.basename(pdf_path)}")

    last = max(absolute)
    counts = Counter(row_to_midi(n_rows, r) for rws in absolute.values() for r in rws)
    shift, dropped = choose_shift(counts)

    words: list[int] = []
    for ac in range(last + 1):
        word = 0
        for r in absolute.get(ac, ()):
            idx = row_to_midi(n_rows, r) + shift - SCALE_BASE_MIDI
            if 0 <= idx < N_NOTES:
                word |= 1 << (N_NOTES - 1 - idx)
        words.append(word)
    words.append(END_MARKER)

    midis = list(counts)
    info = {
        "sections": len(sections),
        "rows": n_rows,
        "columns": last + 1,
        "shift": shift,
        "dropped": dropped,
        "midi_lo": min(m + shift for m in midis if SCALE_BASE_MIDI <= m + shift <= SCALE_BASE_MIDI + N_NOTES - 1),
        "midi_hi": max(m + shift for m in midis if SCALE_BASE_MIDI <= m + shift <= SCALE_BASE_MIDI + N_NOTES - 1),
        "max_chord": max(len(v) for v in absolute.values()),
    }
    return words, info


def build_words_from_melody(song_path: str) -> tuple[list[int], dict]:
    meta = convert_song.parse_song(song_path)
    words = convert_song.encode(meta)
    return words, {"source": "melody", "columns": len(words) - 1}


def words_to_records(words: list[int], tail: int = 0) -> list[tuple[int, int]]:
    """Compress a slot-word list into [(rest_gap, chord), ...] records.

    Trailing rests are collapsed into the final END record's gap, which the
    firmware waits out before looping; `tail` adds extra silent slots there.
    Long gaps are split with filler records so the gap field stays one byte
    (not needed by the current sheets, whose longest rest run is 9).
    """
    body, end = words[:-1], words[-1]
    records: list[tuple[int, int]] = []
    gap = 0
    for w in body:
        if w == 0:
            gap += 1
            continue
        while gap > 255:
            records.append((255, 0))
            gap -= 256
        records.append((gap, w))
        gap = 0
    gap += tail
    while gap > 255:
        records.append((255, 0))
        gap -= 256
    records.append((gap, end))
    return records


def c_bytes_array(name: str, records: list[tuple[int, int]]) -> tuple[str, str]:
    ident = "SONG_" + re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").upper()
    slots = sum(gap + 1 for gap, _ in records)
    lines = [f"// {slots} slots -> {len(records) * 5} bytes (RLE)"]
    lines.append(f"const uint8_t {ident}[] PROGMEM = {{")
    for gap, word in records:
        b = word.to_bytes(4, "little")
        notes = convert_song.word_to_notes(word)
        if word == END_MARKER:
            comment = "END"
        elif gap:
            comment = f"rest x{gap}; {notes}"
        else:
            comment = notes
        lines.append(f"  0x{gap:02X}, 0x{b[0]:02X}, 0x{b[1]:02X}, 0x{b[2]:02X}, 0x{b[3]:02X},  // {comment}")
    lines.append("};")
    return ident, "\n".join(lines)


# ---------------------------------------------------------------------------
#  Main
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", default=None, help="write combined header here")
    ap.add_argument("--print", dest="dump", action="store_true",
                    help="print the decoded slot map of every song")
    ap.add_argument("--pdf", default=None,
                    help="convert a single punch-sheet PDF instead of the built-in plan")
    ap.add_argument("--name", default=None,
                    help="array/song name to use with --pdf (default: from the filename)")
    args = ap.parse_args(argv)

    if args.pdf:
        base = re.sub(r"[^A-Za-z0-9]+", " ", os.path.splitext(os.path.basename(args.pdf))[0]).strip()
        plan = [(args.name or base, args.pdf)]
    else:
        plan = SONG_PLAN

    blocks, idents = [], []
    for name, source in plan:
        matches = glob.glob(source)
        if not matches:
            print(f"!! missing source for {name}: {source}", file=sys.stderr)
            return 1
        path = sorted(matches)[0]
        if path.lower().endswith(".pdf"):
            words, info = build_words_from_pdf(path)
            desc = (f"{info['sections']} sheets, {info['rows']}-note comb, "
                    f"shift {info['shift']:+d}, MIDI {info['midi_lo']}..{info['midi_hi']}, "
                    f"max chord {info['max_chord']}"
                    + (f", DROPPED {info['dropped']}" if info["dropped"] else ""))
        else:
            words, info = build_words_from_melody(path)
            desc = "melody file (no punch sheet)"
        records = words_to_records(words, TAIL_RESTS.get(name, 0))
        tempo = TEMPO_OVERRIDES.get(name, DEFAULT_TEMPO)
        ident, block = c_bytes_array(name, records)
        block += (f"\n#define {ident}_TEMPO {tempo}"
                  f"   // slots/second; quarter-BPM = 30*this (edit to taste)\n")
        idents.append(ident)
        blocks.append(block)
        print(f"{name:28s} {len(words):4d} slots  {len(records)*5:5d} bytes RLE  tempo {tempo}  {desc}",
              file=sys.stderr)
        if args.dump:
            print(f"== {name} ({desc}) ==")
            for i, w in enumerate(words):
                print(f"  {i:3d}: {convert_song.word_to_bits(w)}  {convert_song.word_to_notes(w)}")
            print()

    if args.dump:
        return 0

    out = ["// AUTO-GENERATED by tools/convert_punchsheet.py -- do not edit by hand.",
           "// RLE slot stream: [gap][chord LE x4] records; gap = rest slots before the chord;",
           "// a chord of 0x000000FF ends the tune and its gap is extra rests before looping.",
           "//",
           "// Playback tempo is in SLOTS PER SECOND (one slot = one eighth note, so",
           "// quarter-note BPM = 30 * tempo).  Each song has a SONG_<NAME>_TEMPO macro;",
           "// the firmware uses it when that song is selected.",
           f"#define DEFAULT_TEMPO {DEFAULT_TEMPO}",
           "#pragma once",
           "#include <stdint.h>",
           "#include <avr/pgmspace.h>",
           ""]
    out.extend(blocks)
    out.append("")
    out.append(f"#define NUM_SONGS {len(idents)}")
    text = "\n".join(out) + "\n"

    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print(f"wrote {args.out}", file=sys.stderr)
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
