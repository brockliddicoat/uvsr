include_guard(GLOBAL)
set(UVSR_DONUT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../donut")
cmake_path(NORMAL_PATH UVSR_DONUT_SOURCE_DIR)
set(UVSR_DONUT_EXPECTED_HEAD
    "bc1ea24b0486f1c00d89327fe16c0b4dd11c5937")
include("${CMAKE_CURRENT_LIST_DIR}/VerifyDirectDependencyState.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NoCppExceptions.cmake")

uvsr_verify_dependency_git("${UVSR_DONUT_SOURCE_DIR}" "${UVSR_DONUT_EXPECTED_HEAD}")

# temporary consumers require the independently configured parent targets.
foreach(dependency IN ITEMS nvrhi nvrhi_d3d12 glfw)
    if(NOT TARGET ${dependency})
        message(FATAL_ERROR "Donut requires parent target ${dependency}")
    endif()
endforeach()

# the device host retains logging and inline gamepad math.
add_library(donut_core STATIC EXCLUDE_FROM_ALL
    "${UVSR_DONUT_SOURCE_DIR}/src/core/log.cpp")
target_include_directories(donut_core PUBLIC
    "${UVSR_DONUT_SOURCE_DIR}/include")
target_compile_definitions(donut_core PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
uvsr_disable_cpp_exceptions(donut_core)

file(GLOB donut_app_sources CONFIGURE_DEPENDS
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/app/*.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/app/*.cpp")
list(REMOVE_ITEM donut_app_sources
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/app/imgui_renderer.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/app/imgui_renderer.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/app/imgui_nvrhi.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/app/imgui_nvrhi.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/app/UserInterfaceUtils.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/app/UserInterfaceUtils.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/app/imgui_console.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/app/imgui_console.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/app/Camera.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/app/Camera.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/include/donut/app/ApplicationBase.h"
    "${UVSR_DONUT_SOURCE_DIR}/src/app/ApplicationBase.cpp"
    "${UVSR_DONUT_SOURCE_DIR}/src/app/MediaFileSystem.cpp")
list(APPEND donut_app_sources
    "${UVSR_DONUT_SOURCE_DIR}/src/app/dx12/DeviceManager_DX12.cpp")
add_library(donut_app STATIC EXCLUDE_FROM_ALL ${donut_app_sources})
target_include_directories(donut_app PUBLIC
    "${UVSR_DONUT_SOURCE_DIR}/include")
target_link_libraries(donut_app PUBLIC
    donut_core glfw nvrhi_d3d12 d3d12 dxgi dxguid nvrhi)
target_compile_definitions(donut_app PUBLIC
    DONUT_WITH_DX11=0
    DONUT_WITH_DX12=1
    DONUT_WITH_VULKAN=0
    DONUT_WITH_AFTERMATH=0
    DONUT_WITH_STREAMLINE=0)
target_compile_definitions(donut_app PRIVATE
    DONUT_FORCE_DISCRETE_GPU=0
    UVSR_WITH_NVRHI_VALIDATION=$<BOOL:${NVRHI_WITH_VALIDATION}>)

foreach(target donut_core donut_app)
    set_target_properties("${target}" PROPERTIES FOLDER "Donut")
endforeach()
