foreach(required_variable
        UVSR_EXECUTABLE
        UVSR_D3D12_CORE
        UVSR_D3D12_RUNTIME_DIRECTORY
        UVSR_EXPECTED_SDK_VERSION
        UVSR_LINKER)
    if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "The Direct3D runtime verifier is missing ${required_variable}")
    endif()
endforeach()

if (NOT EXISTS "${UVSR_EXECUTABLE}" OR NOT EXISTS "${UVSR_D3D12_CORE}")
    message(FATAL_ERROR
        "The Direct3D runtime verifier could not find the executable or D3D12Core.dll")
endif()

execute_process(
    COMMAND "${UVSR_LINKER}" /dump /exports "${UVSR_EXECUTABLE}"
    RESULT_VARIABLE dump_result
    OUTPUT_VARIABLE dump_output
    ERROR_VARIABLE dump_error)
if (NOT dump_result EQUAL 0)
    message(FATAL_ERROR
        "The UVSR export table could not be inspected: ${dump_error}")
endif()

foreach(required_export D3D12SDKVersion D3D12SDKPath)
    if (NOT dump_output MATCHES "(^|[\r\n])[^\r\n]*${required_export}([\r\n]|$)")
        message(FATAL_ERROR
            "uvsr.exe does not export the required ${required_export} symbol")
    endif()
endforeach()

file(MAKE_DIRECTORY "${UVSR_D3D12_RUNTIME_DIRECTORY}")
set(staged_core "${UVSR_D3D12_RUNTIME_DIRECTORY}/D3D12Core.dll")
if (NOT EXISTS "${staged_core}")
    message(FATAL_ERROR "The app-local D3D12Core.dll was not staged")
endif()
file(SHA256 "${UVSR_D3D12_CORE}" source_core_sha256)
file(SHA256 "${staged_core}" staged_core_sha256)
if (NOT source_core_sha256 STREQUAL staged_core_sha256)
    message(FATAL_ERROR
        "The staged D3D12Core.dll does not match the configured Agility SDK")
endif()

file(WRITE "${UVSR_D3D12_RUNTIME_DIRECTORY}/uvsr-runtime-contract.txt"
    "schemaVersion=1\n"
    "sdkVersion=${UVSR_EXPECTED_SDK_VERSION}\n"
    "sdkPath=.\\D3D12\\\n"
    "coreSha256=${staged_core_sha256}\n")
