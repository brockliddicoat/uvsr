cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UVSR_BUILD_APPLICATION
    "Build the UVSR application that integrates every enabled producer" ON)
option(UVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS
    "Build the standalone screen-space directional-shadow component" ON)

if (UVSR_BUILD_APPLICATION AND
    NOT UVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS)
    message(FATAL_ERROR
        "UVSR requires the screen-space directional-shadow component. Set "
        "UVSR_BUILD_APPLICATION=OFF to build the component independently.")
endif()

if (UVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS)
    set(UVSR_SCREEN_SPACE_SHADOW_UPSTREAM_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bend_sss/upstream")
    file(SHA256
        "${UVSR_SCREEN_SPACE_SHADOW_UPSTREAM_DIR}/bend_sss_cpu.h"
        UVSR_SCREEN_SPACE_SHADOW_CPU_SHA256)
    file(SHA256
        "${UVSR_SCREEN_SPACE_SHADOW_UPSTREAM_DIR}/bend_sss_gpu.h"
        UVSR_SCREEN_SPACE_SHADOW_GPU_SHA256)
    file(SHA256
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/Apache-2.0.txt"
        UVSR_APACHE_2_LICENSE_SHA256)
    if (NOT UVSR_SCREEN_SPACE_SHADOW_CPU_SHA256 STREQUAL
            "23aae596dbb1b9bdae23d87ac85079b138426823c3153079f7a7dd36f603d02a")
        message(FATAL_ERROR
            "The vendored directional-shadow CPU planner changed; restore the released bytes.")
    endif()
    if (NOT UVSR_SCREEN_SPACE_SHADOW_GPU_SHA256 STREQUAL
            "7fbe24bd2040a62536c31df6cd38a92cb22c172a9a419eed3c0af7df1d50a68c")
        message(FATAL_ERROR
            "The vendored directional-shadow GPU tracer changed; restore the released bytes.")
    endif()
    if (NOT UVSR_APACHE_2_LICENSE_SHA256 STREQUAL
            "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30")
        message(FATAL_ERROR
            "The packaged Apache-2.0 license text changed; restore the complete license.")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${UVSR_SCREEN_SPACE_SHADOW_UPSTREAM_DIR}/bend_sss_cpu.h"
        "${UVSR_SCREEN_SPACE_SHADOW_UPSTREAM_DIR}/bend_sss_gpu.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/Apache-2.0.txt")
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
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/donut-loading-app.patch")
uvsr_stage_patched_sources(
    "${CMAKE_CURRENT_SOURCE_DIR}/donut"
    "${UVSR_DONUT_APP_PATCH_FILES}"
    "${UVSR_DONUT_APP_OVERRIDE_DIR}"
    include/donut/app/ApplicationBase.h
    include/donut/app/DeviceManager.h
    include/donut/app/UserInterfaceUtils.h
    src/app/ApplicationBase.cpp
    src/app/DeviceManager.cpp
    src/app/UserInterfaceUtils.cpp)

