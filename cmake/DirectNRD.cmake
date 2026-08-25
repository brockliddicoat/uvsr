FetchContent_Declare(nrd
    URL
        "https://github.com/NVIDIA-RTX/NRD/archive/792eff196afdd350fd9c3f862119017ccb438a0e.zip"
    URL_HASH
        "SHA256=AD148D3653E7E4A149AF0D1608EC662EEB522144CF34F6A29F9DFD333933BAA8"
    SOURCE_SUBDIR uvsr-no-upstream-build
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(nrd)
if (NOT nrd_POPULATED)
    # Populate only: no file from the fetched tree may execute before the
    # always-run source/stage verifier has accepted it.
    cmake_policy(PUSH)
    if (POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(nrd)
    cmake_policy(POP)
endif()

FetchContent_Declare(mathlib
    URL
        "https://github.com/NVIDIA-RTX/MathLib/archive/974e1387ba936740c7cdc494792d2641bc127e86.zip"
    URL_HASH
        "SHA256=8250A1A903CB9D69234029A226349ABF05D23A61A6CB2F1CAF8FDED8E5BCDEA5"
    SOURCE_SUBDIR uvsr-no-upstream-build
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(mathlib)
if (NOT mathlib_POPULATED)
    # Populate only: no file from the fetched tree may execute before the
    # always-run tree-digest verifier has accepted it.
    cmake_policy(PUSH)
    if (POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(mathlib)
    cmake_policy(POP)
endif()
set(UVSR_MATHLIB_FILE_COUNT 20)
set(UVSR_MATHLIB_TREE_DIGEST
    "d40d2a2b0454720a5ab9cac4a4b5842df71a3d6ee2bbcae1ec082821ed409588")
add_library(MathLib INTERFACE)
target_include_directories(MathLib SYSTEM INTERFACE
    "${mathlib_SOURCE_DIR}")

file(READ "${nrd_SOURCE_DIR}/Include/NRD.h" nrd_version_header)
if (NOT nrd_version_header MATCHES "NRD_VERSION_MAJOR 4" OR
    NOT nrd_version_header MATCHES "NRD_VERSION_MINOR 17" OR
    NOT nrd_version_header MATCHES "NRD_VERSION_BUILD 3")
    message(FATAL_ERROR "The pinned NRD source is not version 4.17.3")
endif()

set(UVSR_NRD_STAGING_DIRECTORY "${CMAKE_BINARY_DIR}/uvsr_nrd_direct")
set(UVSR_NRD_STAGE_TREE_DIGEST
    "ec6039e1235823527158f87f831d78b31e9001506a992347fc97e3d67359952a")
set(UVSR_NRD_RAW_FILE_RECORDS
    "${nrd_SOURCE_DIR}/Include/NRD.h|cd0f0ae6c119ed28b5e6612000622350d8c4a8bd094df136f498d27002d82906"
    "${nrd_SOURCE_DIR}/Include/NRDDescs.h|4313c46f10b359da0d439499a39b232b998596efa30e8a17639b8a3ea0247410"
    "${nrd_SOURCE_DIR}/Include/NRDSettings.h|683c214df52fd9bbc9a51e093151df0702d2c79a45a3b87a0a181beac120b410"
    "${nrd_SOURCE_DIR}/Resources/NRD.rc|73fb62e96a40cb738847c95becb059f00aa815f2266decebd091674c1ed6514a"
    "${nrd_SOURCE_DIR}/Resources/Version.h|07a4ed11d25fba18a98ea8ed2af745baba23a05e26cfa44a7808ad70a5ba48ca"
    "${nrd_SOURCE_DIR}/LICENSE.txt|0f74d24da3082b666de48eea9ec7ff835657575a71576281e383901f3d7c08c2")
set(UVSR_NRD_SOURCE_PATHS
    Source/InstanceImpl.cpp
    Source/InstanceImpl.h
    Source/Reblur.cpp
    Source/Relax.cpp
    Source/Sigma.cpp
    Source/StdAllocator.h
    Source/Timer.cpp
    Source/Timer.h
    Source/Wrapper.cpp)
set(UVSR_NRD_DENOISER_PATHS
    Source/Denoisers/Reblur_Diffuse.hpp
    Source/Denoisers/Relax_Diffuse.hpp
    Source/Denoisers/Sigma_Shadow.hpp)
set(UVSR_NRD_DIRECT_OVERRIDE
    "${CMAKE_CURRENT_SOURCE_DIR}/overrides/nrd-direct-shader-blob.patch")
file(SHA256 "${UVSR_NRD_DIRECT_OVERRIDE}" nrd_override_sha256)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NRDConfig.hlsli.in"
    nrd_config_sha256)
string(CONCAT nrd_staging_identity
    "uvsr-direct-nrd-retained-v1\n"
    "792eff196afdd350fd9c3f862119017ccb438a0e\n"
    "${nrd_override_sha256}\n${nrd_config_sha256}\n")
set(nrd_staging_marker
    "${UVSR_NRD_STAGING_DIRECTORY}/uvsr-direct-nrd.identity")
set(stage_nrd true)
if (EXISTS "${nrd_staging_marker}" AND
    EXISTS "${UVSR_NRD_STAGING_DIRECTORY}/Source/InstanceImpl.h" AND
    EXISTS "${UVSR_NRD_STAGING_DIRECTORY}/Shaders/Shaders.cfg")
    file(READ "${nrd_staging_marker}" staged_nrd_identity)
    if (staged_nrd_identity STREQUAL nrd_staging_identity)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --reverse --check
                --unidiff-zero
                "${UVSR_NRD_DIRECT_OVERRIDE}"
            WORKING_DIRECTORY "${UVSR_NRD_STAGING_DIRECTORY}"
            RESULT_VARIABLE staged_nrd_patch_result
            ERROR_QUIET)
        if (staged_nrd_patch_result EQUAL 0)
            set(stage_nrd false)
        endif()
    endif()
endif()
if (stage_nrd)
    file(GLOB_RECURSE nrd_shader_stage_inputs
        LIST_DIRECTORIES false
        RELATIVE "${nrd_SOURCE_DIR}"
        "${nrd_SOURCE_DIR}/Shaders/*")
    set(nrd_stage_inputs
        ${UVSR_NRD_SOURCE_PATHS}
        ${UVSR_NRD_DENOISER_PATHS}
        ${nrd_shader_stage_inputs})
    file(GLOB_RECURSE staged_nrd_source_files
        LIST_DIRECTORIES false
        RELATIVE "${UVSR_NRD_STAGING_DIRECTORY}"
        "${UVSR_NRD_STAGING_DIRECTORY}/Source/*")
    set(retained_nrd_sources
        ${UVSR_NRD_SOURCE_PATHS} ${UVSR_NRD_DENOISER_PATHS})
    foreach(relative_path IN LISTS staged_nrd_source_files)
        list(FIND retained_nrd_sources "${relative_path}" retained_index)
        if (retained_index EQUAL -1)
            file(REMOVE "${UVSR_NRD_STAGING_DIRECTORY}/${relative_path}")
        endif()
    endforeach()
    foreach(relative_path IN LISTS nrd_stage_inputs)
        get_filename_component(relative_directory
            "${relative_path}" DIRECTORY)
        file(MAKE_DIRECTORY
            "${UVSR_NRD_STAGING_DIRECTORY}/${relative_directory}")
        configure_file(
            "${nrd_SOURCE_DIR}/${relative_path}"
            "${UVSR_NRD_STAGING_DIRECTORY}/${relative_path}"
            COPYONLY)
    endforeach()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
            "${GIT_EXECUTABLE}" apply
            --no-index
            --unidiff-zero
            --whitespace=nowarn
            "${UVSR_NRD_DIRECT_OVERRIDE}"
        WORKING_DIRECTORY "${UVSR_NRD_STAGING_DIRECTORY}"
        RESULT_VARIABLE nrd_patch_result
        ERROR_VARIABLE nrd_patch_error)
    if (NOT nrd_patch_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to stage direct NRD integration: ${nrd_patch_error}")
    endif()
    file(WRITE "${nrd_staging_marker}" "${nrd_staging_identity}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${UVSR_NRD_DIRECT_OVERRIDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NRDConfig.hlsli.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NRDRetainedShaders.cfg")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NRDConfig.hlsli.in"
    "${UVSR_NRD_STAGING_DIRECTORY}/Shaders/NRDConfig.hlsli"
    COPYONLY)

set(UVSR_NRD_SHADER_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/nrd_shaders")
set(UVSR_NRD_DXIL_HEADER_COUNT 29)
set(UVSR_NRD_DXIL_HEADER_TREE_DIGEST
    "53562706da64c883aec7ed1aafcd23d9a1e8a7d9c63b41006dfa532e0b0e52ba")
uvsr_add_direct_shader_bundle(
    TARGET nrd_shaders
    CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NRDRetainedShaders.cfg"
    SOURCE_DIRECTORY "${UVSR_NRD_STAGING_DIRECTORY}/Shaders"
    OUTPUT_DIRECTORY "${UVSR_NRD_SHADER_OUTPUT_DIRECTORY}"
    OUTPUT_FORMAT HEADER
    PRESERVE_SOURCE_STAGE
    EXPECTED_TASKS 34
    OUTPUTS_VARIABLE UVSR_NRD_SHADER_OUTPUTS
    INCLUDE_DIRECTORIES "${mathlib_SOURCE_DIR}"
    DEFINES NRD_INTERNAL
    COMPILER_OPTIONS -Qstrip_reflect -WX -all_resources_bound)

set(UVSR_NRD_SOURCES ${UVSR_NRD_SOURCE_PATHS})
set(UVSR_NRD_DENOISERS ${UVSR_NRD_DENOISER_PATHS})
list(TRANSFORM UVSR_NRD_SOURCES PREPEND
    "${UVSR_NRD_STAGING_DIRECTORY}/")
list(TRANSFORM UVSR_NRD_DENOISERS PREPEND
    "${UVSR_NRD_STAGING_DIRECTORY}/")

add_library(NRD STATIC
    ${UVSR_NRD_SOURCES}
    ${UVSR_NRD_DENOISERS}
    "${nrd_SOURCE_DIR}/Resources/NRD.rc"
    "${nrd_SOURCE_DIR}/Resources/Version.h"
    "${nrd_SOURCE_DIR}/Include/NRD.h"
    "${nrd_SOURCE_DIR}/Include/NRDDescs.h"
    "${nrd_SOURCE_DIR}/Include/NRDSettings.h")
add_dependencies(NRD nrd_shaders)
target_link_libraries(NRD PRIVATE MathLib uvsr_shader_blob)
target_include_directories(NRD
    PUBLIC "${nrd_SOURCE_DIR}/Include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${UVSR_NRD_SHADER_OUTPUT_DIRECTORY}/dxil")
target_compile_definitions(NRD
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        NRD_EMBEDS_SPIRV_SHADERS=0
        NRD_EMBEDS_DXIL_SHADERS=1
        NRD_EMBEDS_DXBC_SHADERS=0
        SPIRV_SREG_OFFSET=0
        SPIRV_BREG_OFFSET=2
        SPIRV_UREG_OFFSET=3
        SPIRV_TREG_OFFSET=20
        NRD_SUPPORTS_VIEWPORT_OFFSET=0
        NRD_SUPPORTS_CHECKERBOARD=0
        NRD_SUPPORTS_HISTORY_CONFIDENCE=0
        NRD_SUPPORTS_DISOCCLUSION_THRESHOLD_MIX=0
        NRD_SUPPORTS_ANTIFIREFLY=1
        REBLUR_PERFORMANCE_MODE=0
    PUBLIC NRD_STATIC_LIBRARY=1)
target_compile_features(NRD PUBLIC cxx_std_17)
if (MSVC)
    target_compile_options(NRD PRIVATE /W4 /WX /wd4324)
endif()
set_target_properties(NRD PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
