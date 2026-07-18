# Engine benchmarking

The benchmark workflow separates speed, search quality, and playing strength.
All comparisons disable the opening book and use the Cardputer's 64 KiB hash
size unless a command explicitly says otherwise.

## Deterministic node suite

```sh
make benchmark
./build/cardputer_chess_benchmark --nodes 500000 --hash-kb 64
```

The twelve fixed positions span openings, tactical middlegames, promotions,
and pawn/rook endings. A stable fingerprint records chosen moves and completed
depths. Wall-clock NPS is useful only when the machine is otherwise idle;
completed depth and move changes are the primary regression signals.

`bench/positions.tsv` also stores Stockfish's top three moves at 500,000 nodes,
scored as 3/2/1 points. Refresh those annotations with:

```sh
uv run --with python-chess tools/oracle.py \
  --engine /path/to/stockfish --nodes 500000
```

## UCI adapter

```sh
make uci
printf 'uci\nisready\nposition startpos\ngo nodes 100000\nquit\n' | \
  ./build/cardputer_chess_uci
```

The adapter exists for host benchmarking and is not linked into firmware. It
supports the UCI position, fixed depth, node, movetime, clock, and hash commands
used by the match harness.

## Paired matches

The Python runner requires `python-chess` and accepts UCI or XBoard opponents.
Every opening is played twice with colors reversed.

```sh
uv run --with python-chess tools/match.py \
  --cardputer ./build/cardputer_chess_uci \
  --opponent '/path/to/fairymax 12 /path/to/fmax.ini' \
  --opponent-protocol xboard-force --depth 5
```

The Fairy-Max hash exponent `12` is approximately a 48 KiB table, keeping the
comparison close to Cardputer Chess's 64 KiB transposition table. External
engines and their data files are benchmark inputs only and are not vendored.
For a legacy XBoard engine that does not implement the `protover 2` handshake,
use `--opponent-protocol xboard-force-legacy`.

## Baseline results

The first baseline used engine revision `afe3bdc`, a 64 KiB hash, and an idle
x86-64 host. The node suite produced:

| Budget per position | Stockfish top-three points | Fingerprint |
| --- | ---: | --- |
| 100,000 nodes | 25 / 36 | `0xc10bb4c15d349340` |
| 500,000 nodes | 22 / 36 | `0xbf386494933e42c2` |

The 500,000-node score being lower is useful evidence that depth alone does not
guarantee better choices with the current evaluation. It is a quality target,
not a performance regression.

Paired fixed-depth-4 matches, eight openings with colors reversed, produced:

| Opponent | Cardputer result | Score |
| --- | --- | ---: |
| Fairy-Max 5.0b, approximately 48 KiB hash | 16 wins, 0 draws, 0 losses | 100% |
| TSCP 1.83b, under 64 KiB RAM | 5 wins, 8 draws, 3 losses | 56.2% |

These are development comparisons, not Elo claims. Games were capped at 120
plies, so TSCP's eight capped games were conservatively recorded as draws.
Fairy-Max is benchmarked as the public-domain micro-Max family candidate. TSCP
is benchmarked unmodified from the author's archive; its
[license requires permission](https://www.tckerrigan.com/Chess/TSCP/) to copy
or create a derivative, so none of its code is included here.
