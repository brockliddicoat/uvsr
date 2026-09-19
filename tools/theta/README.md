# direct Rust Vulkan slice

the [source mapping](../../tests/parity/primary-slice.md) owns the two frozen AGFX cases. the `agfx` crate currently implements the buffer-copy case. its ordinary Rust multi-dispatch shader compiles, but pipeline/descriptor execution is pending. actual NGAPI shader consumers remain in [ngapi-probe](ngapi-probe/README.md).

## host checks

from the repository root with Rust installed:

```text
cargo fmt --all --check
cargo test --workspace --all-targets --locked
cargo test --workspace --doc --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
python -m unittest discover -s tools/theta -p test_agfx_copy.py -v
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

the host selects the first enumerated Vulkan 1.4 adapter with a graphics/compute queue and the exact declared features. the record identifies the actual device. missing support is a failed required run, not a pass or backend substitution. the runner uses the SDK's explicit Khronos layer, core/synchronization validation, a 45-second process budget and a fresh token. it compares all 256 bytes with the unchanged source golden and requires all 17 native controls. source hashes embedded at compilation reject stale executables before device creation. raw output, native streams and versioned `result.json` stay in the output directory.

the runner retains exact intentional loader notices about disabled implicit layers separately. every other callback warning/error, VUID, synchronization hazard, missing case, stale identity, interruption or bad output fails. loader notices are not claimed absent.

## ordinary Rust shader compilation

use the owned RustGPU compiler and its matching ordinary logical32 test sysroot:

```text
python tools/theta/compile_agfx_shader.py --rustgpu-source <owned-rust-gpu> --codegen-backend <rustc_codegen_spirv.dll> --output-dir <ignored-build>/agfx-shaders
```

the command compiles opt0/opt3, validates the final SPIR-V and emits explicit stage, entry, profile, capabilities and compiler/source/payload identities. this is an ordinary storage-buffer descriptor array. it is not NGAPI's native heap profile. the shared input helper preserves all four existing NGAPI fixture payloads. compilation does not establish this shader's GPU execution.
