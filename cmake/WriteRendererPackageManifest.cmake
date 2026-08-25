set(package_root "${CMAKE_INSTALL_PREFIX}")
set(engine "${package_root}/bin/uvsr-engine.exe")
if (NOT DEFINED UVSR_PACKAGE_PRODUCTION OR
    NOT UVSR_PACKAGE_PRODUCTION STREQUAL "true")
    message(FATAL_ERROR
        "Renderer package manifests may be written only by a production install")
endif()
if (NOT EXISTS "${engine}" OR IS_DIRECTORY "${engine}")
    message(FATAL_ERROR "Installed renderer package has no uvsr-engine.exe")
endif()
if (NOT DEFINED UVSR_RENDERER_RELEASE_SEQUENCE OR
    NOT UVSR_RENDERER_RELEASE_SEQUENCE MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "Renderer package release sequence is invalid")
endif()
math(EXPR release_sequence "${UVSR_RENDERER_RELEASE_SEQUENCE}")
if (release_sequence GREATER 9007199254740991)
    message(FATAL_ERROR "Renderer package release sequence exceeds safe range")
endif()

execute_process(
    COMMAND "${engine}" --identity-json
    RESULT_VARIABLE identity_result
    OUTPUT_VARIABLE identity_json
    ERROR_VARIABLE identity_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT identity_result EQUAL 0)
    message(FATAL_ERROR
        "Installed engine identity query failed: ${identity_error}")
endif()
foreach(field executable source_commit source_identity source_tree_clean
        production configuration settings_hash engine_version product_version)
    string(JSON "identity_${field}" ERROR_VARIABLE identity_json_error
        GET "${identity_json}" "${field}")
    if (identity_json_error)
        message(FATAL_ERROR
            "Installed engine omits ${field}: ${identity_json_error}")
    endif()
endforeach()
string(JSON identity_property_count ERROR_VARIABLE identity_json_error
    LENGTH "${identity_json}")
string(LENGTH "${identity_source_commit}" identity_source_commit_length)
string(LENGTH "${identity_settings_hash}" identity_settings_hash_length)
if (identity_json_error OR NOT identity_property_count EQUAL 9 OR
    NOT identity_executable STREQUAL "uvsr-engine.exe" OR
    NOT identity_source_commit_length EQUAL 40 OR
    NOT identity_source_commit MATCHES "^[0-9a-f]+$" OR
    NOT identity_source_identity STREQUAL identity_source_commit OR
    NOT identity_source_tree_clean OR NOT identity_production OR
    NOT identity_configuration STREQUAL "Release" OR
    NOT identity_settings_hash_length EQUAL 32 OR
    NOT identity_settings_hash MATCHES "^[0-9a-f]+$" OR
    NOT identity_engine_version MATCHES
        "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$" OR
    NOT identity_product_version STREQUAL
        "${identity_engine_version}+${identity_settings_hash}")
    message(FATAL_ERROR "Installed engine identity is not canonical")
endif()
set(settings_directory "${package_root}/bin/settings")
set(settings_contract
    "${settings_directory}/canonical-settings.json")
file(MAKE_DIRECTORY "${settings_directory}")
execute_process(
    COMMAND "${engine}" --settings-contract-json
    RESULT_VARIABLE settings_result
    OUTPUT_VARIABLE settings_json
    ERROR_VARIABLE settings_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT settings_result EQUAL 0)
    message(FATAL_ERROR
        "Installed engine settings-contract query failed: ${settings_error}")
endif()
foreach(field schemaVersion settingsHash engineVersion serializationPolicy entries)
    string(JSON "settings_${field}" ERROR_VARIABLE settings_json_error
        GET "${settings_json}" "${field}")
    if (settings_json_error)
        message(FATAL_ERROR
            "Settings contract omits ${field}: ${settings_json_error}")
    endif()
endforeach()
string(JSON settings_root_property_count ERROR_VARIABLE settings_json_error
    LENGTH "${settings_json}")
string(JSON settings_entry_count ERROR_VARIABLE settings_json_error
    LENGTH "${settings_json}" entries)
