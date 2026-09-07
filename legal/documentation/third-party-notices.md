# third party notices

UVSR contains, adapts, links, downloads, or packages third party material under
its own terms. the UVSR license does not replace those terms. exact license
bodies remain under `legal/licenses`, beside the dependency or asset, or in the
verified fetched source. renderer packages copy this summary to
`bin/licenses/third-party-notices.md`.

## renderer code and dependencies

- NVIDIA Donut, pinned NVRHI, Dear ImGui, cgltf, GLFW, JsonCpp, stb, and TinyEXR
  retain their upstream terms. the package carries each applicable notice.
- Fast Approximate AA adapts Google Filament under Apache 2.0 and carries the
  indirect G3D and NVIDIA FXAA notices in
  `bin/licenses/Google-Filament-FXAA-Attribution.md`.
- AgX adapts Benjamin Wrensch's Minimal AgX implementation. preserve the
  missing Deadlines copyright and MIT notice. the earlier AgX data lineage is
  still a commercial clearance question.
- the retained fast acos expression carries Intel XeGTAO's MIT notice.
- DirectX-Headers and the app local Direct3D 12 Agility SDK runtime retain the
  exact Microsoft terms installed beside the engine.

## source and build tools

the recursive source tree contains NVIDIA ShaderMake commit
`5daebdbef45088fc2369d441391ecab0eba25e54` through Donut. UVSR's current build
does not invoke or link ShaderMake, and no ShaderMake binary, source, or license
is renderer package content. a source redistribution that includes the nested
checkout must keep `donut/ShaderMake/LICENSE.txt`.

the build fetches Microsoft DirectX Shader Compiler v1.9.2602 to produce DXIL.
the compiler is not packaged. redistribution of DXC itself requires its
University of Illinois license and upstream third party notices.

## scenes and environments

San Miguel 2.1 is limited to research and educational use with attribution.
the exact Bistro Wine GLB is separate from the supporting CC BY 4.0 archive, so
its chain of title remains unconfirmed. the adapted PBRT San Miguel camera also
has unconfirmed terms. Poly Haven publishes all six retained HDR files under
CC0. package copies preserve the scene licenses and the complete HDR source and
hash inventory.

## fonts

the renderer reads Windows-installed Segoe UI Semibold and Bold and does not
copy or package them. its Dear ImGui dependency retains embedded ProggyClean
from commit `45acd5e0e82f4c954432533ae9985ff0e1aad6d5`, with the full MIT notice
in `bin/licenses/ProggyClean-MIT.txt` (SHA-256
`8B802D79F256D29B45AD253323D212FA14CA952A20DCD227CFBCDB3D140BFE7C`).

the separate native `uvsr-launcher.exe` embeds Noto Sans Regular and Bold with
their OFL, and statically links zlib 1.3.2 with its complete license. its
**Notices** window displays those texts and the UVSR license. Windows platform
libraries are supplied by the operating system. the renderer package does not
duplicate launcher-only resources.

## distribution

audit the exact package and include every applicable license and notice before
redistribution. `legal/documentation/commercial-licensing.md` records known
clearance gaps.
