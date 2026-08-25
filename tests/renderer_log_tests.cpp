#include "engine_startup.h"
#include "renderer_log.h"

#include <chrono>
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
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Renderer log test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
    }

    std::size_t CountOccurrences(
        const std::string& text,
        const std::string& needle)
    {
        std::size_t count = 0u;
        std::size_t offset = 0u;
        while ((offset = text.find(needle, offset)) != std::string::npos)
        {
            ++count;
            offset += needle.size();
        }
        return count;
    }
}

int main(int argc, char** argv)
{
    Require(argc == 3,
        "expected scratch-directory and exact D3D12Core arguments");
    Require(uvsr::VerifyD3D12CoreFile(argv[2]),
        "the pinned D3D12Core bytes were rejected");
    const std::filesystem::path tamperedCore =
        std::filesystem::path(argv[1]) / "tampered-D3D12Core.dll";
    std::filesystem::create_directories(tamperedCore.parent_path());
    std::filesystem::copy_file(argv[2], tamperedCore,
        std::filesystem::copy_options::overwrite_existing);
    {
        std::fstream tamper(
            tamperedCore, std::ios::binary | std::ios::in | std::ios::out);
        tamper.seekp(0);
        tamper.put('\0');
    }
    Require(!uvsr::VerifyD3D12CoreFile(tamperedCore),
        "tampered D3D12Core bytes were accepted");
    std::vector<std::pair<uvsr::log::Severity, std::string>> messages;
    uvsr::log::SetCallback(
        [&](uvsr::log::Severity severity, const char* message)
        {
            messages.emplace_back(severity, message ? message : "");
        });
    uvsr::log::SetMinimumSeverity(uvsr::log::Severity::Info);
    uvsr::log::debug("filtered %d", 1);
    uvsr::log::info("identity %s %d", "value", 7);
    uvsr::log::warning("warning");
    Require(messages.size() == 2u,
        "minimum severity did not filter only debug output");
    Require(messages[0].first == uvsr::log::Severity::Info &&
            messages[0].second == "identity value 7" &&
            messages[1].first == uvsr::log::Severity::Warning &&
            messages[1].second == "warning",
        "severity or printf formatting drifted");

    const uvsr::log::Callback callback = uvsr::log::GetCallback();
    Require(static_cast<bool>(callback), "installed callback was not readable");

    const std::filesystem::path logPath =
        std::filesystem::path(argv[1]) / "nested" / "uvsr-engine.log";
    auto monotonicNow = std::chrono::steady_clock::time_point(
        std::chrono::seconds(100));
    Require(uvsr::InitializeEngineDiagnosticLog(
        logPath,
        [&monotonicNow]()
        {
            return monotonicNow;
        }),
        "explicit diagnostic-log initialization failed");
    Require(std::filesystem::is_regular_file(logPath),
        "diagnostic initialization did not create directory and file");

    uvsr::log::error("urgent flush known answer");
    Require(ReadText(logPath).find("[error] urgent flush known answer") !=
        std::string::npos,
        "error severity was not flushed before shutdown");

    uvsr::log::warning("coalesced warning");
    monotonicNow += std::chrono::seconds(1);
    uvsr::log::warning("coalesced warning");
    monotonicNow += std::chrono::seconds(1);
    uvsr::log::warning("coalesced warning");
    monotonicNow += std::chrono::seconds(3);
    uvsr::log::warning("coalesced warning");
    std::string written = ReadText(logPath);
    Require(CountOccurrences(written, "] coalesced warning") == 2u,
        "five-second repeat window changed");
    Require(written.find(
        "Previous warning repeated 2 additional times") !=
        std::string::npos,
        "expired repeat window did not emit its summary");

    monotonicNow += std::chrono::seconds(1);
    uvsr::log::warning("coalesced warning");
    uvsr::ShutdownEngineDiagnosticLog();
    written = ReadText(logPath);
    Require(written.find(
        "Previous warning repeated 1 additional times") !=
        std::string::npos,
        "shutdown did not emit the pending repeat summary");

    const std::size_t fileSizeAfterShutdown = written.size();
    uvsr::log::info("restored downstream");
    Require(ReadText(logPath).size() == fileSizeAfterShutdown,
        "shutdown left the file callback installed");
    Require(!messages.empty() &&
        messages.back().second == "restored downstream",
        "shutdown did not restore the prior callback");

    uvsr::log::SetCallback({});
    Require(static_cast<bool>(uvsr::log::GetCallback()),
        "empty callback did not restore the direct default");
    uvsr::log::SetMinimumSeverity(uvsr::log::Severity::Info);
    return EXIT_SUCCESS;
}
