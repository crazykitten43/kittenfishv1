"""Offline correctness and UCI tests. Run from repo root with .venv/bin/python."""
import ctypes
import pathlib
import random
import subprocess
import sys
import time
import unittest
import chess
import chess.engine
ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE = str(ROOT / 'kittenfish')
sys.path.insert(0, str(ROOT / 'src'))
from engine import NNUE


def diagnostic(commands):
    result = subprocess.run([ENGINE], input='\n'.join(commands + ['quit'])+'\n',
                            text=True, capture_output=True, timeout=30, check=True)
    if 'invalid command' in result.stdout:
        raise AssertionError(result.stdout)
    return result.stdout


class NativeTests(unittest.TestCase):
    def test_perft_known_positions(self):
        cases = [
            (chess.STARTING_FEN, 5, 4865609),
            ('r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1',4,4085603),
            ('8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1',5,674624),
        ]
        for fen, depth, count in cases:
            with self.subTest(fen=fen):
                self.assertIn(f'perft {count}\n', diagnostic([f'position fen {fen}',f'perft {depth}']))

    def test_move_generation_matches_python_chess(self):
        rng=random.Random(55); board=chess.Board(); boards=[]
        for _ in range(200):
            boards.append(board.copy());moves=list(board.legal_moves)
            if not moves:board=chess.Board()
            else:board.push(rng.choice(moves))
        commands=[]
        for b in boards:commands += ['position fen '+b.fen(),'moves']
        rows=[l.split()[3:] for l in diagnostic(commands).splitlines() if l.startswith('info string moves')]
        self.assertEqual(len(rows),len(boards))
        for b,moves in zip(boards,rows):self.assertEqual(set(moves),{m.uci() for m in b.legal_moves})

    def test_nnue_agrees_with_existing_bridge(self):
        net = NNUE(ROOT / 'networks/lichess500k.nnue')
        rng = random.Random(19)
        boards = [chess.Board(f) for f in [chess.STARTING_FEN,
            'r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1',
            '4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1',
            '4k3/P7/8/8/8/8/8/4K3 w - - 0 1']]
        board = chess.Board()
        for _ in range(100):
            moves=list(board.legal_moves)
            if not moves: break
            board.push(rng.choice(moves)); boards.append(board.copy())
        commands=[];expected=[]
        try:
            for board in boards:
                commands += ['position fen '+board.fen(), 'eval']
                net.push(board); expected.append(net.eval()); net.pop()
            actual=[int(l.rsplit(' ',1)[1]) for l in diagnostic(commands).splitlines() if l.startswith('info string eval ')]
            self.assertEqual(actual,expected)
        finally:net.close()

    def test_incremental_nnue_random_and_special_moves(self):
        for fen in [chess.STARTING_FEN,
                    'r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1',
                    '4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1',
                    '4k3/P7/8/8/8/8/8/4K3 w - - 0 1']:
            self.assertIn('info string verified ', diagnostic(['position fen '+fen,'verify']))

    def test_handshake_depth_pv_and_options(self):
        with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
            e.configure({'Hash': 4, 'Move Overhead': 25})
            self.assertIn('C++',e.id['name'])
            board=chess.Board()
            r=e.analyse(board,chess.engine.Limit(depth=4))
            self.assertEqual(r['depth'],4)
            self.assertEqual(r['score'].pov(chess.WHITE).score(),42)
            self.assertEqual(r['pv'][0].uci(),'c2c4')
            for move in r['pv']:
                self.assertIn(move,board.legal_moves); board.push(move)

    def test_time_nodes_restricted_roots_and_stop(self):
        with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
            board=chess.Board()
            for limit in [chess.engine.Limit(time=.3),chess.engine.Limit(white_clock=.3,black_clock=.3),chess.engine.Limit(nodes=1000)]:
                t=time.monotonic();r=e.play(board,limit,info=chess.engine.INFO_ALL)
                self.assertLess(time.monotonic()-t,1.5)
                self.assertIn(r.move,board.legal_moves)
                if limit.nodes:self.assertLessEqual(r.info.get('nodes',0),1000)
            for move in ['e2e4','d2d4']:
                r=e.play(board,chess.engine.Limit(nodes=1),root_moves=[chess.Move.from_uci(move)])
                self.assertEqual(r.move.uci(),move)
            analysis=e.analysis(board);time.sleep(.03);e.ping();analysis.stop()
            self.assertIn(analysis.wait().move,board.legal_moves)
            e.ping()

    def test_mate_and_draws(self):
        with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
            board=chess.Board('7k/8/5KQ1/8/8/8/8/8 w - - 0 1')
            r=e.play(board,chess.engine.Limit(depth=3));board.push(r.move);self.assertTrue(board.is_checkmate())
            for fen in ['7k/6Q1/5K2/8/8/8/8/8 b - - 100 1','7k/5Q2/5K2/8/8/8/8/8 b - - 0 1']:
                self.assertFalse(e.play(chess.Board(fen),chess.engine.Limit(depth=2)).move)
            board=chess.Board()
            for move in ['g1f3','g8f6','f3g1','f6g8']*2:board.push_uci(move)
            r=e.analyse(board,chess.engine.Limit(depth=3));self.assertEqual(r['score'].pov(chess.WHITE).score(),0)
            board=chess.Board('7k/8/8/8/8/8/8/R5K1 w - - 100 1')
            r=e.analyse(board,chess.engine.Limit(depth=3));self.assertEqual(r['score'].pov(chess.WHITE).score(),0)

    def test_tactics_and_special_move_search(self):
        cases=[('7k/8/8/8/8/8/q7/R5K1 w - - 0 1', 'a1a2'),
               ('7k/P7/8/8/8/8/8/6K1 w - - 0 1','a7a8q')]
        with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
            for fen,expected in cases:
                r=e.play(chess.Board(fen),chess.engine.Limit(depth=3));self.assertEqual(r.move.uci(),expected)
            board=chess.Board('4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1')
            r=e.play(board,chess.engine.Limit(depth=2),root_moves=[chess.Move.from_uci('e5d6')]);self.assertEqual(r.move.uci(),'e5d6')

if __name__=='__main__':unittest.main(verbosity=2)
