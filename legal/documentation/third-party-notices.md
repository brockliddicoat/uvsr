# Third-Party Notices

UVSR contains, adapts, links, downloads, or packages third-party material under
its own terms. The UVSR license does not replace those terms. In the source
distribution, `legal/documentation/README.md` records revisions, relationships, and
reuse boundaries; exact license texts are kept under `legal/licenses/` or
beside the relevant dependency or asset.
Renderer packages copy this complete record to
`bin/licenses/third-party-notices.md`.

## Incorporated and Adapted Code

- NVIDIA Donut keeps its upstream notice. Direct pinned NVRHI, Dear ImGui,
  cgltf, GLFW, JsonCpp, stb, and TinyEXR keep their own upstream terms.
- UVSR's MiniEngine-derived TAA portions retain Microsoft's MIT notice.
- UVSR's Fast Approximate shader follows Google Filament's Apache-licensed
  adaptation lineage through G3D and NVIDIA FXAA; preserve the notices in
  `legal/documentation/google-filament-fxaa.md` from the source distribution.
- UVSR's AgX shader adapts Benjamin Wrensch's MIT-licensed Minimal AgX
  Implementation; preserve the Missing Deadlines copyright and MIT terms.
- The retained fast-acos expression preserves Intel XeGTAO's MIT notice as
  `bin/licenses/Intel-XeGTAO-MIT.txt`.
- The retained generated Sobol table preserves Andrew Helmer's MIT notice as
  `bin/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt`.

## NVIDIA NRD and MathLib

UVSR incorporates NVIDIA Real Time Denoisers 4.17.3 from commit
`792eff196afdd350fd9c3f862119017ccb438a0e`. The retained implementation uses
REBLUR_DIFFUSE, RELAX_DIFFUSE, and SIGMA_SHADOW. It also incorporates NVIDIA
MathLib v11 from commit `974e1387ba936740c7cdc494792d2641bc127e86`.

This software contains source code provided by NVIDIA Corporation.

NRD is licensed under the NVIDIA RTX SDK License Agreement. Its exact fetched
license is installed as `bin/licenses/NRD-LICENSE.txt`. MathLib is MIT-licensed;
its exact fetched license is installed as
`bin/licenses/NVIDIA-MathLib-MIT.txt`. A
distributor must independently satisfy all application, distribution,
protective-term, notice, and attribution requirements. This notice does not
replace them.

## DirectX Shader Compiler

UVSR's build fetches Microsoft DirectX Shader Compiler v1.9.2602 directly and
uses it to produce DXIL. The production package does not include the compiler.
Anyone redistributing DXC itself must include its University of Illinois Open
Source License and upstream third-party notices.

## Launcher Runtime

The self-contained `uvsr-launcher.exe` incorporates Microsoft .NET and Windows
Forms runtime `10.0.11`. It embeds and displays the complete license and
third-party notices supplied by the pinned .NET SDK `10.0.400`; the renderer
package does not duplicate those launcher-only resources.

## Assets and Fonts

Bundled scenes, HDR environments, and fonts retain their adjacent licenses and
attribution. In particular, San Miguel's grant is limited to research and
educational use, Bistro requires CC BY 4.0 attribution, and Poly Haven assets
use CC0. The current renderer bundles unmodified Noto Sans under the SIL Open
Font License 1.1 and installs the complete license as
`bin/licenses/Noto-Sans-OFL-1.1.txt`. Current builds do not copy or package
Windows-installed Segoe UI fonts. The `Ogg (ProggyClean)` option uses
Tristan Grimmer's ProggyClean font already embedded by Dear ImGui under the MIT
License; new packages install its complete separate notice as
`bin/licenses/ProggyClean-MIT.txt` without staging a duplicate font binary.

## Distribution Responsibility

Before redistributing a binary or media bundle, audit the exact build output
and include every applicable license and notice. The source distribution's
`legal/documentation/commercial-licensing.md` records known clearance gaps.
