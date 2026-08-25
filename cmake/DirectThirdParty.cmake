function(uvsr_require_direct_dependency_file path expected_size expected_sha256 label)
    if (NOT EXISTS "${path}")
        message(FATAL_ERROR "${label} is missing from its pinned archive")
    endif()
    file(SIZE "${path}" actual_size)
    file(SHA256 "${path}" actual_sha256)
    string(TOUPPER "${actual_sha256}" actual_sha256)
    if (NOT actual_size EQUAL expected_size OR
        NOT actual_sha256 STREQUAL expected_sha256)
        message(FATAL_ERROR "${label} failed its exact size/SHA-256 check")
    endif()
endfunction()

FetchContent_Declare(uvsr_cgltf
    URL "https://github.com/jkuhlmann/cgltf/archive/fa3b80fa762790192c9532b63c441627416ff300.zip"
    URL_HASH "SHA256=89351D82A140337AC876E018B091F26176FCC8C227479796993CE79BE33ED8A3"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR "uvsr-no-upstream-cmake")
FetchContent_MakeAvailable(uvsr_cgltf)
set(UVSR_CGLTF_FILE_COUNT 19)
set(UVSR_CGLTF_TREE_DIGEST
    "d949633afa4a966b4550497bfab9025bf8f5900ad275d4dde87840831b1bdffc")
uvsr_require_direct_dependency_file(
    "${uvsr_cgltf_SOURCE_DIR}/LICENSE" 1066
    "F619925F80EF862497AAF8E8155EF218FA6A2190055129523CA3DF9119A9BA95"
    "cgltf license")
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
uvsr_require_direct_dependency_file(
    "${uvsr_stb_SOURCE_DIR}/LICENSE" 2510
    "BEBFE904B14301657E4E5D655C811D51FD31B97C455B9CC2D8600D6BAC6CFF63"
    "stb license")
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${uvsr_stb_SOURCE_DIR}")
set_property(TARGET stb PROPERTY UVSR_PINNED_SOURCE "${uvsr_stb_SOURCE_DIR}")
set(UVSR_STB_LICENSE_SOURCE "${uvsr_stb_SOURCE_DIR}/LICENSE")

FetchContent_Declare(uvsr_jsoncpp
    URL "https://github.com/open-source-parsers/jsoncpp/archive/89e2973c754a9c02a49974d839779b151e95afd6.zip"
    URL_HASH "SHA256=02F0804596C1E18C064D890AC9497FA17D585E822FCACF07FF8A8AA0B344A7BD"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR "uvsr-no-upstream-cmake")
FetchContent_MakeAvailable(uvsr_jsoncpp)
set(UVSR_JSONCPP_FILE_COUNT 252)
set(UVSR_JSONCPP_TREE_DIGEST
    "0fca9881e1f50c15ab94d761e0651c2e9d17b4bb2fb96fe2bb1bb27fb23c3f73")
uvsr_require_direct_dependency_file(
    "${uvsr_jsoncpp_SOURCE_DIR}/LICENSE" 2714
    "CEC0DB5F6D7ED6B3A72647BD50AED02E13C3377FD44382B96DC2915534C042AD"
    "JsonCpp license")
add_library(jsoncpp_static STATIC EXCLUDE_FROM_ALL
    "${uvsr_jsoncpp_SOURCE_DIR}/src/lib_json/json_reader.cpp"
    "${uvsr_jsoncpp_SOURCE_DIR}/src/lib_json/json_value.cpp"
    "${uvsr_jsoncpp_SOURCE_DIR}/src/lib_json/json_writer.cpp")
set(UVSR_JSONCPP_COMPATIBILITY_INCLUDE
    "${CMAKE_CURRENT_BINARY_DIR}/uvsr_jsoncpp_compatibility")
file(GENERATE
    OUTPUT "${UVSR_JSONCPP_COMPATIBILITY_INCLUDE}/json/json-forwards.h"
    CONTENT "#pragma once\n#include <json/forwards.h>\n")
set(UVSR_TRANSITIONAL_JSONCPP_ALIAS_RECORDS
    "${UVSR_JSONCPP_COMPATIBILITY_INCLUDE}/json/json-forwards.h|58DEF6D7AB28EC0C676F4766B121802979CEA6745676BFC9BB0A209211EBCDB9")
# Donut's v1.9.6 amalgam names this upstream header json-forwards.h.
# Delete the alias with the transitional Donut JSON caller.
target_include_directories(jsoncpp_static SYSTEM BEFORE PUBLIC
    "${UVSR_JSONCPP_COMPATIBILITY_INCLUDE}"
    "${uvsr_jsoncpp_SOURCE_DIR}/include")
target_compile_features(jsoncpp_static PUBLIC cxx_std_11)
set_property(TARGET jsoncpp_static PROPERTY UVSR_PINNED_SOURCE
    "${uvsr_jsoncpp_SOURCE_DIR}")
set(UVSR_JSONCPP_LICENSE_SOURCE "${uvsr_jsoncpp_SOURCE_DIR}/LICENSE")

FetchContent_Declare(uvsr_tinyexr
    URL "https://github.com/syoyo/tinyexr/archive/58a81c36caad469aed86441cc91080f23b496ffb.zip"
    URL_HASH "SHA256=C745AE7F336760014509F900779187825B12E61E699AE8A49679A546CD5B8147"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR "uvsr-no-upstream-cmake")
FetchContent_MakeAvailable(uvsr_tinyexr)
set(UVSR_TRANSITIONAL_TINYEXR_FILE_COUNT 161)
set(UVSR_TRANSITIONAL_TINYEXR_TREE_DIGEST
    "40d1660c24ba12162818a07b7ae1556484c90836ac55cce00ab806547b82e999")
uvsr_require_direct_dependency_file(
    "${uvsr_tinyexr_SOURCE_DIR}/tinyexr.h" 486188
    "6D744B9EFDCFA18D201D28B21386E99DFEAE622E0D03E11FEA4D8684FA714C4C"
    "TinyEXR header")
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
uvsr_require_direct_dependency_file(
    "${uvsr_glfw_SOURCE_DIR}/LICENSE.md" 904
    "149704059B5D0BF551637E50042DD4DE9C2CAE921021F6636298911E3A5F9462"
    "GLFW license")
set_property(TARGET glfw PROPERTY UVSR_PINNED_SOURCE
    "${uvsr_glfw_SOURCE_DIR}")
set(UVSR_GLFW_LICENSE_SOURCE "${uvsr_glfw_SOURCE_DIR}/LICENSE.md")

foreach(target cgltf stb jsoncpp_static tinyexr glfw)
    set_target_properties("${target}" PROPERTIES FOLDER "Direct Dependencies")
endforeach()
