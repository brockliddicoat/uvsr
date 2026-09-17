include_guard(GLOBAL)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/NoCppExceptions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VerifyDirectDependencyState.cmake")

FetchContent_Declare(uvsr_simdjson
    URL "https://github.com/simdjson/simdjson/archive/61641f9a7aedc762d3b1c049a2b2a44d1c6db8a2.zip"
    URL_HASH "SHA256=c1fe038904b0af61755252fe525af948bf2fec02d866360aadc70004a6119c44"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR "uvsr-no-upstream-cmake")
FetchContent_MakeAvailable(uvsr_simdjson)
set(UVSR_SIMDJSON_FILE_COUNT 741)
set(UVSR_SIMDJSON_TREE_DIGEST
    "72ba2030f4208d5c0f15c909e6fc23887007baacbf095e5ac33138899bdb26f0")
uvsr_verify_dependency_file("${uvsr_simdjson_SOURCE_DIR}/LICENSE"
    "5fa8894e890bd77958f93b165433e0fb0dffa5bc982bfb147e4748e95bad24e5" 11355)
uvsr_verify_dependency_file("${uvsr_simdjson_SOURCE_DIR}/LICENSE-MIT"
    "9ed0a34979f22fc33fc13d942233f22d44906c236d876bd05fadf18cb5abf7da" 1065)
set(UVSR_SIMDJSON_LICENSE_SOURCE "${uvsr_simdjson_SOURCE_DIR}/LICENSE")
set(UVSR_SIMDJSON_MIT_LICENSE_SOURCE "${uvsr_simdjson_SOURCE_DIR}/LICENSE-MIT")
add_library(uvsr_simdjson STATIC EXCLUDE_FROM_ALL
    "${uvsr_simdjson_SOURCE_DIR}/singleheader/simdjson.cpp")
add_library(simdjson::simdjson ALIAS uvsr_simdjson)
target_include_directories(uvsr_simdjson SYSTEM PUBLIC "${uvsr_simdjson_SOURCE_DIR}/singleheader")
target_compile_definitions(uvsr_simdjson PUBLIC SIMDJSON_EXCEPTIONS=0)
target_compile_features(uvsr_simdjson PUBLIC cxx_std_17)

FetchContent_Declare(uvsr_fastgltf
    URL "https://github.com/spnda/fastgltf/archive/f89e438230b6624d5e886fac0d1829b7c7299b2e.zip"
    URL_HASH "SHA256=fdb102ff06ef15ecbff1c9e5ed4256c2dbcc39921589366e1bfd2d698c8189ea"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR "uvsr-no-upstream-cmake")
FetchContent_MakeAvailable(uvsr_fastgltf)
set(UVSR_FASTGLTF_FILE_COUNT 67)
set(UVSR_FASTGLTF_TREE_DIGEST
    "19f90643c1e2bee1daa0d51dc03c1ca28cc009773d964b2ec4705abdaed4d43d")
uvsr_verify_dependency_file("${uvsr_fastgltf_SOURCE_DIR}/LICENSE.md"
    "3b46ffb0349f0fcf6c6d26fcb0d7d98f89f42285adff866f9fc94c7a3029a043" 1101)
set(UVSR_FASTGLTF_LICENSE_SOURCE "${uvsr_fastgltf_SOURCE_DIR}/LICENSE.md")
foreach(option IN ITEMS FASTGLTF_USE_CUSTOM_SMALLVECTOR FASTGLTF_ENABLE_TESTS
        FASTGLTF_ENABLE_EXAMPLES FASTGLTF_ENABLE_DOCS FASTGLTF_ENABLE_GLTF_RS
        FASTGLTF_ENABLE_ASSIMP FASTGLTF_DISABLE_CUSTOM_MEMORY_POOL FASTGLTF_USE_64BIT_FLOAT
        FASTGLTF_COMPILE_AS_CPP20 FASTGLTF_ENABLE_CPP_MODULES FASTGLTF_USE_STD_MODULE
        FASTGLTF_ENABLE_KHR_IMPLICIT_SHAPES FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES
        FASTGLTF_ENABLE_INSTALL)
    set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
# the supplied simdjson target prevents upstream downloads and package discovery.
add_subdirectory("${uvsr_fastgltf_SOURCE_DIR}" "${uvsr_fastgltf_BINARY_DIR}" EXCLUDE_FROM_ALL)

foreach(target IN ITEMS uvsr_simdjson fastgltf)
    uvsr_disable_cpp_exceptions(${target})
    set_target_properties(${target} PROPERTIES FOLDER "Direct Dependencies")
endforeach()
set_property(TARGET uvsr_simdjson PROPERTY UVSR_PINNED_SOURCE "${uvsr_simdjson_SOURCE_DIR}")
set_property(TARGET fastgltf PROPERTY UVSR_PINNED_SOURCE "${uvsr_fastgltf_SOURCE_DIR}")
