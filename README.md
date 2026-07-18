# Cardputer Chess

Cardputer Chess is a complete offline chess game for the M5Stack
Cardputer-Adv. It combines a compact, original C++ engine with a keyboard-first
interface designed for the device's 240×135 display and ESP32-S3FN8 memory
limits.

## Features

- Complete legal chess: castling, en passant, promotion, checkmate, stalemate,
  repetition, fifty-move, and insufficient-material outcomes
- Arrow-key square navigation with highlighted origin, legal destinations,
  last move, and check
- Play as White, Black, or a randomly selected side
- Eight persistent difficulty levels from Beginner to Maximum
- Persistent Coach modes: Off, on-demand with `H`, or automatic every human turn
- Exact top-three analysis with SAN moves, centipawn evaluations, principal
  variations, loss versus the best move, and post-move quality feedback
- Iterative-deepening alpha-beta engine with quiescence search, two-way 64 KiB
  transposition table, selective pruning, tapered positional evaluation, and a
  36-line compact opening book
- Responsive dual-core firmware: the engine searches a private position on a
  background FreeRTOS task
- Host-side perft, rule, and search tests

## Controls

Hold `Fn` with the printed arrow keys: `;` (up), `,` (left), `.` (down), and
`/` (right). The same four keys also navigate without `Fn`; `W/A/S/D` are
aliases.

| Input | Action |
| --- | --- |
| Arrows | Move board cursor or menu selection |
| Enter / Space | Select or confirm |
| Backspace / Esc | Cancel selection / close chooser |
| Tab | Open or close the in-game menu |
| U | Undo the last human turn |
| H | Open Coach or start on-demand top-three analysis |

## Build and test

Host validation requires a C++17 compiler:

```sh
make test
make test-sanitize
make benchmark
make uci
```

Firmware validation and upload use PlatformIO without requiring a persistent
global install:

```sh
uvx --with pip platformio run
uvx --with pip platformio run --target upload
uvx --with pip platformio device monitor
```

`make benchmark` runs the fixed 64 KiB-hash performance suite. The UCI binary
can also play paired external-engine matches through `tools/match.py`; see
[docs/benchmarking.md](docs/benchmarking.md).

The firmware environment pins the M5Cardputer library at version 1.2.0, the
first repository release that explicitly supports the Cardputer-Adv.

## Layout

- `include/cardputer_chess/` — portable rules and engine interfaces
- `src/chess.cpp` — chess position, rules, FEN, and game outcomes
- `src/coach.cpp` — coach modes and move-quality classification
- `src/engine.cpp` — evaluation, search, levels, hash table, and opening book
- `src/main.cpp` — Cardputer UI, controls, persistence, and engine task
- `tests/` — native correctness and search tests
- `tools/` — deterministic benchmark, UCI adapter, and paired-match runner
- `docs/` — architecture, validation, and hardware test checklist

See [docs/architecture.md](docs/architecture.md) for boundaries and memory
decisions, [docs/validation.md](docs/validation.md) for the current evidence,
and [docs/hardware-test-checklist.md](docs/hardware-test-checklist.md) before a
device release.
