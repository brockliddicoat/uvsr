cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES C CXX)

include(FetchContent)
include(CTest)

function(uvsr_test target name)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME ${name} COMMAND ${target})
endfunction()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuntimeInventory.cmake")

if (WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(UVSR_RENDERER_RELEASE_SEQUENCE "16" CACHE STRING
    "Unissued renderer package sequence; publication requires release authority")
if (NOT UVSR_RENDERER_RELEASE_SEQUENCE MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "UVSR_RENDERER_RELEASE_SEQUENCE must be a positive integer")
endif()
math(EXPR UVSR_RENDERER_RELEASE_SEQUENCE_VALUE
    "${UVSR_RENDERER_RELEASE_SEQUENCE}")
if (UVSR_RENDERER_RELEASE_SEQUENCE_VALUE GREATER 9007199254740991)
    message(FATAL_ERROR "UVSR_RENDERER_RELEASE_SEQUENCE exceeds the safe range")
endif()

set(UVSR_SHADER_MODEL "6_5")
if (NOT UVSR_SHADER_MODEL MATCHES "^([0-9]+)_([0-9]+)$")
    message(FATAL_ERROR "UVSR_SHADER_MODEL must use DXC's major_minor form")
endif()
math(EXPR UVSR_SHADER_MODEL_ENCODED
    "${CMAKE_MATCH_1} * 16 + ${CMAKE_MATCH_2}"
    OUTPUT_FORMAT HEXADECIMAL)

# Build identity changes regenerate one small source and Windows resource. They
# do not add a commit-dependent definition to the renderer translation units.
find_package(Git REQUIRED)
set(UVSR_PRODUCTION_BUILD false)
if (NOT BUILD_TESTING)
    set(UVSR_PRODUCTION_BUILD true)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SourceIdentity.cmake")
uvsr_compute_source_identity(
    "${CMAKE_CURRENT_SOURCE_DIR}" "${GIT_EXECUTABLE}"
    "${UVSR_PRODUCTION_BUILD}"
    UVSR_SOURCE_IDENTITY UVSR_SOURCE_TREE_CLEAN UVSR_GIT_COMMIT)
message(STATUS "Configured source identity: ${UVSR_SOURCE_IDENTITY}")

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --git-path HEAD
    OUTPUT_VARIABLE UVSR_GIT_HEAD_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UVSR_GIT_HEAD_PATH}")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" symbolic-ref -q HEAD
    RESULT_VARIABLE UVSR_GIT_SYMBOLIC_REF_RESULT
    OUTPUT_VARIABLE UVSR_GIT_SYMBOLIC_REF
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (UVSR_GIT_SYMBOLIC_REF_RESULT EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
            rev-parse --git-path "${UVSR_GIT_SYMBOLIC_REF}"
        OUTPUT_VARIABLE UVSR_GIT_REF_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UVSR_GIT_REF_PATH}")
endif()

add_library(uvsr_settings_catalog STATIC
    src/settings_catalog.cpp src/settings_contract.cpp src/settings_value.cpp)
target_include_directories(uvsr_settings_catalog PUBLIC src)
if (MSVC)
    target_compile_options(uvsr_settings_catalog PRIVATE /constexpr:steps100000000)
endif()

add_executable(uvsr_build_identity_generator
    tools/build_identity_generator.cpp)
