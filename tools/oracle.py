#!/usr/bin/env python3
"""Generate Stockfish top-move annotations for the benchmark suite."""

from __future__ import annotations

import argparse

import chess
import chess.engine


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True)
    parser.add_argument("--suite", default="bench/positions.tsv")
    parser.add_argument("--nodes", type=int, default=500_000)
    parser.add_argument("--multipv", type=int, default=3)
    args = parser.parse_args()

    engine = chess.engine.SimpleEngine.popen_uci(args.engine)
    engine.configure({"Threads": 1, "Hash": 64})
    try:
        with open(args.suite, encoding="utf-8") as suite:
            for raw_line in suite:
                line = raw_line.rstrip("\n")
                if not line or line.startswith("#"):
                    continue
                identifier, fen, _ = line.split("|", maxsplit=2)
                board = chess.Board(fen)
                analyses = engine.analyse(
                    board,
                    chess.engine.Limit(nodes=args.nodes),
                    multipv=args.multipv,
                )
                moves = []
                for analysis in analyses:
                    pv = analysis.get("pv", [])
                    if pv:
                        moves.append(pv[0].uci())
                print(f"{identifier}|{fen}|{','.join(moves)}", flush=True)
    finally:
        engine.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
