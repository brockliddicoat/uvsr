function(uvsr_read_runtime_shader_inventory inventory output_variable)
    set(expected_count 33)
    if (ARGC GREATER 2)
        set(expected_count "${ARGV2}")
    endif()
    if (NOT EXISTS "${inventory}" OR IS_DIRECTORY "${inventory}" OR
        IS_SYMLINK "${inventory}")
        message(FATAL_ERROR
            "Runtime shader inventory is not a regular file: ${inventory}")
    endif()
    file(STRINGS "${inventory}" paths)
    set(sorted_paths ${paths})
    list(SORT sorted_paths)
    list(REMOVE_DUPLICATES sorted_paths)
    list(LENGTH paths path_count)
    if (NOT path_count EQUAL expected_count OR
        NOT "${paths}" STREQUAL "${sorted_paths}")
        message(FATAL_ERROR
            "Runtime shader inventory must contain ${expected_count} unique sorted paths")
    endif()
    foreach(path IN LISTS paths)
        if (NOT path MATCHES
                "^bin/shaders/(framework|uvsr)/dxil/([A-Za-z0-9_]+/)*[A-Za-z0-9_]+\\.bin$")
            message(FATAL_ERROR
                "Invalid runtime shader inventory path: ${path}")
        endif()
    endforeach()
    set("${output_variable}" "${paths}" PARENT_SCOPE)
endfunction()

function(uvsr_read_runtime_asset_map
        asset_map package_paths_variable source_paths_variable)
    get_filename_component(source_root "${asset_map}/../.." ABSOLUTE)
    if (ARGC GREATER 3)
        set(source_root "${ARGV3}")
    endif()
    if (NOT EXISTS "${asset_map}" OR IS_DIRECTORY "${asset_map}" OR
        IS_SYMLINK "${asset_map}")
        message(FATAL_ERROR
            "Runtime asset map is not a regular file: ${asset_map}")
    endif()
    file(STRINGS "${asset_map}" rows)
    list(LENGTH rows row_count)
    if (NOT row_count EQUAL 309)
        message(FATAL_ERROR
            "Runtime asset map must contain exactly 309 rows")
    endif()

    set(package_paths)
    set(source_paths)
    set(previous_package_path "")
    foreach(row IN LISTS rows)
        string(FIND "${row}" "|" separator)
        if (separator LESS 1)
            message(FATAL_ERROR "Malformed runtime asset mapping: ${row}")
        endif()
        string(SUBSTRING "${row}" 0 ${separator} package_path)
        math(EXPR source_start "${separator} + 1")
        string(SUBSTRING "${row}" ${source_start} -1 source_path)
        string(FIND "${source_path}" "|" second_separator)
        if (NOT second_separator EQUAL -1 OR source_path STREQUAL "")
            message(FATAL_ERROR "Malformed runtime asset mapping: ${row}")
        endif()
        foreach(path IN ITEMS package_path source_path)
            if (${path} MATCHES [[\\|:|;|(^|/)\.\.?(/|$)|^/|/$|//]])
                message(FATAL_ERROR
                    "Unsafe runtime asset map path: ${${path}}")
            endif()
            set(normalized_path "${${path}}")
            cmake_path(NORMAL_PATH normalized_path)
            if (NOT normalized_path STREQUAL "${${path}}")
                message(FATAL_ERROR
                    "Non-canonical runtime asset map path: ${${path}}")
            endif()
        endforeach()
        if (NOT package_path MATCHES "^(media|bin/licenses)/" OR
            (NOT previous_package_path STREQUAL "" AND
                NOT package_path STRGREATER previous_package_path) OR
            package_path IN_LIST package_paths)
            message(FATAL_ERROR
                "Runtime asset map package paths must be unique and sorted")
        endif()
        if (NOT EXISTS "${source_root}/${source_path}" OR
            IS_DIRECTORY "${source_root}/${source_path}" OR
            IS_SYMLINK "${source_root}/${source_path}")
            message(FATAL_ERROR
                "Runtime asset map source is not a regular file: ${source_path}")
        endif()
        list(APPEND package_paths "${package_path}")
        list(APPEND source_paths "${source_root}/${source_path}")
        set(previous_package_path "${package_path}")
    endforeach()
    set("${package_paths_variable}" "${package_paths}" PARENT_SCOPE)
    set("${source_paths_variable}" "${source_paths}" PARENT_SCOPE)
endfunction()
