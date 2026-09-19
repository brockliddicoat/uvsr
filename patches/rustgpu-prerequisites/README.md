# compiler prerequisite patches

these are tested changes from separately owned upstream checkouts. [sources.json](sources.json) pins each base, local verified commit, patch hash and controlling license. they are not published upstream contributions or proof of complete RustGPU support.

## rspirv untyped globals

[rspirv-untyped-global.patch](rspirv-untyped-global.patch) fixes the binary loader's placement of module-scope `OpUntypedVariableKHR`. the regression serializes and parses globals with and without the optional Data Type operand, and a function-local variable. it checks exact word round-trip and placement. this is a loader test, not shader validation or execution.

the one-line placement fix also exists in the pre-existing [upstream PR #264](https://github.com/gfx-rs/rspirv/pull/264). this patch was developed independently before finding that PR. treat the upstream work as a prerequisite to coordinate with, not a new contribution to duplicate. the added regression is local work. all patch code and context retain rspirv's [Apache-2.0 license](../../legal/licenses/rspirv-Apache-2.0.txt).

apply only to an owned checkout at the exact source pin:

```powershell
$theta = '<absolute Theta checkout>'
$rspirv = '<absolute separate rspirv checkout>'
if ((git -C $rspirv rev-parse HEAD) -ne '8afc3d0ac8e158128cd1410bb2e4b4c26ab11bb4') { throw 'wrong rspirv revision' }
$patch = Join-Path $theta 'patches/rustgpu-prerequisites/rspirv-untyped-global.patch'
git -C $rspirv apply --unidiff-zero --check $patch
if ($LASTEXITCODE -ne 0) { throw 'patch does not apply' }
git -C $rspirv apply --unidiff-zero $patch
if ($LASTEXITCODE -ne 0) { throw 'patch failed' }
Set-Location -LiteralPath $rspirv
cargo +nightly-2026-07-03 generate-lockfile
if ($LASTEXITCODE -ne 0) { throw 'lockfile generation failed' }
cargo +nightly-2026-07-03 test -p rspirv --locked -j 1
if ($LASTEXITCODE -ne 0) { throw 'rspirv tests failed' }
```

the pinned upstream repository does not track a lockfile. record the locally resolved lockfile and toolchain with each run. our Windows run passed 82 unit tests, one binary-fixture test and six documentation tests, with zero failures or ignored tests. the existing [RustGPU pipeline probes](../../tests/compiler-probes/rustgpu.md), using this local rspirv and its matching `spirv` crate, then passed all four parser cases. SPIR-T and RustGPU inference still fail later. [execution E-009](../../docs/execution.md#e-009-2026-09-19-repaired-untyped-global-loading) records that boundary.
