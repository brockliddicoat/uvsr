foreach(required UVSR_DXC_EXECUTABLE UVSR_PATH_TRACING_DXIL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

if(NOT EXISTS "${UVSR_PATH_TRACING_DXIL}")
    message(FATAL_ERROR
        "Missing production path-tracing DXIL: ${UVSR_PATH_TRACING_DXIL}")
endif()

execute_process(
    COMMAND "${UVSR_DXC_EXECUTABLE}" -dumpbin
        "${UVSR_PATH_TRACING_DXIL}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE dump
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "DXC could not reflect path tracing: ${error}")
endif()

function(require_regex pattern label)
    if(NOT "${dump}" MATCHES "${pattern}")
        message(FATAL_ERROR "Missing path-tracing reflection: ${label}")
    endif()
endfunction()

require_regex("c_PathTracing[^\r\n]*cb0[ ]+1" "constants b0")
require_regex("c_PathTracing;[^\r\n]*Size:[ ]*1568" "constant ABI")
require_regex("t_WorldBvh[^\r\n]*ras[^\r\n]*t0[ ]+1" "TLAS t0")
require_regex("t_Environment[^\r\n]*f32[^\r\n]*cube[^\r\n]*t1[ ]+1"
    "environment cube t1")
require_regex("t_Noise[^\r\n]*f32[^\r\n]*2darray[^\r\n]*t2[ ]+1"
    "noise array t2")
require_regex("t_PathTracingLights[^\r\n]*struct[^\r\n]*t13[ ]+1"
    "light buffer t13")
require_regex("t_PathTracingInstances[^\r\n]*struct[^\r\n]*t14[ ]+1"
    "instance buffer t14")

foreach(binding
        "u_RawMean;f32;u0"
        "u_SuccessfulSampleCount;u32;u1"
        "u_Motion;f32;u2"
        "u_Depth;f32;u3"
        "u_RetryGeneration;u32;u4")
    list(GET binding 0 name)
    list(GET binding 1 component)
    list(GET binding 2 slot)
    require_regex(
        "${name}[^\r\n]*${component}[^\r\n]*2d[^\r\n]*${slot}[ ]+1"
        "${name} ${slot}")
endforeach()

require_regex("t_RayMaterialBuffers[^\r\n]*t0,space1unbounded"
    "bindless geometry buffers")
require_regex("t_RayMaterialTextures[^\r\n]*t0,space2unbounded"
    "bindless material textures")
require_regex("NumThreads=\\(8,8,1\\)" "8x8x1 dispatch")
require_regex("rayQuery_TraceRayInline" "inline ray-query trace")
require_regex("rayQuery_Proceed" "inline ray-query traversal")

message(STATUS
    "Path-tracing DXIL bindings, retry UAV, threads, and ray queries passed")
