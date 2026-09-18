# Bundled Scenes

the repository retains exactly two converted scenes as future graphics fixtures:

- [Bistro Interior](bistro_interior_retextured/README.md), a converted Wine
  variant associated with Amazon Lumberyard Bistro
- [San Miguel](san_miguel_retextured/README.md), the converted full San Miguel
  2.1 model

each scene keeps its source provenance, license, conversion report, loadable
descriptor, and runtime components together. the JSON reports are generated
evidence and remain byte for byte records. tool names inside them describe how
the retained bytes were made. they are not active tool dependencies.

the new Rust project has not yet defined a runtime asset manifest or asserted
that either scene loads. future code must consume the recorded files directly
and must not infer provenance or legal terms from directory contents.

the scene data is protected. a replacement or repaired asset must update its
adjacent provenance, add a report identifying the original and current bytes,
and update the affected inventory and legal record in the same change. retain
original conversion reports as historical evidence. Bistro's
[cleanup report](bistro_interior_retextured/scene-cleanup-report.json) records
its subsequent object removals without changing the original binary buffers.
