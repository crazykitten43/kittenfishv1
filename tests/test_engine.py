import pathlib
import sys
import threading
import time
import unittest
import chess
import chess.engine

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from engine import NNUE, Search, MATE


class EngineTests(unittest.TestCase):
    def test_nnue_special_moves(self):
        net = NNUE(ROOT / 'networks/lichess500k.nnue')
        for fen, move in [
            ('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1', 'e1g1'),
            ('4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1', 'e5d6'),
            ('4k3/P7/8/8/8/8/8/4K3 w - - 0 1', 'a7a8q'),
            ('4k3/8/8/8/8/8/4K3/8 w - - 0 1', 'e2d3')]:
            board = chess.Board(fen)
            net.push(board)
            original = net.eval()
            board.push_uci(move)
            net.push(board)
            incremental = net.eval()
            net.pop()
            net.pop()
            net.push(board)
            self.assertEqual(incremental, net.eval())
            net.pop()
            board.pop()
            net.push(board)
            self.assertEqual(original, net.eval())
            net.pop()
        net.close()

    def test_terminal_and_repetition(self):
        board = chess.Board()
        for move in ['g1f3', 'g8f6', 'f3g1', 'f6g8'] * 2:
            board.push_uci(move)
        self.assertEqual(Search(board, threading.Event()).negamax(2, -MATE, MATE)[0], 0)
        for fen in ['7k/6Q1/5K2/8/8/8/8/8 b - - 0 1',
                    '7k/5Q2/5K2/8/8/8/8/8 b - - 0 1']:
            self.assertIsNone(Search(chess.Board(fen), threading.Event()).run(2))

    def test_uci(self):
        with chess.engine.SimpleEngine.popen_uci(str(ROOT / 'kittenfish'), timeout=10) as engine:
            self.assertIn('C++', engine.id['name'])
            engine.ping()
            board = chess.Board()
            result = engine.play(board, chess.engine.Limit(depth=2))
            self.assertIn(result.move, board.legal_moves)
            start = time.monotonic()
            result = engine.play(board, chess.engine.Limit(time=.2))
            self.assertLess(time.monotonic() - start, 2)
            self.assertIn(result.move, board.legal_moves)
            result = engine.play(board, chess.engine.Limit(nodes=1), root_moves=[chess.Move.from_uci('e2e4')])
            self.assertEqual(result.move.uci(), 'e2e4')
            analysis = engine.analysis(board)
            time.sleep(.05)
            analysis.stop()
            self.assertIsNotNone(analysis.wait().move)
            engine.ping()
            mate = chess.Board('7k/8/5KQ1/8/8/8/8/8 w - - 0 1')
            result = engine.play(mate, chess.engine.Limit(depth=2))
            mate.push(result.move)
            self.assertTrue(mate.is_checkmate())
            result = engine.play(board, chess.engine.Limit(white_clock=.3, black_clock=.3))
            self.assertIn(result.move, board.legal_moves)
            dead = chess.Board('7k/6Q1/5K2/8/8/8/8/8 b - - 0 1')
            self.assertFalse(engine.play(dead, chess.engine.Limit(depth=1)).move)


if __name__ == '__main__':
    unittest.main()
