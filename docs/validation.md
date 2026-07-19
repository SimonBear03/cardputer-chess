# Validation

This page records the current software validation baseline. It is evidence for
the checked-in revision, not a substitute for testing a release on the physical
device.

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

The validated toolchain uses PlatformIO Core 6.1.19, Espressif32 platform
6.12.0, Arduino-ESP32 2.0.17, and the M5Cardputer Git tag 1.2.0. The current
release build succeeds for the ESP32-S3FN8 target with:

- Static RAM: 176,084 / 327,680 bytes (53.7%)
- Flash image: 553,201 / 3,342,336 bytes (16.6%)
- Output: `.pio/build/cardputer-adv/firmware.bin`

The 64 KiB transposition table is allocated once at runtime and is therefore
not included in PlatformIO's static-RAM figure. Search uses a fixed 16 KiB
FreeRTOS task stack and fixed-capacity working arrays; it does not allocate in
the recursive search path. Opponent and Coach analysis share the same engine
and task rather than allocating a second search instance.

The UI keeps its large reusable `Position` objects in static application storage,
including the notation copy used by the Coach overlay. This avoids overflowing
the Arduino loop task while formatting a principal variation. All three visual
themes share the same 14×14 two-color piece masks. Theme selection, board
coordinates, and the non-blocking animation state add no dynamic allocation or
framebuffer.

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

That smoke check does not replace the operator walkthrough: exact LCD colors,
small-text and piece legibility, physical key behavior, repeated Coach-overlay
use, sustained Maximum/MultiPV searches, battery draw, and thermals still need
the checks in [hardware-test-checklist.md](hardware-test-checklist.md).
