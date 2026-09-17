include_guard(GLOBAL)

function(uvsr_add_mapped_asset_stage target_name staging_root)
    set(asset_manifest
        "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.assets")
    set(asset_manifest_content "")
    set(staged_outputs)
    set(staged_outputs_insensitive)
    set(arguments ${ARGN})
    while(arguments)
        list(POP_FRONT arguments source_path)
        if (NOT arguments)
            message(FATAL_ERROR "${target_name} has an incomplete asset mapping")
        endif()
        list(POP_FRONT arguments relative_path)
        string(REPLACE "\\" "/" relative_path "${relative_path}")
        string(TOLOWER "${relative_path}" relative_path_insensitive)
        list(FIND staged_outputs_insensitive
            "${relative_path_insensitive}" duplicate_output_index)
        if (NOT duplicate_output_index EQUAL -1)
            message(FATAL_ERROR
                "${target_name} maps duplicate output ${relative_path}")
        endif()
        list(APPEND staged_outputs_insensitive
            "${relative_path_insensitive}")
        string(APPEND asset_manifest_content
            "${source_path}\t${relative_path}\n")
        set(staged_output "${staging_root}/${relative_path}")
        get_filename_component(staged_directory
            "${staged_output}" DIRECTORY)
        add_custom_command(
            OUTPUT "${staged_output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${staged_directory}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${source_path}" "${staged_output}"
            COMMAND "${CMAKE_COMMAND}" -E touch_nocreate
                "${staged_output}"
            DEPENDS "${source_path}"
            COMMENT "Staging ${relative_path}"
            VERBATIM)
        list(APPEND staged_outputs "${staged_output}")
    endwhile()
    file(GENERATE OUTPUT "${asset_manifest}"
        CONTENT "${asset_manifest_content}")
    set(asset_stamp
        "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.stamp")
    add_custom_command(
        OUTPUT "${asset_stamp}"
        COMMAND "${CMAKE_COMMAND}"
            "-DUVSR_ASSET_MANIFEST=${asset_manifest}"
            "-DUVSR_ASSET_STAGING_ROOT=${staging_root}"
            -DUVSR_ASSET_PURGE_ONLY=ON
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SyncMappedAssets.cmake"
        COMMAND "${CMAKE_COMMAND}" -E touch "${asset_stamp}"
        DEPENDS "${asset_manifest}" ${staged_outputs}
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SyncMappedAssets.cmake"
        COMMENT "Purging retired ${target_name} outputs"
        VERBATIM)
    add_custom_target("${target_name}" DEPENDS "${asset_stamp}")
endfunction()

