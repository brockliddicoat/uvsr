#include "ui_settings_command_catalog.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "UI Settings command catalog validation failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    bool IsStableLowercaseName(std::string_view name)
    {
        if (name.empty())
            return false;

        for (const unsigned char character : name)
        {
            const bool lowercase = character >= 'a' && character <= 'z';
            const bool digit = character >= '0' && character <= '9';
            if (!lowercase && !digit && character != '.' && character != '-')
                return false;
        }
        return true;
    }

    const uvsr::UiSettingsCommandDefinition* Find(std::string_view name)
    {
        for (const auto& definition : uvsr::UiSettingsCommandCatalog)
        {
            if (definition.name == name)
                return &definition;
        }
        return nullptr;
    }
}

int main()
{
    using namespace uvsr;

    static_assert(UiSettingsCommandCatalog.size() == 222u);
    static_assert(static_cast<std::size_t>(UiSettingsCommandSection::Count) == 14u);
    static_assert(static_cast<std::uint8_t>(UiSettingsCommandKind::Float4) == 7u);
    static_assert(Action("test", Section::General, "test").supportedVerbs ==
        static_cast<std::uint8_t>(UiSettingsCommandVerb::Run));

    constexpr std::array<std::size_t, 14> ExpectedSectionCounts = {
        16u, // UI
        7u,  // General
        12u, // Pathing
        4u,  // Representation
        13u, // Noise
        20u, // Visibility
        31u, // Denoising
        28u, // Aliasing
        4u,  // Debug
        25u, // Sky
        27u, // Lights
        11u, // Directional Shadows
        21u, // Materials
        3u   // Footer
    };
    const std::set<std::string> ExpectedDynamicSelections = {
        "gpu.adapter",
        "light.selected",
        "material.selected",
        "scene.current"
    };
    const std::set<std::string> ExpectedVisibility = {
        "visibility.animate-samples",
        "visibility.ao.enabled",
        "visibility.ao.output-hit-distance",
        "visibility.ao.precision",
        "visibility.ao.strength",
        "visibility.distribution",
        "visibility.enabled",
        "visibility.estimator",
        "visibility.gi.enabled",
        "visibility.gi.output-hit-distance",
        "visibility.gi.intensity",
        "visibility.gi.precision",
        "visibility.noise-pattern",
        "visibility.noise-resolution",
        "visibility.quality",
        "visibility.radius",
        "visibility.resolution",
        "visibility.samples",
        "visibility.specify-noise",
        "visibility.thickness"
    };
    const std::set<std::string> ExpectedRepresentation = {
        "representation.blas.update-mode",
        "representation.bvh.build-preference",
        "representation.allow-ray-traversal",
        "representation.tlas.update-mode"
    };
    const std::set<std::string> ExpectedPathing = {
        "pathing.max-bounces",
        "pathing.nee",
        "pathing.nee-candidates",
        "pathing.rtxdi",
        "pathing.reuse-proposals-during-motion",
        "pathing.russian-roulette",
        "pathing.samples-per-pixel",
        "pathing.shared-primary-surface",
        "pathing.ser",
        "pathing.solver",
        "pathing.spatial-neighbors",
        "pathing.temporal-reuse"
    };
    const std::set<std::string> ExpectedNoise = {
        "noise.accumulate-samples",
        "noise.accumulation-averaging",
        "noise.accumulation-effective-history",
        "noise.accumulation-history-preset",
        "noise.accumulation-minimum-samples",
        "noise.accumulation-minimum-update-rate",
        "noise.accumulation-mode",
        "noise.accumulation-scheduling",
        "noise.accumulation-target-error",
        "noise.accumulation-workload-preset",
        "noise.animate-samples",
        "noise.pattern",
        "noise.resolution"
    };
    const std::set<std::string> ExpectedDenoising = {
        "denoising.ao.anti-lag",
        "denoising.ao.disocclusion",
        "denoising.ao.history",
        "denoising.ao.method",
        "denoising.ao.quality",
        "denoising.ao.radius",
        "denoising.ao.resolution",
        "denoising.gi.anti-lag",
        "denoising.gi.disocclusion",
        "denoising.gi.history",
        "denoising.gi.method",
        "denoising.gi.quality",
        "denoising.gi.radius",
        "denoising.gi.resolution",
        "denoising.shadows.disocclusion",
        "denoising.shadows.method",
        "denoising.shadows.quality",
        "denoising.shadows.radius",
        "denoising.shadows.resolution",
        "denoising.sky.anti-lag",
        "denoising.sky.disocclusion",
        "denoising.sky.history",
        "denoising.sky.method",
        "denoising.sky.quality",
        "denoising.sky.radius",
        "denoising.sky.resolution",
        "denoising.path-tracing.firefly-filter",
        "denoising.path-tracing.firefly-threshold",
        "denoising.path-tracing.method",
        "denoising.path-tracing.resolve-strength",
        "denoising.path-tracing.signal-groups"
    };
    const std::set<std::string> ExpectedDirectionalShadows = {
        "shadows.ray-traced.animate-samples",
        "shadows.ray-traced.enabled",
        "shadows.ray-traced.hard-shadows",
        "shadows.ray-traced.max-distance",
        "shadows.ray-traced.noise-pattern",
        "shadows.ray-traced.noise-resolution",
        "shadows.ray-traced.output-hit-distance",
        "shadows.ray-traced.ratio-estimator",
        "shadows.ray-traced.ray-bias",
        "shadows.ray-traced.samples-per-pixel",
        "shadows.ray-traced.specify-noise"
    };

    std::set<std::string> names;
    std::set<std::string> dynamicSelections;
    std::set<std::string> visibility;
    std::set<std::string> representation;
    std::set<std::string> pathing;
    std::set<std::string> noise;
    std::set<std::string> denoising;
    std::set<std::string> directionalShadows;
    std::array<std::size_t, 14> sectionCounts{};
    std::size_t actionCount = 0u;
    std::size_t dynamicCount = 0u;
    std::size_t snapshotValueCount = 0u;

    for (const UiSettingsCommandDefinition& definition :
        UiSettingsCommandCatalog)
    {
        Require(IsStableLowercaseName(definition.name),
            "every command must use stable lowercase ASCII");
        Require(names.insert(std::string(definition.name)).second,
            "every command must occur exactly once");
        Require(!definition.domain.empty(),
            "every command must describe its accepted values");

        const std::size_t section = static_cast<std::size_t>(definition.section);
        Require(section < sectionCounts.size(),
            "every command must belong to a live UI section");
        ++sectionCounts[section];

        const bool action = definition.kind == UiSettingsCommandKind::Action;
        if (action)
        {
            ++actionCount;
            Require(definition.supportedVerbs ==
                static_cast<std::uint8_t>(UiSettingsCommandVerb::Run),
                "actions must only support run");
            Require(!definition.dynamic,
                "actions must not masquerade as dynamic values");
        }
        else
        {
            Require(definition.Supports(UiSettingsCommandVerb::Get) &&
                    definition.Supports(UiSettingsCommandVerb::Set) &&
                    !definition.Supports(UiSettingsCommandVerb::Run),
                "values must support get and set but not run");
            if (definition.name != "ui.settings-collapsed" &&
                definition.name != "material-editor.visible")
                ++snapshotValueCount;
        }

        const bool boolean = definition.kind == UiSettingsCommandKind::Boolean;
        Require(definition.Supports(UiSettingsCommandVerb::Toggle) == boolean,
            "only booleans may support toggle");

        if (definition.dynamic)
        {
            ++dynamicCount;
            Require(definition.section == Section::General ||
                    definition.section == Section::Lights ||
                    definition.section == Section::Materials,
                "dynamic values must belong to a runtime-owned UI section");
        }
        if (definition.kind == UiSettingsCommandKind::DynamicSelection)
        {
            Require(definition.dynamic,
                "dynamic selections must advertise dynamic metadata");
            dynamicSelections.insert(std::string(definition.name));
        }
        if (definition.section == Section::Visibility)
            visibility.insert(std::string(definition.name));
        if (definition.section == Section::Representation)
            representation.insert(std::string(definition.name));
        if (definition.section == Section::Pathing)
            pathing.insert(std::string(definition.name));
        if (definition.section == Section::Noise)
            noise.insert(std::string(definition.name));
        if (definition.section == Section::Denoising)
            denoising.insert(std::string(definition.name));
        if (definition.section == Section::DirectionalShadows)
            directionalShadows.insert(std::string(definition.name));

        Require(definition.name.find("shadows.svsm") == std::string_view::npos &&
                definition.name.find("shadows.csm") == std::string_view::npos &&
                definition.name.find("benchmark") == std::string_view::npos &&
                definition.name.find("world-material") == std::string_view::npos &&
                definition.name.find("sample-resurrection") == std::string_view::npos &&
                definition.name != "engine.pbr",
            "retired engine taxonomies must not remain discoverable");
    }

    Require(names.size() == UiSettingsCommandCatalog.size(),
        "the compact catalog must contain 222 unique commands");
    Require(sectionCounts == ExpectedSectionCounts,
        "section counts must match the current UI");
    Require(actionCount == 4u,
        "only open-folder, reset, capture, and restart actions remain");
    Require(UiSettingsCommandCatalog.size() - actionCount == 218u,
        "the compact catalog must contain 218 values");
    Require(snapshotValueCount == 216u,
        "the current snapshot schema must represent 216 values after excluding "
        "transient root and Material drawer presentation");
    const UiSettingsCommandDefinition* settingsCollapsed =
        Find("ui.settings-collapsed");
    Require(settingsCollapsed &&
            settingsCollapsed->kind == Kind::Boolean &&
            settingsCollapsed->Supports(UiSettingsCommandVerb::Get) &&
            settingsCollapsed->Supports(UiSettingsCommandVerb::Set) &&
            settingsCollapsed->Supports(UiSettingsCommandVerb::Toggle) &&
            settingsCollapsed->Supports(UiSettingsCommandVerb::Reset),
        "Settings collapse must remain a complete CLI command even though it "
        "is excluded from copied snapshots");
    const UiSettingsCommandDefinition* materialEditorVisible =
        Find("material-editor.visible");
    Require(materialEditorVisible &&
            materialEditorVisible->kind == Kind::Boolean &&
            materialEditorVisible->Supports(UiSettingsCommandVerb::Get) &&
            materialEditorVisible->Supports(UiSettingsCommandVerb::Set) &&
            materialEditorVisible->Supports(UiSettingsCommandVerb::Toggle) &&
            materialEditorVisible->Supports(UiSettingsCommandVerb::Reset),
        "Material drawer visibility must remain a complete CLI command even "
        "though it is excluded from copied snapshots");
    Require(dynamicCount == 50u,
        "runtime lights and materials must retain their 50 dynamic controls");
    Require(dynamicSelections == ExpectedDynamicSelections,
        "dynamic selections must cover adapters, scenes, lights, and materials");
    Require(visibility == ExpectedVisibility,
        "Visibility commands must exactly mirror the direct UI settings");
    Require(representation == ExpectedRepresentation,
        "Representation commands must exactly mirror the direct UI settings");
    Require(pathing == ExpectedPathing,
        "Pathing commands must exactly mirror the transport policy drawer");
    Require(noise == ExpectedNoise,
        "Noise commands must exactly mirror the shared configuration drawer");
    Require(denoising == ExpectedDenoising,
        "Denoising commands must exactly mirror the four signal groups");
    Require(directionalShadows == ExpectedDirectionalShadows,
        "Directional Shadow commands must exactly mirror the direct UI settings");

    const auto requireDomain = [](std::string_view name, std::string_view domain) {
        const UiSettingsCommandDefinition* definition = Find(name);
        Require(definition && definition->domain == domain,
            "command domain must exactly match its visible values");
    };
    requireDomain(
        "reset-settings",
        "restore renderer and interface factory settings");
    requireDomain("ui.skin", "amp|ogg");
    requireDomain(
        "ui.font-family",
        "codex|noto-sans|proggy-clean");
    requireDomain("ui.animations", "on|off");
    requireDomain("ui.override-visual-maxes", "on|off");
    requireDomain("ui.accent.main", "display rgb float3 0..1");
    requireDomain("ui.accent.negative", "display rgb float3 0..1");
    requireDomain("ui.accent.positive", "display rgb float3 0..1");
    for (const std::string_view name : {
            std::string_view("ui.accent.primary"),
            std::string_view("ui.accent.secondary"),
            std::string_view("ui.accent.tertiary"),
            std::string_view("ui.accent.font"),
            std::string_view("ui.accent.primary-background") })
    {
        requireDomain(name, "display rgba float4 0..1");
    }
    requireDomain(
        "gpu.adaptive-sync",
        "off|vendor-agnostic|nvidia-exclusive");
    requireDomain("lighting.solution", "ray-marching|path-tracing");
    requireDomain("pathing.solver", "rtx-pt|restir-pt|restir-gi");
    requireDomain("pathing.nee", "uniform|power|nee-at");
    requireDomain("pathing.max-bounces", "integer 1..96");
    requireDomain("pathing.russian-roulette", "on|off");
    requireDomain("pathing.nee-candidates", "integer 1..63");
    requireDomain("pathing.samples-per-pixel", "integer 1..8");
    requireDomain("pathing.shared-primary-surface", "on|off");
    requireDomain("pathing.ser", "on|off");
    requireDomain("pathing.rtxdi", "on|off");
    requireDomain("pathing.temporal-reuse", "on|off");
    requireDomain("pathing.spatial-neighbors", "integer 0..4");
    requireDomain("pathing.reuse-proposals-during-motion", "on|off");
    requireDomain("noise.pattern",
        "spatial-white|spatial-blue|spatiotemporal-blue");
    requireDomain("noise.resolution",
        "64x64|128x128|256x256|512x512");
    requireDomain("noise.animate-samples", "on|off");
    requireDomain("noise.accumulate-samples", "on|off");
    requireDomain(
        "noise.accumulation-mode",
        "progressive|responsive|variance-guided");
    requireDomain(
        "noise.accumulation-averaging",
        "cumulative|exponential");
    requireDomain(
        "noise.accumulation-scheduling",
        "every-pixel|variance-guided");
    requireDomain(
        "noise.accumulation-effective-history",
        "integer 2..4096");
    requireDomain(
        "noise.accumulation-history-preset",
        "quick-preview|responsive|balanced|stable|very-stable|custom (get only)");
    requireDomain(
        "noise.accumulation-minimum-samples",
        "integer 2..256");
    requireDomain(
        "noise.accumulation-target-error",
        "float 0.001..0.25");
    requireDomain(
        "noise.accumulation-minimum-update-rate",
        "float 0.00390625..1");
    requireDomain(
        "noise.accumulation-workload-preset",
        "full-quality|balanced|performance|maximum-savings|custom (get only)");
    requireDomain("visibility.specify-noise", "on|off");
    requireDomain("visibility.noise-pattern",
        "spatial-white|spatial-blue|spatiotemporal-blue");
    requireDomain("visibility.noise-resolution",
        "64x64|128x128|256x256|512x512");
    requireDomain("visibility.animate-samples", "on|off");
    requireDomain("anti-aliasing.taa.temporal-cost",
        "full-quality|reduced|minimum");
    requireDomain("anti-aliasing.taa.jitter-sequence",
        "rotated-grid-4|uniform-helix-4|halton-8|halton-16|halton-32|sobol-32");
    requireDomain("anti-aliasing.taa.previous-depth",
        "nearest-texel|four-texel-footprint");
    requireDomain("anti-aliasing.taa.history.frames",
        "-1 or integer 1..32; -1 uses quality preset");
    requireDomain("anti-aliasing.taa.history.strength",
        "-1 or float 0..2; -1 uses quality preset");
    requireDomain("anti-aliasing.fxaa.enabled", "on|off");
    requireDomain("anti-aliasing.fxaa.quality", "low|medium|high|ultra");
    requireDomain("anti-aliasing.fxaa.edge-sharpness", "float 2..8");
    requireDomain("anti-aliasing.fxaa.edge-threshold",
        "float 0.08..0.25");
    requireDomain("anti-aliasing.fxaa.minimum-edge-threshold",
        "float 0.04..0.06");
    requireDomain("anti-aliasing.msaa.quality", "low|medium|high|ultra");
    requireDomain("anti-aliasing.msaa.per-sample-shadows", "on|off");
    requireDomain("debug.world.materials",
        "scene|white|white-detail|white-lighting");
    requireDomain("debug.visibility.view",
        "final|ambient-visibility|traced-indirect|applied-indirect");
    requireDomain("representation.bvh.build-preference",
        "fast-trace|balanced|fast-build");
    requireDomain("representation.blas.update-mode", "rebuild|refit");
    requireDomain("representation.tlas.update-mode", "rebuild|refit");
    requireDomain("representation.allow-ray-traversal", "on|off");
    requireDomain("sky.visibility.enabled", "on|off");
    requireDomain("sky.visibility.diffuse-ibl", "on|off");
    requireDomain("sky.visibility.specular-ibl", "on|off");
    requireDomain("sky.visibility.ratio-estimator", "on|off");
    requireDomain("sky.visibility.output-hit-distance", "on|off");
    requireDomain("sky.visibility.specify-noise", "on|off");
    requireDomain("sky.visibility.animate-samples", "on|off");
    requireDomain("sky.visibility.max-distance", "max|32m|16m|8m|4m|2m");
    requireDomain("sky.visibility.noise-pattern",
        "spatial-white|spatial-blue|spatiotemporal-blue");
    requireDomain("sky.visibility.noise-resolution",
        "64x64|128x128|256x256|512x512");
    requireDomain("sky.visibility.samples-per-pixel",
        "1|2|4|8|16|32|64");
    requireDomain("sky.visibility.ray-bias", "world units 0..0.1");
    requireDomain("shadows.ray-traced.enabled", "on|off");
    requireDomain("shadows.ray-traced.ratio-estimator", "on|off");
    requireDomain("shadows.ray-traced.output-hit-distance", "on|off");
    requireDomain("shadows.ray-traced.hard-shadows", "on|off");
    requireDomain("shadows.ray-traced.specify-noise", "on|off");
    requireDomain("shadows.ray-traced.animate-samples", "on|off");
    requireDomain("shadows.ray-traced.max-distance",
        "max|32m|16m|8m|4m|2m");
    requireDomain("shadows.ray-traced.noise-pattern",
        "spatial-white|spatial-blue|spatiotemporal-blue");
    requireDomain("shadows.ray-traced.noise-resolution",
        "64x64|128x128|256x256|512x512");
    requireDomain("shadows.ray-traced.samples-per-pixel",
        "1|2|4|8|16|32|64");
    requireDomain("shadows.ray-traced.ray-bias",
        "world units 0..0.1");
    requireDomain("denoising.ao.method",
        "raw|joint-bilateral|gaussian-bilateral|reblur");
    requireDomain("denoising.gi.method",
        "raw|joint-bilateral|gaussian-bilateral|reblur|relax");
    requireDomain("denoising.shadows.method",
        "raw|joint-bilateral|gaussian-bilateral|sigma");
    requireDomain("denoising.sky.method",
        "raw|joint-bilateral|gaussian-bilateral|reblur|relax");
    for (const std::string_view name : {
            std::string_view("denoising.ao.radius"),
            std::string_view("denoising.gi.radius"),
            std::string_view("denoising.shadows.radius"),
            std::string_view("denoising.sky.radius") })
    {
        requireDomain(name, "float 1..8");
    }
    requireDomain("sky.environment",
        "day|bright-overcast|soft-day|night|starry-night|cloudy");
    requireDomain(
        "denoising.path-tracing.method",
        "raw|spatial-path-resolve");
    requireDomain(
        "denoising.path-tracing.signal-groups",
        "integer 1..3; RESTIR supports 1..2");
    requireDomain(
        "denoising.path-tracing.resolve-strength",
        "float 0..1");
    requireDomain(
        "denoising.path-tracing.firefly-filter",
        "on|off");
    requireDomain(
        "denoising.path-tracing.firefly-threshold",
        "float 0.01..1000000");
    requireDomain("denoising.ao.quality",
        "performance|balanced|quality|ultra");
    requireDomain("denoising.sky.resolution", "quarter|half|full");
    requireDomain(
        "debug.path-tracing.view",
        "final|albedo|geometric-normal|shading-normal|sample-count|update-rate|signal-group|direct-reservoir|indirect-reservoir|primary-transport|indirect-transport");
    requireDomain(
        "debug.pbr.filter",
        "final|surface-normals|geometry-normals|normal-difference|diffuse-environment|environment-direction|reflected-environment|brdf-response|specular-environment|all-environment-light|specular-visibility|environment-level|sky-visibility");
    requireDomain("sky.auto-exposure.enabled", "on|off");
    requireDomain(
        "sky.auto-exposure.exposure-compensation",
        "float -18..8 ev");
    requireDomain(
        "sky.auto-exposure.maximum-brightening",
        "float 0..16 ev");
    requireDomain(
        "sky.auto-exposure.maximum-darkening",
        "float 0..16 ev");
    requireDomain(
        "sky.auto-exposure.adjustment-period",
        "float 0.05..5 seconds");
    requireDomain(
        "light.selected.flashlight.angular-size",
        "degrees 0..20 at 1 meter; flashlight_1");
    requireDomain(
        "light.selected.flashlight.stationary-when-idle",
        "on|off; flashlight_1");

    const auto requireKindAndSection = [](
        std::string_view name,
        UiSettingsCommandKind kind,
        UiSettingsCommandSection section) {
        const UiSettingsCommandDefinition* definition = Find(name);
        Require(definition && definition->kind == kind &&
                definition->section == section,
            "command kind and section must match the represented control");
    };
    for (const std::string_view name : {
            std::string_view("representation.bvh.build-preference"),
            std::string_view("representation.blas.update-mode"),
            std::string_view("representation.tlas.update-mode") })
    {
        requireKindAndSection(
            name, Kind::Enum, Section::Representation);
    }
    requireKindAndSection(
        "representation.allow-ray-traversal",
        Kind::Boolean,
        Section::Representation);
    requireKindAndSection(
        "lighting.solution", Kind::Enum, Section::General);
    requireKindAndSection(
        "pathing.solver", Kind::Enum, Section::Pathing);
    requireKindAndSection(
        "pathing.nee", Kind::Enum, Section::Pathing);
    requireKindAndSection(
        "pathing.max-bounces", Kind::Integer, Section::Pathing);
    requireKindAndSection(
        "pathing.russian-roulette", Kind::Boolean, Section::Pathing);
    requireKindAndSection(
        "pathing.nee-candidates", Kind::Integer, Section::Pathing);
    requireKindAndSection(
        "pathing.samples-per-pixel", Kind::Integer, Section::Pathing);
    requireKindAndSection(
        "pathing.shared-primary-surface", Kind::Boolean, Section::Pathing);
    requireKindAndSection(
        "pathing.ser", Kind::Boolean, Section::Pathing);
    requireKindAndSection(
        "pathing.rtxdi", Kind::Boolean, Section::Pathing);
    requireKindAndSection(
        "pathing.temporal-reuse", Kind::Boolean, Section::Pathing);
    requireKindAndSection(
        "pathing.spatial-neighbors", Kind::Integer, Section::Pathing);
    requireKindAndSection("ui.skin", Kind::Enum, Section::Ui);
    requireKindAndSection("ui.font-family", Kind::Enum, Section::Ui);
    requireKindAndSection("ui.animations", Kind::Boolean, Section::Ui);
    requireKindAndSection(
        "ui.override-visual-maxes", Kind::Boolean, Section::Ui);
    Require(
        Find("ui.font-family")->Supports(UiSettingsCommandVerb::Reset) &&
            !Find("ui.font-family")->dynamic,
        "the font-family preference must be a resettable static value");
    Require(
        Find("ui.animations")->Supports(UiSettingsCommandVerb::Reset) &&
            !Find("ui.animations")->dynamic,
        "the Interface animation preference must be a resettable static value");
    Require(
        Find("ui.override-visual-maxes")->Supports(
            UiSettingsCommandVerb::Reset) &&
            !Find("ui.override-visual-maxes")->dynamic,
        "the visual-max override must be a resettable static value");
    for (const std::string_view name : {
            std::string_view("ui.accent.main"),
            std::string_view("ui.accent.negative"),
            std::string_view("ui.accent.positive") })
    {
        requireKindAndSection(name, Kind::Float3, Section::Ui);
        const UiSettingsCommandDefinition* definition = Find(name);
        Require(definition &&
                definition->Supports(UiSettingsCommandVerb::Reset) &&
                !definition->dynamic,
            "interface accent colors must be resettable static values");
    }
    for (const std::string_view name : {
            std::string_view("ui.accent.primary"),
            std::string_view("ui.accent.secondary"),
            std::string_view("ui.accent.tertiary"),
            std::string_view("ui.accent.font"),
            std::string_view("ui.accent.primary-background") })
    {
        requireKindAndSection(name, Kind::Float4, Section::Ui);
        const UiSettingsCommandDefinition* definition = Find(name);
        Require(definition &&
                definition->Supports(UiSettingsCommandVerb::Reset) &&
                !definition->dynamic,
            "RGBA interface palette roles must be resettable static values");
    }
    requireKindAndSection("noise.pattern", Kind::Enum, Section::Noise);
    requireKindAndSection("noise.resolution", Kind::Enum, Section::Noise);
    requireKindAndSection(
        "noise.animate-samples", Kind::Boolean, Section::Noise);
    requireKindAndSection(
        "noise.accumulate-samples", Kind::Boolean, Section::Noise);
    requireKindAndSection(
        "noise.accumulation-mode", Kind::Enum, Section::Noise);
    requireKindAndSection(
        "noise.accumulation-averaging", Kind::Enum, Section::Noise);
    requireKindAndSection(
        "noise.accumulation-scheduling", Kind::Enum, Section::Noise);
    requireKindAndSection(
        "noise.accumulation-effective-history", Kind::Integer, Section::Noise);
    requireKindAndSection(
        "noise.accumulation-minimum-samples", Kind::Integer, Section::Noise);
    requireKindAndSection(
        "noise.accumulation-target-error", Kind::Float, Section::Noise);
    requireKindAndSection(
        "noise.accumulation-minimum-update-rate", Kind::Float, Section::Noise);
    requireKindAndSection(
        "visibility.specify-noise", Kind::Boolean, Section::Visibility);
    requireKindAndSection(
        "visibility.noise-pattern", Kind::Enum, Section::Visibility);
    requireKindAndSection(
        "visibility.noise-resolution", Kind::Enum, Section::Visibility);
    requireKindAndSection(
        "shadows.ray-traced.enabled",
        Kind::Boolean,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.ratio-estimator",
        Kind::Boolean,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.output-hit-distance",
        Kind::Boolean,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.hard-shadows",
        Kind::Boolean,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.animate-samples",
        Kind::Boolean,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.specify-noise",
        Kind::Boolean,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.noise-pattern",
        Kind::Enum,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.noise-resolution",
        Kind::Enum,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.samples-per-pixel",
        Kind::Enum,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.ray-bias",
        Kind::Float,
        Section::DirectionalShadows);
    requireKindAndSection(
        "shadows.ray-traced.max-distance",
        Kind::Enum,
        Section::DirectionalShadows);
    requireKindAndSection(
        "sky.visibility.enabled", Kind::Boolean, Section::Sky);
    requireKindAndSection(
        "sky.visibility.diffuse-ibl", Kind::Boolean, Section::Sky);
    requireKindAndSection(
        "sky.visibility.specular-ibl", Kind::Boolean, Section::Sky);
    requireKindAndSection(
        "sky.visibility.ratio-estimator", Kind::Boolean, Section::Sky);
    requireKindAndSection(
        "sky.visibility.output-hit-distance", Kind::Boolean, Section::Sky);
    requireKindAndSection(
        "sky.visibility.specify-noise", Kind::Boolean, Section::Sky);
    requireKindAndSection(
        "sky.visibility.samples-per-pixel", Kind::Enum, Section::Sky);
    requireKindAndSection(
        "sky.visibility.noise-pattern", Kind::Enum, Section::Sky);
    requireKindAndSection(
        "sky.visibility.noise-resolution", Kind::Enum, Section::Sky);
    requireKindAndSection(
        "sky.visibility.animate-samples", Kind::Boolean, Section::Sky);
    requireKindAndSection(
        "sky.visibility.max-distance", Kind::Enum, Section::Sky);
    requireKindAndSection(
        "sky.visibility.ray-bias", Kind::Float, Section::Sky);
    requireKindAndSection(
        "sky.auto-exposure.enabled", Kind::Boolean, Section::Sky);
    requireKindAndSection(
        "sky.auto-exposure.exposure-compensation",
        Kind::Float,
        Section::Sky);
    requireKindAndSection(
        "sky.auto-exposure.maximum-brightening",
        Kind::Float,
        Section::Sky);
    requireKindAndSection(
        "sky.auto-exposure.maximum-darkening",
        Kind::Float,
        Section::Sky);
    requireKindAndSection(
        "sky.auto-exposure.adjustment-period", Kind::Float, Section::Sky);
    requireKindAndSection(
        "light.selected.flashlight.angular-size", Kind::Float, Section::Lights);
    requireKindAndSection(
        "light.selected.flashlight.stationary-when-idle",
        Kind::Boolean,
        Section::Lights);
    Require(!Find("light.selected.flashlight.adjustment-speed") &&
            !Find("light.selected.flashlight.time-to-action"),
        "retired flashlight camera-centering controls must remain absent");

    const UiSettingsCommandDefinition* capture = Find("capture");
    Require(capture && capture->kind == Kind::Action &&
            capture->section == Section::Footer,
        "Capture must be the footer screenshot action");

    Require(!Find("visibility.profile") &&
            !Find("ui.accent.primary-font") &&
            !Find("ui.accent.secondary-font") &&
            !Find("ui.accent.secondary-background") &&
            !Find("visibility.sampling.noise-pattern") &&
            !Find("visibility.reconstruction.method") &&
            !Find("visibility.buffers.preset") &&
            !Find("anti-aliasing.method") &&
            !Find("anti-aliasing.taa.nearest-texel") &&
            !Find("debug.visibility.indirect-diffuse-only") &&
            !Find("debug.shadows.edge-overlay") &&
            !Find("debug.shadows.overlay-opacity") &&
            !Find("debug.shadows.isolation") &&
            !Find("shadows.screen-space-directional.debug-view") &&
            !Find("shadows.screen-space-directional.enabled") &&
            !Find("shadows.ratio-estimator.enabled") &&
            !Find("sky.auto-exposure.brightness") &&
            !Find("screenshot") &&
            !Find("visibility.noise") &&
            !Find("statistics.effect"),
        "legacy planner, mode, buffer-profile, and debug-view paths must stay retired");

    std::set<std::string> exemptions;
    for (const std::string_view name : UiSettingsNavigationExemptions)
    {
        Require(IsStableLowercaseName(name) &&
                exemptions.insert(std::string(name)).second,
            "navigation exemptions must be stable and unique");
    }
    for (const std::string_view name : UiSettingsTelemetryExemptions)
    {
        Require(IsStableLowercaseName(name) &&
                exemptions.insert(std::string(name)).second,
            "telemetry exemptions must be stable and unique");
        Require(name.find("benchmark") == std::string_view::npos,
            "benchmark telemetry exemptions must stay retired");
    }
    Require(UiSettingsNavigationExemptions.size() == 5u &&
            UiSettingsTelemetryExemptions.size() == 4u,
        "only live navigation and telemetry surfaces may be exempted");

    return EXIT_SUCCESS;
}
