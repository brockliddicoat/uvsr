# view control data

`renderer_view_fixture.bin` contains the original 1,027 view inputs and independently captured Donut results. its SHA-256 is `6adf6221361a15e99da90965153a4df5b5b4930819407213b5feb6ebd71ef4e0`, with 1,129,780 bytes. it is a fixed test input, never a generated build product or shipped asset.

the control uses Donut `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`, the original view test setup and seed, and MSVC x64 Release, C++17, static CRT, `/O2 /fp:precise /EHs-c-`. [the capture receipt](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-view-fixture-v1/reference-01/receipt.json) preserves the generator, consumed source/header bytes, compiler records, executable and two identical outputs. no candidate view function is compiled or called by that generator.

the file uses little-endian binary32 floats and 32-bit integers. its 80-byte header is `UVVW0001`, a 32-bit case count of 1,027, a 32-bit record size of 1,100, and the original 16-float projection for the invalid-input checks. each record stores these fields in order, without struct padding:

| field | bytes |
|---|---:|
| input viewport, min/max X, Y, Z | 24 |
| input world-to-view matrix, row order | 64 |
| input projection, row order | 64 |
| input pixel offset | 8 |
| expected view constants | 720 |
| expected six frustum planes | 96 |
| expected output viewport | 24 |
| expected scissor, min/max X, Y | 16 |
| expected translated-background matrix | 64 |
| expected view direction | 12 |
| expected reverse-depth and mirrored flags | 8 |

case order is offset perspective, mirrored perspective, mirrored orthographic, then the original 1,024 seeded affine cases. stored inputs preserve the original MSVC argument evaluation order. this is finite regression coverage, not a claim about every matrix or compiler.

the test reads one record at a time and retains exact constants, planes, translated matrices, flags and NVRHI viewport/scissor comparisons. it also retains repeat-build identity, explicit reverse-Z/reconstruction equations and all 16 invalid-input checks requiring unchanged output. CTest selects this directory as its working directory; a direct invocation needs the fixture in its working directory. missing, malformed, truncated and trailing input must fail.
