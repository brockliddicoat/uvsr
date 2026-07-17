cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Donut's asynchronous loader writes m_SceneLoaded on a worker thread and
# reads it on the render thread. Generate an atomic header override without
# modifying the pinned Donut submodule.
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

if (NVRHI_WITH_DX12)
    set(DONUT_D3D_AGILITY_SDK_URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.717.1-preview")
    set(DONUT_D3D_AGILITY_SDK_FETCH_DIR "" CACHE STRING "" FORCE)
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

add_executable(uvsr WIN32
    src/camera_collision.cpp
    src/gpu_performance_monitor.cpp
    src/uvsr.cpp
    src/pbr_deferred_lighting_pass.cpp
    src/scene_catalog.cpp
    src/screen_space_visibility.cpp
    src/sponza_camera_preset.cpp
    src/visibility_blue_noise.cpp
    src/visibility_benchmark_statistics.cpp
    src/visibility_performance_plan.cpp
)
target_link_libraries(uvsr PRIVATE donut_render donut_app donut_engine)
target_compile_definitions(uvsr PRIVATE "UVSR_GIT_COMMIT=\"${UVSR_GIT_COMMIT}\"")
add_dependencies(uvsr uvsr_shaders)

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

add_executable(uvsr_visibility_performance_plan_tests
    tests/visibility_performance_plan_tests.cpp
    src/visibility_performance_plan.cpp
)
target_include_directories(
    uvsr_visibility_performance_plan_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_visibility_performance_plan_reference
    COMMAND uvsr_visibility_performance_plan_tests
)

add_executable(uvsr_visibility_benchmark_statistics_tests
    tests/visibility_benchmark_statistics_tests.cpp
    src/visibility_benchmark_statistics.cpp
)
target_include_directories(
    uvsr_visibility_benchmark_statistics_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
add_test(
    NAME uvsr_visibility_benchmark_statistics_reference
    COMMAND uvsr_visibility_benchmark_statistics_tests
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

set(UVSR_APP_SHADERS
    agx_tonemapping_ps
    pbr_deferred_lighting_cs
    pbr_forward_ps
    pbr_gbuffer_ps
    screen_space_depth_hierarchy_cs
    screen_space_indirect_composite_cs
    screen_space_visibility_algorithmic_cs
    screen_space_visibility_activision_depth_cs
    screen_space_visibility_activision_spatial_cs
    screen_space_visibility_activision_temporal_cs
    screen_space_visibility_constant_scheduler_cs
    screen_space_visibility_diagnostic_cs
    screen_space_visibility_duplicate_off_cs
    screen_space_visibility_filter_activision_cs
    screen_space_visibility_filter_cs
    screen_space_visibility_filter_diagnostic_cs
    screen_space_visibility_filter_exact_cs
    screen_space_visibility_filter_packed_edge_cs
    screen_space_visibility_fixed_cs
    screen_space_visibility_full_mask_exit_off_cs
    screen_space_visibility_fused_apply_cs
    screen_space_visibility_packed_fast_cs
    screen_space_visibility_radius_clamp_32_cs
    screen_space_visibility_radius_clamp_64_cs
    screen_space_visibility_radius_clamp_128_cs
    screen_space_visibility_cs
    screen_space_visibility_temporal_copy_cs
    screen_space_visibility_temporal_cs
    screen_space_visibility_xegtao_denoise_cs
    screen_space_visibility_xegtao_main_cs
    screen_space_visibility_xegtao_prefilter_cs
)

# Keep the runtime package synchronized with every shader source named by the
# ShaderMake configuration. Missing entries otherwise compile successfully into
# the staging directory but fail only when the corresponding lazy PSO is first
# selected at runtime.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.cfg"
    UVSR_SHADER_CONFIG_LINES)
foreach(shader_config_line IN LISTS UVSR_SHADER_CONFIG_LINES)
    string(REGEX MATCH "^[^ ]+" shader_source "${shader_config_line}")
    if(shader_source STREQUAL "")
        continue()
    endif()
    get_filename_component(shader_name "${shader_source}" NAME_WE)
    list(FIND UVSR_APP_SHADERS "${shader_name}" shader_package_index)
    if(shader_package_index EQUAL -1)
        message(FATAL_ERROR
            "Shader '${shader_name}' is compiled by src/shaders.cfg but missing "
            "from UVSR_APP_SHADERS runtime packaging")
    endif()
endforeach()

foreach(shader IN LISTS UVSR_APP_SHADERS)
    add_custom_command(TARGET uvsr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_SHADER_STAGING_DIR}/dxil/${shader}.bin"
            "${UVSR_RUNTIME_APP_SHADER_DIR}/${shader}.bin"
    )
endforeach()

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
add_dependencies(uvsr uvsr_stage_intel_sponza)

add_custom_command(TARGET uvsr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models/a_beautiful_game"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models/cornell_box"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models/nvidia_bistro"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${CMAKE_BINARY_DIR}/media/luts/kodak"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/media/luts/kodak"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/luts/kodak"
        "${CMAKE_BINARY_DIR}/media/luts/kodak"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${CMAKE_BINARY_DIR}/media/noise"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/media/noise"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/noise/visibility_filter_adapted_gauss1_ema035_r8.bin"
        "${CMAKE_SOURCE_DIR}/assets/noise/FAST-LICENSE.txt"
        "${CMAKE_SOURCE_DIR}/assets/noise/README.md"
        "${CMAKE_BINARY_DIR}/media/noise"
)
