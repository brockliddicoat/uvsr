# scene encoding reference data

`renderer_scene_encoding_fixture.bin` retains 3,072 material constant buffers,
1,056 light world-frame and constant-buffer comparisons, 485 local light
transforms, eight singular cutoff results and one zero-scale point-light buffer.
45 setup records preserve the original root/parent transforms as double values.

the [reference receipt](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-encoding-fixture-v1/reference-04/receipt.json)
records two identical captures from the original material and light test
functions. the writer reads only native material/light serialization, native
world frames and native local transforms. it does not read candidate output.
the [consumed inputs](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-encoding-fixture-v1/linked-control.json)
preserve the compiled control and its source/library dependencies.

the file contains 930,568 bytes, SHA-256
`101f0af0e69d954dd87bbaf539faeca60ae0398ad3f785f56b7cf700d449da4a`.
its little-endian stream starts with `UVSE0001`. record tags 1 through 6 identify
material bytes, setup transforms, light frames/bytes, local transforms, cutoff
booleans and point-light bytes. selectors retain the material domain/masks,
setup seed and light index. byte records include their ABI size; transforms use
translation XYZ, quaternion XYZW and scaling XYZ. tag 7 closes the stream with
all six comparison counts and EOF. the reader uses fixed scalar storage.

the original geometry/instance encoding bodies, material descriptor failures,
light ordering, no-op revisions, nine invalid poses, stale/malformed inputs and
singular-output assertions remain. the [reader checks](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-encoding-fixture-v1/reader-checks.json)
reject eleven missing or altered fixtures. both CTest routes pass.

the former Donut adapter has retired. [current owner tests](renderer_scene_tests.cpp)
retain copied preparation, declared bounds, allocation failures, transactions and
complete deep/wide temporal checks. GPU tests use copied canonical input and the
production resource path. native pointer lookup, mirrored values and unsupported
native shadow objects disappear with their adapter. the renderer-only encoding
target consumes no Donut, jsoncpp, NVRHI or native API headers.
neither the reference data nor its generator ships. runtime rendering and
exact-package acceptance remain separate.