target_include_directories(uvsr_build_identity_generator PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(UVSR_GENERATED_SOURCE_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
set(UVSR_BUILD_IDENTITY_SOURCE
    "${UVSR_GENERATED_SOURCE_DIRECTORY}/build_identity.cpp")
set(UVSR_BUILD_IDENTITY_RESOURCE
    "${UVSR_GENERATED_SOURCE_DIRECTORY}/uvsr-engine.rc")
add_custom_target(uvsr_refresh_build_identity
    BYPRODUCTS
        "${UVSR_BUILD_IDENTITY_SOURCE}"
        "${UVSR_BUILD_IDENTITY_RESOURCE}"
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DUVSR_GIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DUVSR_PRODUCTION_BUILD=${UVSR_PRODUCTION_BUILD}"
        "-DUVSR_BUILD_CONFIGURATION=$<CONFIG>"
        "-DUVSR_IDENTITY_GENERATOR=$<TARGET_FILE:uvsr_build_identity_generator>"
        "-DUVSR_IDENTITY_CPP=${UVSR_BUILD_IDENTITY_SOURCE}"
        "-DUVSR_IDENTITY_RC=${UVSR_BUILD_IDENTITY_RESOURCE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RefreshBuildIdentity.cmake"
    DEPENDS
        uvsr_build_identity_generator
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RefreshBuildIdentity.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SourceIdentity.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine_identity.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/settings_snapshot_schema.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui_settings_command_catalog.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/settings_catalog.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui_settings_catalog.def"
    COMMENT "Refreshing the source- and settings-derived engine identity"
    VERBATIM)
set_source_files_properties(
    "${UVSR_BUILD_IDENTITY_SOURCE}"
    "${UVSR_BUILD_IDENTITY_RESOURCE}"
    PROPERTIES GENERATED TRUE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endforeach()

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
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncMappedAssets.cmake"
        COMMAND "${CMAKE_COMMAND}" -E touch "${asset_stamp}"
        DEPENDS "${asset_manifest}" ${staged_outputs}
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncMappedAssets.cmake"
        COMMENT "Purging retired ${target_name} outputs"
        VERBATIM)
    add_custom_target("${target_name}" DEPENDS "${asset_stamp}")
endfunction()

set(UVSR_DONUT_SHADER_STAGING_DIR "${CMAKE_BINARY_DIR}/donut_shaders")
set(DONUT_SHADERS_OUTPUT_DIR "${UVSR_DONUT_SHADER_STAGING_DIR}")

set(DONUT_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_DX12 ON CACHE BOOL "" FORCE)
set(DONUT_WITH_VULKAN OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)

function(uvsr_copy_lf source destination)
    file(READ "${source}" content)
    if (content MATCHES "@[A-Za-z_][A-Za-z0-9_]*@|#[ \t]*cmakedefine")
        message(FATAL_ERROR "Cannot LF-normalize dependency template: ${source}")
    endif()
    configure_file("${source}" "${destination}" @ONLY NEWLINE_STYLE LF)
endfunction()

# UVSR keeps dependencies pristine. Stage reviewed first-party patches in the
# build tree and replace only affected target sources.
function(uvsr_stage_patched_sources
    source_root
    patch_files
    output_root)
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
            # Donut appends backend sources with target-relative paths.
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

set(UVSR_DONUT_ENGINE_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_donut_engine_overrides")
set(UVSR_DONUT_ENGINE_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-engine.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-loading.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-shader-factory-direct-blob.patch")
set(UVSR_DONUT_ENGINE_SOURCES
    include/donut/engine/Scene.h
    include/donut/engine/SceneTypes.h
    include/donut/engine/ThreadPool.h
    src/engine/GltfImporter.cpp
    src/engine/Scene.cpp
    src/engine/ShaderFactory.cpp
    src/engine/TextureCache.cpp
    src/engine/ThreadPool.cpp)
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_ENGINE_PATCH_FILES}"
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}"
    ${UVSR_DONUT_ENGINE_SOURCES})

set(UVSR_DONUT_APP_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_donut_app_overrides")
set(UVSR_DONUT_APP_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-app.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-loading-app.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-app-ui-polish.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-render-disposition.patch")
set(UVSR_DONUT_APP_SOURCES
    include/donut/app/DeviceManager_DX12.h
    src/app/ApplicationBase.cpp
    src/app/DeviceManager.cpp
    src/app/dx12/DeviceManager_DX12.cpp
    src/app/UserInterfaceUtils.cpp)
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_APP_PATCH_FILES}"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    include/donut/app/ApplicationBase.h
    include/donut/app/DeviceManager.h
    include/donut/app/UserInterfaceUtils.h
    ${UVSR_DONUT_APP_SOURCES})

set(UVSR_IMGUI_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_imgui_overrides")
set(UVSR_IMGUI_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-ui.patch")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
    "${UVSR_IMGUI_PATCH_FILES}"
    "${UVSR_IMGUI_OVERRIDE_DIR}"
    imgui.cpp
    imgui.h
    imgui_draw.cpp
    imgui_internal.h
    imgui_tables.cpp
    imgui_widgets.cpp)

set(UVSR_NVRHI_D3D12_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_nvrhi_d3d12_overrides")
set(UVSR_NVRHI_D3D12_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/nvrhi-stable-directx-headers.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/nvrhi-d3d12-portability.patch")
set(UVSR_NVRHI_D3D12_SOURCES
    src/d3d12/d3d12-constants.cpp
    src/d3d12/d3d12-buffer.cpp
    src/d3d12/d3d12-commandlist.cpp
    src/d3d12/d3d12-compute.cpp
    src/d3d12/d3d12-descriptor-heap.cpp
    src/d3d12/d3d12-device.cpp
    src/d3d12/d3d12-graphics.cpp
    src/d3d12/d3d12-meshlets.cpp
    src/d3d12/d3d12-queries.cpp
    src/d3d12/d3d12-raytracing.cpp
    src/d3d12/d3d12-resource-bindings.cpp
    src/d3d12/d3d12-shader.cpp
    src/d3d12/d3d12-state-tracking.cpp
    src/d3d12/d3d12-texture.cpp
    src/d3d12/d3d12-upload.cpp)
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi"
    "${UVSR_NVRHI_D3D12_PATCH_FILES}"
    "${UVSR_NVRHI_D3D12_OVERRIDE_DIR}"
    src/d3d12/d3d12-backend.h
    ${UVSR_NVRHI_D3D12_SOURCES})
set(UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR
    "${CMAKE_BINARY_DIR}/uvsr_nvrhi_d3d12_diagnostics")
file(MAKE_DIRECTORY "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/nvrhi-d3d12-diagnostics.h"
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}/uvsr-d3d12-diagnostics.h"
    COPYONLY)
set(UVSR_NVRHI_DIAGNOSTICS_STAGE_RECORDS
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}/uvsr-d3d12-diagnostics.h|1024BEBDC4AF86844547ED3A2DE37452F16430F491F6F4476E725F04506ED2A0")
set(UVSR_NVRHI_STAGE_TREE_DIGEST
    "68591065a043935f9d09bda5a7b4c8dc3ed959f645997da0cf70ae78a0e09f5a")
set(UVSR_IMGUI_STAGE_TREE_DIGEST
    "980ee9a5cd845bdc8c70364f8989961d5b6e0e945610f572c08d81a0f3979294")
set(UVSR_TRANSITIONAL_DONUT_ENGINE_STAGE_TREE_DIGEST
    "0e164755d95956f5247618982d5cbe74be5044fed772b57bef65e48906cc7339")
set(UVSR_TRANSITIONAL_DONUT_APP_STAGE_TREE_DIGEST
    "ec0d63eda25f5d1ec5c36aac1da74b21d01dcd5f4fc6aa2728060f69129cecff")

add_library(uvsr_shader_blob STATIC src/shader_blob.cpp)
target_include_directories(uvsr_shader_blob PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(uvsr_shader_blob_builder tools/shader_blob_builder.cpp)
target_link_libraries(uvsr_shader_blob_builder PRIVATE uvsr_shader_blob)
target_include_directories(uvsr_shader_blob_builder PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
if (MSVC)
    target_compile_options(uvsr_shader_blob_builder PRIVATE
        /permissive- /W4 /WX)
endif()
# Packaging is a production build action, so its strict validator is an
# uninstalled build-time tool in both configurations. Its CTest remains
# developer-only below.
add_executable(uvsr_renderer_package_contract_validator
    tools/renderer_package_contract_validator.cpp)
set(UVSR_RUNTIME_ASSET_MAP
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-asset-map.def")
uvsr_read_runtime_asset_map(
    "${UVSR_RUNTIME_ASSET_MAP}"
    UVSR_PROTECTED_RUNTIME_PACKAGE_PATHS
    UVSR_PROTECTED_RUNTIME_SOURCE_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(uvsr_renderer_package_contract_validator PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tools"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_renderer_package_contract_validator PRIVATE
    bcrypt version)
if (MSVC)
    target_compile_options(uvsr_renderer_package_contract_validator PRIVATE
        /permissive- /W4 /WX)
endif()

add_executable(uvsr_d3d12_export_contract_validator
    tools/d3d12_export_contract_validator.cpp)
if (MSVC)
    target_compile_options(uvsr_d3d12_export_contract_validator PRIVATE
        /permissive- /W4 /WX)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectDonut.cmake")
uvsr_replace_target_sources(
    nvrhi_d3d12
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi"
    "${UVSR_NVRHI_D3D12_OVERRIDE_DIR}"
    ${UVSR_NVRHI_D3D12_SOURCES})
target_include_directories(nvrhi_d3d12 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/src/d3d12"
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}")
uvsr_replace_target_sources(
    donut_app
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    ${UVSR_DONUT_APP_SOURCES})
target_include_directories(donut_app BEFORE PUBLIC
    "${UVSR_DONUT_APP_OVERRIDE_DIR}/include")
target_include_directories(donut_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}")
uvsr_replace_target_sources(
    donut_engine
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}"
    ${UVSR_DONUT_ENGINE_SOURCES})
target_include_directories(donut_engine BEFORE PUBLIC
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}/include")

FetchContent_Declare(dxc
    URL
        "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/dxc_2026_02_20.zip"
    URL_HASH
        "SHA256=A1E89031421CF3C1FCA6627766AB3020CA4F962AC7E2CAA7FAB2B33A8436151E"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(dxc)
set(UVSR_DXC_FILE_COUNT 37)
set(UVSR_DXC_TREE_DIGEST
    "7b1ee9300f50e01c2347316b6dbc0640455231125986d36762743edb0fdbff24")
set(UVSR_DXC_EXECUTABLE "${dxc_SOURCE_DIR}/bin/x64/dxc.exe")
if (NOT EXISTS "${UVSR_DXC_EXECUTABLE}")
    message(FATAL_ERROR "The pinned x64 DXC executable is missing")
endif()
file(SHA256 "${UVSR_DXC_EXECUTABLE}" UVSR_DXC_SHA256)
if (NOT UVSR_DXC_SHA256 STREQUAL
        "b9cff94181248e080804b385da8964b6319fd07760721baa9053a891cf7a727f")
    message(FATAL_ERROR "The pinned x64 DXC executable failed SHA-256")
endif()
execute_process(
    COMMAND "${UVSR_DXC_EXECUTABLE}" --version
    RESULT_VARIABLE dxc_version_result
    OUTPUT_VARIABLE dxc_version_output
    ERROR_VARIABLE dxc_version_error)
if (NOT dxc_version_result EQUAL 0 OR
    NOT dxc_version_output MATCHES "1\\.9\\.2602")
    message(FATAL_ERROR
        "The pinned DXC version is not 1.9.2602: ${dxc_version_error}")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectXShaderCatalog.cmake")


if (NVRHI_WITH_DX12)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectAgilitySDK.cmake")
    set(UVSR_D3D12_RUNTIME_MAPPINGS
        "${UVSR_D3D12_AGILITY_CORE_DLL}" "D3D12Core.dll")
    if (BUILD_TESTING)
        list(APPEND UVSR_D3D12_RUNTIME_MAPPINGS
            "${UVSR_D3D12_AGILITY_LAYERS_DLL}" "D3D12SDKLayers.dll")
    endif()
    uvsr_add_mapped_asset_stage(
        uvsr_stage_d3d12_runtime
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/D3D12"
        ${UVSR_D3D12_RUNTIME_MAPPINGS})
endif()

set(UVSR_SHADER_STAGING_ROOT "${CMAKE_BINARY_DIR}/uvsr_shaders")
set(UVSR_CORE_SHADER_STAGING_DIR
    "${UVSR_SHADER_STAGING_ROOT}/core")
set(UVSR_SHADER_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.cfg")
set(UVSR_DONUT_SHADER_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/shaders/DonutShaders.cfg")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${UVSR_SHADER_CONFIG}" "${UVSR_DONUT_SHADER_CONFIG}")

uvsr_add_direct_shader_bundle(
    TARGET donut_shaders
    CONFIG "${UVSR_DONUT_SHADER_CONFIG}"
    SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/donut/shaders"
    OUTPUT_DIRECTORY "${UVSR_DONUT_SHADER_STAGING_DIR}"
    EXPECTED_TASKS 51
    OUTPUTS_VARIABLE UVSR_DONUT_SHADER_OUTPUTS
    EXCLUDE_SOURCES
        passes/pixel_readback_cs.hlsl
        passes/taa_cs.hlsl
        passes/light_probe.hlsl
        passes/gbuffer_vs.hlsl
        passes/material_id_ps.hlsl
    ENABLE_16BIT_TYPES
    INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}/donut/include"
        "${CMAKE_CURRENT_SOURCE_DIR}")
uvsr_add_direct_shader_bundle(
    TARGET uvsr_shaders
    CONFIG "${UVSR_SHADER_CONFIG}"
    SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
    OUTPUT_DIRECTORY "${UVSR_CORE_SHADER_STAGING_DIR}"
    EXPECTED_TASKS 32
    OUTPUTS_VARIABLE UVSR_SHADER_OUTPUTS
    OBJECTS_VARIABLE UVSR_SHADER_OBJECTS
    ENABLE_16BIT_TYPES
    INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(donut_engine donut_shaders)
add_dependencies(donut_render donut_shaders)
add_dependencies(donut_app donut_shaders)

set(UVSR_DIRECT_DEPENDENCY_VERIFICATION_SCOPE inputs)
set(UVSR_DIRECT_DEPENDENCY_INVOCATION
    "${CMAKE_CURRENT_BINARY_DIR}/verify-direct-dependency-state.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunDirectDependencyStateVerification.cmake.in"
    "${UVSR_DIRECT_DEPENDENCY_INVOCATION}"
    @ONLY)
add_custom_target(uvsr_verify_direct_dependency_state
    COMMAND "${CMAKE_COMMAND}" -P "${UVSR_DIRECT_DEPENDENCY_INVOCATION}"
    DEPENDS
        "${UVSR_DIRECT_DEPENDENCY_INVOCATION}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyDirectDependencyState.cmake"
    COMMENT "Verifying exact direct dependency source and staged state"
    VERBATIM)

foreach(dependency_target IN ITEMS
        cgltf stb jsoncpp_static tinyexr glfw imgui
        nvrhi nvrhi_d3d12 DirectX-Headers DirectX-Guids
        donut_shaders uvsr_shaders
        donut_core donut_engine donut_render donut_app)
    if (TARGET "${dependency_target}")
        add_dependencies("${dependency_target}"
            uvsr_verify_direct_dependency_state)
    endif()
endforeach()

add_executable(uvsr-engine WIN32
        "${UVSR_BUILD_IDENTITY_SOURCE}"
        "${UVSR_BUILD_IDENTITY_RESOURCE}"
        src/auto_exposure.cpp
        src/agx_tone_mapping_pass.cpp
        src/color_lut.cpp
        src/camera_collision.cpp
        src/d3d12_agility_exports.cpp
        src/engine_diagnostics.cpp
        src/engine_startup.cpp
        src/fast_approximate_aa.cpp
        src/ray_visibility_pass.cpp
        src/image_based_lighting_background_pass.cpp
        src/image_based_lighting_environment.cpp
        src/lighting_accumulation_pass.cpp
        src/noise_texture_library.cpp
        src/path_tracing_pass.cpp
        src/pbr_deferred_lighting_pass.cpp
        src/renderer_log.cpp
        src/renderer_nvrhi_message_callback.cpp
        src/renderer_scene_load_worker.cpp
        src/renderer_scene_retirement.cpp
        src/renderer_geometry_passes.cpp
        src/renderer_light_probe_processing.cpp
        src/renderer_common_passes.cpp
        src/renderer_pixel_readback.cpp
        src/renderer_shader_factory.cpp
        src/renderer_stb_image.cpp
        src/renderer_targets.cpp
        src/renderer_texture_bmp.cpp
        src/scene_catalog.cpp
        src/scene_loading.h
        src/settings_snapshot_decoder.cpp
        src/settings_snapshot_transaction.cpp
        src/uvsr_application.h
        src/uvsr_command_line.cpp
        src/uvsr_scene_viewer.h
        src/uvsr_ui_internal.h
        src/uvsr_lighting_history.cpp
        src/uvsr_flashlight.cpp
        src/uvsr_lifecycle.cpp
        src/uvsr_path_tracing.cpp
        src/uvsr_ray_visibility.cpp
        src/uvsr_render_frame.cpp
        src/uvsr_render_setup.cpp
        src/uvsr_renderer_state.cpp
        src/uvsr_scene_lifecycle.cpp
        src/uvsr_settings_commands.cpp
        src/uvsr_ui_commands.cpp
        src/uvsr_ui_panels.cpp
        src/uvsr_ui_renderer.cpp
        src/uvsr_display_sync.cpp
        src/uvsr.cpp
        src/world_space_representation.cpp
    )
    set_target_properties(uvsr-engine PROPERTIES
        OUTPUT_NAME "uvsr-engine")
    target_include_directories(uvsr-engine BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${UVSR_IMGUI_OVERRIDE_DIR}"
        "${UVSR_DONUT_APP_OVERRIDE_DIR}/include"
        "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}/include")
    target_link_libraries(uvsr-engine PRIVATE
        uvsr_shader_blob
        donut_render
        donut_app
        donut_engine
        bcrypt
        dwmapi)
    target_compile_definitions(uvsr-engine PRIVATE
        "UVSR_D3D12_AGILITY_SDK_VERSION=${UVSR_D3D12_AGILITY_SDK_VERSION}"
        "UVSR_REQUIRED_SHADER_MODEL=${UVSR_SHADER_MODEL_ENCODED}")
if (BUILD_TESTING)
    target_compile_definitions(uvsr-engine PRIVATE UVSR_BUILD_TESTING=1)
    target_sources(uvsr-engine PRIVATE src/retained_runtime_diagnostic.cpp)
endif()
add_dependencies(uvsr-engine uvsr_refresh_build_identity)
add_dependencies(uvsr-engine uvsr_verify_direct_dependency_state)
add_dependencies(uvsr-engine uvsr_stage_d3d12_runtime)
add_dependencies(uvsr-engine uvsr_d3d12_export_contract_validator)
add_custom_command(TARGET uvsr-engine POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DUVSR_EXECUTABLE=$<TARGET_FILE:uvsr-engine>"
            "-DUVSR_D3D12_CORE=${UVSR_D3D12_AGILITY_CORE_DLL}"
            "-DUVSR_D3D12_LAYERS=${UVSR_D3D12_AGILITY_LAYERS_DLL}"
            "-DUVSR_D3D12_RUNTIME_DIRECTORY=$<TARGET_FILE_DIR:uvsr-engine>/D3D12"
            "-DUVSR_EXPECTED_SDK_VERSION=${UVSR_D3D12_AGILITY_SDK_VERSION}"
            "-DUVSR_EXPECT_DEBUG_LAYERS=${BUILD_TESTING}"
            "-DUVSR_EXPORT_VALIDATOR=$<TARGET_FILE:uvsr_d3d12_export_contract_validator>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyD3D12Runtime.cmake"
        COMMENT "Verifying UVSR's app-local Direct3D 12 runtime contract"
        VERBATIM)
add_dependencies(uvsr-engine uvsr_shaders)

if (BUILD_TESTING)

add_executable(uvsr_scene_catalog_tests tests/scene_catalog_tests.cpp
    src/scene_catalog.cpp src/renderer_scene_load_worker.cpp src/renderer_scene_retirement.cpp)
target_include_directories(uvsr_scene_catalog_tests PRIVATE src)
target_link_libraries(uvsr_scene_catalog_tests PRIVATE nvrhi)
add_test(NAME uvsr_scene_catalog_reference COMMAND uvsr_scene_catalog_tests
    "${CMAKE_CURRENT_BINARY_DIR}/scene-acceptance-test/$<CONFIG>")
set_tests_properties(uvsr_scene_catalog_reference PROPERTIES TIMEOUT 30)

add_executable(uvsr_renderer_gpu_contract_tests
    tests/renderer_gpu_contract_tests.cpp)
target_include_directories(uvsr_renderer_gpu_contract_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(uvsr_renderer_resource_contract_tests
    tests/renderer_resource_contract_tests.cpp tests/receiver_contract_tests.cpp
    src/renderer_targets.cpp src/renderer_log.cpp src/renderer_texture_bmp.cpp)
target_include_directories(uvsr_renderer_resource_contract_tests PRIVATE
    src "${UVSR_DONUT_SOURCE_DIR}/include")
target_link_libraries(uvsr_renderer_resource_contract_tests PRIVATE nvrhi)
target_compile_definitions(uvsr_renderer_resource_contract_tests PRIVATE
    UVSR_RENDERER_BMP_ENCODER_ONLY=1 UVSR_REQUIRED_SHADER_MODEL=${UVSR_SHADER_MODEL_ENCODED} NOMINMAX)
if (MSVC)
    target_compile_options(uvsr_renderer_resource_contract_tests PRIVATE /permissive- /W4 /WX)
endif()
add_test(NAME uvsr_renderer_resources COMMAND uvsr_renderer_resource_contract_tests
    "${CMAKE_CURRENT_BINARY_DIR}/renderer-resource-test/$<CONFIG>")

add_executable(uvsr_retained_runtime_diagnostic_tests
    tests/retained_runtime_diagnostic_tests.cpp
    src/engine_diagnostics.cpp "${UVSR_BUILD_IDENTITY_SOURCE}"
    src/retained_runtime_diagnostic.cpp
    src/renderer_log.cpp
    src/settings_snapshot_decoder.cpp
    src/settings_snapshot_transaction.cpp
    src/uvsr_settings_commands.cpp)
target_link_libraries(uvsr_retained_runtime_diagnostic_tests PRIVATE ole32 shell32)
add_dependencies(uvsr_retained_runtime_diagnostic_tests uvsr_refresh_build_identity)
target_include_directories(uvsr_retained_runtime_diagnostic_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uvsr_retained_runtime_diagnostic_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_retained_runtime_diagnostic_tests
    COMMAND uvsr_retained_runtime_diagnostic_tests)

uvsr_test(uvsr_pbr_tests uvsr_pbr_reference
    tests/pbr_reference_tests.cpp)

target_link_libraries(uvsr_pbr_tests PRIVATE donut_core)

uvsr_test(uvsr_path_tracing_semantic_tests uvsr_path_tracing_semantic_reference
    tests/path_tracing_semantic_tests.cpp)

if (MSVC)
    target_compile_options(
        uvsr_path_tracing_semantic_tests
        PRIVATE /permissive- /W4 /WX)
endif()

uvsr_test(uvsr_flashlight_tests uvsr_flashlight_reference
    tests/flashlight_tests.cpp)

uvsr_test(uvsr_command_line_tests uvsr_command_line_reference
    tests/uvsr_command_line_tests.cpp
    src/settings_snapshot_decoder.cpp
    src/uvsr_command_line.cpp)

target_link_libraries(uvsr_command_line_tests PRIVATE bcrypt)
target_compile_definitions(uvsr_command_line_tests PRIVATE
    UVSR_BUILD_TESTING=1)

uvsr_test(uvsr_command_line_production_tests uvsr_command_line_production_reference
    tests/uvsr_command_line_tests.cpp
    src/settings_snapshot_decoder.cpp
    src/uvsr_command_line.cpp)

target_link_libraries(uvsr_command_line_production_tests PRIVATE bcrypt)
if (MSVC)
    foreach(target uvsr_command_line_tests uvsr_command_line_production_tests)
        target_compile_options(${target} PRIVATE /permissive- /W4 /WX)
    endforeach()
endif()

uvsr_test(uvsr_settings_commands_tests uvsr_settings_commands_reference
    tests/uvsr_settings_commands_tests.cpp
    src/renderer_log.cpp
    src/settings_snapshot_decoder.cpp
    src/settings_snapshot_transaction.cpp
    src/uvsr_settings_commands.cpp)

target_link_libraries(uvsr_settings_commands_tests PRIVATE ole32 shell32)

add_executable(uvsr_nvrhi_d3d12_diagnostics_tests
    tests/nvrhi_d3d12_diagnostics_tests.cpp
    src/engine_startup.cpp src/renderer_log.cpp src/renderer_nvrhi_message_callback.cpp)
target_include_directories(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE src
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/include"
    "${UVSR_DIRECTX_HEADERS_SOURCE_DIR}/include/directx")
target_link_libraries(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE
    nvrhi Microsoft::DirectX-Headers bcrypt d3d12 ole32 shell32)
target_compile_definitions(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE NOMINMAX)
if (MSVC)
    target_compile_options(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE
        /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_nvrhi_d3d12_diagnostics
    COMMAND uvsr_nvrhi_d3d12_diagnostics_tests
        "${CMAKE_CURRENT_BINARY_DIR}/backend-diagnostics/$<CONFIG>"
        "${UVSR_D3D12_AGILITY_CORE_DLL}")

add_executable(uvsr_display_controls_tests tests/display_controls_tests.cpp src/color_lut.cpp)
target_include_directories(
    uvsr_display_controls_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/include"
)
target_compile_features(uvsr_display_controls_tests PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(uvsr_display_controls_tests PRIVATE
        /permissive- /W4 /WX)
endif()

add_test(NAME uvsr_display_controls COMMAND uvsr_display_controls_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/luts/kodak")

uvsr_test(uvsr_camera_controls_tests uvsr_camera_controls_reference
    tests/camera_controls_tests.cpp src/camera_collision.cpp)

target_link_libraries(uvsr_camera_controls_tests PRIVATE donut_app)
target_include_directories(uvsr_camera_controls_tests SYSTEM PRIVATE "${UVSR_DONUT_SOURCE_DIR}/include")
foreach(target uvsr_scene_catalog_tests uvsr_camera_controls_tests)
    target_compile_definitions(${target} PRIVATE NOMINMAX)
    if (MSVC)
        target_compile_options(${target} PRIVATE /permissive- /W4 /WX)
    endif()
endforeach()


uvsr_test(uvsr_settings_snapshot_tests uvsr_settings_snapshot_reference
    tests/settings_snapshot_tests.cpp)

add_executable(uvsr_settings_snapshot_schema_probe
    tools/settings_snapshot_schema_probe.cpp
)
target_include_directories(
    uvsr_settings_snapshot_schema_probe
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

uvsr_test(uvsr_settings_snapshot_decoder_tests uvsr_settings_snapshot_decoder_reference
    tests/settings_snapshot_decoder_tests.cpp
    src/settings_snapshot_decoder.cpp)

uvsr_test(uvsr_settings_snapshot_transaction_tests uvsr_settings_snapshot_transaction_reference
    tests/settings_snapshot_transaction_tests.cpp
    src/settings_snapshot_transaction.cpp)

uvsr_test(uvsr_settings_snapshot_values_tests uvsr_settings_snapshot_values_reference
    tests/settings_snapshot_values_tests.cpp
    src/settings_snapshot_transaction.cpp)

add_executable(uvsr_settings_snapshot_decoder
    tools/settings_snapshot_decoder.cpp
    src/settings_snapshot_decoder.cpp)
target_include_directories(
    uvsr_settings_snapshot_decoder
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(uvsr_legal_inventory_validator
    tools/legal_inventory_validator.cpp)
target_link_libraries(uvsr_legal_inventory_validator PRIVATE bcrypt)

add_executable(uvsr_launcher_feed_contract_validator
    tools/launcher_feed_contract_validator.cpp)
target_include_directories(uvsr_launcher_feed_contract_validator PRIVATE src)
if (MSVC)
    target_compile_options(uvsr_launcher_feed_contract_validator PRIVATE
        /permissive- /W4 /WX)
endif()

foreach(target uvsr_retained_runtime_diagnostic_tests uvsr_command_line_tests
        uvsr_command_line_production_tests uvsr_settings_commands_tests
        uvsr_settings_snapshot_tests
        uvsr_settings_snapshot_schema_probe uvsr_settings_snapshot_decoder_tests
        uvsr_settings_snapshot_transaction_tests
        uvsr_settings_snapshot_values_tests
        uvsr_settings_snapshot_decoder)
    target_link_libraries(${target} PRIVATE uvsr_settings_catalog)
endforeach()

add_executable(uvsr_override_inventory_validator
    tools/override_inventory_validator.cpp)
if (MSVC)
    target_compile_options(uvsr_override_inventory_validator PRIVATE
        /permissive- /W4 /WX)
endif()

add_test(NAME uvsr_build_integrity COMMAND "${CMAKE_COMMAND}"
    "-DUVSR_IDENTITY_GENERATOR=$<TARGET_FILE:uvsr_build_identity_generator>"
    "-DUVSR_OVERRIDE_VALIDATOR=$<TARGET_FILE:uvsr_override_inventory_validator>"
    "-DUVSR_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DUVSR_BUILD_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
    "-DUVSR_CONFIGURATION=$<CONFIG>"
    "-DUVSR_GIT_EXECUTABLE=${GIT_EXECUTABLE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestBuildContracts.cmake")
add_test(NAME uvsr_distribution_contract COMMAND "${CMAKE_COMMAND}"
    "-DUVSR_EXPORT_VALIDATOR=$<TARGET_FILE:uvsr_d3d12_export_contract_validator>"
    "-DUVSR_ENGINE=$<TARGET_FILE:uvsr-engine>"
    "-DUVSR_LEGAL_VALIDATOR=$<TARGET_FILE:uvsr_legal_inventory_validator>"
    "-DUVSR_FEED_VALIDATOR=$<TARGET_FILE:uvsr_launcher_feed_contract_validator>"
    "-DUVSR_PACKAGE_VALIDATOR=$<TARGET_FILE:uvsr_renderer_package_contract_validator>"
    "-DUVSR_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DUVSR_SHADER_INVENTORY=${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-shader-inventory.def"
    "-DUVSR_ASSET_MAP=${UVSR_RUNTIME_ASSET_MAP}"
    "-DUVSR_D3D12_CORE=${UVSR_D3D12_AGILITY_CORE_DLL}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestDistributionContracts.cmake")

set_tests_properties(uvsr_distribution_contract PROPERTIES LABELS "production;d3d12;package")

uvsr_test(uvsr_imgui_interaction_tests uvsr_imgui_interaction
    tests/imgui_interaction_tests.cpp)

target_link_libraries(
    uvsr_imgui_interaction_tests
    PRIVATE imgui
)

endif()

# Donut builds a larger catalog outside the runtime tree. This inventory is the
# only owner of staged and packaged shader membership.
set(UVSR_RUNTIME_SHADER_INVENTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-shader-inventory.def")
uvsr_read_runtime_shader_inventory(
    "${UVSR_RUNTIME_SHADER_INVENTORY}"
    UVSR_RUNTIME_SHADER_PACKAGE_PATHS)
set(UVSR_ROOT_SHADERS)
set(UVSR_PASS_SHADERS)
set(UVSR_APP_SHADERS)
set(UVSR_RUNTIME_SHADER_BYPRODUCTS)
foreach(package_path IN LISTS UVSR_RUNTIME_SHADER_PACKAGE_PATHS)
    if (package_path MATCHES
            "^bin/shaders/framework/dxil/passes/([A-Za-z0-9_]+)\\.bin$")
        list(APPEND UVSR_PASS_SHADERS "${CMAKE_MATCH_1}")
    elseif(package_path MATCHES
            "^bin/shaders/framework/dxil/([A-Za-z0-9_]+)\\.bin$")
        list(APPEND UVSR_ROOT_SHADERS "${CMAKE_MATCH_1}")
    elseif(package_path MATCHES
            "^bin/shaders/uvsr/dxil/([A-Za-z0-9_]+)\\.bin$")
        list(APPEND UVSR_APP_SHADERS "${CMAKE_MATCH_1}")
    else()
        message(FATAL_ERROR
            "Invalid runtime shader inventory path: ${package_path}")
    endif()
    string(REGEX REPLACE "^bin/shaders/" "" runtime_relative_path
        "${package_path}")
    list(APPEND UVSR_RUNTIME_SHADER_BYPRODUCTS
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${runtime_relative_path}")
endforeach()

set(UVSR_RUNTIME_APP_SHADER_DIR
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/uvsr/dxil")

function(uvsr_shader_config_families shader_config output_variable)
    file(STRINGS "${shader_config}" shader_config_lines)
    set(families)
    foreach(config_line IN LISTS shader_config_lines)
        string(REGEX MATCH "^[^ \t#]+" source "${config_line}")
        if (source STREQUAL "")
            continue()
        endif()
        get_filename_component(family "${source}" NAME_WE)
        string(REGEX MATCH "(^|[ \t])-s[ \t]+([^ \t]+)"
            suffix_match "${config_line}")
        if (NOT suffix_match STREQUAL "")
            string(APPEND family "${CMAKE_MATCH_2}")
        else()
            string(REGEX MATCH "(^|[ \t])-E[ \t]+([^ \t]+)"
                entry_match "${config_line}")
            if (NOT entry_match STREQUAL "" AND
                NOT CMAKE_MATCH_2 STREQUAL "main")
                string(APPEND family "_${CMAKE_MATCH_2}")
            endif()
        endif()
        list(APPEND families "${family}")
    endforeach()
    list(REMOVE_DUPLICATES families)
    list(SORT families)
    set("${output_variable}" "${families}" PARENT_SCOPE)
endfunction()

uvsr_shader_config_families(
    "${UVSR_SHADER_CONFIG}" UVSR_CONFIGURED_APP_SHADER_FAMILIES)
set(UVSR_INVENTORIED_APP_SHADER_FAMILIES ${UVSR_APP_SHADERS})
list(SORT UVSR_INVENTORIED_APP_SHADER_FAMILIES)
if (NOT "${UVSR_CONFIGURED_APP_SHADER_FAMILIES}" STREQUAL
        "${UVSR_INVENTORIED_APP_SHADER_FAMILIES}")
    message(FATAL_ERROR
        "UVSR shader configuration and runtime inventory families differ")
endif()

add_custom_target(uvsr_stage_runtime_shaders
    BYPRODUCTS
        ${UVSR_RUNTIME_SHADER_BYPRODUCTS}
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND}
        "-DUVSR_RUNTIME_SHADER_INVENTORY=${UVSR_RUNTIME_SHADER_INVENTORY}"
        "-DUVSR_FRAMEWORK_SHADER_SOURCE_ROOT=${UVSR_DONUT_SHADER_STAGING_DIR}"
        "-DUVSR_APP_SHADER_SOURCE_ROOT=${UVSR_CORE_SHADER_STAGING_DIR}"
        "-DUVSR_RUNTIME_SHADER_ALLOWED_PARENT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        "-DUVSR_RUNTIME_SHADER_ROOT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncRuntimeShaderBundle.cmake"
    DEPENDS
        "${UVSR_RUNTIME_SHADER_INVENTORY}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuntimeInventory.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncRuntimeShaderBundle.cmake"
    COMMENT "Synchronizing the exact UVSR runtime shader inventory"
    VERBATIM)
add_dependencies(uvsr_stage_runtime_shaders donut_shaders uvsr_shaders)
add_dependencies(uvsr-engine uvsr_stage_runtime_shaders)

foreach(target uvsr-engine uvsr_build_identity_generator
        uvsr_renderer_package_contract_validator)
    target_link_libraries(${target} PRIVATE uvsr_settings_catalog)
endforeach()

if (BUILD_TESTING)
    add_executable(uvsr_shader_bundle_tests
        tests/shader_bundle_tests.cpp src/renderer_shader_factory.cpp)
    target_include_directories(uvsr_shader_bundle_tests PRIVATE src)
    target_link_libraries(uvsr_shader_bundle_tests PRIVATE uvsr_shader_blob nvrhi)
    add_dependencies(uvsr_shader_bundle_tests uvsr_shader_blob_builder
        uvsr_stage_runtime_shaders uvsr_renderer_gpu_contract_tests)
    if (MSVC)
        target_compile_options(uvsr_shader_bundle_tests PRIVATE /permissive- /W4 /WX)
    endif()
    add_test(NAME uvsr_shader_bundle_contract COMMAND uvsr_shader_bundle_tests
        "${UVSR_RUNTIME_SHADER_INVENTORY}"
        "${UVSR_DONUT_SHADER_STAGING_DIR}"
        "${UVSR_CORE_SHADER_STAGING_DIR}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
        "$<TARGET_FILE:uvsr_shader_blob_builder>"
        "${CMAKE_CURRENT_BINARY_DIR}/shader-bundle-test/$<CONFIG>")
    add_test(NAME uvsr_shader_reflection COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_DXC_EXECUTABLE=${UVSR_DXC_EXECUTABLE}"
        "-DUVSR_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DUVSR_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/shader-reflection/$<CONFIG>"
        "-DUVSR_SHADER_CATALOG_DIRECTORY=${UVSR_CORE_SHADER_STAGING_DIR}/catalogs"
        "-DUVSR_GPU_CONTRACT_TEST=$<TARGET_FILE:uvsr_renderer_gpu_contract_tests>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_reflection.cmake")
    add_test(NAME uvsr_runtime_shader_sync_reference COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_SYNC_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncRuntimeShaderBundle.cmake"
        "-DUVSR_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/runtime-shader-sync-test/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestRuntimeShaderSync.cmake")
    set_tests_properties(uvsr_shader_bundle_contract uvsr_shader_reflection
        uvsr_runtime_shader_sync_reference PROPERTIES LABELS "runtime;production;shaders")
    set_tests_properties(uvsr_shader_reflection PROPERTIES DEPENDS uvsr_shader_bundle_contract)

    add_executable(uvsr_renderer_common_passes_tests
        tests/renderer_common_passes_tests.cpp
        src/renderer_common_passes.cpp
        src/renderer_shader_factory.cpp
        src/renderer_log.cpp)
    target_include_directories(uvsr_renderer_common_passes_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(uvsr_renderer_common_passes_tests PRIVATE
        uvsr_shader_blob
        nvrhi
        nvrhi_d3d12
        Microsoft::DirectX-Headers
        DirectX-Guids
        d3d12
        dxgi)
    target_compile_features(
        uvsr_renderer_common_passes_tests PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(uvsr_renderer_common_passes_tests PRIVATE
            /permissive- /W4 /WX)
    endif()
    add_dependencies(uvsr_renderer_common_passes_tests
        uvsr_stage_runtime_shaders
        uvsr_stage_d3d12_runtime)
    add_test(
        NAME uvsr_renderer_common_passes_reference
        COMMAND uvsr_renderer_common_passes_tests
            "${UVSR_RUNTIME_APP_SHADER_DIR}")
    set_tests_properties(
        uvsr_renderer_common_passes_reference
        PROPERTIES
            LABELS "renderer;d3d12"
            WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            TIMEOUT 120)
    endif()

set(UVSR_SCENE_SOURCE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes")
set(UVSR_SCENE_STAGING_ROOT
    "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models")
if (BUILD_TESTING)
add_executable(uvsr_scene_asset_contract_tests
    tests/scene_asset_contract_tests.cpp
    tests/diffuse_environment_asset_tests.cpp
    src/scene_catalog.cpp)
target_include_directories(uvsr_scene_asset_contract_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_scene_asset_contract_tests
    PRIVATE cgltf stb bcrypt)
target_compile_definitions(uvsr_scene_asset_contract_tests PRIVATE
    NOMINMAX _CRT_SECURE_NO_WARNINGS)
add_test(
    NAME uvsr_scene_asset_contract
    COMMAND uvsr_scene_asset_contract_tests
        "${UVSR_SCENE_SOURCE_ROOT}"
        "${UVSR_SCENE_STAGING_ROOT}"
        "${UVSR_RUNTIME_ASSET_MAP}")
set_tests_properties(
    uvsr_scene_asset_contract
    PROPERTIES
        LABELS "assets;scenes"
        TIMEOUT 600)
endif()

set(UVSR_PROGGY_CLEAN_LICENSE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/proggy-clean/ProggyClean-MIT.txt")

uvsr_verify_dependency_file(
    "${UVSR_PROGGY_CLEAN_LICENSE_SOURCE}"
    "8B802D79F256D29B45AD253323D212FA14CA952A20DCD227CFBCDB3D140BFE7C" 1082)
uvsr_verify_dependency_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Intel-XeGTAO-MIT.txt"
    "1F3BFD6B628535F0F0E779E70C260CFDC1BB45BC8644D7108E65F83E24A05CBA" 1081)
uvsr_verify_dependency_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/TinyEXR-and-OpenEXR-BSD.txt"
    "0D3E165809F0C704B67E8CD860E5E0E148122F70A9D2AC90400D3DF9482C67F4" 3116)
uvsr_verify_dependency_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/NVIDIA-Donut-NVRHI-MIT.txt"
    "25179DAE1293243F83E3C1293BD8C13DBBAC4DD544F51A40A2DBB7C7CFD6E18A" 1090)
uvsr_verify_dependency_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Microsoft-DirectX-Headers-MIT.txt"
    "FD532481D828E13A0B13CCB598E02338A3617740675A862EE6BDC1541B68E93D" 1073)
set(UVSR_MEDIA_MAPPINGS)
set(UVSR_PROTECTED_LICENSE_MAPPINGS)
list(LENGTH UVSR_PROTECTED_RUNTIME_PACKAGE_PATHS
    UVSR_PROTECTED_RUNTIME_PACKAGE_PATH_COUNT)
math(EXPR UVSR_PROTECTED_RUNTIME_PACKAGE_PATH_LAST
    "${UVSR_PROTECTED_RUNTIME_PACKAGE_PATH_COUNT} - 1")
foreach(index RANGE 0 ${UVSR_PROTECTED_RUNTIME_PACKAGE_PATH_LAST})
    list(GET UVSR_PROTECTED_RUNTIME_PACKAGE_PATHS ${index} package_path)
    list(GET UVSR_PROTECTED_RUNTIME_SOURCE_PATHS ${index} source_path)
    if (package_path MATCHES "^media/(.+)$")
        list(APPEND UVSR_MEDIA_MAPPINGS
            "${source_path}" "${CMAKE_MATCH_1}")
    elseif(package_path MATCHES "^bin/licenses/(.+)$")
        list(APPEND UVSR_PROTECTED_LICENSE_MAPPINGS
            "${source_path}" "${CMAKE_MATCH_1}")
    else()
        message(FATAL_ERROR
            "Runtime asset map path has no stage owner: ${package_path}")
    endif()
endforeach()
uvsr_add_mapped_asset_stage(
    uvsr_stage_media "${CMAKE_BINARY_DIR}/media" ${UVSR_MEDIA_MAPPINGS})
add_dependencies(uvsr-engine uvsr_stage_media)
if (BUILD_TESTING)
    add_dependencies(uvsr_scene_asset_contract_tests uvsr_stage_media)
endif()

include(cmake/RuntimeLicenseMappings.cmake)

set(UVSR_LICENSE_MAPPING_ARGUMENTS ${UVSR_LICENSE_MAPPINGS})
set(UVSR_LICENSE_SOURCE_FILES)
while(UVSR_LICENSE_MAPPING_ARGUMENTS)
    list(POP_FRONT UVSR_LICENSE_MAPPING_ARGUMENTS source_path)
    list(POP_FRONT UVSR_LICENSE_MAPPING_ARGUMENTS mapped_path)
    list(APPEND UVSR_LICENSE_SOURCE_FILES "${source_path}")
endwhile()
uvsr_add_mapped_asset_stage(
    uvsr_stage_licenses
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses"
    ${UVSR_LICENSE_MAPPINGS})
add_custom_target(uvsr_sync_exact_licenses
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_ASSET_MANIFEST=${CMAKE_CURRENT_BINARY_DIR}/uvsr_stage_licenses.assets"
        "-DUVSR_ASSET_STAGING_ROOT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncMappedAssets.cmake"
    DEPENDS
        uvsr_stage_licenses
        ${UVSR_LICENSE_SOURCE_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncMappedAssets.cmake"
    COMMENT "Synchronizing and hash-verifying exact runtime notices"
    VERBATIM)
add_dependencies(uvsr_sync_exact_licenses
    uvsr_verify_direct_dependency_state)
add_dependencies(uvsr-engine uvsr_sync_exact_licenses)

if (NOT BUILD_TESTING)
    install(TARGETS uvsr-engine RUNTIME DESTINATION bin)
    install(FILES "${UVSR_D3D12_AGILITY_CORE_DLL}"
        DESTINATION bin/D3D12)
    install(DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/"
        DESTINATION bin/shaders)
    install(DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/"
        DESTINATION bin/licenses)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/media/"
        DESTINATION media)
    install(CODE
        "execute_process(
            COMMAND \"$<TARGET_FILE:uvsr_renderer_package_contract_validator>\"
                --write \"\${CMAKE_INSTALL_PREFIX}\"
                --shader-inventory \"${UVSR_RUNTIME_SHADER_INVENTORY}\"
                --asset-map \"${UVSR_RUNTIME_ASSET_MAP}\"
                --source-commit \"${UVSR_GIT_COMMIT}\"
                --release-sequence ${UVSR_RENDERER_RELEASE_SEQUENCE_VALUE}
            COMMAND_ERROR_IS_FATAL ANY)")

    set(UVSR_PACKAGE_STAGING_DIRECTORY "${CMAKE_BINARY_DIR}/package")
    set(UVSR_PACKAGE_ARCHIVE
        "${CMAKE_BINARY_DIR}/artifacts/uvsr-renderer-windows-11-x64.zip")
    set(UVSR_PACKAGE_VALIDATION_DIRECTORY
        "${CMAKE_BINARY_DIR}/package-archive-validation")
    add_custom_target(uvsr-renderer-package
        COMMAND "${CMAKE_COMMAND}"
            "-DUVSR_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}"
            -DUVSR_REQUIRE_COMMITTED=ON
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyPostmortems.cmake"
        COMMAND "${CMAKE_COMMAND}"
            "-DUVSR_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DUVSR_GIT_EXECUTABLE=${GIT_EXECUTABLE}"
            -DUVSR_PRODUCTION_BUILD=true
            "-DUVSR_BUILD_CONFIGURATION=$<CONFIG>"
            "-DUVSR_EXPECTED_SOURCE_IDENTITY=${UVSR_GIT_COMMIT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RefreshBuildIdentity.cmake"
        COMMAND "${CMAKE_COMMAND}"
            -P "${UVSR_DIRECT_DEPENDENCY_INVOCATION}"
        COMMAND "${CMAKE_COMMAND}"
            "-DUVSR_EXECUTABLE=$<TARGET_FILE:uvsr-engine>"
            "-DUVSR_D3D12_CORE=${UVSR_D3D12_AGILITY_CORE_DLL}"
            "-DUVSR_D3D12_LAYERS=${UVSR_D3D12_AGILITY_LAYERS_DLL}"
            "-DUVSR_D3D12_RUNTIME_DIRECTORY=$<TARGET_FILE_DIR:uvsr-engine>/D3D12"
            "-DUVSR_EXPECTED_SDK_VERSION=${UVSR_D3D12_AGILITY_SDK_VERSION}"
            "-DUVSR_EXPECT_DEBUG_LAYERS=${BUILD_TESTING}"
            "-DUVSR_EXPORT_VALIDATOR=$<TARGET_FILE:uvsr_d3d12_export_contract_validator>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyD3D12Runtime.cmake"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${UVSR_PACKAGE_STAGING_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E remove -f "${UVSR_PACKAGE_ARCHIVE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/artifacts"
        COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
            --config "$<CONFIG>"
            --prefix "${UVSR_PACKAGE_STAGING_DIRECTORY}"
        COMMAND "$<TARGET_FILE:uvsr_renderer_package_contract_validator>"
            --check "${UVSR_PACKAGE_STAGING_DIRECTORY}"
            --shader-inventory "${UVSR_RUNTIME_SHADER_INVENTORY}"
            --asset-map "${UVSR_RUNTIME_ASSET_MAP}"
        COMMAND ${CMAKE_COMMAND} -E chdir
            "${UVSR_PACKAGE_STAGING_DIRECTORY}"
            ${CMAKE_COMMAND} -E tar cf "${UVSR_PACKAGE_ARCHIVE}"
                --format=zip
                package-manifest.json bin media
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${UVSR_PACKAGE_VALIDATION_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${UVSR_PACKAGE_VALIDATION_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E chdir
            "${UVSR_PACKAGE_VALIDATION_DIRECTORY}"
            ${CMAKE_COMMAND} -E tar xvf "${UVSR_PACKAGE_ARCHIVE}"
        COMMAND "$<TARGET_FILE:uvsr_renderer_package_contract_validator>"
            --check "${UVSR_PACKAGE_VALIDATION_DIRECTORY}"
            --shader-inventory "${UVSR_RUNTIME_SHADER_INVENTORY}"
            --asset-map "${UVSR_RUNTIME_ASSET_MAP}"
        DEPENDS
            uvsr_verify_direct_dependency_state
            uvsr-engine
            uvsr_d3d12_export_contract_validator
            uvsr_renderer_package_contract_validator
            uvsr_stage_d3d12_runtime
            uvsr_stage_runtime_shaders
            uvsr_sync_exact_licenses
            uvsr_stage_media
        COMMENT "Creating the strict production renderer package"
        VERBATIM)
endif()
