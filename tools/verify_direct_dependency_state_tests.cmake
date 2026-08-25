cmake_minimum_required(VERSION 3.24)

function(_uvsr_test_fail detail)
    message(FATAL_ERROR "Direct dependency verifier self-test failed: ${detail}")
endfunction()

foreach(required IN ITEMS
        UVSR_VERIFIER
        UVSR_GIT_EXECUTABLE
        UVSR_TEST_ROOT)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        _uvsr_test_fail("-${required}=<value> is required")
    endif()
endforeach()
if (NOT EXISTS "${UVSR_VERIFIER}" OR IS_DIRECTORY "${UVSR_VERIFIER}")
    _uvsr_test_fail("verifier is not a file: ${UVSR_VERIFIER}")
endif()
if (NOT EXISTS "${UVSR_GIT_EXECUTABLE}" OR
    IS_DIRECTORY "${UVSR_GIT_EXECUTABLE}")
    _uvsr_test_fail("Git executable is not a file: ${UVSR_GIT_EXECUTABLE}")
endif()

set(test_root "${UVSR_TEST_ROOT}")
cmake_path(IS_ABSOLUTE test_root test_root_is_absolute)
cmake_path(NORMAL_PATH test_root OUTPUT_VARIABLE test_root)
cmake_path(GET test_root FILENAME test_root_name)
if (NOT test_root_is_absolute OR NOT test_root_name MATCHES
        "^direct-dependency-state-self-test(-[A-Za-z0-9_.-]+)?$")
    _uvsr_test_fail(
        "test root must be an absolute direct-dependency-state-self-test[-config] directory")
endif()
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

function(_uvsr_write_file path content)
    cmake_path(GET path PARENT_PATH parent)
    file(MAKE_DIRECTORY "${parent}")
    file(WRITE "${path}" "${content}")
endfunction()

function(_uvsr_absolute_record output path content)
    _uvsr_write_file("${path}" "${content}")
    file(SHA256 "${path}" hash)
    set(${output} "${path}|${hash}" PARENT_SCOPE)
endfunction()

function(_uvsr_absolute_records output root prefix count)
    set(records)
    foreach(index RANGE 1 ${count})
        set(path "${root}/${prefix}-${index}.txt")
        _uvsr_write_file("${path}" "${prefix}-${index}\n")
        file(SHA256 "${path}" hash)
        list(APPEND records "${path}|${hash}")
    endforeach()
    set(${output} "${records}" PARENT_SCOPE)
endfunction()

function(_uvsr_tree_fixture output_digest root prefix count)
    set(canonical_lines)
    foreach(index RANGE 1 ${count})
        set(relative "${prefix}/file-${index}.txt")
        set(path "${root}/${relative}")
        _uvsr_write_file("${path}" "${prefix}-${index}\n")
        file(SHA256 "${path}" hash)
        string(TOLOWER "${hash}" hash)
        list(APPEND canonical_lines "${relative}|${hash}")
    endforeach()
    list(SORT canonical_lines CASE SENSITIVE ORDER ASCENDING)
    set(canonical_inventory "")
    foreach(line IN LISTS canonical_lines)
        string(APPEND canonical_inventory "${line}\n")
    endforeach()
    string(SHA256 digest "${canonical_inventory}")
    set(${output_digest} "${digest}" PARENT_SCOPE)
endfunction()

function(_uvsr_dxil_header_fixture output_digest root count)
    set(canonical_lines)
    foreach(index RANGE 1 ${count})
        set(relative "shaders/shader-${index}.dxil.h")
        set(path "${root}/${relative}")
        _uvsr_write_file("${path}" "dxil-${index}\n")
        file(SHA256 "${path}" hash)
        string(TOLOWER "${hash}" hash)
        list(APPEND canonical_lines "${relative}|${hash}")
    endforeach()
    list(SORT canonical_lines CASE SENSITIVE ORDER ASCENDING)
    set(canonical_inventory "")
    foreach(line IN LISTS canonical_lines)
        string(APPEND canonical_inventory "${line}\n")
    endforeach()
    string(SHA256 digest "${canonical_inventory}")
    _uvsr_write_file(
        "${root}/dependencies.d"
        "C:/nonportable/build/path/input.h\n")
    _uvsr_write_file("${root}/bundle.stamp" "not part of the header view\n")
    set(${output_digest} "${digest}" PARENT_SCOPE)
