#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <set>
#include <string>
#include <string_view>
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
            "gbuffer_vs_buffer_loads",
            "light_probe_cubemap_gs",
            "light_probe_diffuse_probe_ps",
            "light_probe_environment_brdf_ps",
            "light_probe_mip_ps",
            "light_probe_specular_probe_ps",
            "material_id_ps",
            "pixel_readback_cs",
            "depth_ps",
            "depth_vs_buffer_loads"
        };
        constexpr const char* appShaders[] = {
            "agx_tonemapping_ps",
            "display_output_ps",
            "backdrop_blur_ps",
            "screen_space_directional_shadows_cs",
            "screen_space_directional_shadows_debug_ps",
            "cmaa2_ComputeDispatchArgsCS",
            "cmaa2_DeferredColorApply2x2CS",
            "cmaa2_EdgesColor2x2CS",
            "cmaa2_ProcessCandidatesCS",
            "image_based_lighting_background_ps",
            "msaa_visibility_resolve_cs",
            "pbr_deferred_lighting_cs",
            "pbr_deferred_lighting_msaa_cs",
            "pbr_gbuffer_ps",
            "pixel_zoom_ps",
            "screen_space_indirect_composite_cs",
            "screen_space_visibility_filter_cs",
            "screen_space_visibility_cs",
            "temporal_aa_blend_cs",
            "temporal_aa_minimum_cs",
            "temporal_aa_resolve_cs",
            "temporal_aa_sharpen_cs"
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
    const std::filesystem::path sourceDirectory =
        configPath.parent_path();
    const std::filesystem::path visibilitySourcePath =
        sourceDirectory / "screen_space_visibility_cs.hlsl";
    passed &= Check(
        std::filesystem::is_regular_file(configPath),
        "production shader config must exist");
    passed &= Check(
        std::filesystem::is_regular_file(visibilitySourcePath),
        "visibility shader source must exist");
    passed &= Check(
        std::filesystem::is_regular_file(manifestPath),
        "generated production runtime manifest must exist");
    passed &= Check(
        std::filesystem::is_directory(stageRoot),
        "production runtime shader tree must be staged");
    if (!passed)
        return 1;

    const std::string config = ReadText(configPath);
    const std::string visibilitySource = ReadText(visibilitySourcePath);
    const std::string manifest = ReadText(manifestPath);
    constexpr const char* runtimeParityBundle =
        "screen_space_visibility_cs.hlsl -T cs -E main -D VISIBILITY_ESTIMATOR=1 -D ENABLE_AO=1 -D ENABLE_GI=1 -D RUNTIME_SAMPLE_PARITY={1,2}";
    passed &= Check(
        CountOccurrences(config, runtimeParityBundle) == 1u,
        "the runtime config must package compact even and odd visibility loops");
    passed &= Check(
        visibilitySource.find("#ifndef RUNTIME_SAMPLE_PARITY") !=
                std::string::npos &&
            visibilitySource.find("#define RUNTIME_SAMPLE_PARITY 0") !=
                std::string::npos &&
            visibilitySource.find(
                "#if RUNTIME_SAMPLE_PARITY > 0") !=
                std::string::npos &&
            visibilitySource.find(
                "#if RUNTIME_SAMPLE_PARITY == 1") !=
                std::string::npos &&
            visibilitySource.find(
                "#elif RUNTIME_SAMPLE_PARITY == 2") !=
                std::string::npos &&
            visibilitySource.find(
                "uint selectedSampleCount = "
                "g_Visibility.maximumSampleCount;") !=
                std::string::npos &&
            visibilitySource.find(
                "SchedulerDimension_OddSampleSide") !=
                std::string::npos,
        "Runtime visibility must retain a guarded path, omit the "
        "odd-side fetch for trusted-even counts, and keep stochastic "
        "assignment for trusted-odd counts");
    constexpr const char* forbiddenShaders[] = {
        "smaa",
        "SMAA"
    };
    for (const char* shader : forbiddenShaders)
    {
        passed &= Check(
            config.find(shader) == std::string::npos &&
                manifest.find(shader) == std::string::npos,
            std::string("retired shader must be absent from production: ") +
                shader);
    }

    passed &= Check(
        manifest.find("src/shaders.cfg") != std::string::npos &&
            manifest.find("shaders_production.cfg") == std::string::npos &&
            manifest.find("shaders_experiment_defaults.cfg") ==
                std::string::npos,
        "the runtime manifest must identify the single shader config");
    constexpr const char* temporalUserMatrix =
        "temporal_aa_blend_cs.hlsl -T cs -E main -D TAA_MOTION_SOURCE={0,1,2} -D TAA_CURRENT_RECONSTRUCTION={0,1} -D TAA_HISTORY_FILTER={0,1,2,3} -D TAA_RECTIFICATION={0,1} -D TAA_OPTIMIZED_COMPUTE={0,1} -D TAA_FUSED_OUTPUT={0,1}";
    passed &= Check(
        CountOccurrences(config, temporalUserMatrix) == 1u &&
            CountOccurrences(
                config,
                "temporal_aa_minimum_cs.hlsl -T cs -E main "
                "-D TAA_RUNTIME_BEHAVIOR={0,1}") == 1u &&
            CountOccurrences(config, "temporal_aa_resolve_cs.hlsl") == 1u,
        "TAA must expose one robust matrix plus compact minimum and resolve paths");
    constexpr const char* retiredAxes[] = {
        "TAA_SAMPLE_RESURRECTION",
        "TAA_DEVELOPER_DEBUG",
        "TAA_PIXEL_SHADER",
        "TAA_EXPORT_SELECTIVE",
        "TAA_INTERIOR_WEIGHTING",
        "TAA_LDS_LAYOUT",
        "TAA_SHARED_WORK_REUSE",
        "TAA_EARLY_HISTORY_REJECTION",
        "CMAA2_SUPPORT_HDR_COLOR_RANGE",
        "ENABLE_AO_POWER",
        "ENABLE_BOUNCE",
        "DEPTH_HIERARCHY",
        "VISIBILITY_TEMPORAL"
    };
    for (const char* axis : retiredAxes)
    {
        passed &= Check(
            config.find(axis) == std::string::npos,
            std::string("retired shader axis must remain absent: ") + axis);
    }
    passed &= Check(
        CountShaderPermutations(config) == 268u,
        "the production shader catalog must contain exactly 268 permutations");
    passed &= Check(
        CountOccurrences(config, "cmaa2.hlsl -T cs") == 4u &&
            CountOccurrences(
                config,
                "CMAA2_STATIC_QUALITY_PRESET={0,1,2,3}") == 4u,
        "production must retain all four official CMAA2 stages across Low, "
        "Medium, High, and Ultra display-linear permutations");
    passed &= Check(
        CountOccurrences(
            config,
            "temporal_aa_sharpen_cs.hlsl") == 1u &&
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
    passed &= Check(
        expectedFiles.size() == 39u,
        "production shader contract must enumerate exactly 39 files");
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
