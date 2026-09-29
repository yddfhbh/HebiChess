# Stockfish 14 NNUE reference slice

This directory contains the minimum Stockfish 14 source slice used by the isolated HebiChess evaluator experiment. The source is from official-stockfish/Stockfish tag `sf_14`, commit `773dff020968f7a6f590cfd53e8fd89f12e15e36`.

Stockfish source is GNU GPL v3 or later; see `Copying.txt` and the per-file upstream notices. To coexist with the separately vendored Stockfish 19 experiment, the copied source's C++ namespace and include guards are mechanically renamed from `Stockfish` to `Stockfish14` / `SF14_*`. No Stockfish search, move ordering, or transposition-table implementation is part of this slice.

The official default network is `nn-3475407dc199.nnue`. Its artifact is obtained from `https://tests.stockfishchess.org/api/nn/nn-3475407dc199.nnue`; its name encodes the first 12 hex digits of the SHA-256. The upstream networks repository states that networks were uploaded under CC0. Network weights are not vendored in git; local benchmark builds load the separately downloaded artifact.

Architecture from upstream `src/nnue/nnue_architecture.h`: HalfKAv2 features, 512 transformed dimensions, 16 and 32 clipped-ReLU hidden layers, one output, 8 PSQT buckets and 8 layer stacks. Network format version is `0x7AF32F20`.
