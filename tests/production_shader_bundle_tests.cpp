#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr size_t CanonicalProductionShaderTaskCount = 137u;

    struct ShaderCatalogRow
    {
        std::string family;
        std::string key;
    };

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

    std::vector<ShaderCatalogRow> ReadCatalogRows(
        const std::filesystem::path& directory)
    {
        std::vector<ShaderCatalogRow> rows;
        for (const auto& entry :
            std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file() ||
                entry.path().extension() != ".txt")
            {
                continue;
            }
            std::istringstream input(ReadText(entry.path()));
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty())
                    continue;
                const size_t firstTab = line.find('\t');
                const size_t secondTab = line.find('\t', firstTab + 1u);
                if (firstTab == std::string::npos ||
                    secondTab == std::string::npos)
                {
                    std::cerr << "Malformed generated shader catalog row: "
                              << line << '\n';
                    return {};
                }
                const std::filesystem::path objectPath = line.substr(
                    firstTab + 1u, secondTab - firstTab - 1u);
                std::string stem = objectPath.stem().string();
                const size_t hashSeparator = stem.rfind('.');
                if (hashSeparator == std::string::npos)
                {
                    std::cerr << "Missing shader object hash: "
                              << objectPath << '\n';
                    return {};
                }
                const std::string hash = stem.substr(hashSeparator + 1u);
                if (hash.size() != 16u ||
                    !std::all_of(hash.begin(), hash.end(), [](char value)
                    {
                        return std::isxdigit(
                            static_cast<unsigned char>(value)) != 0;
                    }))
                {
                    std::cerr << "Invalid shader object hash: "
                              << objectPath << '\n';
                    return {};
                }
                rows.push_back({
                    stem.substr(0u, hashSeparator),
                    line.substr(0u, firstTab)
                });
            }
        }
        return rows;
    }

    std::vector<std::string> KeysForFamily(
        const std::vector<ShaderCatalogRow>& rows,
        std::string_view family)
    {
        std::vector<std::string> keys;
        for (const ShaderCatalogRow& row : rows)
        {
            if (row.family == family)
                keys.push_back(row.key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    bool CheckExactKeys(
        const std::vector<ShaderCatalogRow>& rows,
        std::string_view family,
        std::vector<std::string> expected)
    {
        std::sort(expected.begin(), expected.end());
        const std::vector<std::string> actual =
            KeysForFamily(rows, family);
        if (actual == expected)
            return true;
        std::cerr << "FAILED: generated keys changed for " << family << '\n';
        for (const std::string& key : expected)
            std::cerr << "EXPECTED: " << key << '\n';
        for (const std::string& key : actual)
            std::cerr << "ACTUAL: " << key << '\n';
        return false;
    }

    std::string CanonicalKey(std::vector<std::string> tokens)
    {
        std::sort(tokens.begin(), tokens.end());
        std::string key;
        for (const std::string& token : tokens)
        {
            if (!key.empty())
                key.push_back(' ');
            key += token;
        }
        return key;
    }

    std::vector<std::string> OneAxisKeys(
        std::string_view axis,
        std::initializer_list<uint32_t> values)
    {
        std::vector<std::string> keys;
        for (uint32_t value : values)
            keys.push_back(std::string(axis) + '=' + std::to_string(value));
        return keys;
    }

    std::set<std::string> GetExpectedShaderFiles()
    {
        constexpr const char* rootShaders[] = {
            "blit_ps", "fullscreen_vs", "imgui_pixel", "imgui_vertex",
            "rect_vs", "sharpen_ps", "skinning_cs"
        };
        constexpr const char* passShaders[] = {
            "depth_ps", "depth_vs_buffer_loads"
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
            "directional_ray_visibility_cs",
            "ray_traced_flashlight_shadows_cs_GenerateVisibility",
            "ray_traced_flashlight_shadows_cs_GenerateVisibilityAndHitDistance",
            "ray_traced_sky_visibility_cs_Generate",
            "renderer_blit_ps",
            "renderer_fullscreen_vs",
            "renderer_gbuffer_vs_buffer_loads",
            "material_id_ps",
            "renderer_pixel_readback_cs",
            "image_based_lighting_background_ps",
            "light_probe_processing_cubemap_gs",
            "light_probe_processing_environment_brdf_ps",
            "light_probe_processing_mip_ps",
            "light_probe_processing_specular_probe_ps",
            "lighting_accumulation_cs",
            "lighting_accumulation_prepare_cs",
            "msaa_visibility_resolve_cs",
            "path_tracing_cs",
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
            expected.insert("framework/dxil/" +
                std::string(shader) + ".bin");
        for (const char* shader : passShaders)
            expected.insert("framework/dxil/passes/" +
                std::string(shader) + ".bin");
        for (const char* shader : appShaders)
            expected.insert("uvsr/dxil/" +
                std::string(shader) + ".bin");
        return expected;
    }
}

int main(int argc, char** argv)
{
    bool passed = Check(argc == 4,
        "expected generated catalogs, runtime manifest, and staged shader tree");
    if (!passed)
        return 1;

    const std::filesystem::path catalogDirectory = argv[1];
    const std::filesystem::path manifestPath = argv[2];
    const std::filesystem::path stageRoot = argv[3];
    passed &= Check(std::filesystem::is_directory(catalogDirectory),
        "generated shader catalogs must exist");
    passed &= Check(std::filesystem::is_regular_file(manifestPath),
        "generated production runtime manifest must exist");
    passed &= Check(std::filesystem::is_directory(stageRoot),
        "production runtime shader tree must be staged");
    if (!passed)
        return 1;

    const std::vector<ShaderCatalogRow> rows =
        ReadCatalogRows(catalogDirectory);
    const std::string manifest = ReadText(manifestPath);
    passed &= Check(rows.size() == CanonicalProductionShaderTaskCount,
        "the generated production catalog must contain exactly 137 DXIL tasks");

    constexpr std::array<std::pair<std::string_view, size_t>, 38>
        expectedFamilies = {{
            { "agx_tonemapping_ps", 2u },
            { "auto_exposure_histogram_cs", 1u },
            { "auto_exposure_resolve_cs", 1u },
            { "backdrop_blur_ps", 3u },
            { "denoising_prepare_cs", 3u },
            { "denoising_resolve_cs", 3u },
            { "denoising_spatial_cs", 5u },
            { "directional_ray_visibility_cs", 5u },
            { "display_output_ps", 1u },
            { "fast_approximate_aa_ps", 1u },
            { "image_based_lighting_background_ps", 1u },
            { "light_probe_processing_cubemap_gs", 1u },
            { "light_probe_processing_environment_brdf_ps", 1u },
            { "light_probe_processing_mip_ps", 1u },
            { "light_probe_processing_specular_probe_ps", 1u },
            { "lighting_accumulation_cs", 1u },
            { "lighting_accumulation_prepare_cs", 1u },
            { "material_id_ps", 2u },
            { "msaa_visibility_resolve_cs", 4u },
            { "path_tracing_cs", 1u },
            { "pbr_deferred_lighting_cs", 2u },
            { "pbr_deferred_lighting_msaa_cs", 8u },
            { "pbr_gbuffer_ps", 8u },
            { "pixel_zoom_ps", 1u },
            { "ray_traced_flashlight_shadows_cs_GenerateVisibility", 5u },
            { "ray_traced_flashlight_shadows_cs_GenerateVisibilityAndHitDistance", 5u },
            { "ray_traced_sky_visibility_cs_Generate", 10u },
            { "renderer_blit_ps", 1u },
            { "renderer_fullscreen_vs", 2u },
            { "renderer_gbuffer_vs_buffer_loads", 2u },
            { "renderer_pixel_readback_cs", 1u },
            { "screen_space_indirect_composite_cs", 1u },
            { "screen_space_visibility_cs", 28u },
            { "screen_space_visibility_filter_cs", 3u },
            { "temporal_aa_blend_cs", 16u },
            { "temporal_aa_minimum_cs", 2u },
            { "temporal_aa_resolve_cs", 1u },
            { "temporal_aa_sharpen_cs", 2u }
        }};
    std::set<std::string> actualFamilies;
    for (const ShaderCatalogRow& row : rows)
        actualFamilies.insert(row.family);
    std::set<std::string> expectedApplicationFamilies;
    for (const auto& [family, count] : expectedFamilies)
    {
        expectedApplicationFamilies.insert(std::string(family));
        passed &= Check(
            KeysForFamily(rows, family).size() == count,
            "generated shader family count changed: " +
                std::string(family));
    }
    passed &= Check(actualFamilies == expectedApplicationFamilies,
        "the generated shader family set changed");

    passed &= CheckExactKeys(rows, "agx_tonemapping_ps",
        OneAxisKeys("UVSR_UNITY_EXPOSURE", { 0u, 1u }));
    passed &= CheckExactKeys(rows, "denoising_prepare_cs",
        OneAxisKeys("DENOISING_SIGNAL_CLASS", { 0u, 1u, 2u }));
    passed &= CheckExactKeys(rows, "denoising_resolve_cs",
        OneAxisKeys("DENOISING_SIGNAL_CLASS", { 0u, 1u, 2u }));
    passed &= CheckExactKeys(rows, "denoising_spatial_cs",
        OneAxisKeys("DENOISING_OUTPUT_FORMAT", { 0u, 1u, 2u, 3u, 4u }));
    passed &= CheckExactKeys(rows, "directional_ray_visibility_cs",
        OneAxisKeys("DIRECTIONAL_VISIBILITY_SAMPLES", { 1u, 2u, 4u, 8u, 16u }));
    passed &= CheckExactKeys(rows, "material_id_ps",
        OneAxisKeys("ALPHA_TESTED", { 0u, 1u }));
    passed &= CheckExactKeys(rows, "msaa_visibility_resolve_cs",
        OneAxisKeys("MSAA_VISIBILITY_SAMPLES", { 2u, 4u, 8u, 16u }));
    passed &= CheckExactKeys(rows, "renderer_fullscreen_vs",
        OneAxisKeys("UVSR_FULLSCREEN_DEPTH", { 0u, 1u }));
    passed &= CheckExactKeys(rows, "renderer_gbuffer_vs_buffer_loads",
        OneAxisKeys("MOTION_VECTORS", { 0u, 1u }));
    passed &= CheckExactKeys(rows, "temporal_aa_minimum_cs",
        OneAxisKeys("TAA_RUNTIME_BEHAVIOR", { 0u, 1u }));
    passed &= CheckExactKeys(rows, "temporal_aa_sharpen_cs",
        OneAxisKeys("TAA_SHARPEN_INPUT_PREMULTIPLIED", { 0u, 1u }));

    for (std::string_view family : {
        std::string_view("ray_traced_flashlight_shadows_cs_GenerateVisibility"),
        std::string_view("ray_traced_flashlight_shadows_cs_GenerateVisibilityAndHitDistance") })
    {
        passed &= CheckExactKeys(rows, family,
            OneAxisKeys("FLASHLIGHT_VISIBILITY_SAMPLES", { 1u, 2u, 4u, 8u, 16u }));
    }

    std::vector<std::string> skyKeys;
    for (uint32_t hitDistance : { 0u, 1u })
    for (uint32_t samples : { 1u, 2u, 4u, 8u, 16u })
    {
        skyKeys.push_back(CanonicalKey({
            "OUTPUT_HIT_DISTANCE=" + std::to_string(hitDistance),
            "SKY_VISIBILITY_SAMPLES=" + std::to_string(samples)
        }));
    }
    passed &= CheckExactKeys(
        rows, "ray_traced_sky_visibility_cs_Generate", skyKeys);

    std::vector<std::string> pbrMsaaKeys;
    for (uint32_t samples : { 2u, 4u, 8u, 16u })
    for (uint32_t visibility : { 0u, 1u })
    {
        pbrMsaaKeys.push_back(CanonicalKey({
            "PBR_DEFERRED_MSAA_SAMPLES=" + std::to_string(samples),
            "PBR_DEFERRED_MSAA_VISIBILITY=" + std::to_string(visibility)
        }));
    }
    passed &= CheckExactKeys(
        rows, "pbr_deferred_lighting_msaa_cs", pbrMsaaKeys);

    struct TemporalRecipe
    {
        uint32_t reconstruction;
        uint32_t historyFilter;
        uint32_t motionSource;
        uint32_t rectification;
    };
    constexpr std::array<TemporalRecipe, 4> temporalRecipes = {{
        { 0u, 0u, 0u, 0u },
        { 0u, 0u, 2u, 0u },
        { 0u, 1u, 2u, 1u },
        { 1u, 2u, 2u, 1u }
    }};
    std::vector<std::string> temporalKeys;
    for (const TemporalRecipe& recipe : temporalRecipes)
    for (uint32_t optimized : { 0u, 1u })
    for (uint32_t fused : { 0u, 1u })
    {
        temporalKeys.push_back(CanonicalKey({
            "TAA_CURRENT_RECONSTRUCTION=" +
                std::to_string(recipe.reconstruction),
            "TAA_FUSED_OUTPUT=" + std::to_string(fused),
            "TAA_HISTORY_FILTER=" + std::to_string(recipe.historyFilter),
            "TAA_MOTION_SOURCE=" + std::to_string(recipe.motionSource),
            "TAA_OPTIMIZED_COMPUTE=" + std::to_string(optimized),
            "TAA_RECTIFICATION=" + std::to_string(recipe.rectification)
        }));
    }
    passed &= CheckExactKeys(rows, "temporal_aa_blend_cs", temporalKeys);

    std::vector<std::string> visibilityKeys;
    const auto addVisibilityKey = [&](uint32_t estimator,
        uint32_t ao, uint32_t gi, uint32_t parity,
        uint32_t aoHitDistance, uint32_t giHitDistance)
    {
        std::vector<std::string> tokens = {
            "ENABLE_AO=" + std::to_string(ao),
            "ENABLE_GI=" + std::to_string(gi),
            "VISIBILITY_ESTIMATOR=" + std::to_string(estimator)
        };
        if (parity != 0u)
            tokens.push_back("RUNTIME_SAMPLE_PARITY=" +
                std::to_string(parity));
        if (aoHitDistance != 0u)
            tokens.push_back("OUTPUT_AO_HIT_DISTANCE=1");
        if (giHitDistance != 0u)
            tokens.push_back("OUTPUT_GI_HIT_DISTANCE=1");
        visibilityKeys.push_back(CanonicalKey(std::move(tokens)));
    };
    for (uint32_t estimator : { 0u, 1u, 2u })
    {
        addVisibilityKey(estimator, 1u, 0u, 0u, 0u, 0u);
        addVisibilityKey(estimator, 1u, 0u, 0u, 1u, 0u);
        addVisibilityKey(estimator, 0u, 1u, 0u, 0u, 0u);
        addVisibilityKey(estimator, 0u, 1u, 0u, 0u, 1u);
    }
    for (uint32_t estimator : { 0u, 2u })
    for (uint32_t aoHit : { 0u, 1u })
    for (uint32_t giHit : { 0u, 1u })
        addVisibilityKey(estimator, 1u, 1u, 0u, aoHit, giHit);
    for (uint32_t parity : { 1u, 2u })
    for (uint32_t aoHit : { 0u, 1u })
    for (uint32_t giHit : { 0u, 1u })
        addVisibilityKey(1u, 1u, 1u, parity, aoHit, giHit);
    passed &= CheckExactKeys(
        rows, "screen_space_visibility_cs", visibilityKeys);
    passed &= CheckExactKeys(rows, "screen_space_visibility_filter_cs", {
        CanonicalKey({ "ENABLE_AO=0", "ENABLE_GI=1" }),
        CanonicalKey({ "ENABLE_AO=1", "ENABLE_GI=0" }),
        CanonicalKey({ "ENABLE_AO=1", "ENABLE_GI=1" })
    });

    const std::array<std::string_view, 19> retiredTokens = {
        "smaa", "SMAA", "screen_space_directional_shadows",
        "TAA_SAMPLE_RESURRECTION", "TAA_DEVELOPER_DEBUG",
        "TAA_PIXEL_SHADER", "TAA_EXPORT_SELECTIVE",
        "TAA_INTERIOR_WEIGHTING", "TAA_LDS_LAYOUT",
        "TAA_SHARED_WORK_REUSE", "TAA_EARLY_HISTORY_REJECTION",
        "ENABLE_AO_POWER", "ENABLE_BOUNCE", "DEPTH_HIERARCHY",
        "VISIBILITY_TEMPORAL", "OUTPUT_PACKED_EDGES",
        "PACKED_EDGE_RECONSTRUCTION", "diffuse_probe_ps",
        "shaders_experiment_defaults.cfg"
    };
    for (std::string_view token : retiredTokens)
    {
        bool found = manifest.find(token) != std::string::npos;
        for (const ShaderCatalogRow& row : rows)
        {
            found |= row.family.find(token) != std::string::npos ||
                row.key.find(token) != std::string::npos;
        }
        passed &= Check(!found,
            "retired shader contract returned: " + std::string(token));
    }
    passed &= Check(
        manifest.find("src/shaders.cfg") != std::string::npos &&
            manifest.find("shaders_production.cfg") == std::string::npos,
        "the runtime manifest must identify the single shader catalog input");

    std::set<std::string> stagedFiles;
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator(stageRoot))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".bin")
        {
            stagedFiles.insert(std::filesystem::relative(
                entry.path(), stageRoot).generic_string());
        }
    }
    const std::set<std::string> expectedFiles = GetExpectedShaderFiles();
    if (stagedFiles != expectedFiles)
    {
        std::vector<std::string> missing;
        std::vector<std::string> unexpected;
        std::set_difference(expectedFiles.begin(), expectedFiles.end(),
            stagedFiles.begin(), stagedFiles.end(),
            std::back_inserter(missing));
        std::set_difference(stagedFiles.begin(), stagedFiles.end(),
            expectedFiles.begin(), expectedFiles.end(),
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
