cmake_minimum_required(VERSION 3.24)

function(_uvsr_dependency_fail detail)
    message(FATAL_ERROR "Direct dependency state verification failed: ${detail}")
endfunction()

function(_uvsr_require_value name)
    if (NOT DEFINED ${name} OR "${${name}}" STREQUAL "")
        _uvsr_dependency_fail("-${name}=<value> is required")
    endif()
endfunction()

function(_uvsr_normalize_sha256 output hash label)
    string(LENGTH "${hash}" hash_length)
    string(TOLOWER "${hash}" hash)
    if (NOT hash_length EQUAL 64 OR NOT hash MATCHES "^[0-9a-f]+$")
        _uvsr_dependency_fail("${label} SHA-256 is malformed")
    endif()
    set(${output} "${hash}" PARENT_SCOPE)
endfunction()

function(_uvsr_parse_record record output_path output_hash label)
    string(REPLACE "|" ";" fields "${record}")
    list(LENGTH fields field_count)
    if (NOT field_count EQUAL 2)
        _uvsr_dependency_fail(
            "${label} record must be '<path>|<sha256>': '${record}'")
    endif()
    list(GET fields 0 path)
    list(GET fields 1 hash)
    if (path STREQUAL "" OR hash STREQUAL "")
        _uvsr_dependency_fail("${label} record has an empty field")
    endif()
    _uvsr_normalize_sha256(hash "${hash}" "${label} record")
    set(${output_path} "${path}" PARENT_SCOPE)
    set(${output_hash} "${hash}" PARENT_SCOPE)
endfunction()

function(_uvsr_normalize_absolute_path output path label)
    set(candidate "${path}")
    cmake_path(IS_ABSOLUTE candidate is_absolute)
    if (NOT is_absolute)
        _uvsr_dependency_fail("${label} path is not absolute: '${path}'")
    endif()
    cmake_path(NORMAL_PATH candidate OUTPUT_VARIABLE normalized)
    set(${output} "${normalized}" PARENT_SCOPE)
endfunction()

function(_uvsr_validate_relative_path output path label)
    set(candidate "${path}")
    string(FIND "${candidate}" ":" colon)
    if (candidate STREQUAL "" OR NOT colon EQUAL -1 OR
        candidate MATCHES [[\\]] OR candidate MATCHES "[|;\r\n]" OR
        candidate MATCHES "^/" OR candidate MATCHES "/$" OR
        candidate MATCHES "//" OR
        candidate MATCHES [[(^|/)\.\.?(/|$)]])
        _uvsr_dependency_fail(
            "${label} is not a safe normalized relative path: '${path}'")
    endif()
    cmake_path(IS_ABSOLUTE candidate is_absolute)
    cmake_path(NORMAL_PATH candidate OUTPUT_VARIABLE normalized)
    if (is_absolute OR NOT normalized STREQUAL candidate)
        _uvsr_dependency_fail(
            "${label} is not a safe normalized relative path: '${path}'")
    endif()
    set(${output} "${normalized}" PARENT_SCOPE)
endfunction()

function(_uvsr_verify_file path expected_hash label)
    if (NOT EXISTS "${path}" OR IS_DIRECTORY "${path}" OR IS_SYMLINK "${path}")
        _uvsr_dependency_fail("${label} is not a regular file: ${path}")
    endif()
    file(SHA256 "${path}" actual_hash)
    string(TOLOWER "${actual_hash}" actual_hash)
    if (NOT actual_hash STREQUAL expected_hash)
        _uvsr_dependency_fail(
            "${label} SHA-256 is ${actual_hash}, expected ${expected_hash}")
    endif()
endfunction()

function(_uvsr_verify_absolute_records label records_name expected_count)
    set(records "${${records_name}}")
    if (NOT expected_count MATCHES "^[1-9][0-9]*$")
        _uvsr_dependency_fail("${label} expected count is not positive")
    endif()
    list(LENGTH records actual_count)
    if (NOT actual_count EQUAL expected_count)
        _uvsr_dependency_fail(
            "${label} record count is ${actual_count}, expected ${expected_count}")
    endif()

    set(seen_paths)
    foreach(record IN LISTS records)
        _uvsr_parse_record("${record}" path expected_hash "${label}")
        _uvsr_normalize_absolute_path(path "${path}" "${label}")
        string(TOLOWER "${path}" path_key)
        list(FIND seen_paths "${path_key}" duplicate_index)
        if (NOT duplicate_index EQUAL -1)
            _uvsr_dependency_fail("${label} repeats path: ${path}")
        endif()
        list(APPEND seen_paths "${path_key}")
        _uvsr_verify_file("${path}" "${expected_hash}" "${label}")
    endforeach()
