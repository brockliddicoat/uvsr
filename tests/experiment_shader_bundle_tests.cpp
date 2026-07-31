#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
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

    uint64_t CountShaderPermutations(const std::string& config)
    {
        std::istringstream lines(config);
        std::string line;
        uint64_t total = 0u;
        while (std::getline(lines, line))
        {
            const size_t first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos || line[first] == '#')
                continue;

            uint64_t linePermutations = 1u;
            size_t offset = first;
            while ((offset = line.find('{', offset)) != std::string::npos)
            {
                const size_t end = line.find('}', offset + 1u);
                if (end == std::string::npos)
                    return 0u;

                uint64_t values = 1u;
                for (size_t index = offset + 1u; index < end; ++index)
                {
                    if (line[index] == ',')
                        ++values;
                }
                linePermutations *= values;
                offset = end + 1u;
            }
            total += linePermutations;
        }
        return total;
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
            "depth_ps",
            "depth_vs_buffer_loads",
            "forward_ps",
            "forward_vs_buffer_loads",
            "gbuffer_ps",
            "gbuffer_vs_buffer_loads",
            "light_probe_cubemap_gs",
            "light_probe_diffuse_probe_ps",
            "light_probe_environment_brdf_ps",
            "light_probe_mip_ps",
            "light_probe_specular_probe_ps",
            "material_id_ps",
            "pixel_readback_cs"
        };
        constexpr const char* appShaders[] = {
            "agx_tonemapping_ps",
            "backdrop_blur_ps",
            "image_based_lighting_background_ps",
            "msaa_visibility_resolve_cs",
            "pbr_deferred_lighting_cs",
            "pbr_deferred_lighting_msaa_cs",
            "pbr_forward_ps",
            "pbr_gbuffer_ps",
            "pixel_zoom_ps",
            "screen_space_depth_hierarchy_cs",
            "screen_space_indirect_composite_cs",
            "screen_space_visibility_bounce_control_cs",
            "screen_space_visibility_cs",
            "screen_space_visibility_filter_cs",
            "screen_space_visibility_temporal_cs",
            "temporal_aa_blend_cs",
            "temporal_aa_minimum_cs",
            "temporal_aa_resolve_cs",
            "temporal_aa_sharpen_cs"
        };

        std::set<std::string> expected;
        for (const char* shader : rootShaders)
        {
            expected.emplace(
                "framework/dxil/" + std::string(shader) + ".bin");
        }
        for (const char* shader : passShaders)
        {
            expected.emplace(
                "framework/dxil/passes/" +
                std::string(shader) +
                ".bin");
        }
        for (const char* shader : appShaders)
        {
            expected.emplace(
                "uvsr/dxil/" + std::string(shader) + ".bin");
        }
        return expected;
    }
}

int main(int argc, char** argv)
{
    bool passed = Check(
        argc == 4,
        "expected experiment config, runtime manifest, and staged shader tree");
    if (!passed)
        return 1;

    const std::filesystem::path configPath = argv[1];
    const std::filesystem::path manifestPath = argv[2];
    const std::filesystem::path stageRoot = argv[3];
    passed &= Check(
        std::filesystem::is_regular_file(configPath),
        "factory experiment shader config must exist");
    passed &= Check(
        std::filesystem::is_regular_file(manifestPath),
        "factory experiment runtime manifest must exist");
    passed &= Check(
        std::filesystem::is_directory(stageRoot),
        "factory experiment runtime shader tree must be staged");
    if (!passed)
        return 1;

    const std::string config = ReadText(configPath);
    const std::string manifest = ReadText(manifestPath);
    passed &= Check(
        CountShaderPermutations(config) == 61u,
        "factory experiment catalog must contain exactly 61 permutations");
    passed &= Check(
        manifest.find("src/shaders_experiment_defaults.cfg") !=
            std::string::npos,
        "runtime manifest must identify the factory experiment config");
    passed &= Check(
        config.find(
            "VISIBILITY_ESTIMATOR=1 -D ENABLE_AO=1 -D ENABLE_GI=1 "
            "-D ENABLE_BOUNCE_REINJECTION=0 "
            "-D INITIALIZE_BOUNCE_CUMULATIVE=0 "
            "-D ENABLE_BOUNCE_METADATA=0 "
            "-D RUNTIME_SAMPLE_PARITY=1") != std::string::npos,
        "factory visibility trace must be the startup Runtime topology");
    passed &= Check(
        config.find(
            "TAA_SHARED_WORK_REUSE=1 -D TAA_EARLY_HISTORY_REJECTION=0 "
            "-D TAA_FUSED_OUTPUT=0 -D TAA_DEVELOPER_DEBUG=0") !=
            std::string::npos,
        "factory catalog must retain the validated Intel Auto alternate");
    passed &= Check(
        config.find(
            "TAA_LDS_LAYOUT=2 -D TAA_SHARED_WORK_REUSE=1 "
            "-D TAA_EARLY_HISTORY_REJECTION=1 "
            "-D TAA_FUSED_OUTPUT={0,1}") !=
            std::string::npos,
        "factory catalog must retain fused and separate Reduced outputs");
    passed &= Check(
            config.find(
                "temporal_aa_minimum_cs.hlsl -T cs -E main "
                "-D TAA_RUNTIME_BEHAVIOR={0,1}") !=
                std::string::npos &&
            config.find(
                "temporal_aa_minimum_cs.hlsl -T cs -E main") ==
                config.rfind(
                    "temporal_aa_minimum_cs.hlsl -T cs -E main"),
        "factory catalog must package static-default and configurable compact "
        "temporal paths");

    constexpr const char* forbiddenTokens[] = {
        "screen_space_directional_shadows",
        "sparse_virtual_shadow_map",
        "diagnostic_cascaded_shadow_map",
        "cmaa2",
        "CMAA2",
        "screen_space_visibility_composed_edges",
        "screen_space_visibility_filter_packed_edge",
        "screen_space_visibility_fused_apply",
        "RUNTIME_SAMPLE_PARITY=2",
        "ENABLE_BOUNCE_REINJECTION=1",
        "WHITE_WORLD=1",
        "TAA_DEVELOPER_DEBUG=1"
    };
    for (const char* token : forbiddenTokens)
    {
        passed &= Check(
            config.find(token) == std::string::npos &&
                manifest.find(token) == std::string::npos,
            std::string("non-factory shader path must be absent: ") + token);
    }

    std::set<std::string> stagedFiles;
    for (const std::filesystem::directory_entry& entry :
        std::filesystem::recursive_directory_iterator(stageRoot))
    {
        if (!entry.is_regular_file() ||
            entry.path().extension() != ".bin")
        {
            continue;
        }
        stagedFiles.emplace(
            std::filesystem::relative(
                entry.path(),
                stageRoot).generic_string());
    }

    const std::set<std::string> expectedFiles =
        GetExpectedShaderFiles();
    passed &= Check(
        expectedFiles.size() == 40u,
        "factory experiment contract must enumerate exactly 40 files");
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
    std::cout << "Factory experiment shader bundle contract passed\n";
    return 0;
}
