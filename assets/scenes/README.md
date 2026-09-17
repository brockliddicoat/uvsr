# bundled scenes

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

the scene data is protected byte for byte. a replacement or repaired asset must
update its adjacent provenance, generated reports, future runtime inventory,
and legal record in the same change.
