#pragma once

#include <array>
#include <cstddef>
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
        Visibility,
        Buffers,
        Statistics,
        Aliasing,
        Sky,
        Lights,
        ScreenSpaceDirectionalShadows,
        SparseVirtualShadowMaps,
        DiagnosticCascadedShadowMaps,
        Footer,
        Materials,
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

    enum class UiSettingsFactoryMutationPolicy : std::uint8_t
    {
        // Fail closed unless a descriptor is deliberately classified as a
        // presentation-only mutation supported by the factory shader bundle.
        Locked,
        UiSafe
    };

    struct UiSettingsCommandDefinition
    {
        std::string_view name;
        UiSettingsCommandKind kind = UiSettingsCommandKind::Enum;
        UiSettingsCommandSection section =
            UiSettingsCommandSection::General;
        std::uint8_t supportedVerbs = 0u;
        UiSettingsFactoryMutationPolicy factoryMutationPolicy =
            UiSettingsFactoryMutationPolicy::Locked;
        bool dynamic = false;
        std::string_view domain;

        [[nodiscard]] constexpr bool Supports(
            UiSettingsCommandVerb verb) const
        {
            return (supportedVerbs &
                static_cast<std::uint8_t>(verb)) != 0u;
        }
    };

    [[nodiscard]] constexpr std::uint8_t UiSettingsVerbMask(
        UiSettingsCommandVerb verb)
    {
        return static_cast<std::uint8_t>(verb);
    }

    [[nodiscard]] constexpr UiSettingsCommandDefinition
        MakeUiSettingsValueCommand(
            std::string_view name,
            UiSettingsCommandKind kind,
            UiSettingsCommandSection section,
            std::string_view domain,
            bool supportsReset = true,
            bool dynamic = false,
            UiSettingsFactoryMutationPolicy factoryMutationPolicy =
                UiSettingsFactoryMutationPolicy::Locked)
    {
        return {
            name,
            kind,
            section,
            static_cast<std::uint8_t>(
                UiSettingsVerbMask(UiSettingsCommandVerb::Get) |
                UiSettingsVerbMask(UiSettingsCommandVerb::Set) |
                (kind == UiSettingsCommandKind::Boolean
                    ? UiSettingsVerbMask(UiSettingsCommandVerb::Toggle)
                    : 0u) |
                (supportsReset
                    ? UiSettingsVerbMask(UiSettingsCommandVerb::Reset)
                    : 0u)),
            factoryMutationPolicy,
            dynamic,
            domain
        };
    }

    [[nodiscard]] constexpr UiSettingsCommandDefinition
        MakeUiSettingsActionCommand(
            std::string_view name,
            UiSettingsCommandSection section,
            std::string_view domain,
            UiSettingsFactoryMutationPolicy factoryMutationPolicy =
                UiSettingsFactoryMutationPolicy::Locked)
    {
        return {
            name,
            UiSettingsCommandKind::Action,
            section,
            UiSettingsVerbMask(UiSettingsCommandVerb::Run),
            factoryMutationPolicy,
            false,
            domain
        };
    }

    inline constexpr std::array<UiSettingsCommandDefinition, 245>
        UiSettingsCommandCatalog = {{
            // UI presentation: 5.
            MakeUiSettingsValueCommand(
                "ui.skin",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Ui,
                "amp|og",
                true,
                false,
                UiSettingsFactoryMutationPolicy::UiSafe),
            MakeUiSettingsValueCommand(
                "ui.visible",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Ui,
                "on|off",
                true,
                false,
                UiSettingsFactoryMutationPolicy::UiSafe),
            MakeUiSettingsValueCommand(
                "ui.settings-collapsed",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Ui,
                "on|off",
                true,
                false,
                UiSettingsFactoryMutationPolicy::UiSafe),
            MakeUiSettingsValueCommand(
                "ui.zoom",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Ui,
                "off|2x|3x|4x|5x",
                true,
                false,
                UiSettingsFactoryMutationPolicy::UiSafe),
            MakeUiSettingsValueCommand(
                "material-editor.visible",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Ui,
                "on|off",
                true,
                false,
                UiSettingsFactoryMutationPolicy::UiSafe),

            // General: 6.
            MakeUiSettingsValueCommand(
                "gpu.adapter",
                UiSettingsCommandKind::DynamicSelection,
                UiSettingsCommandSection::General,
                "runtime adapter index or unique display name",
                false,
                true),
            MakeUiSettingsValueCommand(
                "camera.mode",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::General,
                "freelook|locked",
                true,
                false,
                UiSettingsFactoryMutationPolicy::UiSafe),
            MakeUiSettingsValueCommand(
                "camera.location",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::General,
                "piloted|position-1",
                true,
                false,
                UiSettingsFactoryMutationPolicy::UiSafe),
            MakeUiSettingsValueCommand(
                "world-materials",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::General,
                "white-world-off|white-world-on|preserve-detail|"
                "preserve-lighting|indirect-diffuse"),
            MakeUiSettingsValueCommand(
                "scene.current",
                UiSettingsCommandKind::DynamicSelection,
                UiSettingsCommandSection::General,
                "runtime scene filename or unique display name",
                false,
                true),
            MakeUiSettingsActionCommand(
                "open-scene-folder",
                UiSettingsCommandSection::General,
                "open the active scene directory"),

            // Visibility: 20.
            MakeUiSettingsValueCommand(
                "visibility.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Visibility,
                "on|off"),
            MakeUiSettingsValueCommand(
                "visibility.resolution",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Visibility,
                "full|half|quarter"),
            MakeUiSettingsValueCommand(
                "visibility.profile",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Visibility,
                "low|medium|high|ultra"),
            MakeUiSettingsValueCommand(
                "visibility.sampling.estimator",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Visibility,
                "projected-angle|solid-angle|cosine-weighted"),
            MakeUiSettingsValueCommand(
                "visibility.sampling.noise-pattern",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Visibility,
                "independent-hash|toroidal-blue"),
            MakeUiSettingsValueCommand(
                "visibility.sampling.samples",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::Visibility,
                "integer 1..64"),
            MakeUiSettingsValueCommand(
                "visibility.sampling.radius",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Visibility,
                "float 0.01..max(scene-diagonal*0.1,1)"),
            MakeUiSettingsValueCommand(
                "visibility.sampling.thickness",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Visibility,
                "float 0..max(scene-diagonal*0.02,0.5)"),
            MakeUiSettingsValueCommand(
                "visibility.sampling.distribution",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Visibility,
                "float 0.5..4"),
            MakeUiSettingsValueCommand(
                "visibility.ao.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Visibility,
                "on|off"),
            MakeUiSettingsValueCommand(
                "visibility.ao.strength",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Visibility,
                "float 0..2"),
            MakeUiSettingsValueCommand(
                "visibility.ao.power",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Visibility,
                "float 0.1..4"),
            MakeUiSettingsValueCommand(
                "visibility.gi.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Visibility,
                "on|off"),
            MakeUiSettingsValueCommand(
                "visibility.gi.limit-bounces",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Visibility,
                "on|off"),
            MakeUiSettingsValueCommand(
                "visibility.gi.bounces",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::Visibility,
                "integer 1..8"),
            MakeUiSettingsValueCommand(
                "visibility.gi.contribution-cutoff",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Visibility,
                "float 0..0.02"),
            MakeUiSettingsValueCommand(
                "visibility.gi.intensity",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Visibility,
                "float 0..10"),
            MakeUiSettingsValueCommand(
                "visibility.reconstruction.method",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Visibility,
                "full-resolution|guide-aware-upsampling|joint-bilateral|"
                "gaussian-bilateral|depth-guided|depth-normal|slope-aware|"
                "leakage-limited"),
            MakeUiSettingsValueCommand(
                "visibility.reconstruction.filter-radius",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Visibility,
                "float 1..12; gaussian-bilateral only"),
            MakeUiSettingsValueCommand(
                "visibility.application",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Visibility,
                "separate|fused|fused-edge"),

            // Visibility buffers: 7.
            MakeUiSettingsValueCommand(
                "visibility.buffers.preset",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Buffers,
                "performance-precision|default-precision|compact-ao|compact-gi"),
            MakeUiSettingsValueCommand(
                "visibility.buffers.trace-ao",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Buffers,
                "half|full"),
            MakeUiSettingsValueCommand(
                "visibility.buffers.current-gi",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Buffers,
                "half-rgba|full-rgba"),
            MakeUiSettingsValueCommand(
                "visibility.buffers.accumulated-gi",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Buffers,
                "half-rgba|full-rgba"),
            MakeUiSettingsValueCommand(
                "visibility.buffers.output-ao",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Buffers,
                "half|full"),
            MakeUiSettingsValueCommand(
                "visibility.buffers.output-gi",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Buffers,
                "half-rgba|full-rgba"),
            MakeUiSettingsValueCommand(
                "visibility.buffers.long-range-depth",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Buffers,
                "half|full"),

            // Statistics and controlled experiments: 9.
            MakeUiSettingsValueCommand(
                "statistics.effect",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Statistics,
                "complete-renderer|geometry|direct-lighting|screen-space-visibility|anti-aliasing|screen-space-directional-shadows|sparse-virtual-shadow-maps|diagnostic-cascaded-shadow-maps|material-picking|environment-background|tone-mapping|output-blit",
                true,
                false,
                UiSettingsFactoryMutationPolicy::UiSafe),
            MakeUiSettingsValueCommand(
                "benchmark.visibility.warmup-frames",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::Statistics,
                "integer 0..600"),
            MakeUiSettingsValueCommand(
                "benchmark.visibility.measured-frames",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::Statistics,
                "integer 1..2000"),
            MakeUiSettingsActionCommand(
                "visibility-benchmark",
                UiSettingsCommandSection::Statistics,
                "run the current visibility configuration"),
            MakeUiSettingsActionCommand(
                "aa-motion-test",
                UiSettingsCommandSection::Statistics,
                "run the current anti-aliasing motion test"),
            MakeUiSettingsActionCommand(
                "cancel-benchmark",
                UiSettingsCommandSection::Statistics,
                "cancel the active visibility or anti-aliasing test"),
            MakeUiSettingsActionCommand(
                "svsm-camera-motion-test",
                UiSettingsCommandSection::Statistics,
                "run the fixed SVSM camera-motion test"),
            MakeUiSettingsActionCommand(
                "svsm-sun-motion-test",
                UiSettingsCommandSection::Statistics,
                "run the fixed SVSM sun-motion test"),
            MakeUiSettingsActionCommand(
                "cancel-svsm-motion-test",
                UiSettingsCommandSection::Statistics,
                "cancel the active SVSM motion test"),

            // Anti-aliasing: 21 production controls.
            MakeUiSettingsValueCommand(
                "anti-aliasing.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Aliasing,
                "on|off"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.method",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "conservative-morphological|temporal-reconstructive|multisample-reference"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.quality",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "low|medium|high|ultra"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.history.frames",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::Aliasing,
                "integer 1..32 or preset"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.history.strength",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Aliasing,
                "percent 0..200 or preset"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.dejitter",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "preset|on|off"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.sharpen.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Aliasing,
                "on|off"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.sharpen.strength",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Aliasing,
                "float 0..1"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.subpixel-morphology",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "preset|off|conservative-low|conservative-medium|conservative-high|conservative-ultra"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.motion-source",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "preset|center|closest-cross|center-first-edge-dilation"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.reconstruction",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "preset|bilinear|one-sample-bicubic|five-tap-catmull-rom|nine-tap-catmull-rom"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.rectification",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "preset|pair-rgb|variance-ycocg"),

            MakeUiSettingsValueCommand(
                "anti-aliasing.temporal-cost",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "minimum|reduced|full-quality"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.history.storage",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "temporal-cost|robust|compact"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.previous-depth-validation",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "temporal-cost|four-texel-footprint|stationary-bypass"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.history.weight",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "temporal-cost|confidence-recurrence|immediate-horizon"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.motion-trust",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "temporal-cost|linear-speed|squared-speed"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.rectification-clip",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "temporal-cost|velocity-dilated-line|tight-component"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.blend-domain",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "temporal-cost|luminance-compressed|linear-rgb"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.sharpen.policy",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "temporal-cost|off|on"),
            MakeUiSettingsValueCommand(
                "anti-aliasing.sample-resurrection",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Aliasing,
                "preset|off|one-older-frame|two-older-frames"),

            // Sky: 9.
            MakeUiSettingsValueCommand(
                "sky.environment",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Sky,
                "day-kloppenheim-03|bright-overcast-snow-field-2|soft-day-farm-field|night-kloppenheim-07|starry-night-qwantani|legacy-quadrangle-cloudy"),
            MakeUiSettingsValueCommand(
                "sky.exposure",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Sky,
                "float -8..8 ev"),
            MakeUiSettingsValueCommand(
                "sky.diffuse-ibl",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Sky,
                "on|off"),
            MakeUiSettingsValueCommand(
                "sky.diffuse-ibl-strength",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Sky,
                "float 0..4"),
            MakeUiSettingsValueCommand(
                "sky.specular-ibl",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Sky,
                "on|off"),
            MakeUiSettingsValueCommand(
                "sky.specular-ibl-strength",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Sky,
                "float 0..4"),
            MakeUiSettingsValueCommand(
                "sky.environment-background",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Sky,
                "on|off"),
            MakeUiSettingsValueCommand(
                "sky.ambient-fill.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Sky,
                "on|off"),
            MakeUiSettingsValueCommand(
                "sky.debug-view",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Sky,
                "off|shading-normal|geometric-normal|normal-difference|diffuse-environment|cardinal-environment-test|prefiltered-specular|environment-brdf|final-specular-ibl|combined-ibl|specular-occlusion|environment-mip"),

            // Dynamic lights: 23.
            MakeUiSettingsValueCommand(
                "light.selected",
                UiSettingsCommandKind::DynamicSelection,
                UiSettingsCommandSection::Lights,
                "runtime editable-light index or unique name",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.azimuth",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "degrees -180..180; directional or spot",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.elevation",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "degrees -90..90; directional or spot",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.color",
                UiSettingsCommandKind::Float3,
                UiSettingsCommandSection::Lights,
                "linear rgb float3",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.irradiance",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "float 0..100; directional",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.angular-size",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "float 0.1..20; directional",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.radius",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "float 0.01..1; point or spot",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.intensity",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "float 0..100; point or spot",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.inner-angle",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "degrees 0..180; spot",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.outer-angle",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "degrees 0..180; spot",
                true,
                true),

            MakeUiSettingsValueCommand(
                "light.selected.flashlight.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Lights,
                "on|off; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.cast-shadows",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Lights,
                "on|off; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.realistic",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Lights,
                "on|off; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.hotspot-size",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "float 0.2..0.75; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.hotspot-strength",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "float 0..0.9; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.sway",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "degrees 0..2; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.aim-correction",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "seconds 0.01..0.5; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.brightness",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "candela 25..4000; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.beam-size",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "degrees 8..100; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.beam-roundness",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "float 0..1; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.edge-softness",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "float 0..1; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.range",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "meters 2..100; flashlight_1",
                true,
                true),
            MakeUiSettingsValueCommand(
                "light.selected.flashlight.camera-offset",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Lights,
                "meters 0..0.4; flashlight_1",
                true,
                true),

            // Screen-space directional shadows: 13.
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.profile",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "default|long|maximum-validation|custom"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.length",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "60|120|240|480|960 pixels"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.surface-thickness",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "float 0..0.05"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.bilinear-threshold",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "float 0..0.1"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.contrast",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "float 1..16"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.hard-samples",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "0|4|8"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.fade-samples",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "0|8|16"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.ignore-edge-pixels",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.precision-offset",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.bilinear-offset-mode",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.early-out",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.screen-space-directional.debug-view",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::ScreenSpaceDirectionalShadows,
                "off|edge|thread|wave"),

            // Sparse virtual shadow maps: 62.
            MakeUiSettingsValueCommand(
                "shadows.svsm.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.profile",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "performance|balanced|quality|custom"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.mode",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "dense-reference|sparse-uncached|sparse-cached"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.filter-kernel",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "nearest-poisson-reference|bilinear-pcf"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.filter-taps",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "1|4|8|16"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.resolution-bias",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "0|+1-mip|+2-mips"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.page-marking",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "per-pixel|8x8-tile|16x16-tile"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.moving-light-resolution-bias",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "off|+1-mip|+2-mips"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.object-invalidation-mode",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "auto|always|rigid|static"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.poisson-ordering",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "legacy-stride|balanced-progressive"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.filtering",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "manual-page-safe|hybrid"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.debug-view",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "off|clipmap-level|required-pages|resident-pages|"
                "cached-pages|dirty-pages|rendered-pages|physical-pages|"
                "fallback-level|missing-pages|tap-count|visibility"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.first-clipmap-extent",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "float 1..500"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.maximum-light-depth",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "float 1..2000"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.physical-pool-pages",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "integer 64..4096"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.unlimited-page-render-budget",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.page-render-budget",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "integer 0..physical-pool-pages"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.moving-light-recovery-frames",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "integer 0..60"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.distance-clamp-start-scale",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "float 0.25..8"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.maximum-distance-clamp-level",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "integer 0..4"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.static-hzb-conservative-bias",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "float 0..0.01"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.scatter-maximum-page-amplification",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "integer 1..64"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.light-depth-guard-fraction",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "float 0.1..1"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.adaptive-filtering",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.receiver-distance-mip-clamp",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.paired-static-dynamic-depth",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.include-coarsest-in-page-budget",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.recent-page-eviction-grace",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.cached-shadow-draw-lists",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.adaptive-static-caster-cache",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.packet-state-sorting",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.packet-page-culling",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.hierarchical-scheduled-page-mask",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.receiver-subpage-mask-culling",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.static-depth-page-hzb-culling",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.dirty-page-scatter-raster",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.allocation-budget-saturation-early-out",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.deduplicate-per-pixel-requests",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.precomposed-clipmap-transforms",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.page-translation-cache",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.light-depth-origin-guard-band",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.finite-budget-static-drain",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.static-page-request-reuse",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.static-visibility-cache",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.scene-state-caching",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.caster-only-scene-revision",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.shared-six-clipmap-packet-builder",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.persistent-caster-source-cache",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.opaque-raster-specialization",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.lean-alpha-tested-bindings",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.deferred-static-depth-merge",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.moving-light-uncached-policy",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.preserve-page-mappings-on-content-invalidation",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.continuous-moving-light-distance-bias",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.localized-caster-invalidation",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.tight-localized-bounds",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.gpu-gated-draw-submission",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.batched-draw-submission",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.per-level-empty-work-skip",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.packet-rectangle-direct-scan",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.scatter-alpha-test-early-reject",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.svsm.detailed-gpu-stage-timing",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::SparseVirtualShadowMaps,
                "on|off"),

            // Diagnostic cascaded shadow maps: 46.
            MakeUiSettingsValueCommand(
                "shadows.csm.enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.profile",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "single-map-reference|low-cost-csm|ue5-csm-reference|cached-single-shadow|optimized-cached-single-shadow|optimized-cached-csm|custom"),
            MakeUiSettingsValueCommand(
                "shadows.csm.cascade-count",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "integer 1..4"),
            MakeUiSettingsValueCommand(
                "shadows.csm.resolution-per-cascade",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "integer 128..8192"),
            MakeUiSettingsValueCommand(
                "shadows.csm.maximum-shadow-distance",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 1..5000"),
            MakeUiSettingsValueCommand(
                "shadows.csm.maximum-light-depth",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 1..10000"),
            MakeUiSettingsValueCommand(
                "shadows.csm.filter",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "ue5-manual-5x5-pcf|svsm-matched-point-poisson"),
            MakeUiSettingsValueCommand(
                "shadows.csm.filter-taps",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "1|4|8|16"),
            MakeUiSettingsValueCommand(
                "shadows.csm.filter-radius",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0..8 texels; Poisson only"),
            MakeUiSettingsValueCommand(
                "shadows.csm.ue-minimum-light-depth",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.split-distribution-exponent",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 1..8"),
            MakeUiSettingsValueCommand(
                "shadows.csm.cascade-transition-fraction",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0..0.5"),
            MakeUiSettingsValueCommand(
                "shadows.csm.shadow-distance-fade-fraction",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0..0.5"),
            MakeUiSettingsValueCommand(
                "shadows.csm.projection-snap-multiple",
                UiSettingsCommandKind::Integer,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "integer 1..16"),
            MakeUiSettingsValueCommand(
                "shadows.csm.depth-bias",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0..100"),
            MakeUiSettingsValueCommand(
                "shadows.csm.slope-scaled-depth-bias",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0..10"),
            MakeUiSettingsValueCommand(
                "shadows.csm.directional-light-shadow-bias",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0..1"),
            MakeUiSettingsValueCommand(
                "shadows.csm.directional-light-slope-bias",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0..1"),
            MakeUiSettingsValueCommand(
                "shadows.csm.receiver-depth-bias",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0..1"),
            MakeUiSettingsValueCommand(
                "shadows.csm.cached-shadow-draw-lists",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.whole-map-reuse",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.whole-cascade-reuse",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.dirty-rectangles",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.scrolling",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.minimum-scroll-overlap",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0.5..1"),
            MakeUiSettingsValueCommand(
                "shadows.csm.input-assembler-caster-fetch",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.receiver-raster-scissor",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.accurate-caster-hull-culling",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.ue-caster-radius-threshold",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.caster-radius-threshold",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "float 0..0.05"),
            MakeUiSettingsValueCommand(
                "shadows.csm.use-16-bit-depth",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.opaque-depth-state-merging",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.position-only-opaque-casters",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.translation-only-caster-transforms",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.precomputed-depth-axis-normalization",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.conservative-saturated-slope-shortcut",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.algebraic-slow-slope-reduction",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.pre-normalized-receiver-light-direction",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.precomposed-clip-to-shadow-transform",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.one-pass-cascade-classification",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.precomputed-receiver-hull-axes",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.shared-caster-light-projection",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.direct-caster-submission",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.batched-full-redraw-clear",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.detailed-gpu-stage-timing",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "on|off"),
            MakeUiSettingsValueCommand(
                "shadows.csm.debug-view",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::DiagnosticCascadedShadowMaps,
                "none|visibility|cascade-selection|cache-action"),

            // Settings footer: 3 actions; ui.zoom is cataloged above.
            MakeUiSettingsActionCommand(
                "reset-settings",
                UiSettingsCommandSection::Footer,
                "restore renderer factory settings"),
            MakeUiSettingsActionCommand(
                "screenshot",
                UiSettingsCommandSection::Footer,
                "copy the current frame to the clipboard"),
            MakeUiSettingsActionCommand(
                "restart",
                UiSettingsCommandSection::Footer,
                "restart UVSR"),

            // Dynamic Material Editor: 21.
            MakeUiSettingsValueCommand(
                "material.selected",
                UiSettingsCommandKind::DynamicSelection,
                UiSettingsCommandSection::Materials,
                "runtime material id or unique name",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.domain",
                UiSettingsCommandKind::Enum,
                UiSettingsCommandSection::Materials,
                "opaque|alpha-tested|alpha-blended|transmissive|transmissive-alpha-tested|transmissive-alpha-blended",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.double-sided",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Materials,
                "on|off",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.base-texture-enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Materials,
                "on|off; requires a base or diffuse texture",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.base-color",
                UiSettingsCommandKind::Float3,
                UiSettingsCommandSection::Materials,
                "linear rgb float3",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.metal-specular-texture-enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Materials,
                "on|off; requires a metal-rough or specular texture",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.specular-color",
                UiSettingsCommandKind::Float3,
                UiSettingsCommandSection::Materials,
                "linear rgb float3; specular-gloss model",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.metalness",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Materials,
                "float 0..1; metal-rough model",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.roughness",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Materials,
                "float 0..1; glossiness alias is one minus roughness",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.opacity",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Materials,
                "float 0..1 or 0..2 with a base texture",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.alpha-cutoff",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Materials,
                "float 0..1; alpha-tested with base texture",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.normal-texture-enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Materials,
                "on|off; requires a normal texture",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.normal-scale",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Materials,
                "float -2..2",
                true,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.occlusion-texture-enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Materials,
                "on|off; requires an occlusion texture",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.occlusion-strength",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Materials,
                "float 0..1; enabled occlusion texture",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.emissive-texture-enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Materials,
                "on|off; requires an emissive texture",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.emissive-color",
                UiSettingsCommandKind::Float3,
                UiSettingsCommandSection::Materials,
                "linear rgb float3",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.emissive-intensity",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Materials,
                "float 0..1000",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.transmission-texture-enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Materials,
                "on|off; transmissive material with transmission texture",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.transmission-factor",
                UiSettingsCommandKind::Float,
                UiSettingsCommandSection::Materials,
                "float 0..1; transmissive material",
                false,
                true),
            MakeUiSettingsValueCommand(
                "material.selected.alpha-mask-texture-enabled",
                UiSettingsCommandKind::Boolean,
                UiSettingsCommandSection::Materials,
                "on|off; requires an opacity texture",
                false,
                true)
        }};

    inline constexpr std::array<std::string_view, 5>
        UiSettingsNavigationExemptions = {
            "settings-drawer-headers",
            "settings-tree-nodes",
            "settings-scroll-state",
            "dropdown-popup-state",
            "command-input-history"
        };

    inline constexpr std::array<std::string_view, 8>
        UiSettingsTelemetryExemptions = {
            "renderer-summary",
            "performance-summary",
            "benchmark-activity-overlay",
            "statistics-timing-tables",
            "benchmark-status",
            "benchmark-progress",
            "visibility-buffer-edge-metadata",
            "fixed-availability-messages"
        };
}
