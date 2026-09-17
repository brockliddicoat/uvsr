include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/VerifyDirectDependencyState.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/PatchedSources.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NoCppExceptions.cmake")

set(UVSR_NVRHI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/nvrhi")
set(UVSR_NVRHI_EXPECTED_HEAD
    "8e8c36e37558acec333204619b95d9d2fcdc4a79")

cmake_path(NORMAL_PATH UVSR_NVRHI_SOURCE_DIR)
uvsr_verify_dependency_git("${UVSR_NVRHI_SOURCE_DIR}" "${UVSR_NVRHI_EXPECTED_HEAD}")

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
uvsr_verify_dependency_git("${UVSR_DIRECTX_HEADERS_SOURCE_DIR}" "${UVSR_DIRECTX_HEADERS_COMMIT}")
get_target_property(nvrhi_source_directory nvrhi SOURCE_DIR)
cmake_path(NORMAL_PATH nvrhi_source_directory)
cmake_path(NORMAL_PATH UVSR_NVRHI_SOURCE_DIR)
if (NOT nvrhi_source_directory STREQUAL UVSR_NVRHI_SOURCE_DIR)
    message(FATAL_ERROR
        "The active NVRHI target is not built from the direct pinned submodule")
endif()

set(UVSR_NVRHI_D3D12_OVERRIDE_DIR
    "${CMAKE_BINARY_DIR}/uvsr_nvrhi_d3d12_overrides")
set(UVSR_NVRHI_D3D12_PATCH_FILES
    "${CMAKE_CURRENT_LIST_DIR}/../overrides/nvrhi-stable-directx-headers.patch"
    "${CMAKE_CURRENT_LIST_DIR}/../overrides/nvrhi-d3d12-portability.patch"
    "${CMAKE_CURRENT_LIST_DIR}/../overrides/nvrhi-d3d12-binding-failure.patch")
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
    "${UVSR_NVRHI_SOURCE_DIR}"
    "${UVSR_NVRHI_D3D12_PATCH_FILES}"
    "${UVSR_NVRHI_D3D12_OVERRIDE_DIR}"
    src/d3d12/d3d12-backend.h
    ${UVSR_NVRHI_D3D12_SOURCES})
set(UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR
    "${CMAKE_BINARY_DIR}/uvsr_nvrhi_d3d12_diagnostics")
file(MAKE_DIRECTORY "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/../overrides/nvrhi-d3d12-diagnostics.h"
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}/uvsr-d3d12-diagnostics.h"
    COPYONLY)
set(UVSR_NVRHI_DIAGNOSTICS_STAGE_RECORDS
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}/uvsr-d3d12-diagnostics.h|1024BEBDC4AF86844547ED3A2DE37452F16430F491F6F4476E725F04506ED2A0")
set(UVSR_NVRHI_STAGE_TREE_DIGEST
    "819e01a2813fc9ee3cd582c801e02c11f4da22363b13bd1fc68334ae560bae71")
uvsr_replace_target_sources(
    nvrhi_d3d12
    "${UVSR_NVRHI_SOURCE_DIR}"
    "${UVSR_NVRHI_D3D12_OVERRIDE_DIR}"
    ${UVSR_NVRHI_D3D12_SOURCES})
target_include_directories(nvrhi_d3d12 PRIVATE
    "${UVSR_NVRHI_SOURCE_DIR}/src/d3d12"
    "${UVSR_NVRHI_D3D12_DIAGNOSTICS_DIR}")

foreach(target IN ITEMS nvrhi nvrhi_d3d12 DirectX-Headers DirectX-Guids)
    uvsr_disable_cpp_exceptions(${target})
    set_target_properties(${target} PROPERTIES FOLDER "Direct Dependencies")
endforeach()
foreach(target IN ITEMS nvrhi nvrhi_d3d12)
    set_property(TARGET ${target} PROPERTY UVSR_PINNED_SOURCE "${UVSR_NVRHI_SOURCE_DIR}")
endforeach()
