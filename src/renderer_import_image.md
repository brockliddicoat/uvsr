# decoded image ownership

[`ImportDecodedImage`](renderer_import_image.h) owns one decoded image. encoded
bytes, path and MIME are borrowed only during `Decode`; an external file must
already be resident. a successful call replaces the previous owner after all
fallible work. failures preserve its metadata, bytes, layouts and view addresses.
move transfers ownership. reset and destruction release a flat layout array and
pixel allocation, with no recursive image graph. calls on one owner are exclusive.

the storage limit covers the state, layout array and decoded allocation. DDS keeps
the entire encoded file, including permitted trailing bytes, because its layout
offsets refer to that owned copy. other formats own decoded pixels with offset
zero. each subresource describes all depth slices; `depthPitch` describes one
slice. array-major, then mip-major order matches the retained uploader. callers
must finish upload borrows before replacing or destroying the owner.

codec selection preserves exact lowercase/uppercase extensions and MIME values.
mixed-case extensions fall through to stb. the [path owner](renderer_import_path.h)
handles Windows drive/UNC roots, leading dots and alternate streams without an
owning filesystem object. MIME takes the same precedence as the retained loader.

## formats and compatibility

the pinned stb implementation retains one, two and four channels, expanding RGB
to RGBA. HDR uses float32; other supported inputs use eight-bit output. source bit
depth reports the retained original channel count. only four-channel eight-bit
output receives the requested sRGB interpretation. alpha metadata stays unknown.

the [shared codec target](../cmake/DirectThirdParty.cmake) compiles stb once with
C++ exceptions disabled. the [GIF patch](../overrides/stb-gif-iteration.patch)
replaces recursive dictionary expansion with reverse collection and forward pixel
emission. each decoder owns 8192 bytes of fixed scratch, matching its dictionary
capacity. collection checks every index and its length before emitting that code.
the existing interlace, transparency and image-end behavior remains in the pixel
loop. invalid links fail decoding; the image owner preserves its previous state.

the [GIF fixture](../tests/stb_gif_tests.cpp) checks independently known palette
grids with interlace and transparency, malformed-code rollback, and a 4091-level
prefix chain producing 8370186 known pixels. these same expectations passed the
original decoder before the patch. this proves selected GIF behavior, not a
general codec memory or stack budget.

the [checked DDS reader](renderer_import_dds.cpp) maps the retained typed formats,
legacy masks and FourCC values into plain image formats. it preserves authored
mips, cube faces, volumes, alpha modes, compressed block pitches and the old
legacy uncompressed sRGB inversion. backend formats require an explicit mapping;
the plain enum is not numerically interchangeable with NVRHI. duplicate retained
DDS codes resolve to ordinary R16/R32 formats before the unreachable depth aliases.

checked dimensions, array products, mip counts and payload bounds precede layout
allocation. complete 1D arrays copy every declared slice; the old loader omitted
the count and consumed only the first. invalid 3D array counts, zero extents,
non-square cubes, overflow and repeated terminal mip levels reject. retained
permissive flag precedence remains: a 1D height without the height flag becomes
one; cube bits on other dimensions are ignored; legacy volume wins over cube.
these cases describe file interpretation, not valid GPU resource capabilities.
the concrete upload owner still validates backend support and creation results.

## EXR boundary

one [independent implementation](renderer_tinyexr.cpp) compiles the existing
TinyEXR pin. the temporary native TextureCache and new importer link that same
implementation. no codec source or dependency revision is replaced.

the lower-level EXR API permits dimension, channel, chunk and storage checks before
pixel decoding. single-part scanline and one-level tiled images retain exact
uppercase R/G/B selection, optional alpha, ignored extra channels and the
one-channel replication into all four output channels. HALF converts to FLOAT.
UINT retains the old convenience API's float-bit interpretation, copied without
aliasing through an incompatible pointer. output remains RGBA32_FLOAT with
128 original bits per pixel. deep/multipart images, missing RGB channels,
unimplemented subsampling and unsupported compression/tile levels reject.

chunk offsets, coordinates, coverage and raw payload lengths are checked before
calling the codec. zero scanline offsets retain sequential reconstruction. the
uncompressed codec ignores its length argument, so the caller checks the complete
raw chunk. dimensions and decoded chunks must also fit the codec's integer
operations. tiled edge copies use actual tile extents and the codec's padded row
stride. first-party traversal and teardown use loops.

this pin writes image cleanup counts only on success. the wrapper seeds channel
and tile counts before decoding so internal failure cleanup releases partial
planes. it then clears the dangling image fields left by that cleanup. successful
images, headers and every error message each have one owner and release.

`maxTemporaryPixelBytes` bounds EXR channel planes, their pointer arrays and the
tile table. the temporary coverage bitset is freed before pixel allocation and
is smaller than those planned planes. codec header, compressed workspace and
standard-library allocations are separate. TinyEXR still has unchecked internal
allocation sites and private vendor containers; general vendor exhaustion remains
fatal with C++ exceptions disabled. first-party allocation injection cannot prove
that boundary recoverable. stb's own allocations and failures also remain codec
operations rather than first-party allocation-count evidence.

the [image fixture](../tests/import_image_tests.cpp) uses actual native decoding
and exact consumed pixel/layout comparisons, explicit tiled edge values, file
extension comparisons, malformed/truncated inputs, capacity boundaries, moves and
first-party allocation failures with retry. native DDS 1D-array correction is a
separate assertion. these CPU checks do not establish GPU upload, rendering,
scene publication or complete codec allocation coverage.

the private [NVRHI resource owner](renderer_scene_resources_nvrhi.md) consumes
these decoded bytes and layouts during bounded upload steps.
