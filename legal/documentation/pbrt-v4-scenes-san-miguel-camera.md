# `PBRT` v4 Scenes San Miguel Camera

## Record

- Relationship: Adapted Implementation
- Status: Current
- Confidence: Confirmed lineage; governing terms require confirmation
- Upstream: [PBRT v4 Scenes San Miguel Entry View](https://github.com/mmp/pbrt-v4-scenes/blob/master/sanmiguel/sanmiguel-entry.pbrt)
- Revision: Repository commit `30cf4a0346ae5a80a2d7a530a3ef7d0fa4f70572`, source blob `3e442fb1f407316e6fb74cb066eddd5bf158ff9a`
- Governing Terms: Not established by UVSR's provenance manifest for this scene-specific camera data

## UVSR Relationship

UVSR adapts the entry camera's eye, target, up vector, and field of view from
PBRT's Z-up coordinates to glTF Y-up coordinates. It does not incorporate the
PBRT renderer or the upstream scene implementation through this relationship;
the reused material is the small camera-data selection only.

## Evidence

- [Recorded Camera Provenance And Mapping](../../assets/scenes/san_miguel_retextured/source-provenance.json)
- [UVSR San Miguel Scene Descriptor](../../assets/scenes/san_miguel_retextured/san_miguel_retextured.scene.json)

## Commercial Clearance

Confirm the terms governing the cited `sanmiguel-entry.pbrt` revision before
commercial reuse of the adapted camera data. If necessary, replace it with an
independently authored camera and update the provenance manifest.
