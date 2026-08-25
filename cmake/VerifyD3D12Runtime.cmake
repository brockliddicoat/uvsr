foreach(required_variable
        UVSR_EXECUTABLE
        UVSR_D3D12_CORE
        UVSR_D3D12_LAYERS
        UVSR_D3D12_RUNTIME_DIRECTORY
        UVSR_EXPECTED_SDK_VERSION
        UVSR_EXPECT_DEBUG_LAYERS
        UVSR_EXPORT_VALIDATOR)
    if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "The Direct3D runtime verifier is missing ${required_variable}")
    endif()
endforeach()

if (NOT UVSR_EXPECTED_SDK_VERSION STREQUAL "619")
    message(FATAL_ERROR
        "UVSR requires the Direct3D Agility SDK export value 619")
endif()
if (NOT EXISTS "${UVSR_EXPORT_VALIDATOR}" OR
    IS_DIRECTORY "${UVSR_EXPORT_VALIDATOR}")
    message(FATAL_ERROR
        "The Direct3D export-value validator is unavailable")
endif()
if (NOT EXISTS "${UVSR_EXECUTABLE}" OR IS_DIRECTORY "${UVSR_EXECUTABLE}" OR
    NOT EXISTS "${UVSR_D3D12_CORE}" OR IS_DIRECTORY "${UVSR_D3D12_CORE}" OR
    NOT EXISTS "${UVSR_D3D12_LAYERS}" OR IS_DIRECTORY "${UVSR_D3D12_LAYERS}")
    message(FATAL_ERROR
        "The Direct3D runtime verifier could not find the executable or D3D12Core.dll")
endif()

execute_process(
    COMMAND "${UVSR_EXPORT_VALIDATOR}" --check
        "${UVSR_EXECUTABLE}" "619" ".\\D3D12\\"
    RESULT_VARIABLE export_result
    OUTPUT_VARIABLE export_output
    ERROR_VARIABLE export_error)
if (NOT export_result EQUAL 0)
    message(FATAL_ERROR
        "The UVSR Direct3D export values are invalid: "
        "${export_output}${export_error}")
endif()

if (NOT IS_DIRECTORY "${UVSR_D3D12_RUNTIME_DIRECTORY}")
    message(FATAL_ERROR "The app-local Direct3D runtime directory was not staged")
endif()
set(staged_core "${UVSR_D3D12_RUNTIME_DIRECTORY}/D3D12Core.dll")
if (NOT EXISTS "${staged_core}")
    message(FATAL_ERROR "The app-local D3D12Core.dll was not staged")
endif()
file(SHA256 "${UVSR_D3D12_CORE}" source_core_sha256)
file(SHA256 "${staged_core}" staged_core_sha256)
set(expected_core_sha256
    "eddf4cff4eda8162624b88694ad2adf4b09bc5aee6339191f39adf8ae48b41e7")
if (NOT source_core_sha256 STREQUAL expected_core_sha256)
    message(FATAL_ERROR
        "The configured D3D12Core.dll is not the immutable 1.619.5 member")
endif()
if (NOT staged_core_sha256 STREQUAL expected_core_sha256)
    message(FATAL_ERROR
        "The staged D3D12Core.dll is not the immutable 1.619.5 member")
endif()

file(SIZE "${UVSR_D3D12_LAYERS}" source_layers_size)
file(SHA256 "${UVSR_D3D12_LAYERS}" source_layers_sha256)
set(expected_layers_sha256
    "a78bca22ebe6c8ccdd6efff630d798b27447f8922bb889de2f50e0dd1ab10f85")
if (NOT source_layers_size EQUAL 4965688 OR
    NOT source_layers_sha256 STREQUAL expected_layers_sha256)
    message(FATAL_ERROR
        "The configured D3D12SDKLayers.dll is not the immutable 1.619.5 member")
endif()
set(staged_layers
    "${UVSR_D3D12_RUNTIME_DIRECTORY}/D3D12SDKLayers.dll")
if (UVSR_EXPECT_DEBUG_LAYERS)
    if (NOT EXISTS "${staged_layers}" OR IS_DIRECTORY "${staged_layers}")
        message(FATAL_ERROR "The developer D3D12SDKLayers.dll was not staged")
    endif()
    file(SIZE "${staged_layers}" staged_layers_size)
    file(SHA256 "${staged_layers}" staged_layers_sha256)
    if (NOT staged_layers_size EQUAL 4965688 OR
        NOT staged_layers_sha256 STREQUAL expected_layers_sha256)
        message(FATAL_ERROR
            "The staged D3D12SDKLayers.dll is not the immutable 1.619.5 member")
    endif()
elseif(EXISTS "${staged_layers}")
    message(FATAL_ERROR
        "Production runtime staging contains the forbidden D3D12 debug layer")
endif()

file(WRITE "${UVSR_D3D12_RUNTIME_DIRECTORY}/uvsr-runtime-contract.txt"
    "schemaVersion=1\n"
    "sdkVersion=${UVSR_EXPECTED_SDK_VERSION}\n"
    "sdkPath=.\\D3D12\\\n"
    "coreSha256=${staged_core_sha256}\n")
