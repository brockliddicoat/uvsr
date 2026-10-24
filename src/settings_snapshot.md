# settings snapshot storage

[`DecodedSettings`](settings_snapshot_storage.h) owns a sorted entry array and
one allocation containing each name/value pair. comparisons use unsigned bytes,
then length. embedded zero bytes remain significant. insert, replacement and
clone check capacity and allocation before publishing; failure preserves the
previous entries and their borrowed views. successful mutation can invalidate
views. growth is bounded by representable allocation sizes, without a fixed
setting count or text limit. insertion shifts entries; sorting is appropriate for
the small settings catalog, not a general large-data index.

[`the decoder`](settings_snapshot_decoder.h) builds a private result before
replacing its output. parsing validates value escapes before rejecting duplicate
names. generic parsing accepts historical names; the application transaction
checks current membership after migration. canonical output orders names and
escapes values exactly. JSON output uses the shared JSON escaping implementation.
both use the existing measured text owner for one checked output allocation.

[`the fingerprint`](settings_snapshot.h) retains the authoritative hash and
registered schema rules. its result holds the 32-byte code and terminator
directly. views borrow that result; a view of a temporary must be consumed in
the same full expression.

[`native catalog lookup`](settings_snapshot_catalogs_win32.cpp) takes an explicit
executable-state or installed location. installed lookup returns the local UVSR
path first, then existing catalogs from sorted package directories. enumeration
attributes classify ordinary entries; reparse entries inspect their targets.
an unusable entry is skipped, while enumeration failure rejects the lookup.
narrow paths retain the CRT UTF-8 override and Windows ANSI/OEM conversion rules.

matching payloads remain owned after the file closes. each input line loses at
most one trailing CR and gains one LF. matching blocks remain in encounter order.
decode scans every selected catalog before reporting absence, collision or a
fingerprint mismatch, so a later read or unterminated-block error still wins.
the checked byte reader also rejects incomplete reads and close failures.

[`native persistence`](settings_snapshot_persistence_win32.cpp) appends the
portable catalog section to unchanged prior bytes. a complete matching section
wins before conflict rejection. formatting still precedes that check. missing
and empty files receive the versioned header; other read failures stop the write.
installed writes use the Windows known folder, preserving their distinct contract
from the decoder's environment-based lookup.

[`the atomic byte writer`](file_write.h) borrows chunks through its synchronous
call. it creates missing parents iteratively and claims an exclusive temporary
sibling. occupied temporary names remain intact. the destination is replaced
only after all chunks, flush and close succeed; failures report the original
error and any failed temporary cleanup. allocation and total file size are
checked before writing. concurrent catalog read/modify/write remains externally
serialized by the controller; atomic publication does not merge concurrent edits.

[`the controller`](uvsr_settings_commands.h) publishes canonical text and its
code together after refresh succeeds. failure retains the previous snapshot;
copying a code requires a successful fresh capture. transaction requests own
their values before decoder storage dies, including while selectors are pending.
a refresh failure after application reports that settings were applied and does
not claim rollback. diagnostics leave an unsuccessful capture absent.

errors carry a category, optional native and cleanup codes, and owned detail when
needed. allocation failure can use a static reason. emitters and input views
borrow live storage only for their synchronous call. the shared range guard
rejects null, excessive and wrapping ranges; it cannot establish whether an
otherwise plausible pointer names readable memory.
