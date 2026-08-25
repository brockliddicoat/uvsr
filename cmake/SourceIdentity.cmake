function(uvsr_compute_source_identity
        source_directory git_executable production
        identity_output clean_output commit_output)
    execute_process(
        COMMAND "${git_executable}" -C "${source_directory}" rev-parse HEAD
        RESULT_VARIABLE commit_result
        OUTPUT_VARIABLE commit
        ERROR_VARIABLE commit_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(LENGTH "${commit}" commit_length)
    if (NOT commit_result EQUAL 0 OR
        NOT commit MATCHES "^[0-9a-f]+$" OR NOT commit_length EQUAL 40)
        message(FATAL_ERROR
            "Cannot determine UVSR's full Git commit: ${commit_error}")
    endif()

    execute_process(
        COMMAND "${git_executable}" -C "${source_directory}"
            submodule foreach --recursive --quiet
            git status --porcelain=v1 --untracked-files=all
        RESULT_VARIABLE submodule_status_result
        OUTPUT_VARIABLE submodule_status
        ERROR_VARIABLE submodule_status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT submodule_status_result EQUAL 0)
        message(FATAL_ERROR
            "Cannot inspect UVSR's submodules: ${submodule_status_error}")
    endif()
    if (submodule_status)
        message(FATAL_ERROR
            "Dirty submodules are not valid UVSR build identity inputs")
    endif()

    execute_process(
        COMMAND "${git_executable}" -C "${source_directory}"
            status --porcelain=v1 --untracked-files=all --ignore-submodules=none
        RESULT_VARIABLE status_result
        OUTPUT_VARIABLE status
        ERROR_VARIABLE status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT status_result EQUAL 0)
        message(FATAL_ERROR
            "Cannot inspect UVSR's source-tree state: ${status_error}")
    endif()

    if (NOT status)
        set("${identity_output}" "${commit}" PARENT_SCOPE)
        set("${clean_output}" true PARENT_SCOPE)
        set("${commit_output}" "${commit}" PARENT_SCOPE)
        return()
    endif()
    if (production)
        message(FATAL_ERROR
            "Production configuration and packaging require a clean source tree")
    endif()

    execute_process(
        COMMAND "${git_executable}" -C "${source_directory}"
            diff --binary --submodule=diff HEAD --
        RESULT_VARIABLE diff_result
        OUTPUT_VARIABLE diff
        ERROR_VARIABLE diff_error)
    execute_process(
        COMMAND "${git_executable}" -C "${source_directory}"
            ls-files --others --exclude-standard
        RESULT_VARIABLE untracked_result
        OUTPUT_VARIABLE untracked
        ERROR_VARIABLE untracked_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT diff_result EQUAL 0 OR NOT untracked_result EQUAL 0)
        message(FATAL_ERROR
            "Cannot fingerprint UVSR's dirty source tree: "
            "${diff_error}${untracked_error}")
    endif()

    set(material "${status}\n--diff--\n${diff}\n--untracked--\n")
    string(REPLACE "\r\n" "\n" untracked "${untracked}")
    string(REPLACE "\n" ";" untracked_files "${untracked}")
    foreach(relative_path IN LISTS untracked_files)
        if (relative_path STREQUAL "")
            continue()
        endif()
        set(path "${source_directory}/${relative_path}")
        if (EXISTS "${path}" AND NOT IS_DIRECTORY "${path}")
            file(SHA256 "${path}" sha256)
            string(APPEND material "${relative_path}\t${sha256}\n")
        endif()
    endforeach()
    string(SHA256 dirty_sha256 "${material}")
    string(SUBSTRING "${dirty_sha256}" 0 12 dirty_discriminator)
    set("${identity_output}"
        "${commit}-dirty-${dirty_discriminator}" PARENT_SCOPE)
    set("${clean_output}" false PARENT_SCOPE)
    set("${commit_output}" "${commit}" PARENT_SCOPE)
endfunction()
