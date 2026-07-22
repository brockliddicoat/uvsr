cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    UVSR_AA_DEVELOPER_OVERRIDES
    "Compile the complete anti-aliasing override permutation matrix and UI"
    OFF
)

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

set(UVSR_DONUT_APP_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_donut_app_overrides")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-app.patch"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    include/donut/app/ApplicationBase.h
    include/donut/app/UserInterfaceUtils.h
    src/app/UserInterfaceUtils.cpp)

set(UVSR_IMGUI_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_imgui_overrides")
set(UVSR_IMGUI_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-ui.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-dropdown-roll.patch")
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

add_subdirectory(donut)
uvsr_replace_target_sources(
    donut_app
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    src/app/UserInterfaceUtils.cpp)
target_include_directories(donut_app BEFORE PUBLIC
    "${UVSR_DONUT_APP_OVERRIDE_DIR}/include")
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
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_internal.h
    imgui_tables.cpp
    imgui_widgets.cpp)
target_include_directories(imgui BEFORE PUBLIC
    "${UVSR_IMGUI_OVERRIDE_DIR}")

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
if (UVSR_AA_DEVELOPER_OVERRIDES)
    set(UVSR_SHADER_CONFIG
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.cfg")
else()
    set(UVSR_SHADER_CONFIG
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders_production.cfg")
endif()
file(GLOB_RECURSE UVSR_SHADER_SOURCES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hlsli"
)
# Donut's Visual Studio shader target otherwise depends only on ShaderMake
# itself. Make both source edits and a developer/production config switch
# invalidate the generated blob instead of staging a stale permutation set.
set(UVSR_SHADER_CONFIGURATION_STAMP
    "${CMAKE_CURRENT_BINARY_DIR}/uvsr_shader_configuration.txt")
file(GENERATE
    OUTPUT "${UVSR_SHADER_CONFIGURATION_STAMP}"
    CONTENT "${UVSR_SHADER_CONFIG}\n"
)
set(UVSR_SHADER_COMPILE_STAMP
    "${UVSR_SHADER_STAGING_DIR}/dxil/.uvsr-shaders.stamp")
add_custom_command(
    OUTPUT "${UVSR_SHADER_COMPILE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${UVSR_SHADER_STAGING_DIR}/dxil"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${UVSR_SHADER_STAGING_DIR}/dxil"
    COMMAND ${SHADERMAKE_PATH}
        --config "${UVSR_SHADER_CONFIG}"
        --out "${UVSR_SHADER_STAGING_DIR}/dxil"
        --platform DXIL
        --binaryBlob
        --outputExt .bin
        -I "${CMAKE_CURRENT_SOURCE_DIR}/donut/include"
        -D TARGET_D3D12
        --compiler "${SHADERMAKE_DXC_PATH}"
        --shaderModel 6_5
        --project UVSR
    COMMAND ${CMAKE_COMMAND} -E touch
        "${UVSR_SHADER_COMPILE_STAMP}"
    DEPENDS
        ${SHADERMAKE_PATH}
        "${UVSR_SHADER_CONFIG}"
        "${UVSR_SHADER_CONFIGURATION_STAMP}"
        ${UVSR_SHADER_SOURCES}
    COMMENT "Compiling UVSR shader bundle from ${UVSR_SHADER_CONFIG}"
    VERBATIM
)
add_custom_target(uvsr_shaders
    DEPENDS "${UVSR_SHADER_COMPILE_STAMP}")

add_executable(uvsr WIN32
    src/camera_collision.cpp
    src/cmaa2.cpp
    src/gpu_performance_monitor.cpp
    src/msaa_visibility_resolve.cpp
    src/pbr_deferred_lighting_pass.cpp
    src/scene_catalog.cpp
    src/screen_space_visibility.cpp
    src/sponza_camera_preset.cpp
    src/taa_miniengine.cpp
    src/temporal_aa_core.cpp
    src/uvsr.cpp
    src/visibility_blue_noise.cpp
    src/visibility_benchmark_statistics.cpp
    src/visibility_performance_plan.cpp
)
target_include_directories(uvsr BEFORE PRIVATE
    "${UVSR_IMGUI_OVERRIDE_DIR}"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}/include"
    "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}/include")
target_link_libraries(
    uvsr
    PRIVATE donut_render donut_app donut_engine dwmapi
)
target_compile_definitions(
    uvsr
    PRIVATE
        "UVSR_GIT_COMMIT=\"${UVSR_GIT_COMMIT}\""
        "UVSR_AA_DEVELOPER_OVERRIDES=$<BOOL:${UVSR_AA_DEVELOPER_OVERRIDES}>"
)
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

add_executable(uvsr_taa_miniengine_tests tests/taa_miniengine_tests.cpp)
target_include_directories(
    uvsr_taa_miniengine_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(
    uvsr_taa_miniengine_tests
    PRIVATE
        "UVSR_AA_DEVELOPER_OVERRIDES=$<BOOL:${UVSR_AA_DEVELOPER_OVERRIDES}>"
)
add_test(
    NAME uvsr_taa_miniengine_reference
    COMMAND uvsr_taa_miniengine_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
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

add_executable(uvsr_imgui_dropdown_roll_tests
    tests/imgui_dropdown_roll_tests.cpp
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
set(UVSR_RUNTIME_SHADER_STAGE_STAMP
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/.uvsr-stage.stamp")
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

add_custom_command(
    OUTPUT "${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${UVSR_RUNTIME_SHADER_DIR}/passes"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${UVSR_RUNTIME_APP_SHADER_DIR}"
    VERBATIM
)

foreach(shader IN LISTS UVSR_ROOT_SHADERS)
    set(UVSR_RUNTIME_SHADER_SOURCE
        "${UVSR_DONUT_SHADER_STAGING_DIR}/dxil/${shader}.bin")
    add_custom_command(
        OUTPUT "${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
        APPEND
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_RUNTIME_SHADER_SOURCE}"
            "${UVSR_RUNTIME_SHADER_DIR}/${shader}.bin"
        DEPENDS "${UVSR_RUNTIME_SHADER_SOURCE}"
    )
endforeach()

foreach(shader IN LISTS UVSR_PASS_SHADERS)
    set(UVSR_RUNTIME_SHADER_SOURCE
        "${UVSR_DONUT_SHADER_STAGING_DIR}/dxil/passes/${shader}.bin")
    add_custom_command(
        OUTPUT "${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
        APPEND
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_RUNTIME_SHADER_SOURCE}"
            "${UVSR_RUNTIME_SHADER_DIR}/passes/${shader}.bin"
        DEPENDS "${UVSR_RUNTIME_SHADER_SOURCE}"
    )
endforeach()

set(UVSR_APP_SHADERS
    agx_tonemapping_ps
    backdrop_blur_ps
    pixel_zoom_ps
    cmaa2_ComputeDispatchArgsCS
    cmaa2_DeferredColorApply2x2CS
    cmaa2_EdgesColor2x2CS
    cmaa2_ProcessCandidatesCS
    msaa_visibility_resolve_cs
    pbr_deferred_lighting_cs
    pbr_deferred_lighting_msaa_cs
    pbr_forward_ps
    pbr_gbuffer_ps
    screen_space_depth_hierarchy_cs
    screen_space_indirect_composite_cs
    screen_space_visibility_bounce_control_cs
    screen_space_visibility_filter_cs
    screen_space_visibility_filter_packed_edge_cs
    screen_space_visibility_fixed_cs
    screen_space_visibility_fused_apply_cs
    screen_space_visibility_packed_fast_cs
    screen_space_visibility_composed_edges_cs
    screen_space_visibility_composed_packed_fast_cs
    screen_space_visibility_packed_fast_edges_cs
    screen_space_visibility_cs
    screen_space_visibility_temporal_cs
    taa_miniengine_blend_cs
    taa_miniengine_resolve_cs
    taa_miniengine_sharpen_cs
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

# Keep the runtime package synchronized with every shader source named by the
# ShaderMake configuration. Missing entries otherwise compile successfully into
# the staging directory but fail only when the corresponding lazy PSO is first
# selected at runtime.
file(STRINGS "${UVSR_SHADER_CONFIG}"
    UVSR_SHADER_CONFIG_LINES)
foreach(shader_config_line IN LISTS UVSR_SHADER_CONFIG_LINES)
    string(REGEX MATCH "^[^ ]+" shader_source "${shader_config_line}")
    if(shader_source STREQUAL "")
        continue()
    endif()
    get_filename_component(shader_name "${shader_source}" NAME_WE)
    list(FIND UVSR_APP_SHADERS "${shader_name}" shader_package_index)
    if(shader_package_index EQUAL -1)
        string(REGEX MATCH "(^| )-E ([^ ]+)"
            shader_entry_match "${shader_config_line}")
        if(NOT "${shader_entry_match}" STREQUAL "")
            set(shader_entry_name "${shader_name}_${CMAKE_MATCH_2}")
            list(FIND UVSR_APP_SHADERS
                "${shader_entry_name}" shader_package_index)
        endif()
    endif()
    if(shader_package_index EQUAL -1)
        message(FATAL_ERROR
            "Shader '${shader_name}' is compiled by src/shaders.cfg but missing "
            "from UVSR_APP_SHADERS runtime packaging")
    endif()
endforeach()

foreach(shader IN LISTS UVSR_APP_SHADERS)
    set(UVSR_RUNTIME_SHADER_SOURCE
        "${UVSR_SHADER_STAGING_DIR}/dxil/${shader}.bin")
    add_custom_command(
        OUTPUT "${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
        APPEND
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_RUNTIME_SHADER_SOURCE}"
            "${UVSR_RUNTIME_APP_SHADER_DIR}/${shader}.bin"
        DEPENDS "${UVSR_RUNTIME_SHADER_SOURCE}"
    )
endforeach()

add_custom_command(
    OUTPUT "${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
    APPEND
    COMMAND ${CMAKE_COMMAND} -E touch
        "${UVSR_RUNTIME_SHADER_STAGE_STAMP}"
    DEPENDS
        "${UVSR_SHADER_COMPILE_STAMP}"
        "${UVSR_RUNTIME_SHADER_MANIFEST}"
)
add_custom_target(uvsr_stage_runtime_shaders
    DEPENDS "${UVSR_RUNTIME_SHADER_STAGE_STAMP}")
add_dependencies(
    uvsr_stage_runtime_shaders
    donut_shaders
    uvsr_shaders
)
add_dependencies(uvsr uvsr_stage_runtime_shaders)

if (NOT UVSR_AA_DEVELOPER_OVERRIDES)
    add_executable(
        uvsr_production_shader_bundle_tests
        tests/production_shader_bundle_tests.cpp
    )
    add_dependencies(
        uvsr_production_shader_bundle_tests
        uvsr_stage_runtime_shaders
    )
    add_test(
        NAME uvsr_production_shader_bundle_contract
        COMMAND uvsr_production_shader_bundle_tests
            "${UVSR_SHADER_CONFIG}"
            "${UVSR_RUNTIME_SHADER_MANIFEST}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
    )
    set_tests_properties(
        uvsr_production_shader_bundle_contract
        PROPERTIES LABELS "production;shaders"
    )
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
add_dependencies(uvsr uvsr_stage_intel_sponza)

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
