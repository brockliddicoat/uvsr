#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    enum class UiSettingsCommandKind : std::uint8_t
    {
        Boolean,
        Integer,
        Float,
        Float3,
        Enum,
        DynamicSelection,
        Action,
        Float4
    };

    enum class UiSettingsCommandSection : std::uint8_t
    {
        Ui,
        General,
        Pathing,
        Representation,
        Noise,
        Visibility,
        Denoising,
        Aliasing,
        Debug,
        Sky,
        Lights,
        DirectionalShadows,
        Materials,
        Footer,
        Count
    };

    enum class UiSettingsCommandVerb : std::uint8_t
    {
        Get = 1u << 0u,
        Set = 1u << 1u,
        Toggle = 1u << 2u,
        Reset = 1u << 3u,
        Run = 1u << 4u
    };

    struct UiSettingsCommandDefinition
    {
        std::string_view name;
        UiSettingsCommandKind kind = UiSettingsCommandKind::Enum;
        UiSettingsCommandSection section = UiSettingsCommandSection::General;
        std::uint8_t supportedVerbs = 0u;
        bool dynamic = false;
        std::string_view domain;

        [[nodiscard]] constexpr bool Supports(UiSettingsCommandVerb verb) const
        {
            return (supportedVerbs & static_cast<std::uint8_t>(verb)) != 0u;
        }
    };

    [[nodiscard]] constexpr UiSettingsCommandDefinition Value(
        std::string_view name,
        UiSettingsCommandKind kind,
        UiSettingsCommandSection section,
        std::string_view domain,
        bool supportsReset = true,
        bool dynamic = false)
    {
        const auto verb = [](UiSettingsCommandVerb value) {
            return static_cast<std::uint8_t>(value);
        };
        return {
            name,
            kind,
            section,
            static_cast<std::uint8_t>(
                verb(UiSettingsCommandVerb::Get) |
                verb(UiSettingsCommandVerb::Set) |
                (kind == UiSettingsCommandKind::Boolean
                    ? verb(UiSettingsCommandVerb::Toggle)
                    : 0u) |
                (supportsReset ? verb(UiSettingsCommandVerb::Reset) : 0u)),
            dynamic,
            domain
        };
    }

    [[nodiscard]] constexpr UiSettingsCommandDefinition Action(
        std::string_view name,
        UiSettingsCommandSection section,
        std::string_view domain)
    {
        return {
            name,
            UiSettingsCommandKind::Action,
            section,
            static_cast<std::uint8_t>(UiSettingsCommandVerb::Run),
            false,
            domain
        };
    }

    using Kind = UiSettingsCommandKind;
    using Section = UiSettingsCommandSection;

    // One descriptor per control in the current Settings and Material Editor UI.
    inline constexpr auto UiSettingsCommandCatalog = std::array{
        // UI.
        Value("ui.skin", Kind::Enum, Section::Ui, "amp|ogg"),
        Value("ui.font-family", Kind::Enum, Section::Ui, "codex|noto-sans|proggy-clean"),
        Value("ui.animations", Kind::Boolean, Section::Ui, "on|off"),
        Value("ui.override-visual-maxes", Kind::Boolean, Section::Ui, "on|off"),
        Value("ui.accent.main", Kind::Float3, Section::Ui, "display rgb float3 0..1"),
        Value("ui.accent.negative", Kind::Float3, Section::Ui, "display rgb float3 0..1"),
        Value("ui.accent.positive", Kind::Float3, Section::Ui, "display rgb float3 0..1"),
        Value("ui.accent.primary", Kind::Float4, Section::Ui, "display rgba float4 0..1"),
        Value("ui.accent.secondary", Kind::Float4, Section::Ui, "display rgba float4 0..1"),
        Value("ui.accent.tertiary", Kind::Float4, Section::Ui, "display rgba float4 0..1"),
        Value("ui.accent.font", Kind::Float4, Section::Ui, "display rgba float4 0..1"),
        Value("ui.accent.primary-background", Kind::Float4, Section::Ui, "display rgba float4 0..1"),
        Value("ui.visible", Kind::Boolean, Section::Ui, "on|off"),
        Value("ui.settings-collapsed", Kind::Boolean, Section::Ui, "on|off"),
        Value("ui.zoom", Kind::Enum, Section::Ui, "off|2x|3x|4x|5x"),
        Value("material-editor.visible", Kind::Boolean, Section::Ui, "on|off"),

        // General.
        Value("lighting.solution", Kind::Enum, Section::General, "ray-marching|path-tracing"),
        Value("gpu.adapter", Kind::DynamicSelection, Section::General, "runtime adapter index or unique display name", false, true),
        Value("gpu.adaptive-sync", Kind::Enum, Section::General, "off|vendor-agnostic|nvidia-exclusive"),
        Value("camera.mode", Kind::Enum, Section::General, "freelook|locked"),
        Value("camera.location", Kind::Enum, Section::General, "piloted|position-1"),
        Value("scene.current", Kind::DynamicSelection, Section::General, "runtime scene filename or unique display name", false, true),
        Action("open-scene-folder", Section::General, "open the active scene directory"),

        // Pathing.
        Value("pathing.solver", Kind::Enum, Section::Pathing, "rtx-pt|restir-pt|restir-gi"),
        Value("pathing.nee", Kind::Enum, Section::Pathing, "uniform|power|nee-at"),
        Value("pathing.max-bounces", Kind::Integer, Section::Pathing, "integer 1..96"),
        Value("pathing.russian-roulette", Kind::Boolean, Section::Pathing, "on|off"),
        Value("pathing.nee-candidates", Kind::Integer, Section::Pathing, "integer 1..63"),
        Value("pathing.samples-per-pixel", Kind::Integer, Section::Pathing, "integer 1..8"),
        Value("pathing.shared-primary-surface", Kind::Boolean, Section::Pathing, "on|off"),
        Value("pathing.ser", Kind::Boolean, Section::Pathing, "on|off"),
        Value("pathing.rtxdi", Kind::Boolean, Section::Pathing, "on|off"),
        Value("pathing.temporal-reuse", Kind::Boolean, Section::Pathing, "on|off"),
        Value("pathing.spatial-neighbors", Kind::Integer, Section::Pathing, "integer 0..4"),
        Value("pathing.reuse-proposals-during-motion", Kind::Boolean, Section::Pathing, "on|off"),

        // Representation.
        Value("representation.bvh.build-preference", Kind::Enum, Section::Representation, "fast-trace|balanced|fast-build"),
        Value("representation.blas.update-mode", Kind::Enum, Section::Representation, "rebuild|refit"),
        Value("representation.tlas.update-mode", Kind::Enum, Section::Representation, "rebuild|refit"),
        Value("representation.allow-ray-traversal", Kind::Boolean, Section::Representation, "on|off"),

        // Noise.
        Value("noise.pattern", Kind::Enum, Section::Noise, "spatial-white|spatial-blue|spatiotemporal-blue"),
        Value("noise.resolution", Kind::Enum, Section::Noise, "64x64|128x128|256x256|512x512"),
        Value("noise.animate-samples", Kind::Boolean, Section::Noise, "on|off"),
        Value("noise.accumulate-samples", Kind::Boolean, Section::Noise, "on|off"),
        Value("noise.accumulation-mode", Kind::Enum, Section::Noise, "progressive|responsive|variance-guided"),
        Value("noise.accumulation-averaging", Kind::Enum, Section::Noise, "cumulative|exponential"),
        Value("noise.accumulation-scheduling", Kind::Enum, Section::Noise, "every-pixel|variance-guided"),
        Value("noise.accumulation-effective-history", Kind::Integer, Section::Noise, "integer 2..4096"),
        Value("noise.accumulation-history-preset", Kind::Enum, Section::Noise, "quick-preview|responsive|balanced|stable|very-stable|custom (get only)"),
        Value("noise.accumulation-minimum-samples", Kind::Integer, Section::Noise, "integer 2..256"),
        Value("noise.accumulation-target-error", Kind::Float, Section::Noise, "float 0.001..0.25"),
        Value("noise.accumulation-minimum-update-rate", Kind::Float, Section::Noise, "float 0.00390625..1"),
        Value("noise.accumulation-workload-preset", Kind::Enum, Section::Noise, "full-quality|balanced|performance|maximum-savings|custom (get only)"),

        // Visibility.
        Value("visibility.enabled", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.quality", Kind::Enum, Section::Visibility, "low|medium|high|ultra|custom"),
        Value("visibility.estimator", Kind::Enum, Section::Visibility, "projected-angle|solid-angle|cosine-weighted"),
        Value("visibility.resolution", Kind::Enum, Section::Visibility, "full|half|quarter"),
        Value("visibility.samples", Kind::Integer, Section::Visibility, "integer 1..64"),
        Value("visibility.radius", Kind::Float, Section::Visibility, "float 0.1..10"),
        Value("visibility.thickness", Kind::Float, Section::Visibility, "float 0.01..2"),
        Value("visibility.distribution", Kind::Float, Section::Visibility, "float 0.25..8"),
        Value("visibility.specify-noise", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.noise-pattern", Kind::Enum, Section::Visibility, "spatial-white|spatial-blue|spatiotemporal-blue"),
        Value("visibility.noise-resolution", Kind::Enum, Section::Visibility, "64x64|128x128|256x256|512x512"),
        Value("visibility.animate-samples", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.ao.enabled", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.ao.strength", Kind::Float, Section::Visibility, "float 0..8"),
        Value("visibility.ao.output-hit-distance", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.ao.precision", Kind::Enum, Section::Visibility, "16-bit|32-bit"),
        Value("visibility.gi.enabled", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.gi.output-hit-distance", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.gi.intensity", Kind::Float, Section::Visibility, "float 0..16"),
        Value("visibility.gi.precision", Kind::Enum, Section::Visibility, "16-bit|32-bit"),
        // Denoising.
        Value("denoising.ao.method", Kind::Enum, Section::Denoising, "raw|joint-bilateral|gaussian-bilateral|reblur"),
        Value("denoising.ao.radius", Kind::Float, Section::Denoising, "float 1..8"),
        Value("denoising.ao.quality", Kind::Enum, Section::Denoising, "performance|balanced|quality|ultra"),
        Value("denoising.ao.resolution", Kind::Enum, Section::Denoising, "quarter|half|full"),
        Value("denoising.ao.history", Kind::Integer, Section::Denoising, "integer 1..32"),
        Value("denoising.ao.disocclusion", Kind::Float, Section::Denoising, "float 0.001..0.1"),
        Value("denoising.ao.anti-lag", Kind::Float, Section::Denoising, "float 0..1"),
        Value("denoising.gi.method", Kind::Enum, Section::Denoising, "raw|joint-bilateral|gaussian-bilateral|reblur|relax"),
        Value("denoising.gi.radius", Kind::Float, Section::Denoising, "float 1..8"),
        Value("denoising.gi.quality", Kind::Enum, Section::Denoising, "performance|balanced|quality|ultra"),
        Value("denoising.gi.resolution", Kind::Enum, Section::Denoising, "quarter|half|full"),
        Value("denoising.gi.history", Kind::Integer, Section::Denoising, "integer 1..32"),
        Value("denoising.gi.disocclusion", Kind::Float, Section::Denoising, "float 0.001..0.1"),
        Value("denoising.gi.anti-lag", Kind::Float, Section::Denoising, "float 0..1"),
        Value("denoising.shadows.method", Kind::Enum, Section::Denoising, "raw|joint-bilateral|gaussian-bilateral|sigma"),
        Value("denoising.shadows.radius", Kind::Float, Section::Denoising, "float 1..8"),
        Value("denoising.shadows.quality", Kind::Enum, Section::Denoising, "performance|balanced|quality|ultra"),
        Value("denoising.shadows.resolution", Kind::Enum, Section::Denoising, "quarter|half|full"),
        Value("denoising.shadows.disocclusion", Kind::Float, Section::Denoising, "float 0.001..0.1"),
        Value("denoising.sky.method", Kind::Enum, Section::Denoising, "raw|joint-bilateral|gaussian-bilateral|reblur|relax"),
        Value("denoising.sky.radius", Kind::Float, Section::Denoising, "float 1..8"),
        Value("denoising.sky.quality", Kind::Enum, Section::Denoising, "performance|balanced|quality|ultra"),
        Value("denoising.sky.resolution", Kind::Enum, Section::Denoising, "quarter|half|full"),
        Value("denoising.sky.history", Kind::Integer, Section::Denoising, "integer 1..32"),
        Value("denoising.sky.disocclusion", Kind::Float, Section::Denoising, "float 0.001..0.1"),
        Value("denoising.sky.anti-lag", Kind::Float, Section::Denoising, "float 0..1"),
        Value("denoising.path-tracing.signal-groups", Kind::Integer, Section::Denoising, "integer 1..3; RESTIR supports 1..2"),
        Value("denoising.path-tracing.resolve-strength", Kind::Float, Section::Denoising, "float 0..1"),
        Value("denoising.path-tracing.firefly-filter", Kind::Boolean, Section::Denoising, "on|off"),
        Value("denoising.path-tracing.firefly-threshold", Kind::Float, Section::Denoising, "float 0.01..1000000"),
        Value("denoising.path-tracing.method", Kind::Enum, Section::Denoising, "raw|spatial-path-resolve"),

        // Anti-Aliasing.
        Value("anti-aliasing.taa.enabled", Kind::Boolean, Section::Aliasing, "on|off"),
        Value("anti-aliasing.taa.quality", Kind::Enum, Section::Aliasing, "low|medium|high|ultra"),
        Value("anti-aliasing.taa.jitter-sequence", Kind::Enum, Section::Aliasing, "rotated-grid-4|uniform-helix-4|halton-8|halton-16|halton-32|sobol-32"),
        Value("anti-aliasing.taa.previous-depth", Kind::Enum, Section::Aliasing, "nearest-texel|four-texel-footprint"),
        Value("anti-aliasing.taa.temporal-cost", Kind::Enum, Section::Aliasing, "full-quality|reduced|minimum"),
        Value("anti-aliasing.taa.motion-source", Kind::Enum, Section::Aliasing, "preset|center|closest-cross|edge-dilation"),
        Value("anti-aliasing.taa.current-sample", Kind::Enum, Section::Aliasing, "preset|direct|de-jittered"),
        Value("anti-aliasing.taa.history-filter", Kind::Enum, Section::Aliasing, "preset|bilinear|bicubic|five-tap-bicubic|nine-tap-bicubic"),
        Value("anti-aliasing.taa.rectification", Kind::Enum, Section::Aliasing, "preset|pair-tristimulus|variance-chroma"),
        Value("anti-aliasing.taa.history.frames", Kind::Integer, Section::Aliasing, "-1 or integer 1..32; -1 uses quality preset"),
        Value("anti-aliasing.taa.history.strength", Kind::Float, Section::Aliasing, "-1 or float 0..2; -1 uses quality preset"),
        Value("anti-aliasing.taa.history.storage", Kind::Enum, Section::Aliasing, "temporal-cost|robust|compact"),
        Value("anti-aliasing.taa.history.weight", Kind::Enum, Section::Aliasing, "temporal-cost|confidence-recurrence|immediate-horizon"),
        Value("anti-aliasing.taa.motion-trust", Kind::Enum, Section::Aliasing, "temporal-cost|linear-speed|squared-speed"),
        Value("anti-aliasing.taa.rectification-clip", Kind::Enum, Section::Aliasing, "temporal-cost|velocity-dilated|tight-component"),
        Value("anti-aliasing.taa.blend-domain", Kind::Enum, Section::Aliasing, "temporal-cost|luminance-compressed|linear-rgb"),
        Value("anti-aliasing.taa.preset-sharpening", Kind::Enum, Section::Aliasing, "auto|off|on"),
        Value("anti-aliasing.sharpen.enabled", Kind::Boolean, Section::Aliasing, "on|off"),
        Value("anti-aliasing.sharpen.strength", Kind::Float, Section::Aliasing, "float 0..1"),
        Value("anti-aliasing.fxaa.enabled", Kind::Boolean, Section::Aliasing, "on|off"),
        Value("anti-aliasing.fxaa.quality", Kind::Enum, Section::Aliasing, "low|medium|high|ultra"),
        Value("anti-aliasing.fxaa.edge-sharpness", Kind::Float, Section::Aliasing, "float 2..8"),
        Value("anti-aliasing.fxaa.edge-threshold", Kind::Float, Section::Aliasing, "float 0.08..0.25"),
        Value("anti-aliasing.fxaa.minimum-edge-threshold", Kind::Float, Section::Aliasing, "float 0.04..0.06"),
        Value("anti-aliasing.msaa.enabled", Kind::Boolean, Section::Aliasing, "on|off"),
        Value("anti-aliasing.msaa.quality", Kind::Enum, Section::Aliasing, "low|medium|high|ultra"),
        Value("anti-aliasing.msaa.samples", Kind::Enum, Section::Aliasing, "2x|4x|8x|16x"),
        Value("anti-aliasing.msaa.per-sample-shadows", Kind::Boolean, Section::Aliasing, "on|off"),

        // Debug.
        Value("debug.world.materials", Kind::Enum, Section::Debug, "scene|white|white-detail|white-lighting"),
        Value("debug.visibility.view", Kind::Enum, Section::Debug, "final|ambient-visibility|traced-indirect|applied-indirect"),
        Value("debug.pbr.filter", Kind::Enum, Section::Debug, "final|surface-normals|geometry-normals|normal-difference|diffuse-environment|environment-direction|reflected-environment|brdf-response|specular-environment|all-environment-light|specular-visibility|environment-level|sky-visibility"),
        Value("debug.path-tracing.view", Kind::Enum, Section::Debug, "final|albedo|geometric-normal|shading-normal|sample-count|update-rate|signal-group|direct-reservoir|indirect-reservoir|primary-transport|indirect-transport"),

        // Sky.
        Value("sky.environment", Kind::Enum, Section::Sky, "day|bright-overcast|soft-day|night|starry-night|cloudy"),
        Value("sky.exposure", Kind::Float, Section::Sky, "float -8..8 ev"),
        Value("sky.auto-exposure.enabled", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.auto-exposure.exposure-compensation", Kind::Float, Section::Sky, "float -18..8 ev"),
        Value("sky.auto-exposure.maximum-brightening", Kind::Float, Section::Sky, "float 0..16 ev"),
        Value("sky.auto-exposure.maximum-darkening", Kind::Float, Section::Sky, "float 0..16 ev"),
        Value("sky.auto-exposure.adjustment-period", Kind::Float, Section::Sky, "float 0.05..5 seconds"),
        Value("sky.diffuse-ibl", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.diffuse-ibl-strength", Kind::Float, Section::Sky, "float 0..4"),
        Value("sky.specular-ibl", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.specular-ibl-strength", Kind::Float, Section::Sky, "float 0..4"),
        Value("sky.environment-background", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.ambient-fill.enabled", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.enabled", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.diffuse-ibl", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.specular-ibl", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.ratio-estimator", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.output-hit-distance", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.samples-per-pixel", Kind::Enum, Section::Sky, "1|2|4|8|16|32|64"),
        Value("sky.visibility.specify-noise", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.noise-pattern", Kind::Enum, Section::Sky, "spatial-white|spatial-blue|spatiotemporal-blue"),
        Value("sky.visibility.noise-resolution", Kind::Enum, Section::Sky, "64x64|128x128|256x256|512x512"),
        Value("sky.visibility.animate-samples", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.max-distance", Kind::Enum, Section::Sky, "max|32m|16m|8m|4m|2m"),
        Value("sky.visibility.ray-bias", Kind::Float, Section::Sky, "world units 0..0.1"),

        // Lights.
        Value("light.selected", Kind::DynamicSelection, Section::Lights, "runtime editable-light index or unique name", true, true),
        Value("light.selected.azimuth", Kind::Float, Section::Lights, "degrees -180..180; directional or spot", true, true),
        Value("light.selected.elevation", Kind::Float, Section::Lights, "degrees -90..90; directional or spot", true, true),
        Value("light.selected.color", Kind::Float3, Section::Lights, "linear rgb float3", true, true),
        Value("light.selected.irradiance", Kind::Float, Section::Lights, "float 0..100; directional", true, true),
        Value("light.selected.angular-size", Kind::Float, Section::Lights, "float 0..20; directional", true, true),
        Value("light.selected.radius", Kind::Float, Section::Lights, "float 0.01..1; point or spot", true, true),
        Value("light.selected.intensity", Kind::Float, Section::Lights, "float 0..100; point or spot", true, true),
        Value("light.selected.inner-angle", Kind::Float, Section::Lights, "degrees 0..180; spot", true, true),
        Value("light.selected.outer-angle", Kind::Float, Section::Lights, "degrees 0..180; spot", true, true),
        Value("light.selected.flashlight.enabled", Kind::Boolean, Section::Lights, "on|off; flashlight_1", true, true),
        Value("light.selected.flashlight.cast-shadows", Kind::Boolean, Section::Lights, "on|off; flashlight_1", true, true),
        Value("light.selected.flashlight.output-hit-distance", Kind::Boolean, Section::Lights, "on|off; flashlight_1", true, true),
        Value("light.selected.flashlight.realistic", Kind::Boolean, Section::Lights, "on|off; flashlight_1", true, true),
        Value("light.selected.flashlight.stationary-when-idle", Kind::Boolean, Section::Lights, "on|off; flashlight_1", true, true),
        Value("light.selected.flashlight.hotspot-size", Kind::Float, Section::Lights, "float 0.2..0.75; flashlight_1", true, true),
        Value("light.selected.flashlight.hotspot-strength", Kind::Float, Section::Lights, "float 0..0.9; flashlight_1", true, true),
        Value("light.selected.flashlight.sway", Kind::Float, Section::Lights, "degrees 0..2; flashlight_1", true, true),
        Value("light.selected.flashlight.aim-correction", Kind::Float, Section::Lights, "seconds 0.01..0.5; flashlight_1", true, true),
        Value("light.selected.flashlight.brightness", Kind::Float, Section::Lights, "candela 25..4000; flashlight_1", true, true),
        Value("light.selected.flashlight.beam-size", Kind::Float, Section::Lights, "degrees 8..100; flashlight_1", true, true),
        Value("light.selected.flashlight.angular-size", Kind::Float, Section::Lights, "degrees 0..20 at 1 meter; flashlight_1", true, true),
        Value("light.selected.flashlight.beam-roundness", Kind::Float, Section::Lights, "float 0..1; flashlight_1", true, true),
        Value("light.selected.flashlight.edge-softness", Kind::Float, Section::Lights, "float 0..1; flashlight_1", true, true),
        Value("light.selected.flashlight.range", Kind::Float, Section::Lights, "meters 2..100; flashlight_1", true, true),
        Value("light.selected.flashlight.horizontal-offset", Kind::Float, Section::Lights, "meters -0.4..0.4; flashlight_1", true, true),
        Value("light.selected.flashlight.vertical-offset", Kind::Float, Section::Lights, "meters -0.4..0.4; flashlight_1", true, true),

        // Directional Shadows.
        Value("shadows.ray-traced.enabled", Kind::Boolean, Section::DirectionalShadows, "on|off"),
        Value("shadows.ray-traced.ratio-estimator", Kind::Boolean, Section::DirectionalShadows, "on|off"),
        Value("shadows.ray-traced.output-hit-distance", Kind::Boolean, Section::DirectionalShadows, "on|off"),
        Value("shadows.ray-traced.hard-shadows", Kind::Boolean, Section::DirectionalShadows, "on|off"),
        Value("shadows.ray-traced.samples-per-pixel", Kind::Enum, Section::DirectionalShadows, "1|2|4|8|16|32|64"),
        Value("shadows.ray-traced.specify-noise", Kind::Boolean, Section::DirectionalShadows, "on|off"),
        Value("shadows.ray-traced.noise-pattern", Kind::Enum, Section::DirectionalShadows, "spatial-white|spatial-blue|spatiotemporal-blue"),
        Value("shadows.ray-traced.noise-resolution", Kind::Enum, Section::DirectionalShadows, "64x64|128x128|256x256|512x512"),
        Value("shadows.ray-traced.animate-samples", Kind::Boolean, Section::DirectionalShadows, "on|off"),
        Value("shadows.ray-traced.max-distance", Kind::Enum, Section::DirectionalShadows, "max|32m|16m|8m|4m|2m"),
        Value("shadows.ray-traced.ray-bias", Kind::Float, Section::DirectionalShadows, "world units 0..0.1"),

        // Material Editor.
        Value("material.selected", Kind::DynamicSelection, Section::Materials, "runtime material id or unique name", false, true),
        Value("material.selected.domain", Kind::Enum, Section::Materials, "opaque|alpha-tested|alpha-blended|transmissive|transmissive-alpha-tested|transmissive-alpha-blended", false, true),
        Value("material.selected.double-sided", Kind::Boolean, Section::Materials, "on|off", false, true),
        Value("material.selected.base-texture-enabled", Kind::Boolean, Section::Materials, "on|off; requires a base or diffuse texture", false, true),
        Value("material.selected.base-color", Kind::Float3, Section::Materials, "linear rgb float3", false, true),
        Value("material.selected.metal-specular-texture-enabled", Kind::Boolean, Section::Materials, "on|off; requires a metal-rough or specular texture", false, true),
        Value("material.selected.specular-color", Kind::Float3, Section::Materials, "linear rgb float3; specular-gloss model", false, true),
        Value("material.selected.metalness", Kind::Float, Section::Materials, "float 0..1; metal-rough model", false, true),
        Value("material.selected.roughness", Kind::Float, Section::Materials, "float 0..1; glossiness alias is one minus roughness", false, true),
        Value("material.selected.opacity", Kind::Float, Section::Materials, "float 0..1 or 0..2 with a base texture", false, true),
        Value("material.selected.alpha-cutoff", Kind::Float, Section::Materials, "float 0..1; alpha-tested with base texture", false, true),
        Value("material.selected.normal-texture-enabled", Kind::Boolean, Section::Materials, "on|off; requires a normal texture", false, true),
        Value("material.selected.normal-scale", Kind::Float, Section::Materials, "float -2..2", true, true),
        Value("material.selected.occlusion-texture-enabled", Kind::Boolean, Section::Materials, "on|off; requires an occlusion texture", false, true),
        Value("material.selected.occlusion-strength", Kind::Float, Section::Materials, "float 0..1; enabled occlusion texture", false, true),
        Value("material.selected.emissive-texture-enabled", Kind::Boolean, Section::Materials, "on|off; requires an emissive texture", false, true),
        Value("material.selected.emissive-color", Kind::Float3, Section::Materials, "linear rgb float3", false, true),
        Value("material.selected.emissive-intensity", Kind::Float, Section::Materials, "float 0..1000", false, true),
        Value("material.selected.transmission-texture-enabled", Kind::Boolean, Section::Materials, "on|off; transmissive material with transmission texture", false, true),
        Value("material.selected.transmission-factor", Kind::Float, Section::Materials, "float 0..1; transmissive material", false, true),
        Value("material.selected.alpha-mask-texture-enabled", Kind::Boolean, Section::Materials, "on|off; requires an opacity texture", false, true),

        // Footer.
        Action("reset-settings", Section::Footer, "restore renderer and interface factory settings"),
        Action("capture", Section::Footer, "copy the current frame to the clipboard"),
        Action("restart", Section::Footer, "restart UVSR")
    };

    inline constexpr std::array<std::string_view, 5>
        UiSettingsNavigationExemptions = {
            "settings-drawer-headers",
            "settings-tree-nodes",
            "settings-scroll-state",
            "dropdown-popup-state",
            "command-input-history"
        };

    inline constexpr std::array<std::string_view, 4>
        UiSettingsTelemetryExemptions = {
            "renderer-summary",
            "performance-summary",
            "statistics-timing-tables",
            "fixed-availability-messages"
        };
}
