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

## On-device benchmark

Press `B` from the firmware Home screen to benchmark the actual ESP32-S3. This
is not a simulator: every search runs through the production engine task on the
Cardputer, with its real CPU, RAM, watchdog yielding, and 64 KiB transposition
table. The benchmark uses trusted positions mirrored from `bench/positions.tsv`,
clears the hash between samples, disables the opening book, and does not touch
the saved game.

**Quick Speed** runs six one-second, maximum-strength samples and reports median
nodes per second, minimum and maximum observed speed, median nodes, and median
completed depth. **Full Levels** runs four positions through the exact time,
depth, error-window, and candidate-count configuration for each of the ten
levels. It reports median nodes per move and depth per level. Results remain in
RAM under **Last Result** until restart or the next run.

The node count converts physical speed into a practical host search budget. If
Maximum reports a median of `DEVICE_NODES` nodes per move, reproduce that amount
of Cardputer work while keeping the Stockfish reference at a chosen time:

```sh
uv run --with python-chess tools/match.py \
  --cardputer ./build/cardputer_chess_uci \
  --opponent /path/to/stockfish \
  --cardputer-nodes DEVICE_NODES \
  --opponent-movetime-ms 100 --opponent-elo 2350 \
  --opponent-option Threads=1 --opponent-option Hash=64 \
  --opening-pairs 32 --max-plies 240
```

This node-equivalent match is an estimate, not direct ESP32-versus-Stockfish
play. It removes the Mac's speed advantage while retaining automation and the
same portable engine code. Position-dependent speed, transposition-table
history during a real game, and the statistical uncertainty of the match still
need to be stated with the result.

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

For UCI-versus-UCI A/B tests, `--movetime-ms 100` replaces the fixed-depth
limit and measures whether a more expensive evaluation earns back its CPU cost.

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

## Optimized-engine result

The accepted optimization pass added mobility and structure evaluation,
delta/reverse-futility pruning, history decay, a two-way table using the same
64 KiB, an endgame conversion term, and broader book coverage. The final node
suite is:

| Budget per position | Stockfish top-three points | Fingerprint |
| --- | ---: | --- |
| 100,000 nodes | 27 / 36 | `0x64cca8e1e511d1fc` |
| 500,000 nodes | 30 / 36 | `0xd3759863bcc95aaf` |

Against the saved original `afe3bdc` engine, the final candidate scored 17
wins, 14 draws, and 1 loss over 32 paired games at an equal 50 ms per move:
**24/32, or 75.0%**. A bishop-development bonus and other intermediate ideas
were rejected when they reduced oracle or match results.

## Stockfish calibration

On 2026-07-20, the portable engine at revision `86a4eca` played a paired
gauntlet against Stockfish 18 on an Apple Silicon Mac. A 16-game ladder first
located the competitive range; the recorded calibration then used all 32
openings in `bench/openings.tsv`, once with each color, for 64 games total.

| Condition | Value |
| --- | --- |
| Opponent | Stockfish 18, `UCI_LimitStrength=true`, `UCI_Elo=2350` |
| Time control | 100 ms per move for each engine |
| Threads | One Stockfish thread; one Cardputer Chess process |
| Hash | Stockfish 64 MiB; Cardputer Chess 64 KiB |
| Opening book | Disabled in the host UCI adapter |
| Game limit | 240 plies; three games reached the cap and counted as draws |
| Result | 27 wins, 12 draws, 25 losses; **33/64 (51.6%)** |
| Estimated rating | **2361 Stockfish-UCI Elo** |
| Approximate 95% interval | **2277–2445** |

This is best reported as **roughly 2360 Elo under the documented host test**.
It is not a FIDE rating, a prediction of performance against human tournament
players, or yet the exact strength of the physical Cardputer. Elo is relative
to a pool and test conditions, and Stockfish's `UCI_Elo` emulates reduced
strength rather than creating a universal rating scale. The interval is also
an approximate Wilson score conversion and does not model correlation between
the color-reversed games.

The result does establish a reproducible external baseline for future engine
changes. A Cardputer-specific number still needs a reliable physical-device
nodes-per-second measurement, followed by a host match at that fixed node
budget. Until then, do not label 2360 as the handheld's tournament Elo.

Reproduce the calibration with Stockfish 18 installed locally:

```sh
uv run --with python-chess tools/match.py \
  --cardputer ./build/cardputer_chess_uci \
  --opponent /path/to/stockfish \
  --movetime-ms 100 --opponent-elo 2350 \
  --opponent-option Threads=1 --opponent-option Hash=64 \
  --opening-pairs 32 --max-plies 240 \
  --pgn-output build/elo-2350.pgn \
  --json-output build/elo-2350.json
```

The earlier 75% equal-time result against the saved original build is about
**+191 Elo relative to that build** under the standard rating formula. It and
the Stockfish calibration measure different comparisons and must not be added
together. The engineering target for the next strength release remains **+150
to +250 Elo at the same search budget and 64 KiB hash**, accepted only through
paired matches and the oracle suite.
