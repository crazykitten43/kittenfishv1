import argparse,chess,chess.engine,chess.pgn,pathlib,json
p=argparse.ArgumentParser();p.add_argument("--baseline",required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];rows=[]
with chess.engine.SimpleEngine.popen_uci([str(pathlib.Path(a.baseline).resolve()),str(root/'networks/kf100m.nnue')]) as old,chess.engine.SimpleEngine.popen_uci(str(root/'kittenfish')) as new:
 for engine in [old,new]:engine.configure({'Hash':4,'Move Overhead':0})
 for opening in ['e2e4 e7e5 g1f3 b8c6 f1b5 a7a6','d2d4 d7d5 c2c4 e7e6 b1c3 g8f6']:
  for new_white in [True,False]:
   b=chess.Board();game=chess.pgn.Game();node=game
   for m in opening.split():b.push_uci(m);node=node.add_variation(b.peek())
   for ply in range(160):
    if b.is_game_over(claim_draw=True):break
    e=new if b.turn==new_white else old
    m=e.play(b,chess.engine.Limit(nodes=10000),game=game).move
    assert m in b.legal_moves
    b.push(m);node=node.add_variation(m)
   result=b.result(claim_draw=True);game.headers.update(White='selective' if new_white else 'baseline',Black='baseline' if new_white else 'selective',Result=result)
   rows.append(dict(opening=opening,new_white=new_white,result=result,plies=len(b.move_stack),pgn=str(game)))
   print({k:v for k,v in rows[-1].items() if k!='pgn'},flush=True)
(root/'native/results/selective-smoke-matches.json').write_text(json.dumps(rows,indent=2)+'\n')
