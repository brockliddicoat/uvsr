# NVIDIA Spatiotemporal Blue Noise

## Record

- Relationship: Independent Implementation and Design Influence
- Status: Current
- Confidence: Confirmed
- Upstream: [Spatiotemporal Blue Noise](https://arxiv.org/abs/2112.09629), [NVIDIA STBN](https://github.com/NVIDIA-RTX/STBN), and [Rendering Guidance](https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-1/)
- Revision: Published paper and public project, without an imported source revision
- Governing Terms: Publication and upstream-repository terms; neither code nor packaged textures are copied

## UVSR Relationship

UVSR's retired deterministic first-party generator followed the published
spatial and temporal optimization objective. Its checked-in generated volumes
remain current runtime assets; NVIDIA's generator and texture assets are not
bundled or translated.

## Evidence

- [Noise Design](../../docs/noise.md)
- [Asset Provenance](../../assets/noise/README.md)
- [Retained Asset Manifest](../../assets/noise/manifest.json)
- Commit `f892c17e33c007db69ca10f055bd7e59301b37d0`

## Commercial Clearance

Do not describe the generated volumes as NVIDIA assets. Any future reuse of the
upstream generator or textures needs a separate license review.
