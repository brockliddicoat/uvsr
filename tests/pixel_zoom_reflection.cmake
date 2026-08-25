foreach(required
        UVSR_DXC_EXECUTABLE
        UVSR_SOURCE_DIRECTORY
        UVSR_TEST_DIRECTORY)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${UVSR_TEST_DIRECTORY}")
set(dxil "${UVSR_TEST_DIRECTORY}/pixel-zoom.dxil")
execute_process(
    COMMAND "${UVSR_DXC_EXECUTABLE}"
        -nologo -T ps_6_5 -E main -O3 -WX
        -I "${UVSR_SOURCE_DIRECTORY}/src"
        -Fo "${dxil}"
        "${UVSR_SOURCE_DIRECTORY}/src/pixel_zoom_ps.hlsl"
    RESULT_VARIABLE compile_result
    ERROR_VARIABLE compile_error)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "Pixel Zoom shader compilation failed: ${compile_error}")
endif()

execute_process(
    COMMAND "${UVSR_DXC_EXECUTABLE}" -dumpbin "${dxil}"
    RESULT_VARIABLE reflection_result
    OUTPUT_VARIABLE reflection
    ERROR_VARIABLE reflection_error)
if(NOT reflection_result EQUAL 0)
    message(FATAL_ERROR
        "Pixel Zoom reflection failed: ${reflection_error}")
endif()
file(WRITE "${UVSR_TEST_DIRECTORY}/pixel-zoom-reflection.txt"
    "${reflection}")

function(require_regex pattern label)
    if(NOT "${reflection}" MATCHES "${pattern}")
        message(FATAL_ERROR "Missing Pixel Zoom reflection: ${label}")
    endif()
endfunction()

function(reject_regex pattern label)
    if("${reflection}" MATCHES "${pattern}")
        message(FATAL_ERROR "Unexpected Pixel Zoom reflection: ${label}")
    endif()
endfunction()

require_regex(
    "SV_Position[ ]+0[ ]+xyzw[ ]+0[ ]+POS[ ]+float[ ]+xy"
    "SV_Position input")
require_regex("UV[ ]+0[ ]+xy[ ]+1[ ]+NONE[ ]+float"
    "UV input")
require_regex(
    "SV_Target[ ]+0[ ]+xyzw[ ]+0[ ]+TARGET[ ]+float[ ]+xyzw"
    "SV_Target output")
require_regex("c_PixelZoom;[^\r\n]*Size:[ ]*96"
    "96-byte constant ABI")
require_regex("c_PixelZoom[^\r\n]*cbuffer[^\r\n]*cb0[ ]+1"
    "constant buffer b0")
require_regex("t_Source[^\r\n]*texture[^\r\n]*2d[^\r\n]*t0[ ]+1"
    "source Texture2D t0")

string(REGEX MATCHALL
    "call[^\r\n]*@dx\\.op\\.textureLoad\\.f32"
    texture_load_calls
    "${reflection}")
list(LENGTH texture_load_calls texture_load_count)
if(NOT texture_load_count EQUAL 1)
    message(FATAL_ERROR
        "Pixel Zoom requires one integer texture load; found ${texture_load_count}")
endif()
string(REGEX MATCHALL
    "call[^\r\n]*@dx\\.op\\.discard"
    discard_calls
    "${reflection}")
list(LENGTH discard_calls discard_count)
if(NOT discard_count EQUAL 1)
    message(FATAL_ERROR
        "Pixel Zoom requires one rounded-cutout discard; found ${discard_count}")
endif()
reject_regex("call[^\r\n]*@dx\\.op\\.sample" "filtered texture sample")
reject_regex("[^\r\n]*sampler[^\r\n]*s[0-9]+[ ]+[0-9]+"
    "sampler binding")

message(STATUS "Pixel Zoom DXC reflection contract passed")
