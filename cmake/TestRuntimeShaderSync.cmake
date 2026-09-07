if (NOT DEFINED UVSR_SYNC_SCRIPT OR NOT DEFINED UVSR_TEST_DIRECTORY)
    message(FATAL_ERROR
        "Runtime shader sync test requires script and test-directory paths")
endif()

file(TO_CMAKE_PATH "${UVSR_TEST_DIRECTORY}" test_directory)
if (NOT test_directory MATCHES "runtime-shader-sync-test")
    message(FATAL_ERROR "Refusing an unexpected runtime shader test path")
endif()

file(REMOVE_RECURSE "${test_directory}")
set(framework_root "${test_directory}/compiled/framework")
set(app_root "${test_directory}/compiled/uvsr")
set(runtime_parent "${test_directory}/bin")
set(runtime_root "${runtime_parent}/shaders")
file(MAKE_DIRECTORY
    "${framework_root}/dxil"
    "${app_root}/dxil"
    "${runtime_root}/stale/empty")
file(WRITE "${framework_root}/dxil/first.bin" "first shader\n")
file(WRITE "${app_root}/dxil/second.bin" "second shader\n")
set(inventory "${test_directory}/runtime-shader-inventory.def")
file(WRITE "${inventory}"
    "bin/shaders/framework/dxil/first.bin\n"
    "bin/shaders/uvsr/dxil/second.bin\n")
foreach(stale_name IN ITEMS
    stale.txt catalog.json compiler.dll probe.exe stale.bin)
    file(WRITE "${runtime_root}/stale/${stale_name}" "retired\n")
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_RUNTIME_SHADER_INVENTORY=${inventory}"
        "-DUVSR_RUNTIME_SHADER_EXPECTED_COUNT=2"
        "-DUVSR_FRAMEWORK_SHADER_SOURCE_ROOT=${framework_root}"
        "-DUVSR_APP_SHADER_SOURCE_ROOT=${app_root}"
        "-DUVSR_RUNTIME_SHADER_ALLOWED_PARENT=${runtime_parent}"
        "-DUVSR_RUNTIME_SHADER_ROOT=${runtime_root}"
        -P "${UVSR_SYNC_SCRIPT}"
    RESULT_VARIABLE sync_result
    OUTPUT_VARIABLE sync_output
    ERROR_VARIABLE sync_error)
if (NOT sync_result EQUAL 0)
    message(FATAL_ERROR
        "Runtime shader synchronization failed: ${sync_output}${sync_error}")
endif()

foreach(expected_file IN ITEMS
    "${runtime_root}/framework/dxil/first.bin"
    "${runtime_root}/uvsr/dxil/second.bin")
    if (NOT EXISTS "${expected_file}")
        message(FATAL_ERROR "Expected staged shader is missing: ${expected_file}")
    endif()
endforeach()
if (EXISTS "${runtime_root}/stale")
    message(FATAL_ERROR
        "Runtime shader synchronization retained an unexpected entry")
endif()

file(GLOB_RECURSE staged_files
    LIST_DIRECTORIES false
    RELATIVE "${runtime_root}"
    "${runtime_root}/*")
list(SORT staged_files)
set(expected_files
    "framework/dxil/first.bin"
    "uvsr/dxil/second.bin")
if (NOT staged_files STREQUAL expected_files)
    message(FATAL_ERROR
        "Runtime shader synchronization produced an inexact file set: "
        "${staged_files}")
endif()
