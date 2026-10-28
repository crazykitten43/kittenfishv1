# Native KittenFish

This is the main engine, launched by both `./kittenfish` and `./kittenfish-cpp`.
The original Python engine is preserved on the `legacy/python-search` branch.
It uses the same verified KFQ1 model without retraining.

Build from the repository root:

```
./scripts/setup.sh
make -C native
./kittenfish-cpp
.venv/bin/python native/test_native.py
.venv/bin/python native/benchmark.py --output native-results.json
```

Everything in search is C++: legal moves, make/unmake, iterative deepening,
fail-soft alpha-beta, principal variation search, quiescence, ordering and NNUE.
The single worker supports asynchronous stop/readiness. Move ordering uses
transposition hints, MVV-LVA captures, promotions, killers and quiet-move history.
A bounded Hash table stores depth-qualified exact/lower/upper scores, with mate
normalization. Score keys include reversible path history and the halfmove clock
to avoid reusing results across different repetition contexts. This deliberately
reduces score-cache sharing; position-only ordering hints are shared more widely.
No null-move pruning, late-move reductions, futility or SEE/delta pruning yet.

Quiescence searches captures, promotions and every check evasion. There is a
96-ply safety bound. Threefold, fifty-move and insufficient-material draws are
treated as draws, with mate taking priority. Completed PV/score/depth remain
consistent when stopped; final statistics include the interrupted iteration.
Statistics also report qnodes, beta cutoffs and score-table cutoffs.

UCI: position startpos/fen with moves; go depth, nodes, movetime, clocks,
increments, movestogo, mate, searchmoves, infinite; stop; isready; ucinewgame;
quit. Options: Hash (1–1024 MiB, default 32), Move Overhead (default 100 ms),
EvalFile. Pondering and Chess960 are unsupported. Set ponder false when integrating with lichess-bot. NNUE failure exits explicitly; there is no silent material fallback.
Time allocation matches the Python baseline; no extra time is used to inflate
benchmark results. `go mate` limits depth, rather than performing proof search.

Offline diagnostics: `eval`, `moves`, `perft N` (0–7), `verify`. Verify compares
incremental/fresh accumulators for every legal move from the initial diagnostic
position and a deterministic random continuation. Python tests compare against
python-chess legal moves and the original C++ NNUE bridge, run known perft
positions, and exercise UCI, tactics, special moves, draw rules and time limits.
These tests are not a rating measurement.

Move generation dependency: Disservin/chess-library, MIT, vendored header at
commit 2a608d58f44776909bb6e2c09505e97ba1451319.
https://github.com/Disservin/chess-library/tree/2a608d58f44776909bb6e2c09505e97ba1451319
The original license is in vendor/LICENSE. Its header is unmodified.
Native NNUE definitions are reused from ../nnue/upstream/main.cpp; see that
folder's provenance. Test dependency python-chess remains GPL-3.0-or-later.
