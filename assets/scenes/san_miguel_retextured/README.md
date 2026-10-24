# San Miguel

UVSR uses a modified main-building scene from San Miguel 2.1 in the
[Computer Graphics Archive](https://casual-effects.com/data/). Guillermo M.
Leal Llaguno created the model. Morgan McGuire, Guedis Cardenas, Michael Mara,
and Nicholas Hull improved the 2017 version with the creator's permission. the
exact source and conversion facts are in
[`source-provenance.json`](source-provenance.json).

## current geometry

[`geometry-revision.json`](geometry-revision.json) records the edits from
`08cbbaaf2893d952726fbd52c1ecddb970711ee6` and audits every current component.
the separate courtyard enclosure, its paving, the street apron, and the
photographic street wall were removed. the main courtyard building remains.

the four elevations use different arrangements of the original interior
arcade pillars, stone arches, and gallery railing:

| elevation, in the original Z-up coordinates | facade |
| --- | --- |
| south | a central three-bay gallery on two floors, with solid side wings |
| north | a longer five-bay gallery on two floors and an offset solid wing |
| west | a single-bay portico around the two existing doorways |
| east | a three-bay entrance porch with an open terrace above |

the outer walls use the existing weathered plaster and stone textures. the
roof and decks use the interior terracotta paving. the curved parapet stays,
with its exposed faces and ends repaired. foliage that crossed the outer
wall was clipped at that boundary. the entrance and original camera remain
open and unchanged in position.

the revision has 10,248,871 rendered triangles, 2,190 unchanged original
meshes, all 287 original materials, and all 269 original images. the facade
instances existing arcade geometry. no texture was reencoded. the five-buffer
layout and 90 MB buffer limit remain intact.

the original provenance, import report, repack report, and supplied license
remain immutable records of the source conversion. the geometry revision
owns the current output hashes. its SHA-256 is
`074D0F1C99680A8F45032D64DE7FDF282AA766B1BBD07D6F2AF0706F44A635A8`.

## source identity and terms

| item | bytes | SHA-256 |
| --- | ---: | --- |
| `San_Miguel.zip` | 535,519,642 | `85874077735808150E679B3C71D70A37A270CB8833F4911325AA1099DA3F7D4A` |
| `san-miguel.obj` | 1,143,041,382 | `22533258BE1D94AA1ECE29053E98F11C91EBC17A8A3626545DEE3CECF90B71E3` |
| `san-miguel.mtl` | 35,143 | `5C0618AE58CEB61B51B09B97C5A16BBB410CCFFC671E99C9076A3926CEE04916` |
| [supplied notice](LICENSE.txt) | 1,298 | `708C9AD36ADAC62D13BD61DDF47D58D2B892B9E318BD87DA93AE9E55E2B5E680` |

the supplied notice permits research and educational use with attribution. it
does not record a general commercial grant. San Miguel is therefore a
commercial distribution blocker unless the relevant rights holders grant
separate permission or the scene is replaced. the
[legal record](../../../legal/documentation/san-miguel-2-1.md) owns that
clearance boundary.

## original conversion

Blender 5.1.2 build `ec6e62d40fa9` imported the OBJ without decimation, Draco,
or texture reencoding. the generated
[`blender-import-report.json`](blender-import-report.json) records 9,963,191
renderable triangles, all 269 used source PNGs, 9,186 rejected faces with
repeated position indices, and 8,322 additional invalid polygons removed by
mesh validation. it also records all 287 materials, 264 source `map_Kd`
bindings, 95 alpha masks, and 56 explicit `N_*` normal maps. the report's
SHA-256 is
`3675AB45846945592C59319489A6568402C6020079DF30AD486BDED597A94131`.

UVSR has no blended or transmissive draw pass. the conversion keeps
`material_041` visible as opaque and flattens transmission for `material_79`,
`materialn`, and `materialo`. the latter fallbacks retain 12 primitives and
63,910 triangles, but do not preserve transmission. the ambiguous height or
normal map is omitted instead of being mislabeled as tangent space data.

the original glTF geometry was repacked by buffer view into five external buffers. the
generated
[`components/buffer-repack-report.json`](components/buffer-repack-report.json)
records all 275 component files, 493,729,206 output bytes, and every SHA-256.
its own SHA-256 is
`8C0CC946F8C28BA36D62628876BE3005AEA415C2A8D6437FD6F4202ACF1DBA41`.

## initial camera

the descriptor adapts the PBRT v4 San Miguel entry camera from Z up to glTF Y
up. the exact source is commit
`30cf4a0346ae5a80a2d7a530a3ef7d0fa4f70572`, blob
`3e442fb1f407316e6fb74cb066eddd5bf158ff9a`, at the
[pinned scene file](https://github.com/mmp/pbrt-v4-scenes/blob/30cf4a0346ae5a80a2d7a530a3ef7d0fa4f70572/sanmiguel/sanmiguel-entry.pbrt).
the terms governing this camera data are not established, so confirm them or
replace the camera before commercial reuse. the descriptor is 415 bytes with
SHA-256
`F8B3E4896A5489E2120B5AED06D35D52912C262E9D540FF953E5B950E8543A7E`.
