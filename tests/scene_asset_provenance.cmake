cmake_minimum_required(VERSION 3.24)

set(_uvsr_github_file_limit_bytes 100000000)
set(_uvsr_repack_buffer_limit_bytes 90000000)
set(_uvsr_scene_names
    intel_sponza
    bistro_interior_retextured
    san_miguel_retextured
    blender_classroom)

function(_uvsr_fail detail)
    message(FATAL_ERROR "Scene asset provenance check failed: ${detail}")
endfunction()

function(_uvsr_json_type output json label)
    string(JSON _type ERROR_VARIABLE _error TYPE "${json}" ${ARGN})
    if (NOT _error STREQUAL "NOTFOUND")
        _uvsr_fail("${label} is missing or malformed: ${_error}")
    endif()
    set(${output} "${_type}" PARENT_SCOPE)
endfunction()

function(_uvsr_json_get output json label)
    string(JSON _value ERROR_VARIABLE _error GET "${json}" ${ARGN})
    if (NOT _error STREQUAL "NOTFOUND")
        _uvsr_fail("${label} is missing or malformed: ${_error}")
    endif()
    set(${output} "${_value}" PARENT_SCOPE)
endfunction()

function(_uvsr_json_get_string output json label)
    _uvsr_json_type(_type "${json}" "${label}" ${ARGN})
    if (NOT _type STREQUAL "STRING")
        _uvsr_fail("${label} must be a JSON string, not ${_type}")
    endif()
    _uvsr_json_get(_value "${json}" "${label}" ${ARGN})
    set(${output} "${_value}" PARENT_SCOPE)
endfunction()

function(_uvsr_json_get_uint output json label)
    _uvsr_json_type(_type "${json}" "${label}" ${ARGN})
    if (NOT _type STREQUAL "NUMBER")
        _uvsr_fail("${label} must be a JSON number, not ${_type}")
    endif()
    _uvsr_json_get(_value "${json}" "${label}" ${ARGN})
    if (NOT _value MATCHES "^(0|[1-9][0-9]*)$")
        _uvsr_fail("${label} must be a non-negative decimal integer")
    endif()
    set(${output} "${_value}" PARENT_SCOPE)
endfunction()

function(_uvsr_json_length output json label expected_type)
    _uvsr_json_type(_type "${json}" "${label}" ${ARGN})
    if (NOT _type STREQUAL "${expected_type}")
        _uvsr_fail("${label} must be a JSON ${expected_type}, not ${_type}")
    endif()
    string(JSON _length ERROR_VARIABLE _error LENGTH "${json}" ${ARGN})
    if (NOT _error STREQUAL "NOTFOUND")
        _uvsr_fail("Could not read the length of ${label}: ${_error}")
    endif()
    set(${output} "${_length}" PARENT_SCOPE)
endfunction()

function(_uvsr_json_expect_string json label expected)
    _uvsr_json_get_string(_actual "${json}" "${label}" ${ARGN})
    if (NOT _actual STREQUAL "${expected}")
        _uvsr_fail("${label} must be '${expected}', but is '${_actual}'")
    endif()
endfunction()

function(_uvsr_json_expect_uint json label expected)
    _uvsr_json_get_uint(_actual "${json}" "${label}" ${ARGN})
    if (NOT _actual EQUAL expected)
        _uvsr_fail("${label} must be ${expected}, but is ${_actual}")
    endif()
endfunction()

function(_uvsr_json_expect_number json label expected)
    _uvsr_json_type(_type "${json}" "${label}" ${ARGN})
    if (NOT _type STREQUAL "NUMBER")
        _uvsr_fail("${label} must be a JSON number, not ${_type}")
    endif()
    _uvsr_json_get(_actual "${json}" "${label}" ${ARGN})
    if (NOT _actual STREQUAL "${expected}")
        _uvsr_fail("${label} must be ${expected}, but is ${_actual}")
    endif()
endfunction()

function(_uvsr_json_expect_bool json label expected)
    _uvsr_json_type(_type "${json}" "${label}" ${ARGN})
    if (NOT _type STREQUAL "BOOLEAN")
        _uvsr_fail("${label} must be a JSON boolean, not ${_type}")
    endif()
    _uvsr_json_get(_actual "${json}" "${label}" ${ARGN})
    if (expected)
        if (NOT _actual)
            _uvsr_fail("${label} must be true")
        endif()
    elseif (_actual)
        _uvsr_fail("${label} must be false")
    endif()
endfunction()

function(_uvsr_normalize_hex output value expected_length label)
    string(LENGTH "${value}" _length)
    string(TOUPPER "${value}" _upper)
    if (NOT _length EQUAL expected_length OR
        NOT _upper MATCHES "^[0-9A-F]+$")
        _uvsr_fail("${label} must be a ${expected_length}-character hexadecimal digest")
    endif()
    if (NOT value STREQUAL _upper)
        _uvsr_fail("${label} must use canonical uppercase hexadecimal")
    endif()
    set(${output} "${_upper}" PARENT_SCOPE)
endfunction()

function(_uvsr_json_expect_hash json label expected)
    _uvsr_json_get_string(_actual "${json}" "${label}" ${ARGN})
    _uvsr_normalize_hex(_actual_normalized "${_actual}" 64 "${label}")
    _uvsr_normalize_hex(_expected_normalized "${expected}" 64 "expected ${label}")
    if (NOT _actual_normalized STREQUAL _expected_normalized)
        _uvsr_fail("${label} does not match the audited source digest")
    endif()
endfunction()

function(_uvsr_validate_relative_path output value label)
    set(_path "${value}")
    if (_path STREQUAL "")
        _uvsr_fail("${label} must not be empty")
    endif()
    string(FIND "${_path}" ";" _semicolon)
    string(FIND "${_path}" ":" _colon)
    if (NOT _semicolon EQUAL -1 OR NOT _colon EQUAL -1 OR
        _path MATCHES [[\\]] OR _path MATCHES "[\r\n]")
        _uvsr_fail("${label} contains a forbidden path character: '${_path}'")
    endif()
    cmake_path(IS_ABSOLUTE _path _absolute)
    if (_absolute OR _path MATCHES "^/" OR _path MATCHES "/$" OR
        _path MATCHES "//" OR _path MATCHES [[(^|/)\.\.?(/|$)]])
        _uvsr_fail("${label} is not a safe normalized relative path: '${_path}'")
    endif()
    cmake_path(NORMAL_PATH _path OUTPUT_VARIABLE _normalized)
    if (NOT _normalized STREQUAL _path)
        _uvsr_fail("${label} is not normalized: '${_path}'")
    endif()
    set(${output} "${_normalized}" PARENT_SCOPE)
endfunction()

function(_uvsr_assert_case_unique list_variable label)
    set(_keys)
    set(_originals)
    foreach (_path IN LISTS ${list_variable})
        string(TOLOWER "${_path}" _key)
        list(FIND _keys "${_key}" _index)
        if (NOT _index EQUAL -1)
            list(GET _originals ${_index} _first)
            _uvsr_fail(
                "${label} has a case-insensitive collision between '${_first}' and '${_path}'")
        endif()
        list(APPEND _keys "${_key}")
        list(APPEND _originals "${_path}")
    endforeach()
endfunction()

function(_uvsr_assert_lists_equal expected_variable actual_variable label)
    list(LENGTH ${expected_variable} _expected_length)
    list(LENGTH ${actual_variable} _actual_length)
    if (NOT _expected_length EQUAL _actual_length)
        _uvsr_fail(
            "${label} count differs: expected ${_expected_length}, found ${_actual_length}")
    endif()
    if (_expected_length EQUAL 0)
        return()
    endif()
    math(EXPR _last "${_expected_length} - 1")
    foreach (_index RANGE 0 ${_last})
        list(GET ${expected_variable} ${_index} _expected)
        list(GET ${actual_variable} ${_index} _actual)
        if (NOT _expected STREQUAL _actual)
            _uvsr_fail(
                "${label} differs at entry ${_index}: expected '${_expected}', found '${_actual}'")
        endif()
    endforeach()
endfunction()

function(_uvsr_collect_scene_files root scene ignore_stage_stamp output)
    set(_scene_root "${root}/${scene}")
    if (NOT IS_DIRECTORY "${_scene_root}")
        _uvsr_fail("required scene directory is missing: ${_scene_root}")
    endif()
    file(GLOB_RECURSE _files
        LIST_DIRECTORIES false
        RELATIVE "${_scene_root}"
        "${_scene_root}/*")
    set(_validated)
    foreach (_file IN LISTS _files)
        file(TO_CMAKE_PATH "${_file}" _file)
        if (ignore_stage_stamp AND _file STREQUAL ".uvsr-stage.stamp")
            continue()
        endif()
        if (NOT ignore_stage_stamp AND _file STREQUAL ".uvsr-stage.stamp")
            _uvsr_fail("source scene '${scene}' contains a staging stamp")
        endif()
        _uvsr_validate_relative_path(_relative "${_file}" "${scene} inventory path")
        if (IS_SYMLINK "${_scene_root}/${_relative}")
            _uvsr_fail("scene inventories must not contain symlinks: ${scene}/${_relative}")
        endif()
        list(APPEND _validated "${_relative}")
    endforeach()
    list(SORT _validated)
    _uvsr_assert_case_unique(_validated "${scene} inventory")
    set(${output} "${_validated}" PARENT_SCOPE)
endfunction()

function(_uvsr_assert_file_sha256 path expected label)
    if (NOT EXISTS "${path}" OR IS_DIRECTORY "${path}" OR IS_SYMLINK "${path}")
        _uvsr_fail("${label} is not a regular file: ${path}")
    endif()
    file(SHA256 "${path}" _actual)
    string(TOUPPER "${_actual}" _actual)
    if (NOT _actual STREQUAL "${expected}")
        _uvsr_fail("${label} has SHA-256 ${_actual}, expected ${expected}")
    endif()
endfunction()

