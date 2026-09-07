# recovery

Git is the recovery authority. historical reports, restore patches, screenshots,
and artifact hashes have been removed because they mixed old evidence with
current instructions. a recoverable commit proves source existed. it does not
prove that an old executable, package, performance result, or design remains
valid now.

the user requested a preserved local archive and
[postmortem for interface animations](postmortem/ui-animations.md). its exact
source identity, archive hash, and selective recovery limits are recorded there.

the retired panel backdrop pass, shader, and overlap geometry are preserved in
`work/astra-cutdown/solid-panel-recovery-c4dbcb31.zip`, SHA-256
`da91543c9f2456b54a4d5de5cd86099150c2b1890f6ffb260a22953f191d4611`.
all 16 files match its manifest. this local archive includes integration files
that must be inspected selectively before recovery.

## current lineage

| boundary | Exact Identity | Meaning |
| --- | --- | --- |
| pre stage two | `e29a41245dbd0e6fd7a819d2341646419ab76e72` | complete source before the stage two renderer and launcher cutdown |
| staged implementation parent | `9c42f44b3818c8a1d9904452ead3c8fb96349ece` | construction checkpoint before the published cutdown commit |
| stage two checkpoint | `c4dbcb31c2a241f5864cb08ae2f80551ef2615fb` | exact cutdown source on `codex/stage-two-cutdown-20260823` |
| integrated main | `4a07af5913c27b20eecd03d266578adf8c8fc336` | merge of `e29a412...` and `c4dbcb31...`, published on `origin/main` |

use the newest verified source as the base for new work. use these identities to
inspect or selectively recover an owner. do not reset a current tree to an old
checkpoint or copy a whole historical orchestration file over later work.

## durable decisions

- screen-space diffuse AO/GI and its drawer are removed. the
  [diffuse postmortem](postmortem/screen-space-diffuse.md) owns its method lineage,
  all recorded optimization candidates, failures, preimage archive, and future
  acceptance criteria. current lighting ownership stays in architecture.
- the [postmortem manifest](postmortem/publication-manifest.tsv) is checked during
  developer integrity, CI, and production packaging. a local preservation pass
  does not prove the reports are committed or uploaded. the publication check
  requires exact committed content and never changes Git or publishes.

- UVSR remains DirectX 12 only, with ImGui, FXAA, all retained AO/GI options, ray traced sky visibility, the flashlight, Bistro, San
  Miguel, and all retained HDR and STBN assets.
- ship only `uvsr-launcher.exe` and `uvsr-engine.exe`. installation and update
  are signed, hash bound, binary only transactions. the configured v2 launcher
  target is canonical. historical v1 alias files and publication paths remain
  required until an authorized migration proves released clients no longer
  need them.
- Donut is retained and attached. direct ImGui, NVRHI, DXC shader blobs,
  readback, targets, and focused passes complement the retained
  framework. they are not steps toward detachment.
- keep one conventional path tracer and one cumulative accumulation contract.
  add transport policy only when a small isolated candidate wins a controlled
  equal time comparison.
- raster lighting and visibility share one visible receiver per pixel.
- TAA and MSAA are deliberately removed. the [TAA](postmortem/taa.md) and
  [MSAA](postmortem/msaa.md) postmortems own the recovery evidence. the native
  launcher supersedes the managed implementation; its old state remains a
  supported migration fixture.
- the optional tonemapper drawer and LUT system remain sunset. any revival must
  also deliver and prove the intended local tonemapper. the fixed neutral AgX
  display transform remains.
- the rejected native reconstruction TAA, time of day sky orbit, flashlight
  camera centering, and visibility sample rotation experiments are not active
  designs. start any successor from a minimal current experiment, not restored
  historical code.

## source recovery index

