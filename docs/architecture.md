# Architecture

This document explains the implementation for contributors. If you only want
to install and play the game, start with the [README](../README.md).

At a high level, the project has three parts:

1. A portable chess-rules library.
2. A portable chess engine and Coach.
3. The Cardputer application that draws the screen, reads keys, saves settings,
   and runs engine work in the background.

## Product boundary

Cardputer Chess is an offline human-versus-engine chess application for the
M5Stack Cardputer-Adv. The firmware owns the complete rules, search engine,
menus, board rendering, keyboard controls, and persistent user preferences.
No network service or companion application is required.

## Hardware constraints

The target uses an ESP32-S3FN8: two 240 MHz LX7 cores, 512 KB on-chip SRAM,
8 MB flash, and no PSRAM. The application therefore follows four constraints:

1. Search memory is allocated once and remains bounded.
2. Search move lists and principal variations are reused by ply, not allocated
   recursively.
3. The LCD is drawn directly; no full-screen sprite is kept in RAM.
4. Flash may hold code and a compact opening book, but microSD is not required.

The background search task uses a fixed 24 KiB stack. Compiler stack-usage
reports show that the search driver itself needs roughly 7.5 KiB before deeper
alpha-beta and quiescence frames, so the prior 16 KiB allocation did not leave a
safe worst-case margin at the harder levels.

## Layers

### Portable chess core

`include/cardputer_chess/chess.hpp` and `src/chess.cpp` implement the board,
legal move generation, make/unmake, FEN, repetition history, draw rules, and
game-state classification, including Standard Algebraic Notation for display.
This layer depends only on the C++ standard library and is compiled into both
host tests and firmware.

`saved_game.hpp` and `saved_game.cpp` provide the portable persistence format.
They encode each completed move into two bytes and protect the versioned record
with a CRC. Restoring by replaying the legal moves reconstructs castling and en
passant state, repetition history, notation, last-move highlighting, and undo
records instead of restoring only the visible board from a FEN snapshot.

### Portable engine

`include/cardputer_chess/engine.hpp` and `src/engine.cpp` implement a bounded
iterative-deepening alpha-beta engine with quiescence search, transposition
table, move ordering, pruning, evaluation, an opening book, time control, and
skill selection. MultiPV repeats the root search while excluding earlier best
moves, producing exact ranked alternatives rather than treating root ordering
bounds as evaluations. `coach.hpp` and `coach.cpp` classify a played candidate
without pretending that an unsearched move has an exact score. These modules
have no display or keyboard dependencies.

The fixed 64 KiB transposition table is two-way associative to reduce
destructive collisions without increasing RAM. Search combines null move,
late-move reduction, shallow reverse futility, delta-pruned quiescence, killer
moves, and decaying history scores. Evaluation is tapered between middlegame
and endgame and includes mobility, bishop pair, pawn structure, passed and
connected pawns, rook files, protected knight outposts, king shelter, and a
small conversion term that drives a materially losing king toward the edge.
The 36 vetted opening lines compile into 163 unique position/move entries.

### Cardputer application

`src/main.cpp` owns the home screen, New Match settings list, persisted
level/color/Coach/theme preferences, board cursor and move selection, promotion
chooser, direct LCD rendering, game history, Coach overlay, the on-device
engine benchmark, and the background engine task. Three shared palettes
provide the Classic, Neon, and Royal themes without duplicating layouts or
allocating framebuffers. Target-sized generated Staunton artwork is reduced to
fixed 14×14 body/detail masks in `piece_glyphs.hpp`, so all six piece types have
solid, distinct silhouettes at a total cost of only a few hundred bytes of flash.
The masks deliberately avoid a full perimeter outline: the second color is
reserved for the bishop cut and shared base accent, while the king's cross stays
part of its solid body. The engine always searches a private position copy, so
the player can keep navigating and can play immediately while automatic Coach
analysis is still winding down.

The application saves the current move history, human side, random seed, and a
monotonic generation to two alternating ESP32 NVS slots. Each completed move or
undo writes the inactive slot, so an interrupted write leaves the previous slot
available. On boot the newest versioned record with a valid CRC is replayed and
offered as **Continue** on the home screen. Engine and Coach searches, open
overlays, and selections are intentionally transient. Entering New Match does
not destroy the previous save; starting the replacement match writes a newer
generation into the alternating-slot journal.

`startWrite()` keeps a display-bus transaction open but does not make a group of
LCD commands appear atomically. The UI therefore never runs a timed full-screen
repaint loop. Cursor movement redraws two 15-pixel squares, selection redraws
only the changed selection and legal-destination squares, Home redraws only the
two affected cursor-and-label actions, and New Match redraws its four-row list
and compact options strip as one bounded content region. Pause, promotion, and
Coach navigation redraw only the affected rows or detail region.
A completed Coach search updates just the side panel and exposes its best move
as `NEXT <SAN>` when the board did not change. Variable panel copy is capped at
the inner panel's 16-character width.

