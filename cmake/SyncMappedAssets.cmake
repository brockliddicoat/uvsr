if (NOT DEFINED UVSR_ASSET_MANIFEST OR
    NOT DEFINED UVSR_ASSET_STAGING_ROOT)
    message(FATAL_ERROR "Asset synchronization arguments are missing")
endif()
cmake_path(ABSOLUTE_PATH UVSR_ASSET_STAGING_ROOT
    NORMALIZE OUTPUT_VARIABLE staging_root)
if (staging_root STREQUAL "" OR staging_root STREQUAL "/" OR
    staging_root MATCHES "^[A-Za-z]:/$")
    message(FATAL_ERROR "Asset staging root is unsafe")
endif()
file(MAKE_DIRECTORY "${staging_root}")
file(STRINGS "${UVSR_ASSET_MANIFEST}" rows)
set(allowed)
foreach(row IN LISTS rows)
    string(FIND "${row}" "\t" separator)
    if (separator LESS 1)
        message(FATAL_ERROR "Malformed asset mapping")
    endif()
    string(SUBSTRING "${row}" 0 ${separator} source)
    math(EXPR relative_start "${separator} + 1")
    string(SUBSTRING "${row}" ${relative_start} -1 relative)
    string(REPLACE "\\" "/" relative "${relative}")
    if (relative STREQUAL "" OR relative MATCHES "(^|/)\.\.(/|$)" OR
        relative MATCHES "^/" OR relative MATCHES ":")
        message(FATAL_ERROR "Unsafe staged asset path: ${relative}")
    endif()
    if (NOT EXISTS "${source}" OR IS_DIRECTORY "${source}")
        message(FATAL_ERROR "Mapped asset is missing: ${source}")
    endif()
    list(APPEND allowed "${relative}")
    if (NOT UVSR_ASSET_PURGE_ONLY)
        set(destination "${staging_root}/${relative}")
        get_filename_component(destination_directory "${destination}" DIRECTORY)
        file(MAKE_DIRECTORY "${destination_directory}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${source}" "${destination}"
            RESULT_VARIABLE copy_result)
        if (NOT copy_result EQUAL 0)
            message(FATAL_ERROR "Cannot stage ${relative}")
        endif()
        if (NOT EXISTS "${destination}" OR IS_DIRECTORY "${destination}")
            message(FATAL_ERROR "Staged asset is missing: ${relative}")
        endif()
        file(SHA256 "${source}" source_sha256)
        file(SHA256 "${destination}" destination_sha256)
        if (NOT source_sha256 STREQUAL destination_sha256)
            message(FATAL_ERROR
                "Staged asset differs from its source: ${relative}")
        endif()
    endif()
endforeach()
list(REMOVE_DUPLICATES allowed)

file(GLOB_RECURSE staged_files
    LIST_DIRECTORIES false
    RELATIVE "${staging_root}"
    "${staging_root}/*")
foreach(relative IN LISTS staged_files)
    string(REPLACE "\\" "/" relative "${relative}")
    list(FIND allowed "${relative}" allowed_index)
    if (allowed_index EQUAL -1)
        file(REMOVE "${staging_root}/${relative}")
    endif()
endforeach()
file(GLOB_RECURSE staged_entries
    LIST_DIRECTORIES true
    RELATIVE "${staging_root}"
    "${staging_root}/*")
list(SORT staged_entries ORDER DESCENDING)
foreach(relative IN LISTS staged_entries)
    set(directory "${staging_root}/${relative}")
    if (IS_DIRECTORY "${directory}")
        file(GLOB children "${directory}/*")
        if (NOT children)
            file(REMOVE_RECURSE "${directory}")
        endif()
    endif()
endforeach()
