# Selective alpha-beta search

The native engine uses the existing KFQ1 NNUE unchanged. No dataset generation,
GPU or additional model is required. The Python reference search is unchanged.

## Requested features

| Feature | Native implementation |
|---|---|
| Iterative-deepening ordering | Last completed root move first; position hints and cached best moves below root |
| MVV-LVA | Victim/attacker capture ordering, with SEE-negative captures behind quiet moves |
| Transpositions / Zobrist | Existing board Zobrist keys; bounded Hash table with exact/lower/upper bounds, depth and mate-distance normalization |
| Killer moves | Two quiet beta-cutoff moves per ply |
| History | Bounded, gravity-updated quiet-move scores; reward cutoffs and penalize previously searched quiet failures |
| Quiescence | Stand pat only outside check; captures/promotions, or all legal evasions when checked; terminal/draw checks retained |
| PVS | Full first-move window, later zero-window probes and full-window re-searches, including root |
| Null move | Non-PV, depth >=3, static eval >= beta, no check, no consecutive/artificial-subtree nulls, at least two non-pawn/non-king pieces for mover, halfmove clock <90; deep cutoffs verified |
| LMR | Later quiet non-checking moves at depth >=3; spare first three, killers, TT move and strong history; reductions capped at three plies; promising results re-searched at full depth |
| SEE | Material swap-off, x-rays, promotions, en passant; reject pinned recaptures and attacked king captures; prune negative exchanges only in non-check quiescence, excluding promotions/checks |
| Futility | Shallow non-PV quiet-move pruning, only after searching a move; exempt checks, promotions, evasions, first/TT move, killers and mate windows |
| Aspiration | +/-25 cp from depth four, exponential widening on failure; only commit a completed successful iteration |
| Check extensions | One ply at checked main-search nodes, capped at eight extensions per line plus absolute stack limit |
| Time management | Clock-derived soft target, stability/score-drop adjustment, forced-root reduction, independent hard cap bounded by remaining clock minus overhead; explicit movetime remains a hard limit |

The score TT deliberately retains reversible-history context and halfmove clock
in its key. This is conservative about repetition/fifty-move draws and limits
score reuse between different reversible paths. Position-only move hints are
shared safely. Null subtrees do not read/write real-path TT score bounds or
claim repetition/fifty-move draws from the artificial pass.

## UCI switches

All default to true: `Null Move Pruning`, `LMR`, `SEE`, `Futility Pruning`,
`Aspiration Windows`, `Check Extensions`, `Adaptive Time`. Set them individually
with `setoption name LMR value false`, for example. This permits ablation tests;
it is not a promise that every combination improves playing strength.

`info seldepth` reports deepest visited ply; `info depth` is the completed
iteration's nominal depth. Pruned/reduced branches need not reach that depth.
The final `info string` includes reduction, re-search, null cutoff, SEE/futility
prune, check-extension and existing beta/TT counters. `see <legal-uci-move>`
prints a material exchange score without changing the root position.

## Validation

Run `make test`, or run `native/test_native.py`, `native/test_selective.py` and
`python -m unittest discover -s tests -v` using an environment with requirements
installed. Tests cover perft, NNUE agreement/update correctness, UCI limits,
mate/draw handling, legal PVs, SEE edge cases, aspiration consistency without
selective pruning, pruning activation and pawn-ending null safeguards.

To reproduce the before/after benchmark, preserve an old native executable,
then run:

```
python native/benchmark_search.py --baseline /path/to/old-kittenfish-cpp
```

Both executables receive the same current network. Results include fixed-depth,
fixed-node and fixed-time cases and a seven-million-node middlegame search.
Depth gains and tactical smoke tests do not establish an Elo gain. Match testing
with paired openings and a larger tactical/endgame suite is still needed before
claiming increased playing strength. These heuristics are an initial untuned
implementation, not Stockfish parity.
