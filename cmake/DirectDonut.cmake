set(UVSR_DONUT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/donut")
set(UVSR_DONUT_EXPECTED_HEAD
    "bc1ea24b0486f1c00d89327fe16c0b4dd11c5937")
set(UVSR_NVRHI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvrhi")
set(UVSR_NVRHI_EXPECTED_HEAD
    "8e8c36e37558acec333204619b95d9d2fcdc4a79")

include("${CMAKE_CURRENT_LIST_DIR}/DirectThirdParty.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DirectImGui.cmake")

function(uvsr_require_pinned_dependency name source_dir expected_commit)
    if (NOT EXISTS "${source_dir}/.git")
        message(FATAL_ERROR
            "${name} is missing. Initialize the pinned ${source_dir} submodule.")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse HEAD
        RESULT_VARIABLE revision_result
        OUTPUT_VARIABLE revision
        ERROR_VARIABLE revision_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT revision_result EQUAL 0 OR
        NOT revision STREQUAL expected_commit)
        message(FATAL_ERROR
            "${name} must be exactly ${expected_commit}; found '${revision}'. "
            "${revision_error}")
    endif()
endfunction()

uvsr_require_pinned_dependency(
    "Donut"
    "${UVSR_DONUT_SOURCE_DIR}"
    "${UVSR_DONUT_EXPECTED_HEAD}")
uvsr_require_pinned_dependency(
    "NVRHI"
    "${UVSR_NVRHI_SOURCE_DIR}"
    "${UVSR_NVRHI_EXPECTED_HEAD}")

add_library(imgui STATIC
    "${UVSR_IMGUI_SOURCE_DIR}/imconfig.h"
    "${UVSR_IMGUI_SOURCE_DIR}/imstb_rectpack.h"
    "${UVSR_IMGUI_SOURCE_DIR}/imstb_textedit.h"
    "${UVSR_IMGUI_SOURCE_DIR}/imstb_truetype.h"
    "${UVSR_IMGUI_OVERRIDE_DIR}/imgui.cpp"
    "${UVSR_IMGUI_OVERRIDE_DIR}/imgui.h"
    "${UVSR_IMGUI_OVERRIDE_DIR}/imgui_draw.cpp"
    "${UVSR_IMGUI_OVERRIDE_DIR}/imgui_internal.h"
    "${UVSR_IMGUI_OVERRIDE_DIR}/imgui_tables.cpp"
    "${UVSR_IMGUI_OVERRIDE_DIR}/imgui_widgets.cpp")
target_include_directories(imgui PUBLIC
    "${UVSR_IMGUI_OVERRIDE_DIR}"
    "${UVSR_IMGUI_SOURCE_DIR}")
set_property(TARGET imgui PROPERTY
    UVSR_PINNED_SOURCE "${UVSR_IMGUI_SOURCE_DIR}")

set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ${BUILD_TESTING} CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_VULKAN OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_NVAPI OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_RTXMU OFF CACHE BOOL "" FORCE)
set(UVSR_DIRECTX_HEADERS_COMMIT
    "ee479f0bd5f7b884f202bcf0c3f076cc050dd256")
set(UVSR_DIRECTX_HEADERS_EXPECTED_HEAD
    "${UVSR_DIRECTX_HEADERS_COMMIT}")
set(NVRHI_DIRECTX_HEADERS_GIT_TAG
    "${UVSR_DIRECTX_HEADERS_COMMIT}" CACHE STRING "" FORCE)
add_subdirectory(
    "${UVSR_NVRHI_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/nvrhi"
    EXCLUDE_FROM_ALL)
if (NOT NVRHI_DIRECTX_HEADERS_GIT_TAG STREQUAL
        UVSR_DIRECTX_HEADERS_COMMIT)
    message(FATAL_ERROR "The effective DirectX-Headers revision changed")
endif()
get_target_property(
    UVSR_DIRECTX_HEADERS_SOURCE_DIR DirectX-Headers SOURCE_DIR)
if (NOT EXISTS "${UVSR_DIRECTX_HEADERS_SOURCE_DIR}/LICENSE")
    message(FATAL_ERROR "The pinned DirectX-Headers license is unavailable")
endif()
if (NOT EXISTS "${UVSR_DIRECTX_HEADERS_SOURCE_DIR}/.git")
    message(FATAL_ERROR
        "The materialized DirectX-Headers source is not a Git worktree")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${UVSR_DIRECTX_HEADERS_SOURCE_DIR}"
        rev-parse HEAD
    RESULT_VARIABLE directx_headers_revision_result
    OUTPUT_VARIABLE directx_headers_revision
    ERROR_VARIABLE directx_headers_revision_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT directx_headers_revision_result EQUAL 0 OR
    NOT directx_headers_revision STREQUAL UVSR_DIRECTX_HEADERS_COMMIT)
    message(FATAL_ERROR
        "DirectX-Headers must materialize exact commit "
        "${UVSR_DIRECTX_HEADERS_COMMIT}; found '${directx_headers_revision}'. "
        "${directx_headers_revision_error}")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${UVSR_DIRECTX_HEADERS_SOURCE_DIR}"
        status --porcelain=v1 --untracked-files=all
    RESULT_VARIABLE directx_headers_status_result
    OUTPUT_VARIABLE directx_headers_status
    ERROR_VARIABLE directx_headers_status_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT directx_headers_status_result EQUAL 0 OR directx_headers_status)
    message(FATAL_ERROR
        "The materialized DirectX-Headers worktree must be clean: "
        "${directx_headers_status}${directx_headers_status_error}")
