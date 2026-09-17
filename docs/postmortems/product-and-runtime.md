# product and runtime

## keep evidence layers separate

**observed.** UVSR repeatedly had strong evidence at one layer and weak evidence at the layer users experienced. source checks and unit tests passed while visual features failed. developer builds worked while exact package proof stayed open. a valid shader or generated instruction did not prove resource states, lifetimes, or the final image.

**recommended.** report these independently:

| layer | minimum evidence |
| --- | --- |
| source | exact revision, diff, dependencies, and generated inputs |
| compile | toolchain, options, successful targets, shader identity, and reflection |
| runtime | exact executable, device, driver, action, validation output, and result |
| visual | fixed fixture, expected relation, numeric or structural oracle, and artifacts |
| temporal | named motion sequence and per-frame validity or error |
| performance | controlled comparison, full path timings, clocks, and uncertainty |
| package | manifest, extracted inventory, hashes, legal files, and package execution |
| release | package evidence plus authority, remote identity, signatures, and publication record |

passing one row does not fill the rows below it.

## bind every claim to identity

**observed.** UVSR work crossed dirty worktrees, copied build trees, staged packages, recovery branches, and long-lived executables. a correct result from the wrong source was easy to mistake for current evidence.

**recommended.** runtime records include source commit and dirty-state disposition, dependency pins, build configuration, settings schema and values, executable path and SHA-256, adapter and driver, scene and camera, viewport, action sequence, and package identity. timestamps are useful for navigation, not identity.

reuse a build tree only when source, tools, generator, architecture, configuration, options, and dependencies match. preserve an old working artifact until the candidate passes. do not let a successful rebuild silently replace the binary whose behavior is under review.

## model presentation controls precisely

**observed.** display synchronization work exposed a recurring naming problem. frame-rate limiting, Vertical Sync, tearing permission, presentation mode, and physical variable refresh are related but distinct. collapsing them into one label created misleading controls and evidence.

**recommended.** each visible control should map to one owned behavior and state what the platform can actually prove. an API flag that permits tearing does not prove a display is using variable refresh. a limiter controls submission cadence, not scanout synchronization. tests should inspect both the configured state and the resulting presentation path where the platform exposes it.

use similarly precise language for exposure, accumulation, antialiasing, resolution, and quality. names should describe observed behavior rather than an implementation guess.

## design settings as transactions

settings need one typed authority for names, domains, defaults, persistence, UI binding, serialization, and migrations. parse and validate a complete candidate before changing live state. apply related selectors and values in a defined order. on failure, either restore every changed owner or publish nothing.

persist only durable user intent. diagnostic choices and temporary experiment controls should remain session-local or absent. accept an old name only through an explicit bounded migration. schema identity, diagnostics, packaged defaults, and runtime readback must agree.

## make packaging a product test

**observed.** launcher source builds, multiple executable identities, copied dependencies, and recovery scripts expanded UVSR's release surface. a developer build did not prove the user could install and run the intended artifact.

**recommended.** stage only required executables, runtime libraries, compiled shaders, assets, settings, notices, and licenses. validate the staged directory and the extracted archive against a manifest. reject missing, extra, modified, linked, debug, source, test, toolchain, interpreter, symbol, and benchmark content. execute the exact extracted artifact through the supported launch path.

legal and provenance records are runtime inputs for distribution decisions. retaining a scene does not prove a right to redistribute it, nor that the renderer can load it. record those results separately.

## preserve failure behavior

resource allocation, shader creation, readback, scene publication, settings apply, package extraction, hash verification, and process startup all need deliberate failure paths. fault injection is valuable because successful cases cannot prove rollback, cleanup, or stale-state avoidance.

when a prerequisite disappears, disable the consumer, invalidate affected history, and require fresh output after recovery. do not reuse an old environment, scene, lighting value, or timing sample as if it belonged to the current frame.

## keep UI responsive and causally simple

background loading and long GPU work should publish clear pending, ready, and failed states. commands should take effect independently of decorative animation. diagnostics should report the active state and the reason a control is unavailable. if an interaction requires several hidden timing assumptions, it will be difficult for both a person and an agent to verify.
