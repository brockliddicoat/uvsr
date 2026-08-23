#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "D3D12 portability source-contract validation failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    std::string ReadSource(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot open " + path.generic_string());
        return std::string{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    std::string_view ExtractBalancedScope(
        std::string_view source,
        std::string_view anchor)
    {
        const size_t anchorPosition = source.find(anchor);
        Require(anchorPosition != std::string_view::npos,
            "missing scope anchor " + std::string(anchor));
        const size_t openPosition = source.find('{', anchorPosition);
        Require(openPosition != std::string_view::npos,
            "missing opening brace for " + std::string(anchor));
        size_t depth = 0u;
        for (size_t position = openPosition; position < source.size(); ++position)
        {
            if (source[position] == '{')
                ++depth;
            else if (source[position] == '}' && --depth == 0u)
            {
                return source.substr(
                    anchorPosition,
                    position - anchorPosition + 1u);
            }
        }
        Fail("unterminated scope for " + std::string(anchor));
    }

    void RequireContains(
        std::string_view source,
        std::string_view token,
        const std::string& message)
    {
        Require(source.find(token) != std::string_view::npos, message);
    }

    void RequireAbsent(
        std::string_view source,
        std::string_view token,
        const std::string& message)
    {
        Require(source.find(token) == std::string_view::npos, message);
    }

    size_t CountOccurrences(
        std::string_view source,
        std::string_view token)
    {
        size_t count = 0u;
        size_t cursor = 0u;
        while ((cursor = source.find(token, cursor)) !=
            std::string_view::npos)
        {
            ++count;
            cursor += token.size();
        }
        return count;
    }
}

int main(int argc, char** argv)
{
    Require(argc == 2,
        "expected the UVSR source root as the only argument");
    const std::filesystem::path root = argv[1];
    const std::string viewer = ReadSource(root / "src/uvsr.cpp");
    const std::string shaderConfig = ReadSource(root / "src/shaders.cfg");
    const std::string portabilityPatch = ReadSource(
        root / "overrides/nvrhi-d3d12-portability.patch");
    const std::string diagnosticsOverride = ReadSource(
        root / "overrides/nvrhi-d3d12-diagnostics.h");
    const std::string buildSystem = ReadSource(root / "CMakeLists.txt");

    const std::string_view viewerConstructor = ExtractBalancedScope(
        viewer, "UvsrSceneViewer(");
    RequireAbsent(
        viewerConstructor,
        "std::make_unique<PathTracingPass>",
        "fresh startup must not compile optional path-tracing PSOs");
    const std::string_view ensurePathTracing = ExtractBalancedScope(
        viewer, "void EnsurePathTracingPass()");
    RequireContains(
        ensurePathTracing,
        "std::make_unique<PathTracingPass>",
        "path tracing must be materialized by its on-demand gate");
    RequireContains(
        viewer,
        "if (pathTracingSelected)\n            EnsurePathTracingPass();",
        "selected path tracing must enter the on-demand gate");
    RequireContains(
        viewer,
        "pathTracingSelected && bool(m_PathTracingPass);",
        "shader reload must not recreate path-tracing PSOs after returning to the baseline renderer");
    RequireContains(
        viewer,
        "deviceParams.featureLevel = D3D_FEATURE_LEVEL_11_0;",
        "device creation must use the universal D3D12 feature-level baseline");
    RequireContains(
        viewer,
        "SupportsBindlessResourceTables(resourceBindingTier)",
        "bindless creation must be gated on Resource Binding Tier 2");
    const std::string_view winMain = ExtractBalancedScope(
        viewer, "int WINAPI WinMain(");
    Require(
        winMain.find("ConfigureD3D12DeviceRemovedDiagnostics(") <
            winMain.find("SelectGraphicsAdapter("),
        "DRED/debug configuration must precede adapter probe devices");
    RequireContains(
        viewer,
        "uvsr-engine.log",
        "engine failures must be preserved in a durable per-user log");
    RequireContains(
        viewer,
        "g_EngineLogSuppressedRepeatCount",
        "repeated renderer warnings must not grow the durable log every frame");

    RequireContains(
        portabilityPatch,
        "for (const D3D12_DESCRIPTOR_RANGE1& range : layout->descriptorRanges)",
        "every unbounded bindless range must become its own table parameter");
    RequireContains(
        portabilityPatch,
        "rootParameterOffset + rangeIndex, tableBase",
        "every split table parameter must receive the shared heap base");
    RequireContains(
        portabilityPatch,
        "D3D_ROOT_SIGNATURE_VERSION_1_0",
        "root-signature 1.0 fallback must remain available");
    RequireContains(
        portabilityPatch,
        "rsDesc10.Desc_1_0.pStaticSamplers",
        "root-signature 1.0 fallback must preserve immutable samplers");
    RequireContains(
        portabilityPatch,
        "GetDeviceRemovedReason",
        "D3D12 object failures must preserve the device-removed reason");
    RequireContains(
        diagnosticsOverride,
        "GetAutoBreadcrumbsOutput1",
        "device removal must persist DRED automatic breadcrumbs");
    RequireContains(
        diagnosticsOverride,
        "GetPageFaultAllocationOutput1",
        "device removal must persist DRED page-fault allocations");
    Require(
        CountOccurrences(portabilityPatch, "MessageSeverity::Fatal") == 2u,
        "a removed device must terminate at the first root-signature or compute-PSO failure");
    RequireContains(
        portabilityPatch,
        "if (!builtRootSignature)",
        "failed root signatures must stop dependent object creation");
    RequireContains(
        portabilityPatch,
        "if (!pRS)",
        "pipeline creation must stop after root-signature failure");
    for (const std::string_view source : {
            "src/d3d12/d3d12-graphics.cpp",
            "src/d3d12/d3d12-meshlets.cpp",
            "src/d3d12/d3d12-raytracing.cpp" })
    {
        Require(
            CountOccurrences(buildSystem, source) >= 2u,
            "every root-signature caller override must replace the pinned source");
    }
    Require(
        CountOccurrences(shaderConfig, "-res-may-alias") == 8u,
        "every shader family using the aliased bindless table must opt in to resource aliasing");

    RequireContains(
        buildSystem,
        "overrides/nvrhi-d3d12-portability.patch",
        "the portability override must be part of configuration");
    RequireContains(
        buildSystem,
        "overrides/nvrhi-d3d12-diagnostics.h",
        "the DRED serializer must be copied beside the staged NVRHI sources");
    Require(
        CountOccurrences(
            buildSystem,
            "src/d3d12/d3d12-resource-bindings.cpp") >= 2u,
        "the staged resource-binding override must replace the pinned source");
    Require(
        CountOccurrences(
            buildSystem,
            "src/d3d12/d3d12-compute.cpp") >= 2u,
        "the staged compute override must replace the pinned source");

    return EXIT_SUCCESS;
}
