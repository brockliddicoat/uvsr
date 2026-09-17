# durable engine logging

[`engine_diagnostic_log.h`](engine_diagnostic_log.h) exposes a native path and a
borrowed context/function clock. [`engine_diagnostic_log_win32.cpp`](engine_diagnostic_log_win32.cpp)
owns the file, fixed message and output buffers, lock, repeat state and callback
restoration. initialization and shutdown have one owner; producers quiesce before
the borrowed clock context is destroyed.

one checked temporary allocation prepares the path and its UTF-8 diagnostic
spelling. parent directories are created iteratively, retaining native relative,
Unicode, dot-segment and extended-path behavior. the C stream is checked when
opened and uses a fixed supplied buffer. `_SH_DENYNO` preserves the previous
MSVC stream's sharing mode. Microsoft's [stream-sharing reference](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fsopen-wfsopen)
defines that native mode.

ordinary output flushes at the one-second boundary. error and fatal messages
flush immediately. an identical warning is coalesced for five seconds; its pending
count is emitted before the next distinct record or shutdown. warning keys use
the logging core's complete bounded message. the private `std::chrono::steady_clock`
keeps the previous monotonic time source; no owning standard container or callback
remains in this owner. replacing that clock would require equivalent native
conversion, range and timing-boundary evidence.

preparation and initial-write failures return false with the previous callback
restored. later write, flush or close failures report once through that callback.
file writes stop for the failed session while downstream messages continue. a
new initialization can retry. flushing preserves the existing process-termination
contract.

the original [diagnostics suite](../tests/nvrhi_d3d12_diagnostics_tests.cpp) retains
its exact severity, DRED/fatal text, suppression and restoration assertions. it also
checks preparation/I/O failures, retries, native paths, exact one/five-second
boundaries and warning keys that differ in their last byte. default startup path
selection and D3D12 file verification remain separate owners in
[`engine_startup.cpp`](engine_startup.cpp).
