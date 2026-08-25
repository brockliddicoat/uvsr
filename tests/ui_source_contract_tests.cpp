#include "ui_settings_command_catalog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "UI integration contract failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
            Fail(message);
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

    void TestSectionDispatchTopology()
    {
        using Section = uvsr::UiSettingsCommandSection;
        using Dispatcher = uvsr::UiSettingsCommandDispatcher;
        constexpr std::array<Dispatcher, 13> Expected = {
            Dispatcher::Ui,
            Dispatcher::General,
            Dispatcher::Representation,
            Dispatcher::Noise,
            Dispatcher::Visibility,
            Dispatcher::Denoising,
            Dispatcher::Aliasing,
            Dispatcher::Debug,
            Dispatcher::Sky,
            Dispatcher::Lights,
            Dispatcher::DirectionalShadows,
            Dispatcher::Materials,
            Dispatcher::None
        };
        static_assert(static_cast<std::size_t>(Section::Count) ==
            Expected.size());
        for (std::size_t index = 0u; index < Expected.size(); ++index)
        {
            Require(uvsr::ResolveUiSettingsCommandDispatcher(
                    static_cast<Section>(index)) == Expected[index],
                "settings section resolved to the wrong dispatcher");
        }
        Require(uvsr::ResolveUiSettingsCommandDispatcher(Section::Count) ==
                Dispatcher::None,
            "section sentinel resolved to a live dispatcher");
        Require(uvsr::ResolveUiSettingsCommandDispatcher(
                static_cast<Section>(0xffu)) == Dispatcher::None,
            "corrupt section resolved to a live dispatcher");
    }

    void TestLoadingLocks()
    {
        using Section = uvsr::UiSettingsCommandSection;
        for (std::uint32_t index = 0u;
            index <= static_cast<std::uint32_t>(Section::Count);
            ++index)
        {
            const Section section = static_cast<Section>(index);
            Require(!uvsr::IsUiSettingsRuntimeMutationLocked(
                    section, false),
                "idle runtime locked a settings section");
            Require(uvsr::IsUiSettingsRuntimeMutationLocked(
                    section, true) == (section != Section::Ui),
                "scene-loading lock did not cover every non-UI section");
        }
    }

    void TestCatalogPublication()
    {
        using Kind = uvsr::UiSettingsCommandKind;
        using Section = uvsr::UiSettingsCommandSection;
        using Verb = uvsr::UiSettingsCommandVerb;
        using Dispatcher = uvsr::UiSettingsCommandDispatcher;

        std::array<std::size_t,
            static_cast<std::size_t>(Section::Count)> sectionCounts{};
        std::size_t previousSection = 0u;
        bool first = true;
        for (const auto& definition : uvsr::UiSettingsCommandCatalog)
        {
            const std::size_t section =
                static_cast<std::size_t>(definition.section);
            Require(section < sectionCounts.size(),
                "catalog entry used a sentinel or corrupt section");
            Require(first || section >= previousSection,
                "catalog section order changed");
            first = false;
            previousSection = section;
            ++sectionCounts[section];

            if (definition.kind == Kind::Action)
            {
                Require(definition.Supports(Verb::Run) &&
                        !definition.Supports(Verb::Get) &&
                        !definition.Supports(Verb::Set),
                    "action verbs changed");
            }
            else
            {
                Require(definition.Supports(Verb::Get) &&
                        definition.Supports(Verb::Set) &&
                        !definition.Supports(Verb::Run),
                    "value verbs changed");
                Require(uvsr::ResolveUiSettingsCommandDispatcher(
                        definition.section) != Dispatcher::None,
                    "value entry has no production dispatcher");
            }
        }
        for (std::size_t count : sectionCounts)
            Require(count > 0u, "live UI section has no catalog owner");
    }

    void TestResetAndMaterialMetadata()
    {
        using Kind = uvsr::UiSettingsCommandKind;
        using Section = uvsr::UiSettingsCommandSection;
        using Verb = uvsr::UiSettingsCommandVerb;
        using Persistence = uvsr::UiSettingsPersistence;

        const auto* reset = Find("reset-settings");
        Require(reset && reset->kind == Kind::Action &&
                reset->section == Section::Footer &&
                reset->Supports(Verb::Run) &&
                reset->persistence == Persistence::None,
            "factory-reset action metadata changed");

        const auto* selected = Find("material.selected");
        Require(selected && selected->kind == Kind::DynamicSelection &&
                selected->section == Section::Materials &&
                selected->dynamic && selected->Supports(Verb::Get) &&
                selected->Supports(Verb::Set) &&
                !selected->Supports(Verb::Reset) &&
                selected->persistence == Persistence::SnapshotCatalog,
            "material selector publication metadata changed");

        const auto* adapter = Find("gpu.adapter");
        const auto* scene = Find("scene.current");
        const auto* light = Find("light.selected");
        Require(adapter && scene && light && adapter->dynamic &&
                scene->dynamic && light->dynamic,
            "runtime selector catalog ownership changed");
    }
}

int main()
{
    TestSectionDispatchTopology();
    TestLoadingLocks();
    TestCatalogPublication();
    TestResetAndMaterialMetadata();
    std::cout << "UVSR UI direct integration contracts passed\n";
    return EXIT_SUCCESS;
}