if (settings_json_error OR NOT settings_root_property_count EQUAL 5 OR
    NOT settings_schemaVersion EQUAL 7 OR
    NOT settings_settingsHash STREQUAL identity_settings_hash OR
    NOT settings_engineVersion STREQUAL identity_engine_version OR
    settings_serializationPolicy STREQUAL "" OR
    NOT settings_entry_count EQUAL 176)
    message(FATAL_ERROR
        "Installed engine settings contract does not match its identity")
endif()
set(settings_snapshot_count 0)
set(settings_session_count 0)
set(settings_previous_name "")
set(settings_allowed_kinds
    Boolean Integer Float Float3 Enum DynamicSelection Float4)
math(EXPR settings_last_entry "${settings_entry_count} - 1")
foreach(settings_index RANGE 0 ${settings_last_entry})
    string(JSON settings_property_count ERROR_VARIABLE settings_json_error
        LENGTH "${settings_json}" entries ${settings_index})
    foreach(field name kind persistence snapshotMember defaultValue domain)
        string(JSON "settings_${field}_type" ERROR_VARIABLE settings_json_error
            TYPE "${settings_json}" entries ${settings_index} "${field}")
        if (settings_json_error)
            message(FATAL_ERROR
                "Settings contract entry ${settings_index} omits ${field}: "
                "${settings_json_error}")
        endif()
        string(JSON "settings_${field}" ERROR_VARIABLE settings_json_error
            GET "${settings_json}" entries ${settings_index} "${field}")
    endforeach()
    if (NOT settings_property_count EQUAL 6 OR
        NOT settings_name_type STREQUAL "STRING" OR
        NOT settings_kind_type STREQUAL "STRING" OR
        NOT settings_persistence_type STREQUAL "STRING" OR
        NOT settings_snapshotMember_type STREQUAL "BOOLEAN" OR
        NOT settings_defaultValue_type STREQUAL "STRING" OR
        NOT settings_domain_type STREQUAL "STRING" OR
        settings_name STREQUAL "" OR
        (NOT settings_previous_name STREQUAL "" AND
            NOT settings_name STRGREATER settings_previous_name) OR
        NOT settings_kind IN_LIST settings_allowed_kinds)
        message(FATAL_ERROR
            "Settings contract entry ${settings_index} is not canonical")
    endif()
    if (settings_persistence STREQUAL "SnapshotCatalog" AND
        settings_snapshotMember)
        math(EXPR settings_snapshot_count "${settings_snapshot_count} + 1")
    elseif(settings_persistence STREQUAL "SessionOnly" AND
        NOT settings_snapshotMember)
        math(EXPR settings_session_count "${settings_session_count} + 1")
    else()
        message(FATAL_ERROR
            "Settings contract entry ${settings_index} has inconsistent persistence")
    endif()
    set(settings_previous_name "${settings_name}")
endforeach()
if (NOT settings_snapshot_count EQUAL 174 OR
    NOT settings_session_count EQUAL 2)
    message(FATAL_ERROR
        "Settings contract snapshot membership is not canonical")
endif()
file(WRITE "${settings_contract}" "${settings_json}\n")

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/runtime-shader-inventory.def"
    expected_shader_paths)
list(LENGTH expected_shader_paths expected_shader_count)
if (NOT expected_shader_count EQUAL 47)
    message(FATAL_ERROR "Runtime shader inventory is not canonical")
endif()
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/runtime-media-inventory.def"
    expected_protected_paths)
set(sorted_expected_protected_paths ${expected_protected_paths})
list(SORT sorted_expected_protected_paths)
list(REMOVE_DUPLICATES sorted_expected_protected_paths)
list(LENGTH expected_protected_paths expected_protected_count)
if (NOT expected_protected_count EQUAL 310 OR
    NOT "${expected_protected_paths}" STREQUAL
        "${sorted_expected_protected_paths}")
    message(FATAL_ERROR "Runtime media inventory is not canonical")
endif()

file(GLOB_RECURSE package_files
    LIST_DIRECTORIES false
    RELATIVE "${package_root}"
    "${package_root}/*")
list(FILTER package_files EXCLUDE REGEX "^package-manifest\\.json$")
list(SORT package_files)
list(LENGTH package_files package_file_count)
if (package_file_count LESS 5 OR package_file_count GREATER 100000)
    message(FATAL_ERROR "Renderer package file count is outside its safe range")
endif()

