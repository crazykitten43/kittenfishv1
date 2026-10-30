"""UCI pondering lifecycle and bounded search-tree persistence regressions."""
import json,pathlib,queue,re,subprocess,tempfile,threading,time,unittest
import chess,chess.engine
ROOT=pathlib.Path(__file__).resolve().parents[1]
ENGINE=str(ROOT/'kittenfish')

class Wire:
 def __init__(self):
  self.p=subprocess.Popen([ENGINE],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
  self.lines=queue.Queue()
  def reader():
   for line in self.p.stdout:self.lines.put(line.strip())
  self.thread=threading.Thread(target=reader,daemon=True);self.thread.start()
  self.send('uci');self.until('uciok');self.send('setoption name Move Overhead value 0');self.send('setoption name Hash value 4')
 def send(self,s):self.p.stdin.write(s+'\n');self.p.stdin.flush()
 def until(self,prefix,timeout=5):
  out=[];deadline=time.monotonic()+timeout
  while time.monotonic()<deadline:
   try:line=self.lines.get(timeout=max(.001,deadline-time.monotonic()))
   except queue.Empty:break
   out.append(line)
   if line.startswith(prefix):return out
  raise AssertionError((prefix,out))
 def quiet(self,seconds=.08):
  end=time.monotonic()+seconds;out=[]
  while time.monotonic()<end:
   try:out.append(self.lines.get(timeout=max(.001,end-time.monotonic())))
   except queue.Empty:break
  return out
 def search(self,position='position startpos',go='go depth 4'):
  self.send(position);self.send(go);return self.until('bestmove')
 def close(self):
  if self.p.poll() is None:self.send('quit');self.p.wait(timeout=5)
  self.p.stdin.close();self.p.stdout.close();self.p.stderr.close();self.thread.join(timeout=1)
 def __enter__(self):return self
 def __exit__(self,*a):self.close()

def count(lines,name):
 text=' '.join(lines);matches=re.findall(r'\b'+name+r' (\d+)',text);assert matches,text;return int(matches[-1])

class PonderTests(unittest.TestCase):
 def test_ponderhit_starts_clock_without_restarting_search(self):
  with Wire() as w:
   w.send('position startpos');w.send('go ponder movetime 80')
   before=w.quiet(.2);self.assertFalse(any(x.startswith('bestmove') for x in before))
   self.assertTrue(any(x.startswith('info depth ') for x in before))
   w.send('isready');self.assertFalse(any(x.startswith('bestmove') for x in w.until('readyok')))
   t=time.monotonic();w.send('ponderhit');after=w.until('bestmove');elapsed=time.monotonic()-t
   self.assertGreater(elapsed,.02);self.assertLess(elapsed,1)
   self.assertEqual(count(before+after,'persistent_searches'),1)
   self.assertIn(chess.Move.from_uci(after[-1].split()[1]),chess.Board().legal_moves)

 def test_finished_ponder_waits_for_hit_or_stop(self):
  for release in ['ponderhit','stop']:
   with Wire() as w:
    w.send('position startpos');w.send('go ponder depth 1')
    w.until('info depth 1');self.assertFalse(any(x.startswith('bestmove') for x in w.quiet()))
    w.send(release);self.assertTrue(w.until('bestmove')[-1].startswith('bestmove '))

 def test_ponder_miss_and_new_game_clear(self):
  with Wire() as w:
   w.send('position startpos moves e2e4 e7e5');w.send('go ponder nodes 10000')
   self.assertFalse(any(x.startswith('bestmove') for x in w.quiet(.15)))
   w.send('stop');w.until('bestmove')
   result=w.search('position startpos moves e2e4 c7c5','go nodes 10000')
   b=chess.Board();b.push_uci('e2e4');b.push_uci('c7c5')
   self.assertIn(chess.Move.from_uci(result[-1].split()[1]),b.legal_moves)
   self.assertGreater(count(result,'retained_before'),0)
   w.send('ucinewgame');result=w.search();self.assertEqual(count(result,'retained_before'),0)

 def test_memory_survives_moves_and_clear_hash(self):
  with Wire() as w:
   first=w.search();second=w.search('position startpos moves e2e4 e7e5')
   self.assertEqual(count(first,'persistent_searches'),1);self.assertEqual(count(second,'persistent_searches'),2)
   self.assertGreater(count(second,'retained_before'),0)
   w.send('setoption name Clear Hash');self.assertEqual(count(w.search(),'retained_before'),0)

 def test_disk_roundtrip_and_corruption_rejection(self):
  with tempfile.TemporaryDirectory() as d:
   path=pathlib.Path(d)/'game.tree'
   with Wire() as w:
    w.send('setoption name TreeFile value '+str(path));w.search();w.send('setoption name Save Tree');w.send('isready');w.until('readyok')
    self.assertTrue(path.exists())
    w.send('setoption name Clear Hash');w.send('setoption name Load Tree')
    self.assertGreater(count(w.search(),'retained_before'),0)
   saved=path.read_bytes();self.assertTrue(saved.startswith(b'KFTREE02'))
   with Wire() as w:
    w.send('setoption name TreeFile value '+str(path));w.send('setoption name Load Tree')
    self.assertGreater(count(w.search(),'retained_before'),0)
   broken=bytearray(path.read_bytes());broken[-1]^=1;path.write_bytes(broken)
   with Wire() as w:
    w.send('setoption name TreeFile value '+str(path));w.send('setoption name Load Tree')
    error=w.until('info string invalid command');self.assertIn('checksum mismatch',' '.join(error))
    self.assertEqual(count(w.search(),'retained_before'),0)
   self.assertEqual(path.read_bytes(),broken,'failed load must not overwrite snapshot on quit')

 def test_options_invalidate_scores_and_snapshot_identity(self):
  with tempfile.TemporaryDirectory() as d,Wire() as w:
   path=pathlib.Path(d)/'game.tree';w.search();w.send('setoption name TreeFile value '+str(path))
   w.send('setoption name Save Tree');w.send('setoption name LMR value false')
   self.assertEqual(count(w.search(),'retained_before'),0)
   w.send('setoption name Load Tree');self.assertIn('model/options mismatch',' '.join(w.until('info string invalid command')))

 def test_python_chess_ponder_integration(self):
  with chess.engine.SimpleEngine.popen_uci(ENGINE) as e:
   e.configure({'Hash':4,'Move Overhead':0});b=chess.Board();game=object()
   r=e.play(b,chess.engine.Limit(time=.06),ponder=True,game=game)
   self.assertIsNotNone(r.ponder);b.push(r.move);self.assertIn(r.ponder,b.legal_moves);b.push(r.ponder)
   r=e.play(b,chess.engine.Limit(time=.06),ponder=True,game=game);self.assertIn(r.move,b.legal_moves)

if __name__=='__main__':unittest.main(verbosity=2)
