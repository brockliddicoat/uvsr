# Microsoft DirectX-Headers

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [Microsoft DirectX-Headers](https://github.com/microsoft/DirectX-Headers)
- Revision: `v1.717.0-preview`, selected by pinned NVRHI
- Governing Terms: [MIT License](https://github.com/microsoft/DirectX-Headers/blob/v1.717.0-preview/LICENSE)

## UVSR Relationship

NVRHI fetches these platform headers for its DirectX 12 backend when equivalent
targets are not supplied externally. UVSR consumes their interfaces indirectly
through NVRHI and does not adapt their source as first-party code.

## Evidence

- [Root Build Configuration](../../CMakeLists.txt)
- [NVRHI Build Configuration](../../donut/nvrhi/CMakeLists.txt) at revision `8e8c36e37558acec333204619b95d9d2fcdc4a79`

## Commercial Clearance

Source or binary distributions containing material from the headers must retain
Microsoft's MIT notice. Preview API compatibility is separate from licensing.
