if (NOT DEFINED UVSR_RUNTIME_SHADER_SOURCE_MANIFEST OR
    NOT DEFINED UVSR_RUNTIME_SHADER_ALLOWED_PARENT OR
    NOT DEFINED UVSR_RUNTIME_SHADER_ROOT OR
    NOT DEFINED UVSR_RUNTIME_SHADER_STAGE_STAMP)
    message(FATAL_ERROR
        "Runtime shader synchronization requires source manifest, allowed parent, "
        "root, and stamp paths")
endif()

function(uvsr_normalize_absolute_path input_path output_variable)
    if (NOT IS_ABSOLUTE "${input_path}")
        message(FATAL_ERROR
            "Runtime shader synchronization requires an absolute path: ${input_path}")
    endif()
    file(TO_CMAKE_PATH "${input_path}" cmake_path)
    cmake_path(NORMAL_PATH cmake_path OUTPUT_VARIABLE normalized_path)
    set("${output_variable}" "${normalized_path}" PARENT_SCOPE)
endfunction()

function(uvsr_paths_equal first_path second_path output_variable)
    set(first_comparison "${first_path}")
    set(second_comparison "${second_path}")
    if (WIN32)
        string(TOLOWER "${first_comparison}" first_comparison)
        string(TOLOWER "${second_comparison}" second_comparison)
    endif()
    if (first_comparison STREQUAL second_comparison)
        set("${output_variable}" true PARENT_SCOPE)
    else()
        set("${output_variable}" false PARENT_SCOPE)
    endif()
endfunction()

function(uvsr_require_path_within_root candidate_path root_path description)
    set(candidate_comparison "${candidate_path}")
    set(root_comparison "${root_path}")
    if (WIN32)
        string(TOLOWER "${candidate_comparison}" candidate_comparison)
        string(TOLOWER "${root_comparison}" root_comparison)
    endif()
    if (candidate_comparison STREQUAL root_comparison)
        return()
    endif()
    string(APPEND root_comparison "/")
    string(FIND "${candidate_comparison}" "${root_comparison}" prefix_index)
    if (NOT prefix_index EQUAL 0)
        message(FATAL_ERROR
            "${description} escapes the allowed runtime shader root: ${candidate_path}")
    endif()
endfunction()

function(uvsr_require_safe_existing_ancestor candidate_path root_path description)
    set(existing_ancestor "${candidate_path}")
    while (NOT EXISTS "${existing_ancestor}")
        get_filename_component(parent_path "${existing_ancestor}" DIRECTORY)
        if (parent_path STREQUAL existing_ancestor)
            message(FATAL_ERROR
                "Could not find an existing ancestor for ${description}: ${candidate_path}")
        endif()
        set(existing_ancestor "${parent_path}")
    endwhile()
    if (IS_SYMLINK "${existing_ancestor}")
        message(FATAL_ERROR
            "${description} has a symbolic-link ancestor: ${existing_ancestor}")
    endif()
    file(REAL_PATH "${existing_ancestor}" resolved_ancestor)
    uvsr_normalize_absolute_path("${resolved_ancestor}" resolved_ancestor)
    uvsr_require_path_within_root(
        "${resolved_ancestor}"
        "${root_path}"
        "${description}")
endfunction()

function(uvsr_validate_relative_shader_path relative_path description)
    string(FIND "${relative_path}" "\\" backslash_index)
    string(FIND "${relative_path}" ":" colon_index)
    if (relative_path STREQUAL "" OR
        IS_ABSOLUTE "${relative_path}" OR
        NOT backslash_index EQUAL -1 OR
        NOT colon_index EQUAL -1 OR
        relative_path MATCHES "(^|/)\\.\\.?(/|$)" OR
        relative_path MATCHES "(^/|/$|//)")
        message(FATAL_ERROR
            "Unsafe ${description}: ${relative_path}")
    endif()
    set(normalized_relative_path "${relative_path}")
    cmake_path(
        NORMAL_PATH
        normalized_relative_path
        OUTPUT_VARIABLE normalized_relative_path)
    if (NOT normalized_relative_path STREQUAL relative_path)
        message(FATAL_ERROR
            "Non-canonical ${description}: ${relative_path}")
    endif()
    get_filename_component(relative_extension "${relative_path}" EXT)
    if (NOT relative_extension STREQUAL ".bin")
        message(FATAL_ERROR
            "Runtime shader mapping does not name a .bin file: ${relative_path}")
    endif()
