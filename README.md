# KittenFish v1

A working baseline chess engine: iterative deepening negamax alpha-beta search,
quiescence search, capture/promotion ordering, UCI, and the trained KittenFish NNUE.
Python 3.10+ handles search and legal moves through python-chess; C++17 handles
incremental NNUE evaluation. Linux and macOS are supported.

## Build and run

```sh
git clone https://github.com/crazykitten43/kittenfishv1.git
cd kittenfishv1
./scripts/setup.sh
./kittenfish
```

Requires Python with venv/pip, Make, and a C++17 compiler (g++/clang++).
The launcher locates the project's virtual environment, shared library and model
by absolute path, so it also works when launched from lichess-bot's directory.
Setup automatically downloads the exact upstream trained model and verifies its
published SHA256. A network connection is required on the first setup. If the
verified model is already present (for example in the downloadable complete
project archive), no model download is needed.

Example UCI session (wait for bestmove before sending quit):

```text
uci
isready
position startpos moves e2e4 e7e5
go movetime 1000
```

Supported: `uci`, `isready`, `ucinewgame`, `position startpos|fen ... moves ...`,
`go depth|nodes|movetime|wtime|btime|winc|binc|movestogo|mate`, `searchmoves`,
`go infinite`, `stop`, `ponderhit`, `setoption`, `quit`. `go mate N` limits the
search to 2N plies; it is not a dedicated proof search. Search runs on a worker
so the engine can respond to stop and readiness commands while thinking.
Info lines report depth, centipawns/mate, nodes, elapsed time, NPS and PV.
No legal moves returns `bestmove 0000`.

UCI options:

- `Move Overhead`: reserve milliseconds for communication (default 100).
- `EvalFile`: path to a custom KFQ1 network; default is the included model.
  A failed load keeps the existing evaluator and prints a diagnostic.

Pondering commands are handled, but the supplied bot setup disables pondering.
Only one search worker is implemented; there are no Threads, Hash, or Syzygy
options. Do not pass Stockfish-specific options.

## Connect to lichess-bot