endfunction()

# The digest is SHA-256 over sorted UTF-8 lines:
# relative/path|lowercase_file_sha256\n
function(_uvsr_verify_tree_digest
    label root_name expected_count expected_digest_name)
    set(tree_glob "*")
    if (ARGC EQUAL 5)
        set(tree_glob "${ARGV4}")
    elseif(ARGC GREATER 5)
        _uvsr_dependency_fail("${label} received an invalid tree digest call")
    endif()
    if (NOT expected_count MATCHES "^[1-9][0-9]*$")
        _uvsr_dependency_fail("${label} expected count is not positive")
    endif()
    _uvsr_normalize_sha256(
        expected_digest "${${expected_digest_name}}" "${label} tree")
    _uvsr_normalize_absolute_path(root "${${root_name}}" "${label} root")
    if (NOT IS_DIRECTORY "${root}" OR IS_SYMLINK "${root}")
        _uvsr_dependency_fail("${label} root is not a directory: ${root}")
    endif()

    file(GLOB_RECURSE entries
        LIST_DIRECTORIES true
        RELATIVE "${root}"
        "${root}/${tree_glob}")
    set(files)
    set(file_keys)
    foreach(relative IN LISTS entries)
        file(TO_CMAKE_PATH "${relative}" relative)
        _uvsr_validate_relative_path(
            relative "${relative}" "${label} inventory path")
        if (IS_SYMLINK "${root}/${relative}")
            _uvsr_dependency_fail(
                "${label} inventory contains a symlink: ${relative}")
        endif()
        if (NOT IS_DIRECTORY "${root}/${relative}")
            string(TOLOWER "${relative}" relative_key)
            list(FIND file_keys "${relative_key}" duplicate_index)
            if (NOT duplicate_index EQUAL -1)
                _uvsr_dependency_fail(
                    "${label} repeats a case-insensitive path: ${relative}")
            endif()
            list(APPEND file_keys "${relative_key}")
            list(APPEND files "${relative}")
        endif()
    endforeach()
    list(SORT files CASE SENSITIVE ORDER ASCENDING)
    list(LENGTH files actual_count)
    if (NOT actual_count EQUAL expected_count)
        _uvsr_dependency_fail(
            "${label} file count is ${actual_count}, expected ${expected_count}")
    endif()

    set(canonical_inventory "")
    foreach(relative IN LISTS files)
        file(SHA256 "${root}/${relative}" file_hash)
        string(TOLOWER "${file_hash}" file_hash)
        string(APPEND canonical_inventory "${relative}|${file_hash}\n")
    endforeach()
    string(SHA256 actual_digest "${canonical_inventory}")
    if (NOT actual_digest STREQUAL expected_digest)
        _uvsr_dependency_fail(
            "${label} tree digest is ${actual_digest}, expected ${expected_digest}")
    endif()
endfunction()

function(_uvsr_verify_git_worktree label source_name head_name)
    _uvsr_normalize_absolute_path(
        source "${${source_name}}" "${label} source")
    if (NOT IS_DIRECTORY "${source}")
        _uvsr_dependency_fail("${label} source is not a directory: ${source}")
    endif()
    set(expected_head "${${head_name}}")
    string(LENGTH "${expected_head}" head_length)
    string(TOLOWER "${expected_head}" expected_head)
    if (NOT head_length EQUAL 40 OR NOT expected_head MATCHES "^[0-9a-f]+$")
        _uvsr_dependency_fail("${label} expected HEAD is malformed")
    endif()

    execute_process(
        COMMAND "${UVSR_GIT_EXECUTABLE}" -C "${source}"
            rev-parse --show-toplevel HEAD
        RESULT_VARIABLE revision_result
        OUTPUT_VARIABLE revision_output
        ERROR_VARIABLE revision_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT revision_result EQUAL 0)
        _uvsr_dependency_fail(
            "${label} revision query failed: ${revision_error}")
    endif()
    string(REPLACE "\n" ";" revision_fields "${revision_output}")
    list(LENGTH revision_fields revision_field_count)
    if (NOT revision_field_count EQUAL 2)
        _uvsr_dependency_fail("${label} revision query returned malformed output")
    endif()
    list(GET revision_fields 0 actual_root)
    list(GET revision_fields 1 actual_head)
    _uvsr_normalize_absolute_path(
        actual_root "${actual_root}" "${label} Git root")
    string(TOLOWER "${source}" source_key)
    string(TOLOWER "${actual_root}" actual_root_key)
    string(TOLOWER "${actual_head}" actual_head)
    if (NOT source_key STREQUAL actual_root_key)
        _uvsr_dependency_fail(
            "${label} path is not its Git worktree root: ${source}")
    endif()
    if (NOT actual_head STREQUAL expected_head)
        _uvsr_dependency_fail(
            "${label} HEAD is ${actual_head}, expected ${expected_head}")
    endif()

    execute_process(
        COMMAND "${UVSR_GIT_EXECUTABLE}" -C "${source}"
            status --porcelain=v1 --untracked-files=all
            --ignore-submodules=none
        RESULT_VARIABLE status_result
        OUTPUT_VARIABLE status_output
        ERROR_VARIABLE status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT status_result EQUAL 0 OR NOT status_output STREQUAL "")
        _uvsr_dependency_fail(
            "${label} worktree is not clean: ${status_output}${status_error}")
    endif()
