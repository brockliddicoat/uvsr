# scene reference data

`import_scene_fixture.bin` preserves ten original graph fixtures and four
classified Donut placement defects. the normal fixtures contain 51 nodes, seven
lights, eight cameras, 11 animations, 12 samplers and 24 keyframes.

the [reference receipt](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-scene-fixture-v1/reference-01/receipt.json)
records two identical captures and the compiler/executable inputs. the writer
reads only the actual retained glTF graph and original input bytes. native
pointer identity defines sampler sharing and first-use order. the original
complete test runs its assertions separately. the [consumed inputs](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-scene-fixture-v1/linked-control.json)
reverify archived source and library bytes.

the 22,474-byte file has SHA-256
`d4c43d14588e8e4cd46cc5aa6491f369c01cbab904ae4655343eae2f2356ff83`.
the little-endian stream starts with `UVIS0001` and a count of 14. each case has
its kind, order, exact JSON/GLB and buffer bytes. normal records contain table
counts, node names, parents, transform presence, double world values, light and
camera fields, animation channels, sampler identity/mode and key data. scalar
fields retain exact equality; all three vectors per key retain byte comparison.
special records contain the original stale-owner and auxiliary-leaf counts.
`UVISEND1` and EOF close the stream. the reader uses fixed storage and no STL
containers. no generator target or fixture is shipped.

corrected behavior stays separate from donor defects. shared camera/light
placements keep reciprocal links, skins retain co-located auxiliary leaves, and
reordered samplers retain the candidate's explicitly checked interpolation.
the old two-mode discrepancy is still counted. the known invalid unused-sampler
control remains unexecuted; its candidate assertions stay unchanged.

all rejection, retry, capacity, move, reset and runtime-light assertions remain.
the 18 runtime-light cases use an ordinary two-value loop in place of an
initializer list. the [reader checks](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-scene-fixture-v1/reader-checks.json)
reject eleven missing or altered fixtures, including changed world values,
lights, cameras, animation targets, keys and classified defects. these are finite
CPU checks; runtime rendering, GPU and exact-package acceptance remain separate.