set(UVSR_IMGUI_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_imgui_overrides")
set(UVSR_IMGUI_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-ui.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-dropdown-roll.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/imgui-runtime-policy.patch")
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
    src/app/ApplicationBase.cpp
    src/app/DeviceManager.cpp
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

set(UVSR_SHADER_STAGING_ROOT "${CMAKE_BINARY_DIR}/uvsr_shaders")
set(UVSR_CORE_SHADER_STAGING_DIR
    "${UVSR_SHADER_STAGING_ROOT}/core")
set(UVSR_SCREEN_SPACE_SHADOW_SHADER_STAGING_DIR
    "${UVSR_SHADER_STAGING_ROOT}/screen_space_directional_shadow")
include(donut/compileshaders.cmake)
set(UVSR_SHADER_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.cfg")
set(UVSR_SCREEN_SPACE_SHADOW_SHADER_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/src/screen_space_directional_shadows_shaders.cfg")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${UVSR_SHADER_CONFIG}"
    "${UVSR_SCREEN_SPACE_SHADOW_SHADER_CONFIG}")

file(GLOB_RECURSE UVSR_SHADER_INPUTS
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bend_sss/upstream/bend_sss_gpu.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/shaders/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/donut/include/donut/shaders/*"
)
list(SORT UVSR_SHADER_INPUTS)

set(UVSR_SHADER_CONFIGURATION_STAMP
    "${CMAKE_CURRENT_BINARY_DIR}/uvsr_shader_configuration.txt")
file(GENERATE
    OUTPUT "${UVSR_SHADER_CONFIGURATION_STAMP}"
    CONTENT "${UVSR_SHADER_CONFIG}\n"
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
            -D TARGET_D3D12
            --compiler "${SHADERMAKE_DXC_PATH}"
            --shaderModel 6_5
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

if (UVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS)
    uvsr_add_shader_bundle(
        uvsr_screen_space_directional_shadow_shaders
        "UVSR Screen-Space Directional Shadows"
        "${UVSR_SCREEN_SPACE_SHADOW_SHADER_CONFIG}"
        "${UVSR_SCREEN_SPACE_SHADOW_SHADER_STAGING_DIR}"
        UVSR_SCREEN_SPACE_SHADOW_SHADER_COMPILE_STAMP)
endif()

if (UVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS)
    add_library(uvsr_screen_space_directional_shadows STATIC
        src/screen_space_directional_shadows.cpp
    )
    target_include_directories(uvsr_screen_space_directional_shadows
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
        PRIVATE "${UVSR_SCREEN_SPACE_SHADOW_UPSTREAM_DIR}")
    target_link_libraries(uvsr_screen_space_directional_shadows
        PUBLIC nvrhi
        PRIVATE donut_render donut_app donut_engine)
    add_dependencies(uvsr_screen_space_directional_shadows
        uvsr_screen_space_directional_shadow_shaders)
endif()

if (UVSR_BUILD_APPLICATION)
    add_executable(uvsr WIN32
        src/camera_collision.cpp
        src/cmaa2.cpp
        src/fast_approximate_aa.cpp
        src/gpu_performance_monitor.cpp
        src/image_based_lighting_background_pass.cpp
        src/image_based_lighting_environment.cpp
        src/msaa_visibility_resolve.cpp
        src/pbr_deferred_lighting_pass.cpp
        src/scene_catalog.cpp
        src/scene_loading.h
        src/screen_space_visibility.cpp
        src/sponza_camera_preset.cpp
        src/temporal_aa.cpp
        src/temporal_aa_core.cpp
        src/uvsr.cpp
        src/visibility_blue_noise.cpp
    )
    target_include_directories(uvsr BEFORE PRIVATE
        "${UVSR_IMGUI_OVERRIDE_DIR}"
        "${UVSR_DONUT_APP_OVERRIDE_DIR}/include"
        "${UVSR_DONUT_ENGINE_OVERRIDE_DIR}/include")
    target_link_libraries(uvsr PRIVATE
        donut_render
        donut_app
        donut_engine
        dwmapi
        uvsr_screen_space_directional_shadows)
    target_compile_definitions(uvsr PRIVATE
        "UVSR_GIT_COMMIT=\"${UVSR_GIT_COMMIT}\"")
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
    add_dependencies(uvsr
        uvsr_shaders
        uvsr_stage_environments)
endif()

enable_testing()
if (WIN32)
    set(UVSR_README_LINE_COUNT_COMMAND
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/update_readme_line_counts.cmd"
    )
else()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
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

add_executable(uvsr_flashlight_tests tests/flashlight_tests.cpp)
target_include_directories(
    uvsr_flashlight_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(
    NAME uvsr_flashlight_reference
    COMMAND uvsr_flashlight_tests)

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

if (UVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS)
    add_executable(uvsr_screen_space_directional_shadow_tests
        tests/screen_space_directional_shadows_tests.cpp
    )
    target_link_libraries(uvsr_screen_space_directional_shadow_tests
        PRIVATE uvsr_screen_space_directional_shadows)
    target_include_directories(uvsr_screen_space_directional_shadow_tests
        PRIVATE "${UVSR_SCREEN_SPACE_SHADOW_UPSTREAM_DIR}")
    add_test(
        NAME uvsr_screen_space_directional_shadow_reference
        COMMAND uvsr_screen_space_directional_shadow_tests
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

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
    display_output_ps
    fast_approximate_aa_ps
    backdrop_blur_ps
    screen_space_directional_shadows_cs
    screen_space_directional_shadows_debug_ps
    cmaa2_ComputeDispatchArgsCS
    cmaa2_DeferredColorApply2x2CS
    cmaa2_EdgesColor2x2CS
    cmaa2_ProcessCandidatesCS
    image_based_lighting_background_ps
    msaa_visibility_resolve_cs
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

set(UVSR_SCREEN_SPACE_SHADOW_APP_SHADERS
    screen_space_directional_shadows_cs
    screen_space_directional_shadows_debug_ps
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
    uvsr_require_shader_config_packaged(
        "${UVSR_SCREEN_SPACE_SHADOW_SHADER_CONFIG}"
        UVSR_APP_SHADERS
        UVSR_SCREEN_SPACE_SHADOW_APP_SHADERS)

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
        if ("${shader}" IN_LIST UVSR_SCREEN_SPACE_SHADOW_APP_SHADERS)
            set(shader_staging_dir "${UVSR_SCREEN_SPACE_SHADOW_SHADER_STAGING_DIR}")
        endif()
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
    list(APPEND UVSR_RUNTIME_SHADER_TARGET_DEPENDENCIES
        uvsr_screen_space_directional_shadow_shaders)
    list(APPEND UVSR_RUNTIME_SHADER_FILE_DEPENDENCIES
        "${UVSR_SCREEN_SPACE_SHADOW_SHADER_COMPILE_STAMP}"
        "${UVSR_SCREEN_SPACE_SHADOW_SHADER_CONFIG}")
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
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/geist/Geist-Medium.ttf")
    set(UVSR_UI_HEADER_FONT_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/geist/Geist-Medium.ttf")
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
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/System/CodexUI.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_UI_HEADER_FONT_SOURCE}"
            "${CMAKE_BINARY_DIR}/media/fonts/System/CodexUI-Semibold.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/microsoft_directx_graphics_samples/LICENSE.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Microsoft-DirectX-Graphics-Samples.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/Apache-2.0.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Apache-2.0.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/BSD-2-Clause.txt"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/BSD-2-Clause.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/google_filament_fxaa/ATTRIBUTION.md"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses/Google-Filament-FXAA-Attribution.md"
    )
endif()
