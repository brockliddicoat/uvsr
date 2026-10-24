include_guard(GLOBAL)
include(FetchContent)

FetchContent_Declare(dxc
    URL
        "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/dxc_2026_02_20.zip"
    URL_HASH
        "SHA256=A1E89031421CF3C1FCA6627766AB3020CA4F962AC7E2CAA7FAB2B33A8436151E"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(dxc)
set(UVSR_DXC_FILE_COUNT 37)
set(UVSR_DXC_TREE_DIGEST
    "7b1ee9300f50e01c2347316b6dbc0640455231125986d36762743edb0fdbff24")
set(UVSR_DXC_EXECUTABLE "${dxc_SOURCE_DIR}/bin/x64/dxc.exe")
if (NOT EXISTS "${UVSR_DXC_EXECUTABLE}")
    message(FATAL_ERROR "The pinned x64 DXC executable is missing")
endif()
file(SHA256 "${UVSR_DXC_EXECUTABLE}" UVSR_DXC_SHA256)
if (NOT UVSR_DXC_SHA256 STREQUAL
        "b9cff94181248e080804b385da8964b6319fd07760721baa9053a891cf7a727f")
    message(FATAL_ERROR "The pinned x64 DXC executable failed SHA-256")
endif()
execute_process(
    COMMAND "${UVSR_DXC_EXECUTABLE}" --version
    RESULT_VARIABLE dxc_version_result
    OUTPUT_VARIABLE dxc_version_output
    ERROR_VARIABLE dxc_version_error)
if (NOT dxc_version_result EQUAL 0 OR
    NOT dxc_version_output MATCHES "1\\.9\\.2602")
    message(FATAL_ERROR
        "The pinned DXC version is not 1.9.2602: ${dxc_version_error}")
endif()

