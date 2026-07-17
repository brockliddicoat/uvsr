# Tonemapper Drawer and LUTs v1 Strategic Sunset

The Tonemapper drawer and LUT system were not failures. They worked, had a
coherent AgX-based design, and produced useful grading options. UVSR is
sunsetting them because they arrived before the scene-lighting foundation was
settled, and their optional surface area was consuming attention out of
proportion to how often it was used.

This is a sequencing decision. The implementation is preserved exactly so it
can return with minimal reconstruction when lighting is more fully developed.
That future revival must also implement the bilateral-grid local tonemapper; the
old global drawer must not return by itself.

## Outcome

UVSR now retains only the required fixed neutral AgX display transform, output
gamut conversion, display transfer, and dithering. The active renderer removes:

- the **Tonemapper** drawer;
- Base, Punchy, Golden, Mix, and Custom preset state;
- Exposure, Contrast, Saturation, Warmth, Tint, Slope, and Power controls;
- `.cube` LUT parsing, discovery, GPU upload, selection, and binding;
- the bundled 2383 Print, Portra 400, and Ektar 100 simulations;
- LUT runtime packaging; and
- the premature bilateral-grid local-tone-mapping roadmap entry.

The explicit forward tonemapperless comparison mode remains available. It is a
renderer comparison path, not the production display contract.

## Why This Was Not a Failure

Nothing in the sunset verdict says the feature was technically unsound,
visually worthless, or incapable of returning. The drawer and film looks were
optional creative controls built on a valid display pipeline. The problem was
their timing.

Lighting changes alter the scene-linear signal that every exposure, contrast,
grade, and LUT decision interprets. While that upstream signal is still moving,
grading controls create extra combinations to inspect, make it easier to mask a
lighting problem with a display adjustment, and invite repeated tuning of a
downstream feature whose target is not stable. A small amount of user-facing
choice also carried loader, GPU-resource, shader-binding, asset, packaging, UI,
tooltip, reset, documentation, and validation obligations.

Those costs were real even though the code worked. Because the controls were
rarely used, keeping them active slowed development of the renderer's more
important lighting foundation. Removing them now is near-YAGNI discipline, not
a negative product evaluation.

## What Remains

Tone mapping itself is not optional for the normal renderer path. UVSR produces
scene-linear HDR radiance, so it still needs a stable transform into the display
target. The retained shader is the former neutral Base behavior reduced to its
essential AgX inset, logarithmic encoding, default contrast curve, gamut
conversion, display transfer, and dither.

The sunset intentionally removes the dormant parameter and LUT machinery
instead of merely hiding the drawer. This keeps the production contract small
and makes the future restoration boundary explicit.

## Archive Identity

- Canonical pre-sunset checkpoint:
  `5f43205ecfe00e31fd64af34cad0f031472a224c`.
- Original AgX pipeline introduction:
  `c90274a01f21db1f4c23e3629d3004e9160fbeb6`.
- Film-look and refined drawer introduction:
  `177176a990fa9996a5da0bd8e638df32152ea9cd`.
- Last major drawer layout and tooltip refinement:
  `f1edae2440d4c07f1110f62d26f186b086e6dd95`.
- Restoration patch:
  [`archive/tonemapper-drawer-v1.restore.patch`](archive/tonemapper-drawer-v1.restore.patch).
- Machine-readable restoration manifest:
  [`archive/tonemapper-drawer-v1.restore.json`](archive/tonemapper-drawer-v1.restore.json).
- Restoration patch SHA-256:
  `e5e85b69d5cf8a5f6ab5cf3933f63b98d765e113df68d4606b4f72f774e5e93a`.

The patch is oriented from the post-sunset production tree back to the exact
pre-sunset feature. It contains the focused CMake, C++, HLSL, LUT documentation,
and LUT asset changes. It deliberately excludes this postmortem, the agent
trigger, and general README prose so revival preserves its own historical
record and reconciles documentation with the future renderer instead of
blindly restoring old roadmap text.

The base checkpoint is a second recovery path. Do not replace a future
`src/uvsr.cpp` wholesale with that version: doing so would erase unrelated work
added after the sunset. Use the focused patch or extract its individual hunks.

## Paired Revival Contract

The phrases **“bring back the tonemapper drawer”**, **“restore the tonemapper
drawer”**, and close equivalents invoke one coupled local implementation task:

