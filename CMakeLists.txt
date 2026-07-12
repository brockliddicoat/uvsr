cmake_minimum_required(VERSION 3.24)

project(UVSR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endforeach()

set(DONUT_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_DX12 ON CACHE BOOL "" FORCE)
set(DONUT_WITH_VULKAN OFF CACHE BOOL "" FORCE)
set(DONUT_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)
add_subdirectory(donut)

include(donut/compileshaders.cmake)
donut_compile_shaders_all_platforms(
    TARGET uvsr_shaders
    PROJECT_NAME "UVSR"
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.cfg
    OUTPUT_BASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/uvsr
)

add_executable(uvsr WIN32 src/uvsr.cpp)
target_link_libraries(uvsr PRIVATE donut_app donut_engine)
add_dependencies(uvsr uvsr_shaders)
