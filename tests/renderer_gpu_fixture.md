# captured GPU reference data

the skin, upload and UI tests compare current GPU readbacks with independently
captured Donut output. the [reference receipt](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-gpu-controls-v1/reference-02/receipt.json)
pins two identical runs of the original suite, its capture-only instrumentation,
compiler records, shaders, D3D12 runtime and Windows font files. the
[consumed inputs](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-gpu-controls-v1/linked-control.json)
preserve the control libraries and source. capture uses the original default
`D3D12CreateDevice` adapter selection. these are exact controls for the recorded
inputs, not a claim of identical rasterization on every device or driver.
the hosted developer and production renderer builds select the captured v143
14.44 compiler family even when the runner's default Visual Studio generator
changes.

| fixture | records | bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `renderer_skin_gpu_fixture.bin` | 93 | 4,872 | `1c5f2eadc5caf3f0eb6dff97d553c9bef5bfce46a0d2d9258e8dc69418d3f823` |
| `renderer_upload_gpu_fixture.bin` | 621 | 42,502 | `afc802e200c30cf46e13fb7081b1463c3b5694ae08a95acc561f3ecf5ca0803a` |
| `renderer_ui_gpu_fixture.bin` | 48 | 1,291,810 | `a0d6b9d6e0ce3a3419d9871976425ed45a3c7b8833cbe835997f7239013b38df` |

skin records retain each input mode, JSON and buffer, six joint palettes and 36
attribute ranges, excluding padding. upload records retain 61 encoded inputs,
60 texture descriptions and 189 tightly packed subresources. the original
159 authored-byte checks remain separate. the existing two-slice DDS exception
still uses its literal control because Donut misclassifies that texture.

the skin fixture keeps inputs, keys, joint palettes, packed attributes, and UV
data byte-exact. transformed float ranges accept at most four IEEE 754 ULP per
word because the hosted CI adapter evaluates the same matrix operations with a
stable one- or two-ULP difference from the captured adapter. the retained
second-UV control range aliases transformed position storage, so it uses the
same bound. larger differences and all non-float differences still fail.

authored image subresources and base levels remain byte-exact. generated mip
levels allow at most one code value per channel for 8-bit normalized formats or
four IEEE 754 ULP per word for 32-bit float formats. this covers bounded
cross-adapter filtering and conversion rounding without weakening source-image,
native-mip, compressed-texture, layout, or ownership checks.

each of the 12 UI cases records its size, scale, scaling mode, format, atlas
dimensions, exact atlas, draw hash and exact rendered pixels. the exact UI
control applies when Segoe UI Semibold is
`2d9b22d71f72de2823fee5d9c8bc1b0fc32b2577c4c27b9ec6abdbb8df0e1731`
and Segoe UI Bold is
`aeb9e4a6ec5cc59f4d72df8189032d7dbb28f45161cf1552174818b5465dac4e`.
the test logs both installed font identities. a different Windows font revision
still renders every case twice and requires byte-exact repetition, but does not
claim equivalence to the inapplicable captured font control. the native draw
hash is captured before rendering: Donut scales clip rectangles in place.
the [capture correction](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-gpu-controls-v1/reference-02/correction.json)
changes only that hash in the two framebuffer-scaling cases. all captured pixels
remain unchanged. current rendering also repeats the frame against the fixed
pixels. original failure, retry, lifetime, budget and input checks remain.

the little-endian stream starts with `UVGZ0001`. each record contains a `uint32`
raw byte count, a `uint32` compressed byte count and an independent zlib stream.
zero followed by the record count and EOF closes the file. input records and
descriptions establish case order. compression is lossless; comparisons use
all decoded bytes. the reader bounds records to 16 MiB, checks allocation and
uses the already retained stb codec. it introduces no runtime dependency.
the [reader checks](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-gpu-controls-v1/reader-checks.json)
compare all 762 records with the raw captures and reject eleven missing or
altered fixtures, including correctly recompressed changes to expected pixels.

CTest supplies the test-data directory explicitly. generators and reference
files do not ship. production rendering, visual review and exact-package
acceptance remain separate.
