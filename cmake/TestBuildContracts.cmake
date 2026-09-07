cmake_minimum_required(VERSION 3.24)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_SOURCE_DIRECTORY=${UVSR_SOURCE_DIRECTORY}"
        -P "${UVSR_SOURCE_DIRECTORY}/cmake/VerifyPostmortems.cmake"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_SOURCE_DIRECTORY=${UVSR_SOURCE_DIRECTORY}"
        "-DUVSR_BUILD_DIRECTORY=${UVSR_BUILD_DIRECTORY}"
        -P "${UVSR_SOURCE_DIRECTORY}/cmake/TestPostmortems.cmake"
    COMMAND_ERROR_IS_FATAL ANY)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_IDENTITY_GENERATOR=${UVSR_IDENTITY_GENERATOR}"
        "-DUVSR_TEST_DIRECTORY=${UVSR_BUILD_DIRECTORY}/identity-generator-test"
        -P "${UVSR_SOURCE_DIRECTORY}/cmake/TestBuildIdentityGenerator.cmake"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_VERIFIER=${UVSR_SOURCE_DIRECTORY}/cmake/VerifyDirectDependencyState.cmake"
        "-DUVSR_GIT_EXECUTABLE=${UVSR_GIT_EXECUTABLE}"
        "-DUVSR_TEST_ROOT=${UVSR_BUILD_DIRECTORY}/direct-dependency-state-self-test-${UVSR_CONFIGURATION}"
        -P "${UVSR_SOURCE_DIRECTORY}/tools/verify_direct_dependency_state_tests.cmake"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${UVSR_OVERRIDE_VALIDATOR}" --self-test
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${UVSR_OVERRIDE_VALIDATOR}" --check "${UVSR_SOURCE_DIRECTORY}"
    COMMAND_ERROR_IS_FATAL ANY)
