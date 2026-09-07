# screen space indirect lighting with visibility bitmask

## record

- relationship: independent implementation and publication influence
- status: current
- confidence: confirmed
- upstream: [paper by Olivier Therrien, Yannick Levesque, and Guillaume Gilet](https://arxiv.org/abs/2301.11376)
- revision: 2023 publication
- terms: publication rights; no upstream implementation code identified

## UVSR relationship

the projected angle estimator follows the paper's finite thickness visibility
bitmask concept. solid angle and cosine weighted estimation, traversal,
validation, and renderer integration are first party work. UVSR does not claim
that publication source code was copied.

## evidence

- [user guide](../../docs/user-guide.md)
- [validation contract](../../docs/validation.md)
- [estimator implementation](../../src/visibility_estimator_shared.h)
- [screen space trace](../../src/screen_space_visibility_cs.hlsl)

cite the paper for the algorithmic foundation. reuse of publication figures,
text, or later upstream code needs separate review.
