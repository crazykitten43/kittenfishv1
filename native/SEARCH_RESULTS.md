# Selective search validation — 30 October 2026

Baseline: repository commit `6d7c3117b03c1052188b4ac26f0526fadbcddde7`,
compiled before edits. Both versions used the unchanged, checksum-verified
`kf100m.nnue`, Hash=32 MiB, one worker, Move Overhead=0. Local Apple Silicon,
Clang C++17 release build. These are three diagnostic positions, not a broad
performance or strength estimate.

| Position / budget | Before | After |
|---|---:|---:|
| Middlegame, 7 million nodes: completed depth | 7 | 10 |
| Middlegame, 7 million nodes: wall time | 11.51 s | 13.63 s |
| Middlegame, 1 second: completed depth | 6 | 8 |
| Start position, 1 second: completed depth | 7 | 9 |
| Endgame, 1 second: completed depth | 9 | 10 |
| Middlegame, nominal depth 6: nodes | 576,422 | 46,741 |

The new search trades some raw NPS for selectivity. Reduced/pruned depth is not
an exhaustive depth guarantee, and before/after nominal-depth-six trees are not
equivalent searches. At seven million nodes the new search's selective depth was
25; its **completed nominal depth was 10**, not 25.

Validation:

- Existing native suite: 8 passed.
- New selective-search suite: 6 passed.
- Existing bridge/reference suite: 3 passed.
- ASan/UBSan: 5 protocol/search tests passed without sanitizer findings.
- Four offline smoke games, two paired openings, 10k nodes/move, Hash=4 MiB:
  selective engine won 3 and drew 1. This sample is much too small for an Elo
  estimate. PGNs and results are in `results/selective-smoke-matches.json`.

Reproduce with `native/benchmark_search.py --baseline /path/to/old-binary`
and `native/match_smoke.py --baseline /path/to/old-binary` using Python with
requirements installed. Raw timing, nodes, PVs and pruning counters are in
`results/selective-alpha-beta-comparison.json`. No live Lichess games or cloud
resources were used, and the running bot was not changed.
