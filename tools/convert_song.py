#!/usr/bin/env python3
"""
convert_song.py -- Convert simple letter-note melodies into the ATtiny85
"Digital Music Box" chord-word format used by the Snap toy.

TARGET FORMAT (from DigitalMusicBox.c)
--------------------------------------
  * A tune is a `const uint32_t[]` stored in PROGMEM.
  * Each element is ONE TIME SLOT (one Watchdog-interrupt period).
  * Within the 32-bit word, bit `n` selects note `Scale[n]`:
        note 0  = bit 31 (0x80000000)  = Scale[0] = G#3  (MIDI 56)
        note 31 = bit 0  (0x00000001)  = Scale[31]= D#6  (MIDI 87)
  * A slot word of 0x00000000 is a rest.
  * The tune is terminated by the sentinel word 0x000000FF.
  * The playback rate is `Tempo` slots/second (WDT prescaler). The reference
    firmware uses Tempo = 4  ->  0.25 s per slot.

INPUT NOTATION (.song text file)
--------------------------------
    name:          Bluey Theme
    tempo:         4          # slots per second (player Tempo)
    slots_per_beat: 2         # slots per quarter-note (2 -> eighth = 1 slot)
    melody:
        G#5/0.5 F#5/0.5 E5/0.5 D#5/1.5 R/1.5
        C4/1+E4/1+G4/1        # '+' = chord (simultaneous notes)

  Token grammar:   <pitch><octave>[: or /]<beats>
      pitch   : C C# Db D D# Eb E F F# Gb G G# Ab A A# Bb B   (case-insensitive)
      octave  : optional digit, default 4                       (C4 = middle C)
      beats   : duration in beats (quarter = 1). Fractions allowed.  R = rest.
      '+'     : joins notes to sound together.

  Notes must land inside the player's range G#3..D#6.

USAGE
-----
    python tools/convert_song.py songs/*.song -o songs.h
    python tools/convert_song.py --decode songs/bluey_theme.song   # show slot map
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import sys

# --- Player constants -------------------------------------------------------
SCALE_BASE_MIDI = 56          # Scale[0] = G#3 = MIDI 56
N_NOTES = 32                  # Scale[] has 32 entries
END_MARKER = 0x000000FF

# Semitone offsets, including enharmonic spellings.
_SEMI = {
    "C": 0, "C#": 1, "DB": 1, "D": 2, "D#": 3, "EB": 3, "E": 4, "FB": 4,
    "F": 5, "E#": 5, "F#": 6, "GB": 6, "G": 7, "G#": 8, "AB": 8, "A": 9,
    "A#": 10, "BB": 10, "B": 11, "CB": 11,
}
_NOTE_RE = re.compile(r"^([A-Ga-g][#bB]?)(-?\d)?$")


def pitch_to_midi(pitch: str, octave: int) -> int:
    """Return MIDI note number for e.g. pitch='Db', octave=5 -> 73."""
    key = pitch.upper()
    if key not in _SEMI:
        raise ValueError(f"unknown pitch {pitch!r}")
    # MIDI: C-1 = 0 ; C4 = 60  =>  midi = (octave+1)*12 + semitone
    return (octave + 1) * 12 + _SEMI[key]


def midi_to_index(midi: int) -> int:
    idx = midi - SCALE_BASE_MIDI
    if not (0 <= idx < N_NOTES):
        lo, hi = SCALE_BASE_MIDI, SCALE_BASE_MIDI + N_NOTES - 1
        raise ValueError(
            f"note MIDI {midi} is outside the playable range "
            f"(MIDI {lo}..{hi}); transcribe it to a different octave"
        )
    return idx


def parse_token(tok: str) -> tuple[list[int], float]:
    """Parse one token -> (list_of_note_indices, beats)."""
    if ":" in tok:
        body, beats_s = tok.rsplit(":", 1)
    elif "/" in tok:
        body, beats_s = tok.rsplit("/", 1)
    else:
        body, beats_s = tok, "1"
    beats = float(beats_s)
    indices: list[int] = []
    for part in body.split("+"):
        part = part.strip()
        if part.upper() == "R":
            continue  # rest -> no notes
        m = _NOTE_RE.match(part)
        if not m:
            raise ValueError(f"bad note token {part!r}")
        pitch, octave = m.group(1), m.group(2)
        octv = int(octave) if octave is not None else 4
        indices.append(midi_to_index(pitch_to_midi(pitch, octv)))
    return indices, beats


def parse_song(path: str) -> dict:
    meta = {"name": os.path.splitext(os.path.basename(path))[0],
            "tempo": 4, "slots_per_beat": 2}
    tokens: list[str] = []
    in_melody = False
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            # '#' starts a comment only at line start or after whitespace, so
            # that sharp note names such as C#5 / F#5 are preserved.
            cut = len(raw)
            for i, ch in enumerate(raw):
                if ch == "#" and (i == 0 or raw[i - 1].isspace()):
                    cut = i
                    break
            line = raw[:cut].strip()
            if not line:
                continue
            low = line.lower()
            if low.startswith("name:"):
                meta["name"] = line.split(":", 1)[1].strip()
                in_melody = False
            elif low.startswith("tempo:"):
                meta["tempo"] = float(line.split(":", 1)[1])
                in_melody = False
            elif low.startswith("slots_per_beat:"):
                meta["slots_per_beat"] = float(line.split(":", 1)[1])
                in_melody = False
            elif low.startswith("melody:"):
                in_melody = True
                rest = line.split(":", 1)[1].strip()
                if rest:
                    tokens += rest.split()
            elif in_melody:
                tokens += line.split()
    meta["tokens"] = tokens
    return meta


def encode(meta: dict) -> list[int]:
    spb = meta["slots_per_beat"]
    words: list[int] = []
    for tok in meta["tokens"]:
        indices, beats = parse_token(tok)
        slots = max(1, int(round(beats * spb)))
        word = 0
        for idx in indices:
            word |= 1 << (31 - idx)
        words.extend([word] * slots)   # retrigger each slot (matches reference)
    words.append(END_MARKER)
    return words


def word_to_bits(word: int) -> str:
    return format(word & 0xFFFFFFFF, "032b")


def word_to_notes(word: int) -> str:
    if word == END_MARKER:
        return "END"
    names = _scale_names()
    if word == 0:
        return "rest"
    return " ".join(names[n] for n in range(N_NOTES) if word & (1 << (31 - n)))


def _scale_names() -> list[str]:
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    out = []
    for i in range(N_NOTES):
        midi = SCALE_BASE_MIDI + i
        out.append(f"{names[midi % 12]}{midi // 12 - 1}")
    return out


def c_array(name: str, words: list[int]) -> str:
    ident = "SONG_" + re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").upper()
    lines = [f"// {meta_bytes(words)} bytes  ({len(words)} slots)"]
    lines.append(f"const uint32_t {ident}[] PROGMEM = {{")
    for w in words:
        comment = word_to_notes(w)
        lines.append(f"  0b{word_to_bits(w)},  // {comment}")
    lines.append("};")
    return ident, "\n".join(lines)


def meta_bytes(words: list[int]) -> int:
    return len(words) * 4


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("songs", nargs="+", help=".song files or globs")
    ap.add_argument("-o", "--out", help="write combined C header here")
    ap.add_argument("--decode", action="store_true",
                    help="print the decoded slot map instead of C code")
    args = ap.parse_args(argv)

    paths: list[str] = []
    for pat in args.songs:
        matches = glob.glob(pat)
        paths.extend(matches if matches else [pat])

    blocks: list[str] = []
    idents: list[str] = []
    for path in paths:
        meta = parse_song(path)
        words = encode(meta)
        if args.decode:
            print(f"== {meta['name']}  ({meta_bytes(words)} bytes, "
                  f"tempo={meta['tempo']}/s, {meta['slots_per_beat']} slots/beat) ==")
            for i, w in enumerate(words):
                print(f"  {i:3d}: {word_to_bits(w)}  {word_to_notes(w)}")
            print()
            continue
        ident, block = c_array(meta["name"], words)
        idents.append(ident)
        blocks.append(block)
        print(f"{meta['name']:28s} {len(words):4d} slots  {meta_bytes(words):5d} bytes",
              file=sys.stderr)

    if args.decode:
        return 0

    out = ["// AUTO-GENERATED by tools/convert_song.py -- do not edit by hand.",
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
