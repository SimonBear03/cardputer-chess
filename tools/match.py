#!/usr/bin/env python3
"""Run deterministic paired matches against an external UCI or XBoard engine."""

from __future__ import annotations

import argparse
import json
import logging
import math
from pathlib import Path
import shlex
import subprocess
from typing import TextIO

import chess
import chess.engine
import chess.pgn


OPENINGS = (
    "e2e4 e7e5 g1f3 b8c6",
    "e2e4 c7c5 g1f3 d7d6",
    "e2e4 c7c6 d2d4 d7d5",
    "e2e4 e7e6 d2d4 d7d5",
    "d2d4 d7d5 c2c4 e7e6",
    "d2d4 g8f6 c2c4 g7g6",
    "c2c4 e7e5 b1c3 g8f6",
    "g1f3 d7d5 d2d4 g8f6",
    "e2e4 e7e5 f2f4 e5f4",
    "e2e4 d7d5 e4d5 d8d5",
    "d2d4 f7f5 g2g3 g8f6",
    "b2b3 e7e5 c1b2 b8c6",
    "g1f3 g8f6 c2c4 g7g6",
    "c2c4 c7c5 g1f3 g8f6",
    "d2d4 g8f6 c2c4 e7e6",
    "g1f3 d7d5 c2c4 e7e6",
)


def load_openings(path: str) -> list[tuple[str, str]]:
    if not path:
        return [(f"builtin-{index + 1}", moves) for index, moves in enumerate(OPENINGS)]
    openings: list[tuple[str, str]] = []
    for raw_line in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        name, separator, moves = line.partition("|")
        if not separator or not name.strip() or not moves.strip():
            raise SystemExit(f"invalid opening line: {raw_line!r}")
        starting_board(moves.strip())
        openings.append((name.strip(), moves.strip()))
    if not openings:
        raise SystemExit(f"opening file is empty: {path}")
    return openings


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cardputer", required=True, help="Cardputer Chess UCI command")
    parser.add_argument("--opponent", required=True, help="opponent command")
    parser.add_argument(
        "--opponent-protocol",
        choices=("uci", "xboard", "xboard-force", "xboard-force-legacy"),
        default="uci",
    )
    parser.add_argument(
        "--opponent-option",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="repeatable UCI option for the opponent",
    )
    parser.add_argument("--depth", type=int, default=5)
    parser.add_argument("--movetime-ms", type=int, default=0)
    parser.add_argument("--cardputer-nodes", type=int, default=0)
    parser.add_argument("--cardputer-movetime-ms", type=int, default=0)
    parser.add_argument("--opponent-movetime-ms", type=int, default=0)
    parser.add_argument("--opponent-elo", type=int, default=0)
    parser.add_argument("--cardputer-hash-kb", type=int, default=64)
    parser.add_argument("--opening-file", default="bench/openings.tsv")
    parser.add_argument("--opening-pairs", type=int, default=0)
    parser.add_argument("--repetitions", type=int, default=1)
    parser.add_argument("--max-plies", type=int, default=240)
    parser.add_argument("--pgn-output", default="")
    parser.add_argument("--json-output", default="")
    parser.add_argument("--trace", action="store_true")
    return parser.parse_args()


def launch(command: str, protocol: str, debug: bool = False) -> chess.engine.SimpleEngine:
    argv = shlex.split(command)
    if protocol == "xboard":
        return chess.engine.SimpleEngine.popen_xboard(argv, debug=debug)
    return chess.engine.SimpleEngine.popen_uci(argv, debug=debug)


def parse_options(raw_options: list[str]) -> dict[str, str]:
    options: dict[str, str] = {}
    for raw in raw_options:
        name, separator, value = raw.partition("=")
        if not separator or not name.strip():
            raise SystemExit(f"invalid --opponent-option {raw!r}; use NAME=VALUE")
        options[name.strip()] = value.strip()
    return options


