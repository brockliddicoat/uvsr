# film LUTs

UVSR includes three original, Kodak-inspired looks generated specifically for
its AgX Base-space pipeline. They are simulations, not official Kodak LUTs.

the Tonemapper drawer selects these three bundled looks or None. the selection
is stored with the other tone controls in settings snapshots. LUT data is
validated before replacing the active GPU texture.

UVSR supports `LUT_3D_SIZE`, `DOMAIN_MIN`, and `DOMAIN_MAX`. One-dimensional
LUTs and combined 1D/3D files are intentionally unsupported.

See `NOTICE.md` for look references and trademark information.
