# architecture and dependencies

## understand a framework before building through it

**observed.** UVSR began on NVIDIA Donut. over time, Donut types and behavior reached the application shell, device and window ownership, views, scene loading, shaders, render passes, resources, build logic, staging, and patches. the project owner did not have a complete mental model of those contracts. later work tried to translate ideas from other renderers while also replacing Donut, which made every boundary harder to reason about.

this is not evidence that Donut is defective. it is evidence that UVSR adopted more framework than the project could confidently own.

**inferred.** an agent could make a local change that compiled while relying on a hidden convention elsewhere. translation became name and type conversion instead of a deliberate mapping of coordinate spaces, resource states, descriptor lifetimes, shader layouts, temporal conventions, frame order, and failure behavior. adapters accumulated because no small project-owned center existed yet.

**recommended.** before adopting a framework, write down the part of its contract the product depends on. build one vertical slice that creates a device, compiles or loads one shader, uploads one resource, submits one operation, reads back a result, and retires every object correctly. if the team cannot explain that slice and diagnose its failures, it is too early to build the renderer above it.

## own the narrowest useful contracts

project-owned boundaries should describe behavior in the project's language and types. useful early contracts include:

- device and queue creation, capability discovery, and failure reporting,
- command recording, submission, completion, and resource retirement,
- resource states, views, layouts, descriptor ownership, and native escape hatches,
- shader source identity, stage, entry point, compiled bytes, reflection, and target metadata,
- scene import records that do not expose parser or graphics-library objects,
- current and previous camera, coordinate, sample, and color conventions,
- frame order and the validity interval of every borrowed value,
- test results that do not depend on one report UI.

use a concrete type when there is one real owner and one real consumer. introduce a shared abstraction only after two working paths demonstrate the same observable contract. speculative interfaces usually preserve imagined flexibility while hiding the decisions the implementation actually needs.

## translate behavior, not repository shapes

**observed.** UVSR drew ideas from several renderers, papers, and sample projects. copying a feature outline into an architecture with different scene data, scheduling, shader ABI, and history ownership often produced a large incomplete path. the integration burden was greater than the apparent algorithm.

**recommended.** treat external projects as inspirations and evidence. for each borrowed idea, record:

1. the user-visible or measurable behavior of interest,
2. the source revision and license,
3. the assumptions made by the original scene, frame, shader, and lifetime model,
4. the target project's equivalent owners,
5. the smallest experiment that can disprove the idea,
6. what will be implemented independently, translated with attribution, or left out.

AGFX and ShaderToHuman are inspirations for this repository. they do not define its implementation, compatibility promise, or test requirements. the project should learn from their explicit APIs, shader examples, goldens, structured results, and report presentation while owning a smaller contract appropriate to Rust and the selected native backends.

## isolate dependencies at transaction boundaries

**observed.** replacing a parser or scene owner became difficult when parser objects, framework math, graphics handles, and mutable application state crossed the same boundary. partial replacements created two owners for one concept.

**recommended.** finish third-party work inside a narrow transaction. parse and validate into private temporary state. convert into project-owned records. publish the complete candidate only after validation succeeds. keep compiler, parser, windowing, and native API objects out of public scene and renderer contracts unless the consumer truly needs them.

replacement should proceed owner by owner:

1. name the current producer, consumers, lifetime, and failure behavior.
2. add the project-owned contract and prove it against the current implementation.
3. move every consumer in one bounded slice.
4. remove the old adapter, build edge, patch, test path, and staged artifact.
5. verify that only one owner remains.

leaving both paths active is not a neutral intermediate state. it doubles states, failure modes, and evidence obligations.

## portability requires explicit differences

**observed.** a generic graphics layer can look portable while depending on one backend's descriptor model, synchronization, shader layout, or feature set. source similarity then hides unsupported cells.

**recommended.** keep common concepts only where behavior is truly common. preserve backend-specific capability queries, limits, synchronization, error details, and native handles where they matter. each language and backend cell remains incomplete until its actual compiler path, ABI, validation, execution, and result are proven.

start risky compiler and API work as bounded probes. a failed Rust-to-Metal route, descriptor-heap instruction, or pointer-layout experiment should change the plan early. it should not be hidden behind an interface that suggests support already exists.

## make lifetime and failure first-class

compilation says little about asynchronous ownership. every GPU resource needs a clear creation owner, last submission, completion proof, retirement owner, and device-loss or allocation-failure path. Rust lexical lifetime is useful but does not replace queue completion.

failures should publish no half-valid state. scene import, settings application, shader reload, package extraction, and resource replacement should validate a complete candidate before swapping it into the live system. if rollback cannot restore all owners, do not expose the operation as transactional.
