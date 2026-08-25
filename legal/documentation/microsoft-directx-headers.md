# Microsoft DirectX-Headers

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [Microsoft DirectX-Headers](https://github.com/microsoft/DirectX-Headers)
- Revision: `ee479f0bd5f7b884f202bcf0c3f076cc050dd256`
- Governing Terms: [MIT License](https://github.com/microsoft/DirectX-Headers/blob/ee479f0bd5f7b884f202bcf0c3f076cc050dd256/LICENSE)

## UVSR Relationship

UVSR pins NVRHI's DirectX-Headers fetch to the immutable commit above. The
resulting external `DirectX-Headers` target supplies NVRHI's DirectX 12 backend;
UVSR does not adapt the fetched source. The 1,093-byte MIT file has SHA-256
`903df5512f7d02609fed0c780a9b704f5a3eeb6e4d84ebe42a29845c81899a3c`
and packages as `bin/licenses/Microsoft-DirectX-Headers-MIT.txt`.

## Evidence

- [Direct Build Configuration](../../cmake/DirectDonut.cmake)
- [Package Mapping](../../CMakeLists.txt)

## Commercial Clearance

Source or binary distributions containing material from the headers must retain
Microsoft's MIT notice. Preview API compatibility is separate from licensing.
