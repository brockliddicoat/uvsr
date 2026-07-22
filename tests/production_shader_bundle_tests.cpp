#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace
{
    bool Check(bool condition, const std::string& message)
    {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }

    size_t CountOccurrences(
        const std::string& text,
        const std::string& needle)
    {
        size_t count = 0u;
        size_t offset = 0u;
        while ((offset = text.find(needle, offset)) != std::string::npos)
        {
            ++count;
            offset += needle.size();
        }
        return count;
    }

    std::set<std::string> GetExpectedShaderFiles()
    {
        constexpr const char* rootShaders[] = {
            "blit_ps",
            "fullscreen_vs",
            "imgui_pixel",
            "imgui_vertex",
            "rect_vs",
            "sharpen_ps",
            "skinning_cs"
        };
        constexpr const char* passShaders[] = {
            "deferred_lighting_cs",
            "forward_ps",
            "forward_vs_buffer_loads",
            "gbuffer_ps",
            "gbuffer_vs_buffer_loads",
            "material_id_ps",
            "pixel_readback_cs",
            "sky_ps"
        };
        constexpr const char* appShaders[] = {
            "agx_tonemapping_ps",
            "backdrop_blur_ps",
            "cmaa2_ComputeDispatchArgsCS",
            "cmaa2_DeferredColorApply2x2CS",
            "cmaa2_EdgesColor2x2CS",
            "cmaa2_ProcessCandidatesCS",
            "msaa_visibility_resolve_cs",
            "pbr_deferred_lighting_cs",
            "pbr_deferred_lighting_msaa_cs",
            "pbr_forward_ps",
            "pbr_gbuffer_ps",
            "pixel_zoom_ps",
            "screen_space_depth_hierarchy_cs",
            "screen_space_indirect_composite_cs",
            "screen_space_visibility_bounce_control_cs",
            "screen_space_visibility_composed_edges_cs",
            "screen_space_visibility_composed_packed_fast_cs",
            "screen_space_visibility_filter_cs",
            "screen_space_visibility_filter_packed_edge_cs",
            "screen_space_visibility_fixed_cs",
            "screen_space_visibility_fused_apply_cs",
            "screen_space_visibility_packed_fast_cs",
            "screen_space_visibility_packed_fast_edges_cs",
            "screen_space_visibility_cs",
            "screen_space_visibility_temporal_cs",
            "taa_miniengine_blend_cs",
            "taa_miniengine_resolve_cs",
            "taa_miniengine_sharpen_cs"
        };

        std::set<std::string> expected;
        for (const char* shader : rootShaders)
        {
            expected.insert(
                "framework/dxil/" +
                std::string(shader) +
                ".bin");
        }
        for (const char* shader : passShaders)
        {
            expected.insert(
                "framework/dxil/passes/" +
                std::string(shader) +
                ".bin");
        }
        for (const char* shader : appShaders)
        {
            expected.insert(
                "uvsr/dxil/" +
                std::string(shader) +
                ".bin");
        }
        return expected;
    }
}

