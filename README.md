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
The model is included: no separate weight download is required.

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

The included **trained** `networks/lichess500k.nnue` is copied from the upstream
`lichess-500k-v1` release, with its published SHA256 verified. The original
training/evaluation source is preserved at `nnue/upstream/main.cpp`.
See [provenance](nnue/PROVENANCE.md) for the exact source commit and asset.
The native bridge updates accumulators incrementally during make/unmake, with
king-bucket rebuilds and independent states for each ply.

```sh
shasum -a 256 networks/lichess500k.nnue
make build/kfnnue
./build/kfnnue eval networks/lichess500k.nnue 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'
```

The custom KFQ1 format is little-endian on supported targets and is incompatible
with Stockfish networks. If the native library or initial model cannot be loaded,
the engine reports the failure and uses material scores. Normal setup uses NNUE.

## Tests and limitations

```sh
make test
```

Tests run the upstream NNUE gradient/roundtrip/update self-test, compare incremental
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
`chess==1.11.2` dependency. The NNUE source and model originate in your
[kittenfishnnue repository](https://github.com/crazykitten43/kittenfishnnue).