class ForceModeXBoard:
    """Minimal adapter for engines such as Fairy-Max that lack setboard."""

    def __init__(self, command: str, protocol_version_2: bool = True) -> None:
        self.process = subprocess.Popen(
            shlex.split(command),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        if self.process.stdin is None or self.process.stdout is None:
            raise RuntimeError("failed to open XBoard engine pipes")
        self.input: TextIO = self.process.stdin
        self.output: TextIO = self.process.stdout
        self._send("xboard")
        if protocol_version_2:
            self._send("protover 2")
            while True:
                response = self._readline()
                if response.startswith("feature ") and "done=1" in response:
                    break

    def _send(self, command: str) -> None:
        self.input.write(command + "\n")
        self.input.flush()

    def _readline(self) -> str:
        response = self.output.readline()
        if not response:
            raise RuntimeError("XBoard engine exited unexpectedly")
        return response.strip()

    def new_game(self, opening: str, depth: int) -> None:
        self._send("new")
        self._send("force")
        self._send(f"sd {depth}")
        for uci in opening.split():
            self._send(uci)

    def notify(self, move: chess.Move) -> None:
        self._send(move.uci())

    def play(self, board: chess.Board) -> chess.Move:
        self._send("go")
        while True:
            response = self._readline()
            if not response.startswith("move "):
                continue
            move = chess.Move.from_uci(response.removeprefix("move ").strip())
            self._send("force")
            if move not in board.legal_moves:
                raise RuntimeError(f"XBoard engine returned illegal move {move}")
            return move

    def quit(self) -> None:
        if self.process.poll() is None:
            self._send("quit")
            self.process.wait(timeout=5)


def starting_board(opening: str) -> chess.Board:
    board = chess.Board()
    for uci in opening.split():
        board.push_uci(uci)
    return board


def search_limit(nodes: int, movetime_ms: int, depth: int) -> chess.engine.Limit:
    if nodes > 0:
        return chess.engine.Limit(nodes=nodes)
    if movetime_ms > 0:
        return chess.engine.Limit(time=movetime_ms / 1000.0)
    return chess.engine.Limit(depth=depth)


def wilson_interval(
    score: float, games: int, z: float = 1.959963984540054
) -> tuple[float, float]:
    if games <= 0:
        return (0.0, 1.0)
    proportion = score / games
    z_squared = z * z
    denominator = 1.0 + z_squared / games
    center = (proportion + z_squared / (2.0 * games)) / denominator
    margin = (
        z
        * math.sqrt(
            (proportion * (1.0 - proportion) + z_squared / (4.0 * games)) / games
        )
        / denominator
    )
    return (max(0.0, center - margin), min(1.0, center + margin))


def elo_delta(proportion: float) -> float:
    bounded = min(1.0 - 1e-9, max(1e-9, proportion))
    return 400.0 * math.log10(bounded / (1.0 - bounded))


def play_game(
    cardputer: chess.engine.SimpleEngine,
    opponent: chess.engine.SimpleEngine | ForceModeXBoard,
    cardputer_white: bool,
    opening: str,
    depth: int,
    cardputer_nodes: int,
    cardputer_movetime_ms: int,
    opponent_movetime_ms: int,
    max_plies: int,
    trace: bool,
) -> tuple[chess.Outcome | None, chess.Board]:
    board = starting_board(opening)
    game_token = object()
    if isinstance(opponent, ForceModeXBoard):
        opponent.new_game(opening, depth)
        if trace:
            print("opponent position initialized", flush=True)
    while len(board.move_stack) < max_plies:
        outcome = board.outcome(claim_draw=True)
        if outcome is not None:
            return outcome, board
        cardputer_turn = board.turn == (chess.WHITE if cardputer_white else chess.BLACK)
        if cardputer_turn:
            if trace:
                print(f"requesting Cardputer move at ply={len(board.move_stack)}", flush=True)
            result = cardputer.play(
                board,
                search_limit(cardputer_nodes, cardputer_movetime_ms, depth),
                game=game_token,
            )
            if result.move is None or result.move not in board.legal_moves:
                raise RuntimeError(
                    f"Cardputer returned illegal move {result.move} in {board.fen()}"
                )
            move = result.move
            board.push(move)
            if trace:
                print(f"ply={len(board.move_stack)} cardputer={move.uci()}", flush=True)
            if isinstance(opponent, ForceModeXBoard):
                opponent.notify(move)
        elif isinstance(opponent, ForceModeXBoard):
            move = opponent.play(board)
            board.push(move)
            if trace:
                print(f"ply={len(board.move_stack)} opponent={move.uci()}", flush=True)
        else:
            result = opponent.play(
                board, search_limit(0, opponent_movetime_ms, depth), game=game_token
            )
            if result.move is None or result.move not in board.legal_moves:
                raise RuntimeError(
                    f"opponent returned illegal move {result.move} in {board.fen()}"
                )
            board.push(result.move)
            if trace:
                print(
                    f"ply={len(board.move_stack)} opponent={result.move.uci()}",
                    flush=True,
                )
    return None, board


def main() -> int:
    args = parse_args()
    if args.movetime_ms > 0:
        if args.cardputer_movetime_ms > 0 or args.opponent_movetime_ms > 0:
            raise SystemExit("--movetime-ms cannot be combined with per-engine movetime")
        args.cardputer_movetime_ms = args.movetime_ms
        args.opponent_movetime_ms = args.movetime_ms
    if args.opponent_movetime_ms > 0 and args.opponent_protocol.startswith(
        "xboard-force"
    ):
        parser_message = "--movetime-ms currently requires a UCI/setboard opponent"
        raise SystemExit(parser_message)
    if args.opponent_option and args.opponent_protocol != "uci":
        raise SystemExit("--opponent-option requires a UCI opponent")
    if args.opponent_elo > 0 and args.opponent_protocol != "uci":
        raise SystemExit("--opponent-elo requires a UCI opponent")
    if args.trace:
        logging.basicConfig(level=logging.DEBUG)
    if args.trace:
        print("launching Cardputer engine", flush=True)
    cardputer = launch(args.cardputer, "uci", args.trace)
    cardputer.configure({"Hash": max(1, args.cardputer_hash_kb)})
    if args.trace:
        print("launching opponent", flush=True)
    opponent: chess.engine.SimpleEngine | ForceModeXBoard
    if args.opponent_protocol in ("xboard-force", "xboard-force-legacy"):
        opponent = ForceModeXBoard(
            args.opponent,
            protocol_version_2=args.opponent_protocol == "xboard-force",
        )
    else:
        opponent = launch(args.opponent, args.opponent_protocol, args.trace)
        opponent_options = parse_options(args.opponent_option)
        if args.opponent_elo > 0:
            opponent_options["UCI_LimitStrength"] = "true"
            opponent_options["UCI_Elo"] = str(args.opponent_elo)
        opponent.configure(opponent_options)
    if args.trace:
        print("engines ready", flush=True)
    cardputer_id = dict(cardputer.id)
    opponent_id = dict(opponent.id) if not isinstance(opponent, ForceModeXBoard) else {}
    openings = load_openings(args.opening_file)
    wins = draws = losses = capped_draws = 0
    pgn_path = Path(args.pgn_output) if args.pgn_output else None
    if pgn_path is not None:
        pgn_path.parent.mkdir(parents=True, exist_ok=True)
    pgn_stream = pgn_path.open("w", encoding="utf-8") if pgn_path else None
    try:
        pairs = (
            len(openings)
            if args.opening_pairs <= 0
            else min(args.opening_pairs, len(openings))
        )
        repetitions = max(1, args.repetitions)
        game_number = 0
        for repetition in range(1, repetitions + 1):
            for opening_index, (opening_name, opening) in enumerate(
                openings[:pairs], start=1
            ):
                for cardputer_white in (True, False):
                    game_number += 1
                    outcome, board = play_game(
                        cardputer,
                        opponent,
                        cardputer_white,
                        opening,
                        args.depth,
                        args.cardputer_nodes,
                        args.cardputer_movetime_ms,
                        args.opponent_movetime_ms,
                        args.max_plies,
                        args.trace,
                    )
                    if outcome is None:
                        result = "1/2-1/2 (ply cap)"
                        draws += 1
                        capped_draws += 1
                    else:
                        result = outcome.result()
                        cardputer_won = (
                            outcome.winner == chess.WHITE and cardputer_white
                        ) or (outcome.winner == chess.BLACK and not cardputer_white)
                        if outcome.winner is None:
                            draws += 1
                        elif cardputer_won:
                            wins += 1
                        else:
                            losses += 1
                    side = "White" if cardputer_white else "Black"
                    print(
                        f"game={game_number} repetition={repetition} "
                        f"opening={opening_index}:{opening_name} "
                        f"cardputer={side} result={result} "
                        f"plies={len(board.move_stack)}",
                        flush=True,
                    )
                    if pgn_stream is not None:
                        game = chess.pgn.Game.from_board(board)
                        game.headers["Event"] = "Cardputer Chess Elo calibration"
                        game.headers["Round"] = str(game_number)
                        game.headers["White"] = (
                            "Cardputer Chess" if cardputer_white else "Opponent"
                        )
                        game.headers["Black"] = (
                            "Opponent" if cardputer_white else "Cardputer Chess"
                        )
                        game.headers["Opening"] = opening_name
                        game.headers["Result"] = (
                            "1/2-1/2" if outcome is None else outcome.result()
                        )
                        print(game, file=pgn_stream, end="\n\n")
    finally:
        cardputer.quit()
        opponent.quit()
        if pgn_stream is not None:
            pgn_stream.close()
    total = wins + draws + losses
    score = wins + 0.5 * draws
    score_fraction = score / total
    score_low, score_high = wilson_interval(score, total)
    print(
        f"summary games={total} wins={wins} draws={draws} losses={losses} "
        f"capped_draws={capped_draws} score={score:.1f}/{total} "
        f"percent={100.0 * score_fraction:.1f}% "
        f"score_ci95={100.0 * score_low:.1f}-{100.0 * score_high:.1f}%"
    )
    result_data: dict[str, object] = {
        "games": total,
        "wins": wins,
        "draws": draws,
        "losses": losses,
        "capped_draws": capped_draws,
        "score": score,
        "score_percent": 100.0 * score_fraction,
        "score_ci95_percent": [100.0 * score_low, 100.0 * score_high],
        "cardputer_nodes": args.cardputer_nodes,
        "cardputer_movetime_ms": args.cardputer_movetime_ms,
        "opponent_movetime_ms": args.opponent_movetime_ms,
        "opponent_elo": args.opponent_elo,
        "opening_pairs": pairs,
        "repetitions": repetitions,
        "cardputer_engine": cardputer_id,
        "opponent_engine": opponent_id,
    }
    if args.opponent_elo > 0:
        estimate = args.opponent_elo + elo_delta(score_fraction)
        estimate_low = args.opponent_elo + elo_delta(score_low)
        estimate_high = args.opponent_elo + elo_delta(score_high)
        result_data["estimated_elo"] = estimate
        result_data["estimated_elo_ci95"] = [estimate_low, estimate_high]
        print(
            f"rating opponent_elo={args.opponent_elo} estimate={estimate:.0f} "
            f"elo_ci95={estimate_low:.0f}-{estimate_high:.0f}"
        )
    if args.json_output:
        json_path = Path(args.json_output)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(
            json.dumps(result_data, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
