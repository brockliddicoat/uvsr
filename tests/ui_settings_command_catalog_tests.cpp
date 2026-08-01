#include "ui_settings_command_catalog.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr
                << "UI settings command catalog validation failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool IsStableLowercaseName(std::string_view name)
    {
        if (name.empty() ||
            name.front() == '.' ||
            name.front() == '-' ||
            name.back() == '.' ||
            name.back() == '-')
        {
            return false;
        }

        for (const unsigned char character : name)
        {
            const bool lowercase =
                character >= static_cast<unsigned char>('a') &&
                character <= static_cast<unsigned char>('z');
            const bool digit =
                character >= static_cast<unsigned char>('0') &&
                character <= static_cast<unsigned char>('9');
            if (!lowercase &&
                !digit &&
                character != static_cast<unsigned char>('.') &&
                character != static_cast<unsigned char>('-'))
            {
                return false;
            }
        }
        return true;
    }

    const uvsr::UiSettingsCommandDefinition* Find(
        std::string_view name)
    {
        for (const auto& definition :
            uvsr::UiSettingsCommandCatalog)
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

    static_assert(UiSettingsCommandCatalog.size() == 245u);
    static_assert(
        static_cast<std::size_t>(
            UiSettingsCommandSection::Count) == 13u);
    static_assert(
        UiSettingsCommandDefinition{}.factoryMutationPolicy ==
        UiSettingsFactoryMutationPolicy::Locked);

    constexpr UiSettingsCommandDefinition ImplicitFactoryValue =
        MakeUiSettingsValueCommand(
            "test.value",
            UiSettingsCommandKind::Enum,
            UiSettingsCommandSection::General,
            "test");
    constexpr UiSettingsCommandDefinition ImplicitFactoryAction =
        MakeUiSettingsActionCommand(
            "test-action",
            UiSettingsCommandSection::General,
            "test");
    static_assert(
        ImplicitFactoryValue.factoryMutationPolicy ==
            UiSettingsFactoryMutationPolicy::Locked &&
        ImplicitFactoryAction.factoryMutationPolicy ==
            UiSettingsFactoryMutationPolicy::Locked);

    constexpr std::array<std::size_t, 13> ExpectedSectionCounts = {
        5u,  // UI
        6u,  // General
        20u, // Visibility
        7u,  // Buffers
        9u,  // Statistics
        21u, // Aliasing
        9u,  // Sky
        23u, // Lights
        13u, // Screen-space directional shadows
        62u, // Sparse virtual shadow maps
        46u, // Diagnostic cascaded shadow maps
        3u,  // Footer
        21u  // Materials
    };

    const std::set<std::string> expectedUiSafe = {
        "camera.location",
        "camera.mode",
        "material-editor.visible",
        "statistics.effect",
        "ui.settings-collapsed",
        "ui.skin",
        "ui.visible",
        "ui.zoom"
    };
    const std::set<std::string> expectedDynamicSelections = {
        "gpu.adapter",
        "light.selected",
        "material.selected",
        "scene.current"
    };

    std::set<std::string> names;
    std::set<std::string> uiSafe;
    std::set<std::string> dynamicSelections;
    std::array<std::size_t, 13> sectionCounts{};
    std::size_t actionCount = 0u;
    std::size_t booleanCount = 0u;
    std::size_t dynamicCount = 0u;

    for (const UiSettingsCommandDefinition& definition :
        UiSettingsCommandCatalog)
    {
        Require(
            IsStableLowercaseName(definition.name),
            "every path/action must be nonempty stable lowercase ASCII");
        Require(
            names.insert(std::string(definition.name)).second,
            "every path/action must occur exactly once");
        Require(
            !definition.domain.empty(),
            "every operation must provide a concise domain summary");

        const std::size_t sectionIndex =
            static_cast<std::size_t>(definition.section);
        Require(
            sectionIndex < sectionCounts.size(),
            "every operation must belong to a known section");
        ++sectionCounts[sectionIndex];

        const bool action =
            definition.kind == UiSettingsCommandKind::Action;
        if (action)
        {
            ++actionCount;
            Require(
                definition.supportedVerbs ==
                    UiSettingsVerbMask(UiSettingsCommandVerb::Run),
                "actions must support run and no value verbs");
            Require(
                !definition.dynamic,
                "actions must not masquerade as dynamic value paths");
        }
        else
        {
            Require(
                definition.Supports(UiSettingsCommandVerb::Get) &&
                    definition.Supports(UiSettingsCommandVerb::Set),
                "every value path must support get and set");
            Require(
                !definition.Supports(UiSettingsCommandVerb::Run),
                "value paths must not support run");
        }

        const bool boolean =
            definition.kind == UiSettingsCommandKind::Boolean;
        if (boolean)
        {
            ++booleanCount;
            Require(
                definition.Supports(UiSettingsCommandVerb::Get) &&
                    definition.Supports(UiSettingsCommandVerb::Set) &&
                    definition.Supports(UiSettingsCommandVerb::Toggle),
                "boolean paths must support get, set, and toggle");
            Require(
                !definition.Supports(UiSettingsCommandVerb::Run),
                "boolean paths must not support run");
        }
        else
        {
            Require(
                !definition.Supports(UiSettingsCommandVerb::Toggle),
                "only boolean paths may support toggle");
        }

        if (definition.dynamic)
        {
            ++dynamicCount;
            Require(
                definition.section ==
                        UiSettingsCommandSection::General ||
                    definition.section ==
                        UiSettingsCommandSection::Lights ||
                    definition.section ==
                        UiSettingsCommandSection::Materials,
                "dynamic paths must belong to a runtime-owned section");
        }

        if (definition.kind ==
            UiSettingsCommandKind::DynamicSelection)
        {
            Require(
                definition.dynamic,
                "dynamic-selection paths must advertise dynamic metadata");
            dynamicSelections.insert(std::string(definition.name));
        }

        if (definition.factoryMutationPolicy ==
            UiSettingsFactoryMutationPolicy::UiSafe)
        {
            uiSafe.insert(std::string(definition.name));
        }
        else
        {
            Require(
                definition.factoryMutationPolicy ==
                    UiSettingsFactoryMutationPolicy::Locked,
                "unknown factory policies must fail closed");
        }
    }

    Require(
        names.size() == UiSettingsCommandCatalog.size(),
        "the catalog must contain exactly 245 unique operations");
    Require(
        sectionCounts == ExpectedSectionCounts,
        "section counts must retain the complete Settings inventory");
    Require(
        actionCount == 10u,
        "the catalog must contain ten Settings actions");
    Require(
        booleanCount > 0u,
        "the catalog must exercise boolean command contracts");
    Require(
        dynamicCount == 46u,
        "the catalog must contain all 46 runtime-owned paths");
    Require(
        dynamicSelections == expectedDynamicSelections,
        "dynamic selections must cover adapters, scenes, lights, and materials");
    Require(
        uiSafe == expectedUiSafe &&
            uiSafe.size() == 8u,
        "only the eight proven topology-neutral operations may mutate the factory build");

    struct SelectionContract
    {
        std::string_view name;
        bool supportsReset;
    };
    constexpr SelectionContract SelectionContracts[] = {
        { "gpu.adapter", false },
        { "scene.current", false },
        { "light.selected", true },
        { "material.selected", false }
    };
    for (const SelectionContract& expected : SelectionContracts)
    {
        const UiSettingsCommandDefinition* definition =
            Find(expected.name);
        Require(
            definition &&
                definition->kind ==
                    UiSettingsCommandKind::DynamicSelection &&
                definition->Supports(
                    UiSettingsCommandVerb::Reset) ==
                    expected.supportsReset,
            "dynamic selection reset metadata must match its UI contract");
    }

    struct DomainContract
    {
        std::string_view name;
        std::string_view domain;
    };
    constexpr DomainContract DomainContracts[] = {
        {
            "world-materials",
            "white-world-off|white-world-on|preserve-detail|"
            "preserve-lighting|indirect-diffuse"
        },
        {
            "visibility.reconstruction.method",
            "full-resolution|guide-aware-upsampling|joint-bilateral|"
            "gaussian-bilateral|depth-guided|depth-normal|slope-aware|"
            "leakage-limited"
        },
        {
            "shadows.svsm.debug-view",
            "off|clipmap-level|required-pages|resident-pages|cached-pages|"
            "dirty-pages|rendered-pages|physical-pages|fallback-level|"
            "missing-pages|tap-count|visibility"
        },
        {
            "shadows.csm.filter-radius",
            "float 0..8 texels; Poisson only"
        },
        {
            "anti-aliasing.temporal-cost",
            "minimum|reduced|full-quality"
        },
        {
            "anti-aliasing.previous-depth-validation",
            "temporal-cost|four-texel-footprint|stationary-bypass"
        },
        {
            "anti-aliasing.sample-resurrection",
            "preset|off|one-older-frame|two-older-frames"
        },
        {
            "shadows.screen-space-directional.debug-view",
            "off|edge|thread|wave"
        },
        {
            "light.selected.flashlight.camera-offset",
            "meters 0..0.4; flashlight_1"
        },
        {
            "light.selected.flashlight.hotspot-size",
            "float 0.2..0.75; flashlight_1"
        }
    };
    for (const DomainContract& expected : DomainContracts)
    {
        const UiSettingsCommandDefinition* definition =
            Find(expected.name);
        Require(
            definition && definition->domain == expected.domain,
            "catalog domains must exactly enumerate their accepted visible "
            "values");
    }
    const UiSettingsCommandDefinition* reconstructionMethod =
        Find("visibility.reconstruction.method");
    Require(
        reconstructionMethod &&
            reconstructionMethod->domain.find(
                "available-packed-edge-profile") ==
                std::string_view::npos,
        "reconstruction completion must not expose a placeholder value");

    std::set<std::string> exemptionNames;
    for (const std::string_view name :
        UiSettingsNavigationExemptions)
    {
        Require(
            IsStableLowercaseName(name),
            "navigation exemptions must use stable lowercase names");
        Require(
            exemptionNames.insert(std::string(name)).second,
            "navigation exemptions must be unique");
    }
    for (const std::string_view name :
        UiSettingsTelemetryExemptions)
    {
        Require(
            IsStableLowercaseName(name),
            "telemetry exemptions must use stable lowercase names");
        Require(
            exemptionNames.insert(std::string(name)).second,
            "telemetry exemptions must be separate and unique");
    }
    Require(
        UiSettingsNavigationExemptions.size() == 5u &&
            UiSettingsTelemetryExemptions.size() == 8u,
        "navigation and telemetry exemptions must stay explicit");

    return EXIT_SUCCESS;
}
