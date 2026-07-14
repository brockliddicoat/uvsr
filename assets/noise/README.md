# Visibility Filter-Adapted Rank Field

`visibility_filter_adapted_gauss1_ema035_r8.bin` is a 64x64x32
`R8_UNORM` scalar-uniform volume in array-slice order. Every temporal slice
contains every 8-bit value exactly 16 times, so the offline optimization
changes correlation without changing the per-frame distribution.

The field was generated with Electronic Arts' BSD-3-Clause FastNoise utility
at commit `2cf53e4bb510d07511fe63a312556d2a2e108c70`:

```text
FastNoise.exe real uniform gauss 1.0 exponential 0.35 0.35 separate 0.5 64 64 32 generated/uvsr_visibility_fast -split -numsteps 10000 -seed 2779096485
```

The 32 output PNG red channels were packed in increasing slice order. The
packed file's SHA-256 is
`EA92F5CC16260356D13E4E0C1165823A62DCB1C32752B3019D93223DC681C188`.

The fixed optimization target is Gaussian sigma 1.0 over space and an
alpha=0.35 exponential moving average over time, combined in FAST's separate
mode with equal spatial and temporal weights. UVSR uses R2-separated spatial
reads when several semantic random values are needed in one frame and a
low-discrepancy global offset after each 32-frame volume cycle.

Sources:

- https://github.com/electronicarts/fastnoise
- https://github.com/electronicarts/importance-sampled-FAST-noise
- https://jcgt.org/published/0014/01/08/

See `FAST-LICENSE.txt` for the generator's BSD-3-Clause license.
