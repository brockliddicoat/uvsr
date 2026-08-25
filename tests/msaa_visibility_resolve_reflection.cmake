foreach(required
        UVSR_DXC_EXECUTABLE
        UVSR_MSAA_VISIBILITY_2X_DXIL
        UVSR_MSAA_VISIBILITY_4X_DXIL
        UVSR_MSAA_VISIBILITY_8X_DXIL
        UVSR_MSAA_VISIBILITY_16X_DXIL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

foreach(samples 2 4 8 16)
    if(NOT EXISTS "${UVSR_MSAA_VISIBILITY_${samples}X_DXIL}")
        message(FATAL_ERROR "Missing ${samples}x MSAA visibility DXIL")
    endif()
    execute_process(
        COMMAND "${UVSR_DXC_EXECUTABLE}" -dumpbin
            "${UVSR_MSAA_VISIBILITY_${samples}X_DXIL}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE dump
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "DXC could not reflect ${samples}x MSAA visibility: ${error}")
    endif()

    foreach(binding
            "t_Depth;t0;f32"
            "t_Diffuse;t1;f32"
            "t_Material;t2;f32"
            "t_Normals;t3;f32"
            "t_Emissive;t4;f32"
            "t_MaterialAmbientOcclusion;t5;f32"
            "t_MotionVectors;t6;f32")
        string(REPLACE ";" ";" parts "${binding}")
        list(GET parts 0 name)
        list(GET parts 1 slot)
        list(GET parts 2 component)
        if(NOT "${dump}" MATCHES
                "${name}[^\r\n]*${component}[^\r\n]*2dMS${samples}[^\r\n]*${slot}[ ]+1")
            message(FATAL_ERROR
                "Missing ${samples}x input ${name} at ${slot}")
        endif()
    endforeach()
    foreach(binding
            "u_Depth;u0;f32"
            "u_Diffuse;u1;f32"
            "u_Material;u2;f32"
            "u_Normals;u3;f32"
            "u_Emissive;u4;f32"
            "u_MaterialAmbientOcclusion;u5;f32"
            "u_MotionVectors;u6;f32")
        string(REPLACE ";" ";" parts "${binding}")
        list(GET parts 0 name)
        list(GET parts 1 slot)
        list(GET parts 2 component)
        if(NOT "${dump}" MATCHES
                "${name}[^\r\n]*${component}[^\r\n]*2d[^\r\n]*${slot}[ ]+1")
            message(FATAL_ERROR
                "Missing ${samples}x output ${name} at ${slot}")
        endif()
    endforeach()
    if(NOT "${dump}" MATCHES "NumThreads=\\(8,8,1\\)")
        message(FATAL_ERROR
            "MSAA visibility ${samples}x thread group is not 8x8x1")
    endif()
endforeach()

message(STATUS "MSAA visibility 2x/4x/8x/16x reflection contract passed")
