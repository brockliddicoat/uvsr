# composition reference data

`import_composition_fixture.bin` preserves 74 original file inputs, 135 graph
comparisons, three GPU joint palettes, two unavailable-source observations and
one classified URI-cache defect. it is 433,182 bytes with SHA-256
`cb2d90f52f9cfd45f5c0e2e84823b47b990c2f88c44c226612817e074c6d968d`.

the [reference receipt](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-composition-fixture-v1/reference-02/receipt.json)
records two identical captures while the complete original native suite passed.
the [linked controls](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-composition-fixture-v1/linked-control/receipt.json)
preserve 14 libraries, their compiler records and the 16-file Donut engine overlay.
expected properties, encoded swizzle data, decoded pixels and joint-buffer bytes
come from the native owners. index selectors use the original, fully asserted
canonical-to-native identity bindings. the fixture therefore also fixes those
indices; changing their internal numbering requires an explicit reference review.

the [current test](import_composition_reference_tests.cpp) retains 36 scenes,
267 nodes, 20 animation channels, 13 texture comparisons and three exact palettes.
the complete CPU loading path separately checks 36 scenes and 13 decoded textures.
all 41 import and 16 canonical allocation failures retain unchanged input arrays,
payloads, pointers and output statistics, followed by successful same-input retry.
the original exact and one-byte-short scratch, geometry and image budgets remain.
these checks run inside the existing CPU composition target with its independent
failure, light and measured 64 KiB hierarchy tests.

physical development/package layouts still load external glTF buffers, encoded
URI filenames and GLB buffer images. they now use `NativeImportFileSource`.
each run creates one unique temporary root, then removes only its own files and
empty directories. only that exact root prefix becomes `$physical` in the
reference. every relative path and file byte remains exact. other input and
expected strings retain their original bytes, including stale URI tails. the
classified defect still requires one decoded filename and the first request's
linear color space in the replacement. embedded swizzles permit the original
single trailing cgltf padding byte.

the little-endian stream starts with `UVIC0001`. tagged input, graph, palette,
error and defect records contain bounded strings, scalar fields and exact spans.
the footer counts every consumed record and requires EOF. native palette capture
used the original default D3D12 device selection; the current comparison is CPU
data against those preserved GPU bytes. the fixture does not add a runtime codec,
GPU test dependency or generator target. test input construction retains private
`string`, `vector` and `filesystem` helpers; file payloads use explicit import
owners. no shared native owners or VFS interfaces remain.

[nineteen negative controls](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-composition-fixture-v1/negative-01/receipt.json)
reject missing, truncated, trailing and altered inputs, including sharing,
transforms, geometry, materials, pixels, swizzles, inverse binds, animation keys
and palettes. production runtime, visual review and exact-package acceptance
remain separate.
