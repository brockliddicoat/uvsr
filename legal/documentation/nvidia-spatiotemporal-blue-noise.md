# NVIDIA spatiotemporal blue noise

## record

- relationship: independent implementation and publication influence
- status: current retained first party output; generator retired
- confidence: confirmed
- upstream: [Spatiotemporal Blue Noise](https://arxiv.org/abs/2112.09629), [NVIDIA STBN](https://github.com/NVIDIA-RTX/STBN), and [rendering guidance](https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-1/)
- revision: published paper and public project; no imported source revision
- terms: publication and upstream repository terms; no NVIDIA code or texture
  copied

## UVSR relationship

UVSR's deterministic first party generator followed the published spatial and
temporal optimization objective. the retained generated volumes are current
runtime assets. NVIDIA's generator source and texture assets were not bundled,
translated, or used as the retained bytes.

## evidence

- [user guide](../../docs/user-guide.md)
- [validation contract](../../docs/validation.md)
- [asset provenance and exact hashes](../../assets/noise/README.md)
- [generated manifest](../../assets/noise/manifest.json)
- generation incorporation commit
  `f892c17e33c007db69ca10f055bd7e59301b37d0`

do not describe the retained volumes as NVIDIA assets. any later reuse of the
upstream generator or textures needs a separate license review.
