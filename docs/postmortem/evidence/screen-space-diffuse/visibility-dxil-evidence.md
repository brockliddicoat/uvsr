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
| Reference Generic AO | 18,936 | 1,561 | 14 | 5 | 31 | 203 | 36 |
| Exact Fixed8 AO | 29,748 | 2,944 | 14 | 5 | 24 | 483 | 129 |
| Exact Fixed12 AO | 38,380 | 4,020 | 18 | 5 | 28 | 677 | 185 |
| Exact Fixed16 AO | 47,000 | 5,096 | 22 | 5 | 32 | 871 | 241 |
| Exact Fixed20 AO | 56,016 | 6,192 | 26 | 5 | 36 | 1,068 | 297 |
| Exact Packed FAST AO | 29,332 | 2,845 | 11 | 5 | 24 | 483 | 129 |
| Diagnostic Constant Trace | 5,956 | 16 | 0 | 1 | 1 | 2 | 0 |
| Diagnostic Depth Trace | 22,524 | 2,201 | 21 | 5 | 29 | 355 | 77 |
| Diagnostic Bitmask Trace | 8,452 | 301 | 0 | 1 | 2 | 66 | 25 |
| Activision Schedule Bitmask | 31,184 | 3,088 | 10 | 5 | 104 | 483 | 130 |
| Eight-Sample Horizon Control | 19,548 | 1,706 | 13 | 5 | 29 | 241 | 49 |
| PS4 Schedule Horizon Control | 29,440 | 2,946 | 19 | 5 | 147 | 340 | 73 |
| Reference Compact Resolve | 21,032 | 1,931 | 42 | 3 | 24 | 266 | 156 |
| Exact Fast Compact Resolve | 20,248 | 1,808 | 42 | 3 | 24 | 238 | 103 |
| Reference Gaussian Resolve | 11,660 | 567 | 6 | 3 | 24 | 63 | 28 |
| Packed Edge 4x4 Resolve (Removed) | 33,608 | 3,162 | 209 | 1 | 2 | 325 | 12 |
| Exact Fused Resolve And Apply | 14,632 | 1,015 | 20 | 1 | 22 | 145 | 79 |
| Reference Composition | 8,860 | 188 | 8 | 1 | 7 | 13 | 7 |

No tested shader emitted a DXIL `textureGather` call. The packed-edge 4x4 path
therefore remained a load-based experiment with 209 static texture loads and
was removed from the source and shader package. The row is retained as
historical generated-code evidence for that decision.

## Confirmed Compiler Effects

- Packed FAST reduces static texture-load call sites from 14 to 11 versus the
  otherwise comparable fixed-eight AO shader, a 21.4% reduction. Static IR also
  falls from 2,944 to 2,845 instructions.
- Exact compact filter algebra survives optimization: static IR falls 6.4%,
  branches fall 10.5%, and DXIL unary-operation call sites fall 34.0% versus the
  reference compact resolve. Texture-load call sites remain unchanged.
- Exact fusion has 1,015 static IR instructions, 20 texture-load call sites, and
  one store. The separate compact-resolve-plus-composition pair totals 2,119
  static IR instructions, 50 texture-load call sites, and four stores. This is
  compiler evidence for the intended pass and traffic removal, not a target-GPU
  timing claim.
- Fixed-count unrolling grows DXIL from 29,748 bytes at eight samples to 56,016
  bytes at twenty samples. That makes instruction-cache and register-pressure
  measurement mandatory; source-level loop removal alone is not evidence of a
  speedup.
- The constant, depth-read, and bitmask-only shaders remain materially distinct
  after optimization. Their static IR counts are 16, 2,201, and 301,
  respectively, so the diagnostic floors were not folded into the same shader.

## Physical Register and Occupancy Limitation

DXIL uses SSA virtual values and does not expose Intel physical GRF allocation,
spill/fill counts, SIMD width, EU occupancy, cache hit rate, or bandwidth. No
Intel GPA, IGA, or `ocloc` executable was available in the build environment.
Those measurements must be collected from the final committed build on the
  target Intel adapter. Controlled target timing is now available, but it does
  not establish any register, occupancy, or native-instruction win.
