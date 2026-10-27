"""Repeatable real-NNUE search benchmark; run with .venv/bin/python."""
import argparse
import json
import pathlib
import sys
import threading
import time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'src'))
import chess
from engine import NNUE, Search, ROOT

POSITIONS = {
    'start': chess.STARTING_FEN,
    'middlegame': 'r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 4 8',
}
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--depth', type=int, default=3)
    args = parser.parse_args()
    net = NNUE(ROOT / 'networks/lichess500k.nnue')
    try:
        for name, fen in POSITIONS.items():
            board = chess.Board(fen)
            search = Search(board, threading.Event(), net)
            start = time.monotonic()
            lines = []
            move = search.run(args.depth, emit=lines.append)
            elapsed = time.monotonic() - start
            print(json.dumps(dict(position=name, depth=args.depth, nodes=search.nodes,
                                  seconds=round(elapsed, 4), nps=round(search.nodes / elapsed),
                                  bestmove=move.uci(), info=lines[-1])))
    finally:
        net.close()
