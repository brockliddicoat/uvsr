# Intel Conservative Morphological Anti-Aliasing 2

## Record

- Relationship: Incorporated Upstream Material and Adapted Implementation
- Status: Historical
- Confidence: Confirmed
- Upstream: [Intel CMAA2](https://github.com/GameTechDev/CMAA2)
- Revision: Upstream shader lineage recorded in the vendored file; no repository pin is presently documented
- Former Governing Terms: [Apache License 2.0](../licenses/Apache-2.0.txt)

## UVSR Relationship

UVSR previously vendored Intel's substantial shader and supplied a first-party
NVRHI host adapter and wrapper shader. The implementation, selectable settings,
runtime binaries, and Intel-specific license copy are no longer shipped. The
former source and its nested Microsoft notices remain recoverable from Git
history.

## Evidence

- Introduction commit `58813cb94054738fc25ff2493444fbdb3dce7d98`
- Settings expansion commit `a9a3dd10d7c8cf21e23c6642f1f93f4a7142192f`
- Last preparation base carrying the implementation:
  `54a57b08a462ad83979ccc8912570f2c6cc7ea03`
- Canonical removal revision: none yet. The removal is composed locally into
  the integration candidate based on
  `3bc13fd3c170d746366f01404a7c4b726efdcab9`; publication remains pending.

## Commercial Clearance

No current binary or source distribution contains this implementation. Git
history preserves its original notices. Any future restoration must recover
and re-audit the exact former source, governing license, modifications, and
nested Microsoft notice before distribution.
