"""Compare old and new native searches with one shared, unchanged NNUE."""
import argparse,json,pathlib,time
import chess,chess.engine
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--baseline',required=True);p.add_argument('--output',default='native/results/selective-alpha-beta-comparison.json');a=p.parse_args()
positions={
 'start':chess.STARTING_FEN,
 'middlegame':'r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 4 8',
 'endgame':'8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1'}
rows=[]
for label,command in [('baseline',[str(pathlib.Path(a.baseline).resolve()),str(ROOT/'networks/kf100m.nnue')]),('selective',[str(ROOT/'kittenfish')])]:
 with chess.engine.SimpleEngine.popen_uci(command,timeout=180) as engine:
  engine.configure({'Hash':32,'Move Overhead':0})
  for name,fen in positions.items():
   modes=[('depth6',chess.engine.Limit(depth=6)),('nodes1m',chess.engine.Limit(nodes=1000000)),('time1s',chess.engine.Limit(time=1))]
   if name=='middlegame':modes.append(('nodes7m',chess.engine.Limit(nodes=7000000)))
   for mode,limit in modes:
    start=time.monotonic();r=engine.analyse(chess.Board(fen),limit,game=object());elapsed=time.monotonic()-start
    b=chess.Board(fen)
    for m in r.get('pv',[]):assert m in b.legal_moves;b.push(m)
    row=dict(engine=label,position=name,mode=mode,depth=r.get('depth'),seldepth=r.get('seldepth'),nodes=r.get('nodes'),nps=r.get('nps'),wall_seconds=round(elapsed,6),score_cp=r['score'].relative.score(mate_score=30000),pv=[m.uci() for m in r.get('pv',[])],diagnostics=r.get('string'))
    rows.append(row);print(json.dumps(row),flush=True)
    output=ROOT/a.output;output.parent.mkdir(exist_ok=True,parents=True);output.write_text(json.dumps(rows,indent=2)+'\n')
