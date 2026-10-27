"""Offline same-host UCI comparison. Writes JSON; never contacts Lichess."""
import argparse
import json
import pathlib
import time
import chess
import chess.engine
ROOT=pathlib.Path(__file__).resolve().parents[1]
POSITIONS={
 'start':chess.STARTING_FEN,
 'middlegame':'r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 4 8',
 'endgame':'8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1',
}
p=argparse.ArgumentParser();p.add_argument('--python-engine',default=str(ROOT/'kittenfish'));p.add_argument('--output',default='native-results.json');args=p.parse_args()
results=[]
for label,path in [('python',args.python_engine),('cpp',str(ROOT/'kittenfish-cpp'))]:
 with chess.engine.SimpleEngine.popen_uci(path,timeout=60) as e:
  for name,fen in POSITIONS.items():
   for mode,limit in [('depth4',chess.engine.Limit(depth=4)),('time2s',chess.engine.Limit(time=2))]:
    board=chess.Board(fen);started=time.monotonic();info=e.analyse(board,limit);wall=time.monotonic()-started
    check=board.copy()
    for m in info.get('pv',[]):
     assert m in check.legal_moves,(label,name,m);check.push(m)
    row=dict(engine=label,position=name,mode=mode,wall_seconds=round(wall,6),
             depth=info.get('depth'),nodes=info.get('nodes'),nps=info.get('nps'),
             reported_seconds=info.get('time'),score_cp=info['score'].pov(board.turn).score(mate_score=30000),
             pv=[m.uci() for m in info.get('pv',[])],diagnostics=info.get('string'))
    results.append(row);print(json.dumps(row),flush=True)
pathlib.Path(args.output).write_text(json.dumps(results,indent=2)+'\n')
