# Pondering and the persistent search tree

The native engine now retains a bounded transposition-backed search tree between
`go` commands and position changes. It preserves score bounds, best-move edges,
quiet-move history and killer moves. It can reuse information along the played
line and other previously searched branches. It does not retain every leaf or
allocate an unbounded explicit tree. Hash bounds the score table; position-only
move hints are capped at 65,537 entries. NNUE and the Python reference engine are
unchanged.

## Pondering

The standard UCI `Ponder` check option defaults to false. The GUI decides when to
send `go ponder`, normally after installing the position including the engine's
move and its predicted opponent reply. With Ponder enabled, `bestmove` includes a
legal `ponder` reply whenever the completed PV contains one.

- `go ponder ...` searches without consuming its clock budget. Search time and
  node counters still include the background work.
- `ponderhit` continues the same search and starts the hard/soft move budgets at
  the hit timestamp. It does not rebuild the tree or reset node counts.
- Depth/node limits and early mate/terminal results may finish pondering work,
  but `bestmove` is withheld until `ponderhit` or `stop`.
- On a wrong prediction, the GUI sends `stop`, consumes the old `bestmove`, sends
  the actual `position`, then starts a new `go`. Cached information survives;
  real-path score keys still distinguish different repetition histories.
- `isready` responds while pondering. `quit` stops and joins the worker.
- `go infinite` remains unclocked until stop, including if combined with ponder.

Example protocol (the GUI must wait/read responses in the usual UCI sequence):

```
setoption name Ponder value true
position startpos moves e2e4 e7e5
go ponder wtime 60000 btime 60000 winc 1000 binc 1000
ponderhit
```

To generate a new lichess-bot configuration with pondering enabled:

```
python scripts/configure_lichess_bot.py /path/to/lichess-bot --ponder
```

For an existing config, set `engine.ponder: true`. The helper deliberately refuses
to overwrite an existing config. No live configuration or running bot is changed
by this repository update. Pondering keeps a CPU worker active during opponent
thinking time; fixed depth/node limits can bound the work.

## Optional persistence across process restarts

By default the tree persists in RAM for the current engine process. To save it
across restarts, supply a writable absolute path:

```
setoption name TreeFile value /absolute/path/kittenfish.tree
```

New paths are saved automatically on clean exit. `setoption name Save Tree`
forces a snapshot. Existing files require explicit `setoption name Load Tree` to
restore them; setting a path alone never silently trusts or overwrites an
existing file. After a successful load, clean exits can update it. Failed loads
preserve both the active tree and the existing snapshot. A crashed/killed process
cannot save its latest work; checkpoints are not written after every node/move.

Snapshots contain TT entries and move hints, not training weights, full boards,
all legal child lists, clock state, or the ongoing search stack. Killer/history
statistics persist in RAM but are not serialized. The GUI must still provide the
root position and complete move history. To resume a saved tree, issue Load Tree
**after** the GUI's initial `ucinewgame`, which deliberately clears RAM.

Versioned little-endian snapshots include an integrity checksum, a network
fingerprint and search-option signature. Loading validates format, identity,
counts, records and checksum before swapping state. Temporary load memory can
reach roughly twice the configured TT size. Saving writes a temporary file and
atomically renames it; this is not a guarantee against sudden power loss. The
checksum/fingerprint detect accidental corruption/mismatches, not malicious
changes. Tree snapshots should be trusted local files.

## Invalidation and boundaries

- `ucinewgame` and the `Clear Hash` button clear the in-memory tree/history.
- Changing Hash rebuilds the bounded table.
- Changing the evaluator or search-selectivity options clears old scores.
- Ponder, clock/overhead settings and TreeFile do not invalidate score entries.
- Illegal cached hint moves never bypass legal move generation.
- Existing reversible-history and halfmove-clock score keys remain in use;
  null-move subtrees cannot contaminate real-path score bounds.

`info string` reports `persistent_searches` and `retained_before`, alongside the
existing pruning counters. These are cache diagnostics, not a playing-strength
measurement. Run `native/test_ponder.py` for hit/miss/stop/clock, GUI integration,
new-game/reset, snapshot roundtrip and corruption/identity checks.

## Local validation sample

With the unchanged 100M NNUE, Hash=16 MiB and nominal depth seven:

| Case | Nodes | Wall time |
|---|---:|---:|
| Fresh starting position | 65,885 | 0.123 s |
| Same root with retained cache | 10,904 | 0.021 s |
| Advance two PV plies, retained cache | 115,269 | 0.217 s |
| Same advanced root, fresh cache | 91,555 | 0.177 s |

This confirms reuse, not a universal speedup: ordering/history can make a
particular advanced-root search take more work. Scores matched within each
comparison, but equal-score root choices can differ. Pondering's main benefit is
additional opponent-turn search time, not guaranteed faster raw node processing.
See `results/persistent-tree-comparison.json` and `benchmark_persistence.py`.

The full local suite passed 24 tests (8 native, 6 selective, 7 pondering/tree,
3 reference/bridge). All seven new lifecycle/persistence tests also passed under
ASan/UBSan. No live bot configuration, deployed process or NNUE was modified.