endif()
get_target_property(nvrhi_source_directory nvrhi SOURCE_DIR)
cmake_path(NORMAL_PATH nvrhi_source_directory)
cmake_path(NORMAL_PATH UVSR_NVRHI_SOURCE_DIR)
if (NOT nvrhi_source_directory STREQUAL UVSR_NVRHI_SOURCE_DIR)
    message(FATAL_ERROR
        "The active NVRHI target is not built from the direct pinned submodule")
endif()

file(GLOB donut_core_sources CONFIGURE_DEPENDS
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/core/chunk/*.h"
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/core/math/*.h"
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/core/vfs/TarFile.h"
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/core/vfs/VFS.h"
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/core/*.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/core/chunk/*.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/src/core/math/*.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/src/core/vfs/TarFile.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/src/core/vfs/VFS.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/src/core/*.cpp")
if (WIN32)
    list(APPEND donut_core_sources
        "${UVSR_DONUT_SOURCE_DIR}/include/donut/core/vfs/WinResFS.h"
        "${UVSR_DONUT_SOURCE_DIR}/src/core/vfs/WinResFS.cpp")
endif()
add_library(donut_core STATIC EXCLUDE_FROM_ALL ${donut_core_sources})
target_include_directories(donut_core PUBLIC
    "${UVSR_DONUT_SOURCE_DIR}/include")
target_link_libraries(donut_core PUBLIC jsoncpp_static)
target_compile_definitions(donut_core PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)

file(GLOB donut_engine_sources CONFIGURE_DEPENDS
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/engine/*.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/engine/*.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/src/engine/*.c"
    "${UVSR_DONUT_SOURCE_DIR}/src/engine/*.h")
list(FILTER donut_engine_sources EXCLUDE REGEX
    "[/\\\\]stb_impl\\.c$")
add_library(donut_engine STATIC EXCLUDE_FROM_ALL ${donut_engine_sources})
target_include_directories(donut_engine PUBLIC
    "${UVSR_DONUT_SOURCE_DIR}/include")
target_link_libraries(donut_engine PUBLIC
    donut_core nvrhi jsoncpp_static stb cgltf uvsr_shader_blob tinyexr)
target_compile_definitions(donut_engine PUBLIC
    NOMINMAX
    DONUT_WITH_TINYEXR
    DONUT_WITH_DX11=0
    DONUT_WITH_DX12=1
    DONUT_WITH_VULKAN=0
    DONUT_WITH_STATIC_SHADERS=0
    DONUT_WITH_AFTERMATH=0)

file(GLOB donut_render_sources CONFIGURE_DEPENDS
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/render/*.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/render/*.cpp")
list(FILTER donut_render_sources EXCLUDE REGEX
    "[/\\\\](PixelReadbackPass|LightProbeProcessingPass)\\.(cpp|h)$")
add_library(donut_render STATIC EXCLUDE_FROM_ALL ${donut_render_sources})
target_include_directories(donut_render PUBLIC
    "${UVSR_DONUT_SOURCE_DIR}/include")
target_link_libraries(donut_render PUBLIC donut_core donut_engine)
target_compile_definitions(donut_render PUBLIC DONUT_WITH_DLSS=0)

file(GLOB donut_app_sources CONFIGURE_DEPENDS
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/app/*.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/app/*.cpp")
list(APPEND donut_app_sources
    "${UVSR_DONUT_SOURCE_DIR}/src/app/dx12/DeviceManager_DX12.cpp")
add_library(donut_app STATIC EXCLUDE_FROM_ALL ${donut_app_sources})
target_include_directories(donut_app PUBLIC
    "${UVSR_DONUT_SOURCE_DIR}/include")
target_link_libraries(donut_app PUBLIC
    donut_core donut_engine glfw imgui nvrhi_d3d12 d3d12 dxgi dxguid nvrhi)
target_compile_definitions(donut_app PUBLIC
    DONUT_WITH_AFTERMATH=0
    DONUT_WITH_STREAMLINE=0)
target_compile_definitions(donut_app PRIVATE DONUT_FORCE_DISCRETE_GPU=0)

foreach(target donut_core donut_engine donut_render donut_app imgui)
    set_target_properties("${target}" PROPERTIES FOLDER "Donut")
endforeach()
