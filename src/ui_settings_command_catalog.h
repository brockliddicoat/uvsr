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
        Action
    };

    enum class UiSettingsCommandSection : std::uint8_t
    {
        Ui,
        General,
        Representation,
        Visibility,
        Aliasing,
        Debug,
        Sky,
        Lights,
        DirectionalShadows,
        ScreenSpaceDirectionalShadows,
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
        Value("ui.skin", Kind::Enum, Section::Ui, "amp|og"),
        Value("ui.visible", Kind::Boolean, Section::Ui, "on|off"),
        Value("ui.settings-collapsed", Kind::Boolean, Section::Ui, "on|off"),
        Value("ui.zoom", Kind::Enum, Section::Ui, "off|2x|3x|4x|5x"),
        Value("material-editor.visible", Kind::Boolean, Section::Ui, "on|off"),

        // General.
        Value("gpu.adapter", Kind::DynamicSelection, Section::General, "runtime adapter index or unique display name", false, true),
        Value("gpu.adaptive-sync", Kind::Enum, Section::General, "off|vendor-agnostic|nvidia-exclusive"),
        Value("camera.mode", Kind::Enum, Section::General, "freelook|locked"),
        Value("camera.location", Kind::Enum, Section::General, "piloted|position-1"),
        Value("scene.current", Kind::DynamicSelection, Section::General, "runtime scene filename or unique display name", false, true),
        Action("open-scene-folder", Section::General, "open the active scene directory"),

        // Representation.
        Value("representation.bvh.build-preference", Kind::Enum, Section::Representation, "fast-trace|balanced|fast-build"),
        Value("representation.blas.update-mode", Kind::Enum, Section::Representation, "rebuild|refit"),
        Value("representation.tlas.update-mode", Kind::Enum, Section::Representation, "rebuild|refit"),

        // Visibility.
        Value("visibility.enabled", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.quality", Kind::Enum, Section::Visibility, "low|medium|high|ultra|custom"),
        Value("visibility.estimator", Kind::Enum, Section::Visibility, "projected-angle|solid-angle|cosine-weighted"),
        Value("visibility.resolution", Kind::Enum, Section::Visibility, "full|half|quarter"),
        Value("visibility.samples", Kind::Integer, Section::Visibility, "integer 1..64"),
        Value("visibility.radius", Kind::Float, Section::Visibility, "float 0.1..10"),
        Value("visibility.thickness", Kind::Float, Section::Visibility, "float 0.01..2"),
        Value("visibility.distribution", Kind::Float, Section::Visibility, "float 0.25..4"),
        Value("visibility.noise", Kind::Enum, Section::Visibility, "permutated-white-noise|void-cluster-blue-noise"),
        Value("visibility.ao.enabled", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.ao.strength", Kind::Float, Section::Visibility, "float 0..4"),
        Value("visibility.ao.precision", Kind::Enum, Section::Visibility, "16-bit|32-bit"),
        Value("visibility.gi.enabled", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.gi.intensity", Kind::Float, Section::Visibility, "float 0..16"),
        Value("visibility.gi.precision", Kind::Enum, Section::Visibility, "16-bit|32-bit"),
        Value("visibility.reconstruction", Kind::Enum, Section::Visibility, "direct-or-guide-aware|packed-depth-normal|packed-slope-aware|packed-leak-controlled"),
        Value("visibility.spatial.enabled", Kind::Boolean, Section::Visibility, "on|off"),
        Value("visibility.spatial.filter", Kind::Enum, Section::Visibility, "joint-bilateral|gaussian-bilateral"),
        Value("visibility.spatial.radius", Kind::Float, Section::Visibility, "float 1..8"),

        // Anti-Aliasing.
        Value("anti-aliasing.taa.enabled", Kind::Boolean, Section::Aliasing, "on|off"),
        Value("anti-aliasing.taa.quality", Kind::Enum, Section::Aliasing, "low|medium|high|ultra"),
        Value("anti-aliasing.taa.jitter-sequence", Kind::Enum, Section::Aliasing, "rotated-grid-4|uniform-helix-4|halton-8|halton-16|halton-32|sobol-32"),
        Value("anti-aliasing.taa.previous-depth", Kind::Enum, Section::Aliasing, "stationary-bypass|four-texel-footprint"),
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
        Value("anti-aliasing.cmaa2.enabled", Kind::Boolean, Section::Aliasing, "on|off"),
        Value("anti-aliasing.cmaa2.quality", Kind::Enum, Section::Aliasing, "low|medium|high|ultra"),
        Value("anti-aliasing.cmaa2.edge-threshold", Kind::Float, Section::Aliasing, "float 0.05..0.15"),
        Value("anti-aliasing.cmaa2.detector", Kind::Enum, Section::Aliasing, "luma|full-color"),
        Value("anti-aliasing.msaa.enabled", Kind::Boolean, Section::Aliasing, "on|off"),
        Value("anti-aliasing.msaa.quality", Kind::Enum, Section::Aliasing, "low|medium|high|ultra"),
        Value("anti-aliasing.msaa.samples", Kind::Enum, Section::Aliasing, "2x|4x|8x|16x"),

        // Debug.
        Value("debug.world.materials", Kind::Enum, Section::Debug, "scene|white|white-detail|white-lighting"),
        Value("debug.visibility.view", Kind::Enum, Section::Debug, "final|ambient-visibility|traced-indirect|applied-indirect"),
        Value("debug.pbr.filter", Kind::Enum, Section::Debug, "final|surface-normals|geometry-normals|normal-difference|diffuse-environment|environment-direction|reflected-environment|brdf-response|specular-environment|all-environment-light|specular-visibility|environment-level"),
        Value("debug.shadows.isolation", Kind::Enum, Section::Debug, "final|thread-lanes|wave-groups"),

        // Sky.
        Value("sky.environment", Kind::Enum, Section::Sky, "day-kloppenheim-03|bright-overcast-snow-field-2|soft-day-farm-field|night-kloppenheim-07|starry-night-qwantani|legacy-quadrangle-cloudy"),
        Value("sky.exposure", Kind::Float, Section::Sky, "float -8..8 ev"),
        Value("sky.diffuse-ibl", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.diffuse-ibl-strength", Kind::Float, Section::Sky, "float 0..4"),
        Value("sky.specular-ibl", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.specular-ibl-strength", Kind::Float, Section::Sky, "float 0..4"),
        Value("sky.environment-background", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.ambient-fill.enabled", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.enabled", Kind::Boolean, Section::Sky, "on|off"),
        Value("sky.visibility.samples-per-pixel", Kind::Enum, Section::Sky, "1|2|4|8|16|32|64"),
        Value("sky.visibility.noise-pattern", Kind::Enum, Section::Sky, "permutated-white-noise|void-cluster-blue-noise"),
        Value("sky.visibility.animate-samples", Kind::Boolean, Section::Sky, "on|off"),
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
        Value("light.selected.flashlight.realistic", Kind::Boolean, Section::Lights, "on|off; flashlight_1", true, true),
        Value("light.selected.flashlight.hotspot-size", Kind::Float, Section::Lights, "float 0.2..0.75; flashlight_1", true, true),
        Value("light.selected.flashlight.hotspot-strength", Kind::Float, Section::Lights, "float 0..0.9; flashlight_1", true, true),
        Value("light.selected.flashlight.sway", Kind::Float, Section::Lights, "degrees 0..2; flashlight_1", true, true),
        Value("light.selected.flashlight.aim-correction", Kind::Float, Section::Lights, "seconds 0.01..0.5; flashlight_1", true, true),
        Value("light.selected.flashlight.brightness", Kind::Float, Section::Lights, "candela 25..4000; flashlight_1", true, true),
        Value("light.selected.flashlight.beam-size", Kind::Float, Section::Lights, "degrees 8..100; flashlight_1", true, true),
        Value("light.selected.flashlight.beam-roundness", Kind::Float, Section::Lights, "float 0..1; flashlight_1", true, true),
        Value("light.selected.flashlight.edge-softness", Kind::Float, Section::Lights, "float 0..1; flashlight_1", true, true),
        Value("light.selected.flashlight.range", Kind::Float, Section::Lights, "meters 2..100; flashlight_1", true, true),
        Value("light.selected.flashlight.camera-offset", Kind::Float, Section::Lights, "meters 0..0.4; flashlight_1", true, true),

        // Directional Shadows.
        Value("shadows.ratio-estimator.enabled", Kind::Boolean, Section::DirectionalShadows, "on|off"),
        Value("shadows.ratio-estimator.hard-shadows", Kind::Boolean, Section::DirectionalShadows, "on|off"),
        Value("shadows.ratio-estimator.samples-per-pixel", Kind::Enum, Section::DirectionalShadows, "1|2|4|8|16|32|64"),
        Value("shadows.ratio-estimator.noise-pattern", Kind::Enum, Section::DirectionalShadows, "permutated-white-noise|void-cluster-blue-noise"),
        Value("shadows.ratio-estimator.animate-samples", Kind::Boolean, Section::DirectionalShadows, "on|off"),
        Value("shadows.ratio-estimator.ray-bias", Kind::Float, Section::DirectionalShadows, "world units 0..0.1"),

        // Screen-Space Directional Shadows.
        Value("shadows.screen-space-directional.enabled", Kind::Boolean, Section::ScreenSpaceDirectionalShadows, "on|off"),
        Value("shadows.screen-space-directional.profile", Kind::Enum, Section::ScreenSpaceDirectionalShadows, "default|long|maximum-validation|custom"),
        Value("shadows.screen-space-directional.length", Kind::Enum, Section::ScreenSpaceDirectionalShadows, "60|120|240|480|960 pixels"),
        Value("shadows.screen-space-directional.surface-thickness", Kind::Float, Section::ScreenSpaceDirectionalShadows, "float 0..0.05"),
        Value("shadows.screen-space-directional.bilinear-threshold", Kind::Float, Section::ScreenSpaceDirectionalShadows, "float 0..0.1"),
        Value("shadows.screen-space-directional.contrast", Kind::Float, Section::ScreenSpaceDirectionalShadows, "float 1..16"),
        Value("shadows.screen-space-directional.hard-samples", Kind::Enum, Section::ScreenSpaceDirectionalShadows, "0|4|8"),
        Value("shadows.screen-space-directional.fade-samples", Kind::Enum, Section::ScreenSpaceDirectionalShadows, "0|8|16"),
        Value("shadows.screen-space-directional.ignore-edge-pixels", Kind::Boolean, Section::ScreenSpaceDirectionalShadows, "on|off"),
        Value("shadows.screen-space-directional.precision-offset", Kind::Boolean, Section::ScreenSpaceDirectionalShadows, "on|off"),
        Value("shadows.screen-space-directional.bilinear-offset-mode", Kind::Boolean, Section::ScreenSpaceDirectionalShadows, "on|off"),
        Value("shadows.screen-space-directional.early-out", Kind::Boolean, Section::ScreenSpaceDirectionalShadows, "on|off"),

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
        Action("reset-settings", Section::Footer, "restore renderer factory settings"),
        Action("screenshot", Section::Footer, "copy the current frame to the clipboard"),
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
