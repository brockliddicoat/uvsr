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

    static_assert(UiSettingsCommandCatalog.size() == 131u);
    static_assert(static_cast<std::size_t>(UiSettingsCommandSection::Count) == 10u);
    static_assert(Action("test", Section::General, "test").supportedVerbs ==
        static_cast<std::uint8_t>(UiSettingsCommandVerb::Run));

    constexpr std::array<std::size_t, 10> ExpectedSectionCounts = {
        5u,  // UI
        5u,  // General
        19u, // Visibility
        31u, // Anti-Aliasing
        4u,  // Debug
        8u,  // Sky
        23u, // Lights
        12u, // Screen-Space Directional Shadows
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
        "visibility.ao.enabled",
        "visibility.ao.precision",
        "visibility.ao.strength",
        "visibility.distribution",
        "visibility.enabled",
        "visibility.estimator",
        "visibility.gi.enabled",
        "visibility.gi.intensity",
        "visibility.gi.precision",
        "visibility.noise",
        "visibility.quality",
        "visibility.radius",
        "visibility.reconstruction",
        "visibility.resolution",
        "visibility.samples",
        "visibility.spatial.enabled",
        "visibility.spatial.filter",
        "visibility.spatial.radius",
        "visibility.thickness"
    };

    std::set<std::string> names;
    std::set<std::string> dynamicSelections;
    std::set<std::string> visibility;
    std::array<std::size_t, 10> sectionCounts{};
    std::size_t actionCount = 0u;
    std::size_t dynamicCount = 0u;

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

        Require(definition.name.find("shadows.svsm") == std::string_view::npos &&
                definition.name.find("shadows.csm") == std::string_view::npos &&
                definition.name.find("benchmark") == std::string_view::npos &&
                definition.name.find("world-material") == std::string_view::npos &&
                definition.name.find("sample-resurrection") == std::string_view::npos &&
                definition.name != "engine.pbr",
            "retired engine taxonomies must not remain discoverable");
    }

    Require(names.size() == UiSettingsCommandCatalog.size(),
        "the compact catalog must contain 131 unique controls");
    Require(sectionCounts == ExpectedSectionCounts,
        "section counts must match the current UI");
    Require(actionCount == 4u,
        "only open-folder, reset, screenshot, and restart actions remain");
    Require(dynamicCount == 46u,
        "runtime lights and materials must retain their 46 dynamic controls");
    Require(dynamicSelections == ExpectedDynamicSelections,
        "dynamic selections must cover adapters, scenes, lights, and materials");
    Require(visibility == ExpectedVisibility,
        "Visibility commands must exactly mirror the direct UI settings");

    const auto requireDomain = [](std::string_view name, std::string_view domain) {
        const UiSettingsCommandDefinition* definition = Find(name);
        Require(definition && definition->domain == domain,
            "command domain must exactly match its visible values");
    };
    requireDomain("visibility.noise",
        "permutated-white-noise|hashed-white-noise|void-cluster-blue-noise");
    requireDomain("visibility.reconstruction",
        "direct-or-guide-aware|packed-depth-normal|packed-slope-aware|packed-leak-controlled");
    requireDomain("visibility.spatial.filter",
        "joint-bilateral|gaussian-bilateral");
    requireDomain("anti-aliasing.taa.temporal-cost",
        "full-quality|reduced|minimum");
    requireDomain("anti-aliasing.taa.jitter-sequence",
        "rotated-grid-4|uniform-helix-4|halton-8|halton-16|halton-32|sobol-32");
    requireDomain("anti-aliasing.taa.previous-depth",
        "stationary-bypass|four-texel-footprint");
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
    requireDomain("anti-aliasing.cmaa2.edge-threshold",
        "float 0.05..0.15");
    requireDomain("anti-aliasing.cmaa2.detector", "luma|full-color");
    requireDomain("anti-aliasing.msaa.quality", "low|medium|high|ultra");
    requireDomain("debug.world.materials",
        "scene|white|white-detail|white-lighting");
    requireDomain("debug.shadows.isolation",
        "final|thread-lanes|wave-groups");
    requireDomain("debug.visibility.view",
        "final|ambient-visibility|traced-indirect|applied-indirect");
    requireDomain("shadows.screen-space-directional.profile",
        "default|long|maximum-validation|custom");

    Require(!Find("visibility.profile") &&
            !Find("visibility.sampling.noise-pattern") &&
            !Find("visibility.reconstruction.method") &&
            !Find("visibility.buffers.preset") &&
            !Find("anti-aliasing.method") &&
            !Find("anti-aliasing.taa.stationary-bypass") &&
            !Find("debug.visibility.indirect-diffuse-only") &&
            !Find("debug.shadows.edge-overlay") &&
            !Find("debug.shadows.overlay-opacity") &&
            !Find("shadows.screen-space-directional.debug-view") &&
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
