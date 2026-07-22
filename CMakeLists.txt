cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UVSR_BUILD_APPLICATION
    "Build the UVSR application that integrates every enabled producer" ON)
option(UVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS
    "Build the standalone Bend screen-space-shadow component" ON)
option(UVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS
    "Build the standalone sparse-virtual-shadow-map component" ON)

if (UVSR_BUILD_APPLICATION AND
    (NOT UVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS OR
        NOT UVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS))
    message(FATAL_ERROR
        "The aggregate UVSR application currently integrates both visibility "
        "producers. Set UVSR_BUILD_APPLICATION=OFF to configure and build "
        "either component independently.")
endif()

if (UVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS)
    set(UVSR_BEND_SSS_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bend_sss/upstream")
    file(SHA256 "${UVSR_BEND_SSS_DIR}/bend_sss_cpu.h"
        UVSR_BEND_SSS_CPU_SHA256)
    file(SHA256 "${UVSR_BEND_SSS_DIR}/bend_sss_gpu.h"
        UVSR_BEND_SSS_GPU_SHA256)
    if (NOT UVSR_BEND_SSS_CPU_SHA256 STREQUAL
            "23aae596dbb1b9bdae23d87ac85079b138426823c3153079f7a7dd36f603d02a")
        message(FATAL_ERROR
            "Bend's upstream CPU header changed; restore the released bytes.")
    endif()
    if (NOT UVSR_BEND_SSS_GPU_SHA256 STREQUAL
            "7fbe24bd2040a62536c31df6cd38a92cb22c172a9a419eed3c0af7df1d50a68c")
        message(FATAL_ERROR
            "Bend's upstream GPU header changed; restore the released bytes.")
    endif()
endif()

# Embed the exact source commit in the executable so experiment titles describe
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
    patch_file
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
endfunction()

function(uvsr_replace_target_sources
    target
    source_root
    override_root)
    get_target_property(target_sources "${target}" SOURCES)
    foreach(relative_path IN LISTS ARGN)
        set(original_source "${source_root}/${relative_path}")
        list(FIND target_sources "${original_source}" source_index)
        if (source_index EQUAL -1)
            message(FATAL_ERROR
                "Cannot replace ${original_source} in target ${target}")
        endif()
        list(REMOVE_ITEM target_sources "${original_source}")
    endforeach()
    set_property(TARGET "${target}" PROPERTY SOURCES "${target_sources}")

    foreach(relative_path IN LISTS ARGN)
        target_sources("${target}" PRIVATE
            "${override_root}/${relative_path}")
    endforeach()
endfunction()

set(UVSR_DONUT_ENGINE_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_donut_engine_overrides")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-engine.patch"
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}"
    include/donut/engine/SceneTypes.h
    src/engine/GltfImporter.cpp
    src/engine/Scene.cpp
    src/engine/TextureCache.cpp)

set(UVSR_IMGUI_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_imgui_overrides")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/thirdparty/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-ui.patch"
    "${UVSR_IMGUI_OVERRIDE_DIR}"
    imgui.cpp
    imgui.h
    imgui_draw.cpp
    imgui_internal.h
    imgui_widgets.cpp)

