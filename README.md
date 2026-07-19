# Cardputer Chess

Cardputer Chess is a complete offline chess game for the M5Stack
Cardputer-Adv. You play against the built-in engine using the keyboard and the
240×135 color display. No Wi-Fi, account, server, or microSD card is required.

## Play your first game

1. Turn on the Cardputer-Adv.
2. On the setup screen, use Up/Down to choose a setting and Left/Right to change
   it.
3. Choose your side, difficulty, Coach mode, and theme.
4. Press Enter or Space from any setup row to start.
5. Move the board cursor with the arrow keys. Press Enter or Space once to pick
   up a piece and again to place it on a highlighted legal square.

The board shows `a`–`h` and `1`–`8` coordinates. They rotate automatically when
you play as Black, so your pieces always begin at the bottom of the screen.

The current game is saved automatically after every completed move and undo.
Turn the Cardputer off whenever you need to put it away; the next boot returns
directly to the same game. If it was the engine's turn, thinking restarts from
the restored position. Choosing New Game from the game menu or result screen
clears the old game and returns to setup.

## Controls

The printed arrow keys use `Fn` with `;`, `,`, `.`, and `/`. The game also
accepts those four keys without `Fn`, plus `W/A/S/D`.

| Input | Action |
| --- | --- |
| Arrows or `W/A/S/D` | Move the board cursor or menu selection |
| Enter or Space | Start, select a piece, or confirm |
| Backspace or Esc | Cancel a selection or close a chooser |
| Tab | Open or close the in-game menu |
| `U` | Undo the last human turn |
| `H` | Open Coach or request analysis |

## Themes and display

- **Classic** — tournament gold and deep teal
- **Neon** — aqua and violet
- **Royal** — bronze and burgundy

All themes use solid Staunton-style pieces designed for the 15-pixel board
squares. Pawns are deliberately shorter, bishops have a visible mitre cut, and
the king uses a solid same-color cross. Theme choice is saved between restarts.

The game highlights the selected piece, legal destinations, the previous move,
and a king in check. Screen and theme changes repaint once to avoid flicker and
leftover scanlines.

## Reading chess moves

The move history and Coach use Standard Algebraic Notation:

- `e3` means a pawn moved to e3.
- `Nf6` means a knight moved to f6.
- `B`, `R`, `Q`, and `K` mean bishop, rook, queen, and king.
- `x` means a capture, `+` means check, and `#` means checkmate.

See [the beginner notation guide](docs/chess-notation.md) for examples such as
`Nxf6+`, castling, and promotion.

## What is included

- Full legal chess, including castling, en passant, promotion, repetition,
  fifty-move draws, and insufficient-material draws
- Play as White, Black, or a randomly selected side
- Eight saved difficulty levels from Beginner to Maximum
- Coach modes: Off, on demand with `H`, or automatic on every human turn
- Three ranked Coach suggestions with evaluations and short continuation lines
- A compact opening book and a bounded chess engine designed for the ESP32-S3
- Automatic power-safe resume from the last completed move
- Responsive background search, so menus and the board remain usable while the
  engine thinks
- Brief non-blocking animations for game start, moves, checks, and results
- Host-side rule, perft, search, and memory-safety tests

## Build and flash

Install PlatformIO once and make sure `platformio` is available in your shell.
From this repository, run:

```sh
platformio run
platformio run --target upload
```

If more than one serial device is connected, list the ports and select the
Cardputer explicitly:

```sh
platformio device list
platformio run --target upload --upload-port /dev/cu.usbmodemXXXX
```

On Windows, the upload port will look like `COM3` instead. To watch serial
output after flashing, run:

```sh
platformio device monitor
```

If the device is not detected, try a known data-capable USB-C cable and check
that the side power switch is on. See the
[hardware checklist](docs/hardware-test-checklist.md) for bootloader recovery
and physical verification steps.

## Test on a computer

Host tests require a C++17 compiler:

```sh
make test
make test-sanitize
```

Engine contributors can also run:

```sh
make benchmark
make uci
```

See [benchmarking.md](docs/benchmarking.md) for the performance and match
workflow.

## Project guide

- `include/cardputer_chess/` — portable chess and engine interfaces
- `src/chess.cpp` — board state, legal moves, rules, and notation
- `src/coach.cpp` — Coach modes and move-quality labels
- `src/engine.cpp` — search, evaluation, difficulty levels, and opening book
- `src/saved_game.cpp` — versioned move-history serialization and validation
- `src/main.cpp` — Cardputer display, keyboard, preferences, and engine task
- `tests/` — host-side correctness tests
- `tools/` — benchmark, UCI, and match utilities

For deeper technical detail, read [architecture.md](docs/architecture.md).
For current test evidence, read [validation.md](docs/validation.md).
