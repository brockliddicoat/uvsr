# material reference data

`import_material_fixture.bin` preserves 14 original fixtures, containing 38
materials, 41 geometry references, 22 textures and eight swizzles. each material
has 53 scalar lanes and the original 208 constant-buffer bytes. both current and
authored values are compared, retaining all 76 original value/encoding checks.

the [reference receipt](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-material-fixture-v1/reference-04/receipt.json)
records two identical captures and the compiler/executable inputs. the writer
reads the original Donut graph before candidate conversion. materials are sorted
by native source index; reference texture IDs come only from native pointers.
`CreateTextureData` records the native type for both file and memory textures.
memory textures bypass Donut's filename cache. no candidate values supply an
expectation. the [consumed inputs](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-material-fixture-v1/linked-control.json)
reverify archived library and source bytes.

the 41,452-byte file has SHA-256
`821ce97bc7bb3a53ed5faf306569c68923750b743fc58d974a11f9271498956d`.
its little-endian `UVIM0001` header specifies 14 cases and 208 constant bytes.
each case contains the exact original JSON/GLB, geometry bytes, model path and
named files, followed by material values, geometry associations, texture sharing,
paths, decoded pixels and swizzle sources/channels. `UVIMEND1` and EOF end it.
the reader uses fixed storage; no generator target or fixture is shipped.

the original exclusions remain explicit. picking IDs use the candidate's local
selection namespace, external paths stop at the first null, and embedded swizzle
data permits one trailing cgltf padding byte. a missing source file retains its
captured empty 1x1 Donut descriptor classification; the first-party decoder must
fail with empty storage. the other 20 primary images compare their interpretation,
array/mip layout and all 320 decoded bytes. primary-image presence is also checked.

the original failure, retry, capacity, move and reset assertions remain. the
temporary filename probe now uses direct Windows file calls and still checks its
four literal bytes through a path with a null and stale tail. recorded executions
confine its new file to an owned temporary directory and verify exact cleanup.
the [reader checks](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-material-fixture-v1/reader-checks.json)
reject ten missing or altered inputs, including changed scalars, constant bytes,
texture identity, pixels and embedded swizzle bytes. these are finite CPU checks;
runtime, GPU and exact-package acceptance remain separate.
