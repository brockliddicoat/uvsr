foreach(input UVSR_TEST_EXECUTABLE UVSR_TEST_DIRECTORY UVSR_D3D12_CORE)
    if (NOT DEFINED ${input} OR NOT IS_ABSOLUTE "${${input}}")
        message(FATAL_ERROR "diagnostic test requires an absolute ${input}")
    endif()
endforeach()

# this fixture replaces its app-local runtime. each run owns a fresh binary directory.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(run_directory "${UVSR_TEST_DIRECTORY}/run-${run_id}")
if (EXISTS "${run_directory}")
    message(FATAL_ERROR "diagnostic test directory already exists: ${run_directory}")
endif()
file(MAKE_DIRECTORY "${run_directory}")
get_filename_component(executable_name "${UVSR_TEST_EXECUTABLE}" NAME)
set(executable "${run_directory}/${executable_name}")
file(COPY_FILE "${UVSR_TEST_EXECUTABLE}" "${executable}")
execute_process(
    COMMAND "${executable}" "${run_directory}/scratch" "${UVSR_D3D12_CORE}"
    WORKING_DIRECTORY "${run_directory}"
    RESULT_VARIABLE result)
if (NOT result STREQUAL "0")
    message(FATAL_ERROR "diagnostic test failed (${result}); evidence: ${run_directory}")
endif()
message(STATUS "diagnostic test passed; evidence: ${run_directory}")