endfunction()

uvsr_normalize_absolute_path(
    "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}"
    UVSR_RUNTIME_SHADER_ALLOWED_PARENT)
uvsr_normalize_absolute_path(
    "${UVSR_RUNTIME_SHADER_ROOT}"
    UVSR_RUNTIME_SHADER_ROOT)
set(expected_runtime_shader_root
    "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}/shaders")
uvsr_normalize_absolute_path(
    "${expected_runtime_shader_root}"
    expected_runtime_shader_root)
uvsr_paths_equal(
    "${UVSR_RUNTIME_SHADER_ROOT}"
    "${expected_runtime_shader_root}"
    runtime_root_is_expected)
if (NOT runtime_root_is_expected)
    message(FATAL_ERROR
        "Refusing to synchronize an unexpected runtime shader root: ${UVSR_RUNTIME_SHADER_ROOT}")
endif()

if (NOT EXISTS "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}" OR
    NOT IS_DIRECTORY "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}")
    message(FATAL_ERROR
        "Runtime shader parent is missing or not a directory: "
        "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}")
endif()
if (IS_SYMLINK "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}")
    message(FATAL_ERROR
        "Refusing a symbolic-link runtime shader parent: "
        "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}")
endif()
file(REAL_PATH
    "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}"
    resolved_runtime_shader_parent)
uvsr_normalize_absolute_path(
    "${resolved_runtime_shader_parent}"
    resolved_runtime_shader_parent)
uvsr_paths_equal(
    "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}"
    "${resolved_runtime_shader_parent}"
    runtime_parent_is_unredirected)
if (NOT runtime_parent_is_unredirected)
    message(FATAL_ERROR
        "Refusing a redirected runtime shader parent: "
        "${UVSR_RUNTIME_SHADER_ALLOWED_PARENT}")
endif()

if (EXISTS "${UVSR_RUNTIME_SHADER_ROOT}")
    if (NOT IS_DIRECTORY "${UVSR_RUNTIME_SHADER_ROOT}" OR
        IS_SYMLINK "${UVSR_RUNTIME_SHADER_ROOT}")
        message(FATAL_ERROR
            "Runtime shader root is not a normal directory: "
            "${UVSR_RUNTIME_SHADER_ROOT}")
    endif()
    file(REAL_PATH "${UVSR_RUNTIME_SHADER_ROOT}" resolved_runtime_shader_root)
    uvsr_normalize_absolute_path(
        "${resolved_runtime_shader_root}"
        resolved_runtime_shader_root)
    uvsr_paths_equal(
        "${UVSR_RUNTIME_SHADER_ROOT}"
        "${resolved_runtime_shader_root}"
        runtime_root_is_unredirected)
    if (NOT runtime_root_is_unredirected)
        message(FATAL_ERROR
            "Refusing a redirected runtime shader root: "
            "${UVSR_RUNTIME_SHADER_ROOT}")
    endif()
else()
    file(MAKE_DIRECTORY "${UVSR_RUNTIME_SHADER_ROOT}")
endif()

if (NOT EXISTS "${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}")
    message(FATAL_ERROR
        "Runtime shader source manifest is missing: ${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}")
endif()

file(STRINGS
    "${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}"
    UVSR_RUNTIME_SHADER_MAPPINGS)
if (NOT UVSR_RUNTIME_SHADER_MAPPINGS)
    message(FATAL_ERROR "Runtime shader source manifest is empty")
endif()

