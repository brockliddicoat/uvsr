# `JsonCpp`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [JsonCpp](https://github.com/open-source-parsers/jsoncpp)
- Revision: `89e2973c754a9c02a49974d839779b151e95afd6` (`1.9.6`)
- Archive SHA-256: `02f0804596c1e18c064d890ac9497fa17d585e822fcacf07ff8a8aa0b344a7bd`
- Governing Terms: Upstream public-domain/MIT choice; copyright Baptiste Lepilleur and the JsonCpp Authors where applicable
- License SHA-256: `cec0db5f6d7ed6b3a72647bd50aed02e13c3377fd44382b96dc2915534c042ad`

## UVSR Relationship

UVSR fetches the immutable upstream `1.9.6` source archive directly and owns
the narrow static target. Donut's remaining JSON callers consume that target;
JsonCpp is not first-party code.

## Evidence

- [Direct Pin and Target](../../cmake/DirectThirdParty.cmake)
- [Transitional Donut JSON Wrapper](../../donut/include/donut/core/json.h)

## Commercial Clearance

The complete upstream choice-of-terms file is installed as
`bin/licenses/JsonCpp-Public-Domain-or-MIT.txt`.
