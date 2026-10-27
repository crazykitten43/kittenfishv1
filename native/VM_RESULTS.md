# C++ engine offline VM results — 27 October 2026

Source: `experimental/cpp-search`, engine commit `c3b66f5258ca31183cf494c806eab44b9d505070`.
Draft PR: https://github.com/crazykitten43/kittenfishv1/pull/2

The Python engine, main branch and live bot were not changed by this experiment.
The new executable is `kittenfish-cpp`; its source and build are in `native/`.

## Test environment

Temporary VM: `kittenfish-cpp-test-20261027`, `us-west1-b`, `c2d-highcpu-2`.
Ubuntu 24.04, AMD EPYC 7B13, 2 vCPUs, GCC 13.3.0, Python 3.12.3.
The engine uses one search worker. Compiler flags: `-O3 -DNDEBUG -std=c++17`.
Source was cloned from GitHub, built on the VM and tested without connecting to
Lichess. The same pinned, checksum-verified KFQ1 model was used for both engines.

Python reference: the optimized engine at `dfe84b8903e820dbbd047b34b20e7e632346c12b`,
not merely the slower main-branch version. Both reference versions were measured;
raw JSON files are saved alongside this report.

## Same-host two-second comparisons

Both engines reserve 100 ms, leaving 1.9 seconds for search. Wall times include
per-search setup (about 20 ms for native Hash/state allocation).

| Position | Python depth | C++ depth | Python NPS | C++ NPS | NPS ratio |
|---|---:|---:|---:|---:|---:|
| start | 5 | 7 | 33,175 | 570,703 | 17.2× |
| middlegame | 4 | 6 | 31,460 | 562,227 | 17.9× |
| endgame | 6 | 9 | 29,841 | 571,206 | 19.1× |

## Equal-depth correctness and wall time

At depth 4 all three positions produced identical scores, best moves and PVs
across the original Python engine, optimized Python engine and native engine.
Native ordering/PVS/score caching explored fewer nodes. Different search trees
mean NPS ratios alone do not express the full change in depth or playing strength.
Short runs also make reported millisecond-resolution NPS less stable.

| Position | Optimized Python wall seconds | C++ wall seconds | Python nodes | C++ nodes |
|---|---:|---:|---:|---:|
| start | 0.2661 | 0.0257 | 8,019 | 3,899 |
| middlegame | 0.7974 | 0.0536 | 23,367 | 17,160 |
| endgame | 0.0919 | 0.0284 | 2,519 | 2,149 |

## Correctness checks

- All eight native tests passed locally, on the release VM build, and on a VM
  build with undefined-behavior sanitizer and fail-on-error enabled.
- Known perft totals: start depth 5 = 4,865,609; Kiwipete depth 4 = 4,085,603;
  rook/pawn endgame depth 5 = 674,624. These cover special moves and legality.
- Legal moves matched python-chess over 200 deterministic random positions.
- NNUE evaluations matched the existing bridge across 104 positions.
- Incremental accumulators matched fresh rebuilds for every legal initial move
  in special-move test positions and deterministic random continuations.
- UCI handshake/options, legal PVs, time/node limits, restricted roots, stop,
  checkmate, repetition, fifty-move draw, promotion and tactical captures passed.
- All six optimized Python regression tests passed on this VM.
- GitHub native and existing-engine CI jobs passed for the engine commit.

## Search behavior and limitations

Native alpha-beta/PVS, stand-pat quiescence, Hash score bounds, transposition move
hints, capture/promotion ordering, killers and history are implemented. The score
table includes reversible-path context and halfmove clock; it deliberately shares
less than an unrestricted position-only TT. No null-move pruning, LMR, futility,
SEE/delta pruning, pondering or Chess960 is enabled.

In the start-position timed native run, 861,194 of 1,084,337 nodes were quiescence
nodes; the engine reported 835,624 beta/stand-pat cutoffs and 50,875 TT cutoffs.
These counters demonstrate pruning but are not a percentage of the unpruned tree.
Quiescence remains a substantial next area for careful tuning.

The tests are a first validation suite, not an Elo measurement or proof of tactical
completeness. No online games were played. The production VM was untouched.

## VM shutdown

The stop command was issued after downloading all results. The temporary VM's
10 GB persistent boot disk is retained so the experiment can be resumed; stopped
VMs do not incur running compute charges, but retained disk storage remains billable.
