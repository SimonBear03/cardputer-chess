# Validation

This page records the current software validation baseline. It is evidence for
the checked-in revision, not a substitute for testing a release on the physical
device.

## Host rules and engine tests

Run:

```sh
make test
```

Current result: **213 assertions pass**. The suite includes the six standard
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

Current result: **213 assertions pass under AddressSanitizer and UBSan**.
LeakSanitizer is disabled because it requires ptrace support that is unavailable
in some sandboxed runners.

## Firmware build

Run:

```sh
uvx --with pip platformio run
```

The validated toolchain uses PlatformIO Core 6.1.19, Espressif32 platform
6.12.0, Arduino-ESP32 2.0.17, and the M5Cardputer Git tag 1.2.0. The current
release build succeeds for the ESP32-S3FN8 target with:

- Static RAM: 171,860 / 327,680 bytes (52.4%)
- Flash image: 551,809 / 3,342,336 bytes (16.5%)
- Output: `.pio/build/cardputer-adv/firmware.bin`

The 64 KiB transposition table is allocated once at runtime and is therefore
not included in PlatformIO's static-RAM figure. Search uses a fixed 16 KiB
FreeRTOS task stack and fixed-capacity working arrays; it does not allocate in
the recursive search path. Opponent and Coach analysis share the same engine
and task rather than allocating a second search instance.

## Independent cross-check

The canonical perft FENs and expected leaf counts were cross-checked against an
independent `python-chess` move generator while developing the suite. This
caught two mistyped test positions before they entered the baseline.

## Remaining hardware boundary

No Cardputer-Adv USB device is attached to the build host, so flashing, key
feel, LCD and Coach-overlay legibility, sustained maximum-level/MultiPV
searches, battery draw, and
thermals are not claimed as physically validated. Follow
[hardware-test-checklist.md](hardware-test-checklist.md) on the device.
