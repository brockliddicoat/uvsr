# Stochastic Sobol Generation

## Record

- Relationship: Generated Derivative and Design Influence
- Status: Current
- Confidence: Confirmed
- Upstream: [Stochastic Generation Author Code](https://github.com/Andrew-Helmer/stochastic-generation/tree/f90b115806675035c8c727bab4575ca5ba1760b6)
- Revision: `f90b115806675035c8c727bab4575ca5ba1760b6`
- Governing Terms: MIT License, copyright 2021 Andrew Helmer

## UVSR Relationship

UVSR stores a fixed 32-point table generated with the upstream stochastic Sobol
tool using seed 43 and the documented `--bn2d` procedure. The generator source
is not bundled. The related paper by Andrew Helmer, Per Christensen, and Andrew
Kensler is the algorithmic foundation.

## Evidence

- [Generation Procedure and Table Context](google-filament-fxaa.md#sobol-32-generation)
- [Temporal Reference Table](../../src/temporal_aa_reference.h)
- [Temporal Options](../../docs/temporal-aa-options.md)
- Commit `a9a3dd10d7c8cf21e23c6642f1f93f4a7142192f`

## Commercial Clearance

Preserve Andrew Helmer's MIT notice with the table provenance. Generated output
does not justify relicensing the generator itself.
