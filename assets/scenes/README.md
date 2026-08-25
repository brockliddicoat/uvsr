# Bundled Scenes

UVSR packages two scenes:

- [Bistro Interior](bistro_interior_retextured/README.md), the Wine variant of
  Amazon Lumberyard Bistro Interior.
- [San Miguel](san_miguel_retextured/README.md), the full San Miguel 2.1 model.

Each directory contains a loadable descriptor, adjacent attribution, source
provenance, and components. CMake uses an explicit allowlist so downloads and
working files cannot enter a package accidentally.

Conversion-tool filenames in the protected provenance JSON record how the
retained bytes were produced. Those strings are immutable history, not active
or recoverable tool dependencies; the named first-party scripts are retired.

Geometry uses ordinary glTF external buffers split only at buffer-view
boundaries to keep every tracked file below GitHub's 100,000,000-byte limit.
UVSR loads these resources directly without reconstruction. Conversion reports
record component sizes, hashes, and the few opaque-material fallbacks required
because UVSR has no blended or transmissive draw pass.

Both retained scene directories are protected byte-for-byte. Change an asset
only for a separately proven asset defect, then update its provenance and legal
record in the same change.
