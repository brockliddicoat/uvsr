foreach(required
        UVSR_DXC_EXECUTABLE
        UVSR_SHADER_OBJECT_DIRECTORY
        UVSR_SHADER_CATALOG_DIRECTORY
        UVSR_TEST_DIRECTORY
        UVSR_FAST_APPROXIMATE_ATTRIBUTION
        UVSR_BSD_LICENSE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${UVSR_TEST_DIRECTORY}")

function(require_count values expected label)
    list(LENGTH values actual)
    if(NOT actual EQUAL expected)
        message(FATAL_ERROR
            "${label}: expected ${expected}, found ${actual}")
    endif()
endfunction()

function(require_occurrences text pattern expected label)
    string(REGEX MATCHALL "${pattern}" matches "${text}")
    list(LENGTH matches actual)
    if(NOT actual EQUAL expected)
        message(FATAL_ERROR
            "${label}: expected ${expected}, found ${actual}")
    endif()
endfunction()

function(require_pattern variable pattern label)
    if(NOT "${${variable}}" MATCHES "${pattern}")
        message(FATAL_ERROR "Missing temporal-AA reflection: ${label}")
    endif()
endfunction()

function(reflect_object path output)
    get_filename_component(name "${path}" NAME)
    execute_process(
        COMMAND "${UVSR_DXC_EXECUTABLE}" -dumpbin "${path}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE reflection
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "DXC could not reflect ${name}: ${error}")
    endif()
    file(WRITE "${UVSR_TEST_DIRECTORY}/${name}.txt" "${reflection}")
    set(${output} "${reflection}" PARENT_SCOPE)
endfunction()

file(GLOB blend_objects
    "${UVSR_SHADER_OBJECT_DIRECTORY}/temporal_aa_blend_cs.*.dxil")
file(GLOB minimum_objects
    "${UVSR_SHADER_OBJECT_DIRECTORY}/temporal_aa_minimum_cs.*.dxil")
file(GLOB resolve_objects
    "${UVSR_SHADER_OBJECT_DIRECTORY}/temporal_aa_resolve_cs.*.dxil")
file(GLOB sharpen_objects
    "${UVSR_SHADER_OBJECT_DIRECTORY}/temporal_aa_sharpen_cs.*.dxil")
file(GLOB fast_approximate_objects
    "${UVSR_SHADER_OBJECT_DIRECTORY}/fast_approximate_aa_ps.*.dxil")
require_count("${blend_objects}" 16 "TAA blend DXIL count")
require_count("${minimum_objects}" 2 "Minimum TAA DXIL count")
require_count("${resolve_objects}" 1 "TAA resolve DXIL count")
require_count("${sharpen_objects}" 2 "TAA sharpen DXIL count")
require_count("${fast_approximate_objects}" 1 "FXAA DXIL count")

file(GLOB catalogs "${UVSR_SHADER_CATALOG_DIRECTORY}/*.txt")
set(blend_catalog "")
set(minimum_catalog "")
set(sharpen_catalog "")
set(fast_approximate_catalog "")
foreach(catalog IN LISTS catalogs)
    file(READ "${catalog}" content)
    foreach(family blend minimum sharpen fast_approximate)
        if(family STREQUAL "blend")
            set(stem "temporal_aa_blend_cs")
        elseif(family STREQUAL "minimum")
            set(stem "temporal_aa_minimum_cs")
        elseif(family STREQUAL "sharpen")
            set(stem "temporal_aa_sharpen_cs")
        else()
            set(stem "fast_approximate_aa_ps")
        endif()
        if(content MATCHES "/objects/${stem}\\.")
            if(NOT "${${family}_catalog}" STREQUAL "")
                message(FATAL_ERROR "Duplicate generated ${family} catalog")
            endif()
            set(${family}_catalog "${content}")
        endif()
    endforeach()
endforeach()
foreach(family blend minimum sharpen fast_approximate)
    if("${${family}_catalog}" STREQUAL "")
        message(FATAL_ERROR "Missing generated ${family} catalog")
    endif()
endforeach()

require_occurrences("${blend_catalog}"
    "temporal_aa_blend_cs\\.[^\t\r\n]+\\.dxil\t" 16
    "expanded TAA blend rows")
require_occurrences("${blend_catalog}" "TAA_OPTIMIZED_COMPUTE=0" 8
    "baseline compute permutations")
require_occurrences("${blend_catalog}" "TAA_OPTIMIZED_COMPUTE=1" 8
    "packed compute permutations")
require_occurrences("${blend_catalog}" "TAA_FUSED_OUTPUT=0" 8
    "separate output permutations")
require_occurrences("${blend_catalog}" "TAA_FUSED_OUTPUT=1" 8
    "fused output permutations")
require_occurrences("${blend_catalog}" "TAA_MOTION_SOURCE=0" 4
    "center-motion recipe permutations")
require_occurrences("${blend_catalog}" "TAA_MOTION_SOURCE=2" 12
    "edge-dilation recipe permutations")
require_occurrences("${blend_catalog}" "TAA_HISTORY_FILTER=0" 8
    "bilinear recipe permutations")
require_occurrences("${blend_catalog}" "TAA_HISTORY_FILTER=1" 4
    "one-sample bicubic recipe permutations")
require_occurrences("${blend_catalog}" "TAA_HISTORY_FILTER=2" 4
    "five-tap recipe permutations")
require_occurrences("${blend_catalog}" "TAA_CURRENT_RECONSTRUCTION=1" 4
    "de-jittered recipe permutations")
require_occurrences("${blend_catalog}" "TAA_RECTIFICATION=1" 8
    "rectified recipe permutations")

set(retired_defines
    TAA_SAMPLE_RESURRECTION
    TAA_COMPUTE_KERNEL
    TAA_EXPORT_SELECTIVE
    TAA_LDS_LAYOUT
    TAA_SHARED_WORK_REUSE
    TAA_EARLY_HISTORY_REJECTION)
foreach(retired IN LISTS retired_defines)
    if(blend_catalog MATCHES "${retired}" OR
            minimum_catalog MATCHES "${retired}")
        message(FATAL_ERROR
            "Retired temporal-AA permutation returned: ${retired}")
    endif()
endforeach()
require_occurrences("${minimum_catalog}" "TAA_RUNTIME_BEHAVIOR=0" 1
    "static Minimum TAA default")
require_occurrences("${minimum_catalog}" "TAA_RUNTIME_BEHAVIOR=1" 1
    "runtime Minimum TAA override")
require_occurrences("${sharpen_catalog}"
    "TAA_SHARPEN_INPUT_PREMULTIPLIED=[01]" 2
    "TAA sharpen alpha-domain permutations")

foreach(object IN LISTS blend_objects)
    reflect_object("${object}" reflection)
    require_pattern(reflection "NumThreads=\\(8,8,1\\)" "8x8 blend group")
    foreach(binding
            "CB1[^\r\n]*cb1"
            "LinearSampler[^\r\n]*s0"
            "VelocityBuffer[^\r\n]*t0"
            "InColor[^\r\n]*t1"
            "InTemporal[^\r\n]*t2"
            "CurDepth[^\r\n]*t3"
            "PreDepth[^\r\n]*t4"
            "OutTemporal[^\r\n]*u0"
            "OutDepth[^\r\n]*u1"
            "dx\\.op\\.isSpecialFloat"
            "dx\\.op\\.textureStore")
        require_pattern(reflection "${binding}" "blend ${binding}")
    endforeach()
endforeach()

set(minimum_gather_count 0)
foreach(object IN LISTS minimum_objects)
    reflect_object("${object}" reflection)
    require_pattern(reflection "NumThreads=\\(8,8,1\\)" "8x8 Minimum group")
    foreach(binding
            "Constants[^\r\n]*cb1"
            "LinearSampler[^\r\n]*s0"
            "VelocityBuffer[^\r\n]*t0"
            "CurrentColor[^\r\n]*t1"
            "PreviousColor[^\r\n]*t2"
            "CurrentDepth[^\r\n]*t3"
            "PreviousDepth[^\r\n]*t4"
            "OutputColor[^\r\n]*u0"
            "OutputDepth[^\r\n]*u1"
            "dx\\.op\\.textureLoad"
            "dx\\.op\\.textureStore")
        require_pattern(reflection "${binding}" "Minimum ${binding}")
    endforeach()
    if(reflection MATCHES "dx\\.op\\.textureGather")
        math(EXPR minimum_gather_count "${minimum_gather_count} + 1")
    endif()
endforeach()
if(NOT minimum_gather_count EQUAL 1)
    message(FATAL_ERROR
        "Exactly one Minimum TAA behavior must retain footprint gathers")
endif()

foreach(object IN LISTS resolve_objects sharpen_objects)
    reflect_object("${object}" reflection)
    foreach(pattern
            "NumThreads=\\(8,8,1\\)"
            "InlineConstants[^\r\n]*cb0"
            "TemporalColor[^\r\n]*t0"
            "OutColor[^\r\n]*u0"
            "dx\\.op\\.textureLoad"
            "dx\\.op\\.textureStore")
        require_pattern(reflection "${pattern}" "output ${pattern}")
    endforeach()
endforeach()

list(GET fast_approximate_objects 0 fast_approximate_object)
reflect_object("${fast_approximate_object}" reflection)
foreach(pattern
        "SV_Position[^\r\n]*float"
        "UV[^\r\n]*float"
        "SV_Target[^\r\n]*float"
        "FastApproximateAaConstants[^\r\n]*cb0"
        "s_LinearClamp[^\r\n]*s0"
        "t_DisplayLinear[^\r\n]*t0"
        "FastApproximateAaConstants[^\r\n]*Size:[ ]*32"
        "dx\\.op\\.dot3"
        "dx\\.op\\.unary\\.f32\\(i32 24"
        "0x3F10000000000000"
        "dx\\.op\\.storeOutput\\.f32")
    require_pattern(reflection "${pattern}" "FXAA ${pattern}")
endforeach()

file(READ "${UVSR_FAST_APPROXIMATE_ATTRIBUTION}" attribution)
file(READ "${UVSR_BSD_LICENSE}" bsd_license)
foreach(pattern
        "47c86eec22e56d75897e16651eb4d2abd64fc29a"
        "NVIDIA CORPORATION. ALL RIGHTS RESERVED"
        "filament/include/filament/Options.h"
        "filament/src/PostProcessManager.cpp"
        "filament/src/PostProcessManager.h")
    if(NOT attribution MATCHES "${pattern}")
        message(FATAL_ERROR "Missing Filament FXAA attribution: ${pattern}")
    endif()
endforeach()
foreach(pattern "BSD 2-Clause License" "Morgan McGuire")
    if(NOT bsd_license MATCHES "${pattern}")
        message(FATAL_ERROR "Missing FXAA license provenance: ${pattern}")
    endif()
endforeach()

message(STATUS
    "Temporal AA/FXAA expanded catalog, DXIL, and provenance contract passed")