Install [lichess-bot](https://github.com/lichess-bot-devs/lichess-bot#steps) and
its requirements using its official installation instructions. Use a Lichess
BOT account and a token with the scopes required by lichess-bot. Keep the token
on your machine.

After building KittenFish, run the following with **lichess-bot's Python
environment** (it supplies PyYAML):

```sh
/path/to/lichess-bot/venv/bin/python /path/to/kittenfishv1/scripts/configure_lichess_bot.py /path/to/lichess-bot
```

Use the actual name of your lichess-bot virtual environment. This reads that
installation's `config.yml.default`, writes a new private `config.yml`, sets
absolute engine paths and compatible UCI options, and enables standard chess.
It refuses to overwrite an existing configuration. Set the token placeholder
locally, then start lichess-bot following its official run instructions.
This repository does not start a live bot or upgrade an account automatically.

For an existing config, replace its engine path/protocol/options with:

```yaml
engine:
  dir: "/absolute/path/to/kittenfishv1"
  name: "kittenfish"
  working_dir: "/absolute/path/to/kittenfishv1"
  protocol: "uci"
  ponder: false
  uci_options:
    Move Overhead: 100
```

Keep the rest of your installed lichess-bot default engine settings. Remove
unsupported UCI options such as Threads, Hash, SyzygyPath and UCI_ShowWDL.
Set `challenge.variants` to `[standard]`. The generator also disables automatic
matchmaking; turn it on yourself when ready. Follow the
[official configuration reference](https://github.com/lichess-bot-devs/lichess-bot/blob/master/config.yml.default)
for the remaining bot settings.

## NNUE copied from kittenfishnnue

The **trained** `networks/lichess500k.nnue` installed by setup is copied from the upstream
`lichess-500k-v1` release, with its published SHA256 verified. The complete training/evaluation source now lives in
[kittenfishtraining/v1](https://github.com/crazykitten43/kittenfishtraining/tree/main/v1).
`nnue/upstream/main.cpp` retains only the inference definitions required by the engine.
See [provenance](nnue/PROVENANCE.md) for the exact source commit and asset.
The native bridge updates accumulators incrementally during make/unmake, with
king-bucket rebuilds and independent states for each ply.

```sh
shasum -a 256 networks/lichess500k.nnue
python3 scripts/fetch_network.py
```

The custom KFQ1 format is little-endian on supported targets and is incompatible
with Stockfish networks. If the native library or initial model cannot be loaded,
the engine reports the failure and uses material scores. Normal setup uses NNUE.

## Tests and limitations

```sh
make test
```

Tests compare incremental
and fresh model scores for castling, en passant, promotion and king moves, and use
python-chess's real UCI client for handshake, legal moves, time controls, restricted
root moves, node limits, stopping, mate and game-over positions. CI repeats these
checks on Linux and verifies the weight checksum.

Search recognizes checkmate, stalemate, insufficient material, threefold repetition
and the fifty-move rule, preserving the supplied game history. It treats available
threefold/fifty-move draws as draws during search. This is an initial implementation:
no transposition table, opening book, tablebases, SIMD tuning or measured rating.
Python search is intended as a functional baseline, with lower throughput than a
fully native engine. Quiescence includes check evasions, captures and promotions;
a hard 96-ply bound prevents unbounded checking sequences. Configure realistic time
controls for your host before enabling fast games.

## Dependency notices

Legal move generation and UCI integration testing use
[python-chess](https://github.com/niklasf/python-chess), copyright Niklas Fiekas
and contributors, licensed GPL-3.0-or-later. Its source is installed via the pinned
`chess==1.11.2` dependency. The complete NNUE trainer is maintained in
[kittenfishtraining/v1](https://github.com/crazykitten43/kittenfishtraining/tree/main/v1).
The NNUE source and model originate in your
[kittenfishnnue repository](https://github.com/crazykitten43/kittenfishnnue).

## Search performance diagnostics

Raw NNUE evaluations per second exclude legal move generation, alpha-beta,
quiescence, make/unmake, accumulator updates and Python/native transfer. Search
NPS includes those costs; do not compare the two as equivalent workloads.

The engine transfers compact bitboards to the native evaluator, avoids repeated
terminal move generation, generates quiescence captures/promotions directly, and
uses a bounded per-search best-move cache for ordering. The cache stores no scores,
so draw histories are evaluated independently. No full score transposition table
or selective pruning is implemented.

Run `.venv/bin/python scripts/benchmark.py --depth 4` with the real model to compare
fixed-depth wall time, node count, score and best move on the same host. NPS alone
can hide gains from improved ordering. Tests cover reference board transfer,
special moves, draw/mate priority, quiet promotions and interruption cleanup.

Startup reports `evaluator NNUE ...` on successful loading; failure explicitly
reports the fallback. Each search reports `search budget_ms ... max_depth ...`.
Clock budgets are milliseconds: `(remaining - Move Overhead) / movestogo + 0.8 *
increment`, with a default of 30 moves, capped to remaining time minus overhead.
`go movetime` also subtracts engine overhead. `go infinite` and `go ponder` disable
the initial deadline. There is no implicit depth-3 cap.

When a deadline interrupts an iteration, the final info line retains the last
completed depth/score/PV but includes all nodes and elapsed time. Previously the
reported work omitted the entire interrupted iteration.

For lichess-bot, inspect `engine.uci_options.go_commands`: depth/nodes/movetime
there can impose limits. `engine.uci_options.Move Overhead` and the bot's top-level
`move_overhead` are separate reserves; preserve sufficient network margin. The
latter defaults to 2000 ms in current lichess-bot. Do not lower reserves merely to
inflate depth or NPS. Keep `ponder: false` with this single-worker engine.