| area | Exact Recovery Identity |
| --- | --- |
| july shader cutdown | complete snapshot `519306cbf7405939df73e5fc72aff48e79f9be4a`; first batch `b63cda9639dedf820f1251aa390b162befa22dd7` |
| august engine cleanup | pre cut `f7c0c87d8cba6880428fbc34400eb2882fb5182e`; integrated successor `b4dc24128e4f38effdeaf5a2dbc33cae107e9134` |
| retired Classroom and Sponza | Classroom `f7c0c87d8cba6880428fbc34400eb2882fb5182e`; Sponza origin `3ac53b382ee16101a04504811b7feb7a055f1773`, camera `a7e51b7d3a09e18cc4e5da085b511623a87cc0ac`; complete pre cut assets `e29a41245dbd0e6fd7a819d2341646419ab76e72` |
| launcher source build and old feeds | initial source build `a9004f51883cfaeb6a9cd375ed350d708322cc40`; compatibility `0c8074848985152ed83f83b4087aaf10013de590`; bridge `639fd74f9d180f3ba835d2cb0110c949e705b1a7`; complete pre cut behavior `e29a41245dbd0e6fd7a819d2341646419ab76e72` |
| historical launcher v2 publication | source `5762bb9f00dd1cd9f62aa19bc56e6f28215f30b4`; release tag `uvsr-launcher-v1.1.14`; signed feed recoverable from `e29a41245dbd0e6fd7a819d2341646419ab76e72`. it uses a retired artifact name and is not current feed or artifact proof. |
| tonemapper drawer and LUTs | pre sunset `5f43205ecfe00e31fd64af34cad0f031472a224c`; AgX introduction `c90274a01f21db1f4c23e3629d3004e9160fbeb6`; film look `177176a990fa9996a5da0bd8e638df32152ea9cd`; final drawer refinement `f1edae2440d4c07f1110f62d26f186b086e6dd95` |
| rejected time of day sky | lineage base `3087874cc89853eeecaa3c81e24d4ca49b6aa0a1`; rejected implementation `c58956327d5cca26f6a36d20daf4d5d2e03cf74f` |
| flashlight camera centering | feature lineage `f892c17e33c007db69ca10f055bd7e59301b37d0`; no exact rejected candidate exists; removal decision is recoverable at `e29a41245dbd0e6fd7a819d2341646419ab76e72` |
| visibility sample rotation | verified base `a7e51b7d3a09e18cc4e5da085b511623a87cc0ac`; documentation merge `869e2241a72faf59f11604b5199a96b3c0218788`; no rejected implementation commit exists; the former record is recoverable at `e29a41245dbd0e6fd7a819d2341646419ab76e72` |

recover complete behavior, not one file. trace settings, ABI, resources, shaders,
passes, invalidation, UI, commands, snapshots, build tasks, package entries,
tests, documentation, assets, and legal obligations before deciding what a
current successor needs.

## unresolved risks

- the current rewrite is not proven until a clean developer gate, exact
  production package, launcher round trip, and provenance bound local DXR smoke
  produce current launcher and engine SHA-256 identities. old artifact hashes
  are not substitutes.
- Donut scene, VFS, view, app, render, GLFW, shader, patch, and nested dependency
  consumers remain as supported boundaries. verify their pins, patches, and
  runtime consumers rather than treating them as removal work.
- single-sample lighting requires current Bistro and
  San Miguel motion, resize, scene transition, and debug-layer evidence.
- the path tracer still needs current reset, convergence, and frame time proof.
- Bistro source lineage and opaque liquid fallback remain review risks. preserve
  its bytes and provenance until those questions are resolved.
- feed files in source do not publish an endpoint. signing, release artifacts,
  live endpoint changes, and retirement of v1 aliases require separate authority
  and released client evidence. the canonical v2 launcher feed file and endpoint
  are absent at this checkpoint; the retained v1 alias files are not live.

## recovery method

inspect with `git show <commit>:<path>` or compare named paths with `git diff`.
create a new current implementation from the smallest required behavior. do not
cherry pick a retired feature blindly, restore deleted plans as instructions, or
reintroduce Python, source build installation, new Donut coupling, aliases, or
fallbacks merely because history contains them.

every restored behavior needs current acceptance criteria, one owner, an exit
condition, and the full affected verification gate. commit, push, release,
signing, and publication remain separate user authorized actions.
