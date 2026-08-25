foreach(required UVSR_IDENTITY_GENERATOR UVSR_TEST_DIRECTORY)
    if (NOT DEFINED "${required}" OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Build-identity test omits ${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${UVSR_TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${UVSR_TEST_DIRECTORY}")

set(clean_identity "0123456789abcdef0123456789abcdef01234567")
set(dirty_identity
    "0123456789abcdef0123456789abcdef01234567-dirty-89abcdef0123")

function(run_positive name identity production configuration)
    set(output_directory "${UVSR_TEST_DIRECTORY}/${name}")
    execute_process(
        COMMAND "${UVSR_IDENTITY_GENERATOR}"
            --cpp "${output_directory}/build_identity.cpp"
            --rc "${output_directory}/uvsr-engine.rc"
            --source-identity "${identity}"
            --production "${production}"
            --configuration "${configuration}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "${name} was rejected: ${error}")
    endif()
    file(READ "${output_directory}/build_identity.cpp" cpp)
    file(READ "${output_directory}/uvsr-engine.rc" resource)
    if (NOT cpp MATCHES
            "return ${production};" OR
        NOT cpp MATCHES
            "return \\\"${configuration}\\\";" OR
        NOT resource MATCHES
            "VALUE \\\"BuildConfiguration\\\", \\\"${configuration}\\\"" OR
        NOT resource MATCHES
            "VALUE \\\"ProductionBuild\\\", \\\"${production}\\\"")
        message(FATAL_ERROR "${name} emitted inconsistent build identity")
    endif()
endfunction()

function(run_negative name identity production configuration)
    set(output_directory "${UVSR_TEST_DIRECTORY}/${name}")
    execute_process(
        COMMAND "${UVSR_IDENTITY_GENERATOR}"
            --cpp "${output_directory}/build_identity.cpp"
            --rc "${output_directory}/uvsr-engine.rc"
            --source-identity "${identity}"
            --production "${production}"
            --configuration "${configuration}"
        RESULT_VARIABLE result
        OUTPUT_QUIET
        ERROR_QUIET)
    if (result EQUAL 0)
        message(FATAL_ERROR "${name} was incorrectly accepted")
    endif()
endfunction()

run_positive(clean_developer "${clean_identity}" false Release)
run_positive(dirty_developer "${dirty_identity}" false Debug)
run_positive(clean_production "${clean_identity}" true Release)
run_negative(dirty_production "${dirty_identity}" true Release)
run_negative(debug_production "${clean_identity}" true Debug)
