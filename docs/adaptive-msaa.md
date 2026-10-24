# Adaptive MSAA and Compact Visibility

UVSR keeps its original full per-sample deferred MSAA renderer as the
authoritative Reference and default. Two optional implementations use the same
sample loading, exact surface identity, conservative grouping, shading, and
resolve contract:

- **Reference** preserves the established per-sample G-buffer and lighting
  path without adaptive sharing.
- **Adaptive G-Buffer** adds a multisampled `RG32_UINT` ownership attachment,
  groups samples only after final depth testing, and shares work only when the
  coherence proof permits it.
- **Compact Visibility** rasterizes an exact key plus depth for supported
  opaque triangles, reconstructs the current PBR surface through UVSR-owned
  scene tables, and fails closed to Reference before submission when the frame
  cannot be reconstructed exactly.

All three modes support requested 2x, 4x, 8x, and 16x sample counts when the
complete DirectX 12 resource topology is supported. An adaptive mode is never
substituted for an unsupported request, and neither adaptive mode becomes the
default without matched image and full-frame performance acceptance.

## Surface Identity

The logical identity is:

```text
SurfaceKey = {
    stableInstanceHandle,
    geometryIndex,
    geometryLocalPrimitiveIndex
}
```

Raster visibility stores `uint2(geometryInstanceHandle,
geometryLocalPrimitiveIndex)`. The first word losslessly resolves the stable
instance and geometry and carries generation/topology validation, so a reused
slot cannot silently name an old surface. Invalid, exhausted, stale, or
overflowed encodings select Reference.

`SurfaceHit = { SurfaceKey, barycentrics, frontFace }` is the common hit
representation. Raster output and future DXR
`{ InstanceIndex, GeometryIndex, PrimitiveIndex }` mapping must produce this
same logical identity. Material, depth, coverage, normals, lighting,
barycentrics, and cache coordinates are deliberately excluded from
`SurfaceKey`. Topology, LOD, deformation, and material revisions remain
explicit invalidation metadata. Picking, surface-cache cards, radiance-cache
cells, and future integrations derive their own addresses from `SurfaceHit`
instead of creating competing primitive identities.

## Grouping and Shading Safety

`SameSurface` means exact key equality. `CanShareShading` is a separate,
conservative depth and world-position coherence test. Equal ownership does not
make final radiance constant: nearby lights, shadows, screen-space visibility,
view response, normal mapping, and sharp specular lobes can vary across the
same triangle.

Each covered sample descriptor is loaded once. Valid samples are grouped by
exact final key with the lowest sample index as the deterministic
representative. Unsafe groups are subdivided into deterministic singletons.
Invariant G-buffer decode and material/BRDF preparation may be redistributed,
but sample-dependent lighting remains per sample unless bit-exact inputs prove
complete-radiance equality. Every contribution is weighted by its covered
sample count divided by the total sample count, and uncovered samples retain
their independently resolved background.

## Adaptive G-Buffer Flow

The adaptive G-buffer raster pass writes the ordinary multisampled PBR
attachments plus one `RG32_UINT` ownership target through UVSR-owned vertex,
pixel, and push-constant paths. Raw `SV_PrimitiveID` is never treated as a
global primitive identifier because it restarts for each draw. `SV_Coverage`
may help classify work, but the post-depth ownership key is the only final
surface identity.

The lighting shader compares exact keys, applies the coherence split, shades
the deterministic representatives, and preserves Reference-compatible
background weighting. Diagnostics-only permutations can evaluate every member
independently, reject a whole shared group when its HDR error is non-finite or
exceeds the frozen budget, and publish the resulting singleton fallbacks.

## Compact Visibility Flow

Compact visibility is a real alternative raster topology, not an extra key
beside the full multisampled G-buffer. Eligible frames store multisampled key
and depth. A generation-validated snapshot resolves keys into instances,
geometry, index and vertex buffers, current and previous transforms, material
records, and textures. The reconstruction shader implements raster
interpolation, front-face handling, explicit `SampleGrad` texture access,
normal mapping, authored material ambient occlusion, world position, and
motion.

The production compact subset is deliberately narrower than that shader
capability. It admits undeformed, single-sided, opaque indexed triangles using
the standard metallic-roughness model. Untextured materials are eligible
without a target certificate. A matching qualification certificate can also
admit base/diffuse (`SRGBA8_UNORM`, `BC1_UNORM_SRGB`), metal/roughness
(`RG8_UNORM`, `BC5_UNORM`), emissive (`SRGBA8_UNORM`, `BC7_UNORM_SRGB`), and
occlusion (`R8_UNORM`, `BC4_UNORM`) textures at 2x, 4x, and 8x. Normal,
opacity/alpha-test, transmission, subsurface, specular-gloss, double-sided,
deformed, and textured 16x draws retain the exact local full-G-buffer payload.
Those are expected conservative fallbacks, not compact reconstruction coverage.

### Texture Qualification

