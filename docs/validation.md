# Validation

This page records the current software validation baseline. It is written for
contributors who want to verify a change before flashing or publishing it.

## Current result at a glance

| Check | Result |
| --- | --- |
| Host rules, engine, and persistence tests | 303 assertions passed |
| AddressSanitizer and UBSan | 303 assertions passed |
| Cardputer-Adv firmware build | Passed |
| Static RAM | 178,180 / 327,680 bytes (54.4%) |
| Flash image | 558,097 / 3,342,336 bytes (16.7%) |
| USB upload | Current build passed on `/dev/cu.usbmodem101` |

These results support the checked-in code, but they do not replace looking at
the real LCD and pressing the physical keys.

## Host rules and engine tests

Run:

```sh
make test
```

Current result: **303 assertions pass**. The suite includes the six standard
perft positions, with start position and endgame coverage extended to depth 4.
It also covers:

- FEN parsing and coordinate conversion
- make/unmake and Zobrist-key identity
- castling, including castling through check
- en passant, including a pinned pawn and repetition-key semantics
- all four promotions
- checkmate, stalemate, fifty-move, threefold-repetition, and dead-position draws
- ten-level naming, strictly increasing time/depth budgets, tightening skill
  error, opening-book legality, mate-in-one search, and bounded stop
- expanded-book coverage through a Berlin Defense mainline
- SAN notation, including capture, disambiguation, castling, promotion, check,
  and checkmate
- exact three-line MultiPV legality, uniqueness, order, PV shape, and caller
  position identity
- Coach mode naming and honest move-quality classification
- saved-game versioning, checksums, corruption rejection, generation rollover,
  compact move replay, and recovery of castling, en passant, and promotion flags

Run the same suite with memory and undefined-behavior instrumentation:

```sh
make test-sanitize
```

Current result: **303 assertions pass under AddressSanitizer and UBSan**.
LeakSanitizer is disabled because it requires ptrace support that is unavailable
in some sandboxed runners.

## Firmware build

Run:

```sh
platformio run
```

The project pins Espressif32 platform 6.12.0 and the M5Cardputer Git tag 1.2.0.
The current release build succeeds for the ESP32-S3FN8 target with:

- Static RAM: 178,180 / 327,680 bytes (54.4%)
- Flash image: 558,097 / 3,342,336 bytes (16.7%)
- Output: `.pio/build/cardputer-adv/firmware.bin`

The 64 KiB transposition table is allocated once at runtime and is therefore
not included in PlatformIO's static-RAM figure. Search uses a fixed 24 KiB
FreeRTOS task stack and fixed-capacity working arrays; it does not allocate in
the recursive search path. Opponent and Coach analysis share the same engine
and task rather than allocating a second search instance.

The UI keeps its large reusable `Position` objects in static application
storage, including the notation copy used by the Coach overlay. This avoids
overflowing the Arduino loop task while formatting a continuation line. All
three themes share the same 14×14 two-color piece masks. Theme selection, board
coordinates, dirty-region redraws, and the three-dot thinking indicator do not
require a framebuffer or dynamic allocation. Home and New Match use localized
action/slot redraws, while finite event accents also change only three two-pixel
dots.

Saved games use a fixed 1,048-byte scratch buffer plus a compact in-memory move
record. Two alternating NVS slots, format versioning, and a CRC preserve the
previous valid generation if power is interrupted during a write. Long searches
briefly block every 4,096 nodes so the core-zero idle task can service the ESP32
task watchdog without shortening Master or Maximum thinking time. ESP32 compiler
stack reports measured about 7.5 KiB in the search driver plus an 864-byte task
wrapper before recursive search frames, which is why the task now reserves 24
KiB instead of the marginal 16 KiB allocation.

## Independent cross-check

The canonical perft FENs and expected leaf counts were cross-checked against an
independent `python-chess` move generator while developing the suite. This
caught two mistyped test positions before they entered the baseline.

## Previous QEMU application check

The following evidence belongs to the earlier persistence/flicker baseline. It
was intentionally **not rerun** for the Home/New Match upgrade because the web
simulator is still under development. It must not be treated as current-shell
visual evidence.

The current firmware was merged into a private padded 8 MiB flash image with
SHA-256 `7d0b9baf49419e40fe059a4402b0f5ab04925c092c5ee49e2e94b97f47e5ea06`.
It ran unmodified on the existing ESP32-S3 QEMU compatibility build at upstream
commit `40edccac415693c5130f91c01d84176ae6008566` plus the simulator's tracked
patches 0001 through 0011. The image and captures remain untracked test inputs.

Starting from empty NVS, the test selected Maximum and played the off-book move
`a3`, forcing a real ten-second search rather than an opening-book reply. The
engine completed its move, the display settled, and QEMU remained running. The
QEMU process was then stopped and relaunched against the same private flash to
model turning the device off and on. The application booted directly into the
saved game. The board-region SHA-256 matched exactly before and after the power
cycle:

```text
21fd7fc91a3fb15aec81ae1d656514c5cb70b708c37be4a563e32d5c96127a16
```

The full-frame hashes intentionally differ because search statistics are
transient and are not restored. This validates the persistence path and the
hard-level stability fix in the modeled CPU, flash, NVS, display, and keyboard
environment. It is not a physical-device thermal, power, or task-watchdog test.

## Physical-device smoke check

The current firmware was uploaded successfully to a Cardputer-Adv detected as
an ESP32-S3 revision v0.2 over its USB-Serial/JTAG interface at
`/dev/cu.usbmodem101`. Bootloader, partition table, and application hashes all
verified, and the upload completed with a hard reset. The flashed build includes
the Home/Continue flow, save-preserving New Match wheel, ten-level preference
migration, dirty-region rendering, localized event and thinking indicators,
bounded panel text, and visible Coach suggestions.

Continue after a hard power cycle and repeated ten-second Maximum searches
remain required operator checks; a successful upload alone does not claim those
behaviors on physical hardware.

That smoke check does not replace the operator walkthrough. Exact LCD colors,
small text, the new vertical-wheel hierarchy, piece and square legibility,
physical keys, repeated Coach use, sustained Maximum-level searches, battery
draw, and temperature still need the checks in
[hardware-test-checklist.md](hardware-test-checklist.md).
