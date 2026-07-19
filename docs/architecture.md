# Architecture

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

## Layers

### Portable chess core

`include/cardputer_chess/chess.hpp` and `src/chess.cpp` implement the board,
legal move generation, make/unmake, FEN, repetition history, draw rules, and
game-state classification, including Standard Algebraic Notation for display.
This layer depends only on the C++ standard library and is compiled into both
host tests and firmware.

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

`src/main.cpp` owns the setup menu, persisted level/color/Coach/theme preferences,
board cursor and move selection, promotion chooser, direct LCD rendering, game
history, Coach overlay, and the background engine task. Three shared palettes
provide the Classic, Neon, and Royal themes without duplicating layouts or
allocating framebuffers. Target-sized generated Staunton artwork is reduced to
fixed 14×14 body/detail masks in `piece_glyphs.hpp`, so all six piece types have
solid, distinct silhouettes at a total cost of only a few hundred bytes of flash.
The masks deliberately avoid a full perimeter outline: the second color is
reserved for the bishop cut and shared base accent, while the king's cross stays
part of its solid body. The engine always searches a private
position copy, so the player can keep navigating and can play immediately while
automatic Coach analysis is still winding down.

The LCD is cleared only when changing screens or themes. Steady-state updates
redraw the relevant board/menu state inside one display write transaction,
avoiding the visible flash caused by repeatedly clearing the physical panel.
Short intro, move/check, and result animations use a non-blocking
`millis()` state machine and redraw at roughly 24 frames per second. They keep
only a few scalar fields and never allocate a full-screen RGB framebuffer.
Theme changes deliberately skip animation: they clear once and repaint the
complete active screen in the new palette so no scanline can remain in an
otherwise untouched menu gap.

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

## Concurrency

The Arduino loop owns all game and display state. Engine work runs in a
FreeRTOS task pinned to the other core with a copied `Position`. The task
publishes only a small result under a critical section. This keeps display and
keyboard handling responsive without concurrent mutation of chess state. The
same task is reused for opponent and Coach work, so two searches never compete
for RAM or CPU simultaneously. If the player moves during Coach analysis, the
task is cancelled and the opponent search starts as soon as it exits.

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

All eight levels use the same engine. Levels change maximum depth, time budget,
and controlled root-move error. Maximum always chooses the best completed root
move and receives the largest time budget. Lower levels sometimes choose a
plausible inferior candidate so they remain meaningfully beatable rather than
merely fast.

Timed search compares unsigned elapsed milliseconds instead of an absolute
deadline, so the ESP32's 32-bit `millis()` rollover cannot create a runaway
search after roughly 49 days of uptime.

## Correctness strategy

Host tests are the rules authority. Standard perft positions validate legal
move generation, while focused tests cover castling, en passant, promotion,
checkmate, stalemate, repetition, fifty-move draws, insufficient material,
make/unmake identity, SAN, MultiPV uniqueness/order, Coach classification,
engine legality, levels, and cancellation. Firmware
compilation validates the hardware API boundary. Physical-device validation is
still required for key legends, LCD appearance, battery life, and thermals.