foreach (_required_variable IN ITEMS SOURCE_MODELS_ROOT STAGED_MODELS_ROOT)
    if (NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        _uvsr_fail("-${_required_variable}=<path> is required")
    endif()
endforeach()

set(_uvsr_source_models_root "${SOURCE_MODELS_ROOT}")
set(_uvsr_staged_models_root "${STAGED_MODELS_ROOT}")
cmake_path(ABSOLUTE_PATH _uvsr_source_models_root NORMALIZE
    OUTPUT_VARIABLE _uvsr_source_models_root)
cmake_path(ABSOLUTE_PATH _uvsr_staged_models_root NORMALIZE
    OUTPUT_VARIABLE _uvsr_staged_models_root)
if (NOT IS_DIRECTORY "${_uvsr_source_models_root}")
    _uvsr_fail("SOURCE_MODELS_ROOT is not a directory: ${_uvsr_source_models_root}")
endif()
if (NOT IS_DIRECTORY "${_uvsr_staged_models_root}")
    _uvsr_fail("STAGED_MODELS_ROOT is not a directory: ${_uvsr_staged_models_root}")
endif()

# These source bytes are independently hashed below. Prevent Git's text filters
# from rewriting the exact downloaded/license/glTF containers on checkout.
set(_uvsr_repository_root "${_uvsr_source_models_root}")
cmake_path(GET _uvsr_repository_root PARENT_PATH _uvsr_repository_root)
cmake_path(GET _uvsr_repository_root PARENT_PATH _uvsr_repository_root)
set(_uvsr_gitattributes_path "${_uvsr_repository_root}/.gitattributes")
if (NOT EXISTS "${_uvsr_gitattributes_path}")
    _uvsr_fail("repository .gitattributes is missing")
endif()
file(STRINGS "${_uvsr_gitattributes_path}" _uvsr_gitattributes_lines)
set(_uvsr_required_binary_attribute_rules
    "assets/scenes/bistro_interior_retextured/LICENSE.txt -diff -text"
    "assets/scenes/bistro_interior_retextured/SOURCE-README.txt -diff -text"
    "assets/scenes/bistro_interior_retextured/components/bistro_interior.gltf -diff -text"
    "assets/scenes/san_miguel_retextured/LICENSE.txt -diff -text"
    "assets/scenes/san_miguel_retextured/components/san_miguel.gltf -diff -text"
    "assets/scenes/blender_classroom/LICENSE.txt -diff -text"
    "assets/scenes/blender_classroom/SOURCE-README.txt -diff -text"
    "assets/scenes/blender_classroom/components/blender_classroom.gltf -diff -text"
    "assets/scenes/blender_classroom/components/blender-export-report.json -diff -text"
    "assets/scenes/blender_classroom/components/buffer-repack-report.json -diff -text"
    "tools/export_blender_classroom.py -diff -text")
foreach (_rule IN LISTS _uvsr_required_binary_attribute_rules)
    list(FIND _uvsr_gitattributes_lines "${_rule}" _rule_index)
    if (_rule_index EQUAL -1)
        _uvsr_fail(".gitattributes omits byte-preserving rule '${_rule}'")
    endif()
endforeach()

# Validate the two source-provenance manifests against independently audited bytes.
set(_uvsr_bistro_archive_sha256
    "0D50E3C724C6C5DA19F8EB99AD3F53E36FEC37FFA2DF9621F9CCF0603F3934E1")
set(_uvsr_bistro_model_sha256
    "47C71CF9FC3F0BBDF213F5788993BA5732565E7EACAE616DF26BC60818FDBC6A")
set(_uvsr_bistro_source_readme_sha256
    "C87C5B60992CEDEE49FCE1EA9BFE10CF60498EBF1113685B723D6DC9006C2BEF")
set(_uvsr_bistro_license_sha256
    "9A9EF3C33320EEBE6126B0C7DC327885806BDE242283BBE6E4AE77641AD703E4")
set(_uvsr_san_archive_sha256
    "85874077735808150E679B3C71D70A37A270CB8833F4911325AA1099DA3F7D4A")
set(_uvsr_san_model_sha256
    "22533258BE1D94AA1ECE29053E98F11C91EBC17A8A3626545DEE3CECF90B71E3")
set(_uvsr_san_material_sha256
    "5C0618AE58CEB61B51B09B97C5A16BBB410CCFFC671E99C9076A3926CEE04916")
set(_uvsr_san_license_sha256
    "708C9AD36ADAC62D13BD61DDF47D58D2B892B9E318BD87DA93AE9E55E2B5E680")

set(_uvsr_bistro_root
    "${_uvsr_source_models_root}/bistro_interior_retextured")
set(_uvsr_bistro_provenance_path "${_uvsr_bistro_root}/source-provenance.json")
if (NOT EXISTS "${_uvsr_bistro_provenance_path}")
    _uvsr_fail("Bistro source-provenance.json is missing")
endif()
file(READ "${_uvsr_bistro_provenance_path}" _uvsr_bistro_provenance)
_uvsr_json_expect_uint("${_uvsr_bistro_provenance}" "Bistro provenance schemaVersion" 1
    schemaVersion)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro provenance scene"
    "bistro_interior_retextured" scene)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro archiveFile"
    "Bistro_v5_2.zip" source archiveFile)
_uvsr_json_expect_uint("${_uvsr_bistro_provenance}" "Bistro archiveBytes" 894377473
    source archiveBytes)
_uvsr_json_expect_hash("${_uvsr_bistro_provenance}" "Bistro archiveSha256"
    "${_uvsr_bistro_archive_sha256}" source archiveSha256)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro archiveIndexUrl"
    "https://casual-effects.com/data" source archiveIndexUrl)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro upstreamProjectUrl"
    "https://developer.nvidia.com/orca/amazon-lumberyard-bistro"
    source upstreamProjectUrl)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro archiveRole"
    "supporting upstream ORCA source, citation, and license package; does not contain modelFile"
    source archiveRole)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro modelFile"
    "BistroInterior_Wine.glb" source modelFile)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro modelContainer"
    "user-supplied Blender-exported GLB associated with the McGuire archive entry"
    source modelContainer)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}"
    "Bistro modelRelationshipToArchive"
    "separate file; not a member of Bistro_v5_2.zip"
    source modelRelationshipToArchive)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro modelGenerator"
    "Khronos glTF Blender I/O v5.1.20" source modelGenerator)
_uvsr_json_expect_uint("${_uvsr_bistro_provenance}"
    "Bistro modelUvsrOrmRepairVersion" 4 source modelUvsrOrmRepairVersion)
_uvsr_json_expect_uint("${_uvsr_bistro_provenance}" "Bistro modelBytes" 421517664
    source modelBytes)
_uvsr_json_expect_hash("${_uvsr_bistro_provenance}" "Bistro modelSha256"
    "${_uvsr_bistro_model_sha256}" source modelSha256)
_uvsr_json_expect_hash("${_uvsr_bistro_provenance}" "Bistro sourceReadmeSha256"
    "${_uvsr_bistro_source_readme_sha256}" source sourceReadmeSha256)
_uvsr_json_expect_hash("${_uvsr_bistro_provenance}" "Bistro licenseSha256"
    "${_uvsr_bistro_license_sha256}" source licenseSha256)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro repack tool"
    "tools/repack_gltf_buffers.py" conversion tool)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro conversion method"
    "lossless buffer-view repack plus explicit opaque material-domain fallback"
    conversion method)
_uvsr_json_expect_uint("${_uvsr_bistro_provenance}" "Bistro maxBufferBytes"
    ${_uvsr_repack_buffer_limit_bytes} conversion maxBufferBytes)
_uvsr_json_expect_uint("${_uvsr_bistro_provenance}" "Bistro trackedFileLimitBytes"
    ${_uvsr_github_file_limit_bytes} conversion trackedFileLimitBytes)
_uvsr_json_expect_string("${_uvsr_bistro_provenance}" "Bistro repack report"
    "components/buffer-repack-report.json" conversion report)
_uvsr_json_expect_uint("${_uvsr_bistro_provenance}"
    "Bistro material fallback primitiveCount" 227
    conversion materialCompatibility primitiveCount)
_uvsr_json_expect_uint("${_uvsr_bistro_provenance}"
    "Bistro material fallback triangleCount" 109600
    conversion materialCompatibility triangleCount)
_uvsr_assert_file_sha256("${_uvsr_bistro_root}/SOURCE-README.txt"
    "${_uvsr_bistro_source_readme_sha256}" "bundled Bistro source README")
_uvsr_assert_file_sha256("${_uvsr_bistro_root}/LICENSE.txt"
    "${_uvsr_bistro_license_sha256}" "bundled Bistro license")

set(_uvsr_san_root "${_uvsr_source_models_root}/san_miguel_retextured")
set(_uvsr_san_provenance_path "${_uvsr_san_root}/source-provenance.json")
if (NOT EXISTS "${_uvsr_san_provenance_path}")
    _uvsr_fail("San Miguel source-provenance.json is missing")
endif()
file(READ "${_uvsr_san_provenance_path}" _uvsr_san_provenance)
_uvsr_json_expect_uint("${_uvsr_san_provenance}" "San Miguel provenance schemaVersion" 1
    schemaVersion)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel provenance scene"
    "san_miguel_retextured" scene)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel archiveFile"
    "San_Miguel.zip" source archiveFile)
_uvsr_json_expect_uint("${_uvsr_san_provenance}" "San Miguel archiveBytes" 535519642
    source archiveBytes)
_uvsr_json_expect_hash("${_uvsr_san_provenance}" "San Miguel archiveSha256"
    "${_uvsr_san_archive_sha256}" source archiveSha256)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel modelFile"
    "san-miguel.obj" source modelFile)
_uvsr_json_expect_uint("${_uvsr_san_provenance}" "San Miguel modelBytes" 1143041382
    source modelBytes)
_uvsr_json_expect_hash("${_uvsr_san_provenance}" "San Miguel modelSha256"
    "${_uvsr_san_model_sha256}" source modelSha256)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel materialLibraryFile"
    "san-miguel.mtl" source materialLibraryFile)
_uvsr_json_expect_uint("${_uvsr_san_provenance}" "San Miguel materialLibraryBytes" 35143
    source materialLibraryBytes)
_uvsr_json_expect_hash("${_uvsr_san_provenance}" "San Miguel materialLibrarySha256"
    "${_uvsr_san_material_sha256}" source materialLibrarySha256)
_uvsr_json_expect_hash("${_uvsr_san_provenance}" "San Miguel licenseSha256"
    "${_uvsr_san_license_sha256}" source licenseSha256)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel import tool"
    "tools/import_san_miguel.py" conversion importTool)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel Blender version"
    "5.1.2" conversion blenderVersion)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel Blender build hash"
    "ec6e62d40fa9" conversion blenderBuildHash)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel repack tool"
    "tools/repack_gltf_buffers.py" conversion repackTool)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel conversion method"
    "full-detail OBJ import with explicit opaque material-domain fallbacks followed by lossless buffer-view repack"
    conversion method)
