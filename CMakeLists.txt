cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES C CXX)

include(FetchContent)
include(CTest)

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

add_executable(uvsr_build_identity_generator
    tools/build_identity_generator.cpp)
target_include_directories(uvsr_build_identity_generator PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

if (BUILD_TESTING)
    add_test(
        NAME uvsr_build_identity_generator_contract
        COMMAND "${CMAKE_COMMAND}"
            "-DUVSR_IDENTITY_GENERATOR=$<TARGET_FILE:uvsr_build_identity_generator>"
            "-DUVSR_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/identity-generator-test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestBuildIdentityGenerator.cmake")
endif()

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
    set(asset_sources)
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
        list(APPEND asset_sources "${source_path}")
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

function(uvsr_add_asset_stage target_name source_root staging_root)
    set(asset_mappings)
    foreach(source_path IN LISTS ARGN)
        file(RELATIVE_PATH relative_path "${source_root}" "${source_path}")
        list(APPEND asset_mappings "${source_path}" "${relative_path}")
    endforeach()
    uvsr_add_mapped_asset_stage(
        "${target_name}" "${staging_root}" ${asset_mappings})
endfunction()

set(UVSR_DONUT_SHADER_STAGING_DIR "${CMAKE_BINARY_DIR}/donut_shaders")
set(DONUT_SHADERS_OUTPUT_DIR "${UVSR_DONUT_SHADER_STAGING_DIR}")

set(DONUT_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_DX12 ON CACHE BOOL "" FORCE)
set(DONUT_WITH_VULKAN OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)

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
        file(READ "${source_root}/${relative_path}" staged_source)
        if (staged_source MATCHES
                "@[A-Za-z_][A-Za-z0-9_]*@|#[ \t]*cmakedefine")
            message(FATAL_ERROR
                "Cannot LF-normalize dependency template-like source: "
                "${source_root}/${relative_path}")
        endif()
        configure_file(
            "${source_root}/${relative_path}"
            "${candidate_root}/${relative_path}"
            @ONLY NEWLINE_STYLE LF)
        list(APPEND expected_files "${relative_path}")
    endforeach()

    set(patch_index 0)
    foreach(patch_file IN LISTS patch_files)
        math(EXPR patch_index "${patch_index} + 1")
        file(READ "${patch_file}" normalized_patch)
        if (normalized_patch MATCHES
                "@[A-Za-z_][A-Za-z0-9_]*@|#[ \t]*cmakedefine")
            message(FATAL_ERROR
                "Cannot LF-normalize dependency template-like patch: "
                "${patch_file}")
        endif()
        set(normalized_patch_file
            "${normalized_patch_root}/${patch_index}.patch")
        configure_file("${patch_file}" "${normalized_patch_file}"
            @ONLY NEWLINE_STYLE LF)
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
    endforeach()
    set_property(TARGET "${target}" PROPERTY SOURCES "${target_sources}")

    foreach(relative_path IN LISTS ARGN)
        target_sources("${target}" PRIVATE
            "${override_root}/${relative_path}")
    endforeach()
endfunction()

set(UVSR_DONUT_ENGINE_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_donut_engine_overrides")
set(UVSR_DONUT_ENGINE_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-engine.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-loading.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-shader-factory-direct-blob.patch")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_ENGINE_PATCH_FILES}"
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}"
    include/donut/engine/Scene.h
    include/donut/engine/SceneTypes.h
    include/donut/engine/ThreadPool.h
    src/engine/GltfImporter.cpp
    src/engine/Scene.cpp
    src/engine/ShaderFactory.cpp
    src/engine/TextureCache.cpp
    src/engine/ThreadPool.cpp)

set(UVSR_DONUT_APP_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_donut_app_overrides")
set(UVSR_DONUT_APP_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-app.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-loading-app.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-app-ui-polish.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-render-disposition.patch")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_APP_PATCH_FILES}"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    include/donut/app/ApplicationBase.h
    include/donut/app/DeviceManager.h
    include/donut/app/DeviceManager_DX12.h
    include/donut/app/UserInterfaceUtils.h
    src/app/ApplicationBase.cpp
    src/app/DeviceManager.cpp
    src/app/dx12/DeviceManager_DX12.cpp
    src/app/UserInterfaceUtils.cpp)

set(UVSR_IMGUI_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_imgui_overrides")
set(UVSR_IMGUI_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-ui.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-slider-controls.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-combo-roll.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-ui-polish.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-tooltip-picker.patch")
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
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi"
    "${UVSR_NVRHI_D3D12_PATCH_FILES}"
    "${UVSR_NVRHI_D3D12_OVERRIDE_DIR}"
    src/d3d12/d3d12-constants.cpp
    src/d3d12/d3d12-backend.h
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
    "f0b834e245e847e1aa423092f9c992e7a71e53822a2ee3952a94a3cb21a5bbf5")
set(UVSR_TRANSITIONAL_DONUT_ENGINE_STAGE_TREE_DIGEST
    "0e164755d95956f5247618982d5cbe74be5044fed772b57bef65e48906cc7339")
set(UVSR_TRANSITIONAL_DONUT_APP_STAGE_TREE_DIGEST
    "f1332bad6914cdc915ab2a897fcadb12ca57dc97c53a97eda461480fed57d878")

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
if (BUILD_TESTING)
    add_executable(uvsr_shader_blob_builder_tests
        tests/shader_blob_builder_tests.cpp)
    add_dependencies(uvsr_shader_blob_builder_tests
        uvsr_shader_blob_builder)
    if (MSVC)
        target_compile_options(uvsr_shader_blob_builder_tests PRIVATE
            /permissive- /W4 /WX)
    endif()
    add_test(
        NAME uvsr_shader_blob_builder_reference
        COMMAND uvsr_shader_blob_builder_tests
            "$<TARGET_FILE:uvsr_shader_blob_builder>"
            "${CMAKE_CURRENT_BINARY_DIR}/shader-blob-builder-test/$<CONFIG>")
endif()

# Packaging is a production build action, so its strict validator is an
# uninstalled build-time tool in both configurations. Its CTest remains
# developer-only below.
add_executable(uvsr_renderer_package_contract_validator
    tools/renderer_package_contract_validator.cpp)
set(UVSR_RUNTIME_MEDIA_INVENTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-media-inventory.def")
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
target_include_directories(nvrhi_d3d12 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/src/d3d12"
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}")
uvsr_replace_target_sources(
    donut_app
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    include/donut/app/DeviceManager_DX12.h
    src/app/ApplicationBase.cpp
    src/app/DeviceManager.cpp
    src/app/dx12/DeviceManager_DX12.cpp
    src/app/UserInterfaceUtils.cpp)
target_include_directories(donut_app BEFORE PUBLIC
    "${UVSR_DONUT_APP_OVERRIDE_DIR}/include")
target_include_directories(donut_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}")
uvsr_replace_target_sources(
    donut_engine
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}"
    include/donut/engine/Scene.h
    include/donut/engine/SceneTypes.h
    include/donut/engine/ThreadPool.h
    src/engine/GltfImporter.cpp
    src/engine/Scene.cpp
    src/engine/ShaderFactory.cpp
    src/engine/TextureCache.cpp
    src/engine/ThreadPool.cpp)
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
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectNRD.cmake")

add_library(uvsr_denoiser STATIC
    src/denoiser_backend.cpp)
target_include_directories(uvsr_denoiser
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_denoiser PUBLIC nvrhi)
target_sources(uvsr_denoiser PRIVATE
    src/nrd_denoiser_backend.cpp
    src/nrd_denoiser_backend.h)
target_link_libraries(uvsr_denoiser PRIVATE NRD)

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
    EXPECTED_TASKS 137
    OUTPUTS_VARIABLE UVSR_SHADER_OUTPUTS
    OBJECTS_VARIABLE UVSR_SHADER_OBJECTS
    ENABLE_16BIT_TYPES
    INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(donut_engine donut_shaders)
add_dependencies(donut_render donut_shaders)
add_dependencies(donut_app donut_shaders)

set(UVSR_TRANSITIONAL_DONUT_SOURCE_DIR "${UVSR_DONUT_SOURCE_DIR}")
set(UVSR_TRANSITIONAL_DONUT_EXPECTED_HEAD "${UVSR_DONUT_EXPECTED_HEAD}")
set(UVSR_NVRHI_STAGE_DIR "${UVSR_NVRHI_D3D12_OVERRIDE_DIR}")
set(UVSR_IMGUI_STAGE_DIR "${UVSR_IMGUI_OVERRIDE_DIR}")
set(UVSR_TRANSITIONAL_DONUT_ENGINE_STAGE_DIR
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}")
set(UVSR_TRANSITIONAL_DONUT_APP_STAGE_DIR
    "${UVSR_DONUT_APP_OVERRIDE_DIR}")
set(UVSR_NRD_STAGE_DIR "${UVSR_NRD_STAGING_DIRECTORY}")
set(UVSR_NRD_DXIL_HEADERS_DIR
    "${UVSR_NRD_SHADER_OUTPUT_DIRECTORY}/dxil")

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

set(UVSR_DIRECT_DEPENDENCY_VERIFICATION_SCOPE nrd-generated-dxil)
set(UVSR_NRD_GENERATED_DXIL_INVOCATION
    "${CMAKE_CURRENT_BINARY_DIR}/verify-nrd-generated-dxil.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunDirectDependencyStateVerification.cmake.in"
    "${UVSR_NRD_GENERATED_DXIL_INVOCATION}"
    @ONLY)
unset(UVSR_DIRECT_DEPENDENCY_VERIFICATION_SCOPE)
add_custom_target(uvsr_verify_nrd_generated_dxil
    COMMAND "${CMAKE_COMMAND}" -P "${UVSR_NRD_GENERATED_DXIL_INVOCATION}"
    DEPENDS
        nrd_shaders
        "${UVSR_NRD_GENERATED_DXIL_INVOCATION}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyDirectDependencyState.cmake"
    COMMENT "Verifying the exact 29-header generated NRD DXIL view"
    VERBATIM)
add_dependencies(NRD uvsr_verify_nrd_generated_dxil)

foreach(dependency_target IN ITEMS
        cgltf stb jsoncpp_static tinyexr glfw imgui
        nvrhi nvrhi_d3d12 DirectX-Headers DirectX-Guids
        MathLib NRD uvsr_denoiser
        nrd_shaders donut_shaders uvsr_shaders
        donut_core donut_engine donut_render donut_app)
    if (TARGET "${dependency_target}")
        add_dependencies("${dependency_target}"
            uvsr_verify_direct_dependency_state)
    endif()
endforeach()
if (BUILD_TESTING)
    add_test(
        NAME uvsr_direct_dependency_state_contract
        COMMAND "${CMAKE_COMMAND}"
            "-DUVSR_VERIFIER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyDirectDependencyState.cmake"
            "-DUVSR_GIT_EXECUTABLE=${GIT_EXECUTABLE}"
            "-DUVSR_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/direct-dependency-state-self-test-$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_direct_dependency_state_tests.cmake")
endif()

add_executable(uvsr-engine WIN32
        "${UVSR_BUILD_IDENTITY_SOURCE}"
        "${UVSR_BUILD_IDENTITY_RESOURCE}"
        src/auto_exposure.cpp
        src/agx_tone_mapping_pass.cpp
        src/camera_collision.cpp
        src/d3d12_agility_exports.cpp
        src/denoising_pass.cpp
        src/engine_diagnostics.cpp
        src/engine_startup.cpp
        src/fast_approximate_aa.cpp
        src/directional_ray_visibility_pass.cpp
        src/image_based_lighting_background_pass.cpp
        src/image_based_lighting_environment.cpp
        src/lighting_accumulation_pass.cpp
        src/msaa_visibility_resolve.cpp
        src/noise_texture_library.cpp
        src/path_tracing_pass.cpp
        src/pbr_deferred_lighting_pass.cpp
        src/ray_traced_flashlight_shadows.cpp
        src/ray_traced_sky_visibility.cpp
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
        src/screen_space_visibility.cpp
        src/settings_snapshot_decoder.cpp
        src/settings_snapshot_transaction.cpp
        src/temporal_aa.cpp
        src/temporal_aa_core.cpp
        src/uvsr_application.h
        src/uvsr_command_line.cpp
        src/uvsr_internal.h
        src/uvsr_ao_gi.cpp
        src/uvsr_flashlight.cpp
        src/uvsr_lifecycle.cpp
        src/uvsr_path_tracing.cpp
        src/uvsr_ray_visibility.cpp
        src/uvsr_renderer_orchestration.cpp
        src/uvsr_scene_lifecycle.cpp
        src/uvsr_settings_commands.cpp
        src/uvsr_ui_commands.cpp
        src/uvsr_ui_panels.cpp
        src/uvsr_ui_renderer.cpp
        src/uvsr_ui_renderer.h
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
        uvsr_denoiser
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
set(UVSR_ENVIRONMENT_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/environments")
    set(UVSR_ENVIRONMENT_STAGE_DIR
        "${CMAKE_BINARY_DIR}/media/environments")
    file(GLOB_RECURSE UVSR_ENVIRONMENT_ASSETS
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${UVSR_ENVIRONMENT_SOURCE_DIR}/*.hdr")
    list(SORT UVSR_ENVIRONMENT_ASSETS)
    set(UVSR_NOISE_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/noise")
    set(UVSR_NOISE_STAGE_DIR
        "${CMAKE_BINARY_DIR}/media/uvsr/noise")
    file(GLOB_RECURSE UVSR_NOISE_ASSETS
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${UVSR_NOISE_SOURCE_DIR}/*.bin")
    list(APPEND UVSR_NOISE_ASSETS
        "${UVSR_NOISE_SOURCE_DIR}/manifest.json")
    list(SORT UVSR_NOISE_ASSETS)
add_dependencies(uvsr-engine uvsr_shaders)

if (BUILD_TESTING)
add_test(
    NAME uvsr_d3d12_export_contract_reference
    COMMAND "$<TARGET_FILE:uvsr_d3d12_export_contract_validator>"
        --self-test "$<TARGET_FILE:uvsr-engine>")
set_tests_properties(
    uvsr_d3d12_export_contract_reference
    PROPERTIES LABELS "production;d3d12")

add_executable(uvsr_scene_catalog_tests
    tests/scene_catalog_tests.cpp
    src/scene_catalog.cpp
)
target_include_directories(
    uvsr_scene_catalog_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(NAME uvsr_scene_catalog_reference COMMAND uvsr_scene_catalog_tests)

add_executable(uvsr_renderer_gpu_contract_tests
    tests/renderer_gpu_contract_tests.cpp)
target_include_directories(uvsr_renderer_gpu_contract_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(uvsr_renderer_shader_factory_tests
    tests/renderer_shader_factory_tests.cpp
    src/renderer_shader_factory.cpp)
target_include_directories(uvsr_renderer_shader_factory_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_renderer_shader_factory_tests PRIVATE
    uvsr_shader_blob
    nvrhi)
add_executable(uvsr_renderer_log_tests
    tests/renderer_log_tests.cpp
    src/engine_startup.cpp
    src/renderer_log.cpp)
target_include_directories(uvsr_renderer_log_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_renderer_log_tests PRIVATE
    Microsoft::DirectX-Headers
    bcrypt d3d12 ole32 shell32)
add_executable(uvsr_renderer_resource_contract_tests
    tests/renderer_resource_contract_tests.cpp)
target_include_directories(uvsr_renderer_resource_contract_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(uvsr_renderer_resource_contract_tests PRIVATE cxx_std_17)
add_executable(uvsr_pbr_deferred_dispatch_contract_tests
    tests/pbr_deferred_dispatch_contract_tests.cpp)
target_include_directories(uvsr_pbr_deferred_dispatch_contract_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(
    uvsr_pbr_deferred_dispatch_contract_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_pbr_deferred_dispatch_contract_reference
    COMMAND uvsr_pbr_deferred_dispatch_contract_tests)
add_executable(uvsr_required_pass_failclose_tests
    tests/required_pass_failclose_tests.cpp)
target_include_directories(uvsr_required_pass_failclose_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    uvsr_required_pass_failclose_tests PRIVATE nvrhi donut_core)
target_compile_features(
    uvsr_required_pass_failclose_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_required_pass_failclose_reference
    COMMAND uvsr_required_pass_failclose_tests)
add_executable(uvsr_renderer_texture_bmp_tests
    tests/renderer_texture_bmp_tests.cpp
    src/renderer_texture_bmp.cpp)
target_include_directories(uvsr_renderer_texture_bmp_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(uvsr_renderer_texture_bmp_tests PRIVATE
    UVSR_RENDERER_BMP_ENCODER_ONLY=1)
target_compile_features(uvsr_renderer_texture_bmp_tests PRIVATE cxx_std_17)
add_executable(uvsr_renderer_pass_detachment_tests
    tests/renderer_pass_detachment_tests.cpp
    src/image_based_lighting_environment.cpp
        src/renderer_common_passes.cpp
        src/renderer_geometry_passes.cpp
        src/renderer_light_probe_processing.cpp
        src/renderer_log.cpp
        src/renderer_shader_factory.cpp
    src/renderer_stb_image.cpp
    src/renderer_targets.cpp)
target_include_directories(uvsr_renderer_pass_detachment_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_renderer_pass_detachment_tests PRIVATE
    uvsr_shader_blob
    nvrhi
    stb)
target_compile_features(uvsr_renderer_pass_detachment_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_renderer_pass_detachment_reference
    COMMAND uvsr_renderer_pass_detachment_tests)
add_executable(uvsr_renderer_scene_load_worker_tests
    tests/renderer_scene_load_worker_tests.cpp
    src/renderer_scene_load_worker.cpp)
target_include_directories(uvsr_renderer_scene_load_worker_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(uvsr_renderer_scene_load_worker_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_renderer_scene_load_worker_reference
    COMMAND uvsr_renderer_scene_load_worker_tests)
add_executable(uvsr_renderer_scene_retirement_tests
    tests/renderer_scene_retirement_tests.cpp
    src/renderer_scene_retirement.cpp)
target_include_directories(uvsr_renderer_scene_retirement_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_renderer_scene_retirement_tests PRIVATE nvrhi)
target_compile_features(uvsr_renderer_scene_retirement_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_renderer_scene_retirement_reference
    COMMAND uvsr_renderer_scene_retirement_tests)
add_executable(uvsr_retained_runtime_diagnostic_tests
    tests/retained_runtime_diagnostic_tests.cpp
    src/retained_runtime_diagnostic.cpp)
target_include_directories(uvsr_retained_runtime_diagnostic_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(uvsr_retained_runtime_diagnostic_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_retained_runtime_diagnostic_tests
    COMMAND uvsr_retained_runtime_diagnostic_tests)
set(UVSR_GPU_CONTRACT_CS_DXIL
    "${CMAKE_CURRENT_BINARY_DIR}/renderer-gpu-contract/$<CONFIG>/probe-cs.dxil")
set(UVSR_GPU_CONTRACT_PS_DXIL
    "${CMAKE_CURRENT_BINARY_DIR}/renderer-gpu-contract/$<CONFIG>/probe-ps.dxil")
add_test(
    NAME uvsr_renderer_contract_reference
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_GPU_CONTRACT_TEST=$<TARGET_FILE:uvsr_renderer_gpu_contract_tests>"
        "-DUVSR_SHADER_FACTORY_TEST=$<TARGET_FILE:uvsr_renderer_shader_factory_tests>"
        "-DUVSR_RENDERER_LOG_TEST=$<TARGET_FILE:uvsr_renderer_log_tests>"
        "-DUVSR_D3D12_CORE=${UVSR_D3D12_AGILITY_CORE_DLL}"
        "-DUVSR_RENDERER_RESOURCE_CONTRACT_TEST=$<TARGET_FILE:uvsr_renderer_resource_contract_tests>"
        "-DUVSR_RENDERER_TEXTURE_BMP_TEST=$<TARGET_FILE:uvsr_renderer_texture_bmp_tests>"
        "-DUVSR_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/renderer-contract/$<CONFIG>"
        "-DUVSR_DXC_EXECUTABLE=${UVSR_DXC_EXECUTABLE}"
        "-DUVSR_CONTRACT_CS_DXIL=${UVSR_GPU_CONTRACT_CS_DXIL}"
        "-DUVSR_CONTRACT_PS_DXIL=${UVSR_GPU_CONTRACT_PS_DXIL}"
        "-DUVSR_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestRendererContracts.cmake")

add_executable(uvsr_scene_light_names_tests
    tests/scene_light_names_tests.cpp
)
target_include_directories(
    uvsr_scene_light_names_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_scene_light_names_reference
    COMMAND uvsr_scene_light_names_tests
)

add_executable(uvsr_pbr_tests tests/pbr_reference_tests.cpp)
target_include_directories(uvsr_pbr_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_pbr_tests PRIVATE donut_core)
add_test(NAME uvsr_pbr_reference COMMAND uvsr_pbr_tests)

add_executable(
    uvsr_directional_ray_visibility_tests
    tests/directional_ray_visibility_tests.cpp)
target_include_directories(
    uvsr_directional_ray_visibility_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    uvsr_directional_ray_visibility_tests
    PRIVATE nvrhi)
add_test(
    NAME uvsr_directional_ray_visibility_reference
    COMMAND uvsr_directional_ray_visibility_tests)

add_executable(
    uvsr_ray_traced_sky_visibility_tests
    tests/ray_traced_sky_visibility_tests.cpp)
target_include_directories(
    uvsr_ray_traced_sky_visibility_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    uvsr_ray_traced_sky_visibility_tests
    PRIVATE nvrhi)
add_test(
    NAME uvsr_sky_visibility_reference
    COMMAND uvsr_ray_traced_sky_visibility_tests)

add_executable(
    uvsr_world_space_representation_settings_tests
    tests/world_space_representation_settings_tests.cpp)
target_include_directories(
    uvsr_world_space_representation_settings_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_world_space_representation_settings_reference
    COMMAND uvsr_world_space_representation_settings_tests)

add_executable(
    uvsr_path_tracing_settings_tests
    tests/path_tracing_settings_tests.cpp)
target_include_directories(
    uvsr_path_tracing_settings_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_path_tracing_settings_reference
    COMMAND uvsr_path_tracing_settings_tests)

add_executable(
    uvsr_path_tracing_semantic_tests
    tests/path_tracing_semantic_tests.cpp)
target_include_directories(
    uvsr_path_tracing_semantic_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(
    uvsr_path_tracing_semantic_tests
    PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(
        uvsr_path_tracing_semantic_tests
        PRIVATE /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_path_tracing_semantic_reference
    COMMAND uvsr_path_tracing_semantic_tests)

set(UVSR_PATH_TRACING_OBJECTS ${UVSR_SHADER_OBJECTS})
list(FILTER UVSR_PATH_TRACING_OBJECTS INCLUDE REGEX
    "/objects/path_tracing_cs\\.[0-9a-f]+\\.dxil$")
list(LENGTH UVSR_PATH_TRACING_OBJECTS UVSR_PATH_TRACING_OBJECT_COUNT)
if (NOT UVSR_PATH_TRACING_OBJECT_COUNT EQUAL 1)
    message(FATAL_ERROR
        "The direct shader catalog must expose one path-tracing object")
endif()
list(GET UVSR_PATH_TRACING_OBJECTS 0 UVSR_PATH_TRACING_DXIL)
add_custom_target(
    uvsr_path_tracing_reflection_input
    DEPENDS "${UVSR_PATH_TRACING_DXIL}")
add_dependencies(
    uvsr_path_tracing_semantic_tests
    uvsr_path_tracing_reflection_input)
add_test(
    NAME uvsr_path_tracing_reflection
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_DXC_EXECUTABLE=${UVSR_DXC_EXECUTABLE}"
        "-DUVSR_PATH_TRACING_DXIL=${UVSR_PATH_TRACING_DXIL}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/path_tracing_reflection.cmake")

add_executable(
    uvsr_lighting_accumulation_semantic_tests
    tests/lighting_accumulation_semantic_tests.cpp)
target_include_directories(
    uvsr_lighting_accumulation_semantic_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(
    uvsr_lighting_accumulation_semantic_tests
    PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(
        uvsr_lighting_accumulation_semantic_tests
        PRIVATE /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_lighting_accumulation_semantic_reference
    COMMAND uvsr_lighting_accumulation_semantic_tests)

add_executable(
    uvsr_ray_visibility_semantic_tests
    tests/ray_visibility_semantic_tests.cpp)
target_include_directories(
    uvsr_ray_visibility_semantic_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(
    uvsr_ray_visibility_semantic_tests
    PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(
        uvsr_ray_visibility_semantic_tests
        PRIVATE /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_ray_visibility_semantic_reference
    COMMAND uvsr_ray_visibility_semantic_tests)

add_executable(
    uvsr_msaa_visibility_resolve_tests
    tests/msaa_visibility_resolve_tests.cpp)
target_include_directories(
    uvsr_msaa_visibility_resolve_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    uvsr_msaa_visibility_resolve_tests
    PRIVATE nvrhi)
target_compile_features(
    uvsr_msaa_visibility_resolve_tests
    PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(
        uvsr_msaa_visibility_resolve_tests
        PRIVATE /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_msaa_visibility_resolve_reference
    COMMAND uvsr_msaa_visibility_resolve_tests)

set(UVSR_MSAA_VISIBILITY_OBJECTS ${UVSR_SHADER_OBJECTS})
list(FILTER UVSR_MSAA_VISIBILITY_OBJECTS INCLUDE REGEX
    "/objects/msaa_visibility_resolve_cs\\.[0-9a-f]+\\.dxil$")
list(LENGTH UVSR_MSAA_VISIBILITY_OBJECTS
    UVSR_MSAA_VISIBILITY_OBJECT_COUNT)
if (NOT UVSR_MSAA_VISIBILITY_OBJECT_COUNT EQUAL 4)
    message(FATAL_ERROR
        "The direct shader catalog must expose four MSAA visibility objects")
endif()
list(GET UVSR_MSAA_VISIBILITY_OBJECTS 0 UVSR_MSAA_VISIBILITY_2X_DXIL)
list(GET UVSR_MSAA_VISIBILITY_OBJECTS 1 UVSR_MSAA_VISIBILITY_4X_DXIL)
list(GET UVSR_MSAA_VISIBILITY_OBJECTS 2 UVSR_MSAA_VISIBILITY_8X_DXIL)
list(GET UVSR_MSAA_VISIBILITY_OBJECTS 3 UVSR_MSAA_VISIBILITY_16X_DXIL)
add_custom_target(
    uvsr_msaa_visibility_resolve_reflection_inputs
    DEPENDS ${UVSR_MSAA_VISIBILITY_OBJECTS})
add_dependencies(
    uvsr_msaa_visibility_resolve_tests
    uvsr_msaa_visibility_resolve_reflection_inputs)
add_test(
    NAME uvsr_msaa_visibility_resolve_reflection
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_DXC_EXECUTABLE=${UVSR_DXC_EXECUTABLE}"
        "-DUVSR_MSAA_VISIBILITY_2X_DXIL=${UVSR_MSAA_VISIBILITY_2X_DXIL}"
        "-DUVSR_MSAA_VISIBILITY_4X_DXIL=${UVSR_MSAA_VISIBILITY_4X_DXIL}"
        "-DUVSR_MSAA_VISIBILITY_8X_DXIL=${UVSR_MSAA_VISIBILITY_8X_DXIL}"
        "-DUVSR_MSAA_VISIBILITY_16X_DXIL=${UVSR_MSAA_VISIBILITY_16X_DXIL}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/msaa_visibility_resolve_reflection.cmake")

add_executable(uvsr_flashlight_tests tests/flashlight_tests.cpp)
target_include_directories(
    uvsr_flashlight_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_flashlight_reference
    COMMAND uvsr_flashlight_tests)

add_executable(
    uvsr_ray_traced_flashlight_shadow_tests
    tests/ray_traced_flashlight_shadow_tests.cpp)
target_include_directories(
    uvsr_ray_traced_flashlight_shadow_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    uvsr_ray_traced_flashlight_shadow_tests
    PRIVATE nvrhi)
add_test(
    NAME uvsr_ray_traced_flashlight_shadow_contract
    COMMAND uvsr_ray_traced_flashlight_shadow_tests)

add_executable(
    uvsr_denoiser_tests
    tests/denoiser_backend_tests.cpp)
target_link_libraries(
    uvsr_denoiser_tests
    PRIVATE uvsr_denoiser)
add_test(
    NAME uvsr_denoiser_reference
    COMMAND uvsr_denoiser_tests)

add_executable(uvsr_command_line_options_tests
    tests/command_line_options_tests.cpp)
target_include_directories(
    uvsr_command_line_options_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_command_line_options_reference
    COMMAND uvsr_command_line_options_tests)

add_executable(uvsr_command_line_tests
    tests/uvsr_command_line_tests.cpp
    src/settings_snapshot_decoder.cpp
    src/uvsr_command_line.cpp)
target_include_directories(uvsr_command_line_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(uvsr_command_line_tests PRIVATE
    UVSR_BUILD_TESTING=1)
target_compile_features(uvsr_command_line_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_command_line_reference
    COMMAND uvsr_command_line_tests)

add_executable(uvsr_command_line_production_tests
    tests/uvsr_command_line_tests.cpp
    src/settings_snapshot_decoder.cpp
    src/uvsr_command_line.cpp)
target_include_directories(uvsr_command_line_production_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(
    uvsr_command_line_production_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_command_line_production_reference
    COMMAND uvsr_command_line_production_tests)

add_executable(uvsr_settings_commands_tests
    tests/uvsr_settings_commands_tests.cpp
    src/renderer_log.cpp
    src/settings_snapshot_decoder.cpp
    src/settings_snapshot_transaction.cpp
    src/uvsr_settings_commands.cpp)
target_include_directories(uvsr_settings_commands_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_settings_commands_tests PRIVATE ole32 shell32)
target_compile_features(uvsr_settings_commands_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_settings_commands_reference
    COMMAND uvsr_settings_commands_tests)

add_executable(uvsr_gpu_capabilities_tests
    tests/gpu_capabilities_tests.cpp)
target_include_directories(
    uvsr_gpu_capabilities_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(
    uvsr_gpu_capabilities_tests
    PRIVATE "UVSR_REQUIRED_SHADER_MODEL=${UVSR_SHADER_MODEL_ENCODED}")
add_test(
    NAME uvsr_gpu_capabilities_reference
    COMMAND uvsr_gpu_capabilities_tests)

add_executable(uvsr_msaa_raster_topology_tests
    tests/msaa_raster_topology_tests.cpp)
target_include_directories(uvsr_msaa_raster_topology_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(uvsr_msaa_raster_topology_tests PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(uvsr_msaa_raster_topology_tests PRIVATE
        /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_msaa_raster_topology_reference
    COMMAND uvsr_msaa_raster_topology_tests)

if (WIN32)
    add_executable(uvsr_d3d12_msaa_capability_probe
        tools/d3d12_msaa_capability_probe.cpp)
    target_link_libraries(uvsr_d3d12_msaa_capability_probe PRIVATE
        Microsoft::DirectX-Headers
        d3d12
        dxgi)
    target_compile_features(
        uvsr_d3d12_msaa_capability_probe PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(uvsr_d3d12_msaa_capability_probe PRIVATE
            /permissive- /W4 /WX)
    endif()
    add_dependencies(uvsr_d3d12_msaa_capability_probe
        uvsr_stage_d3d12_runtime)
endif()

add_executable(uvsr_d3d12_portability_source_contract_tests
    tests/d3d12_portability_source_contract_tests.cpp)
target_include_directories(
    uvsr_d3d12_portability_source_contract_tests BEFORE PRIVATE
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/include"
    "${UVSR_DIRECTX_HEADERS_SOURCE_DIR}/include/directx")
target_compile_definitions(
    uvsr_d3d12_portability_source_contract_tests PRIVATE
    "UVSR_REQUIRED_SHADER_MODEL=${UVSR_SHADER_MODEL_ENCODED}")
target_compile_features(
    uvsr_d3d12_portability_source_contract_tests PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(
        uvsr_d3d12_portability_source_contract_tests PRIVATE
        /permissive- /W4 /WX)
endif()
add_dependencies(uvsr_d3d12_portability_source_contract_tests
    nvrhi_d3d12
    uvsr_verify_direct_dependency_state)
add_test(
    NAME uvsr_d3d12_portability_source_contract
    COMMAND uvsr_d3d12_portability_source_contract_tests)

add_executable(uvsr_nvrhi_d3d12_diagnostics_tests
    tests/nvrhi_d3d12_diagnostics_tests.cpp)
target_include_directories(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/include"
    "${UVSR_DIRECTX_HEADERS_SOURCE_DIR}/include/directx")
target_compile_features(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE
        /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_nvrhi_d3d12_diagnostics
    COMMAND uvsr_nvrhi_d3d12_diagnostics_tests)

# Direct attached-renderer publication, resource-lifetime, and failure-path
# contracts. The shader half of these seams is compiled and reflected below.
add_executable(uvsr_renderer_source_contract_tests
    tests/renderer_source_contract_tests.cpp
    src/renderer_scene_load_worker.cpp
    src/renderer_scene_retirement.cpp)
target_include_directories(uvsr_renderer_source_contract_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_renderer_source_contract_tests PRIVATE nvrhi)

add_executable(uvsr_pbr_lighting_source_contract_tests
    tests/pbr_lighting_source_contract_tests.cpp)
target_include_directories(uvsr_pbr_lighting_source_contract_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(uvsr_ray_traced_sky_visibility_source_contract_tests
    tests/ray_traced_sky_visibility_source_contract_tests.cpp)
target_include_directories(
    uvsr_ray_traced_sky_visibility_source_contract_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    uvsr_ray_traced_sky_visibility_source_contract_tests PRIVATE nvrhi)

foreach(contract_name IN ITEMS
        renderer
        pbr_lighting
        ray_traced_sky_visibility)
    target_compile_features(
        uvsr_${contract_name}_source_contract_tests PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(
            uvsr_${contract_name}_source_contract_tests PRIVATE
            /permissive- /W4 /WX)
    endif()
    add_test(
        NAME uvsr_${contract_name}_source_contract
        COMMAND uvsr_${contract_name}_source_contract_tests)
endforeach()

add_executable(uvsr_windows_executable_path_tests
    tests/windows_executable_path_tests.cpp)
target_include_directories(
    uvsr_windows_executable_path_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_windows_executable_path_reference
    COMMAND uvsr_windows_executable_path_tests)

add_executable(uvsr_adaptive_sync_tests
    tests/adaptive_sync_tests.cpp)
target_include_directories(
    uvsr_adaptive_sync_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_adaptive_sync_reference
    COMMAND uvsr_adaptive_sync_tests)

add_executable(uvsr_diffuse_environment_asset_tests
    tests/diffuse_environment_asset_tests.cpp)
target_include_directories(uvsr_diffuse_environment_asset_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(uvsr_diffuse_environment_asset_tests PRIVATE
    "UVSR_TEST_ENVIRONMENT_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/environments\"")
target_link_libraries(uvsr_diffuse_environment_asset_tests PRIVATE
    donut_core stb)
add_test(
    NAME uvsr_diffuse_environment_asset_reference
    COMMAND uvsr_diffuse_environment_asset_tests)

add_executable(uvsr_temporal_aa_tests tests/temporal_aa_tests.cpp)
target_include_directories(
    uvsr_temporal_aa_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/include"
)
target_compile_features(uvsr_temporal_aa_tests PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(uvsr_temporal_aa_tests PRIVATE
        /permissive- /W4 /WX)
endif()

set(UVSR_TEMPORAL_AA_SHADER_OBJECTS)
foreach(shader_object IN LISTS UVSR_SHADER_OBJECTS)
    get_filename_component(shader_object_name "${shader_object}" NAME)
    if(shader_object_name MATCHES
            "^(fast_approximate_aa_ps|temporal_aa_(blend|minim(um)?|resolve|sharpen)_cs)\\..+\\.dxil$")
        list(APPEND UVSR_TEMPORAL_AA_SHADER_OBJECTS "${shader_object}")
    endif()
endforeach()
list(LENGTH UVSR_TEMPORAL_AA_SHADER_OBJECTS
    UVSR_TEMPORAL_AA_SHADER_OBJECT_COUNT)
if(NOT UVSR_TEMPORAL_AA_SHADER_OBJECT_COUNT EQUAL 22)
    message(FATAL_ERROR
        "Temporal AA/FXAA expands to ${UVSR_TEMPORAL_AA_SHADER_OBJECT_COUNT} DXIL objects; expected 22")
endif()
add_custom_target(uvsr_temporal_aa_reflection_inputs
    DEPENDS ${UVSR_TEMPORAL_AA_SHADER_OBJECTS})
add_dependencies(uvsr_temporal_aa_tests
    uvsr_temporal_aa_reflection_inputs)
add_test(
    NAME uvsr_temporal_aa_reference
    COMMAND uvsr_temporal_aa_tests
)
add_test(
    NAME uvsr_temporal_aa_reflection
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_DXC_EXECUTABLE=${UVSR_DXC_EXECUTABLE}"
        "-DUVSR_SHADER_OBJECT_DIRECTORY=${UVSR_CORE_SHADER_STAGING_DIR}/objects"
        "-DUVSR_SHADER_CATALOG_DIRECTORY=${UVSR_CORE_SHADER_STAGING_DIR}/catalogs"
        "-DUVSR_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/temporal-aa-reflection/$<CONFIG>"
        "-DUVSR_FAST_APPROXIMATE_ATTRIBUTION=${CMAKE_CURRENT_SOURCE_DIR}/legal/documentation/google-filament-fxaa.md"
        "-DUVSR_BSD_LICENSE=${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/BSD-2-Clause.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/temporal_aa_reflection.cmake")
set_tests_properties(uvsr_temporal_aa_reflection PROPERTIES
    DEPENDS uvsr_temporal_aa_reference)

add_executable(uvsr_camera_collision_tests
    tests/camera_collision_tests.cpp
    src/camera_collision.cpp
)
target_include_directories(
    uvsr_camera_collision_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(uvsr_camera_collision_tests PRIVATE donut_core)
add_test(NAME uvsr_camera_collision_reference COMMAND uvsr_camera_collision_tests)

add_executable(uvsr_camera_controls_tests tests/camera_controls_tests.cpp)
target_include_directories(
    uvsr_camera_controls_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(uvsr_camera_controls_tests PRIVATE donut_app)
add_test(NAME uvsr_camera_controls_reference COMMAND uvsr_camera_controls_tests)

add_executable(uvsr_scene_loading_tests tests/scene_loading_tests.cpp)
target_include_directories(uvsr_scene_loading_tests PRIVATE src)
add_test(NAME uvsr_scene_loading_reference COMMAND uvsr_scene_loading_tests)

add_executable(uvsr_auto_exposure_tests tests/auto_exposure_tests.cpp)
target_include_directories(
    uvsr_auto_exposure_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/include")
set(UVSR_AUTO_EXPOSURE_HISTOGRAM_DXIL
    "${UVSR_CORE_SHADER_STAGING_DIR}/objects/auto_exposure_histogram_cs.be081355e9dd110e.dxil")
set(UVSR_AUTO_EXPOSURE_RESOLVE_DXIL
    "${UVSR_CORE_SHADER_STAGING_DIR}/objects/auto_exposure_resolve_cs.85cad487cbd400e1.dxil")
set(UVSR_AGX_AUTOMATIC_DXIL
    "${UVSR_CORE_SHADER_STAGING_DIR}/objects/agx_tonemapping_ps.da00ff8798f9ae54.dxil")
set(UVSR_AGX_UNITY_DXIL
    "${UVSR_CORE_SHADER_STAGING_DIR}/objects/agx_tonemapping_ps.ef1b2525bc917b3f.dxil")
add_custom_target(
    uvsr_auto_exposure_reflection_inputs
    DEPENDS
        "${UVSR_AUTO_EXPOSURE_HISTOGRAM_DXIL}"
        "${UVSR_AUTO_EXPOSURE_RESOLVE_DXIL}"
        "${UVSR_AGX_AUTOMATIC_DXIL}"
        "${UVSR_AGX_UNITY_DXIL}")
add_dependencies(
    uvsr_auto_exposure_tests
    uvsr_auto_exposure_reflection_inputs)
add_test(
    NAME uvsr_auto_exposure_reference
    COMMAND uvsr_auto_exposure_tests)
add_test(
    NAME uvsr_auto_exposure_reflection
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_DXC_EXECUTABLE=${UVSR_DXC_EXECUTABLE}"
        "-DUVSR_AUTO_EXPOSURE_HISTOGRAM_DXIL=${UVSR_AUTO_EXPOSURE_HISTOGRAM_DXIL}"
        "-DUVSR_AUTO_EXPOSURE_RESOLVE_DXIL=${UVSR_AUTO_EXPOSURE_RESOLVE_DXIL}"
        "-DUVSR_AGX_UNITY_DXIL=${UVSR_AGX_UNITY_DXIL}"
        "-DUVSR_AGX_AUTOMATIC_DXIL=${UVSR_AGX_AUTOMATIC_DXIL}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/auto_exposure_reflection.cmake")

add_executable(uvsr_radial_visibility_tests tests/radial_visibility_mask_tests.cpp)
target_include_directories(uvsr_radial_visibility_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(NAME uvsr_radial_visibility_reference COMMAND uvsr_radial_visibility_tests)

add_executable(uvsr_visibility_estimator_tests
    tests/visibility_estimator_reference_tests.cpp
)
target_include_directories(
    uvsr_visibility_estimator_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_visibility_estimator_reference
    COMMAND uvsr_visibility_estimator_tests
)

add_executable(uvsr_visibility_projection_tests
    tests/visibility_projection_tests.cpp
)
target_include_directories(
    uvsr_visibility_projection_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_visibility_projection_reference
    COMMAND uvsr_visibility_projection_tests
)

add_executable(uvsr_visibility_sampling_tests
    tests/visibility_sampling_reference_tests.cpp
)
target_include_directories(
    uvsr_visibility_sampling_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_visibility_sampling_reference
    COMMAND uvsr_visibility_sampling_tests
)

add_executable(uvsr_ui_animation_tests
    tests/ui_animation_tests.cpp
)
target_include_directories(
    uvsr_ui_animation_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_ui_animation_reference
    COMMAND uvsr_ui_animation_tests
)

add_executable(uvsr_ui_skin_tests
    tests/ui_skin_tests.cpp
)
target_include_directories(
    uvsr_ui_skin_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_ui_skin_reference
    COMMAND uvsr_ui_skin_tests
)

add_executable(uvsr_ui_font_family_tests
    tests/ui_font_family_tests.cpp
)
target_include_directories(
    uvsr_ui_font_family_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_ui_font_family_reference
    COMMAND uvsr_ui_font_family_tests
)

add_executable(uvsr_ui_commands_tests
    tests/ui_commands_tests.cpp
)
target_include_directories(
    uvsr_ui_commands_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_ui_commands_reference
    COMMAND uvsr_ui_commands_tests
)

# Direct catalog publication and runtime-lock topology. Drawer rendering is
# covered by the retained runtime and local-hardware visual gate.
add_executable(uvsr_ui_source_contract_tests
    tests/ui_source_contract_tests.cpp)
target_include_directories(uvsr_ui_source_contract_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(uvsr_ui_source_contract_tests PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(uvsr_ui_source_contract_tests PRIVATE
        /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_ui_source_contract
    COMMAND uvsr_ui_source_contract_tests)

add_executable(uvsr_ui_command_layout_tests
    tests/ui_command_layout_tests.cpp
)
target_include_directories(
    uvsr_ui_command_layout_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_ui_command_layout_reference
    COMMAND uvsr_ui_command_layout_tests
)

add_executable(uvsr_ui_settings_command_catalog_tests
    tests/ui_settings_command_catalog_tests.cpp
)
target_include_directories(
    uvsr_ui_settings_command_catalog_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_ui_settings_command_catalog_reference
    COMMAND uvsr_ui_settings_command_catalog_tests
)

add_executable(uvsr_settings_snapshot_tests
    tests/settings_snapshot_tests.cpp
)
target_include_directories(
    uvsr_settings_snapshot_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_settings_snapshot_reference
    COMMAND uvsr_settings_snapshot_tests
)
add_executable(uvsr_settings_snapshot_schema_probe
    tools/settings_snapshot_schema_probe.cpp
)
target_include_directories(
    uvsr_settings_snapshot_schema_probe
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_settings_snapshot_schema_registry
    COMMAND uvsr_settings_snapshot_schema_probe --check
)
add_executable(uvsr_settings_snapshot_decoder_tests
    tests/settings_snapshot_decoder_tests.cpp
    src/settings_snapshot_decoder.cpp)
target_include_directories(
    uvsr_settings_snapshot_decoder_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_settings_snapshot_decoder_reference
    COMMAND uvsr_settings_snapshot_decoder_tests)

add_executable(uvsr_settings_snapshot_transaction_tests
    tests/settings_snapshot_transaction_tests.cpp
    src/settings_snapshot_transaction.cpp)
target_include_directories(
    uvsr_settings_snapshot_transaction_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(
    uvsr_settings_snapshot_transaction_tests PRIVATE cxx_std_17)
add_test(
    NAME uvsr_settings_snapshot_transaction_reference
    COMMAND uvsr_settings_snapshot_transaction_tests)

add_executable(uvsr_settings_snapshot_selector_coordinator_tests
    tests/settings_snapshot_selector_coordinator_tests.cpp
    src/settings_snapshot_transaction.cpp)
target_include_directories(
    uvsr_settings_snapshot_selector_coordinator_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(
    uvsr_settings_snapshot_selector_coordinator_tests PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(
        uvsr_settings_snapshot_selector_coordinator_tests
        PRIVATE /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_settings_snapshot_selector_coordinator_reference
    COMMAND uvsr_settings_snapshot_selector_coordinator_tests)

add_executable(uvsr_settings_snapshot_decoder
    tools/settings_snapshot_decoder.cpp
    src/settings_snapshot_decoder.cpp)
target_include_directories(
    uvsr_settings_snapshot_decoder
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(uvsr_engine_identity_tests
    tests/engine_identity_tests.cpp)
target_include_directories(
    uvsr_engine_identity_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_engine_identity_reference
    COMMAND uvsr_engine_identity_tests)

add_executable(uvsr_engine_diagnostics_tests
    tests/engine_diagnostics_tests.cpp
    src/engine_diagnostics.cpp
    "${UVSR_BUILD_IDENTITY_SOURCE}")
target_include_directories(
    uvsr_engine_diagnostics_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_dependencies(uvsr_engine_diagnostics_tests uvsr_refresh_build_identity)
add_test(
    NAME uvsr_engine_diagnostics_reference
    COMMAND uvsr_engine_diagnostics_tests)

add_executable(uvsr_renderer_nvrhi_message_callback_tests
    tests/renderer_nvrhi_message_callback_tests.cpp
    src/renderer_nvrhi_message_callback.cpp
    src/renderer_log.cpp
    src/engine_startup.cpp)
target_include_directories(
    uvsr_renderer_nvrhi_message_callback_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    uvsr_renderer_nvrhi_message_callback_tests
    PRIVATE nvrhi Microsoft::DirectX-Headers bcrypt d3d12 shell32 ole32)
target_compile_features(
    uvsr_renderer_nvrhi_message_callback_tests PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(
        uvsr_renderer_nvrhi_message_callback_tests
        PRIVATE /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_renderer_nvrhi_message_callback_reference
    COMMAND uvsr_renderer_nvrhi_message_callback_tests
        "${CMAKE_CURRENT_BINARY_DIR}/nvrhi-message-callback/$<CONFIG>")

add_executable(uvsr_renderer_shell_failure_policy_tests
    tests/renderer_shell_failure_policy_tests.cpp)
target_include_directories(
    uvsr_renderer_shell_failure_policy_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(
    uvsr_renderer_shell_failure_policy_tests PRIVATE cxx_std_17)
if (MSVC)
    target_compile_options(
        uvsr_renderer_shell_failure_policy_tests
        PRIVATE /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_renderer_shell_failure_policy_reference
    COMMAND uvsr_renderer_shell_failure_policy_tests)

add_executable(uvsr_legal_inventory_validator
    tools/legal_inventory_validator.cpp)
target_link_libraries(uvsr_legal_inventory_validator PRIVATE bcrypt)
add_test(
    NAME uvsr_legal_inventory_reference
    COMMAND uvsr_legal_inventory_validator
        "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(uvsr_launcher_feed_contract_validator
    tools/launcher_feed_contract_validator.cpp)
if (MSVC)
    target_compile_options(uvsr_launcher_feed_contract_validator PRIVATE
        /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_launcher_feed_contract_self_test
    COMMAND uvsr_launcher_feed_contract_validator --self-test)

add_test(
    NAME uvsr_renderer_package_contract_self_test
    COMMAND uvsr_renderer_package_contract_validator
        --self-test
        --shader-inventory
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-shader-inventory.def"
        --media-inventory
        "${UVSR_RUNTIME_MEDIA_INVENTORY}"
        --d3d12-core
        "${UVSR_D3D12_AGILITY_CORE_DLL}")

add_executable(uvsr_override_inventory_validator
    tools/override_inventory_validator.cpp)
if (MSVC)
    target_compile_options(uvsr_override_inventory_validator PRIVATE
        /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_override_inventory_self_test
    COMMAND uvsr_override_inventory_validator --self-test)
add_test(
    NAME uvsr_override_inventory_reference
    COMMAND uvsr_override_inventory_validator --check
        "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(uvsr_imgui_dropdown_roll_tests
    tests/imgui_dropdown_roll_tests.cpp
)
target_include_directories(
    uvsr_imgui_dropdown_roll_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    uvsr_imgui_dropdown_roll_tests
    PRIVATE imgui
)
add_test(
    NAME uvsr_imgui_dropdown_roll_lifecycle
    COMMAND uvsr_imgui_dropdown_roll_tests
)

add_executable(uvsr_pixel_zoom_tests
    tests/pixel_zoom_tests.cpp
)
target_include_directories(
    uvsr_pixel_zoom_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if (MSVC)
    target_compile_options(uvsr_pixel_zoom_tests PRIVATE
        /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_pixel_zoom_reference
    COMMAND uvsr_pixel_zoom_tests
)
add_test(
    NAME uvsr_pixel_zoom_reflection
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_DXC_EXECUTABLE=${UVSR_DXC_EXECUTABLE}"
        "-DUVSR_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DUVSR_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/pixel-zoom-reflection/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/pixel_zoom_reflection.cmake"
)
endif()

# Donut builds its complete shader catalog outside the runtime tree. One
# checked-in inventory owns the exact subset a renderer package may contain.
set(UVSR_RUNTIME_SHADER_INVENTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-shader-inventory.def")
file(STRINGS "${UVSR_RUNTIME_SHADER_INVENTORY}"
    UVSR_RUNTIME_SHADER_PACKAGE_PATHS)
set(UVSR_SORTED_RUNTIME_SHADER_PACKAGE_PATHS
    ${UVSR_RUNTIME_SHADER_PACKAGE_PATHS})
list(SORT UVSR_SORTED_RUNTIME_SHADER_PACKAGE_PATHS)
list(REMOVE_DUPLICATES UVSR_SORTED_RUNTIME_SHADER_PACKAGE_PATHS)
list(LENGTH UVSR_RUNTIME_SHADER_PACKAGE_PATHS
    UVSR_RUNTIME_SHADER_PACKAGE_PATH_COUNT)
list(LENGTH UVSR_SORTED_RUNTIME_SHADER_PACKAGE_PATHS
    UVSR_SORTED_RUNTIME_SHADER_PACKAGE_PATH_COUNT)
if (NOT UVSR_RUNTIME_SHADER_PACKAGE_PATH_COUNT EQUAL 47 OR
    NOT UVSR_SORTED_RUNTIME_SHADER_PACKAGE_PATH_COUNT EQUAL 47 OR
    NOT "${UVSR_RUNTIME_SHADER_PACKAGE_PATHS}" STREQUAL
        "${UVSR_SORTED_RUNTIME_SHADER_PACKAGE_PATHS}")
    message(FATAL_ERROR
        "Runtime shader inventory must contain 47 unique sorted paths")
endif()
set(UVSR_ROOT_SHADERS)
set(UVSR_PASS_SHADERS)
set(UVSR_APP_SHADERS)
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
endforeach()

set(UVSR_RUNTIME_SHADER_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/framework/dxil")
set(UVSR_RUNTIME_APP_SHADER_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/uvsr/dxil")
set(UVSR_RUNTIME_SHADER_MANIFEST
    "${CMAKE_CURRENT_BINARY_DIR}/uvsr_runtime_shaders.manifest")
set(UVSR_RUNTIME_SHADER_MANIFEST_CONTENT
    "AA config: ${UVSR_SHADER_CONFIG}\nFramework root:\n")
string(JOIN "\n" UVSR_RUNTIME_ROOT_SHADER_MANIFEST
    ${UVSR_ROOT_SHADERS})
string(APPEND UVSR_RUNTIME_SHADER_MANIFEST_CONTENT
    "${UVSR_RUNTIME_ROOT_SHADER_MANIFEST}\nFramework passes:\n")
string(JOIN "\n" UVSR_RUNTIME_PASS_SHADER_MANIFEST
    ${UVSR_PASS_SHADERS})
string(APPEND UVSR_RUNTIME_SHADER_MANIFEST_CONTENT
    "${UVSR_RUNTIME_PASS_SHADER_MANIFEST}\n")

string(APPEND UVSR_RUNTIME_SHADER_MANIFEST_CONTENT
    "UVSR shaders:\n")
string(JOIN "\n" UVSR_RUNTIME_APP_SHADER_MANIFEST
    ${UVSR_APP_SHADERS})
string(APPEND UVSR_RUNTIME_SHADER_MANIFEST_CONTENT
    "${UVSR_RUNTIME_APP_SHADER_MANIFEST}\n")
file(GENERATE
    OUTPUT "${UVSR_RUNTIME_SHADER_MANIFEST}"
    CONTENT "${UVSR_RUNTIME_SHADER_MANIFEST_CONTENT}"
)

function(uvsr_require_shader_config_packaged
        shader_config
        permitted_shader_list_name
        component_shader_list_name)
        file(STRINGS "${shader_config}" shader_config_lines)
        foreach(shader_config_line IN LISTS shader_config_lines)
            string(REGEX MATCH
                "^[^ \t#]+"
                shader_source
                "${shader_config_line}")
            if (shader_source STREQUAL "")
                continue()
            endif()

            get_filename_component(shader_name "${shader_source}" NAME_WE)
            set(packaged_shader_name "${shader_name}")
            string(REGEX MATCH
                "(^|[ \t])-s[ \t]+([^ \t]+)"
                shader_suffix_match
                "${shader_config_line}")
            if (NOT shader_suffix_match STREQUAL "")
                string(APPEND packaged_shader_name "${CMAKE_MATCH_2}")
            else()
                string(REGEX MATCH
                    "(^|[ \t])-E[ \t]+([^ \t]+)"
                    shader_entry_match
                    "${shader_config_line}")
                if (NOT shader_entry_match STREQUAL "" AND
                    NOT CMAKE_MATCH_2 STREQUAL "main")
                    string(APPEND packaged_shader_name "_${CMAKE_MATCH_2}")
                endif()
            endif()

            if (NOT packaged_shader_name IN_LIST
                    ${permitted_shader_list_name})
                message(FATAL_ERROR
                    "Shader '${packaged_shader_name}' is compiled by "
                    "${shader_config} but missing from "
                    "${permitted_shader_list_name} runtime packaging")
            endif()
            if (NOT component_shader_list_name STREQUAL "")
                if (NOT packaged_shader_name IN_LIST ${component_shader_list_name})
                    message(FATAL_ERROR
                        "Shader '${packaged_shader_name}' is compiled by "
                        "${shader_config} but missing from "
                        "${component_shader_list_name} staging ownership")
                endif()
            endif()
        endforeach()
    endfunction()

    uvsr_require_shader_config_packaged(
        "${UVSR_SHADER_CONFIG}"
        UVSR_APP_SHADERS
        "")

    set(UVSR_RUNTIME_SHADER_PATH_MANIFEST_ENTRIES)
    set(UVSR_RUNTIME_SHADER_SOURCE_MANIFEST_ENTRIES)
    set(UVSR_RUNTIME_SHADER_BYPRODUCTS)
    foreach(shader IN LISTS UVSR_ROOT_SHADERS)
        list(APPEND UVSR_RUNTIME_SHADER_PATH_MANIFEST_ENTRIES
            "framework/dxil/${shader}.bin")
        list(APPEND UVSR_RUNTIME_SHADER_SOURCE_MANIFEST_ENTRIES
            "${UVSR_DONUT_SHADER_STAGING_DIR}/dxil/${shader}.bin|framework/dxil/${shader}.bin")
        list(APPEND UVSR_RUNTIME_SHADER_BYPRODUCTS
            "${UVSR_RUNTIME_SHADER_DIR}/${shader}.bin")
    endforeach()
    foreach(shader IN LISTS UVSR_PASS_SHADERS)
        list(APPEND UVSR_RUNTIME_SHADER_PATH_MANIFEST_ENTRIES
            "framework/dxil/passes/${shader}.bin")
        list(APPEND UVSR_RUNTIME_SHADER_SOURCE_MANIFEST_ENTRIES
            "${UVSR_DONUT_SHADER_STAGING_DIR}/dxil/passes/${shader}.bin|framework/dxil/passes/${shader}.bin")
        list(APPEND UVSR_RUNTIME_SHADER_BYPRODUCTS
            "${UVSR_RUNTIME_SHADER_DIR}/passes/${shader}.bin")
    endforeach()
    foreach(shader IN LISTS UVSR_APP_SHADERS)
        set(shader_staging_dir "${UVSR_CORE_SHADER_STAGING_DIR}")
        list(APPEND UVSR_RUNTIME_SHADER_PATH_MANIFEST_ENTRIES
            "uvsr/dxil/${shader}.bin")
        list(APPEND UVSR_RUNTIME_SHADER_SOURCE_MANIFEST_ENTRIES
            "${shader_staging_dir}/dxil/${shader}.bin|uvsr/dxil/${shader}.bin")
        list(APPEND UVSR_RUNTIME_SHADER_BYPRODUCTS
            "${UVSR_RUNTIME_APP_SHADER_DIR}/${shader}.bin")
    endforeach()
    list(SORT UVSR_RUNTIME_SHADER_PATH_MANIFEST_ENTRIES)
    list(SORT UVSR_RUNTIME_SHADER_SOURCE_MANIFEST_ENTRIES)
    list(SORT UVSR_RUNTIME_SHADER_BYPRODUCTS)
    set(UVSR_RUNTIME_SHADER_UNIQUE_PATHS
        ${UVSR_RUNTIME_SHADER_PATH_MANIFEST_ENTRIES})
    list(REMOVE_DUPLICATES UVSR_RUNTIME_SHADER_UNIQUE_PATHS)
    list(LENGTH UVSR_RUNTIME_SHADER_PATH_MANIFEST_ENTRIES
        UVSR_RUNTIME_SHADER_PATH_COUNT)
    list(LENGTH UVSR_RUNTIME_SHADER_UNIQUE_PATHS
        UVSR_RUNTIME_SHADER_UNIQUE_PATH_COUNT)
    if (NOT UVSR_RUNTIME_SHADER_PATH_COUNT EQUAL
            UVSR_RUNTIME_SHADER_UNIQUE_PATH_COUNT)
        message(FATAL_ERROR
            "The UVSR runtime shader manifest contains duplicate paths")
    endif()
    string(JOIN "\n" UVSR_RUNTIME_SHADER_PATH_MANIFEST_CONTENT
        ${UVSR_RUNTIME_SHADER_PATH_MANIFEST_ENTRIES})
    set(UVSR_RUNTIME_SHADER_PATH_MANIFEST
        "${CMAKE_BINARY_DIR}/uvsr_runtime_shader_paths.manifest")
    file(GENERATE
        OUTPUT "${UVSR_RUNTIME_SHADER_PATH_MANIFEST}"
        CONTENT "${UVSR_RUNTIME_SHADER_PATH_MANIFEST_CONTENT}\n")
    string(JOIN "\n" UVSR_RUNTIME_SHADER_SOURCE_MANIFEST_CONTENT
        ${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST_ENTRIES})
    set(UVSR_RUNTIME_SHADER_SOURCE_MANIFEST
        "${CMAKE_BINARY_DIR}/uvsr_runtime_shader_sources.manifest")
    file(GENERATE
        OUTPUT "${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}"
        CONTENT "${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST_CONTENT}\n")

    set(UVSR_RUNTIME_SHADER_STAGE_STAMP
        "${CMAKE_BINARY_DIR}/uvsr_runtime_shaders.stamp")
    set(UVSR_RUNTIME_SHADER_TARGET_DEPENDENCIES
        donut_shaders
        uvsr_shaders)
    set(UVSR_RUNTIME_SHADER_FILE_DEPENDENCIES
        ${UVSR_DONUT_SHADER_OUTPUTS}
        ${UVSR_SHADER_OUTPUTS}
        "${UVSR_RUNTIME_SHADER_MANIFEST}"
        "${UVSR_RUNTIME_SHADER_PATH_MANIFEST}"
        "${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}"
        "${UVSR_SHADER_CONFIG}"
        "${UVSR_DONUT_SHADER_CONFIG}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncRuntimeShaderBundle.cmake")
    add_custom_command(
        OUTPUT "${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
        BYPRODUCTS ${UVSR_RUNTIME_SHADER_BYPRODUCTS}
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND}
            "-DUVSR_RUNTIME_SHADER_SOURCE_MANIFEST=${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}"
            "-DUVSR_RUNTIME_SHADER_ALLOWED_PARENT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            "-DUVSR_RUNTIME_SHADER_ROOT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
            "-DUVSR_RUNTIME_SHADER_STAGE_STAMP=${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncRuntimeShaderBundle.cmake"
        COMMAND ${CMAKE_COMMAND} -E touch
            "${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
        DEPENDS
            ${UVSR_RUNTIME_SHADER_TARGET_DEPENDENCIES}
            ${UVSR_RUNTIME_SHADER_FILE_DEPENDENCIES}
        COMMENT "Synchronizing the exact UVSR runtime shader bundle"
        VERBATIM)
    add_custom_target(uvsr_stage_runtime_shaders
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND}
            "-DUVSR_RUNTIME_SHADER_SOURCE_MANIFEST=${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}"
            "-DUVSR_RUNTIME_SHADER_ALLOWED_PARENT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            "-DUVSR_RUNTIME_SHADER_ROOT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
            "-DUVSR_RUNTIME_SHADER_STAGE_STAMP=${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncRuntimeShaderBundle.cmake"
        DEPENDS
            "${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncRuntimeShaderBundle.cmake"
        COMMENT "Verifying the exact UVSR runtime shader bundle"
        VERBATIM)
    add_dependencies(uvsr_stage_runtime_shaders
        ${UVSR_RUNTIME_SHADER_TARGET_DEPENDENCIES})
    add_dependencies(uvsr-engine uvsr_stage_runtime_shaders)

    if (BUILD_TESTING)
    add_executable(uvsr_runtime_shader_bundle_tests
        tests/runtime_shader_bundle_tests.cpp)
    add_dependencies(uvsr_runtime_shader_bundle_tests
        uvsr_stage_runtime_shaders)
    add_test(
        NAME uvsr_runtime_shader_bundle_contract
        COMMAND uvsr_runtime_shader_bundle_tests
            "${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    add_test(
        NAME uvsr_runtime_shader_sync_reference
        COMMAND "${CMAKE_COMMAND}"
            "-DUVSR_SYNC_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncRuntimeShaderBundle.cmake"
            "-DUVSR_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/runtime-shader-sync-test/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestRuntimeShaderSync.cmake")
    set_tests_properties(
        uvsr_runtime_shader_bundle_contract
        uvsr_runtime_shader_sync_reference
        PROPERTIES LABELS "runtime;shaders")

    add_executable(
        uvsr_production_shader_bundle_tests
        tests/production_shader_bundle_tests.cpp)
    target_compile_features(
        uvsr_production_shader_bundle_tests PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(
            uvsr_production_shader_bundle_tests PRIVATE
            /permissive- /W4 /WX)
    endif()
    add_dependencies(
        uvsr_production_shader_bundle_tests
        uvsr_stage_runtime_shaders)
    add_test(
        NAME uvsr_production_shader_bundle_contract
        COMMAND uvsr_production_shader_bundle_tests
            "${UVSR_CORE_SHADER_STAGING_DIR}/catalogs"
            "${UVSR_RUNTIME_SHADER_MANIFEST}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    set_tests_properties(
        uvsr_production_shader_bundle_contract
        PROPERTIES LABELS "production;shaders")

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
set(UVSR_BUNDLED_SCENE_DIRECTORIES
    bistro_interior_retextured
    san_miguel_retextured)
set(UVSR_SCENE_RUNTIME_ASSETS)
foreach(UVSR_SCENE_DIRECTORY IN LISTS UVSR_BUNDLED_SCENE_DIRECTORIES)
    file(GLOB_RECURSE UVSR_CURRENT_SCENE_RUNTIME_ASSETS
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${UVSR_SCENE_SOURCE_ROOT}/${UVSR_SCENE_DIRECTORY}/*.scene.json"
        "${UVSR_SCENE_SOURCE_ROOT}/${UVSR_SCENE_DIRECTORY}/*.gltf"
        "${UVSR_SCENE_SOURCE_ROOT}/${UVSR_SCENE_DIRECTORY}/*.glb"
        "${UVSR_SCENE_SOURCE_ROOT}/${UVSR_SCENE_DIRECTORY}/*.bin"
        "${UVSR_SCENE_SOURCE_ROOT}/${UVSR_SCENE_DIRECTORY}/*.png")
    list(APPEND UVSR_SCENE_RUNTIME_ASSETS
        ${UVSR_CURRENT_SCENE_RUNTIME_ASSETS})
endforeach()
list(SORT UVSR_SCENE_RUNTIME_ASSETS)

if (BUILD_TESTING)
add_executable(uvsr_scene_asset_contract_tests
    tests/scene_asset_contract_tests.cpp
    src/scene_catalog.cpp)
target_include_directories(uvsr_scene_asset_contract_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_scene_asset_contract_tests
    PRIVATE donut_core cgltf)
add_test(
    NAME uvsr_scene_asset_contract
    COMMAND uvsr_scene_asset_contract_tests
        "${UVSR_SCENE_SOURCE_ROOT}"
        "${UVSR_SCENE_STAGING_ROOT}"
        "${UVSR_RUNTIME_MEDIA_INVENTORY}")
add_test(
    NAME uvsr_scene_asset_provenance
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_MODELS_ROOT=${UVSR_SCENE_SOURCE_ROOT}"
        "-DSTAGED_MODELS_ROOT=${UVSR_SCENE_STAGING_ROOT}"
        "-DRUNTIME_MEDIA_INVENTORY=${UVSR_RUNTIME_MEDIA_INVENTORY}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_asset_provenance.cmake")
set_tests_properties(
    uvsr_scene_asset_contract
    uvsr_scene_asset_provenance
    PROPERTIES
        LABELS "assets;scenes"
        TIMEOUT 600)
endif()

set(UVSR_NOTO_SANS_SOURCE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/noto-sans")
set(UVSR_UI_REGULAR_FONT_SOURCE
    "${UVSR_NOTO_SANS_SOURCE_ROOT}/NotoSans-Regular.ttf")
set(UVSR_UI_SEMIBOLD_FONT_SOURCE
    "${UVSR_NOTO_SANS_SOURCE_ROOT}/NotoSans-SemiBold.ttf")
set(UVSR_UI_BOLD_FONT_SOURCE
    "${UVSR_NOTO_SANS_SOURCE_ROOT}/NotoSans-Bold.ttf")
set(UVSR_UI_FONT_LICENSE_SOURCE
    "${UVSR_NOTO_SANS_SOURCE_ROOT}/OFL.txt")
set(UVSR_PROGGY_CLEAN_LICENSE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/proggy-clean/ProggyClean-MIT.txt")

function(uvsr_require_exact_ui_asset asset expected_size expected_sha256 label)
    if (NOT EXISTS "${asset}")
        message(FATAL_ERROR
            "${label} is missing from the checked-in UVSR UI assets")
    endif()
    file(SIZE "${asset}" actual_size)
    if (NOT actual_size EQUAL expected_size)
        message(FATAL_ERROR
            "${label} has size ${actual_size}; expected ${expected_size}. "
            "Restore the exact checked-in UI asset.")
    endif()
    file(SHA256 "${asset}" actual_sha256)
    string(TOUPPER "${actual_sha256}" actual_sha256)
    if (NOT actual_sha256 STREQUAL expected_sha256)
        message(FATAL_ERROR
            "${label} failed its SHA-256 check. "
            "Restore the exact checked-in UI asset.")
    endif()
endfunction()

uvsr_require_exact_ui_asset(
    "${UVSR_UI_REGULAR_FONT_SOURCE}"
    621572
    "478C558EA716033CD60C03438F628DFA75694DCF6B5F6D505A2F05FD2B4F3823"
    "Noto Sans Regular")
uvsr_require_exact_ui_asset(
    "${UVSR_UI_SEMIBOLD_FONT_SOURCE}"
    625052
    "A4E91FD530AC2B4EF5367240144FF37D7D65D66CF76F2E9A2187B93C676F92D0"
    "Noto Sans SemiBold")
uvsr_require_exact_ui_asset(
    "${UVSR_UI_BOLD_FONT_SOURCE}"
    631484
    "1DF075A380FC7CB898ACF64C1F7B3B4DD780DE3CAA860178BF929DE35817A913"
    "Noto Sans Bold")
uvsr_require_exact_ui_asset(
    "${UVSR_UI_FONT_LICENSE_SOURCE}"
    4396
    "CEE9892F9F0CC8FE882C9E9537EE6A89621D86EE7CEAF70B02E2B2B1C25C061A"
    "Noto Sans OFL 1.1 license")
uvsr_require_exact_ui_asset(
    "${UVSR_PROGGY_CLEAN_LICENSE_SOURCE}"
    1082
    "8B802D79F256D29B45AD253323D212FA14CA952A20DCD227CFBCDB3D140BFE7C"
    "ProggyClean MIT license notice")
uvsr_require_exact_ui_asset(
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Intel-XeGTAO-MIT.txt"
    1081
    "1F3BFD6B628535F0F0E779E70C260CFDC1BB45BC8644D7108E65F83E24A05CBA"
    "Intel XeGTAO MIT license notice")
uvsr_require_exact_ui_asset(
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt"
    1053
    "50A4BE869E51722A4CA90819535A78DF8F82D68FACBAD52A2DA6AB4DC284AD55"
    "Andrew Helmer stochastic-generation MIT license notice")
uvsr_require_exact_ui_asset(
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/TinyEXR-and-OpenEXR-BSD.txt"
    3116
    "0D3E165809F0C704B67E8CD860E5E0E148122F70A9D2AC90400D3DF9482C67F4"
    "TinyEXR and embedded OpenEXR BSD notices")
uvsr_require_exact_ui_asset(
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/NVIDIA-Donut-NVRHI-MIT.txt"
    1090
    "25179DAE1293243F83E3C1293BD8C13DBBAC4DD544F51A40A2DBB7C7CFD6E18A"
    "canonical NVIDIA Donut and NVRHI MIT notice")
uvsr_require_exact_ui_asset(
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Microsoft-DirectX-Headers-MIT.txt"
    1073
    "FD532481D828E13A0B13CCB598E02338A3617740675A862EE6BDC1541B68E93D"
    "canonical Microsoft DirectX-Headers MIT notice")
set(UVSR_MEDIA_MAPPINGS)
foreach(source_path IN LISTS UVSR_ENVIRONMENT_ASSETS)
    file(RELATIVE_PATH relative_path
        "${UVSR_ENVIRONMENT_SOURCE_DIR}" "${source_path}")
    list(APPEND UVSR_MEDIA_MAPPINGS
        "${source_path}" "environments/${relative_path}")
endforeach()
foreach(source_path IN LISTS UVSR_NOISE_ASSETS)
    file(RELATIVE_PATH relative_path
        "${UVSR_NOISE_SOURCE_DIR}" "${source_path}")
    list(APPEND UVSR_MEDIA_MAPPINGS
        "${source_path}" "uvsr/noise/${relative_path}")
endforeach()
foreach(source_path IN LISTS UVSR_SCENE_RUNTIME_ASSETS)
    file(RELATIVE_PATH relative_path
        "${UVSR_SCENE_SOURCE_ROOT}" "${source_path}")
    list(APPEND UVSR_MEDIA_MAPPINGS
        "${source_path}" "glTF-Sample-Assets/Models/${relative_path}")
endforeach()
list(APPEND UVSR_MEDIA_MAPPINGS
    "${UVSR_UI_REGULAR_FONT_SOURCE}" "fonts/NotoSans/NotoSans-Regular.ttf"
    "${UVSR_UI_SEMIBOLD_FONT_SOURCE}" "fonts/NotoSans/NotoSans-SemiBold.ttf"
    "${UVSR_UI_BOLD_FONT_SOURCE}" "fonts/NotoSans/NotoSans-Bold.ttf")
uvsr_add_mapped_asset_stage(
    uvsr_stage_media "${CMAKE_BINARY_DIR}/media" ${UVSR_MEDIA_MAPPINGS})
add_dependencies(uvsr-engine uvsr_stage_media)
if (BUILD_TESTING)
    add_dependencies(uvsr_scene_asset_contract_tests uvsr_stage_media)
endif()

set(UVSR_LICENSE_MAPPINGS
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md"
        "UVSR-Polyform-Noncommercial-1.0.0.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Microsoft-DirectX-Graphics-Samples-MIT.txt"
        "Microsoft-DirectX-Graphics-Samples.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Apache-2.0.txt"
        "Apache-2.0.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/BSD-2-Clause.txt"
        "BSD-2-Clause.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/IOLITE-AgX-MIT.txt"
        "IOLITE-AgX-MIT.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Intel-XeGTAO-MIT.txt"
        "Intel-XeGTAO-MIT.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt"
        "Andrew-Helmer-Stochastic-Generation-MIT.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/documentation/google-filament-fxaa.md"
        "Google-Filament-FXAA-Attribution.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/NVIDIA-Donut-NVRHI-MIT.txt"
        "NVIDIA-Donut-MIT.txt"
    "${UVSR_CGLTF_LICENSE_SOURCE}"
        "cgltf-MIT.txt"
    "${UVSR_GLFW_LICENSE_SOURCE}"
        "GLFW.txt"
    "${UVSR_JSONCPP_LICENSE_SOURCE}"
        "JsonCpp-Public-Domain-or-MIT.txt"
    "${UVSR_STB_LICENSE_SOURCE}"
        "stb-MIT-or-Public-Domain.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/TinyEXR-and-OpenEXR-BSD.txt"
        "TinyEXR-and-OpenEXR-BSD.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/NVIDIA-Donut-NVRHI-MIT.txt"
        "NVIDIA-NVRHI-MIT.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/LICENSE.txt"
        "Dear-ImGui-MIT.txt"
    "${UVSR_UI_FONT_LICENSE_SOURCE}"
        "Noto-Sans-OFL-1.1.txt"
    "${UVSR_PROGGY_CLEAN_LICENSE_SOURCE}"
        "ProggyClean-MIT.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/bistro_interior_retextured/LICENSE.txt"
        "Amazon-Lumberyard-Bistro.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/san_miguel_retextured/LICENSE.txt"
        "San-Miguel-2.1.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/environments/README.md"
        "Poly-Haven-Environments.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/documentation/third-party-notices.md"
        "third-party-notices.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Microsoft-DirectX-Headers-MIT.txt"
        "Microsoft-DirectX-Headers-MIT.txt"
    "${UVSR_D3D12_AGILITY_ROOT}/LICENSE.txt"
        "Microsoft-D3D12-Agility-SDK-Terms.txt"
    "${UVSR_D3D12_AGILITY_ROOT}/LICENSE-CODE.txt"
        "Microsoft-D3D12-Agility-SDK-Code-MIT.txt"
    "${UVSR_D3D12_AGILITY_ROOT}/distributable files.txt"
        "Microsoft-D3D12-Agility-SDK-Distributable-Files.txt"
    "${nrd_SOURCE_DIR}/LICENSE.txt"
        "NRD-LICENSE.txt"
    "${mathlib_SOURCE_DIR}/LICENSE.txt"
        "NVIDIA-MathLib-MIT.txt")

file(STRINGS "${UVSR_RUNTIME_MEDIA_INVENTORY}"
    UVSR_PROTECTED_RUNTIME_PACKAGE_PATHS)
set(UVSR_SORTED_PROTECTED_RUNTIME_PACKAGE_PATHS
    ${UVSR_PROTECTED_RUNTIME_PACKAGE_PATHS})
list(SORT UVSR_SORTED_PROTECTED_RUNTIME_PACKAGE_PATHS)
list(REMOVE_DUPLICATES UVSR_SORTED_PROTECTED_RUNTIME_PACKAGE_PATHS)
list(LENGTH UVSR_PROTECTED_RUNTIME_PACKAGE_PATHS
    UVSR_PROTECTED_RUNTIME_PACKAGE_PATH_COUNT)
if (NOT UVSR_PROTECTED_RUNTIME_PACKAGE_PATH_COUNT EQUAL 310 OR
    NOT "${UVSR_PROTECTED_RUNTIME_PACKAGE_PATHS}" STREQUAL
        "${UVSR_SORTED_PROTECTED_RUNTIME_PACKAGE_PATHS}")
    message(FATAL_ERROR
        "Runtime media inventory must contain 310 unique sorted paths")
endif()
set(UVSR_MAPPED_PROTECTED_RUNTIME_PATHS)
set(UVSR_MEDIA_MAPPING_ARGUMENTS ${UVSR_MEDIA_MAPPINGS})
while(UVSR_MEDIA_MAPPING_ARGUMENTS)
    list(POP_FRONT UVSR_MEDIA_MAPPING_ARGUMENTS ignored_source)
    list(POP_FRONT UVSR_MEDIA_MAPPING_ARGUMENTS mapped_path)
    list(APPEND UVSR_MAPPED_PROTECTED_RUNTIME_PATHS
        "media/${mapped_path}")
endwhile()
set(UVSR_LICENSE_MAPPING_ARGUMENTS ${UVSR_LICENSE_MAPPINGS})
set(UVSR_LICENSE_SOURCE_FILES)
while(UVSR_LICENSE_MAPPING_ARGUMENTS)
    list(POP_FRONT UVSR_LICENSE_MAPPING_ARGUMENTS source_path)
    list(POP_FRONT UVSR_LICENSE_MAPPING_ARGUMENTS mapped_path)
    list(APPEND UVSR_LICENSE_SOURCE_FILES "${source_path}")
    set(package_path "bin/licenses/${mapped_path}")
    if (package_path IN_LIST UVSR_PROTECTED_RUNTIME_PACKAGE_PATHS)
        list(APPEND UVSR_MAPPED_PROTECTED_RUNTIME_PATHS "${package_path}")
    endif()
endwhile()
list(SORT UVSR_MAPPED_PROTECTED_RUNTIME_PATHS)
if (NOT "${UVSR_MAPPED_PROTECTED_RUNTIME_PATHS}" STREQUAL
        "${UVSR_PROTECTED_RUNTIME_PACKAGE_PATHS}")
    message(FATAL_ERROR
        "Source-to-stage mappings differ from the protected runtime inventory")
endif()
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
        "set(UVSR_PACKAGE_PRODUCTION true)\nset(UVSR_RENDERER_RELEASE_SEQUENCE ${UVSR_RENDERER_RELEASE_SEQUENCE_VALUE})\ninclude(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteRendererPackageManifest.cmake\")")

    set(UVSR_PACKAGE_STAGING_DIRECTORY "${CMAKE_BINARY_DIR}/package")
    set(UVSR_PACKAGE_ARCHIVE
        "${CMAKE_BINARY_DIR}/artifacts/uvsr-renderer-windows-11-x64.zip")
    set(UVSR_PACKAGE_VALIDATION_DIRECTORY
        "${CMAKE_BINARY_DIR}/package-archive-validation")
    add_custom_target(uvsr-renderer-package
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
            -P "${UVSR_NRD_GENERATED_DXIL_INVOCATION}"
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
            --media-inventory "${UVSR_RUNTIME_MEDIA_INVENTORY}"
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
            --media-inventory "${UVSR_RUNTIME_MEDIA_INVENTORY}"
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
