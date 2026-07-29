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
    const std::filesystem::path sourceDirectory =
        configPath.parent_path();
    const std::filesystem::path developerConfigPath =
        sourceDirectory / "shaders.cfg";
    const std::filesystem::path visibilitySourcePath =
        sourceDirectory / "screen_space_visibility_cs.hlsl";
    passed &= Check(
        std::filesystem::is_regular_file(configPath),
        "production shader config must exist");
    passed &= Check(
        std::filesystem::is_regular_file(developerConfigPath),
        "developer shader config must exist");
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
    const std::string developerConfig = ReadText(developerConfigPath);
    const std::string visibilitySource = ReadText(visibilitySourcePath);
    const std::string manifest = ReadText(manifestPath);
    constexpr const char* runtimeParityBundle =
        "screen_space_visibility_composed_packed_fast_cs.hlsl -T cs -E main -D VISIBILITY_ESTIMATOR=1 -D ENABLE_AO=1 -D ENABLE_GI=1 -D ENABLE_BOUNCE_REINJECTION=0 -D INITIALIZE_BOUNCE_CUMULATIVE=0 -D ENABLE_BOUNCE_METADATA=0 -D FIXED_SAMPLE_COUNT=0 -D RUNTIME_SAMPLE_PARITY={1,2} -D FIXED_RADIAL_EXPONENT_TWO=1 -D FIXED_DIRECT_DEPTH=1 -D OUTPUT_PACKED_EDGES=0";
    passed &= Check(
        CountOccurrences(config, runtimeParityBundle) == 1u &&
            CountOccurrences(developerConfig, runtimeParityBundle) == 1u,
        "developer and production configs must each package the compact even "
        "and odd Runtime visibility loops");
    passed &= Check(
        visibilitySource.find("#ifndef RUNTIME_SAMPLE_PARITY") !=
                std::string::npos &&
            visibilitySource.find("#define RUNTIME_SAMPLE_PARITY 0") !=
                std::string::npos &&
            visibilitySource.find(
                "#elif RUNTIME_SAMPLE_PARITY > 0") !=
                std::string::npos &&
            visibilitySource.find(
                "#elif RUNTIME_SAMPLE_PARITY == 1") !=
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
        "Runtime visibility must retain a robust Generic path, omit the "
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
            "taa_miniengine_blend_cs.hlsl") == 1u,
        "production must describe the complete normal-user temporal matrix once");
    constexpr const char* temporalUserMatrix =
        "taa_miniengine_blend_cs.hlsl -T cs -E main -D TAA_MOTION_SOURCE={0,1,2} -D TAA_CURRENT_RECONSTRUCTION={0,1} -D TAA_INTERIOR_WEIGHTING={0,1} -D TAA_HISTORY_FILTER={0,1,2,3} -D TAA_RECTIFICATION={0,1,2,3}";
    passed &= Check(
        CountOccurrences(config, temporalUserMatrix) == 1u,
        "production TAA must package motion, de-jittering, stable-interior, "
        "1x/5x/9x reconstruction, and rectification choices");
    passed &= Check(
        CountOccurrences(config, "TAA_EXPORT_SELECTIVE=0") == 1u &&
            CountOccurrences(config, "TAA_SAMPLE_RESURRECTION=0") == 1u &&
            CountOccurrences(config, "TAA_DEVELOPER_DEBUG=0") == 1u &&
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