_uvsr_json_expect_uint("${_uvsr_san_provenance}" "San Miguel maxBufferBytes"
    ${_uvsr_repack_buffer_limit_bytes} conversion maxBufferBytes)
_uvsr_json_expect_uint("${_uvsr_san_provenance}" "San Miguel trackedFileLimitBytes"
    ${_uvsr_github_file_limit_bytes} conversion trackedFileLimitBytes)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel import report"
    "blender-import-report.json" conversion importReport)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel repack report"
    "components/buffer-repack-report.json" conversion repackReport)
_uvsr_json_expect_uint("${_uvsr_san_provenance}"
    "San Miguel transmission fallback primitiveCount" 12
    conversion materialCompatibility transmissionPrimitiveCount)
_uvsr_json_expect_uint("${_uvsr_san_provenance}"
    "San Miguel transmission fallback triangleCount" 63910
    conversion materialCompatibility transmissionTriangleCount)
_uvsr_json_expect_string("${_uvsr_san_provenance}" "San Miguel camera commit"
    "30cf4a0346ae5a80a2d7a530a3ef7d0fa4f70572" camera repositoryCommit)
_uvsr_json_get_string(_uvsr_san_camera_blob "${_uvsr_san_provenance}"
    "San Miguel camera sourceBlobSha1" camera sourceBlobSha1)
string(LENGTH "${_uvsr_san_camera_blob}" _uvsr_san_camera_blob_length)
if (NOT _uvsr_san_camera_blob_length EQUAL 40 OR
    NOT _uvsr_san_camera_blob MATCHES "^[0-9a-f]+$" OR
    NOT _uvsr_san_camera_blob STREQUAL
        "3e442fb1f407316e6fb74cb066eddd5bf158ff9a")
    _uvsr_fail("San Miguel camera sourceBlobSha1 is not the audited PBRT source blob")
endif()
_uvsr_assert_file_sha256("${_uvsr_san_root}/LICENSE.txt"
    "${_uvsr_san_license_sha256}" "bundled San Miguel license")

# Validate the audited Blender import and retain its output identities for the
# independent buffer-repack report check below.
set(_uvsr_san_import_report_path "${_uvsr_san_root}/blender-import-report.json")
if (NOT EXISTS "${_uvsr_san_import_report_path}")
    _uvsr_fail("San Miguel Blender import report is missing")
endif()
file(READ "${_uvsr_san_import_report_path}" _uvsr_san_import_report)
_uvsr_json_expect_uint("${_uvsr_san_import_report}" "San Miguel import schemaVersion" 1
    schemaVersion)
_uvsr_json_expect_string("${_uvsr_san_import_report}" "San Miguel import scene"
    "san_miguel_retextured" scene)
_uvsr_json_expect_string("${_uvsr_san_import_report}" "San Miguel import Blender version"
    "5.1.2" blenderVersion)
_uvsr_json_expect_string("${_uvsr_san_import_report}" "San Miguel import Blender build hash"
    "ec6e62d40fa9" blenderBuildHash)
_uvsr_json_expect_string("${_uvsr_san_import_report}" "San Miguel imported source"
    "san-miguel.obj" source)
_uvsr_json_expect_uint("${_uvsr_san_import_report}" "San Miguel imported sourceBytes"
    1143041382 sourceBytes)
_uvsr_json_expect_hash("${_uvsr_san_import_report}" "San Miguel imported sourceSha256"
    "${_uvsr_san_model_sha256}" sourceSha256)
_uvsr_json_expect_string("${_uvsr_san_import_report}" "San Miguel imported material library"
    "san-miguel.mtl" materialLibrary)
_uvsr_json_expect_uint("${_uvsr_san_import_report}" "San Miguel imported materialLibraryBytes"
    35143 materialLibraryBytes)
_uvsr_json_expect_hash("${_uvsr_san_import_report}"
    "San Miguel imported materialLibrarySha256" "${_uvsr_san_material_sha256}"
    materialLibrarySha256)
_uvsr_json_expect_hash("${_uvsr_san_import_report}" "San Miguel imported licenseSha256"
    "${_uvsr_san_license_sha256}" licenseSha256)
_uvsr_json_expect_uint("${_uvsr_san_import_report}" "San Miguel exported material count"
    287 exportedMaterials materialCount)
_uvsr_json_length(_uvsr_san_transmission_fallback_count
    "${_uvsr_san_import_report}" "San Miguel transmissionFlattened" OBJECT
    materialChanges transmissionFlattened)
if (NOT _uvsr_san_transmission_fallback_count EQUAL 3)
    _uvsr_fail(
        "San Miguel transmissionFlattened must contain three audited materials")
endif()
_uvsr_json_expect_number("${_uvsr_san_import_report}"
    "San Miguel material_79 source transmission" "0.10000000149011612"
    materialChanges transmissionFlattened material_79)
_uvsr_json_expect_number("${_uvsr_san_import_report}"
    "San Miguel materialn source transmission" "0.30000001192092896"
    materialChanges transmissionFlattened materialn)
_uvsr_json_expect_number("${_uvsr_san_import_report}"
    "San Miguel materialo source transmission" "0.36666667461395264"
    materialChanges transmissionFlattened materialo)
_uvsr_json_length(_uvsr_san_exported_transmission_count
    "${_uvsr_san_import_report}" "San Miguel exported transmissionMaterials"
    ARRAY exportedMaterials transmissionMaterials)
if (NOT _uvsr_san_exported_transmission_count EQUAL 0)
    _uvsr_fail("San Miguel Blender export retains transmission materials")
endif()
_uvsr_json_length(_uvsr_san_base_color_texture_count "${_uvsr_san_import_report}"
    "San Miguel exported baseColorTextures" OBJECT exportedMaterials baseColorTextures)
if (NOT _uvsr_san_base_color_texture_count EQUAL 264)
    _uvsr_fail(
        "San Miguel exported baseColorTextures must contain 264 audited bindings, found ${_uvsr_san_base_color_texture_count}")
endif()
_uvsr_json_expect_uint("${_uvsr_san_import_report}" "San Miguel exportedTextureCount"
    269 exportedTextureCount)
_uvsr_json_get_uint(_uvsr_san_exported_texture_bytes "${_uvsr_san_import_report}"
    "San Miguel exportedTextureBytes" exportedTextureBytes)
_uvsr_json_get_uint(_uvsr_san_output_gltf_bytes "${_uvsr_san_import_report}"
    "San Miguel outputGltfBytes" outputGltfBytes)
_uvsr_json_get_string(_uvsr_san_output_gltf_sha256 "${_uvsr_san_import_report}"
    "San Miguel outputGltfSha256" outputGltfSha256)
_uvsr_normalize_hex(_uvsr_san_output_gltf_sha256 "${_uvsr_san_output_gltf_sha256}" 64
    "San Miguel outputGltfSha256")
_uvsr_json_expect_string("${_uvsr_san_import_report}" "San Miguel outputBuffer"
    "san_miguel.bin" outputBuffer)
_uvsr_json_get_uint(_uvsr_san_output_buffer_bytes "${_uvsr_san_import_report}"
    "San Miguel outputBufferBytes" outputBufferBytes)
_uvsr_json_get_string(_uvsr_san_output_buffer_sha256 "${_uvsr_san_import_report}"
    "San Miguel outputBufferSha256" outputBufferSha256)
_uvsr_normalize_hex(_uvsr_san_output_buffer_sha256 "${_uvsr_san_output_buffer_sha256}" 64
    "San Miguel outputBufferSha256")
_uvsr_json_get_uint(_uvsr_san_maximum_buffer_view_bytes "${_uvsr_san_import_report}"
    "San Miguel maximumBufferViewBytes" maximumBufferViewBytes)
if (_uvsr_san_output_gltf_bytes GREATER_EQUAL _uvsr_github_file_limit_bytes)
    _uvsr_fail("San Miguel intermediate glTF JSON unexpectedly exceeds the GitHub limit")
endif()
if (_uvsr_san_output_buffer_bytes LESS _uvsr_github_file_limit_bytes)
    _uvsr_fail("San Miguel import report no longer proves that buffer splitting was required")
endif()
if (_uvsr_san_maximum_buffer_view_bytes GREATER _uvsr_repack_buffer_limit_bytes)
    _uvsr_fail("San Miguel has a buffer view too large for the audited repack cap")
endif()

_uvsr_json_length(_uvsr_san_localized_image_count "${_uvsr_san_import_report}"
    "San Miguel localizedImages" ARRAY localizedImages)
if (NOT _uvsr_san_localized_image_count EQUAL 269)
    _uvsr_fail(
        "San Miguel localizedImages must contain 269 files, found ${_uvsr_san_localized_image_count}")
endif()
set(_uvsr_san_localized_images)
math(EXPR _uvsr_san_localized_image_last "${_uvsr_san_localized_image_count} - 1")
foreach (_index RANGE 0 ${_uvsr_san_localized_image_last})
    _uvsr_json_get_string(_image "${_uvsr_san_import_report}"
        "San Miguel localizedImages[${_index}]" localizedImages ${_index})
    _uvsr_validate_relative_path(_image "${_image}"
        "San Miguel localizedImages[${_index}]")
    list(APPEND _uvsr_san_localized_images "${_image}")
endforeach()
_uvsr_assert_case_unique(_uvsr_san_localized_images "San Miguel localizedImages")
list(SORT _uvsr_san_localized_images)