int main(int argc, char** argv)
{
    bool passed = Check(
        argc == 4,
        "expected production config, runtime manifest, and staged shader tree");
    if (!passed)
        return 1;

    const std::filesystem::path configPath = argv[1];
    const std::filesystem::path manifestPath = argv[2];
    const std::filesystem::path stageRoot = argv[3];
    passed &= Check(
        std::filesystem::is_regular_file(configPath),
        "production shader config must exist");
    passed &= Check(
        std::filesystem::is_regular_file(manifestPath),
        "generated production runtime manifest must exist");
    passed &= Check(
        std::filesystem::is_directory(stageRoot),
        "production runtime shader tree must be staged");
    if (!passed)
        return 1;

    const std::string config = ReadText(configPath);
    const std::string manifest = ReadText(manifestPath);
    constexpr const char* forbiddenShaders[] = {
        "smaa",
        "SMAA"
    };
    for (const char* shader : forbiddenShaders)
    {
        passed &= Check(
            config.find(shader) == std::string::npos &&
                manifest.find(shader) == std::string::npos,
            std::string("developer shader must be absent from production: ") +
                shader);
    }

    passed &= Check(
        config.find("src/shaders.cfg") == std::string::npos &&
            manifest.find("src/shaders_production.cfg") !=
                std::string::npos,
        "production manifest must identify only the production shader config");
    passed &= Check(
        CountOccurrences(
            config,
            "taa_miniengine_blend_cs.hlsl") == 3u,
        "production must compile exactly the three long-term temporal preset bundles");
    passed &= Check(
        CountOccurrences(config, "TAA_EXPORT_SELECTIVE=0") == 3u &&
            CountOccurrences(config, "TAA_SAMPLE_RESURRECTION=0") == 3u &&
            CountOccurrences(config, "TAA_DEVELOPER_DEBUG=0") == 3u &&
            config.find("TAA_EXPORT_SELECTIVE={") ==
                std::string::npos &&
            config.find("TAA_DEVELOPER_DEBUG=1") ==
                std::string::npos &&
            config.find("TAA_PIXEL_SHADER") == std::string::npos,
        "production TAA bundles must disable removed selective morphology output and omit resurrection, debug, and pixel experiments");
    passed &= Check(
        CountOccurrences(config, "cmaa2.hlsl -T cs") == 4u &&
            CountOccurrences(
                config,
                "CMAA2_STATIC_QUALITY_PRESET={0,1,2,3}") == 4u,
        "production must retain all four official CMAA2 stages for Low, Medium, High, and Ultra");
    passed &= Check(
        CountOccurrences(
            config,
            "taa_miniengine_sharpen_cs.hlsl") == 1u &&
            config.find(
                "TAA_SHARPEN_INPUT_PREMULTIPLIED={0,1}") !=
                std::string::npos,
        "production must package distinct premultiplied-history and resolved-presentation sharpen permutations");
    passed &= Check(
        CountOccurrences(
            config,
            "pbr_deferred_lighting_msaa_cs.hlsl") == 1u &&
            config.find(
                "PBR_DEFERRED_MSAA_SAMPLES={2,4,8,16}") !=
                std::string::npos &&
            config.find(
                "PBR_DEFERRED_MSAA_VISIBILITY={0,1}") !=
                std::string::npos &&
            config.find(
                "msaa_visibility_resolve_cs.hlsl -T cs -E main -D MSAA_VISIBILITY_SAMPLES={2,4,8,16}") !=
                std::string::npos,
        "production must retain static 2x, 4x, 8x, and 16x deferred MSAA lighting and coherent visibility-resolve permutations");

    std::set<std::string> stagedFiles;
    for (const std::filesystem::directory_entry& entry :
        std::filesystem::recursive_directory_iterator(stageRoot))
    {
        if (!entry.is_regular_file() ||
            entry.path().extension() != ".bin")
        {
            continue;
        }
        stagedFiles.insert(
            std::filesystem::relative(
                entry.path(),
                stageRoot).generic_string());
    }
    const std::set<std::string> expectedFiles =
        GetExpectedShaderFiles();
    if (stagedFiles != expectedFiles)
    {
        std::vector<std::string> missing;
        std::vector<std::string> unexpected;
        std::set_difference(
            expectedFiles.begin(),
            expectedFiles.end(),
            stagedFiles.begin(),
            stagedFiles.end(),
            std::back_inserter(missing));
        std::set_difference(
            stagedFiles.begin(),
            stagedFiles.end(),
            expectedFiles.begin(),
            expectedFiles.end(),
            std::back_inserter(unexpected));
        for (const std::string& path : missing)
            std::cerr << "MISSING: " << path << '\n';
        for (const std::string& path : unexpected)
            std::cerr << "UNEXPECTED: " << path << '\n';
        passed = false;
    }

    if (!passed)
        return 1;
    std::cout << "Production shader bundle contract passed\n";
    return 0;
}