The fixed `TQC1` certificate binds the exact renderer and GPU-test executables,
build manifest, complete tested shader set, loaded Direct3D runtime modules,
adapter and driver, sampler, feature/format digest, compiled semantic schema,
2x/4x/8x evidence masks, and all four integer viewport-origin parities. Runtime
admission additionally requires a process-monotonic nonzero texture-content
revision and qualification token. The immutable scene snapshot, each material
fingerprint, per-view preparation, and final submission all retain and
revalidate that exact pair.

Only an explicit `--write-compact-texture-certificate <path>` invocation of the
full D3D12 GPU micro-test can issue the certificate. Ordinary CTest and UVSR
runtime execution are read-only consumers. To activate the exact build being
tested, write only its canonical sibling path. Alternate leaf names, existing
reparse-point leaves, and other physical destinations are rejected before GPU
work begins; equivalent directory spellings are normalized to that sibling:

```text
build\bin\uvsr_adaptive_msaa_gpu_micro_tests.exe --write-compact-texture-certificate "build\bin\uvsr_compact_texture_qualification.tqc"
```

The issuer copies observed evidence rather than manufacturing complete masks.
Guide evidence is intersected across every required row for each sample count,
each successful viewport-origin row contributes only its own parity bit, and
alpha, feature/format, finite-output, reference-signal, per-channel error, and
validation-clean bits are recorded only at their proving checks. The existing
certificate validator rejects any incomplete 2x, 4x, or 8x record before the
atomic write.

Missing, malformed, stale, reused,
overflowed, or target-mismatched evidence leaves textured draws on their exact
fallback without runtime dual rendering or readback. UVSR hashes the same
artifact set at the final material publication barrier and again after staged
render-pass shader loading. A shader reload revokes the lease; a textured
compact scene then remains on Reference until a scene reload establishes a new
publication barrier, snapshot, and lease. This intentionally conservative
reload policy prevents a snapshot certified for old shader bytes from being
reused.

Unsupported material or deformation records retain their exact surface key and
use that local full-G-buffer payload. Structural geometry failures, stale
mappings, generation or topology changes, resource mismatches, and unrecoverable
capacity failures select the complete Reference topology before the command
list opens. Prepared raster, guide, visibility, directional shadow, and
compact-shading submissions are revalidated immediately before recording so a
recoverable setup failure can retry Reference in the same render invocation.

Compact invariant and BRDF preparation has two shader-identical scheduling
topologies for matched measurement. Configure a dedicated build tree with
`-DUVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY=inline` for direct
origin-lane preparation, or `separated` for depth-classified simple and complex
wave work lists. Both use the same decoder, grouping, fallback, lighting, and
resolve. The separated permutation has a 128-record fixed capacity and converts
any overflow to the same originating lane's inline invariant preparation, so
covered work is never dropped; diagnostics still report the overflow reason.
Work-list diagnostics are advertised only by that permutation. The build
default remains inline until matched position-1 full-frame measurements select
a visually accepted winner.

## Diagnostics and Benchmark Isolation

The **Adaptive MSAA Diagnostics** Debug section is disabled by default. It
selects implementation and sample count, split-screen or error-heatmap
comparison, camera/frame freeze, and a selected-pixel inspector. Views cover
per-sample ownership, final groups and representatives, fallback reasons,
compact reconstruction, and final adaptive-versus-Reference lighting error.

The task-owned validation scene exposes one deterministic animation control.
`debug.adaptive-msaa.validation-animation-phase` selects an exact normalized
`MotionTaaSweep` phase before scene-graph refresh. The ordinary animation toggle
is a fixed-phase inspection control, not a general animation player. Disabling
it restores phase zero.

An ordinary phase command followed by an ordinary screenshot occurs across two
renderer frames, after one-frame object motion has settled. Use the atomic
two-frame action below for motion, TAA, and frame-to-frame evidence. It starts
from the disabled, phase-zero state, resets temporal and previous-view history,
captures A, then captures B on the immediately following renderer frame. Frame
B therefore consumes frame A's previous transform and, when TAA is enabled,
its temporal history:

```text
/run debug.adaptive-msaa.capture-validation-frame 0.25 "outputs/adaptive-msaa/reference-motion-a.bmp" 0.50 "outputs/adaptive-msaa/reference-motion-b.bmp"
```

Keep diagnostics and camera/frame freeze on, but leave validation animation off
before queuing the action. UVSR still refreshes the scene graph. Only the owned
transaction advances the previous view, TAA phase, and diagnostic seed, using
the frozen seed for A and the next seed for B. All scene, implementation,
sample-count, size, AA, topology, and revision identity is revalidated before
both frames; a selected-path raster, guide, lighting, diagnostic-presentation,
AA, or BMP failure and any identity change abort the pair. Keep comparison and
debug view off when collecting TAA beauty frames because diagnostic
visualizations intentionally bypass presentation TAA.

