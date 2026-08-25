foreach(required
        UVSR_DXC_EXECUTABLE
        UVSR_SOURCE_DIRECTORY
        UVSR_TEST_DIRECTORY
        UVSR_PBR_NORMAL_DXIL
        UVSR_PBR_MSAA_DXIL
        UVSR_SCREEN_SPACE_COMPOSITE_DXIL
        UVSR_SKY_SINGLE_DXIL
        UVSR_SKY_MSAA_DXIL
        UVSR_FLASHLIGHT_VISIBILITY_DXIL
        UVSR_FLASHLIGHT_HIT_DISTANCE_DXIL
        UVSR_DIRECTIONAL_VISIBILITY_DXIL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

set(reflection_directory
    "${UVSR_TEST_DIRECTORY}/dxil/pbr-sky-reflection")
file(MAKE_DIRECTORY "${reflection_directory}")

function(compile_contract output source entry)
    execute_process(
        COMMAND "${UVSR_DXC_EXECUTABLE}"
            -nologo -T cs_6_5 -E "${entry}"
            -D TARGET_D3D12 -enable-16bit-types -O3 -WX
            -I "${UVSR_SOURCE_DIRECTORY}/src"
            -Fo "${output}"
            ${ARGN}
            "${UVSR_SOURCE_DIRECTORY}/${source}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "PBR/sky reflection compile failed for ${source}: ${error}")
    endif()
endfunction()

function(dump_contract path output)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing reflection DXIL: ${path}")
    endif()
    execute_process(
        COMMAND "${UVSR_DXC_EXECUTABLE}" -dumpbin "${path}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE dump
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "DXC could not reflect ${path}: ${error}")
    endif()
    set(${output} "${dump}" PARENT_SCOPE)
endfunction()

function(require_regex text pattern label)
    if(NOT "${text}" MATCHES "${pattern}")
        message(FATAL_ERROR "Missing PBR/sky reflection: ${label}")
    endif()
endfunction()

function(reject_regex text pattern label)
    if("${text}" MATCHES "${pattern}")
        message(FATAL_ERROR "Unexpected PBR/sky reflection: ${label}")
    endif()
endfunction()

function(require_inline_queries text label expected_count)
    foreach(operation
            rayQuery_TraceRayInline
            rayQuery_CommitNonOpaqueTriangleHit)
        string(REGEX MATCHALL
            "call[^\r\n]*@dx\\.op\\.${operation}"
            calls
            "${text}")
        list(LENGTH calls count)
        if(NOT count EQUAL expected_count)
            message(FATAL_ERROR
                "${label} requires ${expected_count} ${operation} calls; found ${count}")
        endif()
    endforeach()

    string(REGEX MATCHALL
        "call[^\r\n]*@dx\\.op\\.rayQuery_Proceed"
        proceed_calls
        "${text}")
    list(LENGTH proceed_calls proceed_count)
    math(EXPR expected_proceed_count "${expected_count} * 2")
    if(NOT proceed_count EQUAL expected_proceed_count)
        message(FATAL_ERROR
            "${label} requires ${expected_proceed_count} rayQuery_Proceed calls; found ${proceed_count}")
    endif()

    string(REGEX MATCHALL
        "call[ \t]+i32[ \t]+@dx\\.op\\.rayQuery_StateScalar\\.i32\\(i32[ \t]+184,"
        committed_status_calls
        "${text}")
    list(LENGTH committed_status_calls committed_status_count)
    if(NOT committed_status_count EQUAL expected_count)
        message(FATAL_ERROR
            "${label} requires ${expected_count} committed-status opcode-184 calls; found ${committed_status_count}")
    endif()
    require_regex(
        "${text}"
        "RayQuery_CommittedStatus"
        "${label} committed-status annotation")
endfunction()

function(require_pbr_environment text label)
    require_regex(
        "${text}"
        "t_DiffuseEnvironment[^\r\n]*f32[^\r\n]*cubearray[^\r\n]*t1[ ]+1"
        "${label} diffuse environment cube array t1")
    require_regex(
        "${text}"
        "t_SpecularEnvironment[^\r\n]*f32[^\r\n]*cubearray[^\r\n]*t2[ ]+1"
        "${label} specular environment cube array t2")
    require_regex(
        "${text}"
        "t_EnvironmentBrdf[^\r\n]*f32[^\r\n]*2d[^\r\n]*t3[ ]+1"
        "${label} environment BRDF 2D t3")
endfunction()

function(require_pbr_visibility text label topology)
    foreach(binding
            "t_FlashlightVisibility;t20"
            "t_SunVisibility;t21"
            "t_SkyVisibility;t22")
        list(GET binding 0 name)
        list(GET binding 1 slot)
        require_regex(
            "${text}"
            "${name}[^\r\n]*f32[^\r\n]*${topology}[^\r\n]*${slot}[ ]+1"
            "${label} ${name} ${topology} ${slot}")
    endforeach()
endfunction()

function(require_sky_bindings text label sample_count hit_distance)
    require_regex(
        "${text}"
        "c_RayTracedSkyVisibility[^\r\n]*cb0[ ]+1"
        "${label} constants b0")
    require_regex(
        "${text}"
        "c_RayTracedSkyVisibility;[^\r\n]*Size:[ ]*768"
        "${label} constant ABI")
    require_regex(
        "${text}"
        "s_RayMaterialSampler[^\r\n]*sampler[^\r\n]*s0[ ]+1"
        "${label} material sampler s0")
    foreach(binding
            "t_RayMaterialGeometries;t10"
            "t_RayMaterials;t11"
            "t_RayGeometryIndexMap;t12"
            "t_WorldBvh;t0"
            "t_Depth;t1"
            "t_GBufferMaterial;t2"
            "t_GBufferNormals;t3"
            "t_Noise;t4"
            "t_AttemptMask;t5"
            "u_Visibility;u0"
            "u_ClosestVisibility;u1")
        list(GET binding 0 name)
        list(GET binding 1 slot)
        require_regex(
            "${text}"
            "${name}[^\r\n]*${slot}[ ]+1"
            "${label} ${name} ${slot}")
    endforeach()
    require_regex(
        "${text}"
        "t_RayMaterialBuffers[^\r\n]*t0,space1unbounded"
        "${label} bindless raw buffers")
    require_regex(
        "${text}"
        "t_RayMaterialTextures[^\r\n]*t0,space2unbounded"
        "${label} bindless textures")
    require_regex(
        "${text}"
        "t_WorldBvh[^\r\n]*ras[^\r\n]*t0[ ]+1"
        "${label} TLAS topology")
    require_regex("${text}" "NumThreads=\\(8,8,1\\)" "${label} threads")

    if(sample_count EQUAL 1)
        set(input_topology "2d")
        set(output_topology "2d")
    else()
        set(input_topology "2dMS${sample_count}")
        set(output_topology "2darray")
    endif()
    foreach(name t_Depth t_GBufferMaterial t_GBufferNormals)
        require_regex(
            "${text}"
            "${name}[^\r\n]*${input_topology}[^\r\n]*t[123]"
            "${label} ${name} ${input_topology}")
    endforeach()
    require_regex(
        "${text}"
        "u_Visibility[^\r\n]*f32[^\r\n]*${output_topology}[^\r\n]*u0[ ]+1"
        "${label} visibility ${output_topology}")
    if(hit_distance)
        require_regex(
            "${text}"
            "u_HitDistance[^\r\n]*f32[^\r\n]*2d[^\r\n]*u2[ ]+1"
            "${label} hit distance u2")
    else()
        reject_regex(
            "${text}"
            "u_HitDistance[^\r\n]*u2"
            "${label} disabled hit distance")
    endif()
    require_inline_queries("${text}" "${label}" ${sample_count})
endfunction()

function(require_directional_bindings text label sample_count)
    require_regex(
        "${text}"
        "c_DirectionalVisibility[^\r\n]*cb0[ ]+1"
        "${label} constants b0")
    require_regex(
        "${text}"
        "c_DirectionalVisibility;[^\r\n]*Size:[ ]*752"
        "${label} constant ABI")
    require_regex(
        "${text}"
        "s_RayMaterialSampler[^\r\n]*sampler[^\r\n]*s0[ ]+1"
        "${label} material sampler s0")
    foreach(binding
            "t_RayMaterialGeometries;t10"
            "t_RayMaterials;t11"
            "t_RayGeometryIndexMap;t12"
            "t_WorldBvh;t0"
            "t_Depth;t1"
            "t_Material;t2"
            "t_Normals;t3"
            "u_Visibility;u0"
            "u_ClosestVisibility;u1"
            "u_ClosestHitDistance;u2")
        list(GET binding 0 name)
        list(GET binding 1 slot)
        require_regex(
            "${text}"
            "${name}[^\r\n]*${slot}[ ]+1"
            "${label} ${name} ${slot}")
    endforeach()
    require_regex(
        "${text}"
        "t_RayMaterialBuffers[^\r\n]*t0,space1unbounded"
        "${label} bindless raw buffers")
    require_regex(
        "${text}"
        "t_RayMaterialTextures[^\r\n]*t0,space2unbounded"
        "${label} bindless textures")
    require_regex(
        "${text}"
        "t_WorldBvh[^\r\n]*ras[^\r\n]*t0[ ]+1"
        "${label} TLAS topology")
    if(sample_count EQUAL 1)
        set(input_topology "2d")
        set(output_topology "2d")
    else()
        set(input_topology "2dMS${sample_count}")
        set(output_topology "2darray")
    endif()
    foreach(name t_Depth t_Material t_Normals)
        require_regex(
            "${text}"
            "${name}[^\r\n]*${input_topology}[^\r\n]*t[123]"
            "${label} ${name} ${input_topology}")
    endforeach()
    require_regex(
        "${text}"
        "u_Visibility[^\r\n]*f32[^\r\n]*${output_topology}[^\r\n]*u0[ ]+1"
        "${label} visibility ${output_topology}")
    foreach(name u_ClosestVisibility u_ClosestHitDistance)
        require_regex(
            "${text}"
            "${name}[^\r\n]*f32[^\r\n]*2d[^\r\n]*u[12][ ]+1"
            "${label} ${name} 2d")
    endforeach()
    require_regex("${text}" "NumThreads=\\(8,8,1\\)" "${label} threads")
    require_inline_queries("${text}" "${label}" ${sample_count})
endfunction()

# Normal PBR: retain both source-radiance layouts.
set(PBR_NORMAL_0_DXIL "${UVSR_PBR_NORMAL_DXIL}")
set(PBR_NORMAL_1_DXIL
    "${reflection_directory}/pbr-normal-source.dxil")
compile_contract(
    "${PBR_NORMAL_1_DXIL}"
    src/pbr_deferred_lighting_cs.hlsl main
    -D WRITE_SOURCE_RADIANCE=1)
foreach(write_source 0 1)
    dump_contract("${PBR_NORMAL_${write_source}_DXIL}" dump)
    require_pbr_environment("${dump}" "PBR_NORMAL_${write_source}")
    require_pbr_visibility("${dump}" "PBR_NORMAL_${write_source}" "2d")
    require_regex("${dump}" "NumThreads=\\(16,16,1\\)"
        "PBR_NORMAL_${write_source} threads")
    if(write_source EQUAL 1)
        require_regex("${dump}" "u_SourceRadiance[^\r\n]*u1[ ]+1"
            "PBR normal source output u1")
    else()
        reject_regex("${dump}" "u_SourceRadiance[^\r\n]*u1"
            "PBR normal disabled source output")
    endif()
endforeach()

# PBR MSAA: reflect every supported receiver count with visibility off/on.
foreach(sample_count 2 4 8 16)
    foreach(visibility 0 1)
        if(sample_count EQUAL 4 AND visibility EQUAL 1)
            set(path "${UVSR_PBR_MSAA_DXIL}")
        else()
            set(path
                "${reflection_directory}/pbr-msaa-${sample_count}-visibility-${visibility}.dxil")
            compile_contract(
                "${path}"
                src/pbr_deferred_lighting_msaa_cs.hlsl main
                -D PBR_DEFERRED_MSAA_SAMPLES=${sample_count}
                -D PBR_DEFERRED_MSAA_VISIBILITY=${visibility})
        endif()
        dump_contract("${path}" dump)
        set(label "PBR_MSAA_${sample_count}_VISIBILITY_${visibility}")
        require_pbr_environment("${dump}" "${label}")
        require_pbr_visibility("${dump}" "${label}" "2darray")
        foreach(name
                t_GBufferDepth t_GBuffer0 t_GBuffer1 t_GBuffer2
                t_GBuffer3 t_MaterialAmbientOcclusion)
            require_regex(
                "${dump}"
                "${name}[^\r\n]*2dMS${sample_count}"
                "${label} ${name} sample topology")
        endforeach()
        foreach(binding
                "t_FlashlightRawClosestVisibility;t23"
                "t_FlashlightDenoisedClosestVisibility;t24"
                "t_SkyRawClosestVisibility;t25"
                "t_SkyDenoisedClosestVisibility;t26"
                "t_SunRawClosestVisibility;t27"
                "t_SunDenoisedClosestVisibility;t28")
            list(GET binding 0 name)
            list(GET binding 1 slot)
            require_regex(
                "${dump}"
                "${name}[^\r\n]*f32[^\r\n]*2d[^\r\n]*${slot}[ ]+1"
                "${label} ${name} ${slot}")
        endforeach()
        if(visibility EQUAL 1)
            require_regex("${dump}" "t_VisibilityBaseLighting[^\r\n]*t18[ ]+1"
                "${label} base lighting t18")
            require_regex("${dump}" "t_VisibilityComposite[^\r\n]*t19[ ]+1"
                "${label} composite t19")
        else()
            reject_regex("${dump}" "t_Visibility(BaseLighting|Composite)"
                "${label} disabled visibility composite resources")
        endif()
        require_regex("${dump}" "NumThreads=\\(16,16,1\\)"
            "${label} threads")
    endforeach()
endforeach()

# Sky: both output layouts at every supported raster sample count.
foreach(sample_count 1 2 4 8 16)
    foreach(hit_distance 0 1)
        if(sample_count EQUAL 1 AND hit_distance EQUAL 0)
            set(path "${UVSR_SKY_SINGLE_DXIL}")
        elseif(sample_count EQUAL 4 AND hit_distance EQUAL 1)
            set(path "${UVSR_SKY_MSAA_DXIL}")
        else()
            set(path
                "${reflection_directory}/sky-${sample_count}-hit-${hit_distance}.dxil")
            compile_contract(
                "${path}"
                src/ray_traced_sky_visibility_cs.hlsl Generate
                -D OUTPUT_HIT_DISTANCE=${hit_distance}
                -D SKY_VISIBILITY_SAMPLES=${sample_count}
                -res-may-alias)
        endif()
        dump_contract("${path}" dump)
        require_sky_bindings(
            "${dump}"
            "SKY_${sample_count}_HIT_${hit_distance}"
            ${sample_count}
            ${hit_distance})
    endforeach()
endforeach()

# Separate-composite IBL/sky bindings remain distinct and ordered by meaning.
dump_contract("${UVSR_SCREEN_SPACE_COMPOSITE_DXIL}" composite_dump)
foreach(binding
        "t_DiffuseEnvironment;cubearray;t7"
        "t_SpecularEnvironment;cubearray;t10"
        "t_EnvironmentBrdf;2d;t11"
        "t_SkyVisibility;2d;t12")
    list(GET binding 0 name)
    list(GET binding 1 topology)
    list(GET binding 2 slot)
    require_regex(
        "${composite_dump}"
        "${name}[^\r\n]*f32[^\r\n]*${topology}[^\r\n]*${slot}[ ]+1"
        "separate composite ${name} ${topology} ${slot}")
endforeach()
require_regex("${composite_dump}" "NumThreads=\\(8,8,1\\)"
    "separate composite threads")

# Flashlight reflection remains the paired direct-light contract: one query
# per receiver sample, with the same material/bindless topology as sky.
foreach(item
        "FLASHLIGHT_VISIBILITY;${UVSR_FLASHLIGHT_VISIBILITY_DXIL};1;0"
        "FLASHLIGHT_HIT_DISTANCE;${UVSR_FLASHLIGHT_HIT_DISTANCE_DXIL};4;1")
    list(GET item 0 label)
    list(GET item 1 path)
    list(GET item 2 sample_count)
    list(GET item 3 hit_distance)
    dump_contract("${path}" dump)
    require_regex("${dump}" "c_FlashlightShadows[^\r\n]*cb0[ ]+1"
        "${label} constants b0")
    require_regex("${dump}" "c_FlashlightShadows;[^\r\n]*Size:[ ]*832"
        "${label} constant ABI")
    require_regex("${dump}" "t_WorldBvh[^\r\n]*ras[^\r\n]*t0[ ]+1"
        "${label} TLAS topology")
    require_regex("${dump}" "t_RayMaterialBuffers[^\r\n]*t0,space1unbounded"
        "${label} bindless raw buffers")
    require_regex("${dump}" "t_RayMaterialTextures[^\r\n]*t0,space2unbounded"
        "${label} bindless textures")
    require_inline_queries("${dump}" "${label}" ${sample_count})
    if(hit_distance)
        require_regex("${dump}" "u_HitDistance[^\r\n]*u2[ ]+1"
            "${label} hit distance u2")
    else()
        reject_regex("${dump}" "u_HitDistance[^\r\n]*u2"
            "${label} disabled hit distance")
    endif()
endforeach()

# Directional visibility must preserve the exact per-raster-sample topology and
# publish both closest-receiver visibility and physical blocker distance.
foreach(sample_count 1 2 4 8 16)
    if(sample_count EQUAL 1)
        set(path "${UVSR_DIRECTIONAL_VISIBILITY_DXIL}")
    else()
        set(path
            "${reflection_directory}/directional-${sample_count}.dxil")
        compile_contract(
            "${path}"
            src/directional_ray_visibility_cs.hlsl main
            -D DIRECTIONAL_VISIBILITY_SAMPLES=${sample_count}
            -res-may-alias)
    endif()
    dump_contract("${path}" dump)
    require_directional_bindings(
        "${dump}" "DIRECTIONAL_${sample_count}" ${sample_count})
endforeach()

message(STATUS
    "PBR/sky/directional DXIL bindings and exact sample variants passed")
