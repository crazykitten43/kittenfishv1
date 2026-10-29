# NNUE provenance

## Inference code

`upstream/main.cpp` contains the inference definitions extracted from
crazykitten43/kittenfishnnue, commit
216fc92c5cc6ed20583650a1dd77c2da321dbdb4, `src/main.cpp`:
https://github.com/crazykitten43/kittenfishnnue/tree/216fc92c5cc6ed20583650a1dd77c2da321dbdb4

## Default model

`networks/kf100m.nnue` replaces the previous `lichess500k.nnue` model. It is
published as the `kf100m.nnue` asset in this repository's `nnue-100m-v1` release:
https://github.com/crazykitten43/kittenfishv1/releases/tag/nnue-100m-v1

- Exported from `models/full100m/kf100m.best.kff` after training on 2026-10-29.
- Trainer: https://github.com/crazykitten43/kittenfishtraining/tree/a46960ce96413e246bd6e425562c57556936b389/v1
- 100,000,000 unique eligible network inputs from the Lichess evaluation archive;
  99,000,067 training positions and 999,933 held-out validation positions.
- White-perspective labels; minimum depth 20; centipawns clipped to +/-2000.
  Deterministic position split, not a game-grouped split or a uniform archive sample.
- CUDA PyTorch 2.9.1, NVIDIA L4; Adam, batch 4096, learning rate 0.0001,
  seed 42, 10 epochs, 247,510 optimizer steps.
- Best model: epoch 10. Float validation MSE 63,930.4994 cp²; MAE 145.1062 cp.
  These metrics precede quantization and do not establish playing strength.
- Exported using the trainer's C++ `export` command into KFQ1.
- Size: 21,238,856 bytes.
- SHA256: 95f19b6465c82de79a0924f5b6a2f3343233ea3ded73712f533bd46bf335f819

The model uses the custom KFQ1 format (16 king buckets, 864 hidden units).
It is not compatible with Stockfish's NNUE file format. Native-endian;
use on little-endian hosts. The engine's inference architecture is unchanged.
