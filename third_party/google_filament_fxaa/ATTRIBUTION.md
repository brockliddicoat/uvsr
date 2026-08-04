# Google Filament Anti-Aliasing Attribution

UVSR's Fast Approximate anti-aliasing shader is an independently expressed,
modified HLSL adaptation of the PC-console FXAA path distributed by Google
Filament at commit `47c86eec22e56d75897e16651eb4d2abd64fc29a`:

`filament/src/materials/antiAliasing/fxaa/fxaa.fs`

UVSR's five Filament-compatible Temporal Reconstructive camera-jitter choices
adapt the public pattern definitions and table construction from the same
revision:

`filament/include/filament/Options.h`

`filament/src/PostProcessManager.cpp`

`filament/src/PostProcessManager.h`

## UVSR Modifications

- Translated the relevant PC-console sampling and filter math from GLSL to
  first-party HLSL.
- Reconstructed perceptual luminance for each sample from UVSR's undithered,
  tone-mapped display-linear RGB while filtering the original display-linear
  RGB values.
- Kept display transfer and dithering in UVSR's downstream presentation pass.
- Exposed Filament's edge sharpness, relative edge threshold, and minimum edge
  threshold as bounded runtime settings.
- Reproduced Rotated Grid 4, Uniform Helix 4, and the 8-, 16-, and 32-sample
  Halton (2,3) choices, including Filament's 409-entry Halton skip.
- Centered the jitter in pixel units for UVSR's DirectX 12 planar-view contract
  and reset temporal history when the selected pattern changes.

## Sobol 32 Generation

UVSR's additional fixed Sobol 32 table was generated from Helmer,
Christensen, and Kensler's stochastic Sobol (0,2) author code at commit
`f90b115806675035c8c727bab4575ca5ba1760b6`. For exact reproduction, replace
the generator's RNG declaration with `RNG rng(43);`, run
`./generate_samples --seq=ssobol --n=32 --nd=2 --bn2d`, subtract 0.5 from each
coordinate, and store the results as floats. The seed produces the initial
point directly. For each subsequent point, the `--bn2d` path tests 100
candidates in the required Sobol stratum and selects the candidate with the
greatest minimum toroidal distance to the points already chosen. UVSR stores
only the generated coordinate table; it does not bundle the generator code.

Google Filament is licensed under the Apache License, Version 2.0. The complete
license is distributed as `licenses/Apache-2.0.txt` in UVSR binary packages and
as `third_party/licenses/Apache-2.0.txt` in the source tree.

## G3D Notice

G3D Innovation Engine, <http://casual-effects.com/g3d>

Copyright 2000-2018, Morgan McGuire. All rights reserved.

Available under the BSD License. The complete BSD 2-Clause text is distributed
as `licenses/BSD-2-Clause.txt` in UVSR binary packages and as
`third_party/licenses/BSD-2-Clause.txt` in the source tree.

## NVIDIA FXAA Notice

NVIDIA FXAA 3.11 by Timothy Lottes, modified for G3D with bug fixes and a PC
GLSL preamble.

COPYRIGHT (C) 2010, 2011 NVIDIA CORPORATION. ALL RIGHTS RESERVED.

TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED
*AS IS* AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS
OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL NVIDIA
OR ITS SUPPLIERS BE LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR
LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION,
OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE
THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGES.
