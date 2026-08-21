cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UVSR_BUILD_APPLICATION
    "Build the UVSR application that integrates every enabled producer" ON)
option(UVSR_WITH_NRD
    "Build the optional NVIDIA NRD denoiser backend" OFF)
option(UVSR_ACCEPT_NRD_LICENSE
    "Acknowledge acceptance of NVIDIA's RTX SDK license for NRD" OFF)

set(UVSR_SHADER_MODEL "6_5")
if (NOT UVSR_SHADER_MODEL MATCHES "^([0-9]+)_([0-9]+)$")
    message(FATAL_ERROR "UVSR_SHADER_MODEL must use ShaderMake's major_minor form")
endif()
math(EXPR UVSR_SHADER_MODEL_ENCODED
    "${CMAKE_MATCH_1} * 16 + ${CMAKE_MATCH_2}"
    OUTPUT_FORMAT HEXADECIMAL)

if (UVSR_WITH_NRD AND NOT UVSR_ACCEPT_NRD_LICENSE)
    message(FATAL_ERROR
        "UVSR_WITH_NRD requires UVSR_ACCEPT_NRD_LICENSE=ON after reviewing "
        "https://github.com/NVIDIA-RTX/NRD/blob/792eff196afdd350fd9c3f862119017ccb438a0e/LICENSE.txt")
endif()

# Embed the exact source commit in the renderer title so the executable records
# the code that was built, not whichever checkout happens to exist at launch.
# Watching both the worktree HEAD and its resolved branch ref makes a normal
# `cmake --build` reconfigure automatically after commits and branch changes.
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=7 HEAD
    RESULT_VARIABLE UVSR_GIT_COMMIT_RESULT
    OUTPUT_VARIABLE UVSR_GIT_COMMIT
    ERROR_VARIABLE UVSR_GIT_COMMIT_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(LENGTH "${UVSR_GIT_COMMIT}" UVSR_GIT_COMMIT_LENGTH)
if (NOT UVSR_GIT_COMMIT_RESULT EQUAL 0 OR
    NOT UVSR_GIT_COMMIT MATCHES "^[0-9a-f]+$" OR
    NOT UVSR_GIT_COMMIT_LENGTH EQUAL 7)
    message(FATAL_ERROR
        "Cannot determine UVSR's seven-character Git commit: ${UVSR_GIT_COMMIT_ERROR}")
endif()

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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endforeach()
set(UVSR_DONUT_SHADER_STAGING_DIR "${CMAKE_BINARY_DIR}/donut_shaders")
set(DONUT_SHADERS_OUTPUT_DIR "${UVSR_DONUT_SHADER_STAGING_DIR}")

set(DONUT_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_DX12 ON CACHE BOOL "" FORCE)
set(DONUT_WITH_VULKAN OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)

# UVSR intentionally keeps Donut and its nested ImGui checkout pinned and
# pristine. Stage the small product-specific dependency changes into the build
# tree, apply reviewed first-party patches there, and replace only the affected
# target sources. This keeps dependency identity auditable without sacrificing
# the custom loading telemetry and UI behavior.
function(uvsr_stage_patched_sources
    source_root
    patch_files
    output_root)
    file(REMOVE_RECURSE "${output_root}")
    foreach(relative_path IN LISTS ARGN)
        get_filename_component(relative_directory
            "${relative_path}" DIRECTORY)
        file(MAKE_DIRECTORY
            "${output_root}/${relative_directory}")
        configure_file(
            "${source_root}/${relative_path}"
            "${output_root}/${relative_path}"
            COPYONLY)
    endforeach()

    foreach(patch_file IN LISTS patch_files)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env
                "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
                "${GIT_EXECUTABLE}" apply
                --no-index
                --whitespace=nowarn
                "${patch_file}"
            WORKING_DIRECTORY "${output_root}"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-loading.patch")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_ENGINE_PATCH_FILES}"
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}"
    include/donut/engine/Scene.h
    include/donut/engine/SceneTypes.h
    include/donut/engine/ThreadPool.h
    src/engine/GltfImporter.cpp
    src/engine/Scene.cpp
    src/engine/TextureCache.cpp
    src/engine/ThreadPool.cpp)

