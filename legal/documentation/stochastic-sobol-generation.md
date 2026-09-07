# stochastic sobol generation

## record

- relationship: generated output and publication influence
- status: retired TAA data, provenance retained
- confidence: confirmed
- upstream: [stochastic generation author code](https://github.com/Andrew-Helmer/stochastic-generation/tree/f90b115806675035c8c727bab4575ca5ba1760b6)
- revision: `f90b115806675035c8c727bab4575ca5ba1760b6`
- terms: MIT License, copyright 2021 Andrew Helmer
- upstream raw license SHA-256: `f026c1653b20a1edcf0d4091b0b22f148d065c12332e8a3b0b249c32ec274c4f`
- packaged license: 1,053 bytes, LF normalized SHA-256
  `50a4be869e51722a4ca90819535a78df8f82d68facbad52a2da6ab4dc284ad55`

## UVSR relationship

UVSR previously retained a fixed 32 point table generated from the upstream tool with seed
43 and its `--bn2d` procedure. the generator source is not retained, built, or
packaged. the related paper by Andrew Helmer, Per Christensen, and Andrew
Kensler is the algorithmic foundation.

## evidence

- [user guide](../../docs/user-guide.md)
- [validation contract](../../docs/validation.md)
- former table: `src/temporal_aa_reference.h`, recoverable at the incorporation commit below
- incorporation commit `a9a3dd10d7c8cf21e23c6642f1f93f4a7142192f`

preserve Andrew Helmer's MIT notice with the table provenance. generated output
does not relicense the generator. the current renderer no longer packages this table or its notice. the complete
MIT text remains in source for historical recovery.

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

the former Filament patterns also included Rotated Grid 4, Uniform Helix 4,
and Halton (2,3) lengths 8, 16, and 32 with the 409-entry skip. these were
centered in pixel units. see the [TAA postmortem](../../docs/postmortem/taa.md).
