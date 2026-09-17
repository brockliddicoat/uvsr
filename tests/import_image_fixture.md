# image reference data

`import_image_fixture.bin` preserves 349 original Donut texture comparisons and
one classified 1D-array omission. the unchanged encoders produce the same PNG,
BMP, TGA, JPEG, HDR, DDS and EXR inputs. the reference writer reads only the
retained decoder's metadata, layout and pixels. its full driver also runs the
existing failure and GIF checks, which do not supply reference values.

the [reference receipt](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-image-fixture-v1/reference-02/receipt.json)
pins two identical captures, the compiler configuration and executable. the
[consumed inputs](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-image-fixture-v1/linked-control.json)
preserve Donut, codec and library records. both captures also archive every
encoded input with SHA-256 provenance. the 132,919-byte fixture has SHA-256
`9ecad4cb52ee7088012e1f8f68748f154b30117cdaa71f53b7ec7307e6e0c920`.

the little-endian stream starts with `UVII0001` and 350 records. each record
identifies its kind, order, path, MIME, sRGB request, encoded length and FNV-1a
fingerprint. the fingerprint detects case drift; it is not cryptographic proof.
normal records store 11 metadata integers, then each subresource's offset, row
pitch, depth pitch, full depth extent and exact pixels. 424 subresources contain
87,552 compared bytes. the array omission stores its original count of one.
`UVIIEND1` and EOF close the stream. the reader uses fixed 256-byte scratch.

format names are normalized through the original mapping. dimension conversion
is checked against the original mapping before capture, including array/cube
distinctions. the original conditional check for a zero native depth pitch stays
conditional. `StbImageBlob::size()` returns zero; its decoded extent comes from
the original layout. other blob ranges are checked during capture.

the two-slice 1D DDS correction remains separate from Donut's omission. the test
also compares both complete slices directly with the encoded DDS payload. all
existing ownership, rejection, allocation, GIF and Windows extension checks
remain. missing, truncated, trailing, wrong-input and changed-pixel reference
data fail the [reader checks](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-image-fixture-v1/reader-checks.json).
these are finite CPU regressions, not runtime or package evidence. no generator
target or fixture is added to the shipped package.
