# pathing references

## record

- Relationship: Algorithm and behavior reference, no SDK dependency
- Status: Current
- Confidence: Pinned source inspected
- Upstream: NVIDIA RTXPT and AMD Capsaicin
- Revision: RTXPT `f08d1c739071e0faad0c7c274d861124c511abab`, Capsaicin `914b91596cd119eda85fbc1d3c7ee6ac391b1452`
- Governing Terms: [NVIDIA RTX SDKs License](https://github.com/NVIDIA-RTX/RTXPT/blob/f08d1c739071e0faad0c7c274d861124c511abab/LICENSE.txt), [Capsaicin MIT](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/914b91596cd119eda85fbc1d3c7ee6ac391b1452/LICENSE)

UVSR independently implements the radiance-cap equations described by RTXPT's
[firefly helpers](https://github.com/NVIDIA-RTX/RTXPT/blob/f08d1c739071e0faad0c7c274d861124c511abab/Rtxpt/Shaders/PathTracer/PathTracerHelpers.hlsli#L194-L218)
and follows the placement in
[path transport](https://github.com/NVIDIA-RTX/RTXPT/blob/f08d1c739071e0faad0c7c274d861124c511abab/Rtxpt/Shaders/PathTracer/PathTracer.hlsli)
and [next-event estimation](https://github.com/NVIDIA-RTX/RTXPT/blob/f08d1c739071e0faad0c7c274d861124c511abab/Rtxpt/Shaders/PathTracer/PathTracerNEE.hlsli#L245-L259).
UVSR uses full precision, exact acos, and an absolute threshold without RTXPT's
exposure normalization. the filter is biased. no NVIDIA helper source, SDK
binary, or header is vendored. NVIDIA's SDK license does not become a general
source-redistribution grant through this reference.

the bounce controls follow Capsaicin's
[ranges](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/914b91596cd119eda85fbc1d3c7ee6ac391b1452/src/core/src/render_techniques/reference_path_tracer/reference_path_tracer.cpp#L229-L238)
and [depth convention](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/914b91596cd119eda85fbc1d3c7ee6ac391b1452/src/core/src/ray_tracing/path_tracing.hlsl#L391-L460).
the camera hit has depth zero, the maximum bounds scattering events, and roulette
eligibility uses current depth greater than the minimum. UVSR retains its own
survival-probability bounds and transaction machinery. no Capsaicin source is
copied. the [user guide](../../docs/user-guide.md#path-tracing) owns product behavior.
