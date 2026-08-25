foreach(required UVSR_SOURCE_DIRECTORY UVSR_GIT_EXECUTABLE
        UVSR_PRODUCTION_BUILD UVSR_BUILD_CONFIGURATION)
    if (NOT DEFINED "${required}" OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Build-identity refresh omits ${required}")
    endif()
endforeach()
if (NOT UVSR_BUILD_CONFIGURATION MATCHES
        "^(Release|Debug|RelWithDebInfo|MinSizeRel)$")
    message(FATAL_ERROR "Build identity has an invalid configuration")
endif()
if (UVSR_PRODUCTION_BUILD AND
    NOT UVSR_BUILD_CONFIGURATION STREQUAL "Release")
    message(FATAL_ERROR
        "Production engine and package targets require Release configuration")
endif()

include("${UVSR_SOURCE_DIRECTORY}/cmake/SourceIdentity.cmake")
uvsr_compute_source_identity(
    "${UVSR_SOURCE_DIRECTORY}" "${UVSR_GIT_EXECUTABLE}"
    "${UVSR_PRODUCTION_BUILD}"
    source_identity source_tree_clean source_commit)

if (DEFINED UVSR_EXPECTED_SOURCE_IDENTITY)
    if (NOT source_identity STREQUAL UVSR_EXPECTED_SOURCE_IDENTITY)
        message(FATAL_ERROR
            "Configured source identity ${UVSR_EXPECTED_SOURCE_IDENTITY} changed to ${source_identity}; reconfigure")
    endif()
    return()
endif()

foreach(required UVSR_IDENTITY_GENERATOR UVSR_IDENTITY_CPP UVSR_IDENTITY_RC)
    if (NOT DEFINED "${required}" OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Build-identity refresh omits ${required}")
    endif()
endforeach()
execute_process(
    COMMAND "${UVSR_IDENTITY_GENERATOR}"
        --cpp "${UVSR_IDENTITY_CPP}"
        --rc "${UVSR_IDENTITY_RC}"
        --source-identity "${source_identity}"
        --production "${UVSR_PRODUCTION_BUILD}"
        --configuration "${UVSR_BUILD_CONFIGURATION}"
    RESULT_VARIABLE generator_result
    ERROR_VARIABLE generator_error)
if (NOT generator_result EQUAL 0)
    message(FATAL_ERROR
        "Build-identity generator failed: ${generator_error}")
endif()
message(STATUS "Verified build source identity ${source_identity}")
