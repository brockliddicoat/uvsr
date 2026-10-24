include_guard(GLOBAL)
find_package(Git REQUIRED)

function(uvsr_copy_lf source destination)
    file(READ "${source}" content)
    if (content MATCHES "@[A-Za-z_][A-Za-z0-9_]*@|#[ \t]*cmakedefine")
        message(FATAL_ERROR "Cannot LF-normalize dependency template: ${source}")
    endif()
    configure_file("${source}" "${destination}" @ONLY NEWLINE_STYLE LF)
endfunction()

# keep dependency sources pristine. apply reviewed patches only in the build tree.
function(uvsr_stage_patched_sources
    source_root
    patch_files
    output_root)
    cmake_path(ABSOLUTE_PATH output_root NORMALIZE)
    cmake_path(IS_PREFIX CMAKE_BINARY_DIR "${output_root}" NORMALIZE in_build_tree)
    if(NOT in_build_tree OR output_root STREQUAL CMAKE_BINARY_DIR)
        message(FATAL_ERROR "Dependency overrides must be below the build tree")
    endif()
    set(candidate_root "${output_root}.candidate")
    set(normalized_patch_root "${output_root}.patches")
    file(REMOVE_RECURSE "${candidate_root}")
    file(REMOVE_RECURSE "${normalized_patch_root}")
    file(MAKE_DIRECTORY "${candidate_root}")
    file(MAKE_DIRECTORY "${normalized_patch_root}")
    set(expected_files)
    foreach(relative_path IN LISTS ARGN)
        get_filename_component(relative_directory
            "${relative_path}" DIRECTORY)
        file(MAKE_DIRECTORY
            "${candidate_root}/${relative_directory}")
        uvsr_copy_lf("${source_root}/${relative_path}"
            "${candidate_root}/${relative_path}")
        list(APPEND expected_files "${relative_path}")
    endforeach()

    set(patch_index 0)
    foreach(patch_file IN LISTS patch_files)
        math(EXPR patch_index "${patch_index} + 1")
        set(normalized_patch_file "${normalized_patch_root}/${patch_index}.patch")
        uvsr_copy_lf("${patch_file}" "${normalized_patch_file}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env
                "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
                "${GIT_EXECUTABLE}"
                -c core.autocrlf=false
                -c core.eol=lf
                apply
                --no-index
                --unidiff-zero
                --whitespace=nowarn
                "${normalized_patch_file}"
            WORKING_DIRECTORY "${candidate_root}"
            RESULT_VARIABLE patch_result
            ERROR_VARIABLE patch_error)
        if (NOT patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to stage UVSR dependency override ${patch_file}: "
                "${patch_error}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY
            CMAKE_CONFIGURE_DEPENDS "${patch_file}")
    endforeach()
    file(GLOB_RECURSE existing_files
        LIST_DIRECTORIES false
        RELATIVE "${output_root}"
        "${output_root}/*")
    foreach(relative_path IN LISTS existing_files)
        list(FIND expected_files "${relative_path}" expected_index)
        if (expected_index EQUAL -1)
            file(REMOVE "${output_root}/${relative_path}")
        endif()
    endforeach()
    foreach(relative_path IN LISTS expected_files)
        get_filename_component(relative_directory
            "${relative_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${output_root}/${relative_directory}")
        configure_file(
            "${candidate_root}/${relative_path}"
            "${output_root}/${relative_path}"
            COPYONLY)
    endforeach()
    file(REMOVE_RECURSE "${candidate_root}")
    file(REMOVE_RECURSE "${normalized_patch_root}")
endfunction()

function(uvsr_replace_target_sources
    target
    source_root
    override_root)
    get_target_property(target_sources "${target}" SOURCES)
    foreach(relative_path IN LISTS ARGN)
        set(original_source "${source_root}/${relative_path}")
        list(FIND target_sources "${original_source}" source_index)
        set(source_to_remove "${original_source}")
        if (source_index EQUAL -1)
            # upstream targets may append sources with target-relative paths.
            list(FIND target_sources "${relative_path}" source_index)
            set(source_to_remove "${relative_path}")
        endif()
        if (source_index EQUAL -1)
            message(FATAL_ERROR
                "Cannot replace ${original_source} in target ${target}")
        endif()
        list(REMOVE_ITEM target_sources "${source_to_remove}")
        list(APPEND target_sources "${override_root}/${relative_path}")
    endforeach()
    set_property(TARGET "${target}" PROPERTY SOURCES "${target_sources}")

endfunction()

