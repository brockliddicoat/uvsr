foreach(required
        UVSR_DXC_EXECUTABLE
        UVSR_AUTO_EXPOSURE_HISTOGRAM_DXIL
        UVSR_AUTO_EXPOSURE_RESOLVE_DXIL
        UVSR_AGX_UNITY_DXIL
        UVSR_AGX_AUTOMATIC_DXIL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

foreach(shader
        AUTO_EXPOSURE_HISTOGRAM
        AUTO_EXPOSURE_RESOLVE
        AGX_UNITY
        AGX_AUTOMATIC)
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
        message(FATAL_ERROR "Missing auto-exposure reflection: ${label}")
    endif()
endfunction()

function(reject_regex text pattern label)
    if("${text}" MATCHES "${pattern}")
        message(FATAL_ERROR "Unexpected auto-exposure reflection: ${label}")
    endif()
endfunction()

foreach(shader AUTO_EXPOSURE_HISTOGRAM AUTO_EXPOSURE_RESOLVE)
    require_regex(
        "${${shader}_DUMP}"
        "c_AutoExposure;[^\r\n]*Size:[ ]*48"
        "${shader} constant-buffer size")
    require_regex(
        "${${shader}_DUMP}"
        "c_AutoExposure[^\r\n]*cb0[ ]+1"
        "${shader} constants b0")
    foreach(member_offset
            viewOrigin=0
            viewSize=8
            frameDeltaSeconds=16
            exposureCompensationEV=20
            adjustmentPeriodSeconds=24
            resetExposure=28
            maximumBrighteningEV=32
            maximumDarkeningEV=36
            padding=40)
        string(REPLACE "=" ";" pair "${member_offset}")
        list(GET pair 0 member)
        list(GET pair 1 offset)
        require_regex(
            "${${shader}_DUMP}"
            "${member};[^\r\n]*Offset:[ ]*${offset}([\r\n]|$)"
            "${shader} ${member}@${offset}")
    endforeach()
endforeach()

require_regex(
    "${AUTO_EXPOSURE_HISTOGRAM_DUMP}"
    "t_SceneColor[^\r\n]*f32[^\r\n]*2d[^\r\n]*t0[ ]+1"
    "histogram scene input t0")
require_regex(
    "${AUTO_EXPOSURE_HISTOGRAM_DUMP}"
    "u_Histogram[^\r\n]*UAV[^\r\n]*u32[^\r\n]*buf[^\r\n]*u0[ ]+1"
    "histogram output u0")
require_regex(
    "${AUTO_EXPOSURE_HISTOGRAM_DUMP}"
    "NumThreads=\\(16,16,1\\)"
    "histogram thread group")

require_regex(
    "${AUTO_EXPOSURE_RESOLVE_DUMP}"
    "t_Histogram[^\r\n]*u32[^\r\n]*buf[^\r\n]*t0[ ]+1"
    "resolve histogram input t0")
require_regex(
    "${AUTO_EXPOSURE_RESOLVE_DUMP}"
    "u_Exposure[^\r\n]*UAV[^\r\n]*f32[^\r\n]*buf[^\r\n]*u0[ ]+1"
    "resolve exposure output u0")
require_regex(
    "${AUTO_EXPOSURE_RESOLVE_DUMP}"
    "NumThreads=\\(1,1,1\\)"
    "resolve single thread")

foreach(shader AGX_UNITY AGX_AUTOMATIC)
    require_regex(
        "${${shader}_DUMP}"
        "t_SceneColor[^\r\n]*f32[^\r\n]*2d[^\r\n]*t0[ ]+1"
        "${shader} scene input t0")
    require_regex(
        "${${shader}_DUMP}"
        "SV_Target[ ]+0[ ]+xyzw"
        "${shader} RGBA output")
endforeach()
reject_regex(
    "${AGX_UNITY_DUMP}"
    "t_AutoExposure"
    "unity AgX exposure resource")
require_regex(
    "${AGX_AUTOMATIC_DUMP}"
    "t_AutoExposure[^\r\n]*f32[^\r\n]*buf[^\r\n]*t1[ ]+1"
    "automatic AgX exposure t1")

message(STATUS "Auto-exposure and AgX DXIL reflection contract passed")
