# UVSR Noise Assets

## Contents

This directory contains the 12 deterministic R8 texture arrays used by UVSR's
Spatial White, Spatial Blue, and Spatiotemporal Blue noise modes at 64x64,
128x128, 256x256, and 512x512.

`manifest.json` is the authoritative dimensions, format, byte-length, seed,
algorithm-version, and SHA-256 record. Array slices are stored consecutively in
row-major order. Spatial files have one slice; spatiotemporal files have 64.

## Regeneration

Run `tools/generate_noise_assets.py` with Python and NumPy to reproduce the
complete set. The generator is a first-party clean-room spectral construction
based on the public spatial and temporal objective in the
[Spatiotemporal Blue Noise paper](https://arxiv.org/abs/2112.09629).

No source code or texture asset from NVIDIA-RTX/STBN is included here.
