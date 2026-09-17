# native executable paths

[`WindowsPath`](windows_executable_path.h) owns terminated UTF-16 storage. its
borrowed pointer remains valid until the owner moves, clears or dies. a failed
query or join preserves the previous pointer and contents and reports its stage
and native code. there is no shared scratch storage or throwing path adapter.

the native executable query allocates one 32,768-code-unit buffer, checks the
[Windows return length](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew),
and trims it in place. the buffer is allocated only when the query executes,
including explicit shader reloads. it does not enlarge every frame's stack.

parent extraction preserves the previous lexical rules for drive-relative paths,
UNC names, device prefixes, separator runs and Unicode code units. it does not
resolve dots, normalize separators or access the named file. `PathCchRemoveFileSpec`
was not equivalent to those rules. the tests compare against the previous MSVC
filesystem expression. empty input, missing parents, embedded nulls and invalid
address/count ranges return an error.

the relative join accepts terminated inputs and a nonempty suffix with no root
name or root directory. it preserves components, checks allocation arithmetic
and permits either input to borrow the destination's old storage. this is the
one-shot join needed by startup, not a general filesystem interface.

[`engine_startup.cpp`](engine_startup.cpp) uses native file-size and SHA checks for
the pinned runtime. it releases the size-query handle before hashing. default log
path failures use the existing warning-and-continue policy for unavailable logs;
the known-folder allocation is released on every return path.

the implementation and startup unit have compiler exception guards. the path
failure controls exist only in a separate test archive. remaining filesystem,
text and throwing ownership in settings, lifecycle and UI callers is separate
migration work; wrapping this result does not close those boundaries.