# Donut's asynchronous loader writes m_SceneLoaded on a worker thread and
# reads it on the render thread. Generate an atomic header override alongside
# the staged source patches.
set(UVSR_DONUT_OVERRIDE_DIR "${CMAKE_BINARY_DIR}/uvsr_donut_overrides")
set(UVSR_DONUT_APPLICATION_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/donut/include/donut/app/ApplicationBase.h")
file(READ "${UVSR_DONUT_APPLICATION_HEADER}" UVSR_DONUT_APPLICATION_HEADER_CONTENT)
string(REPLACE
    "#include <filesystem>"
    "#include <filesystem>\n#include <atomic>"
    UVSR_DONUT_APPLICATION_HEADER_CONTENT
    "${UVSR_DONUT_APPLICATION_HEADER_CONTENT}"
)
string(REPLACE
    "bool m_SceneLoaded;"
    "std::atomic_bool m_SceneLoaded;"
    UVSR_DONUT_APPLICATION_HEADER_CONTENT
    "${UVSR_DONUT_APPLICATION_HEADER_CONTENT}"
)
file(MAKE_DIRECTORY "${UVSR_DONUT_OVERRIDE_DIR}/donut/app")
file(WRITE
    "${UVSR_DONUT_OVERRIDE_DIR}/donut/app/ApplicationBase.h"
    "${UVSR_DONUT_APPLICATION_HEADER_CONTENT}"
)

add_subdirectory(donut)
target_include_directories(donut_app BEFORE PUBLIC "${UVSR_DONUT_OVERRIDE_DIR}")
uvsr_replace_target_sources(
    donut_engine
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}"
    include/donut/engine/SceneTypes.h
    src/engine/GltfImporter.cpp
    src/engine/Scene.cpp
    src/engine/TextureCache.cpp)
target_include_directories(donut_engine BEFORE PUBLIC
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}/include")

uvsr_replace_target_sources(
    imgui
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/thirdparty/imgui"
    "${UVSR_IMGUI_OVERRIDE_DIR}"
    imgui.cpp
    imgui.h
    imgui_draw.cpp
    imgui_internal.h
    imgui_widgets.cpp)
target_include_directories(imgui BEFORE PUBLIC
    "${UVSR_IMGUI_OVERRIDE_DIR}")