The repeating timed animation communicates active Engine or Coach work. Every
300 ms it recolors only three two-pixel status dots in the side panel, or in the
open Coach overlay. Short match-start, positive-Coach-move, and result accents
use the same localized three-dot treatment for six 120 ms frames. None clears a
text row, redraws the board, or allocates a sprite.

Transitions among board-backed screens repaint over the current frame without a
blanking clear. Theme changes and structurally different screens still repaint
the complete active screen once so no pixels from the previous layout remain.
Move, check, and result feedback stays visible as static state after any short
event accent finishes. This avoids continuous tearing while preserving the
64 KiB engine hash and 24 KiB search stack; a 240×135 16-bit framebuffer would
consume another 64.8 KiB on a target with no PSRAM.

The Home-screen `B` shortcut opens a diagnostic benchmark without altering the
saved match. Quick mode measures six one-second searches at the strongest
settings. Full mode applies each production level configuration to four fixed
positions. Both parse FEN directly into the reusable static search position,
clear the engine hash between samples, disable the opening book, and summarize
median node budgets on the LCD. Full mode refreshes its content only between
levels; during a search, only its three thinking dots change.

The board uses 15×15-pixel squares, leaving native-font gutters for file and
rank labels. Coordinate labels and piece placement both follow the human-side
orientation, so the bottom-left label changes correctly when playing Black.

## Controls

The Cardputer prints arrow legends on the `;`, `,`, `.`, and `/` keys. With
`Fn`, the M5Cardputer library reports dedicated arrow states; without `Fn`, it
reports the underlying punctuation. The firmware accepts both forms as Up,
Left, Down, and Right. `W/A/S/D` are aliases for development and accessibility.

- Arrow: move cursor or menu selection
- Enter or Space: select/confirm
- Backspace: cancel a selected piece or leave a chooser
- Tab: open the in-game menu
- `U`: undo the human move and the engine reply
- `H`: open Coach or begin on-demand analysis; Left/Right changes candidate and
  Enter focuses its origin square on the board
- `B` from Home: open Quick Speed, Full Levels, or the latest benchmark report

## Concurrency

The Arduino loop owns all game and display state. Engine work runs in a
FreeRTOS task pinned to the other core with a copied `Position`. The task
publishes only a small result under a critical section. This keeps display and
keyboard handling responsive without concurrent mutation of chess state. The
same task is reused for opponent and Coach work, so two searches never compete
for RAM or CPU simultaneously. If the player moves during Coach analysis, the
task is cancelled and the opponent search starts as soon as it exits.

The search checks its limits every 1,024 nodes and briefly blocks once every
4,096 nodes on Arduino. This gives the core-zero idle task enough execution time
to service the ESP32 task watchdog during the 4.5-second Master and 10-second
Maximum budgets without changing those budgets or disabling watchdog coverage.

## Coach analysis

Coach can be Off, On demand, or Always. Its fixed 1.8-second budget requests
three ranked lines, disables the randomized opening book, and always selects
the strongest root move. Scores are shown from the human side's perspective.
For alternatives the UI reports centipawn loss versus line one; after a played
move it reports a quality label when the move was one of the analyzed three,
or honestly reports `Outside top 3` when no exact candidate score exists.
SAN/PV formatting reuses a dedicated application-owned position instead of
placing another four-kilobyte `Position` on the UI task's stack.

## Strength levels

All ten numbered levels use the same engine. Levels change maximum depth, time
budget, and controlled root-move error. The scale runs from **1 Beginner** to
**9 Master** and **10 Maximum**; Skilled and Advanced provide real intermediate
budgets rather than cosmetic labels. Maximum always chooses the best completed
root move and receives the largest time budget. Lower levels sometimes choose a
plausible inferior candidate so they remain meaningfully beatable rather than
merely fast. A versioned preference migration preserves the effective strength
of older eight-level Master and Maximum selections.

Timed search compares unsigned elapsed milliseconds instead of an absolute
deadline, so the ESP32's 32-bit `millis()` rollover cannot create a runaway
search after roughly 49 days of uptime.

## Correctness strategy

Host tests are the rules authority. Standard perft positions validate legal
move generation, while focused tests cover castling, en passant, promotion,
checkmate, stalemate, repetition, fifty-move draws, insufficient material,
make/unmake identity, SAN, MultiPV uniqueness/order, Coach classification,
engine legality, levels, cancellation, and saved-game serialization/replay. Firmware
compilation validates the hardware API boundary. Physical-device validation is
still required for key legends, LCD appearance, battery life, and thermals.
