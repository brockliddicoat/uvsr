# `JsonCpp`

## record

- relationship: historical dependency integration and comparison evidence
- status: retired from the current build and package mappings
- Confidence: Confirmed
- Upstream: [JsonCpp](https://github.com/open-source-parsers/jsoncpp)
- Revision: `89e2973c754a9c02a49974d839779b151e95afd6` (`1.9.6`)
- Archive SHA-256: `02f0804596c1e18c064d890ac9497fa17d585e822fcacf07ff8a8aa0b344a7bd`
- Governing Terms: Upstream public-domain/MIT choice; copyright Baptiste Lepilleur and the JsonCpp Authors where applicable
- License SHA-256: `cec0db5f6d7ed6b3a72647bd50aed02e13c3377fd44382b96dc2915534c042ad`

## UVSR relationship

UVSR previously fetched the immutable upstream `1.9.6` source archive directly
for Donut's JSON callers. those callers and the static target have been retired.
[captured import controls](../../tests/import_description_fixture.md) preserve
the independent native comparison results. JsonCpp is upstream code.

## evidence

- [current dependency configuration](../../cmake/DirectThirdParty.cmake)
- [historical target and license records](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-core-prune-v1/before-records.json)

## commercial clearance

the complete upstream choice-of-terms file previously installed as
`bin/licenses/JsonCpp-Public-Domain-or-MIT.txt` is preserved in the
[retirement evidence](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-core-prune-v1/build-before/JsonCpp-Public-Domain-or-MIT.txt).