if (NVRHI_WITH_DX12)
    set(DONUT_D3D_AGILITY_SDK_URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.717.1-preview")
    set(DONUT_D3D_AGILITY_SDK_FETCH_DIR "" CACHE PATH
        "Optional existing or shared D3D12 Agility SDK extraction")
    include("${CMAKE_CURRENT_SOURCE_DIR}/donut/cmake/FetchAgilitySDK.cmake")
    file(COPY
        "${DONUT_D3D_AGILITY_SDK_PATH}/build/native/bin/x64/D3D12Core.dll"
        "${DONUT_D3D_AGILITY_SDK_PATH}/build/native/bin/x64/D3D12SDKLayers.dll"
        DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/D3D12/"
    )
endif()

set(UVSR_SHADER_STAGING_DIR "${CMAKE_BINARY_DIR}/uvsr_shaders")
include(donut/compileshaders.cmake)
donut_compile_shaders_all_platforms(
    TARGET uvsr_shaders
    PROJECT_NAME "UVSR"
    CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.cfg"
    OUTPUT_BASE "${UVSR_SHADER_STAGING_DIR}"
)

if (UVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS)
    donut_compile_shaders_all_platforms(
        TARGET uvsr_bend_screen_space_shadow_shaders
        PROJECT_NAME "UVSR Bend Screen-Space Shadows"
        CONFIG
            "${CMAKE_CURRENT_SOURCE_DIR}/src/bend_screen_space_shadows_shaders.cfg"
        OUTPUT_BASE "${UVSR_SHADER_STAGING_DIR}"
    )
endif()

if (UVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS)
    donut_compile_shaders_all_platforms(
        TARGET uvsr_sparse_virtual_shadow_map_shaders
        PROJECT_NAME "UVSR Sparse Virtual Shadow Maps"
        CONFIG
            "${CMAKE_CURRENT_SOURCE_DIR}/src/sparse_virtual_shadow_map_shaders.cfg"
        OUTPUT_BASE "${UVSR_SHADER_STAGING_DIR}"
    )
endif()

if (UVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS)
    add_library(uvsr_bend_screen_space_shadows STATIC
        src/bend_screen_space_shadows.cpp
    )
    target_include_directories(uvsr_bend_screen_space_shadows
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bend_sss/upstream")
    target_link_libraries(uvsr_bend_screen_space_shadows
        PUBLIC nvrhi
        PRIVATE donut_render donut_app donut_engine)
    add_dependencies(uvsr_bend_screen_space_shadows
        uvsr_bend_screen_space_shadow_shaders)
endif()

if (UVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS)
    add_library(uvsr_gpu_crash_diagnostics STATIC
        src/gpu_crash_diagnostics.cpp
    )
    target_include_directories(uvsr_gpu_crash_diagnostics
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(uvsr_gpu_crash_diagnostics
        PUBLIC nvrhi
        PRIVATE donut_render donut_app donut_engine)

    add_library(uvsr_sparse_virtual_shadow_maps STATIC
        src/sparse_virtual_shadow_map.cpp
    )
    target_include_directories(uvsr_sparse_virtual_shadow_maps
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(uvsr_sparse_virtual_shadow_maps
        PUBLIC
            donut_core
            nvrhi
        PRIVATE
            donut_render
            donut_app
            donut_engine
            uvsr_gpu_crash_diagnostics)
    add_dependencies(uvsr_sparse_virtual_shadow_maps
        uvsr_sparse_virtual_shadow_map_shaders)
endif()

if (UVSR_BUILD_APPLICATION)
    add_executable(uvsr WIN32
        src/camera_collision.cpp
        src/gpu_performance_monitor.cpp
        src/pbr_deferred_lighting_pass.cpp
        src/scene_catalog.cpp
        src/screen_space_visibility.cpp
        src/sponza_camera_preset.cpp
        src/taa_miniengine.cpp
        src/uvsr.cpp
        src/visibility_blue_noise.cpp
    )
    target_include_directories(uvsr BEFORE PRIVATE
        "${UVSR_IMGUI_OVERRIDE_DIR}"
        "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}/include")
    target_link_libraries(uvsr PRIVATE
        donut_render
        donut_app
        donut_engine
        uvsr_gpu_crash_diagnostics
        uvsr_bend_screen_space_shadows
        uvsr_sparse_virtual_shadow_maps)
    target_compile_definitions(uvsr PRIVATE
        "UVSR_GIT_COMMIT=\"${UVSR_GIT_COMMIT}\"")
    add_dependencies(uvsr uvsr_shaders)
endif()

enable_testing()
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

add_executable(uvsr_pbr_tests tests/pbr_reference_tests.cpp)
target_include_directories(uvsr_pbr_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_pbr_tests PRIVATE donut_core)
add_test(NAME uvsr_pbr_reference COMMAND uvsr_pbr_tests)

if (UVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS)
    add_executable(uvsr_bend_screen_space_shadow_tests
        tests/bend_screen_space_shadows_tests.cpp
    )
    target_link_libraries(uvsr_bend_screen_space_shadow_tests
        PRIVATE uvsr_bend_screen_space_shadows)
    add_test(
        NAME uvsr_bend_screen_space_shadow_reference
        COMMAND uvsr_bend_screen_space_shadow_tests
    )
endif()

if (UVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS)
    add_executable(uvsr_sparse_virtual_shadow_map_tests
        tests/sparse_virtual_shadow_map_tests.cpp
    )
    target_link_libraries(uvsr_sparse_virtual_shadow_map_tests
        PRIVATE uvsr_sparse_virtual_shadow_maps)
    add_test(
        NAME uvsr_sparse_virtual_shadow_map_reference
        COMMAND uvsr_sparse_virtual_shadow_map_tests
    )
endif()

add_executable(uvsr_taa_miniengine_tests tests/taa_miniengine_tests.cpp)
target_include_directories(
    uvsr_taa_miniengine_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_taa_miniengine_reference
    COMMAND uvsr_taa_miniengine_tests
)

add_executable(uvsr_world_material_view_tests
    tests/world_material_view_tests.cpp
)
target_include_directories(
    uvsr_world_material_view_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_world_material_view_reference
    COMMAND uvsr_world_material_view_tests
)

add_executable(uvsr_experiment_title_tests tests/experiment_title_tests.cpp)
target_include_directories(
    uvsr_experiment_title_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(NAME uvsr_experiment_title_reference COMMAND uvsr_experiment_title_tests)

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
    src/visibility_blue_noise.cpp
)
target_include_directories(
    uvsr_visibility_sampling_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_visibility_sampling_reference
    COMMAND uvsr_visibility_sampling_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/noise/visibility_filter_adapted_gauss1_ema035_r8.bin"
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
    deferred_lighting_cs
    forward_ps
    forward_vs_buffer_loads
    gbuffer_ps
    gbuffer_vs_buffer_loads
    material_id_ps
    pixel_readback_cs
    sky_ps
)

set(UVSR_RUNTIME_SHADER_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/framework/dxil")
set(UVSR_RUNTIME_APP_SHADER_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/uvsr/dxil")

if (UVSR_BUILD_APPLICATION)
    add_custom_command(TARGET uvsr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${UVSR_RUNTIME_SHADER_DIR}/passes"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${UVSR_RUNTIME_APP_SHADER_DIR}"
    )

    foreach(shader IN LISTS UVSR_ROOT_SHADERS)
        add_custom_command(TARGET uvsr POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UVSR_DONUT_SHADER_STAGING_DIR}/dxil/${shader}.bin"
                "${UVSR_RUNTIME_SHADER_DIR}/${shader}.bin"
        )
    endforeach()

    foreach(shader IN LISTS UVSR_PASS_SHADERS)
        add_custom_command(TARGET uvsr POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UVSR_DONUT_SHADER_STAGING_DIR}/dxil/passes/${shader}.bin"
                "${UVSR_RUNTIME_SHADER_DIR}/passes/${shader}.bin"
        )
    endforeach()
endif()

set(UVSR_APP_SHADERS
    agx_tonemapping_ps
    backdrop_blur_ps
    bend_screen_space_shadows_cs
    bend_screen_space_shadows_debug_ps
    pbr_deferred_lighting_cs
    pbr_forward_ps
    pbr_gbuffer_ps
    screen_space_depth_hierarchy_cs
    screen_space_indirect_composite_cs
    screen_space_visibility_filter_cs
    screen_space_visibility_cs
    screen_space_visibility_temporal_cs
    sparse_virtual_shadow_map_depth_ps
    sparse_virtual_shadow_map_debug_ps
    sparse_virtual_shadow_map_resolve_cs
    sparse_virtual_shadow_map_sparse_cs_allocate
    sparse_virtual_shadow_map_sparse_cs_clearPages
    sparse_virtual_shadow_map_sparse_cs_fillIndirect
    sparse_virtual_shadow_map_sparse_cs_finalize
    sparse_virtual_shadow_map_sparse_cs_mark
    sparse_virtual_shadow_map_sparse_cs_prepare
    sparse_virtual_shadow_map_sparse_cs_recycle
    sparse_virtual_shadow_map_sparse_cs_stats
    sparse_virtual_shadow_map_sparse_depth_pixelMain
    sparse_virtual_shadow_map_sparse_depth_vertexMain
    sparse_virtual_shadow_map_sparse_resolve_cs
    taa_miniengine_blend_cs
    taa_miniengine_resolve_cs
    taa_miniengine_sharpen_cs
)

if (UVSR_BUILD_APPLICATION)
    foreach(shader IN LISTS UVSR_APP_SHADERS)
        add_custom_command(TARGET uvsr POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UVSR_SHADER_STAGING_DIR}/dxil/${shader}.bin"
                "${UVSR_RUNTIME_APP_SHADER_DIR}/${shader}.bin"
        )
    endforeach()
endif()

set(UVSR_INTEL_SPONZA_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets/scenes/intel_sponza")
set(UVSR_INTEL_SPONZA_STAGING_DIR
    "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models/intel_sponza")
file(GLOB_RECURSE UVSR_INTEL_SPONZA_RUNTIME_ASSETS
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${UVSR_INTEL_SPONZA_SOURCE_DIR}/*"
)
list(SORT UVSR_INTEL_SPONZA_RUNTIME_ASSETS)
string(JOIN "\n" UVSR_INTEL_SPONZA_ASSET_MANIFEST_CONTENT
    ${UVSR_INTEL_SPONZA_RUNTIME_ASSETS})
set(UVSR_INTEL_SPONZA_ASSET_MANIFEST
    "${CMAKE_BINARY_DIR}/intel_sponza_runtime_assets.manifest")
file(GENERATE
    OUTPUT "${UVSR_INTEL_SPONZA_ASSET_MANIFEST}"
    CONTENT "${UVSR_INTEL_SPONZA_ASSET_MANIFEST_CONTENT}\n"
)
set(UVSR_INTEL_SPONZA_STAGE_STAMP
    "${UVSR_INTEL_SPONZA_STAGING_DIR}/.uvsr-stage.stamp")
add_custom_command(
    OUTPUT "${UVSR_INTEL_SPONZA_STAGE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${UVSR_INTEL_SPONZA_STAGING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${UVSR_INTEL_SPONZA_STAGING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${UVSR_INTEL_SPONZA_SOURCE_DIR}"
        "${UVSR_INTEL_SPONZA_STAGING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch
        "${UVSR_INTEL_SPONZA_STAGE_STAMP}"
    DEPENDS
        ${UVSR_INTEL_SPONZA_RUNTIME_ASSETS}
        "${UVSR_INTEL_SPONZA_ASSET_MANIFEST}"
    COMMENT "Synchronizing Intel PBR Sponza runtime assets"
    VERBATIM
)
add_custom_target(uvsr_stage_intel_sponza
    DEPENDS "${UVSR_INTEL_SPONZA_STAGE_STAMP}"
)
if (UVSR_BUILD_APPLICATION)
    add_dependencies(uvsr uvsr_stage_intel_sponza)
endif()

if (WIN32)
    file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts/SegUIVar.ttf"
        UVSR_UI_FONT_SOURCE)
    if (NOT EXISTS "${UVSR_UI_FONT_SOURCE}")
        file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts/segoeui.ttf"
            UVSR_UI_FONT_SOURCE)
    endif()
    if (NOT EXISTS "${UVSR_UI_FONT_SOURCE}")
        message(FATAL_ERROR
            "Segoe UI system font was not found in the Windows font directory")
    endif()
    file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts/seguisb.ttf"
        UVSR_UI_HEADER_FONT_SOURCE)
    if (NOT EXISTS "${UVSR_UI_HEADER_FONT_SOURCE}")
        file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts/seguibl.ttf"
            UVSR_UI_HEADER_FONT_SOURCE)
    endif()
    if (NOT EXISTS "${UVSR_UI_HEADER_FONT_SOURCE}")
        message(FATAL_ERROR
            "Segoe UI Semibold or Bold was not found in the Windows font directory")
    endif()
else()
    set(UVSR_UI_FONT_SOURCE
        "${CMAKE_SOURCE_DIR}/assets/fonts/geist/Geist-Medium.ttf")
    set(UVSR_UI_HEADER_FONT_SOURCE
        "${CMAKE_SOURCE_DIR}/assets/fonts/geist/Geist-Medium.ttf")
endif()

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
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/media/fonts/System"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/System/CodexUI.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_HEADER_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/System/CodexUI-Semibold.ttf"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/media/noise"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/noise/visibility_filter_adapted_gauss1_ema035_r8.bin"
            "${CMAKE_SOURCE_DIR}/assets/noise/FAST-LICENSE.txt"
            "${CMAKE_SOURCE_DIR}/assets/noise/README.md"
            "${CMAKE_BINARY_DIR}/media/noise"
    )
endif()
