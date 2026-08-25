foreach(required
        UVSR_DXC_EXECUTABLE
        UVSR_FULLSCREEN_ZERO_DXIL
        UVSR_FULLSCREEN_ONE_DXIL
        UVSR_BLIT_DXIL
        UVSR_PIXEL_READBACK_DXIL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

foreach(shader FULLSCREEN_ZERO FULLSCREEN_ONE BLIT PIXEL_READBACK)
    if(NOT EXISTS "${UVSR_${shader}_DXIL}")
        message(FATAL_ERROR "Missing ${shader} DXIL")
    endif()
    execute_process(
        COMMAND "${UVSR_DXC_EXECUTABLE}" -dumpbin "${UVSR_${shader}_DXIL}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE dump
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "DXC could not reflect ${shader}: ${error}")
    endif()
    set(${shader}_DUMP "${dump}")
endforeach()

function(require_regex text pattern label)
    if(NOT "${text}" MATCHES "${pattern}")
        message(FATAL_ERROR "Missing renderer common reflection: ${label}")
    endif()
endfunction()

foreach(shader FULLSCREEN_ZERO FULLSCREEN_ONE)
    require_regex(
        "${${shader}_DUMP}"
        "SV_VertexID[ ]+0[ ]+x"
        "${shader} vertex ID")
    require_regex(
        "${${shader}_DUMP}"
        "SV_Position[ ]+0[ ]+xyzw"
        "${shader} position")
    require_regex(
        "${${shader}_DUMP}"
        "UV[ ]+0[ ]+xy"
        "${shader} UV")
endforeach()
require_regex(
    "${FULLSCREEN_ZERO_DUMP}"
    "storeOutput\\.f32[^\r\n]*i8 2, float 0\\.000000e\\+00"
    "zero-depth vertex output")
require_regex(
    "${FULLSCREEN_ONE_DUMP}"
    "storeOutput\\.f32[^\r\n]*i8 2, float 1\\.000000e\\+00"
    "one-depth vertex output")

require_regex("${BLIT_DUMP}" "UV[ ]+0[ ]+xy" "blit UV input")
require_regex("${BLIT_DUMP}" "SV_Target[ ]+0[ ]+xyzw" "blit RGBA output")
require_regex("${BLIT_DUMP}" "s_LinearClamp[^\r\n]*s0" "blit sampler")
require_regex("${BLIT_DUMP}" "t_Source[^\r\n]*t0" "blit source")

foreach(member_offset pixelX=0 pixelY=4 padding0=8 padding1=12)
    string(REPLACE "=" ";" pair "${member_offset}")
    list(GET pair 0 member)
    list(GET pair 1 offset)
    require_regex(
        "${PIXEL_READBACK_DUMP}"
        "${member};[^\r\n]*Offset:[ ]*${offset}([\r\n]|$)"
        "readback ${member}@${offset}")
endforeach()
require_regex(
    "${PIXEL_READBACK_DUMP}"
    "c_Readback;[^\r\n]*Size:[ ]*16"
    "readback cbuffer size")
require_regex(
    "${PIXEL_READBACK_DUMP}"
    "NumThreads=\\(1,1,1\\)"
    "readback thread group")
require_regex(
    "${PIXEL_READBACK_DUMP}"
    "c_Readback[^\r\n]*cb0"
    "readback cbuffer binding")
require_regex(
    "${PIXEL_READBACK_DUMP}"
    "t_Source[^\r\n]*u32[^\r\n]*2d[^\r\n]*t0"
    "readback source binding")
require_regex(
    "${PIXEL_READBACK_DUMP}"
    "u_Destination[^\r\n]*UAV[^\r\n]*u32[^\r\n]*buf[^\r\n]*u0"
    "readback destination binding")

message(STATUS "Renderer common/readback DXIL reflection contract passed")
