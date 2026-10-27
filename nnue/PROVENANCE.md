# NNUE provenance

upstream/main.cpp contains the inference definitions extracted from crazykitten43/kittenfishnnue,
commit 216fc92c5cc6ed20583650a1dd77c2da321dbdb4, src/main.cpp.
https://github.com/crazykitten43/kittenfishnnue/tree/216fc92c5cc6ed20583650a1dd77c2da321dbdb4

networks/lichess500k.nnue is copied byte for byte from release lichess-500k-v1:
https://github.com/crazykitten43/kittenfishnnue/releases/tag/lichess-500k-v1
Asset ID: 681803404; size: 21,238,856 bytes.
SHA256: 4e44ea059ecad6ed495a19c167d7b426036b9652dcfe73e2372791ddbea39be9

The complete original trainer and tests are moved to
https://github.com/crazykitten43/kittenfishtraining/tree/main/v1

The model uses the custom KFQ1 format (16 king buckets, 864 hidden units).
It is not compatible with Stockfish's NNUE file format. Native-endian;
use on little-endian hosts. No retraining or strength claim is made here.
