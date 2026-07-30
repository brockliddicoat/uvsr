#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

#include "../src/renderer_statistics.h"

namespace
{
    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        std::ostringstream contents;
        contents << stream.rdbuf();
        std::string source = contents.str();
        source.erase(
            std::remove(source.begin(), source.end(), '\r'),
            source.end());
        return source;
    }

    std::string_view ExtractSection(
        std::string_view source,
        std::string_view begin,
        std::string_view end)
    {
        const size_t beginPosition = source.find(begin);
        if (beginPosition == std::string_view::npos)
            return {};
        const size_t endPosition = source.find(
            end, beginPosition + begin.size());
        if (endPosition == std::string_view::npos)
            return {};
        return source.substr(
            beginPosition, endPosition - beginPosition);
    }

    bool ExpectContains(
        std::string_view source,
        std::string_view required,
        const char* contract)
    {
        if (source.find(required) != std::string_view::npos)
            return true;
        std::cerr << "FAIL: " << contract << " must contain '"
                  << required << "'.\n";
        return false;
    }

    bool ExpectAbsent(
        std::string_view source,
        std::string_view forbidden,
        const char* contract)
    {
        if (source.find(forbidden) == std::string_view::npos)
            return true;
        std::cerr << "FAIL: " << contract << " must not contain '"
                  << forbidden << "'.\n";
        return false;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: uvsr_renderer_source_contract_tests <root>\n";
        return 2;
    }

    const std::filesystem::path root = argv[1];
    const std::string viewer = ReadFile(root / "src/uvsr.cpp");
    const std::string cmaa = ReadFile(root / "src/cmaa2.cpp");
    bool passed = true;

    const std::string_view refresh = ExtractSection(
        viewer,
        "void RefreshAntiAliasingTargetPasses(bool sampleCountChanged)",
        "void CreateRenderPasses()");
    passed &= ExpectContains(
        refresh,
        "m_PbrDeferredLightingPass->ResetBindingCache();",
        "AA-only target refresh");
    passed &= ExpectContains(
        refresh,
        "m_ScreenSpaceVisibilityPass->ResetBindingCache();",
        "AA-only target refresh");
    passed &= ExpectContains(
        refresh,
        "m_ScreenSpaceVisibilityPass->ResetHistory();",
        "AA-only target refresh history invalidation");
    passed &= ExpectContains(
        refresh,
        "m_Cmaa2Pass->UpdateSourceColor(",
        "CMAA2 resource retention");
    passed &= ExpectAbsent(
        refresh,
        "std::make_unique<ScreenSpaceVisibilityPass>",
        "AA-only target refresh");
    passed &= ExpectAbsent(
        refresh,
        "std::make_unique<PbrDeferredLightingPass>",
        "AA-only target refresh");

    passed &= ExpectContains(
        viewer,
        "sameNonAaTopology && antiAliasingTopologyChanged",
        "AA-only topology classification");
    const std::string_view msaaSampleCountResolution = ExtractSection(
        viewer,
        "static uint32_t ResolveSupportedMsaaSampleCount(",
        "class RenderTargets : public GBufferRenderTargets");
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "bool enablePbr",
        "MSAA allocation-aware PBR format selection");
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "nvrhi::utils::ChooseFormat(",
        "MSAA exact Donut depth-format selection");
    for (const std::string_view format : {
             std::string_view("DXGI_FORMAT_R8G8B8A8_UNORM_SRGB"),
             std::string_view("DXGI_FORMAT_D24_UNORM_S8_UINT"),
             std::string_view("DXGI_FORMAT_D32_FLOAT_S8X24_UINT"),
             std::string_view("DXGI_FORMAT_D32_FLOAT"),
             std::string_view("DXGI_FORMAT_D16_UNORM") })
    {
        passed &= ExpectContains(
            msaaSampleCountResolution,
            format,
            "MSAA exact allocated format");
    }
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "supportsFormat(selectedDepthDxgiFormat, sampleCount)",
        "MSAA selected depth-format validation");
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "struct MsaaSampleCountCache",
        "MSAA per-device query cache");
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "cache.device == nativeDevice &&\n"
        "        cache.requestedSampleCount == requestedSampleCount &&\n"
        "        cache.enablePbr == enablePbr",
        "MSAA query cache topology key");
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "return cache.resolvedSampleCount;",
        "MSAA cached per-frame resolution");
    passed &= ExpectAbsent(
        msaaSampleCountResolution,
        "std::any_of(",
        "MSAA any-depth false-positive acceptance");
    passed &= ExpectContains(
        viewer,
        ".rasterSampleCount,\n"
        "                        m_ui.EnablePbr);",
        "MSAA PBR topology routing");
    passed &= ExpectContains(
        viewer,
        "RefreshAntiAliasingTargetPasses(\n"
        "                    antiAliasingSampleCountChanged);",
        "AA-only refresh dispatch");
    passed &= ExpectContains(
        cmaa,
        "void Cmaa2Pass::UpdateSourceColor(",
        "CMAA2 source rebinding");
    passed &= ExpectContains(
        cmaa,
        "RebuildBindingSet(sourceColor);",
        "CMAA2 source rebinding");

    const std::string_view commandLine = ExtractSection(
        viewer,
        "bool ProcessCommandLine(",
        "std::string FormatExperimentLaunchTime(");
    passed &= ExpectContains(
        commandLine,
        "else if (!strcmp(argv[i], \"--aa-rectification\"))",
        "AA rectification benchmark option");
    for (const std::string_view mode : {
            std::string_view("\"pair-rgb\""),
            std::string_view("\"per-pixel-rgb\""),
            std::string_view("\"per-pixel-ycocg\""),
            std::string_view("\"variance-ycocg\"") })
    {
        passed &= ExpectContains(
            commandLine,
            mode,
            "AA rectification benchmark mode");
    }
    passed &= ExpectContains(
        commandLine,
        "aaBenchmark.settings.algorithmOverrides.rectification",
        "AA rectification benchmark override routing");

    const std::string_view mouseButtonUpdate = ExtractSection(
        viewer,
        "virtual bool MouseButtonUpdate(",
        "virtual bool MouseScrollUpdate(");
    passed &= ExpectContains(
        mouseButtonUpdate,
        "button == GLFW_MOUSE_BUTTON_MIDDLE",
        "middle-button material picking");
    passed &= ExpectAbsent(
        mouseButtonUpdate,
        "GLFW_MOUSE_BUTTON_2",
        "right-button material picking");

    passed &= ExpectContains(
        viewer,
        "SubmittedTriangleCountingPass geometryPass(\n"
        "                *m_GBufferPass);",
        "deferred submitted-triangle accounting");
    passed &= ExpectContains(
        viewer,
        "SubmittedTriangleCountingPass geometryPass(\n"
        "                *m_ForwardPass);",
        "forward submitted-triangle accounting");
    passed &= ExpectContains(
        viewer,
        "m_SubmittedMainViewTriangles =\n"
        "                geometryPass.GetSubmittedTriangles();",
        "submitted-triangle publication");

    const auto expectTriangleFormat =
        [&passed](uint64_t count, const char* expected)
        {
            const std::string actual =
                uvsr::FormatTriangleCount(count);
            if (actual == expected)
                return;
            passed = false;
            std::cerr << "FAIL: triangle count " << count
                      << " formatted as '" << actual
                      << "', expected '" << expected << "'.\n";
        };
    expectTriangleFormat(0u, "0 tris");
    expectTriangleFormat(999u, "999 tris");
    expectTriangleFormat(1'000u, "1.0k tris");
    expectTriangleFormat(999'949u, "999.9k tris");
    expectTriangleFormat(999'950u, "1.0m tris");
    expectTriangleFormat(1'200'000u, "1.2m tris");
    expectTriangleFormat(999'949'999u, "999.9m tris");
    expectTriangleFormat(999'950'000u, "1.0b tris");
    expectTriangleFormat(
        999'950'000'000ull,
        "999.9b+ tris");

    const uint64_t maximumSubmittedTriangles =
        uvsr::CountSubmittedTriangleListPrimitives(
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max());
    passed &= maximumSubmittedTriangles ==
        uint64_t(
            std::numeric_limits<uint32_t>::max() / 3u) *
        uint64_t(std::numeric_limits<uint32_t>::max());
    passed &= uvsr::CountSubmittedTriangleListPrimitives(
        8u,
        3u) == 6u;

    if (!passed)
        return 1;
    std::cout << "UVSR renderer source contracts passed.\n";
    return 0;
}
