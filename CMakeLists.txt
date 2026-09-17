cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES C CXX)

include(FetchContent)
include(CTest)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/NoCppExceptions.cmake")

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

add_library(uvsr_json STATIC src/json_document.cpp)
target_include_directories(uvsr_json PUBLIC src)
uvsr_disable_cpp_exceptions(uvsr_json PRIVATE)

add_library(uvsr_file_bytes STATIC src/file_bytes_win32.cpp)
target_include_directories(uvsr_file_bytes PUBLIC src)
uvsr_disable_cpp_exceptions(uvsr_file_bytes PRIVATE)

add_library(uvsr_sha256 STATIC src/sha256_win32.cpp)
target_include_directories(uvsr_sha256 PUBLIC src)
target_link_libraries(uvsr_sha256 PRIVATE bcrypt)
target_compile_definitions(uvsr_sha256 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
uvsr_disable_cpp_exceptions(uvsr_sha256 PRIVATE)
if (MSVC)
    target_compile_options(uvsr_sha256 PRIVATE /permissive- /W4 /WX)
endif()

add_library(uvsr_windows_paths STATIC src/windows_executable_path_win32.cpp src/windows_file_identity_win32.cpp)
add_library(uvsr_windows_paths_test STATIC EXCLUDE_FROM_ALL src/windows_executable_path_win32.cpp src/windows_file_identity_win32.cpp)
target_compile_definitions(uvsr_windows_paths_test PRIVATE UVSR_WINDOWS_PATH_TEST_HOOKS=1)
foreach(target uvsr_windows_paths uvsr_windows_paths_test)
    target_include_directories(${target} PUBLIC src)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    uvsr_disable_cpp_exceptions(${target} PRIVATE)
    if (MSVC)
        target_compile_options(${target} PRIVATE /permissive- /W4 /WX)
    endif()
endforeach()

add_library(uvsr_scene_catalog_path STATIC src/scene_catalog_path.cpp)
add_library(uvsr_windows_path_text STATIC src/windows_path_text_win32.cpp)
add_library(uvsr_windows_path_text_test STATIC EXCLUDE_FROM_ALL src/windows_path_text_win32.cpp)
target_compile_definitions(uvsr_windows_path_text_test PUBLIC UVSR_WINDOWS_PATH_TEXT_TEST_HOOKS=1)
foreach(target uvsr_scene_catalog_path uvsr_windows_path_text uvsr_windows_path_text_test)
    target_include_directories(${target} PUBLIC src)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    uvsr_disable_cpp_exceptions(${target} PRIVATE)
    if (MSVC)
        target_compile_options(${target} PRIVATE /permissive- /W4 /WX)
    endif()
endforeach()
target_link_libraries(uvsr_windows_paths PRIVATE uvsr_scene_catalog_path)
target_link_libraries(uvsr_windows_paths_test PRIVATE uvsr_scene_catalog_path)
target_link_libraries(uvsr_windows_path_text PRIVATE uvsr_scene_catalog_path)
target_link_libraries(uvsr_windows_path_text_test PRIVATE uvsr_scene_catalog_path)

add_library(uvsr_file_write STATIC src/file_write_win32.cpp)
add_library(uvsr_file_write_test STATIC EXCLUDE_FROM_ALL src/file_write_win32.cpp)
target_compile_definitions(uvsr_file_write_test PUBLIC UVSR_FILE_WRITE_TEST_HOOKS=1)
foreach(target uvsr_file_write uvsr_file_write_test)
    target_include_directories(${target} PUBLIC src)
    uvsr_disable_cpp_exceptions(${target} PRIVATE)
    if (MSVC)
        target_compile_options(${target} PRIVATE /permissive- /W4 /WX)
    endif()
endforeach()
target_link_libraries(uvsr_file_write PRIVATE uvsr_windows_paths)
target_link_libraries(uvsr_file_write_test PRIVATE uvsr_windows_paths_test)

if(BUILD_TESTING)
    add_library(uvsr_retained_runtime_output STATIC
        src/retained_runtime_capture_file.cpp src/retained_runtime_json.cpp
        src/retained_runtime_semantic.cpp src/retained_runtime_diagnostic.cpp src/retained_runtime_provenance.cpp)
    target_include_directories(uvsr_retained_runtime_output PUBLIC src)
    target_compile_definitions(uvsr_retained_runtime_output PRIVATE UVSR_RETAINED_CASE_TEST_HOOKS=1)
    # final consumers select the production or test settings-value implementation.
    target_link_libraries(uvsr_retained_runtime_output PUBLIC uvsr_json PRIVATE
        uvsr_windows_paths uvsr_windows_path_text uvsr_scene_catalog_path)
    target_compile_definitions(uvsr_retained_runtime_output PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    uvsr_disable_cpp_exceptions(uvsr_retained_runtime_output PRIVATE)
    if (MSVC)
        target_compile_options(uvsr_retained_runtime_output PRIVATE /permissive- /W4 /WX)
        # provenance retains each borrowed getenv value before the next query.
        set_source_files_properties(src/retained_runtime_provenance.cpp PROPERTIES
            COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
    endif()
    # library failure hooks must not select the application's developer paths.
    target_compile_definitions(uvsr_json PUBLIC UVSR_JSON_TEST_HOOKS=1)
    target_compile_definitions(uvsr_file_bytes PUBLIC UVSR_FILE_BYTES_TEST_HOOKS=1)
    uvsr_test(uvsr_json_document_tests uvsr_json_document tests/json_document_tests.cpp)
    target_link_libraries(uvsr_json_document_tests PRIVATE uvsr_json)
    uvsr_disable_cpp_exceptions(uvsr_json_document_tests)
    uvsr_test(uvsr_file_bytes_tests uvsr_file_bytes tests/file_bytes_tests.cpp)
    target_sources(uvsr_file_bytes_tests PRIVATE tests/noise_texture_asset_tests.cpp src/noise_texture_asset.cpp)
    target_compile_definitions(uvsr_file_bytes_tests PRIVATE UVSR_WINDOWS_PATH_TEST_HOOKS=1)
    target_link_libraries(uvsr_file_bytes_tests PRIVATE uvsr_file_bytes uvsr_windows_paths_test uvsr_windows_path_text_test)
    uvsr_disable_cpp_exceptions(uvsr_file_bytes_tests)
endif()

add_library(uvsr_settings_catalog STATIC
    src/settings_catalog.cpp src/settings_contract.cpp)
target_include_directories(uvsr_settings_catalog PUBLIC src)
target_link_libraries(uvsr_settings_catalog PUBLIC uvsr_json)
uvsr_disable_cpp_exceptions(uvsr_settings_catalog PRIVATE)
if (MSVC)
    target_compile_options(uvsr_settings_catalog PRIVATE /constexpr:steps100000000)
endif()

add_library(uvsr_settings_value STATIC src/settings_value.cpp)
add_library(uvsr_settings_value_test STATIC EXCLUDE_FROM_ALL src/settings_value.cpp)
target_compile_definitions(uvsr_settings_value_test PUBLIC UVSR_SETTINGS_VALUE_TEST_HOOKS=1)
foreach(target uvsr_settings_value uvsr_settings_value_test)
    target_include_directories(${target} PUBLIC src)
    target_link_libraries(${target} PUBLIC uvsr_settings_catalog)
    uvsr_disable_cpp_exceptions(${target} PRIVATE)
    if (MSVC)
        target_compile_options(${target} PRIVATE /permissive- /W4 /WX /constexpr:steps100000000)
    endif()
endforeach()
if(BUILD_TESTING)
    uvsr_test(uvsr_settings_value_tests uvsr_settings_value tests/settings_value_tests.cpp)
    target_link_libraries(uvsr_settings_value_tests PRIVATE uvsr_settings_value_test)
    uvsr_disable_cpp_exceptions(uvsr_settings_value_tests)
endif()

add_library(uvsr_settings_controller OBJECT src/uvsr_settings_commands.cpp)
target_include_directories(uvsr_settings_controller PRIVATE src)
target_compile_features(uvsr_settings_controller PRIVATE cxx_std_17)
uvsr_disable_cpp_exceptions(uvsr_settings_controller PRIVATE)
if (MSVC)
    target_compile_options(uvsr_settings_controller PRIVATE /permissive- /W4 /WX)
endif()

add_library(uvsr_settings_snapshot STATIC
    src/settings_snapshot_storage.cpp src/settings_snapshot_decoder.cpp src/settings_snapshot_catalogs_win32.cpp
    src/settings_snapshot_persistence_win32.cpp src/settings_snapshot_transaction.cpp src/settings_snapshot_legacy.cpp
    src/settings_snapshot_selectors.cpp $<TARGET_OBJECTS:uvsr_settings_controller>)
add_library(uvsr_settings_snapshot_test STATIC EXCLUDE_FROM_ALL
    src/settings_snapshot_storage.cpp src/settings_snapshot_decoder.cpp src/settings_snapshot_catalogs_win32.cpp
    src/settings_snapshot_persistence_win32.cpp src/settings_snapshot_transaction.cpp src/settings_snapshot_legacy.cpp
    src/settings_snapshot_selectors.cpp $<TARGET_OBJECTS:uvsr_settings_controller>)
target_compile_definitions(uvsr_settings_snapshot_test PUBLIC UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS=1)
foreach(target uvsr_settings_snapshot uvsr_settings_snapshot_test)
    target_include_directories(${target} PUBLIC src)
    target_link_libraries(${target} PUBLIC uvsr_file_bytes PRIVATE ole32 shell32)
    uvsr_disable_cpp_exceptions(${target} PRIVATE)
    if (MSVC)
        target_compile_options(${target} PRIVATE /permissive- /W4 /WX)
    endif()
endforeach()
target_link_libraries(uvsr_settings_snapshot PUBLIC uvsr_settings_value)
target_link_libraries(uvsr_settings_snapshot_test PUBLIC uvsr_settings_value_test)
target_link_libraries(uvsr_settings_snapshot PRIVATE uvsr_windows_paths uvsr_file_write)
target_link_libraries(uvsr_settings_snapshot_test PRIVATE uvsr_windows_paths_test uvsr_file_write_test)

add_library(uvsr_renderer_scene STATIC src/renderer_scene.cpp src/renderer_scene_draw.cpp
    src/renderer_scene_ray.cpp src/renderer_scene_encoding.cpp src/renderer_scene_material_mode.cpp src/renderer_scene_light.cpp)
target_include_directories(uvsr_renderer_scene PUBLIC src)
target_compile_definitions(uvsr_renderer_scene PRIVATE _HAS_EXCEPTIONS=0)
if (MSVC)
    target_compile_options(uvsr_renderer_scene PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise)
else()
    target_compile_options(uvsr_renderer_scene PRIVATE -fno-exceptions)
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

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuntimeAssetStage.cmake")

set(DONUT_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_DX12 ON CACHE BOOL "" FORCE)
set(DONUT_WITH_VULKAN OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchedSources.cmake")

set(UVSR_DONUT_APP_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_donut_app_overrides")
set(UVSR_DONUT_APP_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-app.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-render-disposition.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-frame-owner.patch")
set(UVSR_DONUT_APP_SOURCES
    include/donut/app/DeviceManager_DX12.h
    src/app/DeviceManager.cpp
    src/app/dx12/DeviceManager_DX12.cpp)
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_APP_PATCH_FILES}"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    include/donut/app/DeviceManager.h
    ${UVSR_DONUT_APP_SOURCES})

set(UVSR_TRANSITIONAL_DONUT_APP_STAGE_TREE_DIGEST
    "8f33819b5f72b0fa4447cbb15b1b5c09bef0bc81c1aeae511722f0ba2351d0da")

add_library(uvsr_shader_blob STATIC src/shader_bytecode.cpp)
target_include_directories(uvsr_shader_blob PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(uvsr_shader_blob_builder tools/shader_blob_builder.cpp)
target_compile_definitions(uvsr_shader_blob_builder PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
uvsr_disable_cpp_exceptions(uvsr_shader_blob_builder)
if(BUILD_TESTING)
    target_compile_definitions(uvsr_shader_blob_builder PRIVATE UVSR_BUILD_TESTING=1)
endif()
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
    uvsr_sha256 version uvsr_json uvsr_file_bytes)
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

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectThirdParty.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectImGui.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectNVRHI.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectFastgltf.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectDonut.cmake")
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
add_library(uvsr_log STATIC src/renderer_log.cpp)
target_include_directories(uvsr_log PUBLIC src)
target_compile_features(uvsr_log PRIVATE cxx_std_17)
target_compile_definitions(uvsr_log PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
# the plain callback contract does not expose a standard-library ABI.
uvsr_disable_cpp_exceptions(uvsr_log PRIVATE)

add_library(uvsr_engine_log STATIC src/engine_diagnostic_log_win32.cpp)
target_include_directories(uvsr_engine_log PUBLIC src)
target_link_libraries(uvsr_engine_log PUBLIC uvsr_log)
target_compile_features(uvsr_engine_log PRIVATE cxx_std_17)
target_compile_definitions(uvsr_engine_log PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
uvsr_disable_cpp_exceptions(uvsr_engine_log PRIVATE)
if (BUILD_TESTING)
    target_compile_definitions(uvsr_engine_log PRIVATE UVSR_ENGINE_LOG_TEST_HOOKS=1)
endif()

add_library(uvsr_camera STATIC src/camera_controllers.cpp src/renderer_vector_math.cpp)
target_include_directories(uvsr_camera PUBLIC src)
target_link_libraries(uvsr_camera PRIVATE glfw)
uvsr_disable_cpp_exceptions(uvsr_camera PRIVATE)
if(MSVC)
    target_compile_options(uvsr_camera PRIVATE /permissive- /W4 /WX /fp:precise)
endif()

add_library(uvsr_renderer_nvrhi STATIC
    src/renderer_shader_factory_nvrhi.cpp
    src/renderer_scene_descriptors_nvrhi.cpp
    src/pbr_binding_sets_nvrhi.cpp
    src/renderer_ui_nvrhi.cpp
    src/renderer_geometry_passes_nvrhi.cpp
    src/renderer_light_probe_processing_nvrhi.cpp
    src/renderer_common_passes_nvrhi.cpp
    src/renderer_pixel_readback_nvrhi.cpp
    src/renderer_texture_bmp_nvrhi.cpp)
target_include_directories(uvsr_renderer_nvrhi PUBLIC src)
target_link_libraries(uvsr_renderer_nvrhi PRIVATE uvsr_shader_blob uvsr_bmp uvsr_log nvrhi imgui)
target_compile_features(uvsr_renderer_nvrhi PRIVATE cxx_std_17)
target_compile_definitions(uvsr_renderer_nvrhi PRIVATE NOMINMAX)
uvsr_disable_cpp_exceptions(uvsr_renderer_nvrhi)

add_library(uvsr_bmp STATIC src/renderer_texture_bmp.cpp)
target_include_directories(uvsr_bmp PUBLIC src)
target_compile_features(uvsr_bmp PRIVATE cxx_std_17)
uvsr_disable_cpp_exceptions(uvsr_bmp)

add_library(uvsr_view STATIC src/renderer_view.cpp)
target_include_directories(uvsr_view PUBLIC src)
target_compile_features(uvsr_view PRIVATE cxx_std_17)
uvsr_disable_cpp_exceptions(uvsr_view)

add_library(uvsr_ui_context STATIC src/renderer_ui_context.cpp src/renderer_ui_input_glfw.cpp)
target_include_directories(uvsr_ui_context PUBLIC src)
target_link_libraries(uvsr_ui_context PRIVATE imgui glfw)
target_compile_features(uvsr_ui_context PRIVATE cxx_std_17)
target_compile_definitions(uvsr_ui_context PRIVATE NOMINMAX GLFW_INCLUDE_NONE)
uvsr_disable_cpp_exceptions(uvsr_ui_context)
if(BUILD_TESTING)
    target_compile_definitions(uvsr_renderer_nvrhi PRIVATE UVSR_BUILD_TESTING=1)
    target_compile_definitions(uvsr_ui_context PRIVATE UVSR_BUILD_TESTING=1)
endif()
uvsr_disable_cpp_exceptions(uvsr_shader_blob)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DirectDXC.cmake")
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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${UVSR_SHADER_CONFIG}")
uvsr_add_direct_shader_bundle(
    TARGET uvsr_shaders
    CONFIG "${UVSR_SHADER_CONFIG}"
    SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
    OUTPUT_DIRECTORY "${UVSR_CORE_SHADER_STAGING_DIR}"
    EXPECTED_TASKS 35
    OUTPUTS_VARIABLE UVSR_SHADER_OUTPUTS
    OBJECTS_VARIABLE UVSR_SHADER_OBJECTS
    ENABLE_16BIT_TYPES
    INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}")

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
        cgltf stb tinyexr glfw imgui uvsr_simdjson fastgltf
        uvsr_stb_image uvsr_tinyexr
        nvrhi nvrhi_d3d12 DirectX-Headers DirectX-Guids
        uvsr_shaders
        donut_core donut_app)
    if (TARGET "${dependency_target}")
        add_dependencies("${dependency_target}"
            uvsr_verify_direct_dependency_state)
    endif()
endforeach()

set_source_files_properties(src/camera_collision.cpp PROPERTIES COMPILE_DEFINITIONS "_HAS_EXCEPTIONS=0")
set_source_files_properties(src/engine_startup.cpp src/uvsr_command_line.cpp src/color_lut.cpp
    src/gpu_adapter_catalog.cpp src/scene_catalog.cpp src/scene_loading.cpp tests/scene_history_owner_tests.cpp tests/restart_command_line_tests.cpp src/noise_texture_asset.cpp tests/noise_texture_asset_tests.cpp src/world_space_representation_nvrhi.cpp src/image_based_lighting_environment_nvrhi.cpp src/image_based_lighting_radiance.cpp src/image_based_lighting_asset_path_win32.cpp tests/image_based_lighting_owner_tests.cpp src/windows_file_status_win32.cpp src/retained_scene_paths_win32.cpp src/color_lut_asset_path_win32.cpp tests/windows_path_text_tests.cpp src/ui_light_defaults.cpp tests/ui_light_defaults_tests.cpp src/uvsr.cpp src/uvsr_lifecycle.cpp src/uvsr_ui_renderer.cpp src/renderer_frame_donut.cpp src/uvsr_scene_lifecycle.cpp src/uvsr_render_frame.cpp src/uvsr_render_setup.cpp src/uvsr_path_tracing.cpp src/uvsr_ray_visibility.cpp src/uvsr_ui_panels.cpp src/uvsr_display_sync.cpp src/uvsr_ui_commands.cpp
    PROPERTIES COMPILE_DEFINITIONS "_HAS_EXCEPTIONS=0")
if (MSVC)
    set_source_files_properties(src/camera_collision.cpp PROPERTIES COMPILE_OPTIONS "/EHs-c-;/fp:precise")
    set_source_files_properties(src/engine_startup.cpp src/uvsr_command_line.cpp src/color_lut.cpp
    src/gpu_adapter_catalog.cpp src/scene_catalog.cpp src/scene_loading.cpp tests/scene_history_owner_tests.cpp tests/restart_command_line_tests.cpp src/noise_texture_asset.cpp tests/noise_texture_asset_tests.cpp src/world_space_representation_nvrhi.cpp src/image_based_lighting_environment_nvrhi.cpp src/image_based_lighting_radiance.cpp src/image_based_lighting_asset_path_win32.cpp tests/image_based_lighting_owner_tests.cpp src/windows_file_status_win32.cpp src/retained_scene_paths_win32.cpp src/color_lut_asset_path_win32.cpp tests/windows_path_text_tests.cpp src/ui_light_defaults.cpp tests/ui_light_defaults_tests.cpp src/uvsr.cpp src/uvsr_lifecycle.cpp src/uvsr_ui_renderer.cpp src/renderer_frame_donut.cpp src/uvsr_scene_lifecycle.cpp src/uvsr_render_frame.cpp src/uvsr_render_setup.cpp src/uvsr_path_tracing.cpp src/uvsr_ray_visibility.cpp src/uvsr_ui_panels.cpp src/uvsr_display_sync.cpp src/uvsr_ui_commands.cpp PROPERTIES COMPILE_OPTIONS
        "/EHs-c-;/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
    set_property(SOURCE src/color_lut.cpp APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
else()
    set_source_files_properties(src/camera_collision.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
    set_source_files_properties(src/engine_startup.cpp src/uvsr_command_line.cpp src/color_lut.cpp
    src/gpu_adapter_catalog.cpp src/scene_catalog.cpp src/scene_loading.cpp tests/scene_history_owner_tests.cpp tests/restart_command_line_tests.cpp src/noise_texture_asset.cpp tests/noise_texture_asset_tests.cpp src/world_space_representation_nvrhi.cpp src/image_based_lighting_environment_nvrhi.cpp src/image_based_lighting_radiance.cpp src/image_based_lighting_asset_path_win32.cpp tests/image_based_lighting_owner_tests.cpp src/windows_file_status_win32.cpp src/retained_scene_paths_win32.cpp src/color_lut_asset_path_win32.cpp tests/windows_path_text_tests.cpp src/ui_light_defaults.cpp tests/ui_light_defaults_tests.cpp src/uvsr.cpp src/uvsr_lifecycle.cpp src/uvsr_ui_renderer.cpp src/renderer_frame_donut.cpp src/uvsr_scene_lifecycle.cpp src/uvsr_render_frame.cpp src/uvsr_render_setup.cpp src/uvsr_path_tracing.cpp src/uvsr_ray_visibility.cpp src/uvsr_ui_panels.cpp src/uvsr_display_sync.cpp src/uvsr_ui_commands.cpp PROPERTIES COMPILE_OPTIONS
        "-fno-exceptions;-include;${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()

add_executable(uvsr-engine WIN32
        "${UVSR_BUILD_IDENTITY_SOURCE}"
        "${UVSR_BUILD_IDENTITY_RESOURCE}"
        src/auto_exposure_nvrhi.cpp
        src/agx_tone_mapping_pass_nvrhi.cpp
        src/color_lut.cpp
        src/color_lut_asset_path_win32.cpp
        src/ui_light_defaults.cpp
        src/retained_scene_paths_win32.cpp
        src/windows_file_status_win32.cpp
        src/camera_collision.cpp
        src/d3d12_agility_exports.cpp
        src/engine_diagnostics.cpp
        src/engine_startup.cpp
        src/fast_approximate_aa_nvrhi.cpp
        src/ray_visibility_pass_nvrhi.cpp
        src/image_based_lighting_background_pass_nvrhi.cpp
        src/image_based_lighting_environment_nvrhi.cpp
        src/image_based_lighting_radiance.cpp
        src/image_based_lighting_asset_path_win32.cpp
        src/lighting_accumulation_pass_nvrhi.cpp
        src/noise_texture_library_nvrhi.cpp
        src/noise_texture_asset.cpp
        src/path_tracing_pass_nvrhi.cpp
        src/pbr_deferred_lighting_pass_nvrhi.cpp
        src/renderer_nvrhi_message_callback.cpp
        src/renderer_scene_load_worker_win32.cpp
        src/renderer_scene_retirement.cpp
        src/renderer_scene_retirement_nvrhi.cpp
        src/renderer_targets_nvrhi.cpp
        src/gpu_adapter_catalog.cpp
        src/scene_catalog.cpp
        src/scene_loading.h
        src/scene_loading.cpp
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
        src/renderer_frame_donut.cpp
        src/uvsr_render_setup.cpp
        src/uvsr_renderer_state.cpp
        src/uvsr_scene_lifecycle.cpp
        src/uvsr_ui_commands.cpp
        src/uvsr_ui_panels.cpp
        src/uvsr_ui_renderer.cpp
        src/uvsr_display_sync.cpp
        src/uvsr.cpp
        src/world_space_representation_nvrhi.cpp
    )
    set_target_properties(uvsr-engine PROPERTIES
        OUTPUT_NAME "uvsr-engine")
    target_include_directories(uvsr-engine BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${UVSR_IMGUI_OVERRIDE_DIR}"
        "${UVSR_DONUT_APP_OVERRIDE_DIR}/include")
    target_link_libraries(uvsr-engine PRIVATE
        uvsr_ui_context
        uvsr_renderer_nvrhi
        uvsr_log
        uvsr_engine_log
        uvsr_camera
        uvsr_view
        uvsr_shader_blob
        donut_app
        imgui
        uvsr_sha256
        uvsr_windows_paths
        uvsr_windows_path_text
        uvsr_settings_snapshot
        dwmapi)
    target_compile_definitions(uvsr-engine PRIVATE
        "UVSR_D3D12_AGILITY_SDK_VERSION=${UVSR_D3D12_AGILITY_SDK_VERSION}"
        "UVSR_REQUIRED_SHADER_MODEL=${UVSR_SHADER_MODEL_ENCODED}")
if (BUILD_TESTING)
    target_compile_definitions(uvsr-engine PRIVATE UVSR_BUILD_TESTING=1)
    target_link_libraries(uvsr-engine PRIVATE uvsr_retained_runtime_output uvsr_file_write)
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

add_library(uvsr_renderer_scene_gpu_nvrhi STATIC src/renderer_scene_gpu_nvrhi.cpp)
target_include_directories(uvsr_renderer_scene_gpu_nvrhi PRIVATE src)
target_link_libraries(uvsr_renderer_scene_gpu_nvrhi PRIVATE uvsr_renderer_scene uvsr_renderer_nvrhi nvrhi)
target_compile_definitions(uvsr_renderer_scene_gpu_nvrhi PRIVATE _HAS_EXCEPTIONS=0)
if(MSVC)
    target_compile_options(uvsr_renderer_scene_gpu_nvrhi PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise)
else()
    target_compile_options(uvsr_renderer_scene_gpu_nvrhi PRIVATE -fno-exceptions)
endif()
target_link_libraries(uvsr-engine PRIVATE uvsr_renderer_scene_gpu_nvrhi uvsr_renderer_scene uvsr_file_bytes)

add_library(uvsr_renderer_import STATIC EXCLUDE_FROM_ALL
    src/renderer_import.cpp src/renderer_import_metadata.cpp src/renderer_import_scene.cpp
    src/renderer_import_path.cpp src/renderer_import_description.cpp src/renderer_import_composition.cpp
    src/renderer_import_image.cpp src/renderer_import_dds.cpp src/renderer_import_load.cpp)
target_include_directories(uvsr_renderer_import PUBLIC src)
target_link_libraries(uvsr_renderer_import PRIVATE fastgltf::fastgltf simdjson::simdjson uvsr_renderer_scene stb tinyexr uvsr_tinyexr)
target_compile_definitions(uvsr_renderer_import PRIVATE _HAS_EXCEPTIONS=0)
if(MSVC)
    target_compile_options(uvsr_renderer_import PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_renderer_import PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()

if(WIN32)
    add_library(uvsr_renderer_import_win32 STATIC EXCLUDE_FROM_ALL
        src/renderer_import_file_win32.cpp src/renderer_import_load_status_win32.cpp)
    target_link_libraries(uvsr_renderer_import_win32 PRIVATE uvsr_renderer_import)
    target_link_libraries(uvsr-engine PRIVATE uvsr_renderer_import_win32)
    target_include_directories(uvsr_renderer_import_win32 PUBLIC src)
    target_compile_features(uvsr_renderer_import_win32 PRIVATE cxx_std_17)
    target_compile_definitions(uvsr_renderer_import_win32 PRIVATE _HAS_EXCEPTIONS=0 NOMINMAX)
    if(MSVC)
        target_compile_options(uvsr_renderer_import_win32 PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
    else()
        target_compile_options(uvsr_renderer_import_win32 PRIVATE -fno-exceptions
            -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
    endif()
endif()

add_library(uvsr_renderer_scene_resources_nvrhi STATIC EXCLUDE_FROM_ALL src/renderer_scene_resources_nvrhi.cpp)
target_include_directories(uvsr_renderer_scene_resources_nvrhi PUBLIC src)
target_link_libraries(uvsr_renderer_scene_resources_nvrhi PRIVATE uvsr_renderer_import nvrhi)
target_link_libraries(uvsr_renderer_scene_gpu_nvrhi PRIVATE uvsr_renderer_scene_resources_nvrhi)
target_compile_features(uvsr_renderer_scene_resources_nvrhi PRIVATE cxx_std_17)
target_compile_definitions(uvsr_renderer_scene_resources_nvrhi PRIVATE _HAS_EXCEPTIONS=0)
if(MSVC)
    target_compile_options(uvsr_renderer_scene_resources_nvrhi PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_renderer_scene_resources_nvrhi PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()

if (BUILD_TESTING)

target_compile_definitions(uvsr_renderer_import PRIVATE UVSR_BUILD_TESTING=1)
target_compile_definitions(uvsr_renderer_scene_resources_nvrhi PRIVATE UVSR_BUILD_TESTING=1)
if(WIN32)
    uvsr_test(uvsr_import_load_tests uvsr_import_load tests/import_load_tests.cpp
        src/renderer_scene_load_worker_win32.cpp)
    target_link_libraries(uvsr_import_load_tests PRIVATE uvsr_renderer_import uvsr_renderer_import_win32 stb)
    target_compile_definitions(uvsr_import_load_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0 NOMINMAX)
    if(MSVC)
        target_compile_options(uvsr_import_load_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
    else()
        target_compile_options(uvsr_import_load_tests PRIVATE -fno-exceptions
            -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
    endif()
    set_tests_properties(uvsr_import_load PROPERTIES TIMEOUT 30)
endif()

uvsr_test(uvsr_import_image_tests uvsr_import_image tests/import_image_tests.cpp tests/import_image_fixture.cpp tests/stb_gif_tests.cpp)
target_link_libraries(uvsr_import_image_tests PRIVATE uvsr_renderer_import stb tinyexr)
target_compile_definitions(uvsr_import_image_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(uvsr_import_image_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_import_image_tests PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()
set_tests_properties(uvsr_import_image PROPERTIES TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")

uvsr_test(uvsr_import_composition_tests uvsr_import_composition
    tests/import_composition_tests.cpp tests/import_composition_reference_tests.cpp)
target_link_libraries(uvsr_import_composition_tests PRIVATE
    uvsr_renderer_import uvsr_renderer_import_win32 uvsr_renderer_scene stb uvsr_stb_image)
target_compile_definitions(uvsr_import_composition_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0 NOMINMAX _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(uvsr_import_composition_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_import_composition_tests PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()
set_tests_properties(uvsr_import_composition PROPERTIES TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")

uvsr_test(uvsr_import_path_tests uvsr_import_path tests/import_path_tests.cpp)
target_link_libraries(uvsr_import_path_tests PRIVATE uvsr_renderer_import)
target_compile_definitions(uvsr_import_path_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0)
if(MSVC)
    target_compile_options(uvsr_import_path_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_import_path_tests PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()
set_tests_properties(uvsr_import_path PROPERTIES TIMEOUT 30)

uvsr_test(uvsr_import_description_tests uvsr_import_description tests/import_description_tests.cpp)
target_link_libraries(uvsr_import_description_tests PRIVATE uvsr_renderer_import uvsr_renderer_scene)
target_compile_definitions(uvsr_import_description_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0)
if(MSVC)
    target_compile_options(uvsr_import_description_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_import_description_tests PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()
set_tests_properties(uvsr_import_description PROPERTIES TIMEOUT 30)

uvsr_test(uvsr_import_geometry_tests uvsr_import_geometry tests/import_geometry_tests.cpp tests/import_geometry_fixture.cpp)
target_link_libraries(uvsr_import_geometry_tests PRIVATE uvsr_renderer_import uvsr_renderer_scene)
target_compile_definitions(uvsr_import_geometry_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(uvsr_import_geometry_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_import_geometry_tests PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()
set_tests_properties(uvsr_import_geometry PROPERTIES TIMEOUT 30
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")

uvsr_test(uvsr_import_material_tests uvsr_import_material tests/import_material_tests.cpp tests/import_material_fixture.cpp)
target_link_libraries(uvsr_import_material_tests PRIVATE uvsr_renderer_import uvsr_renderer_scene stb uvsr_stb_image)
target_compile_definitions(uvsr_import_material_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(uvsr_import_material_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_import_material_tests PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()
set_tests_properties(uvsr_import_material PROPERTIES TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")

uvsr_test(uvsr_import_scene_tests uvsr_import_scene tests/import_scene_tests.cpp tests/import_scene_fixture.cpp)
target_link_libraries(uvsr_import_scene_tests PRIVATE uvsr_renderer_import uvsr_renderer_scene)
target_compile_definitions(uvsr_import_scene_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(uvsr_import_scene_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_import_scene_tests PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()
set_tests_properties(uvsr_import_scene PROPERTIES TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")

uvsr_test(uvsr_import_accessors_tests uvsr_import_accessors tests/import_accessors_tests.cpp)
target_link_libraries(uvsr_import_accessors_tests PRIVATE uvsr_renderer_import)
target_compile_definitions(uvsr_import_accessors_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0)
if(MSVC)
    target_compile_options(uvsr_import_accessors_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_import_accessors_tests PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()

uvsr_test(uvsr_import_parser_tests uvsr_import_parser tests/import_parser_tests.cpp)
target_link_libraries(uvsr_import_parser_tests PRIVATE fastgltf::fastgltf)
target_compile_definitions(uvsr_import_parser_tests PRIVATE _HAS_EXCEPTIONS=0 SIMDJSON_EXCEPTIONS=0)
if(MSVC)
    target_compile_options(uvsr_import_parser_tests PRIVATE /permissive- /W4 /WX /EHs-c-
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
else()
    target_compile_options(uvsr_import_parser_tests PRIVATE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RequireNoCppExceptions.h")
endif()

target_compile_definitions(uvsr_renderer_scene_gpu_nvrhi PRIVATE UVSR_BUILD_TESTING=1)

uvsr_test(uvsr_renderer_scene_encoding_tests uvsr_renderer_scene_encoding
    tests/renderer_scene_encoding_tests.cpp tests/renderer_scene_light_tests.cpp tests/renderer_scene_encoding_fixture.cpp)
target_link_libraries(uvsr_renderer_scene_encoding_tests PRIVATE uvsr_renderer_scene)
target_compile_definitions(uvsr_renderer_scene_encoding_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(uvsr_renderer_scene_encoding_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise)
else()
    target_compile_options(uvsr_renderer_scene_encoding_tests PRIVATE -fno-exceptions)
endif()
set_tests_properties(uvsr_renderer_scene_encoding PROPERTIES TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_compile_definitions(uvsr_renderer_scene PRIVATE UVSR_BUILD_TESTING=1)
uvsr_test(uvsr_renderer_scene_ray_tests uvsr_renderer_scene_ray tests/renderer_scene_ray_tests.cpp)
target_link_libraries(uvsr_renderer_scene_ray_tests PRIVATE uvsr_renderer_scene)
target_compile_definitions(uvsr_renderer_scene_ray_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0)
if (MSVC)
    target_compile_options(uvsr_renderer_scene_ray_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise)
else()
    target_compile_options(uvsr_renderer_scene_ray_tests PRIVATE -fno-exceptions)
endif()
uvsr_test(uvsr_renderer_scene_draw_tests uvsr_renderer_scene_draw tests/renderer_scene_draw_tests.cpp)
target_link_libraries(uvsr_renderer_scene_draw_tests PRIVATE uvsr_renderer_scene)
target_compile_definitions(uvsr_renderer_scene_draw_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0)
if (MSVC)
    target_compile_options(uvsr_renderer_scene_draw_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise)
else()
    target_compile_options(uvsr_renderer_scene_draw_tests PRIVATE -fno-exceptions)
endif()
uvsr_test(uvsr_renderer_scene_tests uvsr_renderer_scene
    tests/renderer_scene_tests.cpp tests/renderer_scene_native_math_tests.cpp tests/renderer_scene_material_mode_tests.cpp)
target_link_libraries(uvsr_renderer_scene_tests PRIVATE uvsr_renderer_scene)
target_compile_definitions(uvsr_renderer_scene_tests PRIVATE UVSR_BUILD_TESTING=1 _HAS_EXCEPTIONS=0)
if (MSVC)
    target_compile_options(uvsr_renderer_scene_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise)
else()
    target_compile_options(uvsr_renderer_scene_tests PRIVATE -fno-exceptions)
endif()
set_tests_properties(uvsr_renderer_scene PROPERTIES TIMEOUT 30)

add_executable(uvsr_scene_catalog_tests tests/scene_catalog_tests.cpp
    src/scene_catalog.cpp
    src/renderer_scene_load_worker_win32.cpp src/renderer_scene_retirement.cpp
    tests/renderer_scene_load_worker_tests.cpp tests/scene_catalog_owner_tests.cpp
    src/scene_loading.cpp tests/scene_history_owner_tests.cpp
    src/image_based_lighting_radiance.cpp src/image_based_lighting_asset_path_win32.cpp
    tests/image_based_lighting_owner_tests.cpp)
target_compile_definitions(uvsr_scene_catalog_tests PRIVATE UVSR_SCENE_CATALOG_TEST_HOOKS=1 UVSR_SCENE_HISTORY_TEST_HOOKS=1
    UVSR_IBL_RADIANCE_TEST_HOOKS=1 UVSR_WINDOWS_PATH_TEST_HOOKS=1 _CRT_SECURE_NO_WARNINGS)
target_include_directories(uvsr_scene_catalog_tests PRIVATE src)
target_link_libraries(uvsr_scene_catalog_tests PRIVATE uvsr_json uvsr_file_bytes uvsr_windows_paths_test uvsr_stb_image uvsr_windows_path_text_test)
add_test(NAME uvsr_scene_catalog_reference COMMAND uvsr_scene_catalog_tests
    "${CMAKE_CURRENT_BINARY_DIR}/scene-acceptance-test/$<CONFIG>")
set_tests_properties(uvsr_scene_catalog_reference PROPERTIES TIMEOUT 30)

add_executable(uvsr_renderer_gpu_contract_tests
    tests/renderer_gpu_contract_tests.cpp)
target_include_directories(uvsr_renderer_gpu_contract_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(uvsr_checked_worklist_tests tests/checked_worklist_tests.cpp)
target_include_directories(uvsr_checked_worklist_tests PRIVATE src)
add_test(NAME uvsr_checked_worklist COMMAND uvsr_checked_worklist_tests)
if (MSVC)
    target_compile_options(uvsr_checked_worklist_tests PRIVATE /permissive- /W4 /WX)
endif()

add_executable(uvsr_renderer_view_tests
    tests/renderer_view_tests.cpp)
target_include_directories(uvsr_renderer_view_tests PRIVATE src "${UVSR_NVRHI_SOURCE_DIR}/include")
target_link_libraries(uvsr_renderer_view_tests PRIVATE uvsr_view)
target_compile_definitions(uvsr_renderer_view_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
uvsr_disable_cpp_exceptions(uvsr_renderer_view_tests)
add_test(NAME uvsr_renderer_view_conventions COMMAND uvsr_renderer_view_tests)
set_tests_properties(uvsr_renderer_view_conventions PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")

add_executable(uvsr_renderer_resource_contract_tests
    tests/renderer_resource_contract_tests.cpp tests/receiver_contract_tests.cpp
    src/renderer_targets_nvrhi.cpp)
target_include_directories(uvsr_renderer_resource_contract_tests PRIVATE
    src "${UVSR_DONUT_SOURCE_DIR}/include")
target_link_libraries(uvsr_renderer_resource_contract_tests PRIVATE uvsr_bmp uvsr_log nvrhi)
target_compile_definitions(uvsr_renderer_resource_contract_tests PRIVATE
    UVSR_REQUIRED_SHADER_MODEL=${UVSR_SHADER_MODEL_ENCODED} NOMINMAX)
if (MSVC)
    target_compile_options(uvsr_renderer_resource_contract_tests PRIVATE /permissive- /W4 /WX)
endif()
add_test(NAME uvsr_renderer_resources COMMAND uvsr_renderer_resource_contract_tests
    "${CMAKE_CURRENT_BINARY_DIR}/renderer-resource-test/$<CONFIG>")

add_executable(uvsr_retained_runtime_diagnostic_tests
    tests/retained_runtime_diagnostic_tests.cpp
    src/engine_diagnostics.cpp "${UVSR_BUILD_IDENTITY_SOURCE}")
target_link_libraries(uvsr_retained_runtime_diagnostic_tests PRIVATE
    uvsr_log uvsr_windows_path_text uvsr_retained_runtime_output ole32 shell32)
target_compile_definitions(uvsr_retained_runtime_diagnostic_tests PRIVATE UVSR_RETAINED_CASE_TEST_HOOKS=1)
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
    tests/uvsr_command_line_tests.cpp tests/restart_command_line_tests.cpp
    src/uvsr_command_line.cpp)

target_link_libraries(uvsr_command_line_tests PRIVATE uvsr_sha256 uvsr_windows_paths_test)
target_compile_definitions(uvsr_command_line_tests PRIVATE
    UVSR_BUILD_TESTING=1 UVSR_WINDOWS_PATH_TEST_HOOKS=1 UVSR_RESTART_COMMAND_LINE_TEST_HOOKS=1)

uvsr_test(uvsr_command_line_production_tests uvsr_command_line_production_reference
    tests/uvsr_command_line_tests.cpp tests/restart_command_line_tests.cpp
    src/uvsr_command_line.cpp)

target_link_libraries(uvsr_command_line_production_tests PRIVATE uvsr_sha256 uvsr_windows_paths_test)
target_compile_definitions(uvsr_command_line_production_tests PRIVATE UVSR_WINDOWS_PATH_TEST_HOOKS=1 UVSR_RESTART_COMMAND_LINE_TEST_HOOKS=1)
if (MSVC)
    foreach(target uvsr_command_line_tests uvsr_command_line_production_tests)
        target_compile_options(${target} PRIVATE /permissive- /W4 /WX)
    endforeach()
endif()

uvsr_test(uvsr_settings_commands_tests uvsr_settings_commands_reference
    tests/uvsr_settings_commands_tests.cpp)

# executable-policy fixtures own their catalog beside this test binary.
set_target_properties(uvsr_settings_commands_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-bin/settings-commands")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(uvsr_settings_commands_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_CURRENT_BINARY_DIR}/test-bin/settings-commands/${config}")
endforeach()
target_link_libraries(uvsr_settings_commands_tests PRIVATE uvsr_windows_paths uvsr_log ole32 shell32)

add_executable(uvsr_nvrhi_d3d12_diagnostics_tests
    tests/nvrhi_d3d12_diagnostics_tests.cpp
    src/engine_startup.cpp src/renderer_nvrhi_message_callback.cpp)
target_include_directories(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE src
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/include"
    "${UVSR_DIRECTX_HEADERS_SOURCE_DIR}/include/directx")
target_link_libraries(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE
    uvsr_engine_log uvsr_sha256 uvsr_windows_paths nvrhi Microsoft::DirectX-Headers d3d12 ole32 shell32)
target_compile_definitions(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE NOMINMAX)
if (BUILD_TESTING)
    target_compile_definitions(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE UVSR_ENGINE_LOG_TEST_HOOKS=1)
endif()
if (MSVC)
    target_compile_options(uvsr_nvrhi_d3d12_diagnostics_tests PRIVATE
        /permissive- /W4 /WX)
endif()
add_test(
    NAME uvsr_nvrhi_d3d12_diagnostics
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_TEST_EXECUTABLE=$<TARGET_FILE:uvsr_nvrhi_d3d12_diagnostics_tests>"
        "-DUVSR_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/backend-diagnostics/$<CONFIG>"
        "-DUVSR_D3D12_CORE=${UVSR_D3D12_AGILITY_CORE_DLL}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestD3D12Diagnostics.cmake")

add_executable(uvsr_display_controls_tests tests/display_controls_tests.cpp src/color_lut.cpp
    tests/windows_path_text_tests.cpp src/color_lut_asset_path_win32.cpp
    tests/ui_light_defaults_tests.cpp src/ui_light_defaults.cpp)
target_include_directories(
    uvsr_display_controls_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/include"
)
target_compile_features(uvsr_display_controls_tests PRIVATE cxx_std_17)
target_link_libraries(uvsr_display_controls_tests PRIVATE uvsr_json uvsr_file_bytes uvsr_windows_path_text_test uvsr_windows_paths_test)
target_compile_definitions(uvsr_display_controls_tests PRIVATE UVSR_COLOR_LUT_TEST_HOOKS=1 UVSR_WINDOWS_PATH_TEST_HOOKS=1 UVSR_UI_LIGHT_DEFAULTS_TEST_HOOKS=1)
if (MSVC)
    target_compile_options(uvsr_display_controls_tests PRIVATE
        /permissive- /W4 /WX)
endif()

add_test(NAME uvsr_display_controls COMMAND uvsr_display_controls_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/luts/kodak")

uvsr_test(uvsr_camera_controls_tests uvsr_camera_controls_reference
    tests/camera_controls_tests.cpp src/camera_collision.cpp)

add_executable(uvsr_camera_collision_tests
    tests/camera_collision_tests.cpp src/camera_collision.cpp)
target_include_directories(uvsr_camera_collision_tests PRIVATE src)
target_compile_definitions(uvsr_camera_collision_tests PRIVATE _HAS_EXCEPTIONS=0 UVSR_BUILD_TESTING=1)
add_test(NAME uvsr_camera_collision_structure COMMAND uvsr_camera_collision_tests)

# this consumer links only the portable scene owner and collision code.
# native include paths or renderer libraries would defeat the boundary check.
add_executable(uvsr_renderer_scene_portable_tests
    tests/renderer_scene_portable_tests.cpp src/camera_collision.cpp)
target_link_libraries(uvsr_renderer_scene_portable_tests PRIVATE uvsr_renderer_scene)
target_compile_definitions(uvsr_renderer_scene_portable_tests PRIVATE _HAS_EXCEPTIONS=0)
add_test(NAME uvsr_renderer_scene_portable COMMAND uvsr_renderer_scene_portable_tests)
if (MSVC)
    target_compile_options(uvsr_camera_collision_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise)
    target_compile_options(uvsr_renderer_scene_portable_tests PRIVATE /permissive- /W4 /WX /EHs-c- /fp:precise /showIncludes)
else()
    target_compile_options(uvsr_camera_collision_tests PRIVATE -fno-exceptions)
    target_compile_options(uvsr_renderer_scene_portable_tests PRIVATE -fno-exceptions)
endif()

target_link_libraries(uvsr_camera_controls_tests PRIVATE uvsr_camera glfw)
uvsr_disable_cpp_exceptions(uvsr_camera_controls_tests)
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
)

uvsr_test(uvsr_settings_snapshot_transaction_tests uvsr_settings_snapshot_transaction_reference
    tests/settings_snapshot_transaction_tests.cpp)

uvsr_test(uvsr_settings_snapshot_values_tests uvsr_settings_snapshot_values_reference
    tests/settings_snapshot_values_tests.cpp)

add_executable(uvsr_settings_snapshot_decoder
    tools/settings_snapshot_decoder.cpp
)
target_include_directories(
    uvsr_settings_snapshot_decoder
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_settings_snapshot_decoder PRIVATE uvsr_settings_snapshot)
uvsr_disable_cpp_exceptions(uvsr_settings_snapshot_decoder)
uvsr_disable_cpp_exceptions(uvsr_settings_snapshot_decoder_tests)
target_compile_definitions(uvsr_settings_snapshot_decoder_tests PRIVATE UVSR_WINDOWS_PATH_TEST_HOOKS=1)
foreach(target uvsr_retained_runtime_diagnostic_tests uvsr_command_line_tests
        uvsr_command_line_production_tests uvsr_settings_commands_tests
        uvsr_settings_snapshot_decoder_tests uvsr_settings_snapshot_transaction_tests
        uvsr_settings_snapshot_values_tests)
    target_link_libraries(${target} PRIVATE uvsr_settings_snapshot_test)
endforeach()

if (BUILD_TESTING)
    uvsr_test(uvsr_settings_snapshot_selectors_tests uvsr_settings_snapshot_selectors
        tests/settings_snapshot_selectors_tests.cpp)
    target_link_libraries(uvsr_settings_snapshot_selectors_tests PRIVATE uvsr_settings_snapshot_test)
    uvsr_disable_cpp_exceptions(uvsr_settings_snapshot_selectors_tests)
    if (MSVC)
        target_compile_options(uvsr_settings_snapshot_selectors_tests PRIVATE /permissive- /W4 /WX)
    endif()
    uvsr_test(uvsr_settings_snapshot_legacy_tests uvsr_settings_snapshot_legacy
        tests/settings_snapshot_legacy_tests.cpp)
    target_link_libraries(uvsr_settings_snapshot_legacy_tests PRIVATE uvsr_settings_snapshot_test)
    uvsr_disable_cpp_exceptions(uvsr_settings_snapshot_legacy_tests)
    if (MSVC)
        target_compile_options(uvsr_settings_snapshot_legacy_tests PRIVATE /permissive- /W4 /WX)
    endif()
    uvsr_test(uvsr_settings_snapshot_persistence_tests uvsr_settings_snapshot_persistence
        tests/settings_snapshot_persistence_tests.cpp)
    target_link_libraries(uvsr_settings_snapshot_persistence_tests PRIVATE uvsr_settings_snapshot_test uvsr_file_write_test)
    target_compile_definitions(uvsr_settings_snapshot_persistence_tests PRIVATE UVSR_WINDOWS_PATH_TEST_HOOKS=1)
    uvsr_disable_cpp_exceptions(uvsr_settings_snapshot_persistence_tests)
    if (MSVC)
        target_compile_options(uvsr_settings_snapshot_persistence_tests PRIVATE /permissive- /W4 /WX)
    endif()
    uvsr_test(uvsr_file_write_tests uvsr_file_write tests/file_write_tests.cpp)
    target_link_libraries(uvsr_file_write_tests PRIVATE uvsr_file_write_test uvsr_file_bytes)
    target_compile_definitions(uvsr_file_write_tests PRIVATE UVSR_WINDOWS_PATH_TEST_HOOKS=1)
    uvsr_disable_cpp_exceptions(uvsr_file_write_tests)
    if (MSVC)
        target_compile_options(uvsr_file_write_tests PRIVATE /permissive- /W4 /WX)
    endif()
    uvsr_test(uvsr_settings_snapshot_storage_tests uvsr_settings_snapshot_storage
        tests/settings_snapshot_storage_tests.cpp)
    target_link_libraries(uvsr_settings_snapshot_storage_tests PRIVATE uvsr_settings_snapshot_test)
    uvsr_disable_cpp_exceptions(uvsr_settings_snapshot_storage_tests)
    if (MSVC)
        target_compile_options(uvsr_settings_snapshot_storage_tests PRIVATE /permissive- /W4 /WX)
    endif()
endif()

add_executable(uvsr_legal_inventory_validator
    tools/legal_inventory_validator.cpp)
target_link_libraries(uvsr_legal_inventory_validator PRIVATE uvsr_sha256)

add_executable(uvsr_launcher_feed_contract_validator
    tools/launcher_feed_contract_validator.cpp)
target_include_directories(uvsr_launcher_feed_contract_validator PRIVATE src)
target_link_libraries(uvsr_launcher_feed_contract_validator PRIVATE uvsr_json uvsr_file_bytes)
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

# this inventory owns staged and packaged shader membership. native test-oracle
# shaders are compiled separately and never enter the runtime tree.
set(UVSR_RUNTIME_SHADER_INVENTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-shader-inventory.def")
uvsr_read_runtime_shader_inventory(
    "${UVSR_RUNTIME_SHADER_INVENTORY}"
    UVSR_RUNTIME_SHADER_PACKAGE_PATHS)
set(UVSR_APP_SHADERS)
set(UVSR_RUNTIME_SHADER_BYPRODUCTS)
foreach(package_path IN LISTS UVSR_RUNTIME_SHADER_PACKAGE_PATHS)
    if (package_path MATCHES
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
add_dependencies(uvsr_stage_runtime_shaders uvsr_shaders)
add_dependencies(uvsr-engine uvsr_stage_runtime_shaders)

foreach(target uvsr-engine uvsr_build_identity_generator
        uvsr_renderer_package_contract_validator)
    target_link_libraries(${target} PRIVATE uvsr_settings_catalog)
endforeach()

if (BUILD_TESTING)
    add_executable(uvsr_shader_bundle_tests
        tests/shader_bundle_tests.cpp tests/shader_blob_fixture.cpp)
    target_include_directories(uvsr_shader_bundle_tests PRIVATE src)
    target_link_libraries(uvsr_shader_bundle_tests PRIVATE uvsr_renderer_nvrhi uvsr_shader_blob nvrhi)
    uvsr_disable_cpp_exceptions(uvsr_shader_bundle_tests)
    add_dependencies(uvsr_shader_bundle_tests uvsr_shader_blob_builder
        uvsr_stage_runtime_shaders uvsr_renderer_gpu_contract_tests)
    if (MSVC)
        target_compile_options(uvsr_shader_bundle_tests PRIVATE /permissive- /W4 /WX)
    endif()
    add_test(NAME uvsr_shader_bundle_contract COMMAND uvsr_shader_bundle_tests
        "${UVSR_RUNTIME_SHADER_INVENTORY}"
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

    set(UVSR_SCENE_INSTANCE_PROBE "${CMAKE_CURRENT_BINARY_DIR}/test-scene-instance-probe.dxil")
    add_custom_command(OUTPUT "${UVSR_SCENE_INSTANCE_PROBE}"
        COMMAND "${UVSR_DXC_EXECUTABLE}" -T cs_6_6 -E main -WX -Zpr
            -I "${CMAKE_CURRENT_SOURCE_DIR}/src" -Fo "${UVSR_SCENE_INSTANCE_PROBE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/renderer_scene_instance_probe.hlsl"
        DEPENDS tests/renderer_scene_instance_probe.hlsl src/renderer_gpu_contract.h
            src/renderer_gpu_scalar.h src/renderer_view_contract.h
        VERBATIM)
    add_custom_target(uvsr_scene_instance_probe DEPENDS "${UVSR_SCENE_INSTANCE_PROBE}")
    # the direct test object supplies the hook; the engine keeps the plain archive member.
    add_executable(uvsr_renderer_common_passes_tests
        src/renderer_common_passes_nvrhi.cpp
        tests/renderer_common_passes_tests.cpp
        tests/renderer_gpu_fixture.cpp
        tests/renderer_ui_tests.cpp
        tests/renderer_binding_tests.cpp
        tests/nvrhi_descriptor_failure_tests.cpp
        tests/renderer_scene_retirement_tests.cpp
        tests/renderer_targets_tests.cpp
        tests/import_skin_gpu_tests.cpp
        tests/import_upload_gpu_tests.cpp
        tests/renderer_scene_gpu_tests.cpp
        src/renderer_scene_load_worker_win32.cpp
        src/renderer_targets_nvrhi.cpp
        src/renderer_scene_retirement.cpp
        src/renderer_scene_retirement_nvrhi.cpp
        src/world_space_representation_nvrhi.cpp)
    target_include_directories(uvsr_renderer_common_passes_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_include_directories(uvsr_renderer_common_passes_tests SYSTEM PRIVATE
        "$<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>")
    set_source_files_properties(tests/nvrhi_descriptor_failure_tests.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${UVSR_NVRHI_D3D12_OVERRIDE_DIR}/src/d3d12;${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi/src/d3d12")
    target_compile_definitions(uvsr_renderer_common_passes_tests PRIVATE
        UVSR_BLIT_PIPELINE_TEST_HOOKS=1 UVSR_RENDER_TARGETS_TEST_FAILURES=1 _HAS_EXCEPTIONS=0 NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(uvsr_renderer_common_passes_tests PRIVATE
        uvsr_ui_context
        uvsr_log
        uvsr_renderer_nvrhi
        uvsr_view
        uvsr_renderer_scene_gpu_nvrhi
        uvsr_renderer_import
        uvsr_renderer_scene_resources_nvrhi
        stb
        imgui
        uvsr_shader_blob
        nvrhi
        nvrhi_d3d12
        Microsoft::DirectX-Headers
        DirectX-Guids
        d3d12
        dxgi)
    target_compile_features(
        uvsr_renderer_common_passes_tests PRIVATE cxx_std_17)
    uvsr_disable_cpp_exceptions(uvsr_renderer_common_passes_tests PRIVATE)
    set_source_files_properties(tests/renderer_scene_gpu_tests.cpp tests/import_upload_gpu_tests.cpp tests/import_skin_gpu_tests.cpp tests/renderer_ui_tests.cpp PROPERTIES
        COMPILE_DEFINITIONS "UVSR_BUILD_TESTING=1")
    if (MSVC)
        target_compile_options(uvsr_renderer_common_passes_tests PRIVATE
            /permissive- /W4 /WX /EHs-c- /fp:precise)
    else()
        target_compile_options(uvsr_renderer_common_passes_tests PRIVATE -fno-exceptions)
    endif()
    add_dependencies(uvsr_renderer_common_passes_tests
        uvsr_scene_instance_probe
        uvsr_stage_runtime_shaders
        uvsr_stage_d3d12_runtime)
    add_test(
        NAME uvsr_renderer_common_passes_reference
        COMMAND uvsr_renderer_common_passes_tests
            "${UVSR_RUNTIME_APP_SHADER_DIR}" "${UVSR_SCENE_INSTANCE_PROBE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests")
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
    PRIVATE cgltf stb uvsr_sha256 uvsr_json uvsr_file_bytes uvsr_scene_catalog_path)
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

foreach(image_consumer IN ITEMS uvsr-engine uvsr_import_load_tests uvsr_import_image_tests
        uvsr_renderer_common_passes_tests)
    if(TARGET ${image_consumer})
        target_link_libraries(${image_consumer} PRIVATE uvsr_stb_image)
    endif()
endforeach()

if(BUILD_TESTING)
    add_executable(uvsr_shader_factory_tests tests/shader_factory_tests.cpp)
    target_link_libraries(uvsr_shader_factory_tests PRIVATE uvsr_renderer_nvrhi nvrhi)
    target_compile_definitions(uvsr_shader_factory_tests PRIVATE UVSR_BUILD_TESTING=1 NOMINMAX)
    uvsr_disable_cpp_exceptions(uvsr_shader_factory_tests)
    set(UVSR_SHADER_FACTORY_TEST_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shader-factory-test")
    file(MAKE_DIRECTORY "${UVSR_SHADER_FACTORY_TEST_DIRECTORY}")
    add_test(NAME uvsr_shader_factory COMMAND uvsr_shader_factory_tests "${UVSR_SHADER_FACTORY_TEST_DIRECTORY}")
    uvsr_test(uvsr_imgui_window_tests uvsr_imgui_window tests/imgui_window_tests.cpp)
    target_link_libraries(uvsr_imgui_window_tests PRIVATE imgui)
    uvsr_disable_cpp_exceptions(uvsr_imgui_window_tests)
endif()