set(UVSR_EXPECTED_RELATIVE_PATHS)
set(UVSR_EXPECTED_RELATIVE_PATH_KEYS)
set(UVSR_RUNTIME_SHADER_SYNCHRONIZATION_REQUIRED false)
foreach(mapping IN LISTS UVSR_RUNTIME_SHADER_MAPPINGS)
    string(FIND "${mapping}" "|" separator)
    if (separator LESS 1)
        message(FATAL_ERROR
            "Malformed runtime shader source mapping: ${mapping}")
    endif()
    string(SUBSTRING "${mapping}" 0 ${separator} source_path)
    math(EXPR relative_start "${separator} + 1")
    string(SUBSTRING "${mapping}" ${relative_start} -1 relative_path)
    string(FIND "${relative_path}" "|" second_separator)
    if (NOT second_separator EQUAL -1)
        message(FATAL_ERROR
            "Malformed runtime shader source mapping: ${mapping}")
    endif()
    uvsr_validate_relative_shader_path(
        "${relative_path}"
        "runtime shader relative path")
    if (NOT IS_ABSOLUTE "${source_path}" OR
        NOT EXISTS "${source_path}" OR
        IS_DIRECTORY "${source_path}")
        message(FATAL_ERROR
            "Compiled runtime shader is missing: ${source_path}")
    endif()
    file(SIZE "${source_path}" source_size)
    if (source_size EQUAL 0)
        message(FATAL_ERROR
            "Compiled runtime shader is empty: ${source_path}")
    endif()

    set(relative_path_key "${relative_path}")
    if (WIN32)
        string(TOLOWER "${relative_path_key}" relative_path_key)
    endif()
    if (relative_path_key IN_LIST UVSR_EXPECTED_RELATIVE_PATH_KEYS)
        message(FATAL_ERROR
            "Duplicate runtime shader relative path: ${relative_path}")
    endif()
    list(APPEND UVSR_EXPECTED_RELATIVE_PATHS "${relative_path}")
    list(APPEND UVSR_EXPECTED_RELATIVE_PATH_KEYS "${relative_path_key}")
    set(target_path "${UVSR_RUNTIME_SHADER_ROOT}/${relative_path}")
    uvsr_normalize_absolute_path("${target_path}" target_path)
    uvsr_require_path_within_root(
        "${target_path}"
        "${UVSR_RUNTIME_SHADER_ROOT}"
        "Runtime shader target")
    uvsr_require_safe_existing_ancestor(
        "${target_path}"
        "${UVSR_RUNTIME_SHADER_ROOT}"
        "Runtime shader target")
    if (IS_SYMLINK "${target_path}")
        message(FATAL_ERROR
            "Refusing a symbolic-link runtime shader target: ${target_path}")
    elseif (EXISTS "${target_path}" AND IS_DIRECTORY "${target_path}")
        message(FATAL_ERROR
            "Runtime shader target is a directory: ${target_path}")
    elseif (NOT EXISTS "${target_path}")
        set(UVSR_RUNTIME_SHADER_SYNCHRONIZATION_REQUIRED true)
    else()
        file(SIZE "${target_path}" target_size)
        if (NOT target_size EQUAL source_size)
            set(UVSR_RUNTIME_SHADER_SYNCHRONIZATION_REQUIRED true)
        else()
            file(SHA256 "${source_path}" source_hash)
            file(SHA256 "${target_path}" target_hash)
            if (NOT source_hash STREQUAL target_hash)
                set(UVSR_RUNTIME_SHADER_SYNCHRONIZATION_REQUIRED true)
            endif()
        endif()
    endif()
endforeach()

file(GLOB_RECURSE
    UVSR_ACTUAL_RUNTIME_FILES
    LIST_DIRECTORIES false
    RELATIVE "${UVSR_RUNTIME_SHADER_ROOT}"
    "${UVSR_RUNTIME_SHADER_ROOT}/*")
list(SORT UVSR_EXPECTED_RELATIVE_PATHS)
list(SORT UVSR_ACTUAL_RUNTIME_FILES)
set(UVSR_UNEXPECTED_RUNTIME_FILES)
foreach(relative_path IN LISTS UVSR_ACTUAL_RUNTIME_FILES)
    set(actual_path "${UVSR_RUNTIME_SHADER_ROOT}/${relative_path}")
    uvsr_normalize_absolute_path("${actual_path}" actual_path)
    uvsr_require_path_within_root(
        "${actual_path}"
        "${UVSR_RUNTIME_SHADER_ROOT}"
        "Discovered runtime shader")
    if (IS_SYMLINK "${actual_path}")
        message(FATAL_ERROR
            "Refusing a symbolic-link runtime shader: ${actual_path}")
    endif()
    file(REAL_PATH "${actual_path}" resolved_actual_path)
    uvsr_normalize_absolute_path("${resolved_actual_path}" resolved_actual_path)
    uvsr_require_path_within_root(
        "${resolved_actual_path}"
        "${UVSR_RUNTIME_SHADER_ROOT}"
        "Resolved runtime shader")
    set(relative_path_key "${relative_path}")
    if (WIN32)
        string(TOLOWER "${relative_path_key}" relative_path_key)
    endif()
    if (NOT relative_path_key IN_LIST UVSR_EXPECTED_RELATIVE_PATH_KEYS)
        list(APPEND UVSR_UNEXPECTED_RUNTIME_FILES "${actual_path}")
        set(UVSR_RUNTIME_SHADER_SYNCHRONIZATION_REQUIRED true)
    else()
        uvsr_validate_relative_shader_path(
            "${relative_path}"
            "discovered runtime shader relative path")
    endif()