endfunction()

_uvsr_require_value(UVSR_DIRECT_DEPENDENCY_VERIFICATION_SCOPE)

if (UVSR_DIRECT_DEPENDENCY_VERIFICATION_SCOPE STREQUAL "inputs")
    foreach(required IN ITEMS
            UVSR_GIT_EXECUTABLE
            UVSR_NVRHI_SOURCE_DIR
            UVSR_NVRHI_EXPECTED_HEAD
            UVSR_DIRECTX_HEADERS_SOURCE_DIR
            UVSR_DIRECTX_HEADERS_EXPECTED_HEAD
            UVSR_TRANSITIONAL_DONUT_SOURCE_DIR
            UVSR_TRANSITIONAL_DONUT_EXPECTED_HEAD
            UVSR_DXC_ROOT UVSR_DXC_FILE_COUNT UVSR_DXC_TREE_DIGEST
            UVSR_CGLTF_ROOT UVSR_CGLTF_FILE_COUNT UVSR_CGLTF_TREE_DIGEST
            UVSR_STB_ROOT UVSR_STB_FILE_COUNT UVSR_STB_TREE_DIGEST
            UVSR_JSONCPP_ROOT UVSR_JSONCPP_FILE_COUNT UVSR_JSONCPP_TREE_DIGEST
            UVSR_TRANSITIONAL_TINYEXR_ROOT
            UVSR_TRANSITIONAL_TINYEXR_FILE_COUNT
            UVSR_TRANSITIONAL_TINYEXR_TREE_DIGEST
            UVSR_TRANSITIONAL_GLFW_ROOT
            UVSR_TRANSITIONAL_GLFW_FILE_COUNT
            UVSR_TRANSITIONAL_GLFW_TREE_DIGEST
            UVSR_AGILITY_NOTICE_RECORDS
            UVSR_NVRHI_DIAGNOSTICS_STAGE_RECORDS
            UVSR_TRANSITIONAL_JSONCPP_ALIAS_RECORDS
            UVSR_IMGUI_SOURCE_DIR UVSR_IMGUI_SOURCE_TREE_DIGEST
            UVSR_NVRHI_STAGE_DIR UVSR_NVRHI_STAGE_TREE_DIGEST
            UVSR_IMGUI_STAGE_DIR UVSR_IMGUI_STAGE_TREE_DIGEST
            UVSR_TRANSITIONAL_DONUT_ENGINE_STAGE_DIR
            UVSR_TRANSITIONAL_DONUT_ENGINE_STAGE_TREE_DIGEST
            UVSR_TRANSITIONAL_DONUT_APP_STAGE_DIR
            UVSR_TRANSITIONAL_DONUT_APP_STAGE_TREE_DIGEST
            UVSR_NRD_RAW_FILE_RECORDS
            UVSR_NRD_STAGE_DIR UVSR_NRD_STAGE_TREE_DIGEST
            UVSR_MATHLIB_ROOT
            UVSR_MATHLIB_FILE_COUNT
            UVSR_MATHLIB_TREE_DIGEST)
        _uvsr_require_value(${required})
    endforeach()

    _uvsr_normalize_absolute_path(
        git_executable "${UVSR_GIT_EXECUTABLE}" "Git executable")
    if (NOT EXISTS "${git_executable}" OR
        IS_DIRECTORY "${git_executable}" OR IS_SYMLINK "${git_executable}")
        _uvsr_dependency_fail("Git executable is not a regular file")
    endif()
    set(UVSR_GIT_EXECUTABLE "${git_executable}")

    _uvsr_verify_git_worktree(
        "NVRHI" UVSR_NVRHI_SOURCE_DIR UVSR_NVRHI_EXPECTED_HEAD)
    _uvsr_verify_git_worktree(
        "DirectX-Headers"
        UVSR_DIRECTX_HEADERS_SOURCE_DIR
        UVSR_DIRECTX_HEADERS_EXPECTED_HEAD)
    _uvsr_verify_git_worktree(
        "Donut (transitional attachment-only)"
        UVSR_TRANSITIONAL_DONUT_SOURCE_DIR
        UVSR_TRANSITIONAL_DONUT_EXPECTED_HEAD)

    _uvsr_verify_tree_digest(
        "DXC archive" UVSR_DXC_ROOT
        "${UVSR_DXC_FILE_COUNT}" UVSR_DXC_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "cgltf archive" UVSR_CGLTF_ROOT
        "${UVSR_CGLTF_FILE_COUNT}" UVSR_CGLTF_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "stb archive" UVSR_STB_ROOT
        "${UVSR_STB_FILE_COUNT}" UVSR_STB_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "JsonCpp archive" UVSR_JSONCPP_ROOT
        "${UVSR_JSONCPP_FILE_COUNT}" UVSR_JSONCPP_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "TinyEXR archive (transitional attachment-only)"
        UVSR_TRANSITIONAL_TINYEXR_ROOT
        "${UVSR_TRANSITIONAL_TINYEXR_FILE_COUNT}"
        UVSR_TRANSITIONAL_TINYEXR_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "GLFW archive (transitional attachment-only)"
        UVSR_TRANSITIONAL_GLFW_ROOT
        "${UVSR_TRANSITIONAL_GLFW_FILE_COUNT}"
        UVSR_TRANSITIONAL_GLFW_TREE_DIGEST)

    _uvsr_verify_absolute_records(
        "Direct3D Agility SDK notices" UVSR_AGILITY_NOTICE_RECORDS 3)
    _uvsr_verify_absolute_records(
        "NVRHI generated diagnostics stage"
        UVSR_NVRHI_DIAGNOSTICS_STAGE_RECORDS 1)

    # These aliases and stages exist only while their Donut callers remain.
    _uvsr_verify_absolute_records(
        "JsonCpp generated alias (transitional Donut attachment-only)"
        UVSR_TRANSITIONAL_JSONCPP_ALIAS_RECORDS 1)
    _uvsr_verify_tree_digest(
        "ImGui source" UVSR_IMGUI_SOURCE_DIR 15
        UVSR_IMGUI_SOURCE_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "NVRHI staged overrides" UVSR_NVRHI_STAGE_DIR 16
        UVSR_NVRHI_STAGE_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "ImGui staged overrides" UVSR_IMGUI_STAGE_DIR 6
        UVSR_IMGUI_STAGE_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "Donut engine staged overrides (transitional attachment-only)"
        UVSR_TRANSITIONAL_DONUT_ENGINE_STAGE_DIR 8
        UVSR_TRANSITIONAL_DONUT_ENGINE_STAGE_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "Donut app staged overrides (transitional attachment-only)"
        UVSR_TRANSITIONAL_DONUT_APP_STAGE_DIR 8
        UVSR_TRANSITIONAL_DONUT_APP_STAGE_TREE_DIGEST)

    _uvsr_verify_absolute_records(
        "NRD direct raw files" UVSR_NRD_RAW_FILE_RECORDS 6)
    _uvsr_verify_tree_digest(
        "NRD staged retained tree" UVSR_NRD_STAGE_DIR 87
        UVSR_NRD_STAGE_TREE_DIGEST)
    _uvsr_verify_tree_digest(
        "MathLib archive" UVSR_MATHLIB_ROOT
        "${UVSR_MATHLIB_FILE_COUNT}" UVSR_MATHLIB_TREE_DIGEST)

    message(STATUS "Direct dependency input state is exact")
elseif(UVSR_DIRECT_DEPENDENCY_VERIFICATION_SCOPE STREQUAL
        "nrd-generated-dxil")
    foreach(required IN ITEMS
            UVSR_NRD_DXIL_HEADERS_DIR
            UVSR_NRD_DXIL_HEADERS_TREE_DIGEST)
        _uvsr_require_value(${required})
    endforeach()
    _uvsr_verify_tree_digest(
        "NRD generated DXIL header view"
        UVSR_NRD_DXIL_HEADERS_DIR 29
        UVSR_NRD_DXIL_HEADERS_TREE_DIGEST "*.dxil.h")
    message(STATUS "NRD generated DXIL header state is exact")
else()
    _uvsr_dependency_fail(
        "UVSR_DIRECT_DEPENDENCY_VERIFICATION_SCOPE must be 'inputs' or 'nrd-generated-dxil'")
endif()
