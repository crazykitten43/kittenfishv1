"""Search correctness, exchange legality, pruning activation, and UCI limits."""
import pathlib
import re
import subprocess
import time
import unittest
import chess
import chess.engine
ROOT=pathlib.Path(__file__).resolve().parents[1]
ENGINE=str(ROOT/'kittenfish')
MIDDLE='r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 4 8'
NEW=['Null Move Pruning','LMR','SEE','Futility Pruning','Aspiration Windows','Check Extensions','Adaptive Time']

def command(lines):
 r=subprocess.run([ENGINE],input='\n'.join(lines+['quit'])+'\n',capture_output=True,text=True,check=True,timeout=30)
 assert 'invalid command' not in r.stdout,r.stdout
 return r.stdout

class SelectiveTests(unittest.TestCase):
 def test_see_special_moves_and_legal_recaptures(self):
  cases=[
   ('r6k/8/8/8/8/8/p7/Q5K1 w - - 0 1','a1a2',-800),
   ('4k3/4n3/2p5/1B6/8/8/8/4R1K1 w - - 0 1','b5c6',100),
   ('8/2k5/2p5/1B6/8/8/8/2R3K1 w - - 0 1','b5c6',100),
   ('k7/8/8/3pP3/8/8/7K/3r4 w - d6 0 1','e5d6',0),
   ('7k/P7/8/8/8/8/8/6K1 w - - 0 1','a7a8q',800),
   ('k7/8/8/8/8/7K/4Rp2/4r3 w - - 0 1','e2e1',-800),
  ]
  for fen,move,expected in cases:
   with self.subTest(move=move,fen=fen):
    b=chess.Board(fen);self.assertIn(chess.Move.from_uci(move),b.legal_moves)
    self.assertIn(f'info string see {expected}\n',command(['position fen '+fen,'see '+move]))

 def test_aspiration_matches_full_window_without_selectivity(self):
  for fen in [chess.STARTING_FEN,MIDDLE,'8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1']:
   scores=[]
   with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
    e.configure({name:False for name in NEW})
    for aspiration in [False,True]:
     e.configure({'Aspiration Windows':aspiration})
     info=e.analyse(chess.Board(fen),chess.engine.Limit(depth=5))
     scores.append(info['score']);self.assertEqual(info['depth'],5)
   self.assertEqual(*scores)

 def test_pruning_counters_and_pv_legality(self):
  with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
   info=e.analyse(chess.Board(MIDDLE),chess.engine.Limit(nodes=120000))
   counters=dict((name,int(n)) for name,n in re.findall(r'(\w+) (\d+)',info['string']))
   for name in ['null_cutoffs','lmr_reductions','see_prunes','futility_prunes','check_extensions']:
    self.assertGreater(counters[name],0,(name,counters))
   self.assertLessEqual(info['nodes'],120000)
   b=chess.Board(MIDDLE)
   for m in info['pv']:self.assertIn(m,b.legal_moves);b.push(m)

 def test_pawn_endings_do_not_use_null_move(self):
  with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
   r=e.analyse(chess.Board('8/8/4k3/4p3/4P3/4K3/8/8 w - - 0 1'),chess.engine.Limit(depth=8))
   self.assertIn('null_cutoffs 0 ',r['string'])

 def test_check_evasions_quiescence_and_mate(self):
  with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
   for fen in ['4r2k/8/8/8/8/8/8/4K3 w - - 0 1',
               '7k/8/5KQ1/8/8/8/8/8 w - - 0 1']:
    b=chess.Board(fen);r=e.play(b,chess.engine.Limit(depth=4))
    self.assertIn(r.move,b.legal_moves)
    if not b.is_check():b.push(r.move);self.assertTrue(b.is_checkmate())

 def test_clock_hard_limit_and_restricted_root(self):
  with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
   e.configure({'Move Overhead':0})
   for limit in [chess.engine.Limit(time=.05),chess.engine.Limit(white_clock=.15,black_clock=.15,white_inc=10,black_inc=10)]:
    t=time.monotonic();r=e.play(chess.Board(MIDDLE),limit)
    self.assertLess(time.monotonic()-t,.8)
    self.assertIn(r.move,chess.Board(MIDDLE).legal_moves)
   for nodes in [1,2,50]:
    m=chess.Move.from_uci('e2e4')
    self.assertEqual(e.play(chess.Board(),chess.engine.Limit(nodes=nodes),root_moves=[m]).move,m)

if __name__=='__main__':unittest.main(verbosity=2)
