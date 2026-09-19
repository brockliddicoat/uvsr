# installed SPIRV-Tools probes

these four authored assembly fixtures isolate tool support before RustGPU semantics change. they do not establish Rust-authored shaders, SPIR-T support, an upstream compiler regression pass, or GPU output. all generated binaries and logs remain ignored.

the separate [RustGPU pipeline recipe](rustgpu.md) runs these same inputs through the compiler's parser, SPIR-T and linker in both tool configurations.

```powershell
python tools/theta/probe_spirv.py --tools <SDK>/Bin --output work/theta/evidence/spirv-tools
```

the runner assembles, validates, links, optimizes with `-O`, disassembles, checks required instructions, reassembles, and validates each output under `vulkan1.3` (SPIR-V 1.6). the Windows host's API minimum remains Vulkan 1.4. the fixture target tests tool acceptance, not the final Rust artifact profile.

| case | discriminator |
| --- | --- |
| `logical_store` | ordinary logical storage-buffer control remains logical |
| `physical_store` | physical address conversion and `Aligned 4` store survive |
| `untyped_store` | ordinary storage-buffer descriptor with untyped access survives, explicitly separate from heaps |
| `descriptor_heaps` | real resource/sampler built-ins, descriptor-size/stride instructions, nonzero slots, texture sampling, and physical `Aligned 16` output survive together without ordinary descriptor bindings |

two non-executed negative controls remove physical alignment or the heap capability. validation must reject each with the expected diagnostic. missing tools, failed commands, missing outputs, lost instructions, and a changed negative-control mutation fail the run. `results.json` retains the fixed denominator of six cases, identities, commands, first failures, and pending RustGPU stages.

the raw-address fixtures are not dispatched. any later execution must use a real live allocation of adequate size and alignment, with host/device visibility, exclusive write access and completion before retirement. synthetic addresses must never be dereferenced.

the instruction contracts are the Khronos [physical storage buffer extension](https://github.khronos.org/SPIRV-Registry/extensions/KHR/SPV_KHR_physical_storage_buffer.html), [untyped pointers extension](https://github.khronos.org/SPIRV-Registry/extensions/KHR/SPV_KHR_untyped_pointers.html), and [descriptor heap extension](https://github.khronos.org/SPIRV-Registry/extensions/EXT/SPV_EXT_descriptor_heap.html). these tests are authored combinations of the documented instructions, not copied compiler implementations.
