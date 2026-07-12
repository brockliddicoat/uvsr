# UVSR agent guide

- Build the first-party renderer target with `cmake --build build --config Release --target uvsr`.
- The executable target is lowercase `uvsr`; the displayed project name is uppercase `UVSR`.
- Donut is a pinned dependency. Do not edit files under `donut/`.
- `Donut-Samples/` is local reference material, not UVSR source.
- Use DirectX 12 as the default backend. Keep Vulkan and DX11 disabled unless a task explicitly requires them.
