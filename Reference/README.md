# Reference sources

These files are **reference-only**: none of them is compiled or flashed by any
sketch in this project. They are the upstream/source material the firmware was
adapted from, kept here for future work (e.g. reusing a pattern or engine detail
in a later phase).

| File | What it is |
| ---- | ---------- |
| `DigitalMusicBox.c` | David Johnson-Davies' 4-voice DDS music engine (CC BY 4.0). The audio engine in `Phase1_MusicBox/` and `Phase2_LedMusicBox/` is adapted from it. |
| `LedAttinyTouchx2.ino` | 30-px WS2812B lamp: the `ADCTouch` calibration/debounce method and the LED patterns that Phase 2's effects are ported from. |
| `ATtiny85 NeoPixel + Capacitive Touch.md` | Notes on the NeoPixel + capacitive-touch wiring and power budget. |

Nothing else in the repository depends on these files at build time.