function(_uvsr_validate_classroom_reports)
    set(_scene "blender_classroom")
    set(_scene_root "${_uvsr_source_models_root}/${_scene}")
    set(_component_root "${_scene_root}/components")
    set(_provenance_path "${_scene_root}/source-provenance.json")
    set(_export_report_path "${_component_root}/blender-export-report.json")
    set(_repack_report_path "${_component_root}/buffer-repack-report.json")

    foreach (_required_path IN ITEMS
        "${_provenance_path}"
        "${_export_report_path}"
        "${_repack_report_path}"
        "${_component_root}/blender_classroom.gltf"
        "${_scene_root}/LICENSE.txt"
        "${_scene_root}/SOURCE-README.txt")
        if (NOT EXISTS "${_required_path}" OR IS_DIRECTORY "${_required_path}" OR
            IS_SYMLINK "${_required_path}")
            _uvsr_fail("Blender Classroom required file is missing: ${_required_path}")
        endif()
    endforeach()

    file(READ "${_provenance_path}" _provenance)
    _uvsr_json_type(_provenance_type "${_provenance}"
        "Blender Classroom provenance document")
    if (NOT _provenance_type STREQUAL "OBJECT")
        _uvsr_fail("Blender Classroom source-provenance.json must be a JSON object")
    endif()
    _uvsr_json_expect_uint("${_provenance}"
        "Blender Classroom provenance schemaVersion" 1 schemaVersion)
    _uvsr_json_expect_string("${_provenance}"
        "Blender Classroom provenance scene" "blender_classroom" scene)
    _uvsr_json_expect_uint("${_provenance}"
        "Blender Classroom archive-file source images" 19
        conversion sourceTextureRestoration archiveFileImagesCopiedByteForByte)
    _uvsr_json_expect_uint("${_provenance}"
        "Blender Classroom restored output images" 19
        conversion sourceTextureRestoration outputImages)
    _uvsr_json_expect_uint("${_provenance}"
        "Blender Classroom restored generated normals" 0
        conversion sourceTextureRestoration generatedNormalTextures)
    _uvsr_json_expect_uint("${_provenance}"
        "Blender Classroom packaged output images" 19
        conversion outputImageCount)
    _uvsr_json_length(_generated_source_image_count "${_provenance}"
        "Blender Classroom generated source image inventory" ARRAY
        conversion sourceTextureRestoration blenderGeneratedSourceImages)
    if (NOT _generated_source_image_count EQUAL 0)
        _uvsr_fail(
            "Blender Classroom must not package Blender-generated source images")
    endif()
    _uvsr_json_expect_string("${_provenance}"
        "Blender Classroom diagnostic source image" "checker"
        conversion sourceTextureRestoration diagnosticUvTestReplacement sourceImage)
    _uvsr_json_expect_string("${_provenance}"
        "Blender Classroom diagnostic source material" "drawing"
        conversion sourceTextureRestoration diagnosticUvTestReplacement sourceMaterial)
    _uvsr_json_expect_string("${_provenance}"
        "Blender Classroom blank-paper appearance reference" "drawing.004"
        conversion sourceTextureRestoration diagnosticUvTestReplacement
        appearanceReferenceMaterial)
    _uvsr_json_expect_uint("${_provenance}"
        "Blender Classroom diagnostic sheet count" 8
        conversion sourceTextureRestoration diagnosticUvTestReplacement
        affectedSheets)
    _uvsr_json_expect_bool("${_provenance}"
        "Blender Classroom diagnostic image packaging" false
        conversion sourceTextureRestoration diagnosticUvTestReplacement packaged)
    _uvsr_json_expect_uint("${_provenance}"
        "Blender Classroom provenance spawn-corner omitted triangles" 58320
        conversion geometry removedTriangles spawnCornerTrashCluster)
    _uvsr_json_expect_uint("${_provenance}"
        "Blender Classroom provenance exported triangles" 545830
        conversion geometry exportedTriangles)
    _uvsr_json_length(_output_domain_count "${_provenance}"
        "Blender Classroom provenance output domains" ARRAY
        conversion materialCompatibility outputDomains)
    if (NOT _output_domain_count EQUAL 1)
        _uvsr_fail(
            "Blender Classroom provenance must record one output material domain")
    endif()
    _uvsr_json_expect_string("${_provenance}"
        "Blender Classroom provenance output material domain" "OPAQUE"
        conversion materialCompatibility outputDomains 0)

    _uvsr_json_get_string(_manifest_export_tool_sha256 "${_provenance}"
        "Blender Classroom provenance export tool SHA-256"
        conversion exportToolSha256)
    _uvsr_normalize_hex(_manifest_export_tool_sha256
        "${_manifest_export_tool_sha256}" 64
        "Blender Classroom provenance export tool SHA-256")
    _uvsr_assert_file_sha256(
        "${_uvsr_repository_root}/tools/export_blender_classroom.py"
        "${_manifest_export_tool_sha256}"
        "Blender Classroom exporter")

    _uvsr_json_get_string(_manifest_export_report_sha256 "${_provenance}"
        "Blender Classroom provenance export report SHA-256"
        conversion exportReportSha256)
    _uvsr_normalize_hex(_manifest_export_report_sha256
        "${_manifest_export_report_sha256}" 64
        "Blender Classroom provenance export report SHA-256")
    _uvsr_assert_file_sha256(
        "${_export_report_path}"
        "${_manifest_export_report_sha256}"
        "Blender Classroom export report")
    _uvsr_json_get_uint(_manifest_export_report_bytes "${_provenance}"
        "Blender Classroom provenance export report bytes"
        conversion exportReportBytes)
    file(SIZE "${_export_report_path}" _actual_export_report_bytes)
    if (NOT _actual_export_report_bytes EQUAL _manifest_export_report_bytes)
        _uvsr_fail(
            "Blender Classroom export report has ${_actual_export_report_bytes} bytes, expected ${_manifest_export_report_bytes}")
    endif()

    _uvsr_json_get_string(_manifest_repack_report_sha256 "${_provenance}"
        "Blender Classroom provenance repack report SHA-256"
        conversion repackReportSha256)
    _uvsr_normalize_hex(_manifest_repack_report_sha256
        "${_manifest_repack_report_sha256}" 64
        "Blender Classroom provenance repack report SHA-256")
    _uvsr_assert_file_sha256(
        "${_repack_report_path}"
        "${_manifest_repack_report_sha256}"
        "Blender Classroom repack report")
    _uvsr_json_get_uint(_manifest_repack_report_bytes "${_provenance}"
        "Blender Classroom provenance repack report bytes"
        conversion repackReportBytes)
    file(SIZE "${_repack_report_path}" _actual_repack_report_bytes)
    if (NOT _actual_repack_report_bytes EQUAL _manifest_repack_report_bytes)
        _uvsr_fail(
            "Blender Classroom repack report has ${_actual_repack_report_bytes} bytes, expected ${_manifest_repack_report_bytes}")
    endif()

    file(READ "${_export_report_path}" _export_report)
    _uvsr_json_type(_export_report_type "${_export_report}"
        "Blender Classroom export report")
    if (NOT _export_report_type STREQUAL "OBJECT")
        _uvsr_fail("Blender Classroom export report must be a JSON object")
    endif()
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom export schemaVersion" 1 schemaVersion)
    _uvsr_json_expect_string("${_export_report}"
        "Blender Classroom export scene" "blender_classroom" scene)
    _uvsr_json_expect_string("${_export_report}"
        "Blender Classroom export displayName" "Blender Classroom" displayName)
    _uvsr_json_expect_string("${_export_report}"
        "Blender Classroom export Blender version" "5.1.2" blenderVersion)
    _uvsr_json_expect_string("${_export_report}"
        "Blender Classroom export Blender build hash" "ec6e62d40fa9"
        blenderBuildHash)
    _uvsr_json_expect_hash("${_export_report}"
        "Blender Classroom source blend hash"
        "5C526EA3F280566E80253673C9955640527CD0F247EA41B1742620B5BC39F7A4"
        source sha256)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom source frame triangles" 607484
        geometry sourceFrameOneTriangles)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom evaluated instances" 910
        geometry evaluatedInstances)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom evaluated mesh instances" 854
        geometry meshInstances)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom unique evaluated meshes" 400
        geometry uniqueEvaluatedMeshes)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom converted curve instances" 56
        geometry curveInstancesConverted)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted portal triangles" 6
        geometry droppedTrianglesByMaterial dayLight_portal)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted clock-glass triangles" 3328
        geometry droppedTrianglesByMaterial wallClock_Glass)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted spawn-corner triangles" 58320
        geometry omittedTrianglesTotal)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted Cylinder instances" 1
        geometry omittedInstancesBySourceObject Cylinder)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted Cylinder triangles" 768
        geometry omittedTrianglesBySourceObject Cylinder)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted Cylinder.056 instances" 1
        geometry omittedInstancesBySourceObject Cylinder.056)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted Cylinder.056 triangles" 880
        geometry omittedTrianglesBySourceObject Cylinder.056)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted Cylinder.057 instances" 1
        geometry omittedInstancesBySourceObject Cylinder.057)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted Cylinder.057 triangles" 3840
        geometry omittedTrianglesBySourceObject Cylinder.057)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted Paper instances" 13
        geometry omittedInstancesBySourceObject Paper)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom omitted Paper triangles" 52832
        geometry omittedTrianglesBySourceObject Paper)
    _uvsr_json_expect_string("${_export_report}"
        "Blender Classroom omitted hierarchy root" "dustBin"
        geometry omittedSourceHierarchy root)
    _uvsr_json_length(_omitted_owner_count "${_export_report}"
        "Blender Classroom omitted hierarchy owners" ARRAY
        geometry omittedSourceHierarchy owners)
    if (NOT _omitted_owner_count EQUAL 14)
        _uvsr_fail(
            "Blender Classroom omitted hierarchy must contain dustBin and 13 paper-ball owners")
    endif()
    _uvsr_json_length(_omitted_instance_count "${_export_report}"
        "Blender Classroom omitted hierarchy instances" ARRAY
        geometry omittedSourceHierarchy instances)
    if (NOT _omitted_instance_count EQUAL 16)
        _uvsr_fail(
            "Blender Classroom omitted hierarchy must contain 16 evaluated instances")
    endif()
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom expected output triangles" 545830
        geometry exportedTrianglesExpected)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom glTF triangles" 545830 gltf triangles)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom exported nodes" 876 gltf nodes)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom exported meshes" 381 gltf meshes)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom exported materials" 66 gltf materials)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom exported images" 19 gltf images)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom exported texture bindings" 36 gltf textures)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom exported buffer views" 1665 gltf bufferViews)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom exported accessors" 1665 gltf accessors)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom opaque material count" 66
        gltf alphaModes OPAQUE)
    _uvsr_json_length(_gltf_extension_count "${_export_report}"
        "Blender Classroom glTF extensions" ARRAY gltf extensionsUsed)
    if (NOT _gltf_extension_count EQUAL 0)
        _uvsr_fail(
            "Blender Classroom glTF must not require material extensions")
    endif()
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom exported camera count" 1 gltf cameras)
    _uvsr_json_expect_uint("${_export_report}"
        "Blender Classroom exported source buffer count" 1 gltf buffers)
    _uvsr_json_expect_bool("${_export_report}"
        "Blender Classroom source-image byte preservation" true
        conversion materialPolicy sourceBaseColorImagesCopiedByteForByte)
    _uvsr_json_expect_bool("${_export_report}"
        "Blender Classroom generated-normal policy" false
        conversion materialPolicy generatedNormalTextures)
    _uvsr_json_expect_bool("${_export_report}"
        "Blender Classroom generated UV-test packaging policy" false
        conversion materialPolicy sourceGeneratedUvTestImagesPackaged)
    _uvsr_json_expect_string("${_export_report}"
        "Blender Classroom diagnostic UV-test source image" "checker"
        conversion materialPolicy diagnosticUvTestImagePolicy drawing sourceImage)
    _uvsr_json_expect_string("${_export_report}"
        "Blender Classroom diagnostic UV-test appearance reference" "drawing.004"
        conversion materialPolicy diagnosticUvTestImagePolicy drawing
        appearanceReferenceMaterial)
    _uvsr_json_expect_bool("${_export_report}"
        "Blender Classroom diagnostic UV-test packaged state" false
        conversion materialPolicy diagnosticUvTestImagePolicy drawing packaged)

    _uvsr_json_length(_source_image_count "${_export_report}"
        "Blender Classroom source image inventory" ARRAY source images)
    set(_source_image_paths)
    set(_source_image_sizes)
    set(_source_image_hashes)
    math(EXPR _source_image_last "${_source_image_count} - 1")
    foreach (_index RANGE 0 ${_source_image_last})
        _uvsr_json_get_string(_source_path "${_export_report}"
            "Blender Classroom source image ${_index} path"
            source images ${_index} path)
        _uvsr_validate_relative_path(_source_path "${_source_path}"
            "Blender Classroom source image ${_index} path")
        _uvsr_json_get_uint(_source_bytes "${_export_report}"
            "Blender Classroom source image ${_index} bytes"
            source images ${_index} bytes)
        _uvsr_json_get_string(_source_sha256 "${_export_report}"
            "Blender Classroom source image ${_index} SHA-256"
            source images ${_index} sha256)
        _uvsr_normalize_hex(_source_sha256 "${_source_sha256}" 64
            "Blender Classroom source image ${_index} SHA-256")
        list(APPEND _source_image_paths "${_source_path}")
        list(APPEND _source_image_sizes "${_source_bytes}")
        list(APPEND _source_image_hashes "${_source_sha256}")
    endforeach()
    _uvsr_assert_case_unique(_source_image_paths
        "Blender Classroom source images")

    _uvsr_json_length(_source_export_count "${_export_report}"
        "Blender Classroom source image exports" ARRAY
        conversion sourceImageExports)
    if (NOT _source_export_count EQUAL 19)
        _uvsr_fail(
            "Blender Classroom export must record 19 source-authored images, found ${_source_export_count}")
    endif()
    set(_source_export_paths)
    set(_archive_image_count 0)
    set(_generated_source_image_count 0)
    math(EXPR _source_export_last "${_source_export_count} - 1")
    foreach (_index RANGE 0 ${_source_export_last})
        _uvsr_json_get_string(_output_path "${_export_report}"
            "Blender Classroom source image export ${_index} output path"
            conversion sourceImageExports ${_index} outputPath)
        if (NOT _output_path MATCHES "^components/(.+)$")
            _uvsr_fail(
                "Blender Classroom source image export is outside components: '${_output_path}'")
        endif()
        set(_component_path "${CMAKE_MATCH_1}")
        _uvsr_validate_relative_path(_component_path "${_component_path}"
            "Blender Classroom source image export ${_index} output path")
        if (_component_path MATCHES "_normal\\.png$")
            _uvsr_fail(
                "Blender Classroom source-texture package contains a generated normal: '${_component_path}'")
        endif()
        if (_component_path STREQUAL "textures/checker.png")
            _uvsr_fail(
                "Blender Classroom source-texture package must not contain the diagnostic checker")
        endif()
        list(APPEND _source_export_paths "${_component_path}")

        _uvsr_json_get_uint(_output_bytes "${_export_report}"
            "Blender Classroom source image export ${_index} bytes"
            conversion sourceImageExports ${_index} outputBytes)
        _uvsr_json_get_string(_output_sha256 "${_export_report}"
            "Blender Classroom source image export ${_index} SHA-256"
            conversion sourceImageExports ${_index} outputSha256)
        _uvsr_normalize_hex(_output_sha256 "${_output_sha256}" 64
            "Blender Classroom source image export ${_index} SHA-256")
        set(_actual_path "${_component_root}/${_component_path}")
        if (NOT EXISTS "${_actual_path}" OR IS_DIRECTORY "${_actual_path}" OR
            IS_SYMLINK "${_actual_path}")
            _uvsr_fail(
                "Blender Classroom source image export is missing: '${_component_path}'")
        endif()
        file(SIZE "${_actual_path}" _actual_bytes)
        file(SHA256 "${_actual_path}" _actual_sha256)
        string(TOUPPER "${_actual_sha256}" _actual_sha256)
        if (NOT _actual_bytes EQUAL _output_bytes OR
            NOT _actual_sha256 STREQUAL _output_sha256)
            _uvsr_fail(
                "Blender Classroom source image export metadata differs from '${_component_path}'")
        endif()

        _uvsr_json_get_string(_source_type "${_export_report}"
            "Blender Classroom source image export ${_index} type"
            conversion sourceImageExports ${_index} sourceType)
        if (_source_type STREQUAL "archiveFile")
            math(EXPR _archive_image_count "${_archive_image_count} + 1")
            _uvsr_json_get_string(_archive_path "${_export_report}"
                "Blender Classroom source image export ${_index} archive path"
                conversion sourceImageExports ${_index} sourcePath)
            _uvsr_json_get_uint(_archive_bytes "${_export_report}"
                "Blender Classroom source image export ${_index} archive bytes"
                conversion sourceImageExports ${_index} sourceBytes)
            _uvsr_json_get_string(_archive_sha256 "${_export_report}"
                "Blender Classroom source image export ${_index} archive SHA-256"
                conversion sourceImageExports ${_index} sourceSha256)
            _uvsr_normalize_hex(_archive_sha256 "${_archive_sha256}" 64
                "Blender Classroom source image export ${_index} archive SHA-256")
            list(FIND _source_image_paths "${_archive_path}" _archive_index)
            if (_archive_index EQUAL -1)
                _uvsr_fail(
                    "Blender Classroom exported image is absent from the archive inventory: '${_archive_path}'")
            endif()
            list(GET _source_image_sizes ${_archive_index} _expected_archive_bytes)
            list(GET _source_image_hashes ${_archive_index} _expected_archive_sha256)
            if (NOT _archive_bytes EQUAL _expected_archive_bytes OR
                NOT _archive_sha256 STREQUAL _expected_archive_sha256 OR
                NOT _output_bytes EQUAL _archive_bytes OR
                NOT _output_sha256 STREQUAL _archive_sha256)
                _uvsr_fail(
                    "Blender Classroom archive image was not copied byte-for-byte: '${_archive_path}'")
            endif()
        else()
            _uvsr_fail(
                "Blender Classroom source image export has unsupported type '${_source_type}'")
        endif()
    endforeach()
    _uvsr_assert_case_unique(_source_export_paths
        "Blender Classroom source image exports")
    if (NOT _archive_image_count EQUAL 19 OR
        NOT _generated_source_image_count EQUAL 0)
        _uvsr_fail(
            "Blender Classroom source image exports must contain 19 archive files and no Blender-generated images")
    endif()

    file(GLOB _packaged_normal_files LIST_DIRECTORIES false
        "${_component_root}/textures/*_normal.png")
    if (_packaged_normal_files)
        _uvsr_fail(
            "Blender Classroom source-texture package must not contain generated normal maps")
    endif()
    file(READ "${_component_root}/blender_classroom.gltf" _classroom_gltf)
    string(FIND "${_classroom_gltf}" "\"normalTexture\""
        _normal_texture_property)
    if (NOT _normal_texture_property EQUAL -1)
        _uvsr_fail(
            "Blender Classroom glTF must not bind generated normal textures")
    endif()
    _uvsr_json_length(_unused_material_count "${_export_report}"
        "Blender Classroom unused source materials" ARRAY gltf unusedSourceMaterials)
    set(_found_unused_cork false)
    if (_unused_material_count GREATER 0)
        math(EXPR _unused_material_last "${_unused_material_count} - 1")
        foreach (_index RANGE 0 ${_unused_material_last})
            _uvsr_json_get_string(_unused_material "${_export_report}"
                "Blender Classroom unused material ${_index}"
                gltf unusedSourceMaterials ${_index})
            if (_unused_material STREQUAL "cork")
                set(_found_unused_cork true)
            endif()
        endforeach()
    endif()
    if (NOT _found_unused_cork)
        _uvsr_fail(
            "Blender Classroom export does not audit cork as an unused source material")
    endif()

    _uvsr_json_length(_export_file_count "${_export_report}"
        "Blender Classroom Blender-export files" ARRAY files)
    set(_export_paths)
    set(_export_sizes)
    set(_export_hashes)
    set(_export_gltf_count 0)
    set(_export_buffer_count 0)
    math(EXPR _export_file_last "${_export_file_count} - 1")
    foreach (_index RANGE 0 ${_export_file_last})
        _uvsr_json_get_string(_path "${_export_report}"
            "Blender Classroom export files[${_index}].path" files ${_index} path)
        if (NOT _path MATCHES "^components/(.+)$")
            _uvsr_fail(
                "Blender Classroom export file is outside components: '${_path}'")
        endif()
        set(_path "${CMAKE_MATCH_1}")
        _uvsr_validate_relative_path(_path "${_path}"
            "Blender Classroom export files[${_index}].path")
        _uvsr_json_get_uint(_bytes "${_export_report}"
            "Blender Classroom export files[${_index}].bytes" files ${_index} bytes)
        _uvsr_json_get_string(_sha256 "${_export_report}"
            "Blender Classroom export files[${_index}].sha256" files ${_index} sha256)
        _uvsr_normalize_hex(_sha256 "${_sha256}" 64
            "Blender Classroom export files[${_index}].sha256")
        list(APPEND _export_paths "${_path}")
        list(APPEND _export_sizes "${_bytes}")
        list(APPEND _export_hashes "${_sha256}")
        if (_path STREQUAL "blender_classroom.gltf")
            math(EXPR _export_gltf_count "${_export_gltf_count} + 1")
            set(_export_gltf_bytes "${_bytes}")
            set(_export_gltf_sha256 "${_sha256}")
        elseif (_path STREQUAL "blender_classroom.bin")
            math(EXPR _export_buffer_count "${_export_buffer_count} + 1")
        endif()
    endforeach()
    _uvsr_assert_case_unique(_export_paths "Blender Classroom Blender-export files")
    if (NOT _export_gltf_count EQUAL 1 OR NOT _export_buffer_count EQUAL 1)
        _uvsr_fail(
            "Blender Classroom Blender export must contain one glTF and one source buffer")
    endif()

    file(READ "${_repack_report_path}" _repack_report)
    _uvsr_json_expect_uint("${_repack_report}"
        "Blender Classroom repack schemaVersion" 2 schemaVersion)
    _uvsr_json_expect_string("${_repack_report}"
        "Blender Classroom repack sourceContainer" "gltf" sourceContainer)
    _uvsr_json_expect_string("${_repack_report}"
        "Blender Classroom repack sourceId"
        "blender-classroom-cc0-20240926" sourceId)
    _uvsr_json_expect_uint("${_repack_report}"
        "Blender Classroom repack sourceContainerBytes"
        ${_export_gltf_bytes} sourceContainerBytes)
    _uvsr_json_expect_hash("${_repack_report}"
        "Blender Classroom repack sourceContainerSha256"
        "${_export_gltf_sha256}" sourceContainerSha256)
    _uvsr_json_expect_hash("${_repack_report}"
        "Blender Classroom repack sourceSha256"
        "${_export_gltf_sha256}" sourceSha256)
    _uvsr_json_expect_uint("${_repack_report}"
        "Blender Classroom repack sourceBufferCount" 1 sourceBufferCount)
    _uvsr_json_expect_uint("${_repack_report}"
        "Blender Classroom repack outputBufferCount" 1 outputBufferCount)
    _uvsr_json_expect_uint("${_repack_report}"
        "Blender Classroom repack maxBufferBytes"
        ${_uvsr_repack_buffer_limit_bytes} maxBufferBytes)
    _uvsr_json_expect_uint("${_repack_report}"
        "Blender Classroom repack trackedFileLimitBytes"
        ${_uvsr_github_file_limit_bytes} trackedFileLimitBytes)
    _uvsr_json_length(_external_image_count "${_repack_report}"
        "Blender Classroom copied images" ARRAY externalImagesCopied)
    if (NOT _external_image_count EQUAL 19)
        _uvsr_fail(
            "Blender Classroom repack report must copy 19 source images, found ${_external_image_count}")
    endif()
    _uvsr_json_length(_material_override_count "${_repack_report}"
        "Blender Classroom repack material overrides" ARRAY
        materialCompatibilityOverrides)
    if (NOT _material_override_count EQUAL 0)
        _uvsr_fail(
            "Blender Classroom renderer compatibility must be resolved before repacking")
    endif()

    _uvsr_json_length(_source_file_count "${_repack_report}"
        "Blender Classroom repack sourceFiles" ARRAY sourceFiles)
    if (NOT _source_file_count EQUAL _export_file_count)
        _uvsr_fail(
            "Blender Classroom repack source inventory differs from the Blender export")
    endif()
    set(_repack_source_paths)
    math(EXPR _source_file_last "${_source_file_count} - 1")
    foreach (_index RANGE 0 ${_source_file_last})
        _uvsr_json_get_string(_path "${_repack_report}"
            "Blender Classroom sourceFiles[${_index}].path"
            sourceFiles ${_index} path)
        _uvsr_validate_relative_path(_path "${_path}"
            "Blender Classroom sourceFiles[${_index}].path")
        _uvsr_json_get_uint(_bytes "${_repack_report}"
            "Blender Classroom sourceFiles[${_index}].bytes"
            sourceFiles ${_index} bytes)
        _uvsr_json_get_string(_sha256 "${_repack_report}"
            "Blender Classroom sourceFiles[${_index}].sha256"
            sourceFiles ${_index} sha256)
        _uvsr_normalize_hex(_sha256 "${_sha256}" 64
            "Blender Classroom sourceFiles[${_index}].sha256")
        list(FIND _export_paths "${_path}" _export_index)
        if (_export_index EQUAL -1)
            _uvsr_fail(
                "Blender Classroom repack source is absent from its export report: '${_path}'")
        endif()
        list(GET _export_sizes ${_export_index} _expected_bytes)
        list(GET _export_hashes ${_export_index} _expected_sha256)
        if (NOT _bytes EQUAL _expected_bytes OR
            NOT _sha256 STREQUAL _expected_sha256)
            _uvsr_fail(
                "Blender Classroom repack source differs from its export report: '${_path}'")
        endif()
        list(APPEND _repack_source_paths "${_path}")
    endforeach()
    _uvsr_assert_case_unique(_repack_source_paths
        "Blender Classroom repack sourceFiles")
    list(SORT _export_paths)
    list(SORT _repack_source_paths)
    _uvsr_assert_lists_equal(_export_paths _repack_source_paths
        "Blender Classroom Blender-export/repack source inventory")

    _uvsr_json_length(_output_file_count "${_repack_report}"
        "Blender Classroom repack files" ARRAY files)
    if (_output_file_count EQUAL 0)
        _uvsr_fail("Blender Classroom repack report has an empty file inventory")
    endif()
    set(_output_paths)
    set(_output_bin_count 0)
    set(_output_gltf_count 0)
    math(EXPR _output_file_last "${_output_file_count} - 1")
    foreach (_index RANGE 0 ${_output_file_last})
        _uvsr_json_get_string(_path "${_repack_report}"
            "Blender Classroom files[${_index}].path" files ${_index} path)
        _uvsr_validate_relative_path(_path "${_path}"
            "Blender Classroom files[${_index}].path")
        _uvsr_json_get_uint(_bytes "${_repack_report}"
            "Blender Classroom files[${_index}].bytes" files ${_index} bytes)
        _uvsr_json_get_string(_sha256 "${_repack_report}"
            "Blender Classroom files[${_index}].sha256" files ${_index} sha256)
        _uvsr_normalize_hex(_sha256 "${_sha256}" 64
            "Blender Classroom files[${_index}].sha256")
        if (_bytes GREATER_EQUAL _uvsr_github_file_limit_bytes)
            _uvsr_fail(
                "Blender Classroom output '${_path}' is too large for GitHub: ${_bytes} bytes")
        endif()

        set(_actual_path "${_component_root}/${_path}")
        if (NOT EXISTS "${_actual_path}" OR IS_DIRECTORY "${_actual_path}" OR
            IS_SYMLINK "${_actual_path}")
            _uvsr_fail("Blender Classroom report output is missing: ${_path}")
        endif()
        file(SIZE "${_actual_path}" _actual_bytes)
        file(SHA256 "${_actual_path}" _actual_sha256)
        string(TOUPPER "${_actual_sha256}" _actual_sha256)
        if (NOT _actual_bytes EQUAL _bytes OR NOT _actual_sha256 STREQUAL _sha256)
            _uvsr_fail(
                "Blender Classroom report metadata differs from '${_path}'")
        endif()

        list(APPEND _output_paths "${_path}")
        if (_path MATCHES "^buffers/[^/]+\\.bin$")
            math(EXPR _output_bin_count "${_output_bin_count} + 1")
            if (_bytes GREATER _uvsr_repack_buffer_limit_bytes)
                _uvsr_fail("Blender Classroom buffer exceeds the audited repack cap")
            endif()
        elseif (_path STREQUAL "blender_classroom.gltf")
            math(EXPR _output_gltf_count "${_output_gltf_count} + 1")
        endif()
    endforeach()
    _uvsr_assert_case_unique(_output_paths "Blender Classroom repack files")
    if (NOT _output_bin_count EQUAL 1 OR NOT _output_gltf_count EQUAL 1)
        _uvsr_fail(
            "Blender Classroom repack must contain one glTF and one external buffer")
    endif()

    file(GLOB_RECURSE _component_inventory
        LIST_DIRECTORIES false
        RELATIVE "${_component_root}"
        "${_component_root}/*")
    set(_component_outputs)
    foreach (_path IN LISTS _component_inventory)
        file(TO_CMAKE_PATH "${_path}" _path)
        _uvsr_validate_relative_path(_path "${_path}"
            "Blender Classroom component path")
        if (_path STREQUAL "buffer-repack-report.json" OR
            _path STREQUAL "blender-export-report.json")
            continue()
        endif()
        list(APPEND _component_outputs "${_path}")
    endforeach()
    _uvsr_assert_case_unique(_component_outputs
        "Blender Classroom component inventory")
    list(SORT _component_outputs)
    list(SORT _output_paths)
    _uvsr_assert_lists_equal(_component_outputs _output_paths
        "Blender Classroom report/component inventory")