After B, UVSR automatically disables the validation animation and restores
phase zero. Wait through its two settlement refreshes before changing the
implementation or queuing the matched adaptive pair; retain the same frozen
frame seed and replay the exact same action and phases. The benchmark gate
remains closed while the animation, capture transaction, restore refreshes,
temporal reset, or any diagnostic resource is active.

The inspector reports every sample's key, depth, barycentrics, group,
representative, weight, fallback/error reason, and radiance. Counters cover the
group histogram, lighting evaluations saved, fallbacks, stale/invalid keys,
mapping failures, work occupancy/overflow, and pass timing. GPU-resident
current and temporal error history is reset on topology, implementation,
sample-count, scene, or size changes.

Diagnostics, validation-animation control, dual rendering, asynchronous
readback, capture, and debug views are benchmark-disqualifying. Accepted
performance measurements keep all of them at their defaults, which prevents
diagnostic allocation, scene mutation, dispatch, readback, or dual rendering
from entering the measured topology.

## Reference Image Validation

The checked-in recipe
`assets/scenes/adaptive_msaa_validation/image_diff_matrix.json` defines the
fixed 1920 x 1080 camera, two animation phases, 15 adversarial cases, four
sample counts, and all three implementations. The unattended runner derives
the adjacent validation scene, keeps one frozen stochastic seed for the whole
suite, and executes 180 two-frame capture jobs: 60 Reference jobs and 120
adaptive jobs. It rejects implementation or sample-count downgrades, stale
destinations, changed scene or renderer identity, incomplete BMP output, and
any missing exact diagnostics-token receipt.

Run it only with a new path or an existing empty directory:

```powershell
build\bin\uvsr.exe `
  -adaptive-msaa-validation-plan `
    "assets\scenes\adaptive_msaa_validation\image_diff_matrix.json" `
  -adaptive-msaa-validation-output `
    "outputs\adaptive-msaa-captures"
```

The process exits with failure unless all 360 BMP files and all 360 matching
receipts are durable and the schema-2
`adaptive_msaa_validation_manifest.json` has been published atomically. Each
receipt binds the exact build/source identity, staged runtime shader bundle,
recipe, scene, camera, settings, frozen seed, revisions, capture phase,
implementation, sample count, diagnostic token, grouping and fallback work,
error-budget rejections, and compact reconstruction result. Reference receipts
must prove Reference execution; candidate receipts must prove their requested
adaptive implementation.

The schema-2 manifest reuses each Reference capture for its two candidate
comparisons and contains exactly 120 runs. Evaluate it with:

```powershell
tools\adaptive_msaa_image_diff.cmd `
  --manifest `
    outputs\adaptive-msaa-captures\adaptive_msaa_validation_manifest.json `
  --report outputs\adaptive-msaa-report\report.json `
  --heatmap-directory outputs\adaptive-msaa-report\heatmaps
```

The evaluator rejects any missing or extra schema field, identity mismatch,
invalid work proof, non-finite pixel, dimension mismatch, insufficient
lighting work, or exceeded spatial/temporal limit. Its self-test builds and
mutates the complete 120-run matrix, including all receipt and error-budget
failure classes. Configure a separate acceptance tree with
`UVSR_ADAPTIVE_MSAA_VALIDATION_MANIFEST` pointing to the completed manifest to
register the same gate as a CTest; an incomplete capture set never registers as
accepted evidence.

Display-domain spatial error is the maximum absolute sRGB code-value channel
difference. Temporal error compares candidate and Reference frame deltas. The
frozen display gate requires p99 at or below 0.5 code value and maximum at or
below 2 code values for every frame and pooled run. Scene-linear diagnostics
use relative HDR error with p99 at or below `1/1024` and maximum at or below
`1/128`. A group exceeding the HDR budget is rejected before its diagnostic
result is accepted, and its reason, rejected-group count, and rejected-sample
count are preserved in the receipt and aggregate report.

## Artifact Identity

The generated `uvsr_adaptive_msaa_build.manifest` records the exact requested
base commit, the complete first-party tracked and untracked source-delta hash,
toolchain identity, compact preparation topology, and a sorted SHA-256 entry
for every staged runtime DXIL file. Shader staging republishes this manifest
only after byte-for-byte synchronization, and the runtime bundle test rehashes
both every file and the aggregate list. Captures therefore cannot authenticate
an executable against stale or independently replaced shader bytes.

## Source Provenance

The primary implementation source is The Forge `Visibility_Buffer` sample at
commit `a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b`, licensed Apache-2.0.
Supplementary identity and grouping design comes from AMD Capsaicin commit
`914b91596cd119eda85fbc1d3c7ee6ac391b1452`, Burns and Hunt's visibility-buffer
supplement, and Epic's Lumen technical documentation. Exact file-level
provenance, hashes, adaptation boundaries, and required notices are recorded in
[`third_party/adaptive_msaa_sources/ATTRIBUTION.md`](../third_party/adaptive_msaa_sources/ATTRIBUTION.md).

No file under `donut/` is modified by the implementation.
