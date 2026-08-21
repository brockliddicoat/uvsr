# Third-Party Notices

UVSR contains, adapts, links, downloads, or packages third-party material under
its own terms. The UVSR license does not replace those terms. In the source
distribution, `legal/documentation/README.md` records revisions, relationships, and
reuse boundaries; exact license texts are kept under `legal/licenses/` or
beside the relevant dependency or asset.

## Incorporated and Adapted Code

- NVIDIA Donut, NVRHI, ShaderMake, Dear ImGui, and Donut's transitive
  dependencies retain their upstream notices and licenses.
- UVSR's MiniEngine-derived TAA portions retain Microsoft's MIT notice.
- UVSR's Fast Approximate shader follows Google Filament's Apache-licensed
  adaptation lineage through G3D and NVIDIA FXAA; preserve the notices in
  `legal/documentation/google-filament-fxaa.md` from the source distribution.
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
educational use, Intel Sponza and Bistro require CC BY 4.0 attribution, and
Blender Classroom and Poly Haven assets use CC0. The current renderer bundles
unmodified Noto Sans under the SIL Open Font License 1.1 and installs the
complete license as `licenses/Noto-Sans-OFL-1.1.txt`. Geist remains a historical
source-only font asset. A transition build may also contain byte-identical Noto
Sans aliases under historical `CodexUI` paths and a temporary copy of the Geist
OFL under its historical notice filename for Launcher sequence 9 compatibility;
neither is a runtime Geist or Segoe font route. Current builds do not copy or
package Windows-installed Segoe UI fonts. The `Ogg (ProggyClean)` option uses
Tristan Grimmer's ProggyClean font already embedded by Dear ImGui under the MIT
License; new packages install its complete separate notice as
`licenses/ProggyClean-MIT.txt` without staging a duplicate font binary.

## Distribution Responsibility

Before redistributing a binary or media bundle, audit the exact build output
and include every applicable license and notice. The source distribution's
`legal/documentation/commercial-licensing.md` records known clearance gaps.
