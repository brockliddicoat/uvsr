cmake_minimum_required(VERSION 3.24)
project(uvsr_portable_contracts LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(UVSR_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# this is an isolated graph. normalize the default before applying one mode to
# every linked source, without mixing _HAS_EXCEPTIONS values with the old engine.
if(MSVC)
    string(REGEX REPLACE "[/\\-]EH[a-zA-Z0-9\\-]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()
add_library(probe_policy INTERFACE)
target_compile_definitions(probe_policy INTERFACE _HAS_EXCEPTIONS=0 SIMDJSON_EXCEPTIONS=0)
if(MSVC)
    target_compile_options(probe_policy INTERFACE /EHs-c-
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/probe_mode.h")
else()
    target_compile_options(probe_policy INTERFACE -fno-exceptions
        -include "${CMAKE_CURRENT_SOURCE_DIR}/probe_mode.h")
endif()

set(portable_headers
    array_view.h checked_worklist.h renderer_gpu_scalar.h renderer_view_contract.h
    renderer_gpu_contract.h renderer_material_contract.h pbr_material.h
    renderer_pixel_readback.h renderer_pixel_readback_cb.h renderer_resource_contract.h
    renderer_light_probe_contract.h renderer_producer_contract.h shader_math.h
    lighting_accumulation_contract.h path_tracing_accumulation_contract.h
    pbr_surface_light_contract.h renderer_scene_retirement.h
    renderer_scene_load_worker.h renderer_target_layout.h renderer_upload_layout.h)
foreach(header IN LISTS portable_headers)
    string(REPLACE "." "_" target "${header}")
    set(probe "${CMAKE_CURRENT_BINARY_DIR}/${target}.cpp")
    file(WRITE "${probe}" "#include \"${header}\"\n")
    add_library("header_${target}" OBJECT "${probe}")
    target_include_directories("header_${target}" PRIVATE "${UVSR_SOURCE_ROOT}/src")
    target_link_libraries("header_${target}" PRIVATE probe_policy)
    if(MSVC)
        target_compile_options("header_${target}" PRIVATE /permissive- /W4 /WX /showIncludes)
    endif()
endforeach()

add_executable(portable_worklist "${UVSR_SOURCE_ROOT}/tests/checked_worklist_tests.cpp")
target_include_directories(portable_worklist PRIVATE "${UVSR_SOURCE_ROOT}/src")
target_link_libraries(portable_worklist PRIVATE probe_policy)
target_compile_definitions(portable_worklist PRIVATE UVSR_REQUIRE_NO_EXCEPTIONS=1)

add_library(worklist_throwing_assignment OBJECT EXCLUDE_FROM_ALL throwing_assignment.cpp)
target_include_directories(worklist_throwing_assignment PRIVATE "${UVSR_SOURCE_ROOT}/src")
target_link_libraries(worklist_throwing_assignment PRIVATE probe_policy)

add_library(retirement_core OBJECT "${UVSR_SOURCE_ROOT}/src/renderer_scene_retirement.cpp")
target_include_directories(retirement_core PRIVATE "${UVSR_SOURCE_ROOT}/src")
target_link_libraries(retirement_core PRIVATE probe_policy)

if(WIN32)
    add_executable(portable_worker
        "${UVSR_SOURCE_ROOT}/src/renderer_scene_load_worker_win32.cpp"
        "${UVSR_SOURCE_ROOT}/tests/renderer_scene_load_worker_tests.cpp"
        worker_main.cpp)
    target_include_directories(portable_worker PRIVATE "${UVSR_SOURCE_ROOT}/src")
    target_link_libraries(portable_worker PRIVATE probe_policy)
    if(MSVC)
        target_compile_options(portable_worker PRIVATE /permissive- /W4 /WX /showIncludes)
    endif()
endif()

# compilation of the actual implementation, not another readback wrapper. this
# target does not link a device or claim a no-exception NVRHI runtime graph.
add_library(readback_slice OBJECT "${UVSR_SOURCE_ROOT}/src/renderer_pixel_readback_nvrhi.cpp")
target_include_directories(readback_slice PRIVATE "${UVSR_SOURCE_ROOT}/src")
target_include_directories(readback_slice SYSTEM PRIVATE "${UVSR_SOURCE_ROOT}/third_party/nvrhi/include")
target_link_libraries(readback_slice PRIVATE probe_policy)
if(MSVC)
    target_compile_options(readback_slice PRIVATE /permissive- /W4 /WX /showIncludes)
endif()

option(UVSR_PROBE_IMPORTER "build the pinned importer probe from supplied source roots" OFF)
if(UVSR_PROBE_IMPORTER)
    if(NOT EXISTS "${FASTGLTF_SOURCE}/CMakeLists.txt" OR
        NOT EXISTS "${SIMDJSON_SOURCE}/singleheader/simdjson.cpp")
        message(FATAL_ERROR "provide the verified FASTGLTF_SOURCE and SIMDJSON_SOURCE roots")
    endif()
    add_library(simdjson STATIC "${SIMDJSON_SOURCE}/singleheader/simdjson.cpp")
    add_library(simdjson::simdjson ALIAS simdjson)
    target_include_directories(simdjson SYSTEM PUBLIC "${SIMDJSON_SOURCE}/singleheader")
    target_link_libraries(simdjson PUBLIC probe_policy)
    set(FASTGLTF_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${FASTGLTF_SOURCE}" fastgltf-build)
    if(MSVC)
        get_target_property(fastgltf_options fastgltf COMPILE_OPTIONS)
        list(FILTER fastgltf_options EXCLUDE REGEX "^/EH")
        set_property(TARGET fastgltf PROPERTY COMPILE_OPTIONS "${fastgltf_options}")
    endif()
    target_link_libraries(fastgltf PRIVATE probe_policy)
    add_executable(importer_probe importer_probe.cpp)
    target_link_libraries(importer_probe PRIVATE fastgltf::fastgltf probe_policy)
endif()

enable_testing()
add_test(NAME portable_worklist COMMAND portable_worklist)
if(WIN32)
    add_test(NAME portable_worker COMMAND portable_worker)
endif()
if(UVSR_PROBE_IMPORTER)
    add_test(NAME importer_policy COMMAND importer_probe)
endif()
