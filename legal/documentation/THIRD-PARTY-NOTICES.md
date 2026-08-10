# Third-Party Notices

UVSR contains, adapts, links, downloads, or packages third-party material under
its own terms. The UVSR license does not replace those terms. In the source
distribution, `legal/sources/README.md` records revisions, relationships, and
reuse boundaries; exact license texts are kept under `legal/licenses/` or
beside the relevant dependency or asset.

## Incorporated and Adapted Code

- NVIDIA Donut, NVRHI, ShaderMake, Dear ImGui, and Donut's transitive
  dependencies retain their upstream notices and licenses.
- Intel CMAA2 remains under Apache 2.0. Its incorporated Microsoft portion and
  UVSR's MiniEngine-derived TAA portions retain Microsoft's MIT notice.
- UVSR's Fast Approximate shader follows Google Filament's Apache-licensed
  adaptation lineage through G3D and NVIDIA FXAA; preserve the notices in
  `legal/sources/google-filament-fxaa.md` from the source distribution.
- UVSR's AgX shader adapts Benjamin Wrensch's MIT-licensed Minimal AgX
  Implementation; preserve the Missing Deadlines copyright and MIT terms.

## NVIDIA NRD

An optional build can incorporate NVIDIA Real Time Denoisers 4.17.3 from commit
`792eff196afdd350fd9c3f862119017ccb438a0e`.

This software contains source code provided by NVIDIA Corporation.

NRD is licensed under the NVIDIA RTX SDK License Agreement. When enabled, the
exact fetched license is installed as `licenses/NRD-LICENSE.txt`. A distributor
must independently satisfy all application, distribution, protective-term,
notice, and attribution requirements. This notice does not replace them.

## Assets and Fonts

Bundled scenes, HDR environments, and fonts retain their adjacent licenses and
attribution. In particular, San Miguel's grant is limited to research and
educational use, Intel Sponza and Bistro require CC BY 4.0 attribution, Blender
Classroom and Poly Haven assets use CC0, and Geist uses the SIL Open Font
License 1.1. Segoe UI font redistribution is not granted by this repository.

## Distribution Responsibility

Before redistributing a binary or media bundle, audit the exact build output
and include every applicable license and notice. The source distribution's
`legal/documentation/COMMERCIAL-LICENSING.md` records known clearance gaps.
