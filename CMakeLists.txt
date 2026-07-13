cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    src/gpu_performance_monitor.cpp
    src/reconstructive_temporal_aa.cpp
    src/reconstructive_temporal_aa_settings.cpp
    src/uvsr.cpp
    src/pbr_deferred_lighting_pass.cpp
    src/screen_space_visibility.cpp
)
target_link_libraries(uvsr PRIVATE donut_render donut_app donut_engine)
add_dependencies(uvsr uvsr_shaders)

enable_testing()
add_executable(uvsr_pbr_tests tests/pbr_reference_tests.cpp)
target_include_directories(uvsr_pbr_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(uvsr_pbr_tests PRIVATE donut_core)
add_test(NAME uvsr_pbr_reference COMMAND uvsr_pbr_tests)

add_executable(uvsr_radial_visibility_tests tests/radial_visibility_mask_tests.cpp)
target_include_directories(uvsr_radial_visibility_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(NAME uvsr_radial_visibility_reference COMMAND uvsr_radial_visibility_tests)

add_executable(uvsr_rtaa_reference_tests
    tests/rtaa_reference_tests.cpp
    src/reconstructive_temporal_aa_settings.cpp
)
target_include_directories(uvsr_rtaa_reference_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(NAME uvsr_rtaa_reference COMMAND uvsr_rtaa_reference_tests)

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
    rtaa_prepare_cs
    rtaa_resolve_cs
    screen_space_depth_hierarchy_cs
    screen_space_indirect_composite_cs
    screen_space_visibility_cs
)

foreach(shader IN LISTS UVSR_APP_SHADERS)
    add_custom_command(TARGET uvsr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UVSR_SHADER_STAGING_DIR}/dxil/${shader}.bin"
            "${UVSR_RUNTIME_APP_SHADER_DIR}/${shader}.bin"
    )
endforeach()

add_custom_command(TARGET uvsr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models/nvidia_bistro"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/scenes/nvidia_bistro/BistroExterior.glb"
        "${CMAKE_SOURCE_DIR}/assets/scenes/nvidia_bistro/BistroInterior_Wine.glb"
        "${CMAKE_SOURCE_DIR}/assets/scenes/nvidia_bistro/LICENSE.txt"
        "${CMAKE_SOURCE_DIR}/assets/scenes/nvidia_bistro/README.txt"
        "${CMAKE_BINARY_DIR}/media/glTF-Sample-Assets/Models/nvidia_bistro"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${CMAKE_BINARY_DIR}/media/luts/kodak"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/media/luts/kodak"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/luts/kodak"
        "${CMAKE_BINARY_DIR}/media/luts/kodak"
)
