import ctypes
import pathlib
import random
import sys
import threading
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'src'))
import chess
from engine import NNUE, ROOT, Search, MATE


class RegressionTests(unittest.TestCase):
    def test_bitboards_match_reference_board_transfer(self):
        net = NNUE(ROOT / 'networks/lichess500k.nnue')
        board = chess.Board()
        rng = random.Random(43)
        try:
            for _ in range(160):
                net.push(board)
                actual = net.eval()
                net.pop()
                squares = (ctypes.c_int * 64)()
                for sq, piece in board.piece_map().items():
                    squares[sq] = piece.piece_type + (0 if piece.color else 6)
                self.assertTrue(net.lib.kf_push(net.ptr, squares, int(not board.turn)))
                self.assertEqual(actual, net.eval())
                # Keep reference parent on stack to test incremental updates too.
                moves = list(board.legal_moves)
                if not moves:
                    break
                board.push(rng.choice(moves))
        finally:
            for _ in range(len(board.move_stack) + 1):
                net.pop()
            net.close()

    def test_quiescence_terminal_and_quiet_promotions(self):
        for fen, expected in [
            ('7k/5Q2/5K2/8/8/8/8/8 b - - 0 1', 0),
            ('7k/6Q1/5K2/8/8/8/8/8 b - - 100 1', -MATE),
            ('7k/8/8/8/8/8/8/R5K1 w - - 100 1', 0),
        ]:
            search = Search(chess.Board(fen), threading.Event())
            self.assertEqual(search.negamax(0, -MATE-1, MATE+1)[0], expected)
        search = Search(chess.Board('7k/P7/8/8/8/8/8/6K1 w - - 0 1'), threading.Event())
        score, pv = search.negamax(0, -MATE-1, MATE+1)
        self.assertEqual(score, 900)
        self.assertEqual(pv[0].promotion, chess.QUEEN)

    def test_stop_restores_board_and_reports_all_nodes(self):
        board = chess.Board()
        net = NNUE(ROOT / 'networks/lichess500k.nnue')
        search = Search(board, threading.Event(), net, nodes=1000)
        lines = []
        try:
            move = search.run(10, emit=lines.append)
            self.assertIn(move, board.legal_moves)
            self.assertEqual(board.fen(), chess.STARTING_FEN)
            self.assertEqual(len(board.move_stack), 0)
            self.assertEqual(search.nodes, 1000)
            self.assertIn('nodes 1000 ', lines[-1])
            # A second independent search verifies balanced NNUE push/pop.
            followup = Search(board, threading.Event(), net)
            self.assertIsNotNone(followup.run(1))
        finally:
            net.close()
