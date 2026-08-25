foreach(required
        UVSR_DXC_EXECUTABLE
        UVSR_CONTRACT_CS_DXIL
        UVSR_CONTRACT_PS_DXIL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

foreach(shader CS PS)
    if(NOT EXISTS "${UVSR_CONTRACT_${shader}_DXIL}")
        message(FATAL_ERROR "Missing ${shader} contract probe DXIL")
    endif()
    execute_process(
        COMMAND "${UVSR_DXC_EXECUTABLE}"
            -dumpbin "${UVSR_CONTRACT_${shader}_DXIL}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE dump
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "DXC could not reflect the ${shader} contract probe: ${error}")
    endif()
    set(${shader}_DUMP "${dump}")
endforeach()

function(require_regex text pattern label)
    if(NOT "${text}" MATCHES "${pattern}")
        message(FATAL_ERROR "Missing renderer GPU reflection contract: ${label}")
    endif()
endfunction()

function(require_member text member offset label)
    require_regex(
        "${text}"
        "${member}(\\[[0-9]+\\])?;[^\r\n]*Offset:[ ]*${offset}([\r\n]|$)"
        "${label}.${member}@${offset}")
endfunction()

set(planar_members
    matWorldToView=0
    matViewToClip=64
    matWorldToClip=128
    matClipToView=192
    matViewToWorld=256
    matClipToWorld=320
    matViewToClipNoOffset=384
    matWorldToClipNoOffset=448
    matClipToViewNoOffset=512
    matClipToWorldNoOffset=576
    viewportOrigin=640
    viewportSize=648
    viewportSizeInv=656
    pixelOffset=664
    clipToWindowScale=672
    clipToWindowBias=680
    windowToClipScale=688
    windowToClipBias=696
    cameraDirectionOrPosition=704)
set(material_members
    baseOrDiffuseColor=0
    flags=12
    specularColor=16
    materialID=28
    emissiveColor=32
    domain=44
    opacity=48
    roughness=52
    metalness=56
    normalTextureScale=60
    occlusionStrength=64
    alphaCutoff=68
    transmissionFactor=72
    baseOrDiffuseTextureIndex=76
    metalRoughOrSpecularTextureIndex=80
    emissiveTextureIndex=84
    normalTextureIndex=88
    occlusionTextureIndex=92
    transmissionTextureIndex=96
    opacityTextureIndex=100
    normalTextureTransformScale=104
    padding1=112
    sssScale=124
    sssTransmissionColor=128
    sssAnisotropy=140
    sssScatteringColor=144
    hairMelanin=156
    hairBaseColor=160
    hairMelaninRedness=172
    hairLongitudinalRoughness=176
    hairAzimuthalRoughness=180
    hairIor=184
    hairCuticleAngle=188
    hairDiffuseReflectionTint=192
    hairDiffuseReflectionWeight=204)
set(geometry_members
    numIndices=0
    numVertices=4
    indexBufferIndex=8
    indexOffset=12
    vertexBufferIndex=16
    positionOffset=20
    prevPositionOffset=24
    texCoord1Offset=28
    texCoord2Offset=32
    normalOffset=36
    tangentOffset=40
    curveRadiusOffset=44
    materialIndex=48
    pad0=52
    pad1=56
    pad2=60)
set(instance_members
    flags=0
    firstGeometryInstanceIndex=4
    firstGeometryIndex=8
    numGeometries=12
    transform=16
    prevTransform=64)

foreach(contract planar material geometry instance)
    foreach(member_offset IN LISTS ${contract}_members)
        string(REPLACE "=" ";" pair "${member_offset}")
        list(GET pair 0 member)
        list(GET pair 1 offset)
        require_member("${CS_DUMP}" "${member}" "${offset}" "${contract}")
    endforeach()
endforeach()

set(global_members
    shadowFadeScale=992
    shadowFadeBias=1000
    shadowMapCenterUV=1008
    shadowFalloffDistance=1016
    shadowMapArrayIndex=1020
    shadowMapSizeTexels=1024
    shadowMapSizeTexelsInv=1032
    direction=1040
    lightType=1052
    position=1056
    radius=1068
    color=1072
    intensity=1084
    angularSizeOrInvRange=1088
    innerAngle=1092
    outerAngle=1096
    outOfBoundsShadow=1100
    shadowCascades=1104
    perObjectShadows=1120
    shadowChannel=1136
    diffuseScale=1152
    specularScale=1156
    mipLevels=1160
    diffuseArrayIndex=1168
    specularArrayIndex=1172
    frustumPlanes=1184
    startInstanceLocation=1280
    startVertexLocation=1284
    positionOffset=1288
    prevPositionOffset=1292
    texCoordOffset=1296
    normalOffset=1300
    tangentOffset=1304
    shadowMapTextureSize=720
    enableAmbientOcclusion=728
    ambientColorTop=736
    ambientColorBottom=752
    numLights=768
    numLightProbes=772
    indirectDiffuseScale=776
    indirectSpecularScale=780
    randomOffset=784
    noisePattern=800)
foreach(member_offset IN LISTS global_members)
    string(REPLACE "=" ";" pair "${member_offset}")
    list(GET pair 0 member)
    list(GET pair 1 offset)
    require_member("${CS_DUMP}" "${member}" "${offset}" "cbuffer")
endforeach()

require_regex("${CS_DUMP}" "g_View;[^\r\n]*Offset:[ ]*0" "Planar start")
require_regex("${CS_DUMP}" "g_Material;[^\r\n]*Offset:[ ]*720" "Material start")
require_regex("${CS_DUMP}" "g_Shadow;[^\r\n]*Offset:[ ]*928" "Shadow start")
require_regex("${CS_DUMP}" "g_Light;[^\r\n]*Offset:[ ]*1040" "Light start")
require_regex("${CS_DUMP}" "g_Probe;[^\r\n]*Offset:[ ]*1152" "Probe start")
require_regex("${CS_DUMP}" "g_Push;[^\r\n]*Offset:[ ]*1280" "Push start")
require_regex("${CS_DUMP}" "c_Probe;[^\r\n]*Size:[ ]*1308" "combined cbuffer size")
require_regex("${CS_DUMP}" "g_Deferred;[^\r\n]*Size:[ ]*6496" "deferred cbuffer size")
require_regex("${CS_DUMP}" "g_GBuffer;[^\r\n]*Size:[ ]*1440" "GBuffer cbuffer size")
require_regex("${CS_DUMP}" "lights\\[16\\];[^\r\n]*Offset:[ ]*864" "light array")
require_regex("${CS_DUMP}" "shadows\\[16\\];[^\r\n]*Offset:[ ]*2656" "shadow array")
require_regex("${CS_DUMP}" "lightProbes\\[16\\];[^\r\n]*Offset:[ ]*4448" "probe array")
require_regex("${CS_DUMP}" "viewPrev;[^\r\n]*Offset:[ ]*720" "previous view")
foreach(size 64 112 208)
    require_regex("${CS_DUMP}" "\\$Element;[^\r\n]*Size:[ ]*${size}" "structured stride ${size}")
endforeach()

foreach(binding
        "c_Probe[^\r\n]*cb0"
        "g_Deferred[^\r\n]*cb1"
        "g_GBuffer[^\r\n]*cb2"
        "s_Shadow[^\r\n]*s1"
        "t_Shadow[^\r\n]*t7"
        "t_Geometry[^\r\n]*t8"
        "t_Instance[^\r\n]*t9"
        "t_Materials[^\r\n]*t10"
        "u_Output[^\r\n]*u0")
    require_regex("${CS_DUMP}" "${binding}" "resource ${binding}")
endforeach()

foreach(signature
        "POS[ ]+0[ ]+xyz"
        "PREV_POS[ ]+0[ ]+xyz"
        "TEXCOORD[ ]+0[ ]+xy"
        "NORMAL[ ]+0[ ]+xyz"
        "TANGENT[ ]+0[ ]+xyzw"
        "NORMAL[ ]+0[^\r\n]*centroid"
        "TANGENT[ ]+0[^\r\n]*centroid")
    require_regex("${PS_DUMP}" "${signature}" "pixel input ${signature}")
endforeach()

message(STATUS "Renderer GPU DXIL reflection contract passed")
