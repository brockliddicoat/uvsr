foreach(required
        UVSR_GPU_CONTRACT_TEST
        UVSR_SHADER_FACTORY_TEST
        UVSR_RENDERER_LOG_TEST
        UVSR_D3D12_CORE
        UVSR_RENDERER_RESOURCE_CONTRACT_TEST
        UVSR_RENDERER_TEXTURE_BMP_TEST
        UVSR_TEST_DIRECTORY
        UVSR_DXC_EXECUTABLE
        UVSR_CONTRACT_CS_DXIL
        UVSR_CONTRACT_PS_DXIL
        UVSR_SOURCE_DIRECTORY)
    if (NOT DEFINED "${required}" OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Renderer contract test omits ${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${UVSR_TEST_DIRECTORY}")
file(MAKE_DIRECTORY
    "${UVSR_TEST_DIRECTORY}/shader-factory"
    "${UVSR_TEST_DIRECTORY}/dxil")
get_filename_component(cs_directory "${UVSR_CONTRACT_CS_DXIL}" DIRECTORY)
get_filename_component(ps_directory "${UVSR_CONTRACT_PS_DXIL}" DIRECTORY)
file(MAKE_DIRECTORY "${cs_directory}" "${ps_directory}")

execute_process(
    COMMAND "${UVSR_GPU_CONTRACT_TEST}"
    RESULT_VARIABLE gpu_result)
if (NOT gpu_result EQUAL 0)
    message(FATAL_ERROR "Renderer GPU C++ ABI contract failed")
endif()
execute_process(
    COMMAND "${UVSR_SHADER_FACTORY_TEST}"
        "${UVSR_TEST_DIRECTORY}/shader-factory"
    RESULT_VARIABLE factory_result)
if (NOT factory_result EQUAL 0)
    message(FATAL_ERROR "Renderer shader-factory contract failed")
endif()
execute_process(
    COMMAND "${UVSR_RENDERER_LOG_TEST}"
        "${UVSR_TEST_DIRECTORY}/renderer-log"
        "${UVSR_D3D12_CORE}"
    RESULT_VARIABLE log_result)
if (NOT log_result EQUAL 0)
    message(FATAL_ERROR "Renderer logging contract failed")
endif()
execute_process(
    COMMAND "${UVSR_RENDERER_RESOURCE_CONTRACT_TEST}"
    RESULT_VARIABLE resource_result)
if (NOT resource_result EQUAL 0)
    message(FATAL_ERROR "Renderer resource contract failed")
endif()
execute_process(
    COMMAND "${UVSR_RENDERER_TEXTURE_BMP_TEST}"
        "${UVSR_TEST_DIRECTORY}/bmp"
    RESULT_VARIABLE bmp_result)
if (NOT bmp_result EQUAL 0)
    message(FATAL_ERROR "Renderer BMP contract failed")
endif()

foreach(kind CS PS)
    if (kind STREQUAL "CS")
        set(profile cs_6_5)
        set(entry main)
    else()
        set(profile ps_6_5)
        set(entry material_main)
    endif()
    execute_process(
        COMMAND "${UVSR_DXC_EXECUTABLE}"
            -nologo -T "${profile}" -E "${entry}" -D TARGET_D3D12
            -enable-16bit-types -O3 -WX
            -I "${UVSR_SOURCE_DIRECTORY}/src"
            -Fo "${UVSR_CONTRACT_${kind}_DXIL}"
            "${UVSR_SOURCE_DIRECTORY}/tests/renderer_gpu_contract_probe.hlsl"
        RESULT_VARIABLE dxc_result
        ERROR_VARIABLE dxc_error)
    if (NOT dxc_result EQUAL 0)
        message(FATAL_ERROR
            "Renderer ${kind} GPU contract compilation failed: ${dxc_error}")
    endif()
endforeach()

set(UVSR_FULLSCREEN_ZERO_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/renderer-fullscreen-zero.dxil")
set(UVSR_FULLSCREEN_ONE_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/renderer-fullscreen-one.dxil")
set(UVSR_BLIT_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/renderer-blit.dxil")
set(UVSR_PIXEL_READBACK_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/renderer-pixel-readback.dxil")
foreach(depth ZERO ONE)
    if (depth STREQUAL "ZERO")
        set(depth_value 0)
    else()
        set(depth_value 1)
    endif()
    execute_process(
        COMMAND "${UVSR_DXC_EXECUTABLE}"
            -nologo -T vs_6_5 -E main
            -D "UVSR_FULLSCREEN_DEPTH=${depth_value}"
            -O3 -WX
            -Fo "${UVSR_FULLSCREEN_${depth}_DXIL}"
            "${UVSR_SOURCE_DIRECTORY}/src/renderer_fullscreen_vs.hlsl"
        RESULT_VARIABLE common_dxc_result
        ERROR_VARIABLE common_dxc_error)
    if (NOT common_dxc_result EQUAL 0)
        message(FATAL_ERROR
            "Renderer fullscreen ${depth_value} compilation failed: ${common_dxc_error}")
    endif()
endforeach()
execute_process(
    COMMAND "${UVSR_DXC_EXECUTABLE}"
        -nologo -T ps_6_5 -E main -O3 -WX
        -Fo "${UVSR_BLIT_DXIL}"
        "${UVSR_SOURCE_DIRECTORY}/src/renderer_blit_ps.hlsl"
    RESULT_VARIABLE common_dxc_result
    ERROR_VARIABLE common_dxc_error)
if (NOT common_dxc_result EQUAL 0)
    message(FATAL_ERROR "Renderer blit compilation failed: ${common_dxc_error}")
endif()
execute_process(
    COMMAND "${UVSR_DXC_EXECUTABLE}"
        -nologo -T cs_6_5 -E main -O3 -WX
        -I "${UVSR_SOURCE_DIRECTORY}/src"
        -Fo "${UVSR_PIXEL_READBACK_DXIL}"
        "${UVSR_SOURCE_DIRECTORY}/src/renderer_pixel_readback_cs.hlsl"
    RESULT_VARIABLE common_dxc_result
    ERROR_VARIABLE common_dxc_error)
if (NOT common_dxc_result EQUAL 0)
    message(FATAL_ERROR
        "Renderer pixel-readback compilation failed: ${common_dxc_error}")
endif()
include("${UVSR_SOURCE_DIRECTORY}/tests/renderer_common_passes_reflection.cmake")

function(uvsr_compile_binding_contract output profile entry source)
    execute_process(
        COMMAND "${UVSR_DXC_EXECUTABLE}"
            -nologo -T "${profile}" -E "${entry}"
            -D TARGET_D3D12 -enable-16bit-types -O3 -WX
            -I "${UVSR_SOURCE_DIRECTORY}/src"
            -Fo "${output}"
            ${ARGN}
            "${UVSR_SOURCE_DIRECTORY}/${source}"
        RESULT_VARIABLE binding_dxc_result
        ERROR_VARIABLE binding_dxc_error)
    if(NOT binding_dxc_result EQUAL 0)
        message(FATAL_ERROR
            "Renderer binding contract compilation failed for ${source}: "
            "${binding_dxc_error}")
    endif()
endfunction()

set(UVSR_PBR_NORMAL_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/pbr-normal.dxil")
set(UVSR_PBR_MSAA_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/pbr-msaa.dxil")
set(UVSR_SKY_SINGLE_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/sky-single.dxil")
set(UVSR_SKY_MSAA_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/sky-msaa.dxil")
set(UVSR_SCREEN_SPACE_COMPOSITE_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/screen-space-indirect-composite.dxil")
set(UVSR_FLASHLIGHT_VISIBILITY_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/flashlight-visibility.dxil")
set(UVSR_FLASHLIGHT_HIT_DISTANCE_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/flashlight-hit-distance.dxil")
set(UVSR_DIRECTIONAL_VISIBILITY_DXIL
    "${UVSR_TEST_DIRECTORY}/dxil/directional-visibility.dxil")
uvsr_compile_binding_contract(
    "${UVSR_PBR_NORMAL_DXIL}" cs_6_5 main
    src/pbr_deferred_lighting_cs.hlsl
    -D WRITE_SOURCE_RADIANCE=0)
uvsr_compile_binding_contract(
    "${UVSR_PBR_MSAA_DXIL}" cs_6_5 main
    src/pbr_deferred_lighting_msaa_cs.hlsl
    -D PBR_DEFERRED_MSAA_SAMPLES=4
    -D PBR_DEFERRED_MSAA_VISIBILITY=1)
uvsr_compile_binding_contract(
    "${UVSR_SKY_SINGLE_DXIL}" cs_6_5 Generate
    src/ray_traced_sky_visibility_cs.hlsl
    -D OUTPUT_HIT_DISTANCE=0
    -D SKY_VISIBILITY_SAMPLES=1
    -res-may-alias)
uvsr_compile_binding_contract(
    "${UVSR_SKY_MSAA_DXIL}" cs_6_5 Generate
    src/ray_traced_sky_visibility_cs.hlsl
    -D OUTPUT_HIT_DISTANCE=1
    -D SKY_VISIBILITY_SAMPLES=4
    -res-may-alias)
uvsr_compile_binding_contract(
    "${UVSR_SCREEN_SPACE_COMPOSITE_DXIL}" cs_6_5 main
    src/screen_space_indirect_composite_cs.hlsl)
uvsr_compile_binding_contract(
    "${UVSR_FLASHLIGHT_VISIBILITY_DXIL}" cs_6_5 GenerateVisibility
    src/ray_traced_flashlight_shadows_cs.hlsl
    -D FLASHLIGHT_VISIBILITY_SAMPLES=1
    -res-may-alias)
uvsr_compile_binding_contract(
    "${UVSR_FLASHLIGHT_HIT_DISTANCE_DXIL}" cs_6_5
    GenerateVisibilityAndHitDistance
    src/ray_traced_flashlight_shadows_cs.hlsl
    -D FLASHLIGHT_VISIBILITY_SAMPLES=4
    -res-may-alias)
uvsr_compile_binding_contract(
    "${UVSR_DIRECTIONAL_VISIBILITY_DXIL}" cs_6_5 main
    src/directional_ray_visibility_cs.hlsl
    -D DIRECTIONAL_VISIBILITY_SAMPLES=1
    -res-may-alias)
include("${UVSR_SOURCE_DIRECTORY}/tests/pbr_sky_binding_reflection.cmake")

include("${UVSR_SOURCE_DIRECTORY}/tests/renderer_gpu_contract_reflection.cmake")
