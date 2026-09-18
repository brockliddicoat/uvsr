# PBRT v4 San Miguel camera

## record

- relationship: adapted implementation
- status: current
- confidence: confirmed lineage, with governing terms requiring confirmation
- upstream: [PBRT v4 Scenes San Miguel entry view](https://github.com/mmp/pbrt-v4-scenes/blob/master/sanmiguel/sanmiguel-entry.pbrt)
- revision: repository commit `30cf4a0346ae5a80a2d7a530a3ef7d0fa4f70572`, source blob `3e442fb1f407316e6fb74cb066eddd5bf158ff9a`
- governing terms: not established by the provenance manifest for this scene-specific camera data

## relationship

the retained scene descriptor adapts the entry camera's eye, target, up vector,
and field of view from PBRT's Z-up coordinates to glTF Y-up coordinates. it does
not incorporate the PBRT renderer or the upstream scene implementation through
this relationship. the reused material is the small camera-data selection only.

## evidence

- [recorded camera provenance and mapping](../assets/scenes/san_miguel_retextured/source-provenance.json)
- [San Miguel scene descriptor](../assets/scenes/san_miguel_retextured/san_miguel_retextured.scene.json)

## commercial clearance

confirm the terms governing the cited `sanmiguel-entry.pbrt` revision before
commercial reuse of the adapted camera data. if necessary, replace it with an
independently authored camera and update the provenance manifest.
