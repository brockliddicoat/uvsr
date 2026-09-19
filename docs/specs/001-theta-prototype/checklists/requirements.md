# specification audit checklist

this checks documentation and traceability. checked items are not implementation passes. the clean agent handoff is [prompt.md](../prompt.md).

- [x] current main and the source pins have dated evidence.
- [x] the primary deliverable is the RustGPU PR draft enabling actual NGAPI.
- [x] native Windows Vulkan is the first local path, with Linux portability separate.
- [x] full testbed parity and report polish do not block the primary contribution.
- [x] AGFX and ShaderToHuman source parity remains explicit and scoped.
- [x] Metal is second backend priority, DirectX lowest, with no secondary-backend implementation gates.
- [x] additional-language adapters and the nine-cell requirement are retired.
- [x] safe Rust defaults and every necessary exception have a prominent documentation and review contract.
- [x] tracked execution and reusable lessons are required at meaningful checkpoints.
- [x] size preference has matched-behavior measurement rules and quality constraints.
- [x] every functional requirement maps to work and acceptance below.
- [x] the agent prompt contains execution instructions separately from human review history.
- [x] implementation task completion remains unchecked.

## requirement traceability

| requirement | tasks | acceptance |
| --- | --- | --- |
| FR-001 | T001 | pinned baseline, preserved inputs/index, SC-007 |
| FR-002 | T002-T003, T006-T008, T024 | small native Windows Vulkan port, then scoped parity, SC-002/SC-003 |
| FR-003 | T002, T025 | complete source denominator and dispositions, SC-002 |
| FR-004 | T001, T004, T007, T017, T023 | Windows direct/NGAPI evidence, explicit future boundaries, SC-001/SC-003 |
| FR-005 | T004, T006, T017 | artifact/profile and tool identity, SC-001/SC-003 |
| FR-006 | T005, T010-T013 | upstream P/A/R evidence, SC-001/SC-006 |
| FR-007 | T005, T014-T016 | actual H instruction/runtime evidence, SC-001/SC-006 |
| FR-008 | T007-T009, T017, T019 | actual Windows NGAPI plus direct diagnosis, SC-001/SC-003 |
| FR-009 | T026-T029 | library and all source behavior mappings, SC-002/SC-004 |
| FR-010 | T003, T009, T027, T029 | preserved oracles, separate corrections, SC-004/SC-005 |
| FR-011 | T030, T032 | supporting populated tabs and navigation, SC-005/SC-008 |
| FR-012 | T009, T031 | canonical records and reproduction, SC-005 |
| FR-013 | T009, T031, T036 | non-pass propagation and denominator, SC-005 |
| FR-014 | T008, T016, T018, T024 | capability/lifetime/profile checks, SC-001/SC-002/SC-003 |
| FR-015 | T013, T015, T033-T034 | complete promised-feature and CI crosswalk, SC-006 |
| FR-016 | T033, T035-T036 | independent generic diff and local PR draft, SC-006 |
| FR-017 | T002-T003, T025-T028, T035 | exact source and notice mappings, SC-002/SC-006 |
| FR-018 | T001 and every checkpoint | work card, settings, bounded recovery, SC-007 |
| FR-019 | T032 | independent observed evaluation, SC-008 |
| FR-020 | T019, T025, T029, T034-T036 | primary, parity, pending CI and upstream states separate |
| FR-021 | T006, T012, T036 and each boundary change | enforced safe default, complete UNSAFE.md, SC-009 |
| FR-022 | T001, T036 and every checkpoint | tracked execution and evidence-qualified lessons, SC-007/SC-010 |
| FR-023 | T006, T025, T036 | fixed matched-scope size/dependency manifest and simplicity review, SC-011 |

## implementation acceptance, unrun

- [ ] SC-001. actual NGAPI on native Windows Vulkan.
- [ ] SC-002. declared Vulkan source parity with full source dispositions.
- [ ] SC-003. minimal direct Windows Vulkan slice and explicit boundaries. Linux portability reported separately.
- [ ] SC-004. ShaderToHuman fixtures and additional source behavior.
- [ ] SC-005. primary truthful records/negative controls, then supporting report/query agreement.
- [ ] SC-006. local RustGPU draft with promised-feature evidence and complete CI status.
- [ ] SC-007. resumable implementation state and evidence.
- [ ] SC-008. measured supporting human/agent usability.
- [ ] SC-009. enforced safe default and documented, reviewed necessary boundaries.
- [ ] SC-010. execution and lessons maintained through implementation.
- [ ] SC-011. matched-scope measurements and quality-preserving simplicity review.

M3 requires SC-001, the primary Windows/boundary portion of SC-003, the primary record portion of SC-005, and SC-006/SC-007/SC-009-SC-011 for the delivered slice. SC-002/SC-004/SC-008 and the remaining supporting portions are separate outcomes. successful full CI remains separately pending until T034 passes.

[tasks](../tasks.md) owns implementation completion. this file does not duplicate run status. [research](../research.md#open-engineering-questions) identifies unresolved experiments.