endfunction()

_uvsr_validate_classroom_reports()

function(_uvsr_validate_repack_report
    scene expected_container expected_container_path expected_container_bytes
    expected_container_sha256 expected_archive_file expected_archive_sha256
    expected_source_sha256 expected_output_gltf expected_source_buffer
    expected_source_buffer_bytes expected_source_buffer_sha256
    expected_output_buffer_count)

    set(_component_root "${_uvsr_source_models_root}/${scene}/components")
    set(_report_path "${_component_root}/buffer-repack-report.json")
    if (NOT EXISTS "${_report_path}")
        _uvsr_fail("${scene} buffer-repack-report.json is missing")
    endif()
    file(READ "${_report_path}" _report)

    _uvsr_json_expect_uint("${_report}" "${scene} repack schemaVersion" 2 schemaVersion)
    _uvsr_json_expect_string("${_report}" "${scene} sourceContainer"
        "${expected_container}" sourceContainer)
    _uvsr_json_get_string(_source_id "${_report}" "${scene} sourceId" sourceId)
    foreach (_identity IN ITEMS
        "${expected_archive_file}"
        "${expected_archive_sha256}"
        "${expected_source_sha256}")
        string(FIND "${_source_id}" "${_identity}" _identity_index)
        if (_identity_index EQUAL -1)
            _uvsr_fail("${scene} sourceId omits audited identity '${_identity}'")
        endif()
    endforeach()
    _uvsr_json_expect_uint("${_report}" "${scene} sourceContainerBytes"
        ${expected_container_bytes} sourceContainerBytes)
    _uvsr_json_expect_hash("${_report}" "${scene} sourceContainerSha256"
        "${expected_container_sha256}" sourceContainerSha256)
    _uvsr_json_expect_hash("${_report}" "${scene} sourceSha256"
        "${expected_container_sha256}" sourceSha256)
    _uvsr_json_get_uint(_source_bytes "${_report}" "${scene} sourceBytes" sourceBytes)
    _uvsr_json_expect_uint("${_report}" "${scene} sourceBufferCount" 1 sourceBufferCount)
    _uvsr_json_get_uint(_buffer_view_count "${_report}"
        "${scene} bufferViewCount" bufferViewCount)
    _uvsr_json_get_uint(_copied_bytes "${_report}"
        "${scene} copiedBufferViewBytes" copiedBufferViewBytes)
    _uvsr_json_get_uint(_padding_bytes "${_report}"
        "${scene} alignmentPaddingBytes" alignmentPaddingBytes)
    _uvsr_json_expect_uint("${_report}" "${scene} maxBufferBytes"
        ${_uvsr_repack_buffer_limit_bytes} maxBufferBytes)
    _uvsr_json_expect_uint("${_report}" "${scene} trackedFileLimitBytes"
        ${_uvsr_github_file_limit_bytes} trackedFileLimitBytes)
    _uvsr_json_expect_uint("${_report}" "${scene} outputBufferCount"
        ${expected_output_buffer_count}
        outputBufferCount)
    if (_buffer_view_count EQUAL 0 OR _copied_bytes EQUAL 0)
        _uvsr_fail("${scene} repack report has no copied buffer-view payload")
    endif()

    _uvsr_json_length(_source_file_count "${_report}" "${scene} sourceFiles"
        ARRAY sourceFiles)
    if (_source_file_count EQUAL 0)
        _uvsr_fail("${scene} sourceFiles must not be empty")
    endif()
    set(_source_paths)
    set(_source_sizes)
    set(_source_hashes)
    set(_source_external_paths)
    set(_source_sum 0)
    set(_source_container_entries 0)
    set(_source_buffer_entries 0)
    math(EXPR _source_file_last "${_source_file_count} - 1")
    foreach (_index RANGE 0 ${_source_file_last})
        _uvsr_json_get_string(_path "${_report}"
            "${scene} sourceFiles[${_index}].path" sourceFiles ${_index} path)
        _uvsr_validate_relative_path(_path "${_path}"
            "${scene} sourceFiles[${_index}].path")
        _uvsr_json_get_uint(_bytes "${_report}"
            "${scene} sourceFiles[${_index}].bytes" sourceFiles ${_index} bytes)
        _uvsr_json_get_string(_sha256 "${_report}"
            "${scene} sourceFiles[${_index}].sha256" sourceFiles ${_index} sha256)
        _uvsr_normalize_hex(_sha256 "${_sha256}" 64
            "${scene} sourceFiles[${_index}].sha256")
        list(APPEND _source_paths "${_path}")
        list(APPEND _source_sizes "${_bytes}")
        list(APPEND _source_hashes "${_sha256}")
        math(EXPR _source_sum "${_source_sum} + ${_bytes}")
        if (_path STREQUAL expected_container_path)
            math(EXPR _source_container_entries "${_source_container_entries} + 1")
            if (NOT _bytes EQUAL expected_container_bytes OR
                NOT _sha256 STREQUAL expected_container_sha256)
                _uvsr_fail("${scene} source container entry does not match its audited bytes")
            endif()
        elseif (NOT expected_source_buffer STREQUAL "" AND
            _path STREQUAL expected_source_buffer)
            math(EXPR _source_buffer_entries "${_source_buffer_entries} + 1")
            if (NOT _bytes EQUAL expected_source_buffer_bytes OR
                NOT _sha256 STREQUAL expected_source_buffer_sha256)
                _uvsr_fail("${scene} source buffer entry does not match the Blender export")
            endif()
        else()
            list(APPEND _source_external_paths "${_path}")
        endif()
    endforeach()
    _uvsr_assert_case_unique(_source_paths "${scene} sourceFiles")
    if (NOT _source_sum EQUAL _source_bytes)
        _uvsr_fail(
            "${scene} sourceBytes is ${_source_bytes}, but sourceFiles account for ${_source_sum}")
    endif()
    if (NOT _source_container_entries EQUAL 1)
        _uvsr_fail("${scene} sourceFiles must contain its source container exactly once")
    endif()
    if (expected_container STREQUAL "gltf")
        if (NOT _source_buffer_entries EQUAL 1)
            _uvsr_fail("${scene} sourceFiles must contain its Blender buffer exactly once")
        endif()
    elseif (NOT _source_buffer_entries EQUAL 0)
        _uvsr_fail("${scene} GLB source unexpectedly lists an external source buffer")
    endif()

    _uvsr_json_length(_external_image_count "${_report}"
        "${scene} externalImagesCopied" ARRAY externalImagesCopied)
    set(_external_images)
    if (_external_image_count GREATER 0)
        math(EXPR _external_image_last "${_external_image_count} - 1")
        foreach (_index RANGE 0 ${_external_image_last})
            _uvsr_json_get_string(_path "${_report}"
                "${scene} externalImagesCopied[${_index}]"
                externalImagesCopied ${_index})
            _uvsr_validate_relative_path(_path "${_path}"
                "${scene} externalImagesCopied[${_index}]")
            list(APPEND _external_images "${_path}")
        endforeach()
    endif()
    _uvsr_assert_case_unique(_external_images "${scene} externalImagesCopied")
    list(SORT _external_images)
    list(SORT _source_external_paths)
    _uvsr_assert_lists_equal(_external_images _source_external_paths
        "${scene} copied-image source inventory")

    if (scene STREQUAL "bistro_interior_retextured")
        if (NOT _external_image_count EQUAL 0 OR NOT _source_file_count EQUAL 1)
            _uvsr_fail("Bistro's embedded GLB must have one source file and no external images")
        endif()
    elseif (scene STREQUAL "san_miguel_retextured")
        if (NOT _external_image_count EQUAL 269)
            _uvsr_fail("San Miguel repack report must copy all 269 localized images")
        endif()
        _uvsr_assert_lists_equal(_uvsr_san_localized_images _external_images
            "San Miguel Blender/repack localized image inventory")
    endif()

    _uvsr_json_length(_extension_count "${_report}" "${scene} extensionsUsed"
        ARRAY extensionsUsed)
    set(_extensions)
    if (_extension_count GREATER 0)
        math(EXPR _extension_last "${_extension_count} - 1")
        foreach (_index RANGE 0 ${_extension_last})
            _uvsr_json_get_string(_extension "${_report}"
                "${scene} extensionsUsed[${_index}]" extensionsUsed ${_index})
            if (NOT _extension MATCHES "^[A-Za-z0-9_]+$")
                _uvsr_fail("${scene} has malformed extension name '${_extension}'")
            endif()
            list(APPEND _extensions "${_extension}")
        endforeach()
    endif()
    _uvsr_assert_case_unique(_extensions "${scene} extensionsUsed")

    _uvsr_json_length(_material_override_count "${_report}"
        "${scene} materialCompatibilityOverrides" ARRAY
        materialCompatibilityOverrides)
    if (scene STREQUAL "bistro_interior_retextured")
        if (NOT _extension_count EQUAL 0)
            _uvsr_fail("Bistro output unexpectedly declares glTF extensions")
        endif()
        if (NOT _material_override_count EQUAL 5)
            _uvsr_fail("Bistro must audit exactly five opaque material fallbacks")
        endif()
        set(_expected_override_indices 0 1 27 72 73)
        set(_expected_override_names Water Ice Beer Red_Wine White_Wine)
        set(_expected_override_primitives 39 117 6 33 32)
        foreach (_index RANGE 0 4)
            list(GET _expected_override_indices ${_index} _expected_material_index)
            list(GET _expected_override_names ${_index} _expected_material_name)
            list(GET _expected_override_primitives ${_index} _expected_primitives)
            _uvsr_json_expect_uint("${_report}"
                "Bistro material override ${_index} index"
                ${_expected_material_index}
                materialCompatibilityOverrides ${_index} materialIndex)
            _uvsr_json_expect_string("${_report}"
                "Bistro material override ${_index} name"
                "${_expected_material_name}"
                materialCompatibilityOverrides ${_index} materialName)
            _uvsr_json_expect_uint("${_report}"
                "Bistro material override ${_index} primitiveCount"
                ${_expected_primitives}
                materialCompatibilityOverrides ${_index} primitiveCount)
            _uvsr_json_expect_string("${_report}"
                "Bistro material override ${_index} sourceAlphaMode" "BLEND"
                materialCompatibilityOverrides ${_index} sourceAlphaMode)
            _uvsr_json_expect_string("${_report}"
                "Bistro material override ${_index} outputAlphaMode" "OPAQUE"
                materialCompatibilityOverrides ${_index} outputAlphaMode)
            _uvsr_json_length(_removed_extension_count "${_report}"
                "Bistro material override ${_index} removedExtensions" OBJECT
                materialCompatibilityOverrides ${_index} removedExtensions)
            if (NOT _removed_extension_count EQUAL 0)
                _uvsr_fail(
                    "Bistro override ${_index} unexpectedly removes an extension")
            endif()
            _uvsr_json_get(_source_alpha "${_report}"
                "Bistro material override ${_index} sourceBaseColorAlpha"
                materialCompatibilityOverrides ${_index} sourceBaseColorAlpha)
            _uvsr_json_get(_output_alpha "${_report}"
                "Bistro material override ${_index} outputBaseColorAlpha"
                materialCompatibilityOverrides ${_index} outputBaseColorAlpha)
            if (NOT _source_alpha STREQUAL _output_alpha)
                _uvsr_fail(
                    "Bistro override ${_index} changes its authored base alpha")
            endif()
        endforeach()
    elseif (scene STREQUAL "san_miguel_retextured")
        set(_expected_extensions KHR_materials_specular KHR_materials_ior)
        _uvsr_assert_lists_equal(_expected_extensions _extensions
            "San Miguel renderer-compatible extensions")
        if (NOT _material_override_count EQUAL 0)
            _uvsr_fail(
                "San Miguel material fallbacks must occur in the Blender importer")
        endif()
    endif()

    _uvsr_json_length(_output_file_count "${_report}" "${scene} files" ARRAY files)
    if (_output_file_count EQUAL 0)
        _uvsr_fail("${scene} repack output file inventory must not be empty")
    endif()
    set(_output_paths)
    set(_output_sizes)
    set(_output_hashes)
    set(_output_external_paths)
    set(_output_bin_count 0)
    set(_output_bin_bytes 0)
    set(_output_gltf_count 0)
    set(_output_external_bytes 0)
    math(EXPR _output_file_last "${_output_file_count} - 1")
    foreach (_index RANGE 0 ${_output_file_last})
        _uvsr_json_get_string(_path "${_report}"
            "${scene} files[${_index}].path" files ${_index} path)
        _uvsr_validate_relative_path(_path "${_path}"
            "${scene} files[${_index}].path")
        _uvsr_json_get_uint(_bytes "${_report}"
            "${scene} files[${_index}].bytes" files ${_index} bytes)
        _uvsr_json_get_string(_sha256 "${_report}"
            "${scene} files[${_index}].sha256" files ${_index} sha256)
        _uvsr_normalize_hex(_sha256 "${_sha256}" 64
            "${scene} files[${_index}].sha256")
        if (_bytes GREATER_EQUAL _uvsr_github_file_limit_bytes)
            _uvsr_fail(
                "${scene} report output '${_path}' is ${_bytes} bytes; every file must be strictly below ${_uvsr_github_file_limit_bytes}")
        endif()

        set(_actual_path "${_component_root}/${_path}")
        if (NOT EXISTS "${_actual_path}" OR IS_DIRECTORY "${_actual_path}" OR
            IS_SYMLINK "${_actual_path}")
            _uvsr_fail("${scene} report output is not a regular file: ${_path}")
        endif()
        file(SIZE "${_actual_path}" _actual_bytes)
        file(SHA256 "${_actual_path}" _actual_sha256)
        string(TOUPPER "${_actual_sha256}" _actual_sha256)
        if (NOT _actual_bytes EQUAL _bytes OR NOT _actual_sha256 STREQUAL _sha256)
            _uvsr_fail(
                "${scene} report metadata does not match actual output '${_path}'")
        endif()

        list(APPEND _output_paths "${_path}")
        list(APPEND _output_sizes "${_bytes}")
        list(APPEND _output_hashes "${_sha256}")
        if (_path MATCHES "^buffers/[^/]+\\.bin$")
            math(EXPR _output_bin_count "${_output_bin_count} + 1")
            math(EXPR _output_bin_bytes "${_output_bin_bytes} + ${_bytes}")
            if (_bytes GREATER _uvsr_repack_buffer_limit_bytes)
                _uvsr_fail("${scene} output buffer '${_path}' exceeds maxBufferBytes")
            endif()
        elseif (_path STREQUAL expected_output_gltf)
            math(EXPR _output_gltf_count "${_output_gltf_count} + 1")
        else()
            list(APPEND _output_external_paths "${_path}")
            math(EXPR _output_external_bytes "${_output_external_bytes} + ${_bytes}")
        endif()
    endforeach()
    _uvsr_assert_case_unique(_output_paths "${scene} report files")
    if (NOT _output_bin_count EQUAL expected_output_buffer_count)
        _uvsr_fail(
            "${scene} report declares ${expected_output_buffer_count} buffers but lists ${_output_bin_count}")
    endif()
    if (NOT _output_gltf_count EQUAL 1)
        _uvsr_fail("${scene} report must list '${expected_output_gltf}' exactly once")
    endif()
    math(EXPR _accounted_buffer_bytes "${_copied_bytes} + ${_padding_bytes}")
    if (NOT _output_bin_bytes EQUAL _accounted_buffer_bytes)
        _uvsr_fail(
            "${scene} output buffers total ${_output_bin_bytes} bytes, but copied bytes plus alignment padding total ${_accounted_buffer_bytes}")
    endif()
    list(SORT _output_external_paths)
    _uvsr_assert_lists_equal(_external_images _output_external_paths
        "${scene} copied-image output inventory")

    foreach (_external_image IN LISTS _external_images)
        list(FIND _source_paths "${_external_image}" _source_index)
        list(FIND _output_paths "${_external_image}" _output_index)
        if (_source_index EQUAL -1 OR _output_index EQUAL -1)
            _uvsr_fail("${scene} copied image '${_external_image}' is missing metadata")
        endif()
        list(GET _source_sizes ${_source_index} _source_size)
        list(GET _source_hashes ${_source_index} _source_hash)
        list(GET _output_sizes ${_output_index} _output_size)
        list(GET _output_hashes ${_output_index} _output_hash)
        if (NOT _source_size EQUAL _output_size OR NOT _source_hash STREQUAL _output_hash)
            _uvsr_fail("${scene} copied image '${_external_image}' changed during repacking")
        endif()
    endforeach()
    if (scene STREQUAL "san_miguel_retextured" AND
        NOT _output_external_bytes EQUAL _uvsr_san_exported_texture_bytes)
        _uvsr_fail(
            "San Miguel copied images total ${_output_external_bytes} bytes, not the Blender-reported ${_uvsr_san_exported_texture_bytes}")
    endif()

    file(GLOB_RECURSE _component_inventory
        LIST_DIRECTORIES false
        RELATIVE "${_component_root}"
        "${_component_root}/*")
    set(_component_outputs)
    foreach (_path IN LISTS _component_inventory)
        file(TO_CMAKE_PATH "${_path}" _path)
        _uvsr_validate_relative_path(_path "${_path}" "${scene} component path")
        if (_path STREQUAL "buffer-repack-report.json" OR
            _path STREQUAL "blender-export-report.json")
            continue()
        endif()
        if (IS_SYMLINK "${_component_root}/${_path}")
            _uvsr_fail("${scene} component inventory contains symlink '${_path}'")
        endif()
        list(APPEND _component_outputs "${_path}")
    endforeach()
    _uvsr_assert_case_unique(_component_outputs "${scene} component inventory")
    list(SORT _component_outputs)
    list(SORT _output_paths)
    _uvsr_assert_lists_equal(_component_outputs _output_paths
        "${scene} report/component inventory")
