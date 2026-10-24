cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(UVSRLauncher LANGUAGES C CXX RC)
    include(CTest)
    include(FetchContent)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config}
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endforeach()
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    include(FetchContent)
endif()

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "UVSR Launcher requires the x64 Microsoft Windows toolchain")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "UVSR Launcher requires an x64 generator")
endif()

set(UVSR_LAUNCHER_VERSION "1.3.0")
set(UVSR_LAUNCHER_RELEASE_SEQUENCE "17")
set(UVSR_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
string(REPLACE "." "," UVSR_LAUNCHER_VERSION_COMMAS
    "${UVSR_LAUNCHER_VERSION}")

if(NOT DEFINED UVSR_LAUNCHER_SOURCE_COMMIT)
    if(DEFINED UVSR_GIT_COMMIT)
        set(UVSR_LAUNCHER_SOURCE_COMMIT "${UVSR_GIT_COMMIT}")
    else()
        find_package(Git REQUIRED)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${UVSR_REPOSITORY_ROOT}"
                rev-parse HEAD
            OUTPUT_VARIABLE UVSR_LAUNCHER_SOURCE_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
endif()
string(LENGTH "${UVSR_LAUNCHER_SOURCE_COMMIT}"
    UVSR_LAUNCHER_SOURCE_COMMIT_LENGTH)
if(NOT UVSR_LAUNCHER_SOURCE_COMMIT MATCHES "^[0-9a-f]+$" OR
    NOT UVSR_LAUNCHER_SOURCE_COMMIT_LENGTH EQUAL 40)
    message(FATAL_ERROR "UVSR launcher source commit is not canonical")
endif()

if(NOT DEFINED UVSR_LAUNCHER_SOURCE_CLEAN)
    if(DEFINED UVSR_SOURCE_TREE_CLEAN)
        set(UVSR_LAUNCHER_SOURCE_CLEAN "${UVSR_SOURCE_TREE_CLEAN}")
    else()
        find_package(Git REQUIRED)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${UVSR_REPOSITORY_ROOT}"
                status --porcelain=v1 --untracked-files=all
                --ignore-submodules=none
            OUTPUT_VARIABLE UVSR_LAUNCHER_STATUS
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
        if(UVSR_LAUNCHER_STATUS STREQUAL "")
            set(UVSR_LAUNCHER_SOURCE_CLEAN ON)
        else()
            set(UVSR_LAUNCHER_SOURCE_CLEAN OFF)
        endif()
    endif()
endif()

set(UVSR_LAUNCHER_PRODUCT_VERSION
    "${UVSR_LAUNCHER_VERSION}+${UVSR_LAUNCHER_SOURCE_COMMIT}")
if(NOT UVSR_LAUNCHER_SOURCE_CLEAN)
    string(APPEND UVSR_LAUNCHER_PRODUCT_VERSION ".dirty")
endif()

FetchContent_Declare(uvsr_launcher_miniz
    URL
        "https://github.com/richgel999/miniz/archive/77d0dce8627735138c51770d1799a1ef48f2117d.tar.gz"
    URL_HASH
        "SHA256=baa14349e7c5efb4a493a03af7d2f29d0ef45d87d488567fe5360edb33f8b7ba"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(uvsr_launcher_miniz)
target_compile_options(miniz PRIVATE /W3)

file(TO_CMAKE_PATH "${UVSR_REPOSITORY_ROOT}/LICENSE.md"
    UVSR_LICENSE_RESOURCE)
file(TO_CMAKE_PATH
    "${UVSR_REPOSITORY_ROOT}/assets/fonts/noto-sans/NotoSans-Regular.ttf"
    UVSR_NOTO_REGULAR_RESOURCE)
file(TO_CMAKE_PATH
    "${UVSR_REPOSITORY_ROOT}/assets/fonts/noto-sans/NotoSans-Bold.ttf"
    UVSR_NOTO_BOLD_RESOURCE)
file(TO_CMAKE_PATH
    "${UVSR_REPOSITORY_ROOT}/assets/fonts/noto-sans/OFL.txt"
    UVSR_NOTO_LICENSE_RESOURCE)
file(TO_CMAKE_PATH
    "${UVSR_REPOSITORY_ROOT}/cmake/runtime-shader-inventory.def"
    UVSR_SHADER_INVENTORY_RESOURCE)
file(TO_CMAKE_PATH
    "${UVSR_REPOSITORY_ROOT}/cmake/runtime-media-inventory.def"
    UVSR_MEDIA_INVENTORY_RESOURCE)
file(TO_CMAKE_PATH
    "${CMAKE_CURRENT_LIST_DIR}/third_party/miniz-LICENSE.txt"
    UVSR_MINIZ_LICENSE_RESOURCE)
configure_file(src/native/app.manifest.in
    "${CMAKE_CURRENT_BINARY_DIR}/uvsr-launcher.manifest" @ONLY)
file(TO_CMAKE_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/uvsr-launcher.manifest"
    UVSR_MANIFEST_RESOURCE)
configure_file(src/native/resources.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/uvsr-launcher.rc" @ONLY)

set(UVSR_LAUNCHER_NATIVE_SOURCES
    src/native/core.cpp
    src/native/download.cpp
    src/native/package.cpp
    src/native/engine.cpp
    src/native/ui.cpp)

add_library(uvsr_launcher_core STATIC ${UVSR_LAUNCHER_NATIVE_SOURCES})
target_include_directories(uvsr_launcher_core PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src/native"
    "${UVSR_REPOSITORY_ROOT}/src")
target_compile_features(uvsr_launcher_core PUBLIC cxx_std_20)
target_compile_definitions(uvsr_launcher_core PUBLIC
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A00000B
    UVSR_LAUNCHER_VERSION="${UVSR_LAUNCHER_VERSION}"
    UVSR_LAUNCHER_RELEASE_SEQUENCE=${UVSR_LAUNCHER_RELEASE_SEQUENCE}
    UVSR_LAUNCHER_SOURCE_COMMIT="${UVSR_LAUNCHER_SOURCE_COMMIT}"
    UVSR_LAUNCHER_SOURCE_CLEAN=$<BOOL:${UVSR_LAUNCHER_SOURCE_CLEAN}>)
target_compile_options(uvsr_launcher_core PRIVATE
    /permissive- /W4 /WX /utf-8 /sdl /guard:cf /Zc:__cplusplus)
target_link_libraries(uvsr_launcher_core PUBLIC
    miniz
    advapi32 bcrypt comctl32 crypt32 dwmapi gdi32 ole32 oleaut32
    propsys shell32 shlwapi user32 uxtheme version winhttp wintrust)

add_executable(uvsr-launcher WIN32
    src/native/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/uvsr-launcher.rc")
target_link_libraries(uvsr-launcher PRIVATE uvsr_launcher_core)
set_target_properties(uvsr-launcher PROPERTIES
    OUTPUT_NAME "uvsr-launcher"
    LINK_FLAGS "/MANIFEST:NO /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /CETCOMPAT")

if(BUILD_TESTING)
    add_executable(uvsr_launcher_contract_tests
        tests/native_tests.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/uvsr-launcher.rc")
    target_link_libraries(uvsr_launcher_contract_tests PRIVATE
        uvsr_launcher_core)
    target_compile_options(uvsr_launcher_contract_tests PRIVATE
        /permissive- /W4 /WX /utf-8 /sdl /guard:cf /Zc:__cplusplus)
    set_target_properties(uvsr_launcher_contract_tests PROPERTIES
        LINK_FLAGS "/MANIFEST:NO /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /CETCOMPAT")
    add_test(NAME uvsr_launcher_contract
        COMMAND uvsr_launcher_contract_tests)
endif()
