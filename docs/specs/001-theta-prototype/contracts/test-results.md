# test, report, and query contract

## one result model

the runner, JSON, bounded queries, and offline HTML viewer share the [data model](../data-model.md). preserve source IDs as well as stable Rust case/variant IDs. record the complete required inventory and the diagnostic selection separately. also retain the primary/supporting/future scope classification, so deferred variants never appear as passes. minimal truthful records support the primary RustGPU contribution. full report/query presentation is supporting work.

outcomes are `pass`, `fail`, `unsupported`, `blocked`, `unimplemented`, and `incomplete`. `unsupported` requires a real optional backend/device capability reason. absent hardware is `blocked`. compiler gaps are not device limitations. required missing cases, empty selections, all-skipped selections, missing goldens/readback, stale identity, and interruption cannot yield accepted-suite success.

write phase events incrementally and publish completed results atomically. a cached pass belongs only to the same source/diff, compiler, shader, fixture, oracle, configuration, and device-relevant identity. preserve raw diagnostics and native validation messages alongside concise interpretations.

## oracles

preserve source assertions and reviewed goldens. use exact buffer/state comparisons, numeric tolerances with an explicit domain, alpha, glyph/write masks, selected pixels/regions, and perceptual metrics where appropriate. a global FLIP threshold cannot detect every small formatting defect. agreement between variants supplements independent CPU/analytic or source golden evidence. the active authored route is RustGPU to SPIR-V on Vulkan, with Windows and Linux host evidence recorded separately.

ShaderToHuman's reference configuration has exact 800 x 600 RGBA comparisons, fixed camera/fixture state, and two technique executions per capture. reproduce resource formats, sRGB conversion, initialization, dispatch/write ordering, barriers, row pitch, and persistent UI state explicitly. ambiguous Gigi defaults require reference evidence. scatter cases need defined ownership of overlapping writes.

capture timing and scripted input states deterministically. a source 3D drawing algorithm using rays is not automatically a hardware-raytracing requirement. source scene/example behavior remains inventoried when a smaller fixture is used.

## report appearance

this section is supporting testbed acceptance, not a prerequisite to draft the RustGPU PR.

adapt AGFX's pinned report components: dark panels, image cards, status borders, badges, filter chips, output/golden previews, numeric comparisons, and detailed failures. add top-level **AGFX** and **ShaderToHuman** tabs with independent counts, search, filters, and suite identity. C/Cpp/Ez source API flavors must not be confused with authored shader language or host OS. a future language field does not mandate another compiler route now.

retain the source palette unless a documented accessibility fix is needed: background `#0e1014`, panel `#171a21`, border `#2a2f3a`, text `#e5e8ee`, pass `#3fb950`, fail `#f4564a`, skip `#d2a13a`, accent `#4a9eff`. use text status, semantic tabs, labels, keyboard navigation, and visible focus. deep links identify suite, case, backend, language, and artifact.

presentation acceptance uses the actual populated report, both tabs, filters, a failed-case detail, keyboard interaction, screenshots, and counts checked against the JSON. optional ShaderToHuman debugging overlays are separate artifacts and cannot change the canonical comparison output.

## bounded query interface

proposed verbs are `list`, `show`, `run`, `compare`, and `explain`. these are requirements to implement, not installed commands. use stable IDs, filters, bounded pages, concise/default and opt-in detailed output, and explicit truncation with total counts.

`explain` returns recorded evidence, not an LLM-generated verdict: failing phase, expected/actual discriminator, first byte or pixel/channel, relevant source span, reproduction argument array, and linked log/image/crop/disassembly. `compare` rejects incompatible result identities instead of making a misleading comparison. preserve source-language diagnostics through compiler intermediates.

negative controls cover a wrong byte, wrong pixel, alpha-only change, faulty oracle, missing golden/shader/readback, empty selection, required-feature skip, stale identity, and interrupted execution. the exit state, result record, and report must agree. verify synchronized fixture resets between cases.

## usability evaluation

compare the original report/console workflow with focused result queries on matched seeded faults using the same model/settings. keep separate unseen variants. use independent assertions to grade correct localization, verified repair, false passes, unnecessary edits, repeat attempts, tool calls, and context use. include human tasks to find a failure, inspect a difference, compare variants, and reproduce a result.

the format is a design proposal until those observations exist. an agent's confidence or a visually attractive report does not establish an improvement or GPU correctness.
