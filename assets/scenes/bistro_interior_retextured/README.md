# Bistro interior

UVSR packages a converted, user supplied Blender GLB of the Wine variant
associated with Amazon Lumberyard Bistro. the exact source and conversion facts
are in [`source-provenance.json`](source-provenance.json). the source notes,
license, provenance JSON, and generated reports are preserved records.

## source identity and terms

| item | bytes | SHA-256 |
| --- | ---: | --- |
| supporting `Bistro_v5_2.zip` archive | 894,377,473 | `0D50E3C724C6C5DA19F8EB99AD3F53E36FEC37FFA2DF9621F9CCF0603F3934E1` |
| supplied `BistroInterior_Wine.glb` | 421,517,664 | `47C71CF9FC3F0BBDF213F5788993BA5732565E7EACAE616DF26BC60818FDBC6A` |
| [`SOURCE-README.txt`](SOURCE-README.txt) | 1,686 | `C87C5B60992CEDEE49FCE1EA9BFE10CF60498EBF1113685B723D6DC9006C2BEF` |
| [CC BY 4.0 license](LICENSE.txt) | 19,044 | `9A9EF3C33320EEBE6126B0C7DC327885806BDE242283BBE6E4AE77641AD703E4` |

the supporting archive and citation come from the
[Computer Graphics Archive](https://casual-effects.com/data/) and
[NVIDIA ORCA](https://developer.nvidia.com/orca/amazon-lumberyard-bistro).
Amazon Lumberyard created the original Bistro asset, and the supporting package
states Creative Commons Attribution 4.0.

the supplied GLB is a separate Blender export. it is not a member of the hashed
`Bistro_v5_2.zip`. the supporting package therefore does not by itself prove
that the CC BY 4.0 grant covers this exact GLB. preserve the attribution and
modification disclosures, and confirm the GLB's chain of title before commercial
distribution. the [legal record](../../../legal/documentation/amazon-lumberyard-bistro.md)
owns that clearance boundary.

## conversion

the GLB was repacked as standard glTF with five external buffers. buffer views
were copied without decoding or reencoding; only alignment padding was added.
the generated
[`components/buffer-repack-report.json`](components/buffer-repack-report.json)
records all six output files, 423,001,606 output bytes, and every SHA-256. its
own SHA-256 is
`0E65F90AF33D12DF98DE3AAD1868507768A829391B51FBEAD372B995343E65F0`.

UVSR has no blended draw pass. the conversion changes `Water`, `Ice`, `Beer`,
`Red_Wine`, and `White_Wine` from BLEND to OPAQUE while preserving their other
recorded values. this keeps 227 primitives and 109,600 triangles visible, but
does not preserve liquid transparency. no analytic lights are present.

## initial camera

the descriptor retains the source direction, up vector, and 33.9666 degree
vertical field of view. its position is the embedded camera translated 1 metre
along +X and 0.5 metre along -Z into a nearby enclosed area. the descriptor is
408 bytes with SHA-256
`BDAC7D44996BDB76335F1A504CCAD272BA0717F4ABBCCD50C2E26222497CE7A1`.