endfunction()

_uvsr_validate_repack_report(
    bistro_interior_retextured
    glb
    BistroInterior_Wine.glb
    421517664
    "${_uvsr_bistro_model_sha256}"
    Bistro_v5_2.zip
    "${_uvsr_bistro_archive_sha256}"
    "${_uvsr_bistro_model_sha256}"
    bistro_interior.gltf
    ""
    0
    ""
    5)

_uvsr_validate_repack_report(
    san_miguel_retextured
    gltf
    san_miguel.gltf
    "${_uvsr_san_output_gltf_bytes}"
    "${_uvsr_san_output_gltf_sha256}"
    San_Miguel.zip
    "${_uvsr_san_archive_sha256}"
    "${_uvsr_san_model_sha256}"
    san_miguel.gltf
    san_miguel.bin
    "${_uvsr_san_output_buffer_bytes}"
    "${_uvsr_san_output_buffer_sha256}"
    5)

# Compare the complete runtime inventories for exactly the four supported scene
# roots, allowing only the build-generated stamp in a staged scene root.
file(GLOB _uvsr_staged_top_level
    LIST_DIRECTORIES true
    RELATIVE "${_uvsr_staged_models_root}"
    "${_uvsr_staged_models_root}/*")
foreach (_entry IN LISTS _uvsr_staged_top_level)
    string(TOLOWER "${_entry}" _entry_lower)
    if (_entry_lower STREQUAL "nvidia_bistro")
        _uvsr_fail("the user-owned nvidia_bistro directory must not be staged")
    endif()