set(UVSR_DONUT_APP_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_donut_app_overrides")
set(UVSR_DONUT_APP_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-app.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-loading-app.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-app-ui-polish.patch")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_APP_PATCH_FILES}"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    include/donut/app/ApplicationBase.h
    include/donut/app/DeviceManager.h
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
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/thirdparty/imgui"
    "${UVSR_IMGUI_PATCH_FILES}"
    "${UVSR_IMGUI_OVERRIDE_DIR}"
    imgui.cpp
    imgui.h
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_internal.h
    imgui_tables.cpp
    imgui_widgets.cpp)

set(UVSR_NVRHI_D3D12_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_nvrhi_d3d12_overrides")
set(UVSR_NVRHI_D3D12_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/nvrhi-stable-directx-headers.patch")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/nvrhi"
    "${UVSR_NVRHI_D3D12_PATCH_FILES}"
    "${UVSR_NVRHI_D3D12_OVERRIDE_DIR}"
    src/d3d12/d3d12-constants.cpp)

# Keep NVRHI's headers on the same stable DirectX release line as UVSR's
# app-local Agility runtime. The pinned dependency remains pristine; this
# first-party cache override is also honored by offline launcher builds.
set(NVRHI_DIRECTX_HEADERS_GIT_TAG "v1.619.5" CACHE STRING "Pinned DirectX-Headers release used by UVSR" FORCE)
add_subdirectory(donut)
uvsr_replace_target_sources(
    nvrhi_d3d12
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/nvrhi"
    "${UVSR_NVRHI_D3D12_OVERRIDE_DIR}"
    src/d3d12/d3d12-constants.cpp)
target_include_directories(nvrhi_d3d12 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/nvrhi/src/d3d12")
uvsr_replace_target_sources(
    donut_app
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    src/app/ApplicationBase.cpp
    src/app/DeviceManager.cpp
    src/app/dx12/DeviceManager_DX12.cpp
    src/app/UserInterfaceUtils.cpp)
target_include_directories(donut_app BEFORE PUBLIC
    "${UVSR_DONUT_APP_OVERRIDE_DIR}/include")
uvsr_replace_target_sources(
    donut_engine
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}"
    include/donut/engine/Scene.h
    include/donut/engine/SceneTypes.h
    include/donut/engine/ThreadPool.h
    src/engine/GltfImporter.cpp
    src/engine/Scene.cpp
    src/engine/TextureCache.cpp
    src/engine/ThreadPool.cpp)
target_include_directories(donut_engine BEFORE PUBLIC
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}/include")

uvsr_replace_target_sources(
    imgui
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/thirdparty/imgui"
    "${UVSR_IMGUI_OVERRIDE_DIR}"
    imgui.cpp
    imgui.h
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_internal.h
    imgui_tables.cpp
    imgui_widgets.cpp)
target_include_directories(imgui BEFORE PUBLIC
    "${UVSR_IMGUI_OVERRIDE_DIR}")

if (UVSR_WITH_NRD)
    set(NRD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
    set(NRD_NRI OFF CACHE BOOL "" FORCE)
    set(NRD_EMBEDS_DXIL_SHADERS ON CACHE BOOL "" FORCE)
    set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)
    set(NRD_EMBEDS_SPIRV_SHADERS OFF CACHE BOOL "" FORCE)
    set(NRD_SUPPORTS_VIEWPORT_OFFSET OFF CACHE BOOL "" FORCE)
    set(NRD_SUPPORTS_CHECKERBOARD OFF CACHE BOOL "" FORCE)
    set(NRD_SUPPORTS_HISTORY_CONFIDENCE OFF CACHE BOOL "" FORCE)
    set(NRD_SUPPORTS_DISOCCLUSION_THRESHOLD_MIX OFF CACHE BOOL "" FORCE)
    set(NRD_SUPPORTS_ANTIFIREFLY ON CACHE BOOL "" FORCE)
    set(REBLUR_PERFORMANCE_MODE OFF CACHE BOOL "" FORCE)
    set(NRD_NORMAL_ENCODING "4" CACHE STRING "" FORCE)
    set(NRD_ROUGHNESS_ENCODING "1" CACHE STRING "" FORCE)

    FetchContent_Declare(nrd
        URL
            "https://github.com/NVIDIA-RTX/NRD/archive/792eff196afdd350fd9c3f862119017ccb438a0e.zip"
        URL_HASH
            "SHA256=AD148D3653E7E4A149AF0D1608EC662EEB522144CF34F6A29F9DFD333933BAA8"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nrd)
endif()

add_library(uvsr_denoiser STATIC
    src/denoiser_backend.cpp)
target_include_directories(uvsr_denoiser
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_denoiser PUBLIC nvrhi)
target_compile_definitions(uvsr_denoiser
    PUBLIC UVSR_WITH_NRD=$<BOOL:${UVSR_WITH_NRD}>)
if (UVSR_WITH_NRD)
    target_sources(uvsr_denoiser PRIVATE
        src/nrd_denoiser_backend.cpp
        src/nrd_denoiser_backend.h)
    target_link_libraries(uvsr_denoiser PRIVATE NRD)
endif()

if (NVRHI_WITH_DX12)
    set(UVSR_D3D12_AGILITY_PACKAGE_VERSION "1.619.5")
    set(UVSR_D3D12_AGILITY_SDK_VERSION 619)
    set(DONUT_D3D_AGILITY_SDK_URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.619.5")
    set(DONUT_D3D_AGILITY_SDK_FETCH_DIR "" CACHE PATH
        "Optional existing or shared D3D12 Agility SDK extraction")
    include("${CMAKE_CURRENT_SOURCE_DIR}/donut/cmake/FetchAgilitySDK.cmake")
    if (NOT DONUT_D3D_AGILITY_SDK_ENABLED OR
        NOT DONUT_D3D_AGILITY_SDK_VERSION EQUAL UVSR_D3D12_AGILITY_SDK_VERSION)
        message(FATAL_ERROR
            "UVSR requires the complete Direct3D Agility SDK "
            "${UVSR_D3D12_AGILITY_PACKAGE_VERSION} runtime contract "
            "(D3D12SDKVersion ${UVSR_D3D12_AGILITY_SDK_VERSION})")
    endif()
    file(COPY
        "${DONUT_D3D_AGILITY_SDK_CORE_DLL}"
        "${DONUT_D3D_AGILITY_SDK_LAYERS_DLL}"
        DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/D3D12/"
    )
endif()

set(UVSR_SHADER_STAGING_ROOT "${CMAKE_BINARY_DIR}/uvsr_shaders")
set(UVSR_CORE_SHADER_STAGING_DIR
    "${UVSR_SHADER_STAGING_ROOT}/core")
include(donut/compileshaders.cmake)
set(UVSR_SHADER_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.cfg")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${UVSR_SHADER_CONFIG}")

file(GLOB_RECURSE UVSR_SHADER_INPUTS
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/shaders/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/include/donut/shaders/*"
)
list(SORT UVSR_SHADER_INPUTS)

set(UVSR_SHADER_CONFIGURATION_STAMP
    "${CMAKE_CURRENT_BINARY_DIR}/uvsr_shader_configuration.txt")
file(GENERATE
    OUTPUT "${UVSR_SHADER_CONFIGURATION_STAMP}"
    CONTENT "${UVSR_SHADER_CONFIG}\nshaderModel=${UVSR_SHADER_MODEL}\n"
)

# Donut's Visual Studio shader helper otherwise tracks only ShaderMake and does
# not clear old outputs. Use one stamped producer per bundle so header edits,
# config switches, and removed entries cannot leave stale DXIL in staging.
function(uvsr_add_shader_bundle
    target_name
    project_name
    shader_config
    staging_directory
    output_stamp_variable)
    set(shader_compile_stamp
        "${staging_directory}/dxil/.${target_name}.stamp")
    add_custom_command(
        OUTPUT "${shader_compile_stamp}"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${staging_directory}/dxil"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${staging_directory}/dxil"
        COMMAND ${SHADERMAKE_PATH}
            --config "${shader_config}"
            --out "${staging_directory}/dxil"
            --platform DXIL
            --binaryBlob
            --outputExt .bin
            -I "${CMAKE_CURRENT_SOURCE_DIR}/donut/include"
            -I "${CMAKE_CURRENT_SOURCE_DIR}"
            -D TARGET_D3D12
            --compiler "${SHADERMAKE_DXC_PATH}"
            --shaderModel "${UVSR_SHADER_MODEL}"
            --project "${project_name}"
            --compactProgress
        COMMAND ${CMAKE_COMMAND} -E touch
            "${shader_compile_stamp}"
        DEPENDS
            ${SHADERMAKE_PATH}
            "${SHADERMAKE_DXC_PATH}"
            "${shader_config}"
            "${UVSR_SHADER_CONFIGURATION_STAMP}"
            ${UVSR_SHADER_INPUTS}
        COMMENT "Compiling ${project_name} shader bundle from ${shader_config}"
        VERBATIM)
    add_custom_target("${target_name}"
        DEPENDS "${shader_compile_stamp}")
    if (SHADERMAKE_TOOL)
        add_dependencies("${target_name}" ShaderMake)
    endif()
    set("${output_stamp_variable}" "${shader_compile_stamp}" PARENT_SCOPE)
endfunction()

uvsr_add_shader_bundle(
    uvsr_shaders
    "UVSR"
    "${UVSR_SHADER_CONFIG}"
    "${UVSR_CORE_SHADER_STAGING_DIR}"
    UVSR_SHADER_COMPILE_STAMP)

if (UVSR_BUILD_APPLICATION)
    add_executable(uvsr WIN32
        src/auto_exposure.cpp
        src/camera_collision.cpp
        src/d3d12_agility_exports.cpp
        src/denoising_pass.cpp
        src/fast_approximate_aa.cpp
        src/heitz_ratio_estimator_shadows.cpp
        src/image_based_lighting_background_pass.cpp
        src/image_based_lighting_environment.cpp
        src/lighting_accumulation_pass.cpp
        src/msaa_visibility_resolve.cpp
        src/noise_texture_library.cpp
        src/path_tracing_pass.cpp
        src/path_tracing_stable_plane_resolve_pass.cpp
        src/pbr_deferred_lighting_pass.cpp
        src/ray_traced_flashlight_shadows.cpp
        src/ray_traced_sky_visibility.cpp
        src/scene_catalog.cpp
        src/scene_loading.h
        src/screen_space_visibility.cpp
        src/sponza_camera_preset.cpp
        src/temporal_aa.cpp
        src/temporal_aa_core.cpp
        src/uvsr.cpp
        src/world_space_representation.cpp
    )
    target_include_directories(uvsr BEFORE PRIVATE
        "${UVSR_IMGUI_OVERRIDE_DIR}"
        "${UVSR_DONUT_APP_OVERRIDE_DIR}/include"
        "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}/include")
    target_link_libraries(uvsr PRIVATE
        uvsr_denoiser
        donut_render
        donut_app
        donut_engine
        dwmapi)
    target_compile_definitions(uvsr PRIVATE
        "UVSR_GIT_COMMIT=\"${UVSR_GIT_COMMIT}\""
        "UVSR_D3D12_AGILITY_SDK_VERSION=${UVSR_D3D12_AGILITY_SDK_VERSION}"
        "UVSR_REQUIRED_SHADER_MODEL=${UVSR_SHADER_MODEL_ENCODED}")
    add_custom_command(TARGET uvsr POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DUVSR_EXECUTABLE=$<TARGET_FILE:uvsr>"
            "-DUVSR_D3D12_CORE=${DONUT_D3D_AGILITY_SDK_CORE_DLL}"
            "-DUVSR_D3D12_RUNTIME_DIRECTORY=$<TARGET_FILE_DIR:uvsr>/D3D12"
            "-DUVSR_EXPECTED_SDK_VERSION=${UVSR_D3D12_AGILITY_SDK_VERSION}"
            "-DUVSR_LINKER=${CMAKE_LINKER}"
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
        "${UVSR_ENVIRONMENT_SOURCE_DIR}/*")
    list(SORT UVSR_ENVIRONMENT_ASSETS)
    string(JOIN "\n" UVSR_ENVIRONMENT_ASSET_MANIFEST_CONTENT
        ${UVSR_ENVIRONMENT_ASSETS})
    set(UVSR_ENVIRONMENT_ASSET_MANIFEST
        "${CMAKE_BINARY_DIR}/uvsr_environment_assets.manifest")
    file(GENERATE
        OUTPUT "${UVSR_ENVIRONMENT_ASSET_MANIFEST}"
        CONTENT "${UVSR_ENVIRONMENT_ASSET_MANIFEST_CONTENT}\n")
    set(UVSR_ENVIRONMENT_STAGE_STAMP
        "${UVSR_ENVIRONMENT_STAGE_DIR}/.uvsr-stage.stamp")
    add_custom_command(
        OUTPUT "${UVSR_ENVIRONMENT_STAGE_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${UVSR_ENVIRONMENT_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${UVSR_ENVIRONMENT_SOURCE_DIR}"
            "${UVSR_ENVIRONMENT_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch
            "${UVSR_ENVIRONMENT_STAGE_STAMP}"
        DEPENDS
            ${UVSR_ENVIRONMENT_ASSETS}
            "${UVSR_ENVIRONMENT_ASSET_MANIFEST}"
        COMMENT "Staging the CC0 image-based-lighting environments"
        VERBATIM
    )
    add_custom_target(uvsr_stage_environments
        DEPENDS "${UVSR_ENVIRONMENT_STAGE_STAMP}")

    set(UVSR_NOISE_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/noise")
    set(UVSR_NOISE_STAGE_DIR
        "${CMAKE_BINARY_DIR}/media/uvsr/noise")
    file(GLOB_RECURSE UVSR_NOISE_ASSETS
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${UVSR_NOISE_SOURCE_DIR}/*")
    list(SORT UVSR_NOISE_ASSETS)
    string(JOIN "\n" UVSR_NOISE_ASSET_MANIFEST_CONTENT
        ${UVSR_NOISE_ASSETS})
    set(UVSR_NOISE_ASSET_MANIFEST
        "${CMAKE_BINARY_DIR}/uvsr_noise_assets.manifest")
    file(GENERATE
        OUTPUT "${UVSR_NOISE_ASSET_MANIFEST}"
        CONTENT "${UVSR_NOISE_ASSET_MANIFEST_CONTENT}\n")
    set(UVSR_NOISE_STAGE_STAMP
        "${UVSR_NOISE_STAGE_DIR}/.uvsr-stage.stamp")
    add_custom_command(
        OUTPUT "${UVSR_NOISE_STAGE_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${UVSR_NOISE_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${UVSR_NOISE_SOURCE_DIR}"
            "${UVSR_NOISE_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch
            "${UVSR_NOISE_STAGE_STAMP}"
        DEPENDS
            ${UVSR_NOISE_ASSETS}
            "${UVSR_NOISE_ASSET_MANIFEST}"
        COMMENT "Staging the precomputed UVSR noise textures"
        VERBATIM)
    add_custom_target(uvsr_stage_noise
        DEPENDS "${UVSR_NOISE_STAGE_STAMP}")
    add_dependencies(uvsr
        uvsr_shaders
        uvsr_stage_environments
        uvsr_stage_noise)
endif()

enable_testing()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
if (WIN32)
    set(UVSR_README_LINE_COUNT_COMMAND
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/update_readme_line_counts.cmd"
    )
else()
    set(UVSR_README_LINE_COUNT_COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/update_readme_line_counts.py"
    )
endif()
add_test(
    NAME uvsr_readme_line_count_self_test
    COMMAND ${UVSR_README_LINE_COUNT_COMMAND} --self-test
)
add_test(
    NAME uvsr_readme_line_count_contract
    COMMAND ${UVSR_README_LINE_COUNT_COMMAND} --check
)
set_tests_properties(
    uvsr_readme_line_count_self_test
    uvsr_readme_line_count_contract
    PROPERTIES LABELS "documentation"
)
if (WIN32)
    add_test(
        NAME uvsr_launch_menu_contract
        COMMAND powershell.exe
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/launch_uvsr_menu_tests.ps1"
            -Launcher "${CMAKE_CURRENT_SOURCE_DIR}/LaunchUVSR.cmd"
            -LaunchHelper "${CMAKE_CURRENT_SOURCE_DIR}/tools/launch_uvsr.ps1"
    )
    set_tests_properties(
        uvsr_launch_menu_contract
        PROPERTIES
            LABELS "launcher;windows"
            TIMEOUT 30
    )
endif()

add_executable(uvsr_scene_catalog_tests
    tests/scene_catalog_tests.cpp
    src/scene_catalog.cpp
)
target_include_directories(
    uvsr_scene_catalog_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(uvsr_scene_catalog_tests PRIVATE donut_core)
add_test(NAME uvsr_scene_catalog_reference COMMAND uvsr_scene_catalog_tests)

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
    uvsr_heitz_ratio_estimator_tests
    tests/heitz_ratio_estimator_tests.cpp)
target_include_directories(
    uvsr_heitz_ratio_estimator_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_heitz_ratio_estimator_reference
    COMMAND uvsr_heitz_ratio_estimator_tests)

add_executable(
    uvsr_heitz_ratio_estimator_source_contract_tests
    tests/heitz_ratio_estimator_source_contract_tests.cpp)
add_test(
    NAME uvsr_heitz_ratio_estimator_source_contract
    COMMAND uvsr_heitz_ratio_estimator_source_contract_tests
        "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(
    uvsr_ray_traced_sky_visibility_tests
    tests/ray_traced_sky_visibility_tests.cpp)
target_include_directories(
    uvsr_ray_traced_sky_visibility_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_sky_visibility_reference
    COMMAND uvsr_ray_traced_sky_visibility_tests)

add_executable(
    uvsr_ray_traced_sky_visibility_source_contract_tests
    tests/ray_traced_sky_visibility_source_contract_tests.cpp)
add_test(
    NAME uvsr_sky_visibility_source_contract
    COMMAND uvsr_ray_traced_sky_visibility_source_contract_tests
        "${CMAKE_CURRENT_SOURCE_DIR}")

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
    uvsr_sample_accumulation_settings_tests
    tests/sample_accumulation_settings_tests.cpp)
target_include_directories(
    uvsr_sample_accumulation_settings_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_sample_accumulation_settings_reference
    COMMAND uvsr_sample_accumulation_settings_tests)

add_executable(
    uvsr_path_tracing_source_contract_tests
    tests/path_tracing_source_contract_tests.cpp)
add_test(
    NAME uvsr_path_tracing_source_contract
    COMMAND uvsr_path_tracing_source_contract_tests
        "${CMAKE_CURRENT_SOURCE_DIR}")

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
add_test(
    NAME uvsr_ray_traced_flashlight_shadow_contract
    COMMAND uvsr_ray_traced_flashlight_shadow_tests
        "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(
    uvsr_denoiser_tests
    tests/denoiser_backend_tests.cpp)
target_link_libraries(
    uvsr_denoiser_tests
    PRIVATE uvsr_denoiser)
add_test(
    NAME uvsr_denoiser_reference
    COMMAND uvsr_denoiser_tests)

add_executable(uvsr_pbr_lighting_source_contract_tests
    tests/pbr_lighting_source_contract_tests.cpp)
target_include_directories(
    uvsr_pbr_lighting_source_contract_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_pbr_lighting_source_contract
    COMMAND uvsr_pbr_lighting_source_contract_tests
        "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(uvsr_command_line_options_tests
    tests/command_line_options_tests.cpp)
target_include_directories(
    uvsr_command_line_options_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_command_line_options_reference
    COMMAND uvsr_command_line_options_tests)

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
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/donut/thirdparty/stb")
target_compile_definitions(uvsr_diffuse_environment_asset_tests PRIVATE
    "UVSR_TEST_ENVIRONMENT_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/environments\"")
target_link_libraries(uvsr_diffuse_environment_asset_tests PRIVATE donut_core)
add_test(
    NAME uvsr_diffuse_environment_asset_reference
    COMMAND uvsr_diffuse_environment_asset_tests)

add_executable(uvsr_temporal_aa_tests tests/temporal_aa_tests.cpp)
target_include_directories(
    uvsr_temporal_aa_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_temporal_aa_reference
    COMMAND uvsr_temporal_aa_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

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
        "${CMAKE_CURRENT_SOURCE_DIR}/donut/nvrhi/include")
add_test(
    NAME uvsr_auto_exposure_reference
    COMMAND uvsr_auto_exposure_tests "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(uvsr_sponza_camera_tests
    tests/sponza_camera_preset_tests.cpp
    src/sponza_camera_preset.cpp
)
target_include_directories(
    uvsr_sponza_camera_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(uvsr_sponza_camera_tests PRIVATE donut_core)
add_test(
    NAME uvsr_sponza_camera_reference
    COMMAND uvsr_sponza_camera_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/intel_sponza"
)

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

add_executable(uvsr_ui_source_contract_tests
    tests/ui_source_contract_tests.cpp
)
add_test(
    NAME uvsr_ui_source_contract
    COMMAND uvsr_ui_source_contract_tests
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(uvsr_renderer_source_contract_tests
    tests/renderer_source_contract_tests.cpp
)
add_test(
    NAME uvsr_renderer_source_contract
    COMMAND uvsr_renderer_source_contract_tests
        "${CMAKE_CURRENT_SOURCE_DIR}"
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
add_test(
    NAME uvsr_settings_snapshot_decoder_reference
    COMMAND "${CMAKE_COMMAND}" -E env
        "PYTHONDONTWRITEBYTECODE=1"
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/settings_snapshot_decoder_tests.py"
)

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
add_test(
    NAME uvsr_pixel_zoom_reference
    COMMAND uvsr_pixel_zoom_tests
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Donut builds its complete shader catalog as an internal dependency. Stage it
# outside the runtime tree, then package only the shaders instantiated by UVSR.
set(UVSR_ROOT_SHADERS
    blit_ps
    fullscreen_vs
    imgui_pixel
    imgui_vertex
    rect_vs
    sharpen_ps
    skinning_cs
)
set(UVSR_PASS_SHADERS
    depth_ps
    depth_vs_buffer_loads
    gbuffer_vs_buffer_loads
    light_probe_cubemap_gs
    light_probe_diffuse_probe_ps
    light_probe_environment_brdf_ps
    light_probe_mip_ps
    light_probe_specular_probe_ps
    material_id_ps
    pixel_readback_cs
)

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

set(UVSR_APP_SHADERS
    agx_tonemapping_ps
    auto_exposure_histogram_cs
    auto_exposure_resolve_cs
    display_output_ps
    fast_approximate_aa_ps
    backdrop_blur_ps
    denoising_prepare_cs
    denoising_resolve_cs
    denoising_spatial_cs
    heitz_ratio_estimator_shadows_cs_Generate
    ray_traced_flashlight_shadows_cs_GenerateVisibility
    ray_traced_flashlight_shadows_cs_GenerateVisibilityAndHitDistance
    ray_traced_sky_visibility_cs_Generate
    image_based_lighting_background_ps
    lighting_accumulation_cs
    lighting_accumulation_prepare_cs
    msaa_visibility_resolve_cs
    path_tracing_cs
    path_tracing_primary_surface_cs
    path_tracing_stable_plane_resolve_cs
    pbr_deferred_lighting_cs
    pbr_deferred_lighting_msaa_cs
    pbr_gbuffer_ps
    pixel_zoom_ps
    screen_space_indirect_composite_cs
    screen_space_visibility_filter_cs
    screen_space_visibility_cs
    temporal_aa_blend_cs
    temporal_aa_minimum_cs
    temporal_aa_resolve_cs
    temporal_aa_sharpen_cs
)
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

if (UVSR_BUILD_APPLICATION)
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
        ${UVSR_SHADER_INPUTS}
        "${UVSR_SHADER_COMPILE_STAMP}"
        "${UVSR_RUNTIME_SHADER_MANIFEST}"
        "${UVSR_RUNTIME_SHADER_PATH_MANIFEST}"
        "${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}"
        "${UVSR_SHADER_CONFIG}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SyncRuntimeShaderBundle.cmake"
        "${SHADERMAKE_PATH}"
        "${SHADERMAKE_DXC_PATH}")
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
    add_dependencies(uvsr uvsr_stage_runtime_shaders)

    add_executable(uvsr_runtime_shader_bundle_tests
        tests/runtime_shader_bundle_tests.cpp)
    add_dependencies(uvsr_runtime_shader_bundle_tests
        uvsr_stage_runtime_shaders)
    add_test(
        NAME uvsr_runtime_shader_bundle_contract
        COMMAND uvsr_runtime_shader_bundle_tests
            "${UVSR_RUNTIME_SHADER_SOURCE_MANIFEST}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    set_tests_properties(
        uvsr_runtime_shader_bundle_contract
        PROPERTIES LABELS "runtime;shaders")

    add_executable(
        uvsr_production_shader_bundle_tests
        tests/production_shader_bundle_tests.cpp)
    add_dependencies(
        uvsr_production_shader_bundle_tests
        uvsr_stage_runtime_shaders)
    add_test(
        NAME uvsr_production_shader_bundle_contract
        COMMAND uvsr_production_shader_bundle_tests
            "${UVSR_SHADER_CONFIG}"
            "${UVSR_RUNTIME_SHADER_MANIFEST}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    set_tests_properties(
        uvsr_production_shader_bundle_contract
        PROPERTIES LABELS "production;shaders")
endif()

set(UVSR_SCENE_SOURCE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes")
set(UVSR_SCENE_STAGING_ROOT
    "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models")
set(UVSR_BUNDLED_SCENE_DIRECTORIES
    intel_sponza
    bistro_interior_retextured
    san_miguel_retextured
    blender_classroom)
set(UVSR_SCENE_RUNTIME_ASSETS)
set(UVSR_SCENE_STAGE_COMMANDS)
foreach(UVSR_SCENE_DIRECTORY IN LISTS UVSR_BUNDLED_SCENE_DIRECTORIES)
    file(GLOB_RECURSE UVSR_CURRENT_SCENE_RUNTIME_ASSETS
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${UVSR_SCENE_SOURCE_ROOT}/${UVSR_SCENE_DIRECTORY}/*")
    list(APPEND UVSR_SCENE_RUNTIME_ASSETS
        ${UVSR_CURRENT_SCENE_RUNTIME_ASSETS})
    list(APPEND UVSR_SCENE_STAGE_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${UVSR_SCENE_SOURCE_ROOT}/${UVSR_SCENE_DIRECTORY}"
            "${UVSR_SCENE_STAGING_ROOT}/${UVSR_SCENE_DIRECTORY}")
endforeach()
list(SORT UVSR_SCENE_RUNTIME_ASSETS)
string(JOIN "\n" UVSR_SCENE_ASSET_MANIFEST_CONTENT
    ${UVSR_SCENE_RUNTIME_ASSETS})
set(UVSR_SCENE_ASSET_MANIFEST
    "${CMAKE_BINARY_DIR}/scene_runtime_assets.manifest")
file(GENERATE
    OUTPUT "${UVSR_SCENE_ASSET_MANIFEST}"
    CONTENT "${UVSR_SCENE_ASSET_MANIFEST_CONTENT}\n"
)
set(UVSR_SCENE_STAGE_STAMP
    "${UVSR_SCENE_STAGING_ROOT}/.uvsr-stage.stamp")
add_custom_command(
    OUTPUT "${UVSR_SCENE_STAGE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${UVSR_SCENE_STAGING_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${UVSR_SCENE_STAGING_ROOT}"
    ${UVSR_SCENE_STAGE_COMMANDS}
    COMMAND ${CMAKE_COMMAND} -E touch
        "${UVSR_SCENE_STAGE_STAMP}"
    DEPENDS
        ${UVSR_SCENE_RUNTIME_ASSETS}
        "${UVSR_SCENE_ASSET_MANIFEST}"
    COMMENT "Synchronizing the explicit UVSR runtime scene bundle"
    VERBATIM
)
add_custom_target(uvsr_stage_scenes
    DEPENDS "${UVSR_SCENE_STAGE_STAMP}"
)
if (UVSR_BUILD_APPLICATION)
    add_dependencies(uvsr uvsr_stage_scenes)
endif()

add_executable(uvsr_scene_asset_contract_tests
    tests/scene_asset_contract_tests.cpp
    src/scene_catalog.cpp)
target_include_directories(uvsr_scene_asset_contract_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_scene_asset_contract_tests
    PRIVATE donut_core cgltf)
add_dependencies(uvsr_scene_asset_contract_tests uvsr_stage_scenes)
add_test(
    NAME uvsr_scene_asset_contract
    COMMAND uvsr_scene_asset_contract_tests
        "${UVSR_SCENE_SOURCE_ROOT}"
        "${UVSR_SCENE_STAGING_ROOT}")
add_test(
    NAME uvsr_scene_asset_provenance
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_MODELS_ROOT=${UVSR_SCENE_SOURCE_ROOT}"
        "-DSTAGED_MODELS_ROOT=${UVSR_SCENE_STAGING_ROOT}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_asset_provenance.cmake")
set_tests_properties(
    uvsr_scene_asset_contract
    uvsr_scene_asset_provenance
    PROPERTIES
        LABELS "assets;scenes"
        TIMEOUT 600)

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
set(UVSR_UI_TRANSITION_LICENSE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/geist/LICENSE.txt")

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
    "${UVSR_UI_TRANSITION_LICENSE_SOURCE}"
    4476
    "336949B89D706C480F5807F4CA1AC0484CAD93FFC2765818526F6035572970AC"
    "transitional Geist OFL 1.1 compatibility notice")

if (UVSR_BUILD_APPLICATION)
    add_custom_command(TARGET uvsr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models/a_beautiful_game"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models/cornell_box"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models/nvidia_bistro"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/media/luts/kodak"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/media/noise"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/media/fonts/Inter"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/media/fonts/Geist"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/media/fonts/System"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/media/fonts/NotoSans"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/media/fonts/NotoSans"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/media/fonts/System"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_REGULAR_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/NotoSans/NotoSans-Regular.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_SEMIBOLD_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/NotoSans/NotoSans-SemiBold.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_BOLD_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/NotoSans/NotoSans-Bold.ttf"
        # UVSR Launcher sequence 9 requires the historical paths while the
        # public source transitions to Noto Sans. These are byte-for-byte Noto
        # aliases; the renderer never loads the compatibility names.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_REGULAR_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/System/CodexUI.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_SEMIBOLD_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/System/CodexUI-Semibold.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_BOLD_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/System/CodexUI-Bold.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/UVSR-Polyform-Noncommercial-1.0.0.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Microsoft-DirectX-Graphics-Samples-MIT.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Microsoft-DirectX-Graphics-Samples.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/Apache-2.0.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Apache-2.0.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/BSD-2-Clause.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/BSD-2-Clause.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/legal/licenses/IOLITE-AgX-MIT.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/IOLITE-AgX-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/legal/documentation/google-filament-fxaa.md"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Google-Filament-FXAA-Attribution.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/donut/LICENSE.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/NVIDIA-Donut-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/donut/ThirdPartyLicenses.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Donut-Third-Party-Licenses.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/donut/nvrhi/LICENSE.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/NVIDIA-NVRHI-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/donut/ShaderMake/LICENSE.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/NVIDIA-ShaderMake-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/donut/thirdparty/imgui/LICENSE.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Dear-ImGui-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_FONT_LICENSE_SOURCE}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Noto-Sans-OFL-1.1.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_PROGGY_CLEAN_LICENSE_SOURCE}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/ProggyClean-MIT.txt"
        # No Geist font is staged or loaded by the current renderer. Sequence 9
        # requires this historical notice filename during the transition.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_TRANSITION_LICENSE_SOURCE}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Geist-OFL-1.1.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/intel_sponza/LICENSE.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Intel-PBR-Sponza.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/bistro_interior_retextured/LICENSE.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Amazon-Lumberyard-Bistro.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/san_miguel_retextured/LICENSE.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/San-Miguel-2.1.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/blender_classroom/LICENSE.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Blender-Classroom-CC0-1.0.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/environments/README.md"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Poly-Haven-Environments.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/legal/documentation/third-party-notices.md"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/third-party-notices.md"
    )
    if (EXISTS "${DirectX-Headers_SOURCE_DIR}/LICENSE")
        add_custom_command(TARGET uvsr POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DirectX-Headers_SOURCE_DIR}/LICENSE"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Microsoft-DirectX-Headers-MIT.txt")
    endif()
    if (EXISTS "${DONUT_D3D_AGILITY_SDK_PATH}/LICENSE.txt")
        add_custom_command(TARGET uvsr POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DONUT_D3D_AGILITY_SDK_PATH}/LICENSE.txt"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Microsoft-D3D12-Agility-SDK-Terms.txt"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DONUT_D3D_AGILITY_SDK_PATH}/LICENSE-CODE.txt"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Microsoft-D3D12-Agility-SDK-Code-MIT.txt")
    endif()
    if (UVSR_WITH_NRD)
        add_custom_command(TARGET uvsr POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${nrd_SOURCE_DIR}/LICENSE.txt"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/NRD-LICENSE.txt"
        )
    endif()
endif()
