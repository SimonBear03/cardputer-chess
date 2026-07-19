# Beginner guide to chess notation

Cardputer Chess writes moves in **Standard Algebraic Notation**, usually called
SAN. The notation is compact once you know the pattern.

## Board coordinates

Every square has a letter and a number:

- Files run from `a` to `h` from left to right when White is at the bottom.
- Ranks run from `1` to `8` from White's side toward Black's side.
- A square such as `e3` means file `e`, rank `3`.

The labels on the Cardputer board rotate when you play as Black, but the square
names themselves never change.

## Piece letters

| Letter | Piece |
| --- | --- |
| `K` | King |
| `Q` | Queen |
| `R` | Rook |
| `B` | Bishop |
| `N` | Knight |

Knights use `N` because `K` already means king. Pawns do not use a letter.

## Common examples

- `e3` — a pawn moves to e3.
- `Nf6` — a knight moves to f6.
- `Bxc6` — a bishop captures something on c6.
- `Nxf6+` — a knight captures on f6 and gives check.
- `Qh7#` — the queen moves to h7 and gives checkmate.
- `O-O` — castle on the king side.
- `O-O-O` — castle on the queen side.
- `e8=Q` — a pawn reaches e8 and promotes to a queen.

## Extra symbols

| Symbol | Meaning |
| --- | --- |
| `x` | Capture |
| `+` | Check |
| `#` | Checkmate |
| `=Q`, `=R`, `=B`, `=N` | Pawn promotion |

Sometimes two identical pieces can move to the same destination. SAN then adds
the starting file or rank to remove the ambiguity. For example, `Nbd2` means
the knight currently on the `b` file moves to d2.

You do not need to type notation while playing. The game creates it
automatically for the move history and Coach suggestions.
