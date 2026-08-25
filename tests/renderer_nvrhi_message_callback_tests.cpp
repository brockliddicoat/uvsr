#include "engine_startup.h"
#include "renderer_log.h"
#include "renderer_nvrhi_message_callback.h"

#include <Windows.h>
#include <crtdbg.h>
#include <process.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* FatalChildArgument = "--fatal-child";
    constexpr const char* FatalMessage =
        "ExecuteCommandLists Signal failed, HRESULT = 0x887a0005\n"
        "DRED breadcrumb[0]: list='visibility', completed=7/8";

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Renderer NVRHI callback test failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        std::string text{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        for (std::size_t position = 0u;
            (position = text.find("\r\n", position)) != std::string::npos;)
        {
            text.erase(position, 1u);
        }
        return text;
    }
}

int main(int argc, char** argv)
{
    using uvsr::RendererNvrhiMessageCallback;

    if (argc == 3 && std::string(argv[1]) == FatalChildArgument)
    {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        Require(
            uvsr::InitializeEngineDiagnosticLog(argv[2]),
            "fatal child could not initialize its durable log");
        RendererNvrhiMessageCallback callback;
        callback.message(nvrhi::MessageSeverity::Fatal, FatalMessage);
        return EXIT_SUCCESS;
    }

    Require(argc == 2, "expected one scratch-directory argument");
    std::vector<std::pair<uvsr::log::Severity, std::string>> messages;
    uvsr::log::SetMinimumSeverity(uvsr::log::Severity::Info);
    uvsr::log::SetCallback(
        [&](uvsr::log::Severity severity, const char* message)
        {
            messages.emplace_back(severity, message ? message : "");
        });

    RendererNvrhiMessageCallback callback;
    callback.message(nvrhi::MessageSeverity::Info, "device selected");
    callback.message(nvrhi::MessageSeverity::Warning, "heap pressure");
    callback.message(
        nvrhi::MessageSeverity::Error,
        "CreateGraphicsPipelineState failed, HRESULT = 0x887a0006\n"
        "DRED page-fault VA = 0x1234");
    Require(
        messages.size() == 3u &&
            messages[0] == std::pair{
                uvsr::log::Severity::Info,
                std::string("device selected") } &&
            messages[1] == std::pair{
                uvsr::log::Severity::Warning,
                std::string("heap pressure") } &&
            messages[2] == std::pair{
                uvsr::log::Severity::Error,
                std::string(
                    "CreateGraphicsPipelineState failed, HRESULT = "
                    "0x887a0006\nDRED page-fault VA = 0x1234") },
        "severity mapping or exact HRESULT/DRED text changed");
    uvsr::log::ResetCallback();

    const std::filesystem::path scratchDirectory = argv[1];
    std::filesystem::create_directories(scratchDirectory);
    const std::filesystem::path fatalLog =
        scratchDirectory / "fatal-nvrhi.log";
    std::error_code removeError;
    std::filesystem::remove(fatalLog, removeError);

    const std::string fatalLogArgument = fatalLog.string();
    const char* childArguments[] = {
        argv[0],
        FatalChildArgument,
        fatalLogArgument.c_str(),
        nullptr
    };
    const intptr_t childResult = _spawnv(
        _P_WAIT,
        argv[0],
        childArguments);
    Require(childResult != -1, "could not start the fatal callback child");
    Require(childResult != 0, "fatal callback returned a successful exit code");
    const std::string written = ReadText(fatalLog);
    Require(
        written.find("[fatal] " + std::string(FatalMessage)) !=
            std::string::npos,
        "fatal HRESULT/DRED text was not flushed before termination");

    return EXIT_SUCCESS;
}
