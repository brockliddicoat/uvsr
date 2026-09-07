# zlib

- relationship: static native launcher dependency
- version: 1.3.2
- upstream: [zlib](https://zlib.net/)
- archive: `https://zlib.net/fossils/zlib-1.3.2.tar.gz`
- archive SHA-256: `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
- terms: the complete upstream `LICENSE` embedded in the launcher

`launcher/CMakeLists.txt` fetches the exact archive and links `zlibstatic`.
`launcher/native/archive.cpp` uses raw DEFLATE inflation after validating ZIP
paths, sizes, headers, and supported encodings. the archive verifier and its
resource limits are first-party code. the renderer does not gain this dependency.

resource 105 in `launcher/native/launcher.rc` embeds the complete license
from the verified fetched source. the **Notices** window displays it. preserve
that text with every binary containing zlib; this record does not replace it.
