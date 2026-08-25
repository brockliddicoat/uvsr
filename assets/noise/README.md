# UVSR Noise Assets

## Contents

This directory contains the 12 deterministic R8 texture arrays used by UVSR's
Spatial White, Spatial Blue, and Spatiotemporal Blue noise modes at 64x64,
128x128, 256x256, and 512x512.

`manifest.json` is the authoritative dimensions, format, byte-length, seed,
algorithm-version, and SHA-256 record. Array slices are stored consecutively in
row-major order. Spatial files have one slice; spatiotemporal files have 64.

The one-time generator was retired after these bytes were frozen. Preserve the
manifest hashes; a future replacement requires separately reviewed provenance
and rendered-quality evidence. No NVIDIA-RTX/STBN source or texture asset is
included here.
