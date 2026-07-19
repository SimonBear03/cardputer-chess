# Validation

This page records the current software validation baseline. It is written for
contributors who want to verify a change before flashing or publishing it.

## Current result at a glance

| Check | Result |
| --- | --- |
| Host rules and engine tests | 217 assertions passed |
| AddressSanitizer and UBSan | 217 assertions passed |
| Cardputer-Adv firmware build | Passed |
| Static RAM | 176,084 / 327,680 bytes (53.7%) |
| Flash image | 553,029 / 3,342,336 bytes (16.5%) |
| USB upload | Passed with verified hashes and hard reset |

These results support the checked-in code, but they do not replace looking at
the real LCD and pressing the physical keys.

## Host rules and engine tests

Run:

```sh
make test
```

Current result: **217 assertions pass**. The suite includes the six standard
perft positions, with start position and endgame coverage extended to depth 4.
It also covers:

- FEN parsing and coordinate conversion
- make/unmake and Zobrist-key identity
- castling, including castling through check
- en passant, including a pinned pawn and repetition-key semantics
- all four promotions
- checkmate, stalemate, fifty-move, threefold-repetition, and dead-position draws
- level monotonicity, opening-book legality, mate-in-one search, and bounded stop
- expanded-book coverage through a Berlin Defense mainline
- SAN notation, including capture, disambiguation, castling, promotion, check,
  and checkmate
- exact three-line MultiPV legality, uniqueness, order, PV shape, and caller
  position identity
- Coach mode naming and honest move-quality classification

Run the same suite with memory and undefined-behavior instrumentation:

```sh
make test-sanitize
```

Current result: **217 assertions pass under AddressSanitizer and UBSan**.
LeakSanitizer is disabled because it requires ptrace support that is unavailable
in some sandboxed runners.

## Firmware build

Run:

```sh
platformio run
```

The project pins Espressif32 platform 6.12.0 and the M5Cardputer Git tag 1.2.0.
The current release build succeeds for the ESP32-S3FN8 target with:

- Static RAM: 176,084 / 327,680 bytes (53.7%)
- Flash image: 553,029 / 3,342,336 bytes (16.5%)
- Output: `.pio/build/cardputer-adv/firmware.bin`

The 64 KiB transposition table is allocated once at runtime and is therefore
not included in PlatformIO's static-RAM figure. Search uses a fixed 16 KiB
FreeRTOS task stack and fixed-capacity working arrays; it does not allocate in
the recursive search path. Opponent and Coach analysis share the same engine
and task rather than allocating a second search instance.

The UI keeps its large reusable `Position` objects in static application
storage, including the notation copy used by the Coach overlay. This avoids
overflowing the Arduino loop task while formatting a continuation line. All
three themes share the same 14×14 two-color piece masks. Theme selection, board
coordinates, and animations do not require a framebuffer or dynamic allocation.

## Independent cross-check

The canonical perft FENs and expected leaf counts were cross-checked against an
independent `python-chess` move generator while developing the suite. This
caught two mistyped test positions before they entered the baseline.

## Physical-device smoke check

The firmware was uploaded successfully to a Cardputer-Adv detected as an
ESP32-S3 revision v0.2 over its USB-Serial/JTAG interface. Bootloader, partition
table, and application hashes all verified, and the upload completed with a
hard reset. A short 115200-baud monitor window showed no panic or repeated boot
output.

That smoke check does not replace the operator walkthrough. Exact LCD colors,
small text, piece and square legibility, physical keys, repeated Coach use,
sustained Maximum-level searches, battery draw, and temperature still need the
checks in [hardware-test-checklist.md](hardware-test-checklist.md).
