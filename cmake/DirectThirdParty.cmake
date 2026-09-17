include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/VerifyDirectDependencyState.cmake")
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/NoCppExceptions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/PatchedSources.cmake")

FetchContent_Declare(uvsr_cgltf
    URL "https://github.com/jkuhlmann/cgltf/archive/fa3b80fa762790192c9532b63c441627416ff300.zip"
    URL_HASH "SHA256=89351D82A140337AC876E018B091F26176FCC8C227479796993CE79BE33ED8A3"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR "uvsr-no-upstream-cmake")
FetchContent_MakeAvailable(uvsr_cgltf)
set(UVSR_CGLTF_FILE_COUNT 19)
set(UVSR_CGLTF_TREE_DIGEST
    "d949633afa4a966b4550497bfab9025bf8f5900ad275d4dde87840831b1bdffc")
uvsr_verify_dependency_file(
    "${uvsr_cgltf_SOURCE_DIR}/LICENSE"
    "F619925F80EF862497AAF8E8155EF218FA6A2190055129523CA3DF9119A9BA95" 1066)
add_library(cgltf INTERFACE)
target_include_directories(cgltf SYSTEM INTERFACE
    "${uvsr_cgltf_SOURCE_DIR}")
set_property(TARGET cgltf PROPERTY UVSR_PINNED_SOURCE
    "${uvsr_cgltf_SOURCE_DIR}")
set(UVSR_CGLTF_LICENSE_SOURCE "${uvsr_cgltf_SOURCE_DIR}/LICENSE")

FetchContent_Declare(uvsr_stb
    URL "https://github.com/nothings/stb/archive/2e2bef463a5b53ddf8bb788e25da6b8506314c08.zip"
    URL_HASH "SHA256=F6A4669309A29DD8634C3C2C7A955DA72469C2DC61471F68D9C499E517AB823F"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR "uvsr-no-upstream-cmake")
FetchContent_MakeAvailable(uvsr_stb)
set(UVSR_STB_FILE_COUNT 428)
set(UVSR_STB_TREE_DIGEST
    "2a1b67948c3c20bdc379e35be58ecfcf99f2518b5390afb4476bf7b58d0abfb4")
uvsr_verify_dependency_file(
    "${uvsr_stb_SOURCE_DIR}/LICENSE"
    "BEBFE904B14301657E4E5D655C811D51FD31B97C455B9CC2D8600D6BAC6CFF63" 2510)
set(UVSR_STB_OVERRIDE_DIR "${CMAKE_BINARY_DIR}/uvsr_stb_overrides")
set(UVSR_STB_PATCH_FILES "${CMAKE_CURRENT_LIST_DIR}/../overrides/stb-gif-iteration.patch")
uvsr_stage_patched_sources("${uvsr_stb_SOURCE_DIR}" "${UVSR_STB_PATCH_FILES}"
    "${UVSR_STB_OVERRIDE_DIR}" stb_image.h)
set(UVSR_STB_STAGE_TREE_DIGEST
    "0a37cbe6f761813a9f6fbe7d228bfd5bc4c050ffc03e125f9debd7f815e28948")
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${UVSR_STB_OVERRIDE_DIR}" "${uvsr_stb_SOURCE_DIR}")
set_property(TARGET stb PROPERTY UVSR_PINNED_SOURCE "${uvsr_stb_SOURCE_DIR}")
set(UVSR_STB_LICENSE_SOURCE "${uvsr_stb_SOURCE_DIR}/LICENSE")

FetchContent_Declare(uvsr_tinyexr
    URL "https://github.com/syoyo/tinyexr/archive/58a81c36caad469aed86441cc91080f23b496ffb.zip"
    URL_HASH "SHA256=C745AE7F336760014509F900779187825B12E61E699AE8A49679A546CD5B8147"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR "uvsr-no-upstream-cmake")
FetchContent_MakeAvailable(uvsr_tinyexr)
set(UVSR_TRANSITIONAL_TINYEXR_FILE_COUNT 161)
set(UVSR_TRANSITIONAL_TINYEXR_TREE_DIGEST
    "40d1660c24ba12162818a07b7ae1556484c90836ac55cce00ab806547b82e999")
uvsr_verify_dependency_file(
    "${uvsr_tinyexr_SOURCE_DIR}/tinyexr.h"
    "6D744B9EFDCFA18D201D28B21386E99DFEAE622E0D03E11FEA4D8684FA714C4C" 486188)
add_library(tinyexr INTERFACE)
target_include_directories(tinyexr SYSTEM INTERFACE
    "${uvsr_tinyexr_SOURCE_DIR}")
set_property(TARGET tinyexr PROPERTY UVSR_PINNED_SOURCE
    "${uvsr_tinyexr_SOURCE_DIR}")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WIN32 ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_COCOA OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(uvsr_glfw
    URL "https://github.com/glfw/glfw/archive/7b6aead9fb88b3623e3b3725ebb42670cbe4c579.zip"
    URL_HASH "SHA256=699BF0B3D0BD422C0212263F30C4B6FC1AB4F67320824B27854F1E5C6949A2A0"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(uvsr_glfw)
set(UVSR_TRANSITIONAL_GLFW_FILE_COUNT 167)
set(UVSR_TRANSITIONAL_GLFW_TREE_DIGEST
    "f00c423a12e4bc41452a4d7fb2b6334253f242192fa4e43ba47d8942f9cf1a24")
uvsr_verify_dependency_file(
    "${uvsr_glfw_SOURCE_DIR}/LICENSE.md"
    "149704059B5D0BF551637E50042DD4DE9C2CAE921021F6636298911E3A5F9462" 904)
set_property(TARGET glfw PROPERTY UVSR_PINNED_SOURCE
    "${uvsr_glfw_SOURCE_DIR}")
set(UVSR_GLFW_LICENSE_SOURCE "${uvsr_glfw_SOURCE_DIR}/LICENSE.md")

foreach(target cgltf stb tinyexr glfw)
    set_target_properties("${target}" PROPERTIES FOLDER "Direct Dependencies")
endforeach()

# one implementation serves the engine and its image/native comparison fixtures.
add_library(uvsr_stb_image STATIC EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/../src/renderer_stb_image.cpp")
target_link_libraries(uvsr_stb_image PUBLIC stb)
uvsr_disable_cpp_exceptions(uvsr_stb_image)

add_library(uvsr_tinyexr STATIC EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/../src/renderer_tinyexr.cpp")
target_link_libraries(uvsr_tinyexr PRIVATE tinyexr)
target_compile_features(uvsr_tinyexr PRIVATE cxx_std_17)
target_compile_definitions(uvsr_tinyexr PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
uvsr_disable_cpp_exceptions(uvsr_tinyexr)
if(MSVC)
    target_compile_options(uvsr_tinyexr PRIVATE /fp:precise)
endif()


set_target_properties(uvsr_stb_image uvsr_tinyexr PROPERTIES FOLDER "Direct Dependencies")
