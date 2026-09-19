# first AGFX Vulkan slice

T002/T003 source mapping and fixture freeze, 2026-09-19. source: AGFX `f91b108a111d2ca3ca4b6586b6cb5dd750064fd7`. [file and fixture hashes](primary-slice-sources.json) freeze this read scope. all port dispositions below are **blocked/unimplemented**, with equivalent mappings proposed for the duplicate C/Cpp/Ez paths. no Rust or AGFX reference execution is claimed.

[AGFX port notes](../../docs/agfx-port-notes.md) retain source behaviors worth revisiting for optimization or cleaner Rust. A-001 through A-007 cover transfer waits/resource churn, submission allocations, descriptor index types, mapped views, barrier scope, memory selection and queue-family assumptions. update inheritance and disposition as each owner is implemented.

## cases and exact oracles

| planned stable case | source registrations | preserved behavior / oracle | target owner |
| --- | --- | --- | --- |
| `agfx.copy_buffer_to_buffer` | `CopyBufferToBuffer` C, Cpp, Ez in `src/agfx/agfx_tests/tests/test_copy_buffer_to_buffer.cpp` | 64 little-endian words. source word `0xC0DE0000 \| (i * 7 + 1)`. copy source bytes 0..128 to destination 0..128, then source 64..192 to destination 128..256. compare all 256 bytes against the unchanged upstream golden | `crates/agfx`, copy/readback integration case |
| `agfx.compute_multi_dispatch_buffer` | `ComputeMultiDispatchBuffer` C, Cpp, Ez in `src/agfx/agfx_tests/tests/test_compute_multi_dispatch_buffer.cpp` | zero 64 words. four dispatches of one 64-thread group. each pass stores `previous * 2 + pass_index + lane_index`, with a UAV barrier between passes. expected word `15 * i + 11`, exact 256-byte comparison | `crates/agfx`, Rust fixture under `shaders/rust` |

the copied [copy golden](fixtures/agfx/copy_buffer_to_buffer.bin) and [compute golden](fixtures/agfx/compute_multi_dispatch_buffer.bin) are source inputs. both match the independently evaluated source formulas. that verifies fixture identity and interpretation, not GPU/reference parity. retain exact byte comparisons, initialization, bounds checks, upload failure, missing output and completion assertions. candidate output never writes these files.

## owner and caller mapping

| source owner / symbols | required slice behavior | proposed Rust mapping / boundary |
| --- | --- | --- |
| `agfx.h`, `agfx_vulkan.cpp`: `agfxDeviceCreate`, command queue creation, `agfxDeviceWaitIdle` | explicit Vulkan selection, queried capabilities, headless device, graphics/compute-capable queue, cleanup after partial creation | one `Device` owner. native Windows loader is the first route. Linux remains separately unverified |
| `agfxBufferCreate`, `agfxBufferMap`, `agfxBufferUnmap`, `agfxBufferDestroy` | exact sizes, upload/GPU/readback memory roles, initialized upload bytes, mapped lifetime and completion before reuse | owned `Buffer` borrowing its device. checked ranges and explicit completion. native calls/mapping need a reviewed unsafe record before implementation |
| `agfxBufferViewCreate`, `agfxBufferViewGetHandle` | raw writable view, source host handle returned as `uint64_t`, shader resource index as 32 bits | preserve widths/sentinel per field and reject invalid conversion. ordinary source resources use set 0 binding 0, samplers binding 1. this does not implement NGAPI heaps. [A-003/A-008](../../docs/agfx-port-notes.md) record handle and slot-allocation candidates |
| `agfxShaderModuleCreate`, `agfxComputePipelineCreate` | SPIR-V words, exact entry, stage and group size; shader module retired after pipeline creation | explicit artifact metadata and a compute pipeline owner. active authored shader is Rust, source HLSL remains the oracle |
| compute pass begin/end, pipeline binding, push constants, copy/dispatch, `BufferUAVBarrier` | 16-byte ordinary AGFX root: resource index, element count, pass index, padding. source state transitions and ordered dependent dispatches | concrete recording owner and barriers. no render graph, implicit shader ABI, or generic backend layer |
| queue submit/signal, `agfxFenceWait`, `GpuFixture::RecordAndSubmit`, `UploadBuffer`, `ReadbackBuffer` | timeline completion before CPU reads and destruction; source failure assertions | completion token tied to the originating queue/device. unfinished work prevents retirement |
| `agfx.hpp`, `agfx_ez.hpp`, Ez test paths | Cpp RAII destruction, Ez upload/view/state handling, frame submission and `DrainGPU` before readback | use ordinary Rust ownership for equivalent behavior. do not claim Ez parity until these distinct lifetime/state assertions are implemented and tested |

the Windows source xmake route selects D3D12; its Linux route selects `agfx_vulkan.cpp` and the SPIR-V compiler. the new Rust owner must choose Vulkan explicitly. AGFX reference execution on Windows Vulkan is therefore unavailable from the unchanged stock build route. this is a source/platform limitation, not a waived reference requirement. no source checkout was changed or built.

## scope, accounting and attribution

the primary slice contains two behavior groups and six source registrations. the full source-only inventory (401 registrations, 149 distinct names, 109 test C++ files) remains pending under T025. texture/image behavior, other AGFX APIs and meaningful Ez behavior remain supporting Vulkan scope. ShaderToHuman remains pending under T026-T029. future Metal/DirectX and additional language variants are deferred only by D011-D013 in [research](../../docs/specs/001-theta-prototype/research.md). missing Vulkan behavior is never reclassified as deferred.

the source file list also supports later matched-scope measurement. these files include unported behavior, so their total LOC cannot be compared with a two-case Rust port as an equivalent size claim. no size reduction is measured yet.

the fixtures retain AGFX copyright 2026 Amélie Heinrich and the complete [MIT notice](../../legal/licenses/AGFX-MIT.txt). the source HLSL and C/C++ were inspected and hashed, not translated into an implemented Rust shader yet. no ShaderToHuman files or goldens are imported in this checkpoint.