1. restore the archived global Tonemapper drawer, grading presets, `.cube`
   loader, film-look assets, and AgX grade/LUT shader path;
2. implement the bilateral-grid local tonemapper in the same candidate; and
3. verify and present the combined result as one feature.

Restoring only the old global drawer is explicitly incomplete. Applying the
archive patch is the beginning of revival, not its done condition.

The local tonemapper contract preserved from the retired roadmap is:

- a first-party DirectX 12 GPU bilateral-grid analysis pass over the final
  scene-linear display source after visibility composition and before AgX;
- one scalar local-EV correction applied inside the AgX display pass;
- `src/local_tone_mapping*` ownership for the local analysis implementation;
- explicit AgX binding, display-eligibility, reference-test, resource-lifetime,
  and UI coverage;
- a zero-correction path that matches the revived global pipeline; and
- no motion-reprojected local-exposure history unless a future request
  explicitly expands the design.

The revived drawer's Exposure remains the global EV adjustment. The local pass
provides a spatial EV correction that is combined with global exposure before
the AgX inset transform. Presets and LUTs remain later global grading stages in
their archived order.

This contract records the required pairing, not permission to publish it. The
future request authorizes scoped local implementation and verification under
the repository's conversational rules. Commit, push, pull request, merge,
release, and deployment authority remain separate.

## Revival Gate

Revival is appropriate when the user explicitly asks for it. The best technical
timing is after UVSR has a stable, accepted lighting baseline with saved scenes,
cameras, and reference captures. The combined candidate should then be judged
against that baseline rather than tuned against a moving target.

The local tonemapper does not need to be designed now. The future implementer
must inspect the newest Canonical verified renderer and adapt the archived
bindings to its current display source, resource model, and shader interfaces.
The global drawer and LUT code should otherwise return as close to the archived
implementation as current interfaces safely allow.

## Restoration Procedure

1. Read this postmortem and
   `docs/postmortem/archive/tonemapper-drawer-v1.restore.json`.
2. Start from the newest Canonical verified checkpoint in a new task branch and
   isolated worktree.
3. Verify the archive checksum.
4. Check the focused restoration against the current tree:

   ```powershell
   git apply --check docs/postmortem/archive/tonemapper-drawer-v1.restore.patch
   ```

5. If the check succeeds, apply it. If intervening code has changed the touched
   files, use a clean index with `git apply --3way` and review every conflict
   semantically. Never overwrite a future `uvsr.cpp` with the old whole file.
6. Implement the bilateral-grid local tonemapper contract above before calling
   the revival complete or presenting the drawer as restored.
7. Reconcile the combined CPU/HLSL bindings, runtime resources, shader
   packaging, reset behavior, UI order, tooltips, README, PBR documentation,
   and tests.
8. Build the renderer and PBR tests, run CTest, launch the exact candidate with
   a valid experiment label, and compare saved lighting references with global
   controls neutral and local correction zero.
9. Validate representative global grades, bundled and external `.cube` LUTs,
   local adaptation, resize/reload/reset paths, forward/deferred eligibility,
   and the tonemapperless comparison mode.
10. Obtain fresh product acceptance for the exact combined artifact and
    settings before any integration.

## Required Revival Evidence

- Neutral global settings plus zero local correction reproduce the accepted
  fixed-display baseline within an explicitly reviewed tolerance.
- The bilateral grid has a demonstrated local-adaptation benefit on at least
  one accepted high-dynamic-range lighting case without obvious halos,
  pumping, banding, hue shifts, or unstable exposure.
- Global exposure, every archived preset, no-LUT output, every bundled LUT, and
  at least one valid external `.cube` file are exercised.
- Invalid or unsupported LUT input fails safely without corrupting the active
  display binding.
- Resource creation, resize, shader reload, Reset Settings, and renderer restart
  cannot retain stale global or local tone state.
- Full-frame and relevant pass timings are recorded after image quality passes.
- The combined feature receives direct user acceptance; successful restoration
  of old code alone is insufficient.

## Final Decision

Sunset the optional drawer, grading presets, and LUT system now. Preserve the
exact code and assets because they remain valuable and were not rejected.
Reintroduce them only as a paired revival that also delivers the bilateral-grid
local tonemapper, after the lighting foundation is ready to support meaningful
grading decisions.
