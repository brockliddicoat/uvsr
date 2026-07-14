# UVSR agent guide

## Product and scope

- Treat UVSR as a focused production renderer, not a general Donut sample.
- Follow near-YAGNI principles: keep only behavior the product currently needs.
  Avoid speculative features, compatibility layers, unused controls, dormant
  shaders, and abstractions with no active caller.
- The executable and engineering slug are lowercase `uvsr`; the displayed
  product name is uppercase `UVSR`.
- Donut is a pinned dependency. Do not edit files under `donut/`; implement UVSR
  behavior in first-party code or narrowly scoped build-time overrides.
- `Donut-Samples/` is local reference material, never UVSR source.
- DirectX 12 is the product backend. Keep Vulkan and DX11 disabled unless a task
  explicitly requires them.
- Bistro scene sources are licensed local assets. Do not commit files from
  `assets/scenes/nvidia_bistro/`, generated `build/` content, or `work/`
  artifacts.

## Coming Soon coordination

- Treat the complete **Coming Soon** section in `README.md` as the shared
  coordination ledger for every unmerged project or feature and every project
  or feature an agent is currently working on, including experiments.
- At the start of every project or feature task, before writing implementation
  code, inspect open pull requests, unmerged branches, and visible agent
  worktrees; then read the entire Coming Soon section into working memory.
  Reconcile every missing item discovered, not only the current task.
- If the task has no entry, add or update one as the first repository edit. Name
  its status and branch when one exists, intended scope, affected subsystems,
  integration dependencies, and any interaction with NRA-RTAA or another listed
  effort.
- Cite the exact Coming Soon entry in the task's implementation plan and record
  the overlap assessment there. Do not begin implementation until the entry and
  plan reference both exist.
- For work already in flight when this policy is encountered, complete the same
  reconciliation before the next implementation edit or push.
- Re-read Coming Soon after pulling or rebasing and before editing a subsystem
  named by another entry. Preserve other agents' entries and work; coordinate
  overlapping scope instead of silently replacing it.
- Keep the same entry current when its scope, branch, or status changes. If the
  user has authorized publication, commit and push the coordination update
  before creating or pushing implementation commits so simultaneous agents can
  consume it. This workflow never grants push or PR permission by itself.
- When work merges, remove its Coming Soon entry and update the renderer baseline
  or relevant design documentation with durable shipped behavior. Remove
  abandoned work explicitly.

## Change discipline

- Inspect `git status` and the relevant diff before editing. Preserve unrelated
  user changes and never discard them to simplify a task.
- Create a named checkpoint commit before broad deletion, pruning, or a risky
  refactor. Follow it with focused commits after verification.
- Format every commit subject in lowercase as `<type>: <description>`, where
  `<type>` is a one-word summary such as `fix`, `feat`, `docs`, or `checkpoint`.
- Before pushing an agent branch, review every agent-authored commit that is not
  in its target branch and rewrite any nonconforming subject to the required
  lowercase format. Do not rewrite already-published or merged history without
  the user's explicit authorization.
- When the user explicitly authorizes a GitHub update, use a lowercase
  `<type>: <description>` pull-request title. If creating a merge commit, set
  its subject to `merge: pull request #<number> <description>` in lowercase
  (for example, `merge: pull request #5 update renderer documentation`) instead
  of accepting GitHub's generated `Merge pull request #...` subject.
- For cleanup work, trace references before deleting. Remove an obsolete feature
  end to end: source and declarations, CPU/UI state, GPU resources, shader entry
  points and files, `shaders.cfg`, CMake/runtime packaging, assets, tests, and
  stale documentation.
- Search again after removal so dead includes, constants, comments, build rules,
  and fallback paths do not remain.
- Ask before deleting any code, shader, or asset whose runtime, rendering, UI, or
  packaging dependency is uncertain.
- Do not push, publish, or open a pull request unless the user explicitly asks.

## UI and rendering safeguards

- Treat the existing UI layout, wording, order, widths, defaults, and alignment
  as user-designed product behavior. Change them only when the task explicitly
  calls for a UI change.
- Every new or changed UVSR-owned interactive control must have a concise hover
  tooltip that describes its effect.
- Cleanup and infrastructure work must not silently change authored colors, AgX
  output, screen-space indirect lighting, scene lighting, or other visible
  rendering behavior.
- Keep the forward and deferred PBR paths on the shared UVSR material/lighting
  contract. The PBR comparison implementation is retained for possible future
  experiments, but its production UI control is hidden. If restored, the
  toggle must retain the same camera, scene, tonemapper, sky, and lights.
- Preserve the display pipeline order: scene-linear HDR radiance, camera white
  balance, exposure in EV, AgX inset transform, logarithmic encoding, contrast
  tone scale, Base-space grade/LUT, output gamut conversion, display transfer,
  then dithering.

## Documentation

- Keep the root file named `README.md`.
- Use title capitalization for Markdown headings and prominent bold text in
  `README.md`, including the document title, section or project headings, and
  bold list-item headings. Leave inline UI labels, control names, and product
  names at their established casing.
- Update `README.md` whenever a change affects user-visible behavior, defaults,
  controls, required assets, build/test/run steps, or intentional omissions.
  Remove stale claims instead of preserving historical behavior.
- Update `docs/pbr-foundation.md` when the material contract, G-buffer packing,
  BSDF equations, debug views, limitations, or extension path changes.
- Prefer extensive, high-signal code comments that preserve implementation
  reasoning: design intent, invariants, assumptions, tradeoffs, edge cases,
  failure modes, and non-obvious cross-component interactions. Optimize comments
  for human debuggability and future LLM inference, while avoiding comments that
  merely restate straightforward syntax.

## Verification

- Build the first-party renderer with
  `cmake --build build --config Release --target uvsr`.
- For PBR or rendering changes, also build `uvsr_pbr_tests` and run
  `ctest --test-dir build -C Release --output-on-failure`.
- Launch through `tools/launch_uvsr.ps1 -Experiment "<oneword>"` and
  smoke-test after runtime, rendering, shader, scene-loading, or UI changes.
  Never launch the bare executable during agent work: every renderer taskbar
  title must identify the experiment with an ASCII alphanumeric one-word
  description; the renderer appends its source commit and local `HHmm` launch
  time. Exercise the relevant
  forward/deferred and screen-space AO/GI combinations when they could be
  affected.
- Documentation-only changes require link and diff validation, not a renderer
  rebuild.
- Run `git diff --check` before committing.
