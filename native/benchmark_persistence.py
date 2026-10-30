"""Measure cold/repeated/advanced-root cache reuse with the same NNUE."""
import pathlib,json,time
import chess,chess.engine
ROOT=pathlib.Path(__file__).resolve().parents[1]
ENGINE=str(ROOT/'kittenfish')
rows=[]
def run(engine,board,label,game):
 t=time.monotonic();r=engine.analyse(board,chess.engine.Limit(depth=7),game=game)
 check=board.copy()
 for m in r['pv']:assert m in check.legal_moves;check.push(m)
 rows.append(dict(case=label,depth=r['depth'],nodes=r['nodes'],seconds=time.monotonic()-t,pv=[m.uci() for m in r['pv']],score=r['score'].relative.score(mate_score=30000),diagnostics=r['string']))
 print(json.dumps(rows[-1]),flush=True);return r
with chess.engine.SimpleEngine.popen_uci(ENGINE) as warm:
 warm.configure({'Hash':16});game=object();b=chess.Board()
 first=run(warm,b,'cold-root',game);run(warm,b,'repeated-root',game)
 for m in first['pv'][:2]:b.push(m)
 run(warm,b,'advanced-root-retained',game)
 with chess.engine.SimpleEngine.popen_uci(ENGINE) as cold:
  cold.configure({'Hash':16});run(cold,b,'advanced-root-fresh',object())
(ROOT/'native/results/persistent-tree-comparison.json').write_text(json.dumps(rows,indent=2)+'\n')
