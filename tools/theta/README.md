# direct Rust Vulkan slice

the [source mapping](../../tests/parity/primary-slice.md) owns the two frozen AGFX cases. the `agfx` crate executes both the buffer-copy case and the four-pass ordinary Rust compute case on Windows Vulkan. actual NGAPI shader consumers remain in [ngapi-probe](ngapi-probe/README.md).

## host checks

from the repository root with Rust installed:

```text
cargo fmt --all --check
cargo test --workspace --all-targets --locked
cargo test --workspace --doc --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
python -m unittest discover -s tools/theta -p test_agfx_*.py -v
```

ordinary Cargo tests never open Vulkan. the three compile-fail examples check buffer/device lifetime, sending a device and sharing device access across threads. one build worker is sufficient. when using the owned RustGPU environment, use a separate host `CARGO_HOME` without its compiler dependency patches and a separate `CARGO_TARGET_DIR`. do not change a user's Rust installation or the compiler's configuration to build this host.

## explicit Windows GPU check

review [U-010](../../UNSAFE.md#u-010-rust-vulkan-device-buffer-and-synchronous-submission) before changing or executing the boundary. build `buffer_copy` in Debug and Release, then run each separately:

```text
cargo build --bin buffer_copy --locked
cargo build --bin buffer_copy --release --locked
python tools/theta/run_agfx_copy.py --executable <host-target>/debug/buffer_copy.exe --sdk <Vulkan-SDK-root> --output-dir <ignored-evidence>/copy-debug
python tools/theta/run_agfx_copy.py --executable <host-target>/release/buffer_copy.exe --sdk <Vulkan-SDK-root> --output-dir <ignored-evidence>/copy-release
```

the host preserves AGFX's discrete/integrated/virtual/CPU preference, choosing the first enumerated Vulkan 1.4 adapter within each kind that has a graphics/compute queue and the exact declared features. the record identifies the actual device. missing support is a failed required run, not a pass or backend substitution. the runner uses the SDK's explicit Khronos layer, core/synchronization validation, a 45-second process budget and a fresh token. it compares all 256 bytes with the unchanged source golden and requires all 17 native controls. source hashes embedded at compilation reject stale executables before device creation. raw output, native streams, incremental `events.jsonl` and atomic versioned `result.json` stay in the output directory. an interrupted native process can leave an unknown execution count (`null`), never a zero-case pass. a new run invalidates the previous result before preflight.

the runner retains exact intentional loader notices about disabled implicit layers separately. every other callback warning/error, VUID, synchronization hazard, missing case, stale identity, interruption or bad output fails. loader notices are not claimed absent.

## ordinary Rust shader compilation

use the owned RustGPU compiler and its matching ordinary logical32 test sysroot:

```text
python tools/theta/compile_agfx_shader.py --rustgpu-source <owned-rust-gpu> --codegen-backend <rustc_codegen_spirv.dll> --output-dir <ignored-build>/agfx-shaders
```

the command compiles opt0/opt3, validates the final SPIR-V and emits explicit stage, entry, profile, capabilities and compiler/source/payload identities. this is an ordinary storage-buffer descriptor array. it is not NGAPI's native heap profile. the shared input helper preserves all four existing NGAPI fixture payloads. compilation alone does not establish GPU execution. the separate runner below performs that check.

## four-pass ordinary compute execution

review [U-009/U-011](../../UNSAFE.md#u-011-bounded-ordinary-storage-buffer-compute-pipeline) and the final modules before changing the accepted shader hashes. `BufferCompute` uses four ordinary descriptors, a 16-byte root and one 64-thread group per pass. its unsafe constructor has an explicit bounded shader contract. the fixture admits only the two reviewed payload identities and matching source/metadata before opening Vulkan. it exposes no safe arbitrary bytecode loader.

```text
cargo build --bin multi_dispatch --locked
cargo build --bin multi_dispatch --release --locked
python tools/theta/check_agfx_artifacts.py --executable <host-target>/debug/multi_dispatch.exe --shader-dir <agfx-shaders> --output-dir <ignored-evidence>/artifact-controls
python tools/theta/run_agfx_compute.py --executable <host-target>/debug/multi_dispatch.exe --sdk <Vulkan-SDK-root> --shader-dir <agfx-shaders> --output-dir <ignored-evidence>/compute-debug
python tools/theta/run_agfx_compute.py --executable <host-target>/release/multi_dispatch.exe --sdk <Vulkan-SDK-root> --shader-dir <agfx-shaders> --output-dir <ignored-evidence>/compute-release
```

four required cases cover opt0/opt3 and resource slots 1/3. each compares 256 selected bytes with the frozen AGFX golden and all 768 unselected sentinel bytes exactly. ten native controls isolate missing initialization, wrong index, size, memory role and device. eight artifact controls reject missing/corrupt payloads or incompatible metadata before Vulkan. size and foreign-device controls are initialized first, so their rejection cannot be satisfied by an unrelated initialization error.

pipeline/layout/pool/set ownership remains live through completion. every descriptor is rewritten before reuse, including after a prior case's buffers have been destroyed. no test depends on undefined GPU access, invalid descriptor dereferencing or deliberate device loss. these cases do not establish full AGFX C/Cpp/Ez or Linux runtime parity.

## ShaderToHuman library compilation

```text
python tools/theta/compile_s2h_library.py --rustgpu-source <owned-rust-gpu> --codegen-backend <rustc_codegen_spirv.dll> --output-dir <ignored-build>/s2h-library
```

the [library probe](../../shaders/rust/shader_to_human_library.rs) covers input-dependent gather, widgets, scatter and 3D calls at opt0 and opt3. the tool uses the same pinned logical sysroot and exact glam/libm libraries as the existing compiler gate. it records both compile commands, full library and entry hashes, SPIR-V identity, capabilities and independent validation. it neither dispatches a GPU nor establishes golden parity. the [mapping](../../tests/parity/shader-to-human.md) owns source coverage and remaining fixture/example work.

## ShaderToHuman original image comparison

the [Features mapping](../../tests/parity/shader-to-human-features.md) covers `compile_s2h_library.py --features`, `run_s2h_features.py` and `compare_s2h_features.py`. it separates the passing native/state/structural gate from the still-failing strict HLSL image comparison. Features requires queried float32 NaN preservation, current pre-execution review identities and all54 fixed steps at both shader optimization levels.

```text
python tools/theta/compile_s2h_library.py --images --rustgpu-source <owned-rust-gpu> --codegen-backend <rustc_codegen_spirv.dll> --output-dir <ignored-build>/s2h-images
cargo build --bin shader_to_human --locked
python tools/theta/run_s2h_fixtures.py --images --executable <host-target>/debug/shader_to_human.exe --sdk <Vulkan-SDK-root> --shader-dir <ignored-build>/s2h-images --output-dir <ignored-evidence>/s2h-images-debug
python -m unittest discover -s tools/theta -p test_s2h_images.py
```

the image runner requires Pillow and repeats for Release with a separate output directory. it checks two completed executions into the same RGBA8_UNORM image, with native conversion and complete alpha-inclusive comparison against the frozen source PNGs. `execution_passed` is separate from exact golden `passed`. all ten executions currently pass while six exact comparisons pass, so the complete image parity gate returns failure. the [mapping](../../tests/parity/shader-to-human.md) records the remaining 2D/3D differences. expected images and thresholds are unchanged. [U-018](../../UNSAFE.md#u-018-shadertohuman-rgba8-image-entries-and-two-executions) registers bounded shader writes, artifact identities and host ownership. original float diagnostics remain available below.

## ShaderToHuman fixture float readbacks

```text
python tools/theta/compile_s2h_library.py --fixtures --rustgpu-source <owned-rust-gpu> --codegen-backend <rustc_codegen_spirv.dll> --output-dir <ignored-build>/s2h-fixtures
cargo build --bin shader_to_human --locked
python tools/theta/run_s2h_fixtures.py --executable <host-target>/debug/shader_to_human.exe --sdk <Vulkan-SDK-root> --shader-dir <ignored-build>/s2h-fixtures --output-dir <ignored-evidence>/s2h-fixtures
python -m unittest discover -s tools/theta -p test_s2h_fixtures.py
```

the ten required cases cover all five original fixtures at opt0/opt3. each uses the [U-016](../../UNSAFE.md#u-016-ordinary-storage-compute-interface-and-ordered-dispatch) ordinary storage owner, a zeroed 800x600 float4 buffer and synchronous readback. exact reviewed payloads, embedded/live source identities, root bytes, dispatch dimensions, finite output, output hashes and core/synchronization validation are required. each native case has its own process, timeout and preserved logs. the eight CPU controls reject stale/missing/skipped evidence, changed ABI, invalid floats, signed-zero empty output and unsupported parity claims. a failed native process leaves execution unknown until a complete record proves it.

this is an intermediate float-buffer diagnostic, not the source RGBA8 storage-image path or a golden pass. all source PNGs remain unchanged. original image conversion, two-run capture behavior, interactions, documentation/examples and actual NGAPI integration remain required. the shared CPU fixture bodies can also be run with `cargo run -p shader-to-human --example render_fixtures -- <case> <output.rgba32f> [camera.txt]`.

## AGFX texture transfer goldens

the later [format-view mapping](../../tests/parity/format-views.md) covers `compile_agfx_graphics.py --views` and `run_agfx_views.py`, including the required current review record. it tests UNORM/sRGB storage, sampled and attachment views through one texture owner.

```text
cargo build --bin texture_copy --locked
cargo build --bin texture_copy --release --locked
python tools/theta/run_agfx_textures.py --executable <host-target>/debug/texture_copy.exe --sdk <Vulkan-SDK-root> --output-dir <ignored-evidence>/textures-debug
python tools/theta/run_agfx_textures.py --executable <host-target>/release/texture_copy.exe --sdk <Vulkan-SDK-root> --output-dir <ignored-evidence>/textures-release
python -m unittest discover -s tools/theta -p test_agfx_textures.py
```

use a Python environment with Pillow for these two commands. the [texture mapping](../../tests/parity/textures.md) owns the source cases and remaining scope. the runner checks all original pixels/bytes, four additional exact outputs, twelve native rejections, fresh source/executable identity and explicit core/synchronization validation. timeout or abnormal exit retains unknown execution. this runner does not execute shaders, infer full texture API parity or waive ShaderToHuman's original image comparison.
