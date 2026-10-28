# KittenFish v1

The main engine uses C++17 for search, legal moves and incremental NNUE evaluation.
This promotes `experimental/cpp-search`, the engine currently used by the bot,
without changing its C++ search implementation. Linux and macOS are supported.
The original Python engine is preserved on the
[`legacy/python-search` branch](https://github.com/crazykitten43/kittenfishv1/tree/legacy/python-search).
Python remains a setup and test dependency; its source is retained for NNUE
comparison tests. Both `./kittenfish` and `./kittenfish-cpp` launch the C++ engine.
See [native engine details](native/README.md) for search and UCI behavior.

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
`go infinite`, `stop`, `setoption`, `quit`. `go mate N` limits the
search to 2N plies; it is not a dedicated proof search. Search runs on a worker
so the engine can respond to stop and readiness commands while thinking.
Info lines report depth, centipawns/mate, nodes, elapsed time, NPS and PV.
No legal moves returns `bestmove 0000`.

UCI options:

- `Move Overhead`: reserve milliseconds for communication (default 100).
- `EvalFile`: path to a custom KFQ1 network; default is the included model.
  NNUE load failure exits explicitly.

- `Hash`: transposition table size in MiB (1–1024, default 32).

Pondering and Chess960 are unsupported; keep pondering disabled. Only one search
worker is implemented. Threads and Syzygy options are unsupported.

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
unsupported UCI options such as Threads, SyzygyPath and UCI_ShowWDL.
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
with Stockfish networks. The C++ engine exits explicitly if the NNUE model cannot be loaded.

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
threefold/fifty-move draws as draws during search. The C++ search includes a bounded transposition table, principal variation search,
killers and quiet-move history. Quiescence includes check evasions, captures and
promotions, with a 96-ply safety bound. There is no opening book, tablebase support
or measured rating. See [native tests and limitations](native/README.md).

## Dependency notices

The C++ engine uses the MIT-licensed vendored Disservin/chess-library for legal
move generation; see [its license](native/vendor/LICENSE).
UCI integration testing and reference comparisons use
[python-chess](https://github.com/niklasf/python-chess), copyright Niklas Fiekas
and contributors, licensed GPL-3.0-or-later. Its source is installed via the pinned
`chess==1.11.2` dependency. The complete NNUE trainer is maintained in
[kittenfishtraining/v1](https://github.com/crazykitten43/kittenfishtraining/tree/main/v1).
The NNUE source and model originate in your
[kittenfishnnue repository](https://github.com/crazykitten43/kittenfishnnue).