endfunction()

function(_uvsr_create_git_worktree output_head path label)
    file(MAKE_DIRECTORY "${path}")
    file(WRITE "${path}/tracked.txt" "${label}\n")
    execute_process(
        COMMAND "${UVSR_GIT_EXECUTABLE}" init --quiet "${path}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error)
    if (NOT result EQUAL 0)
        _uvsr_test_fail("could not initialize ${label}: ${error}")
    endif()
    foreach(arguments IN ITEMS
            "config|core.autocrlf|false"
            "config|user.name|UVSR Self Test"
            "config|user.email|uvsr-self-test@example.invalid"
            "add|tracked.txt"
            "commit|--quiet|-m|initial")
        string(REPLACE "|" ";" arguments "${arguments}")
        execute_process(
            COMMAND "${UVSR_GIT_EXECUTABLE}" -C "${path}" ${arguments}
            RESULT_VARIABLE result
            ERROR_VARIABLE error)
        if (NOT result EQUAL 0)
            _uvsr_test_fail("could not prepare ${label}: ${error}")
        endif()
    endforeach()
    execute_process(
        COMMAND "${UVSR_GIT_EXECUTABLE}" -C "${path}" rev-parse HEAD
        RESULT_VARIABLE result
        OUTPUT_VARIABLE head
        ERROR_VARIABLE error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT result EQUAL 0)
        _uvsr_test_fail("could not read ${label} HEAD: ${error}")
    endif()
    set(${output_head} "${head}" PARENT_SCOPE)
endfunction()

set(nvrhi_root "${test_root}/nvrhi")
set(directx_headers_root "${test_root}/directx-headers")
set(donut_root "${test_root}/donut")
_uvsr_create_git_worktree(nvrhi_head "${nvrhi_root}" "NVRHI")
_uvsr_create_git_worktree(
    directx_headers_head "${directx_headers_root}" "DirectX-Headers")
_uvsr_create_git_worktree(donut_head "${donut_root}" "Donut")

set(dxc_root "${test_root}/archives/dxc")
set(cgltf_root "${test_root}/archives/cgltf")
set(stb_root "${test_root}/archives/stb")
set(jsoncpp_root "${test_root}/archives/jsoncpp")
set(tinyexr_root "${test_root}/archives/tinyexr")
set(glfw_root "${test_root}/archives/glfw")
set(mathlib_root "${test_root}/archives/mathlib")
set(dxc_count 4)
set(cgltf_count 3)
set(stb_count 4)
set(jsoncpp_count 5)
set(tinyexr_count 3)
set(glfw_count 6)
set(mathlib_count 7)
_uvsr_tree_fixture(dxc_digest "${dxc_root}" dxc ${dxc_count})
_uvsr_tree_fixture(cgltf_digest "${cgltf_root}" cgltf ${cgltf_count})
_uvsr_tree_fixture(stb_digest "${stb_root}" stb ${stb_count})
_uvsr_tree_fixture(jsoncpp_digest "${jsoncpp_root}" jsoncpp ${jsoncpp_count})
_uvsr_tree_fixture(tinyexr_digest "${tinyexr_root}" tinyexr ${tinyexr_count})
_uvsr_tree_fixture(glfw_digest "${glfw_root}" glfw ${glfw_count})
_uvsr_tree_fixture(mathlib_digest "${mathlib_root}" mathlib ${mathlib_count})

set(imgui_source_root "${test_root}/imgui-source")
set(nvrhi_stage_root "${test_root}/nvrhi-stage")
set(imgui_stage_root "${test_root}/imgui-stage")
set(donut_engine_stage_root "${test_root}/donut-engine-stage")
set(donut_app_stage_root "${test_root}/donut-app-stage")
set(nrd_stage_root "${test_root}/nrd-stage")
set(nrd_dxil_root "${test_root}/nrd-generated/dxil")
_uvsr_tree_fixture(imgui_source_digest "${imgui_source_root}" imgui 15)
_uvsr_tree_fixture(nvrhi_stage_digest "${nvrhi_stage_root}" nvrhi 16)
_uvsr_tree_fixture(imgui_stage_digest "${imgui_stage_root}" imgui 6)
_uvsr_tree_fixture(
    donut_engine_stage_digest "${donut_engine_stage_root}" engine 8)
_uvsr_tree_fixture(donut_app_stage_digest "${donut_app_stage_root}" app 8)
_uvsr_tree_fixture(nrd_stage_digest "${nrd_stage_root}" nrd 87)
_uvsr_dxil_header_fixture(nrd_dxil_digest "${nrd_dxil_root}" 29)

set(nvrhi_diagnostics_path
    "${test_root}/generated/uvsr_nvrhi_d3d12_diagnostics/uvsr-d3d12-diagnostics.h")
set(jsoncpp_alias_path
    "${test_root}/generated/uvsr_jsoncpp_compatibility/json/json-forwards.h")
set(agility_notice_path "${test_root}/raw/agility/agility-1.txt")
set(nrd_raw_path "${test_root}/raw/nrd/nrd-1.txt")
_uvsr_absolute_records(
    agility_notice_records "${test_root}/raw/agility" agility 3)
_uvsr_absolute_record(
    nvrhi_diagnostics_records "${nvrhi_diagnostics_path}" "diagnostics\n")
_uvsr_absolute_record(
    jsoncpp_alias_records "${jsoncpp_alias_path}"
    "#pragma once\n#include <json/forwards.h>\n")
_uvsr_absolute_records(nrd_raw_records "${test_root}/raw/nrd" nrd 6)

function(_uvsr_run_inputs_verifier output_result output_log)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DUVSR_DIRECT_DEPENDENCY_VERIFICATION_SCOPE=inputs
            "-DUVSR_GIT_EXECUTABLE=${UVSR_GIT_EXECUTABLE}"
            "-DUVSR_NVRHI_SOURCE_DIR=${nvrhi_root}"
            "-DUVSR_NVRHI_EXPECTED_HEAD=${nvrhi_head}"
            "-DUVSR_DIRECTX_HEADERS_SOURCE_DIR=${directx_headers_root}"
            "-DUVSR_DIRECTX_HEADERS_EXPECTED_HEAD=${directx_headers_head}"
            "-DUVSR_TRANSITIONAL_DONUT_SOURCE_DIR=${donut_root}"
            "-DUVSR_TRANSITIONAL_DONUT_EXPECTED_HEAD=${donut_head}"
            "-DUVSR_DXC_ROOT=${dxc_root}"
            -DUVSR_DXC_FILE_COUNT=${dxc_count}
            "-DUVSR_DXC_TREE_DIGEST=${dxc_digest}"
            "-DUVSR_CGLTF_ROOT=${cgltf_root}"
            -DUVSR_CGLTF_FILE_COUNT=${cgltf_count}
            "-DUVSR_CGLTF_TREE_DIGEST=${cgltf_digest}"
            "-DUVSR_STB_ROOT=${stb_root}"
            -DUVSR_STB_FILE_COUNT=${stb_count}
            "-DUVSR_STB_TREE_DIGEST=${stb_digest}"
            "-DUVSR_JSONCPP_ROOT=${jsoncpp_root}"
            -DUVSR_JSONCPP_FILE_COUNT=${jsoncpp_count}
            "-DUVSR_JSONCPP_TREE_DIGEST=${jsoncpp_digest}"
            "-DUVSR_TRANSITIONAL_TINYEXR_ROOT=${tinyexr_root}"
            -DUVSR_TRANSITIONAL_TINYEXR_FILE_COUNT=${tinyexr_count}
            "-DUVSR_TRANSITIONAL_TINYEXR_TREE_DIGEST=${tinyexr_digest}"
            "-DUVSR_TRANSITIONAL_GLFW_ROOT=${glfw_root}"
            -DUVSR_TRANSITIONAL_GLFW_FILE_COUNT=${glfw_count}
            "-DUVSR_TRANSITIONAL_GLFW_TREE_DIGEST=${glfw_digest}"
            "-DUVSR_AGILITY_NOTICE_RECORDS=${agility_notice_records}"
            "-DUVSR_NVRHI_DIAGNOSTICS_STAGE_RECORDS=${nvrhi_diagnostics_records}"
            "-DUVSR_TRANSITIONAL_JSONCPP_ALIAS_RECORDS=${jsoncpp_alias_records}"
            "-DUVSR_IMGUI_SOURCE_DIR=${imgui_source_root}"
            "-DUVSR_IMGUI_SOURCE_TREE_DIGEST=${imgui_source_digest}"
            "-DUVSR_NVRHI_STAGE_DIR=${nvrhi_stage_root}"
            "-DUVSR_NVRHI_STAGE_TREE_DIGEST=${nvrhi_stage_digest}"
            "-DUVSR_IMGUI_STAGE_DIR=${imgui_stage_root}"
            "-DUVSR_IMGUI_STAGE_TREE_DIGEST=${imgui_stage_digest}"
            "-DUVSR_TRANSITIONAL_DONUT_ENGINE_STAGE_DIR=${donut_engine_stage_root}"
            "-DUVSR_TRANSITIONAL_DONUT_ENGINE_STAGE_TREE_DIGEST=${donut_engine_stage_digest}"
            "-DUVSR_TRANSITIONAL_DONUT_APP_STAGE_DIR=${donut_app_stage_root}"
            "-DUVSR_TRANSITIONAL_DONUT_APP_STAGE_TREE_DIGEST=${donut_app_stage_digest}"
            "-DUVSR_NRD_RAW_FILE_RECORDS=${nrd_raw_records}"
            "-DUVSR_NRD_STAGE_DIR=${nrd_stage_root}"
            "-DUVSR_NRD_STAGE_TREE_DIGEST=${nrd_stage_digest}"
            "-DUVSR_MATHLIB_ROOT=${mathlib_root}"
            -DUVSR_MATHLIB_FILE_COUNT=${mathlib_count}
            "-DUVSR_MATHLIB_TREE_DIGEST=${mathlib_digest}"
            -P "${UVSR_VERIFIER}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    set(${output_result} "${result}" PARENT_SCOPE)
    set(${output_log} "${output}${error}" PARENT_SCOPE)
endfunction()

function(_uvsr_run_generated_verifier output_result output_log)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DUVSR_DIRECT_DEPENDENCY_VERIFICATION_SCOPE=nrd-generated-dxil
            "-DUVSR_NRD_DXIL_HEADERS_DIR=${nrd_dxil_root}"
            "-DUVSR_NRD_DXIL_HEADERS_TREE_DIGEST=${nrd_dxil_digest}"
            -P "${UVSR_VERIFIER}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    set(${output_result} "${result}" PARENT_SCOPE)
    set(${output_log} "${output}${error}" PARENT_SCOPE)
endfunction()

function(_uvsr_expect_inputs_failure label expected_text)
    _uvsr_run_inputs_verifier(result log)
    if (result EQUAL 0 OR NOT log MATCHES "${expected_text}")
        _uvsr_test_fail(
            "${label} did not fail for the expected reason: ${log}")
    endif()
endfunction()

function(_uvsr_expect_generated_failure label expected_text)
    _uvsr_run_generated_verifier(result log)
    if (result EQUAL 0 OR NOT log MATCHES "${expected_text}")
        _uvsr_test_fail(
            "${label} did not fail for the expected reason: ${log}")
    endif()
endfunction()

_uvsr_run_inputs_verifier(result log)
if (NOT result EQUAL 0)
    _uvsr_test_fail("positive input fixture was rejected: ${log}")
endif()
_uvsr_run_generated_verifier(result log)
if (NOT result EQUAL 0)
    _uvsr_test_fail("positive generated fixture was rejected: ${log}")
endif()

file(APPEND "${dxc_root}/dxc/file-1.txt" "tampered")
_uvsr_expect_inputs_failure("archive digest tamper" "DXC archive tree digest")
file(WRITE "${dxc_root}/dxc/file-1.txt" "dxc-1\n")

file(WRITE "${cgltf_root}/unexpected.txt" "unexpected\n")
_uvsr_expect_inputs_failure("archive count tamper" "cgltf archive file count")
file(REMOVE "${cgltf_root}/unexpected.txt")

file(APPEND "${nvrhi_root}/tracked.txt" "dirty")
_uvsr_expect_inputs_failure("dirty Git worktree" "NVRHI worktree is not clean")
file(WRITE "${nvrhi_root}/tracked.txt" "NVRHI\n")

file(APPEND "${donut_root}/tracked.txt" "dirty")
_uvsr_expect_inputs_failure(
    "dirty transitional Donut worktree" "Donut .* worktree is not clean")
file(WRITE "${donut_root}/tracked.txt" "Donut\n")

file(APPEND "${agility_notice_path}" "tampered")
_uvsr_expect_inputs_failure(
    "Agility raw-file tamper" "Direct3D Agility SDK notices")
file(WRITE "${agility_notice_path}" "agility-1\n")

file(APPEND "${nvrhi_diagnostics_path}" "tampered")
_uvsr_expect_inputs_failure(
    "post-config NVRHI diagnostics tamper" "NVRHI generated diagnostics")
file(WRITE "${nvrhi_diagnostics_path}" "diagnostics\n")

file(APPEND "${jsoncpp_alias_path}" "tampered")
_uvsr_expect_inputs_failure(
    "post-config JsonCpp alias tamper" "JsonCpp generated alias")
file(WRITE "${jsoncpp_alias_path}"
    "#pragma once\n#include <json/forwards.h>\n")

file(WRITE "${nvrhi_stage_root}/unexpected.txt" "unexpected\n")
_uvsr_expect_inputs_failure(
    "staged tree count tamper" "NVRHI staged overrides")
file(REMOVE "${nvrhi_stage_root}/unexpected.txt")

file(APPEND "${nrd_raw_path}" "tampered")
_uvsr_expect_inputs_failure("NRD raw-file tamper" "NRD direct raw files")
file(WRITE "${nrd_raw_path}" "nrd-1\n")

file(APPEND "${nrd_stage_root}/nrd/file-2.txt" "tampered")
_uvsr_expect_inputs_failure(
    "NRD staged tree tamper" "NRD staged retained tree")
file(WRITE "${nrd_stage_root}/nrd/file-2.txt" "nrd-2\n")

file(APPEND "${mathlib_root}/mathlib/file-1.txt" "tampered")
_uvsr_expect_inputs_failure("MathLib tree tamper" "MathLib archive tree digest")
file(WRITE "${mathlib_root}/mathlib/file-1.txt" "mathlib-1\n")

file(APPEND "${nrd_dxil_root}/shaders/shader-1.dxil.h" "tampered")
_uvsr_expect_generated_failure(
    "generated DXIL tamper" "NRD generated DXIL header view")
file(WRITE "${nrd_dxil_root}/shaders/shader-1.dxil.h" "dxil-1\n")

file(WRITE "${nrd_dxil_root}/unexpected.dxil.h" "unexpected\n")
_uvsr_expect_generated_failure(
    "generated DXIL count tamper" "NRD generated DXIL header view")

file(REMOVE_RECURSE "${test_root}")
message(STATUS
    "Direct dependency verifier input/generated positives and 13 tampered fixtures passed")
