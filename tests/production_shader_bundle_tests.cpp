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
            "deferred_lighting_cs",
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
            "pixel_readback_cs",
            "depth_ps",
            "depth_vs_buffer_loads"
        };
        constexpr const char* appShaders[] = {
            "agx_tonemapping_ps",
            "backdrop_blur_ps",
            "screen_space_directional_shadows_cs",
            "screen_space_directional_shadows_debug_ps",
            "cmaa2_ComputeDispatchArgsCS",
            "cmaa2_DeferredColorApply2x2CS",
            "cmaa2_EdgesColor2x2CS",
            "cmaa2_ProcessCandidatesCS",
            "diagnostic_cascaded_shadow_map_clear_vs",
            "diagnostic_cascaded_shadow_map_depth_vs",
            "diagnostic_cascaded_shadow_map_depth_vs_alpha_tested",
            "diagnostic_cascaded_shadow_map_depth_vs_alpha_tested_input_assembler",
            "diagnostic_cascaded_shadow_map_depth_vs_main_input_assembler",
            "diagnostic_cascaded_shadow_map_scroll_ps",
            "diagnostic_cascaded_shadow_map_resolve_cs",
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
            "screen_space_visibility_composed_edges_cs",
            "screen_space_visibility_filter_cs",
            "screen_space_visibility_filter_packed_edge_cs",
            "screen_space_visibility_fused_apply_cs",
            "screen_space_visibility_cs",
            "screen_space_visibility_temporal_cs",
            "sparse_virtual_shadow_map_depth_ps",
            "sparse_virtual_shadow_map_debug_ps",
            "sparse_virtual_shadow_map_resolve_cs",
            "sparse_virtual_shadow_map_sparse_cs_allocate",
            "sparse_virtual_shadow_map_sparse_cs_buildScheduledPageTileMasks",
            "sparse_virtual_shadow_map_sparse_cs_buildStaticDepthHierarchy",
            "sparse_virtual_shadow_map_sparse_cs_clearPages",
            "sparse_virtual_shadow_map_sparse_cs_fillIndirect",
            "sparse_virtual_shadow_map_sparse_cs_finalize",
            "sparse_virtual_shadow_map_sparse_cs_invalidatePages",
            "sparse_virtual_shadow_map_sparse_cs_mark",
            "sparse_virtual_shadow_map_sparse_cs_prepare",
            "sparse_virtual_shadow_map_sparse_cs_recycle",
            "sparse_virtual_shadow_map_sparse_cs_scheduleFine",
            "sparse_virtual_shadow_map_sparse_cs_stats",
            "sparse_virtual_shadow_map_sparse_depth_pixelMain",
            "sparse_virtual_shadow_map_sparse_depth_vertexMain",
            "sparse_virtual_shadow_map_sparse_resolve_cs_reference_legacy",
            "sparse_virtual_shadow_map_sparse_resolve_cs_reference_balanced",
            "sparse_virtual_shadow_map_sparse_resolve_cs_translation_cache_legacy",
            "sparse_virtual_shadow_map_sparse_resolve_cs_translation_cache_balanced",
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
        "screen_space_visibility_cs.hlsl -T cs -E main -D VISIBILITY_ESTIMATOR=1 -D ENABLE_AO=1 -D ENABLE_GI=1 -D ENABLE_BOUNCE_REINJECTION=0 -D INITIALIZE_BOUNCE_CUMULATIVE=0 -D ENABLE_BOUNCE_METADATA=0 -D RUNTIME_SAMPLE_PARITY={1,2}";
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
            "temporal_aa_blend_cs.hlsl") == 3u,
        "production must describe the full-quality, resurrection, and "
        "reduced temporal matrices");
    constexpr const char* temporalUserMatrix =
        "temporal_aa_blend_cs.hlsl -T cs -E main -D TAA_MOTION_SOURCE={0,1,2} -D TAA_CURRENT_RECONSTRUCTION={0,1} -D TAA_HISTORY_FILTER={0,1,2,3} -D TAA_RECTIFICATION={0,1}";
    passed &= Check(
        CountOccurrences(config, temporalUserMatrix) == 3u,
        "all production TAA matrices must package all three motion sources, "
        "de-jittering, 1x/5x/9x reconstruction, and both rectification "
        "choices");
    passed &= Check(
        CountOccurrences(config, "TAA_EXPORT_SELECTIVE=0") == 3u &&
            CountOccurrences(config, "TAA_SAMPLE_RESURRECTION=0") == 2u &&
            CountOccurrences(
                config,
                "TAA_SAMPLE_RESURRECTION={1,2}") == 1u &&
            CountOccurrences(config, "TAA_DEVELOPER_DEBUG=0") == 3u &&
            CountOccurrences(config, "TAA_SHARED_WORK_REUSE={0,1}") == 1u &&
            CountOccurrences(
                config,
                "TAA_LDS_LAYOUT=2 -D TAA_SHARED_WORK_REUSE=1 "
                "-D TAA_EARLY_HISTORY_REJECTION=1 "
                "-D TAA_FUSED_OUTPUT={0,1}") == 1u &&
            CountOccurrences(
                config,
                "temporal_aa_minimum_cs.hlsl -T cs -E main "
                "-D TAA_RUNTIME_BEHAVIOR={0,1}") == 1u &&
            config.find("TAA_EXPORT_SELECTIVE={") ==
                std::string::npos &&
            config.find("TAA_INTERIOR_WEIGHTING") == std::string::npos &&
            config.find("TAA_RECTIFICATION={0,1,2") == std::string::npos &&
            config.find("TAA_DEVELOPER_DEBUG=1") ==
                std::string::npos &&
            config.find("TAA_PIXEL_SHADER") == std::string::npos,
        "production TAA must retain robust and resurrection matrices, package "
        "the reduced topology and compact minimum path, and omit retired "
        "experiments");
    passed &= Check(
        CountShaderPermutations(config) == 601u &&
            CountShaderPermutations(developerConfig) == 2811u,
        "shader catalogs must contain 601 production and 2,811 developer "
        "permutations with the temporal cost paths");
    passed &= Check(
        CountOccurrences(config, "cmaa2.hlsl -T cs") == 4u &&
            CountOccurrences(
                config,
                "CMAA2_STATIC_QUALITY_PRESET={0,1,2,3}") == 4u,
        "production must retain all four official CMAA2 stages for Low, Medium, High, and Ultra");
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
        expectedFiles.size() == 77u,
        "production shader contract must enumerate exactly 77 files");
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