endforeach()

set(UVSR_EXPECTED_RUNTIME_DIRECTORIES)
set(UVSR_EXPECTED_RUNTIME_DIRECTORY_KEYS)
foreach(relative_path IN LISTS UVSR_EXPECTED_RELATIVE_PATHS)
    get_filename_component(relative_directory "${relative_path}" DIRECTORY)
    while(relative_directory)
        list(APPEND UVSR_EXPECTED_RUNTIME_DIRECTORIES "${relative_directory}")
        set(relative_directory_key "${relative_directory}")
        if (WIN32)
            string(TOLOWER "${relative_directory_key}" relative_directory_key)
        endif()
        list(APPEND UVSR_EXPECTED_RUNTIME_DIRECTORY_KEYS
            "${relative_directory_key}")
        get_filename_component(relative_directory
            "${relative_directory}" DIRECTORY)
    endwhile()
endforeach()
list(REMOVE_DUPLICATES UVSR_EXPECTED_RUNTIME_DIRECTORIES)
list(REMOVE_DUPLICATES UVSR_EXPECTED_RUNTIME_DIRECTORY_KEYS)
file(GLOB_RECURSE UVSR_RUNTIME_SHADER_ENTRIES
    LIST_DIRECTORIES true
    RELATIVE "${UVSR_RUNTIME_SHADER_ROOT}"
    "${UVSR_RUNTIME_SHADER_ROOT}/*")
set(UVSR_UNEXPECTED_RUNTIME_DIRECTORIES)
foreach(relative_entry IN LISTS UVSR_RUNTIME_SHADER_ENTRIES)
    set(entry_path "${UVSR_RUNTIME_SHADER_ROOT}/${relative_entry}")
    if (NOT IS_DIRECTORY "${entry_path}")
        continue()
    endif()
    if (IS_SYMLINK "${entry_path}")
        message(FATAL_ERROR
            "Refusing a symbolic-link runtime shader directory: ${entry_path}")
    endif()
    uvsr_normalize_absolute_path("${entry_path}" entry_path)
    uvsr_require_path_within_root(
        "${entry_path}"
        "${UVSR_RUNTIME_SHADER_ROOT}"
        "Runtime shader directory")
    set(relative_entry_key "${relative_entry}")
    if (WIN32)
        string(TOLOWER "${relative_entry_key}" relative_entry_key)
    endif()
    if (NOT relative_entry_key IN_LIST UVSR_EXPECTED_RUNTIME_DIRECTORY_KEYS)
        list(APPEND UVSR_UNEXPECTED_RUNTIME_DIRECTORIES "${entry_path}")
        set(UVSR_RUNTIME_SHADER_SYNCHRONIZATION_REQUIRED true)
    endif()
endforeach()

if (UVSR_RUNTIME_SHADER_SYNCHRONIZATION_REQUIRED)
    foreach(mapping IN LISTS UVSR_RUNTIME_SHADER_MAPPINGS)
        string(FIND "${mapping}" "|" separator)
        string(SUBSTRING "${mapping}" 0 ${separator} source_path)
        math(EXPR relative_start "${separator} + 1")
        string(SUBSTRING "${mapping}" ${relative_start} -1 relative_path)
        set(target_path "${UVSR_RUNTIME_SHADER_ROOT}/${relative_path}")
        get_filename_component(target_directory "${target_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${target_directory}")
        file(COPY_FILE
            "${source_path}"
            "${target_path}"
            ONLY_IF_DIFFERENT)
    endforeach()
    foreach(unexpected_file IN LISTS UVSR_UNEXPECTED_RUNTIME_FILES)
        file(REMOVE "${unexpected_file}")
    endforeach()
    list(SORT UVSR_UNEXPECTED_RUNTIME_DIRECTORIES ORDER DESCENDING)
    foreach(unexpected_directory IN LISTS UVSR_UNEXPECTED_RUNTIME_DIRECTORIES)
        if (EXISTS "${unexpected_directory}")
            file(REMOVE_RECURSE "${unexpected_directory}")
        endif()
    endforeach()
    file(TOUCH "${UVSR_RUNTIME_SHADER_STAGE_STAMP}")
    message(STATUS "Repaired the exact UVSR runtime shader bundle")
endif()
