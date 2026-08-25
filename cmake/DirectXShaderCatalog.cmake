function(uvsr_expand_shader_line line output_variable)
    string(FIND "${line}" "{" opening)
    if (opening EQUAL -1)
        set("${output_variable}" "${line}" PARENT_SCOPE)
        return()
    endif()
    string(FIND "${line}" "}" closing)
    if (closing EQUAL -1 OR closing LESS opening)
        message(FATAL_ERROR "Malformed shader permutation: ${line}")
    endif()
    math(EXPR value_start "${opening} + 1")
    math(EXPR value_length "${closing} - ${value_start}")
    string(SUBSTRING "${line}" 0 ${opening} prefix)
    string(SUBSTRING "${line}" ${value_start} ${value_length} choices)
    math(EXPR suffix_start "${closing} + 1")
    string(SUBSTRING "${line}" ${suffix_start} -1 suffix)
    string(REPLACE "," ";" choices "${choices}")
    set(expanded)
    foreach(choice IN LISTS choices)
        uvsr_expand_shader_line(
            "${prefix}${choice}${suffix}" nested_expanded)
        list(APPEND expanded ${nested_expanded})
    endforeach()
    set("${output_variable}" "${expanded}" PARENT_SCOPE)
endfunction()

function(uvsr_add_direct_shader_bundle)
    set(options ENABLE_16BIT_TYPES PRESERVE_SOURCE_STAGE)
    set(one_value_arguments
        TARGET CONFIG SOURCE_DIRECTORY OUTPUT_DIRECTORY EXPECTED_TASKS
        OUTPUTS_VARIABLE OBJECTS_VARIABLE OUTPUT_FORMAT)
    set(multi_value_arguments
        INCLUDE_DIRECTORIES DEFINES COMPILER_OPTIONS EXCLUDE_SOURCES)
    cmake_parse_arguments(shader "${options}" "${one_value_arguments}"
        "${multi_value_arguments}" ${ARGN})
    foreach(required TARGET CONFIG SOURCE_DIRECTORY OUTPUT_DIRECTORY
            EXPECTED_TASKS OUTPUTS_VARIABLE)
        if (NOT shader_${required})
            message(FATAL_ERROR
                "uvsr_add_direct_shader_bundle omits ${required}")
        endif()
    endforeach()
    if (NOT shader_OUTPUT_FORMAT)
        set(shader_OUTPUT_FORMAT BINARY)
    endif()
    if (NOT shader_OUTPUT_FORMAT MATCHES "^(BINARY|HEADER)$")
        message(FATAL_ERROR
            "Unsupported shader bundle output format ${shader_OUTPUT_FORMAT}")
    endif()

    set(include_arguments)
    set(dependency_scan_arguments
        --include-directory "${shader_SOURCE_DIRECTORY}")
    foreach(include_directory IN LISTS shader_INCLUDE_DIRECTORIES)
        list(APPEND include_arguments -I "${include_directory}")
        list(APPEND dependency_scan_arguments
            --include-directory "${include_directory}")
    endforeach()
    set(global_define_arguments -D TARGET_D3D12)
    foreach(define IN LISTS shader_DEFINES)
        list(APPEND global_define_arguments -D "${define}")
    endforeach()
    set(enable_16bit_argument)
    if (shader_ENABLE_16BIT_TYPES)
        set(enable_16bit_argument -enable-16bit-types)
    endif()

    file(STRINGS "${shader_CONFIG}" config_lines)
    set(task_count 0)
    set(family_ids)
    set(active_generated_files)
    set(object_outputs)
    foreach(config_line IN LISTS config_lines)
        string(STRIP "${config_line}" config_line)
        if (config_line STREQUAL "" OR config_line MATCHES "^(#|//)")
            continue()
        endif()
        uvsr_expand_shader_line("${config_line}" expanded_lines)
        foreach(expanded_line IN LISTS expanded_lines)
            separate_arguments(tokens WINDOWS_COMMAND "${expanded_line}")
            list(POP_FRONT tokens source)
            list(FIND shader_EXCLUDE_SOURCES "${source}" excluded_index)
            if (NOT excluded_index EQUAL -1)
                continue()
            endif()
            math(EXPR task_count "${task_count} + 1")
            set(profile "")
            set(entry_point main)
            set(output_subdirectory "")
            set(output_suffix "")
            set(shader_model "${UVSR_SHADER_MODEL}")
            set(optimization 3)
            set(defines)
            set(compiler_options)
            while(tokens)
                list(POP_FRONT tokens option)
                if (option STREQUAL "-T" OR option STREQUAL "-E" OR
                    option STREQUAL "-D" OR option STREQUAL "-o" OR
                    option STREQUAL "-O" OR option STREQUAL "-s" OR
                    option STREQUAL "-m" OR
                    option STREQUAL "--compilerOptionsDXIL")
                    if (NOT tokens)
                        message(FATAL_ERROR
                            "Shader option ${option} has no value: ${expanded_line}")
                    endif()
                    list(POP_FRONT tokens value)
                else()
                    message(FATAL_ERROR
                        "Unsupported direct DXC shader option '${option}': "
                        "${expanded_line}")
                endif()
                if (option STREQUAL "-T")
                    set(profile "${value}")
                elseif(option STREQUAL "-E")
                    set(entry_point "${value}")
                elseif(option STREQUAL "-D")
                    list(APPEND defines "${value}")
                elseif(option STREQUAL "-o")
                    set(output_subdirectory "${value}")
                elseif(option STREQUAL "-O")
                    set(optimization "${value}")
                elseif(option STREQUAL "-s")
                    set(output_suffix "${value}")
                elseif(option STREQUAL "-m")
                    set(shader_model "${value}")
                elseif(option STREQUAL "--compilerOptionsDXIL")
                    separate_arguments(parsed_compiler_options
                        WINDOWS_COMMAND "${value}")
                    list(APPEND compiler_options ${parsed_compiler_options})
                endif()
            endwhile()
            if (NOT profile OR NOT optimization MATCHES "^[0-3]$" OR
                NOT shader_model MATCHES "^[0-9]+_[0-9]+$")
                message(FATAL_ERROR "Invalid direct DXC shader task: ${expanded_line}")
            endif()

            if (shader_PRESERVE_SOURCE_STAGE)
                cmake_path(GET source FILENAME shader_name)
                string(REGEX REPLACE "\\.[^.]+$" "" shader_name
                    "${shader_name}")
            else()
                cmake_path(GET source STEM shader_name)
            endif()
            cmake_path(GET source PARENT_PATH shader_parent)
            if (output_subdirectory)
                set(shader_parent "${output_subdirectory}")
            endif()
            if (NOT entry_point STREQUAL "main")
                string(APPEND shader_name "_${entry_point}")
            endif()
            string(APPEND shader_name "${output_suffix}")
            if (shader_parent)
                set(family "${shader_parent}/${shader_name}")
            else()
                set(family "${shader_name}")
            endif()
            string(REPLACE "\\" "/" family "${family}")
            string(SHA256 family_id "${family}")
            list(FIND family_ids "${family_id}" family_index)
            if (family_index EQUAL -1)
                list(APPEND family_ids "${family_id}")
                set("family_path_${family_id}" "${family}")
                set("family_rows_${family_id}" "")
                set("family_objects_${family_id}" "")
                set("family_keys_${family_id}" "")
            elseif(NOT "${family_path_${family_id}}" STREQUAL "${family}")
                message(FATAL_ERROR "Shader family hash collision")
            endif()

            set(sorted_defines ${defines})
            list(SORT sorted_defines)
            string(JOIN " " permutation_key ${sorted_defines})
            set(existing_keys ${family_keys_${family_id}})
            list(FIND existing_keys "${permutation_key}" key_index)
            if (NOT key_index EQUAL -1)
                message(FATAL_ERROR
                    "Duplicate shader permutation '${permutation_key}' in ${family}")
            endif()
            list(APPEND existing_keys "${permutation_key}")
            set("family_keys_${family_id}" "${existing_keys}")

            string(SHA256 task_id
                "${expanded_line};${shader_DEFINES};${shader_COMPILER_OPTIONS}")
            string(SUBSTRING "${task_id}" 0 16 task_id)
            set(object
                "${shader_OUTPUT_DIRECTORY}/objects/${family}.${task_id}.dxil")
            set(depfile "${object}.d")
            list(APPEND object_outputs "${object}")
            list(APPEND active_generated_files "${object}" "${depfile}")
            get_filename_component(object_directory "${object}" DIRECTORY)
            set(define_arguments ${global_define_arguments})
            foreach(define IN LISTS defines)
                list(APPEND define_arguments -D "${define}")
            endforeach()
            set(source_path "${shader_SOURCE_DIRECTORY}/${source}")
            set(dxc_arguments
                -nologo
                -Fo "${object}"
                -T "${profile}_${shader_model}"
                -E "${entry_point}"
                ${define_arguments}
                ${include_arguments}
                "-O${optimization}"
                ${enable_16bit_argument}
                ${shader_COMPILER_OPTIONS}
                ${compiler_options})
            add_custom_command(
                OUTPUT "${object}"
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${object_directory}"
                COMMAND $<TARGET_FILE:uvsr_shader_blob_builder>
                    --scan-dependencies
                    --source "${source_path}"
                    --target "${object}"
                    --depfile "${depfile}"
                    ${dependency_scan_arguments}
                COMMAND "${UVSR_DXC_EXECUTABLE}"
                    ${dxc_arguments}
                    "${source_path}"
                DEPENDS
                    uvsr_shader_blob_builder
                    "${source_path}"
                    "${UVSR_DXC_EXECUTABLE}"
                DEPFILE "${depfile}"
                WORKING_DIRECTORY "${shader_SOURCE_DIRECTORY}"
                COMMENT "DXC ${family} [${permutation_key}]"
                VERBATIM)
            string(APPEND "family_rows_${family_id}"
                "${permutation_key}\t${object}\t${depfile}\n")
            list(APPEND "family_objects_${family_id}" "${object}")
        endforeach()
    endforeach()

    if (NOT task_count EQUAL shader_EXPECTED_TASKS)
        message(FATAL_ERROR
            "${shader_CONFIG} expands to ${task_count} direct DXC tasks; "
            "expected ${shader_EXPECTED_TASKS}")
    endif()

    set(blob_outputs)
    foreach(family_id IN LISTS family_ids)
        set(family "${family_path_${family_id}}")
        set(header_arguments)
        if (shader_OUTPUT_FORMAT STREQUAL HEADER)
            set(blob "${shader_OUTPUT_DIRECTORY}/dxil/${family}.dxil.h")
            string(REGEX REPLACE "[^A-Za-z0-9_]" "_" header_stem "${family}")
            list(APPEND header_arguments
                --header-symbol "g_${header_stem}_dxil")
        else()
            set(blob "${shader_OUTPUT_DIRECTORY}/dxil/${family}.bin")
        endif()
        set(blob_depfile "${blob}.d")
        set(blob_stamp "${blob}.stamp")
        set(catalog "${shader_OUTPUT_DIRECTORY}/catalogs/${family_id}.txt")
        list(APPEND active_generated_files
            "${catalog}" "${blob}" "${blob_depfile}" "${blob_stamp}")
        get_filename_component(catalog_directory "${catalog}" DIRECTORY)
        get_filename_component(blob_directory "${blob}" DIRECTORY)
        file(MAKE_DIRECTORY "${catalog_directory}")
        # file(GENERATE) preserves the catalog timestamp when content is
        # unchanged, so a no-op configure cannot rebuild shader families.
        file(GENERATE OUTPUT "${catalog}"
            CONTENT "${family_rows_${family_id}}")
        add_custom_command(
            OUTPUT "${blob_stamp}"
            BYPRODUCTS "${blob}" "${blob_depfile}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${blob_directory}"
            COMMAND $<TARGET_FILE:uvsr_shader_blob_builder>
                --output "${blob}"
                --depfile "${blob_depfile}"
                --catalog "${catalog}"
                --working-directory "${shader_SOURCE_DIRECTORY}"
                ${header_arguments}
            COMMAND ${CMAKE_COMMAND} -E touch "${blob_stamp}"
            DEPENDS
                uvsr_shader_blob_builder
                "${catalog}"
                ${family_objects_${family_id}}
            COMMENT "Packing direct DXC shader family ${family}"
            VERBATIM)
        list(APPEND blob_stamps "${blob_stamp}")
        list(APPEND blob_outputs "${blob}")
    endforeach()
    file(GLOB_RECURSE existing_generated_files
        LIST_DIRECTORIES false
        "${shader_OUTPUT_DIRECTORY}/objects/*"
        "${shader_OUTPUT_DIRECTORY}/catalogs/*"
        "${shader_OUTPUT_DIRECTORY}/dxil/*")
    set(stale_generated_count 0)
    foreach(existing_file IN LISTS existing_generated_files)
        list(FIND active_generated_files "${existing_file}" active_index)
        if (active_index EQUAL -1)
            file(REMOVE "${existing_file}")
            math(EXPR stale_generated_count "${stale_generated_count} + 1")
        endif()
    endforeach()
    if (stale_generated_count GREATER 0)
        message(STATUS
            "${shader_TARGET}: purged ${stale_generated_count} retired generated files")
    endif()
    list(SORT blob_outputs)
    list(LENGTH family_ids family_count)
    add_custom_target("${shader_TARGET}" DEPENDS ${blob_stamps})
    set("${shader_OUTPUTS_VARIABLE}" "${blob_outputs}" PARENT_SCOPE)
    if (shader_OBJECTS_VARIABLE)
        set("${shader_OBJECTS_VARIABLE}" "${object_outputs}" PARENT_SCOPE)
    endif()
    message(STATUS
        "${shader_TARGET}: ${task_count} direct DXC tasks, "
        "${family_count} shader families")
endfunction()
