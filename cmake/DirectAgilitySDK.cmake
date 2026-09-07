set(UVSR_D3D12_AGILITY_PACKAGE_VERSION "1.619.5")
set(UVSR_D3D12_AGILITY_SDK_VERSION 619)
set(UVSR_D3D12_AGILITY_ARCHIVE_NAME
    "microsoft.direct3d.d3d12.1.619.5.nupkg")
set(UVSR_D3D12_AGILITY_DOWNLOAD_DIRECTORY
    "${CMAKE_BINARY_DIR}/downloads/uvsr-d3d12-agility-${UVSR_D3D12_AGILITY_PACKAGE_VERSION}")
set(UVSR_D3D12_AGILITY_ARCHIVE
    "${UVSR_D3D12_AGILITY_DOWNLOAD_DIRECTORY}/archive.tar")
FetchContent_Declare(uvsr_d3d12_agility
    URL
        "https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/1.619.5/microsoft.direct3d.d3d12.1.619.5.nupkg"
    URL_HASH
        "SHA256=0E9BCF32AAC9A79343EDE9B21E4864950EE54577E3D8E19BFCDF002BB4E9BFD6"
    DOWNLOAD_DIR "${UVSR_D3D12_AGILITY_DOWNLOAD_DIRECTORY}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(uvsr_d3d12_agility)

uvsr_verify_dependency_file("${UVSR_D3D12_AGILITY_ARCHIVE}"
    "0e9bcf32aac9a79343ede9b21e4864950ee54577e3d8e19bfcdf002bb4e9bfd6" 35271083)

set(UVSR_D3D12_AGILITY_ROOT "${uvsr_d3d12_agility_SOURCE_DIR}")
set(UVSR_D3D12_AGILITY_INCLUDE_DIR
    "${UVSR_D3D12_AGILITY_ROOT}/build/native/include")
set(UVSR_D3D12_AGILITY_CORE_DLL
    "${UVSR_D3D12_AGILITY_ROOT}/build/native/bin/x64/D3D12Core.dll")
set(UVSR_D3D12_AGILITY_LAYERS_DLL
    "${UVSR_D3D12_AGILITY_ROOT}/build/native/bin/x64/d3d12SDKLayers.dll")

file(READ "${UVSR_D3D12_AGILITY_ROOT}/Microsoft.Direct3D.D3D12.nuspec"
    agility_nuspec)
if (NOT agility_nuspec MATCHES
        "<id>Microsoft\\.Direct3D\\.D3D12</id>" OR
    NOT agility_nuspec MATCHES "<version>1\\.619\\.5</version>")
    message(FATAL_ERROR "The Direct3D Agility SDK package metadata changed")
endif()
file(READ "${UVSR_D3D12_AGILITY_INCLUDE_DIR}/d3d12.idl" agility_idl)
string(REGEX MATCH "const UINT D3D12_SDK_VERSION = ([0-9]+)"
    agility_version_match "${agility_idl}")
if (NOT agility_version_match OR
    NOT CMAKE_MATCH_1 EQUAL UVSR_D3D12_AGILITY_SDK_VERSION)
    message(FATAL_ERROR "The Direct3D Agility SDK runtime version changed")
endif()

uvsr_verify_dependency_file(
    "${UVSR_D3D12_AGILITY_ROOT}/build/native/bin/x64/D3D12Core.dll"
    "eddf4cff4eda8162624b88694ad2adf4b09bc5aee6339191f39adf8ae48b41e7" 5027640)
uvsr_verify_dependency_file(
    "${UVSR_D3D12_AGILITY_ROOT}/build/native/bin/x64/d3d12SDKLayers.dll"
    "a78bca22ebe6c8ccdd6efff630d798b27447f8922bb889de2f50e0dd1ab10f85" 4965688)
uvsr_verify_dependency_file(
    "${UVSR_D3D12_AGILITY_ROOT}/LICENSE.txt"
    "5239850894610071566f7ecee0b751fde43c862032d92b99d7d0f596b3433ebd" 13147)
uvsr_verify_dependency_file(
    "${UVSR_D3D12_AGILITY_ROOT}/LICENSE-CODE.txt"
    "903df5512f7d02609fed0c780a9b704f5a3eeb6e4d84ebe42a29845c81899a3c" 1093)
uvsr_verify_dependency_file(
    "${UVSR_D3D12_AGILITY_ROOT}/distributable files.txt"
    "18be111795af241547e17596a010cb7606aa8670d9a26096425ba46a0f76ef9e" 93)
set(UVSR_AGILITY_NOTICE_RECORDS
    "${UVSR_D3D12_AGILITY_ROOT}/LICENSE.txt|5239850894610071566f7ecee0b751fde43c862032d92b99d7d0f596b3433ebd"
    "${UVSR_D3D12_AGILITY_ROOT}/LICENSE-CODE.txt|903df5512f7d02609fed0c780a9b704f5a3eeb6e4d84ebe42a29845c81899a3c"
    "${UVSR_D3D12_AGILITY_ROOT}/distributable files.txt|18be111795af241547e17596a010cb7606aa8670d9a26096425ba46a0f76ef9e")

message(STATUS "Direct3D Agility SDK ${UVSR_D3D12_AGILITY_PACKAGE_VERSION}: exact")
