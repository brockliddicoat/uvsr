# Visibility DXIL Evidence

This snapshot compares optimized DXIL generated from the AO performance
permutations. Run `tools/measure_visibility_dxil.ps1` after a Release configure
to reproduce the CSV, disassemblies, and Markdown report under
`build/shader_evidence/current`.

The counts below are static LLVM/DXIL IR call sites in the optimized `main`
function. They are useful for confirming that source changes survive DXC and
for comparing texture-operation call sites in fully unrolled variants. They
are not dynamic instruction counts. In particular, fixed-count shaders expose
unrolled work as more static IR while the generic trace and Gaussian resolve
retain loops whose runtime iterations are not represented by duplicating their
bodies in this table.

## Generated DXIL Comparison

| Variant | DXIL Bytes | Static IR Instructions | Texture Loads | Texture Stores | Constant-Buffer Loads | Branches | DXIL Unary Operations |
|---|---:|---:|---:|---:|---:|---:|---:|
| Reference Generic AO | 18,292 | 1,539 | 14 | 5 | 29 | 198 | 36 |
| Exact Fixed8 AO | 29,104 | 2,904 | 14 | 5 | 22 | 484 | 129 |
| Exact Fixed12 AO | 37,700 | 3,964 | 18 | 5 | 26 | 680 | 185 |
| Exact Fixed16 AO | 46,296 | 5,024 | 22 | 5 | 30 | 876 | 241 |
| Exact Fixed20 AO | 55,260 | 6,103 | 26 | 5 | 34 | 1,075 | 297 |
| Exact Packed FAST AO | 28,672 | 2,805 | 11 | 5 | 22 | 484 | 129 |
| Diagnostic Constant Trace | 5,484 | 16 | 0 | 1 | 1 | 2 | 0 |
| Diagnostic Depth Trace | 21,772 | 2,166 | 21 | 5 | 22 | 351 | 77 |
| Diagnostic Bitmask Trace | 7,976 | 301 | 0 | 1 | 2 | 66 | 25 |
| Activision Schedule Bitmask | 30,532 | 3,048 | 10 | 5 | 102 | 484 | 130 |
| Eight-Sample Horizon Control | 18,820 | 1,673 | 13 | 5 | 22 | 237 | 51 |
| PS4 Schedule Horizon Control | 24,164 | 2,363 | 14 | 5 | 118 | 317 | 63 |
| Reference Compact Resolve | 20,540 | 1,931 | 42 | 3 | 24 | 266 | 156 |
| Exact Fast Compact Resolve | 19,756 | 1,808 | 42 | 3 | 24 | 238 | 103 |
| Reference Gaussian Resolve | 11,168 | 567 | 6 | 3 | 24 | 63 | 28 |
| Packed Edge 4x4 Resolve | 33,112 | 3,162 | 209 | 1 | 2 | 325 | 12 |
| Exact Fused Resolve And Apply | 13,264 | 863 | 20 | 1 | 18 | 101 | 63 |
| Reference Composition | 8,368 | 188 | 8 | 1 | 7 | 13 | 7 |

No tested shader emitted a DXIL `textureGather` call. The packed-edge 4x4 path
therefore remains a load-based experiment; a GatherRed permutation is an
explicitly deferred experiment rather than an implemented claim.

## Confirmed Compiler Effects

- Packed FAST reduces static texture-load call sites from 14 to 11 versus the
  otherwise comparable fixed-eight AO shader, a 21.4% reduction. Static IR also
  falls from 2,904 to 2,805 instructions.
- Exact compact filter algebra survives optimization: static IR falls 6.4%,
  branches fall 10.5%, and DXIL unary-operation call sites fall 34.0% versus the
  reference compact resolve. Texture-load call sites remain unchanged.
- Exact fusion has 863 static IR instructions, 20 texture-load call sites, and
  one store. The separate compact-resolve-plus-composition pair totals 2,119
  static IR instructions, 50 texture-load call sites, and four stores. This is
  compiler evidence for the intended pass and traffic removal, not a target-GPU
  timing claim.
- Fixed-count unrolling grows DXIL from 29,104 bytes at eight samples to 55,260
  bytes at twenty samples. That makes instruction-cache and register-pressure
  measurement mandatory; source-level loop removal alone is not evidence of a
  speedup.
- The constant, depth-read, and bitmask-only shaders remain materially distinct
  after optimization. Their static IR counts are 16, 2,166, and 301,
  respectively, so the diagnostic floors were not folded into the same shader.

## Physical Register and Occupancy Limitation

DXIL uses SSA virtual values and does not expose Intel physical GRF allocation,
spill/fill counts, SIMD width, EU occupancy, cache hit rate, or bandwidth. No
Intel GPA, IGA, or `ocloc` executable was available in the build environment.
Those measurements must be collected from the final committed build on the
target Core Ultra 9 185H. Until then, the fixed-count variants are optional
benchmark candidates and no register, occupancy, or native-instruction win is
claimed.