set(file_json "")
set(has_engine false)
set(has_d3d12_core false)
set(has_shader false)
set(has_license false)
set(has_media false)
set(has_settings false)
set(found_shader_paths)
set(found_protected_paths)
set(executable_sha256 "")
foreach(relative_path IN LISTS package_files)
    string(REPLACE "\\" "/" manifest_path "${relative_path}")
    if (manifest_path STREQUAL "bin/uvsr-engine.exe")
        set(has_engine true)
    elseif(manifest_path STREQUAL "bin/D3D12/D3D12Core.dll")
        set(has_d3d12_core true)
    elseif(manifest_path MATCHES "^bin/shaders/.+")
        list(FIND expected_shader_paths "${manifest_path}" shader_index)
        if (shader_index EQUAL -1)
            message(FATAL_ERROR
                "Unexpected renderer package shader: ${manifest_path}")
        endif()
        list(APPEND found_shader_paths "${manifest_path}")
        set(has_shader true)
    elseif(manifest_path MATCHES "^bin/licenses/.+")
        set(has_license true)
        if (manifest_path IN_LIST expected_protected_paths)
            list(APPEND found_protected_paths "${manifest_path}")
        endif()
    elseif(manifest_path STREQUAL "bin/settings/canonical-settings.json")
        set(has_settings true)
    elseif(manifest_path MATCHES "^media/.+")
        if (NOT manifest_path IN_LIST expected_protected_paths)
            message(FATAL_ERROR
                "Unexpected renderer package media: ${manifest_path}")
        endif()
        list(APPEND found_protected_paths "${manifest_path}")
        set(has_media true)
    else()
        message(FATAL_ERROR
            "Unexpected renderer package path: ${manifest_path}")
    endif()
    if (manifest_path MATCHES
            "(^|/)[^/]+\\.(py|pyc|ps1|cmd|bat|cmake|cpp|cxx|cc|h|hpp|hlsl|hlsli|pdb|ilk|lib|exp|obj|sln|vcxproj)$")
        message(FATAL_ERROR
            "Forbidden developer file in renderer package: ${manifest_path}")
    endif()
    set(absolute_path "${package_root}/${relative_path}")
    file(SIZE "${absolute_path}" file_size)
    file(SHA256 "${absolute_path}" file_sha256)
    string(TOLOWER "${file_sha256}" file_sha256)
    if (manifest_path STREQUAL "bin/uvsr-engine.exe")
        set(executable_sha256 "${file_sha256}")
    endif()
    if (file_json)
        string(APPEND file_json ",\n")
    endif()
    string(APPEND file_json
        "    {\"relativePath\":\"${manifest_path}\",\"size\":${file_size},"
        "\"sha256\":\"${file_sha256}\"}")
endforeach()
list(SORT found_shader_paths)
list(SORT found_protected_paths)
if (NOT "${found_shader_paths}" STREQUAL "${expected_shader_paths}")
    message(FATAL_ERROR "Renderer package shader inventory is incomplete")
endif()
if (NOT "${found_protected_paths}" STREQUAL "${expected_protected_paths}")
    message(FATAL_ERROR "Renderer package protected media is incomplete")
endif()
if (NOT has_engine OR NOT has_d3d12_core OR NOT has_shader OR
    NOT has_license OR NOT has_settings OR NOT has_media OR
    executable_sha256 STREQUAL "")
    message(FATAL_ERROR "Renderer package inventory is incomplete")
endif()

string(CONCAT manifest
    "{\n"
    "  \"schemaVersion\":1,\n"
    "  \"productId\":\"0c47a7a8-1ec4-4ffd-b6c4-2f7614181223\",\n"
    "  \"production\":true,\n"
    "  \"configuration\":\"Release\",\n"
    "  \"releaseSequence\":${release_sequence},\n"
    "  \"sourceCommit\":\"${identity_source_commit}\",\n"
    "  \"settingsHash\":\"${identity_settings_hash}\",\n"
    "  \"engineVersion\":\"${identity_engine_version}\",\n"
    "  \"executableSha256\":\"${executable_sha256}\",\n"
    "  \"files\":[\n${file_json}\n  ]\n"
    "}\n")
file(WRITE "${package_root}/package-manifest.json" "${manifest}")
message(STATUS
    "Wrote strict renderer package manifest sequence ${release_sequence} for "
    "${identity_source_commit} (${identity_settings_hash}, "
    "${identity_engine_version})")
