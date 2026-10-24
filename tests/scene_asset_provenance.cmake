if(NOT DEFINED SOURCE_ROOT OR NOT IS_DIRECTORY "${SOURCE_ROOT}")
    message(FATAL_ERROR "The source scene root is missing: ${SOURCE_ROOT}")
endif()
if(NOT DEFINED STAGING_ROOT OR NOT IS_DIRECTORY "${STAGING_ROOT}")
    message(FATAL_ERROR "The staged scene root is missing: ${STAGING_ROOT}")
endif()

set(manifest_path "${SOURCE_ROOT}/downloaded_scene_imports.json")
if(NOT EXISTS "${manifest_path}")
    message(FATAL_ERROR "The downloaded-scene provenance manifest is missing")
endif()
file(READ "${manifest_path}" manifest_json)
string(JSON scene_count LENGTH "${manifest_json}" scenes)
if(NOT scene_count EQUAL 2)
    message(FATAL_ERROR
        "Expected two downloaded scenes in the provenance manifest; found ${scene_count}")
endif()

set(seen_slugs)
math(EXPR last_scene_index "${scene_count} - 1")
foreach(scene_index RANGE 0 ${last_scene_index})
    string(JSON slug GET "${manifest_json}" scenes ${scene_index} slug)
    if(slug IN_LIST seen_slugs)
        message(FATAL_ERROR "Duplicate downloaded-scene slug in manifest: ${slug}")
    endif()
    list(APPEND seen_slugs "${slug}")

    foreach(hash_field archiveSha256 sourceObjSha256 outputGlbSha256)
        string(JSON recorded_hash
            GET "${manifest_json}" scenes ${scene_index} ${hash_field})
        string(LENGTH "${recorded_hash}" hash_length)
        if(NOT hash_length EQUAL 64 OR NOT recorded_hash MATCHES "^[0-9A-F]+$")
            message(FATAL_ERROR
                "Invalid ${hash_field} for downloaded scene ${slug}")
        endif()
    endforeach()

    set(glb_path "${SOURCE_ROOT}/${slug}/components/${slug}.glb")
    if(NOT EXISTS "${glb_path}")
        message(FATAL_ERROR "Manifest GLB is missing: ${glb_path}")
    endif()
    file(SHA256 "${glb_path}" actual_glb_hash)
    string(TOUPPER "${actual_glb_hash}" actual_glb_hash)
    string(JSON recorded_glb_hash
        GET "${manifest_json}" scenes ${scene_index} outputGlbSha256)
    if(NOT actual_glb_hash STREQUAL recorded_glb_hash)
        message(FATAL_ERROR
            "Manifest GLB hash differs for downloaded scene ${slug}")
    endif()

    file(SIZE "${glb_path}" actual_glb_bytes)
    string(JSON recorded_glb_bytes
        GET "${manifest_json}" scenes ${scene_index} outputBytes)
    if(NOT actual_glb_bytes EQUAL recorded_glb_bytes)
        message(FATAL_ERROR
            "Manifest GLB byte count differs for downloaded scene ${slug}")
    endif()
endforeach()

set(expected_slugs living_room sponza)
list(SORT seen_slugs)
list(SORT expected_slugs)
if(NOT seen_slugs STREQUAL expected_slugs)
    message(FATAL_ERROR
        "Downloaded-scene manifest slugs differ from the expected set")
endif()

file(GLOB_RECURSE source_files
    LIST_DIRECTORIES false
    RELATIVE "${SOURCE_ROOT}"
    "${SOURCE_ROOT}/*")
file(GLOB_RECURSE staged_files
    LIST_DIRECTORIES false
    RELATIVE "${STAGING_ROOT}"
    "${STAGING_ROOT}/*")
list(REMOVE_ITEM staged_files ".uvsr-stage.stamp")
list(SORT source_files)
list(SORT staged_files)
if(NOT source_files STREQUAL staged_files)
    message(FATAL_ERROR
        "The staged scene inventory differs from the source inventory")
endif()

foreach(relative_path IN LISTS source_files)
    file(SHA256 "${SOURCE_ROOT}/${relative_path}" source_hash)
    file(SHA256 "${STAGING_ROOT}/${relative_path}" staged_hash)
    if(NOT source_hash STREQUAL staged_hash)
        message(FATAL_ERROR
            "The staged scene asset differs from its source: ${relative_path}")
    endif()
endforeach()

message(STATUS
    "Downloaded-scene provenance and ${scene_count}-scene staged inventory passed")