endforeach()
if (EXISTS "${_uvsr_staged_models_root}/nvidia_bistro")
    _uvsr_fail("the user-owned nvidia_bistro directory must not be staged")
endif()

set(_uvsr_all_source_paths)
set(_uvsr_all_staged_paths)
foreach (_scene IN LISTS _uvsr_scene_names)
    _uvsr_collect_scene_files(
        "${_uvsr_source_models_root}" "${_scene}" false _source_files)
    _uvsr_collect_scene_files(
        "${_uvsr_staged_models_root}" "${_scene}" true _staged_files)
    _uvsr_assert_lists_equal(_source_files _staged_files
        "${_scene} source/staged inventory")

    foreach (_relative IN LISTS _source_files)
        set(_source "${_uvsr_source_models_root}/${_scene}/${_relative}")
        set(_staged "${_uvsr_staged_models_root}/${_scene}/${_relative}")
        file(SIZE "${_source}" _source_bytes)
        file(SIZE "${_staged}" _staged_bytes)
        if (_source_bytes GREATER_EQUAL _uvsr_github_file_limit_bytes)
            _uvsr_fail(
                "source file '${_scene}/${_relative}' is ${_source_bytes} bytes; every tracked file must be strictly below ${_uvsr_github_file_limit_bytes}")
        endif()
        if (_staged_bytes GREATER_EQUAL _uvsr_github_file_limit_bytes)
            _uvsr_fail(
                "staged file '${_scene}/${_relative}' is ${_staged_bytes} bytes; every staged file must be strictly below ${_uvsr_github_file_limit_bytes}")
        endif()
        if (NOT _source_bytes EQUAL _staged_bytes)
            _uvsr_fail("staged file size differs for '${_scene}/${_relative}'")
        endif()
        file(SHA256 "${_source}" _source_sha256)
        file(SHA256 "${_staged}" _staged_sha256)
        string(TOUPPER "${_source_sha256}" _source_sha256)
        string(TOUPPER "${_staged_sha256}" _staged_sha256)
        if (NOT _source_sha256 STREQUAL _staged_sha256)
            _uvsr_fail("staged file digest differs for '${_scene}/${_relative}'")
        endif()
        list(APPEND _uvsr_all_source_paths "${_scene}/${_relative}")
        list(APPEND _uvsr_all_staged_paths "${_scene}/${_relative}")
    endforeach()
endforeach()
_uvsr_assert_case_unique(_uvsr_all_source_paths "combined source scene inventory")
_uvsr_assert_case_unique(_uvsr_all_staged_paths "combined staged scene inventory")

message(STATUS
    "Scene asset provenance, GitHub file limits, and exact staged parity are valid")
