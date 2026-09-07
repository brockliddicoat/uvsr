cmake_minimum_required(VERSION 3.24)

execute_process(
    COMMAND "${UVSR_EXPORT_VALIDATOR}" --self-test "${UVSR_ENGINE}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${UVSR_LEGAL_VALIDATOR}" "${UVSR_SOURCE_DIRECTORY}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${UVSR_FEED_VALIDATOR}" --self-test
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${UVSR_PACKAGE_VALIDATOR}" --self-test
        --shader-inventory "${UVSR_SHADER_INVENTORY}"
        --asset-map "${UVSR_ASSET_MAP}"
        --d3d12-core "${UVSR_D3D12_CORE}"
    COMMAND_ERROR_IS_FATAL ANY)
