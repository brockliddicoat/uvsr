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

    size_t CountExactLines(
        const std::string& text,
        std::string_view expected)
    {
        std::istringstream lines(text);
        std::string line;
        size_t count = 0u;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line == expected)
                ++count;
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
            "auto_exposure_histogram_cs",
            "auto_exposure_resolve_cs",
            "display_output_ps",
            "fast_approximate_aa_ps",
            "backdrop_blur_ps",
            "denoising_prepare_cs",
            "denoising_resolve_cs",
            "denoising_spatial_cs",
            "heitz_ratio_estimator_shadows_cs_Generate",
            "ray_traced_flashlight_shadows_cs_GenerateVisibility",
            "ray_traced_flashlight_shadows_cs_GenerateVisibilityAndHitDistance",
            "ray_traced_sky_visibility_cs_Generate",
            "image_based_lighting_background_ps",
            "lighting_accumulation_cs",
            "lighting_accumulation_prepare_cs",
            "msaa_visibility_resolve_cs",
            "path_tracing_cs",
            "path_tracing_primary_surface_cs",
            "path_tracing_stable_plane_resolve_cs",
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
        CountExactLines(config, runtimeParityBundle) == 1u,
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
        "SMAA",
        "screen_space_directional_shadows",
        "screen_space_directional_shadows_debug"
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
        CountShaderPermutations(config) == 311u,
        "the production shader catalog must contain exactly 311 permutations");
    passed &= Check(
        CountExactLines(
            config,
            "path_tracing_primary_surface_cs.hlsl -T cs -E main "
                "-D UVSR_PT_RTXDI={0,1} "
                "-D UVSR_PT_NEE_MODE={0,1,2} "
                "--compilerOptionsDXIL \"-res-may-alias\"") == 1u,
        "production must package the shared primary/direct RTXDI and NEE matrix");
    passed &= Check(
        CountExactLines(
            config,
            "agx_tonemapping_ps.hlsl -T ps -E main "
                "-D UVSR_UNITY_EXPOSURE={0,1}") == 1u &&
            CountOccurrences(config, "agx_tonemapping_ps.hlsl") == 1u,
        "production must package exposure-buffer and unity-exposure AgX paths");
    passed &= Check(
        CountExactLines(
            config,
            "lighting_accumulation_prepare_cs.hlsl -T cs -E main") == 1u &&
            CountOccurrences(
                config,
                "lighting_accumulation_prepare_cs.hlsl") == 1u,
        "production must package one transactional lighting-attempt prepare shader");
    passed &= Check(
        CountExactLines(
            config,
            "path_tracing_cs.hlsl -T cs -E main "
                "-D UVSR_PT_SOLVER={0,1,2} "
                "-D UVSR_PT_RTXDI={0,1} "
                "-D UVSR_PT_NEE_MODE={0,1,2} "
                "--compilerOptionsDXIL \"-res-may-alias\"") == 1u,
        "production must package the complete solver, RTXDI, and NEE path-transport matrix");
    passed &= Check(
        CountExactLines(
            config,
            "path_tracing_stable_plane_resolve_cs.hlsl -T cs -E main") ==
                1u,
        "production must package one spatial stable-plane resolve shader");
    passed &= Check(
        CountOccurrences(
            config,
            "heitz_ratio_estimator_shadows_cs.hlsl -T cs -E Generate") ==
                3u &&
            CountOccurrences(
                config,
                "heitz_ratio_estimator_shadows_cs.hlsl") == 3u &&
            config.find(
                "heitz_ratio_estimator_shadows_cs.hlsl -T cs -E Generate "
                "-D HEITZ_RASTER_SAMPLES=1 "
                "-D OUTPUT_SOURCE_MODULATION=0 "
                "-D OUTPUT_HIT_DISTANCE={0,1}") != std::string::npos &&
            config.find(
                "heitz_ratio_estimator_shadows_cs.hlsl -T cs -E Generate "
                "-D HEITZ_RASTER_SAMPLES=1 "
                "-D OUTPUT_SOURCE_MODULATION=1 "
                "-D OUTPUT_HIT_DISTANCE=0") != std::string::npos &&
            config.find(
                "heitz_ratio_estimator_shadows_cs.hlsl -T cs -E Generate "
                "-D HEITZ_RASTER_SAMPLES={2,4,8,16} "
                "-D OUTPUT_SOURCE_MODULATION=1 "
                "-D OUTPUT_HIT_DISTANCE=0") != std::string::npos,
        "production must package mutually exclusive 1x source/hit outputs and multisample source visibility");
    passed &= Check(
        CountOccurrences(
            config,
            "ray_traced_sky_visibility_cs.hlsl -T cs -E Generate") ==
                1u &&
            config.find(
                "ray_traced_sky_visibility_cs.hlsl -T cs -E Generate "
                "-D OUTPUT_HIT_DISTANCE={0,1}") != std::string::npos &&
            manifest.find("ray_traced_sky_visibility_cs_Generate") !=
                std::string::npos,
        "production must package sky visibility with optional hit distance");
    passed &= Check(
        CountOccurrences(
            config,
            "ray_traced_flashlight_shadows_cs.hlsl -T cs -E ") == 2u &&
            CountOccurrences(
                config,
                "ray_traced_flashlight_shadows_cs.hlsl -T cs -E "
                "GenerateVisibilityAndHitDistance") == 1u &&
            manifest.find(
                "ray_traced_flashlight_shadows_cs_GenerateVisibility") !=
                std::string::npos &&
            manifest.find(
                "ray_traced_flashlight_shadows_cs_"
                "GenerateVisibilityAndHitDistance") != std::string::npos,
        "production must package flashlight visibility with optional hit "
        "distance");
    passed &= Check(
        config.find(
            "denoising_prepare_cs.hlsl -T cs -E main "
            "-D DENOISING_SIGNAL_CLASS={0,1,2}") != std::string::npos &&
            config.find(
                "denoising_resolve_cs.hlsl -T cs -E main "
                "-D DENOISING_SIGNAL_CLASS={0,1,2}") != std::string::npos &&
            config.find(
                "denoising_spatial_cs.hlsl -T cs -E main "
                "-D DENOISING_OUTPUT_FORMAT={0,1,2,3,4}") !=
                std::string::npos &&
            manifest.find("denoising_prepare_cs") != std::string::npos &&
            manifest.find("denoising_resolve_cs") != std::string::npos &&
            manifest.find("denoising_spatial_cs") != std::string::npos,
        "production must package third-party preparation plus all five "
        "built-in spatial output formats");

    constexpr const char* requiredVisibilityHitBundles[] = {
        "-D VISIBILITY_ESTIMATOR={0,1,2} -D ENABLE_AO=1 -D ENABLE_GI=0 "
            "-D OUTPUT_AO_HIT_DISTANCE=1",
        "-D VISIBILITY_ESTIMATOR={0,1,2} -D ENABLE_AO=0 -D ENABLE_GI=1 "
            "-D OUTPUT_GI_HIT_DISTANCE=1",
        "-D VISIBILITY_ESTIMATOR={0,2} -D ENABLE_AO=1 -D ENABLE_GI=1 "
            "-D OUTPUT_AO_HIT_DISTANCE=1",
        "-D VISIBILITY_ESTIMATOR={0,2} -D ENABLE_AO=1 -D ENABLE_GI=1 "
            "-D OUTPUT_GI_HIT_DISTANCE=1",
        "-D VISIBILITY_ESTIMATOR={0,2} -D ENABLE_AO=1 -D ENABLE_GI=1 "
            "-D OUTPUT_AO_HIT_DISTANCE=1 -D OUTPUT_GI_HIT_DISTANCE=1",
        "-D VISIBILITY_ESTIMATOR=1 -D ENABLE_AO=1 -D ENABLE_GI=1 "
            "-D RUNTIME_SAMPLE_PARITY={1,2} -D OUTPUT_AO_HIT_DISTANCE=1",
        "-D VISIBILITY_ESTIMATOR=1 -D ENABLE_AO=1 -D ENABLE_GI=1 "
            "-D RUNTIME_SAMPLE_PARITY={1,2} -D OUTPUT_GI_HIT_DISTANCE=1",
        "-D VISIBILITY_ESTIMATOR=1 -D ENABLE_AO=1 -D ENABLE_GI=1 "
            "-D RUNTIME_SAMPLE_PARITY={1,2} -D OUTPUT_AO_HIT_DISTANCE=1 "
            "-D OUTPUT_GI_HIT_DISTANCE=1"
    };
    for (const char* bundle : requiredVisibilityHitBundles)
    {
        passed &= Check(
            config.find(bundle) != std::string::npos,
            std::string("missing visibility hit-distance permutation: ") +
                bundle);
    }
    passed &= Check(
        CountOccurrences(config, "OUTPUT_AO_HIT_DISTANCE=1") == 5u &&
            CountOccurrences(config, "OUTPUT_GI_HIT_DISTANCE=1") == 5u,
        "AO and GI hit distance outputs must cover every reachable topology");
    passed &= Check(
        config.find("OUTPUT_PACKED_EDGES") == std::string::npos &&
            config.find("PACKED_EDGE_RECONSTRUCTION") ==
                std::string::npos &&
            visibilitySource.find("OUTPUT_PACKED_EDGES") ==
                std::string::npos,
        "retired packed Diffuse reconstruction permutations must stay absent");
    passed &= Check(
        CountOccurrences(
            config,
            "fast_approximate_aa_ps.hlsl -T ps -E main") == 1u &&
            manifest.find("fast_approximate_aa_ps") != std::string::npos,
        "production must package one runtime-configured Fast Approximate shader");
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
        expectedFiles.size() == 48u,
        "production shader contract must enumerate exactly 48 files");
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
