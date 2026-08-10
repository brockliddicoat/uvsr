# Electronic Arts `FAST` Noise

## Record

- Relationship: Generated Derivative and Incorporated Upstream Material
- Status: Historical; generated volume and notice removed
- Confidence: Confirmed
- Upstream: [FastNoise](https://github.com/electronicarts/fastnoise), [Importance-Sampled FAST Noise](https://github.com/electronicarts/importance-sampled-FAST-noise), and [JCGT Paper](https://jcgt.org/published/0014/01/08/)
- Revision: FastNoise commit `2cf53e4bb510d07511fe63a312556d2a2e108c70`
- Governing Terms: BSD 3-Clause; Copyright 2023 Electronic Arts Inc.

## UVSR Relationship

UVSR historically used the FastNoise executable offline to optimize 32 noise
slices, then packed their red channels into a checked-in `R8_UNORM` volume.
UVSR did not ship the generator's source or executable, but the generated data
had a documented tool and algorithm lineage and was accompanied by EA's notice.

## Evidence

- Generated asset and notice added at `537e4c42396a62be59a0e782e8afb59053613b70`; inspect with `git show 537e4c:assets/noise/README.md`
- Generated asset removed at `b63cda9639dedf820f1251aa390b162befa22dd7`

## Commercial Clearance

No current distribution obligation arises from the removed volume. If it is
restored, preserve the BSD notice and generation recipe, confirm whether the
generator's license notice should accompany the derived binary, and avoid
using EA's name for endorsement.
