#include "ui_settings_command_catalog.h"
#include "settings_snapshot.h"
#include "engine_identity.h"
#include "auto_exposure_shared.h"
#include "display_presentation.h"
#include "tone_mapping_settings.h"
#include "path_tracing_settings.h"
#include "flashlight.h"
#include "image_based_lighting_sources.h"

#include <charconv>
#include <limits>

namespace uvsr
{
namespace
{
    [[nodiscard]] constexpr UiSettingsTypedDomain BooleanDomain(
        std::string_view presentation = "on|off") noexcept
    {
        return { UiSettingsDomainKind::Boolean,
            UiSettingsSelectorKind::None, false, 0.0, 0.0, false, 0.0,
            false, 0.0, {}, 0u, presentation };
    }

    [[nodiscard]] constexpr UiSettingsTypedDomain IntegerDomain(
        std::int64_t minimum,
        std::int64_t maximum,
        std::string_view presentation) noexcept
    {
        return { UiSettingsDomainKind::Integer,
            UiSettingsSelectorKind::None, true,
            static_cast<double>(minimum), static_cast<double>(maximum),
            false, 0.0, false, 0.0, {}, 0u, presentation };
    }

    [[nodiscard]] constexpr UiSettingsTypedDomain IntegerDomainWithAlternative(
        std::int64_t minimum,
        std::int64_t maximum,
        std::int64_t alternative,
        std::string_view presentation) noexcept
    {
        UiSettingsTypedDomain result =
            IntegerDomain(minimum, maximum, presentation);
        result.hasAlternative = true;
        result.alternative = static_cast<double>(alternative);
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDomain FloatDomain(
        float minimum,
        float maximum,
        std::string_view presentation) noexcept
    {
        return { UiSettingsDomainKind::Float,
            UiSettingsSelectorKind::None, true,
            static_cast<double>(minimum), static_cast<double>(maximum),
            false, 0.0, false, 0.0, {}, 0u, presentation };
    }

    [[nodiscard]] constexpr UiSettingsTypedDomain FloatDomainWithAlternative(
        float minimum,
        float maximum,
        float alternative,
        std::string_view presentation) noexcept
    {
        UiSettingsTypedDomain result =
            FloatDomain(minimum, maximum, presentation);
        result.hasAlternative = true;
        result.alternative = static_cast<double>(alternative);
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDomain FloatDomainWithContextMaximum(
        float minimum,
        float maximum,
        float contextMaximum,
        std::string_view presentation) noexcept
    {
        UiSettingsTypedDomain result =
            FloatDomain(minimum, maximum, presentation);
        result.hasContextMaximum = true;
        result.contextMaximum = static_cast<double>(contextMaximum);
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDomain Float3Domain(
        float minimum,
        float maximum,
        std::string_view presentation) noexcept
    {
        UiSettingsTypedDomain result =
            FloatDomain(minimum, maximum, presentation);
        result.kind = UiSettingsDomainKind::Float3;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDomain EnumerationDomain(
        std::string_view tokens) noexcept
    {
        UiSettingsTypedDomain result;
        result.kind = UiSettingsDomainKind::Enumeration;
        result.presentation = tokens;
        while (!tokens.empty())
        {
            if (result.tokenCount == result.tokens.size())
                return {};
            const std::size_t separator = tokens.find('|');
            result.tokens[result.tokenCount++] = tokens.substr(0u, separator);
            if (separator == std::string_view::npos)
                break;
            tokens.remove_prefix(separator + 1u);
        }
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDomain SelectorDomain(
        UiSettingsSelectorKind selector,
        std::string_view presentation) noexcept
    {
        return { UiSettingsDomainKind::Selector, selector, false, 0.0, 0.0,
            false, 0.0, false, 0.0, {}, 0u, presentation };
    }

    [[nodiscard]] constexpr UiSettingsTypedDomain ActionDomain(
        std::string_view presentation) noexcept
    {
        return { UiSettingsDomainKind::Action,
            UiSettingsSelectorKind::None, false, 0.0, 0.0, false, 0.0,
            false, 0.0, {}, 0u, presentation };
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault LiteralBooleanDefault(
        bool value) noexcept
    {
        UiSettingsTypedDefault result;
        result.value.kind = UiSettingsDefaultValueKind::Boolean;
        result.value.boolean = value;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault LiteralIntegerDefault(
        std::int64_t value) noexcept
    {
        UiSettingsTypedDefault result;
        result.value.kind = UiSettingsDefaultValueKind::Integer;
        result.value.integer = value;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault LiteralFloatDefault(
        float value) noexcept
    {
        UiSettingsTypedDefault result;
        result.value.kind = UiSettingsDefaultValueKind::Float;
        result.value.scalar = value;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault LiteralFloat3Default(
        float x,
        float y,
        float z) noexcept
    {
        UiSettingsTypedDefault result;
        result.value.kind = UiSettingsDefaultValueKind::Vector;
        result.value.vector = { x, y, z, 0.f };
        result.value.componentCount = 3u;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault LiteralTokenDefault(
        std::string_view token) noexcept
    {
        UiSettingsTypedDefault result;
        result.value.kind = UiSettingsDefaultValueKind::Token;
        result.value.text = token;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault ContextDefault(
        UiSettingsDefaultPolicy policy) noexcept
    {
        return { policy, {} };
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault ContextBooleanDefault(
        UiSettingsDefaultPolicy policy,
        bool value) noexcept
    {
        UiSettingsTypedDefault result = LiteralBooleanDefault(value);
        result.policy = policy;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault ContextFloatDefault(
        UiSettingsDefaultPolicy policy,
        float value) noexcept
    {
        UiSettingsTypedDefault result = LiteralFloatDefault(value);
        result.policy = policy;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault ContextIntegerDefault(
        UiSettingsDefaultPolicy policy,
        std::int64_t value) noexcept
    {
        UiSettingsTypedDefault result = LiteralIntegerDefault(value);
        result.policy = policy;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault ContextTokenDefault(
        UiSettingsDefaultPolicy policy,
        std::string_view value) noexcept
    {
        UiSettingsTypedDefault result = LiteralTokenDefault(value);
        result.policy = policy;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault ContextFloat3Default(
        UiSettingsDefaultPolicy policy,
        float x,
        float y,
        float z) noexcept
    {
        UiSettingsTypedDefault result = LiteralFloat3Default(x, y, z);
        result.policy = policy;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsTypedDefault ContextSelectorDefault(
        UiSettingsDefaultPolicy policy,
        std::string_view selector) noexcept
    {
        UiSettingsTypedDefault result;
        result.policy = policy;
        result.value.kind = UiSettingsDefaultValueKind::Selector;
        result.value.text = selector;
        return result;
    }

    [[nodiscard]] constexpr UiSettingsCommandKind ResolveUiSettingsCommandKind(
        UiSettingsDomainKind kind) noexcept
    {
        switch (kind)
        {
        case UiSettingsDomainKind::Boolean:
            return UiSettingsCommandKind::Boolean;
        case UiSettingsDomainKind::Integer:
            return UiSettingsCommandKind::Integer;
        case UiSettingsDomainKind::Float:
            return UiSettingsCommandKind::Float;
        case UiSettingsDomainKind::Float3:
            return UiSettingsCommandKind::Float3;
        case UiSettingsDomainKind::Enumeration:
            return UiSettingsCommandKind::Enum;
        case UiSettingsDomainKind::Selector:
            return UiSettingsCommandKind::DynamicSelection;
        case UiSettingsDomainKind::Action:
            return UiSettingsCommandKind::Action;
        }
        return UiSettingsCommandKind::Enum;
    }

    struct Setting : UiSettingsCommandDefinition
    {
        constexpr Setting& Available(UiSettingsAvailability value) noexcept
        {
            availability = value;
            return *this;
        }
        constexpr Setting& After(SettingId value) noexcept
        {
            dependsOn = value;
            return *this;
        }
        constexpr Setting& ValueAfter(SettingId value) noexcept
        {
            valueDependsOn = value;
            return *this;
        }
        constexpr Setting& Preserve() noexcept
        {
            factoryReset = UiSettingsFactoryResetPolicy::Preserve;
            return *this;
        }
        constexpr Setting& RawSnapshot() noexcept
        {
            storage = UiSettingsStoragePolicy::Latent;
            snapshotRead = UiSettingsSnapshotReadPolicy::LatentValue;
            return *this;
        }
        constexpr Setting& NoEffects() noexcept
        {
            effects = UiSettingsMutationEffect::None;
            return *this;
        }
        constexpr Setting& Track(double minimum, double maximum) noexcept
        {
            presentation.hasTrackRange = true;
            presentation.trackMinimum = minimum;
            presentation.trackMaximum = maximum;
            return *this;
        }
        constexpr Setting& Display(float scale, UiSettingsDisplayUnit unit) noexcept
        {
            presentation.displayScale = scale;
            presentation.displayUnit = unit;
            return *this;
        }
        constexpr Setting& Label(std::string_view token, std::string_view label) noexcept
        {
            for (std::uint8_t index = 0u; index < typedDomain.tokenCount; ++index)
            {
                if (typedDomain.tokens[index] == token)
                    presentation.tokenLabels[index] = label;
            }
            return *this;
        }
    };

    [[nodiscard]] constexpr Setting Value(SettingId id, Section section,
        UiSettingsTypedDomain domain, UiSettingsTypedDefault defaultValue,
        bool supportsReset = true, bool dynamic = false,
        UiSettingsPersistence persistence = UiSettingsPersistence::SnapshotCatalog)
    {
        Setting result;
        result.id = id;
        result.name = SettingName(id);
        result.kind = ResolveUiSettingsCommandKind(domain.kind);
        result.section = section;
        result.dynamic = dynamic;
        result.persistence = persistence;
        result.typedDomain = domain;
        result.typedDefault = defaultValue;
        result.presentation = { domain.hasRange, domain.minimum, domain.maximum };
        result.supportedVerbs = static_cast<std::uint8_t>(UiSettingsCommandVerb::Get) |
            static_cast<std::uint8_t>(UiSettingsCommandVerb::Set) |
            (result.kind == Kind::Boolean ? static_cast<std::uint8_t>(UiSettingsCommandVerb::Toggle) : 0u) |
            (supportsReset ? static_cast<std::uint8_t>(UiSettingsCommandVerb::Reset) : 0u);
        result.factoryReset = UiSettingsFactoryResetPolicy::Reset;
        result.effects = section == Section::Ui || section == Section::General ||
            section == Section::Noise || section == Section::Footer || section == Section::Count
            ? UiSettingsMutationEffect::None : UiSettingsMutationEffect::RendererHistory;
        if (result.kind == Kind::DynamicSelection)
        {
            result.applicationRole = domain.selector == UiSettingsSelectorKind::Adapter
                ? UiSettingsApplicationRole::StartupPrecondition : UiSettingsApplicationRole::Selector;
            result.Preserve().NoEffects();
            if (domain.selector == UiSettingsSelectorKind::Light ||
                domain.selector == UiSettingsSelectorKind::Material)
                result.After(SettingId::SceneCurrent);
        }
        else if (dynamic)
        {
            result.applicationRole = UiSettingsApplicationRole::LiveDependent;
            result.Preserve();
            if (section == Section::Lights)
                result.After(SettingId::LightSelected).Available(UiSettingsAvailability::SelectedLight);
            if (section == Section::Materials)
            {
                result.After(SettingId::MaterialSelected).Available(UiSettingsAvailability::SelectedMaterial).RawSnapshot();
                result.effects = UiSettingsMutationEffect::Material | UiSettingsMutationEffect::RendererHistory;
            }
        }
        if (defaultValue.policy == UiSettingsDefaultPolicy::FlashlightDefault ||
            defaultValue.policy == UiSettingsDefaultPolicy::SelectedLightColor)
        {
            result.factoryReset = UiSettingsFactoryResetPolicy::GlobalFlashlight;
            if (defaultValue.policy == UiSettingsDefaultPolicy::FlashlightDefault)
                result.Available(UiSettingsAvailability::SelectedFlashlight);
        }
        return result;
    }
    [[nodiscard]] constexpr UiSettingsCommandDefinition Action(
        ActionId actionId,
        std::string_view name,
        UiSettingsCommandSection section,
        std::string_view presentation)
    {
        (void)actionId;
        return {
            name,
            UiSettingsCommandKind::Action,
            section,
            static_cast<std::uint8_t>(UiSettingsCommandVerb::Run),
            false,
            UiSettingsPersistence::None,
            SettingId::Invalid,
            actionId,
            ActionDomain(presentation),
            {},
            {},
            UiSettingsApplicationRole::Action,
            SettingId::Invalid,
            SettingId::Invalid,
            UiSettingsAvailability::Always,
            UiSettingsStoragePolicy::ContextOnly,
            UiSettingsSnapshotReadPolicy::AvailableValue,
            UiSettingsFactoryResetPolicy::Preserve,
            UiSettingsMutationEffect::None,
            0xffffu
        };
    }

    using Kind = UiSettingsCommandKind;
    using Section = UiSettingsCommandSection;

    [[nodiscard]] constexpr UiSettingsCommandDefinition DescribeSetting(SettingId id) noexcept
    {
        switch (id)
        {
#define UVSR_SETTING(symbol, name, metadata) case SettingId::symbol: return metadata;
#include "ui_settings_catalog.def"
#undef UVSR_SETTING
        default: return {};
        }
    }
    [[nodiscard]] constexpr UiSettingsCommandDefinition DescribeAction(
        ActionId id) noexcept
    {
        switch (id)
        {
    #define UVSR_DESCRIBE_ACTION(symbol, name, section, presentation) \
        case ActionId::symbol: \
            return Action(id, name, Section::section, presentation);
        UVSR_ACTION_ID_LIST(UVSR_DESCRIBE_ACTION)
    #undef UVSR_DESCRIBE_ACTION
        default:
            return {};
        }
    }

    #undef UVSR_ACTION_ID_LIST

    [[nodiscard]] constexpr auto BuildCanonicalUiSettingsCatalog() noexcept
    {
        std::array<UiSettingsCommandDefinition,
            AllSettingIds.size() + AllActionIds.size()> definitions{};
        std::size_t cursor = 0u;
        for (const SettingId id : AllSettingIds)
        {
            definitions[cursor] = DescribeSetting(id);
            definitions[cursor].bindingIndex =
                static_cast<std::uint16_t>(cursor);
            ++cursor;
        }
        std::uint16_t actionIndex = 0u;
        for (const ActionId id : AllActionIds)
        {
            definitions[cursor] = DescribeAction(id);
            definitions[cursor].bindingIndex = actionIndex++;
            ++cursor;
        }
        return definitions;
    }

    constexpr auto CanonicalCatalog =
        BuildCanonicalUiSettingsCatalog();

    [[nodiscard]] constexpr bool IsCanonicalSettingId(
        SettingId id) noexcept
    {
        for (const SettingId candidate : AllSettingIds)
        {
            if (candidate == id)
                return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool IsUiSettingsDefaultCompatible(
        const UiSettingsCommandDefinition& definition) noexcept
    {
        const UiSettingsDefaultValue& value = definition.typedDefault.value;
        if (value.kind == UiSettingsDefaultValueKind::None)
            return definition.typedDefault.policy !=
                UiSettingsDefaultPolicy::Literal;

        switch (definition.typedDomain.kind)
        {
        case UiSettingsDomainKind::Boolean:
            return value.kind == UiSettingsDefaultValueKind::Boolean;
        case UiSettingsDomainKind::Integer:
            if (value.kind != UiSettingsDefaultValueKind::Integer)
                return false;
            return static_cast<double>(value.integer) >=
                    definition.typedDomain.minimum &&
                static_cast<double>(value.integer) <=
                    definition.typedDomain.maximum ||
                definition.typedDomain.hasAlternative &&
                    static_cast<double>(value.integer) ==
                        definition.typedDomain.alternative;
        case UiSettingsDomainKind::Float:
        {
            if (value.kind != UiSettingsDefaultValueKind::Float)
                return false;
            const double scalar = static_cast<double>(value.scalar);
            return scalar >= definition.typedDomain.minimum &&
                    scalar <= (definition.typedDomain.hasContextMaximum
                        ? definition.typedDomain.contextMaximum
                        : definition.typedDomain.maximum) ||
                definition.typedDomain.hasAlternative &&
                    scalar == definition.typedDomain.alternative;
        }
        case UiSettingsDomainKind::Float3:
        {
            constexpr std::uint8_t componentCount = 3u;
            if (value.kind != UiSettingsDefaultValueKind::Vector ||
                value.componentCount != componentCount)
            {
                return false;
            }
            for (std::uint8_t index = 0u; index < componentCount; ++index)
            {
                const double component =
                    static_cast<double>(value.vector[index]);
                if (component < definition.typedDomain.minimum ||
                    component > definition.typedDomain.maximum)
                {
                    return false;
                }
            }
            return true;
        }
        case UiSettingsDomainKind::Enumeration:
            if (value.kind != UiSettingsDefaultValueKind::Token)
                return false;
            for (std::uint8_t index = 0u;
                index < definition.typedDomain.tokenCount; ++index)
            {
                if (definition.typedDomain.tokens[index] == value.text)
                    return true;
            }
            return false;
        case UiSettingsDomainKind::Selector:
            return value.kind == UiSettingsDefaultValueKind::Selector;
        case UiSettingsDomainKind::Action:
            return false;
        }
        return false;
    }

    template<std::size_t Size>
    [[nodiscard]] constexpr bool ValidateCatalog(
        const std::array<UiSettingsCommandDefinition, Size>& definitions)
        noexcept
    {
        if (Size != AllSettingIds.size() + AllActionIds.size())
            return false;

        std::size_t valueCount = 0u;
        std::size_t actionCount = 0u;
        for (std::size_t index = 0u; index < Size; ++index)
        {
            const UiSettingsCommandDefinition& definition =
                definitions[index];
            if (definition.name.empty() ||
                definition.typedDomain.presentation.empty())
            {
                return false;
            }
            const bool action =
                definition.kind == UiSettingsCommandKind::Action;
            if (action !=
                (definition.persistence == UiSettingsPersistence::None))
            {
                return false;
            }
            if (action)
            {
                ++actionCount;
                if (definition.id != SettingId::Invalid ||
                    definition.actionId == ActionId::Invalid ||
                    definition.bindingIndex >= AllActionIds.size() ||
                    AllActionIds[definition.bindingIndex] !=
                        definition.actionId ||
                    definition.applicationRole !=
                        UiSettingsApplicationRole::Action ||
                    definition.typedDomain.kind !=
                        UiSettingsDomainKind::Action ||
                    definition.typedDomain.tokenCount != 0u ||
                     definition.typedDefault.HasValue() ||
                     definition.dependsOn != SettingId::Invalid ||
                     definition.valueDependsOn != SettingId::Invalid)
                {
                    return false;
                }
            }
            else
            {
                ++valueCount;
                if (!IsCanonicalSettingId(definition.id) ||
                    definition.name != SettingName(definition.id) ||
                    static_cast<std::uint64_t>(definition.id) !=
                        StableUiSettingsIdValue(definition.name) ||
                    definition.actionId != ActionId::Invalid ||
                    definition.bindingIndex >= AllSettingIds.size() ||
                    AllSettingIds[definition.bindingIndex] != definition.id ||
                    definition.applicationRole ==
                        UiSettingsApplicationRole::Action ||
                    definition.typedDomain.kind ==
                        UiSettingsDomainKind::Action ||
                    !IsUiSettingsDefaultCompatible(definition) ||
                     definition.dependsOn == definition.id ||
                     definition.dependsOn != SettingId::Invalid &&
                         !IsCanonicalSettingId(definition.dependsOn) ||
                     definition.valueDependsOn == definition.id ||
                     definition.valueDependsOn != SettingId::Invalid &&
                         !IsCanonicalSettingId(definition.valueDependsOn))
                {
                    return false;
                }
            }

            const UiSettingsTypedDomain& domain = definition.typedDomain;
            const bool numeric =
                domain.kind == UiSettingsDomainKind::Integer ||
                domain.kind == UiSettingsDomainKind::Float ||
                domain.kind == UiSettingsDomainKind::Float3;
            if (numeric != domain.hasRange ||
                domain.hasRange && domain.minimum > domain.maximum ||
                domain.hasAlternative && !numeric ||
                domain.hasContextMaximum &&
                    (!numeric || domain.contextMaximum < domain.maximum) ||
                (domain.kind == UiSettingsDomainKind::Enumeration) !=
                    (domain.tokenCount != 0u))
            {
                return false;
            }
            for (std::uint8_t token = 0u; token < domain.tokenCount; ++token)
            {
                if (domain.tokens[token].empty())
                    return false;
                for (std::uint8_t other = token + 1u;
                    other < domain.tokenCount; ++other)
                {
                    if (domain.tokens[token] == domain.tokens[other])
                        return false;
                }
            }
            for (std::size_t token = domain.tokenCount;
                token < definition.presentation.tokenLabels.size(); ++token)
            {
                if (!definition.presentation.tokenLabels[token].empty())
                    return false;
            }
            if (definition.presentation.hasTrackRange &&
                (!domain.hasRange ||
                 definition.presentation.trackMinimum < domain.minimum ||
                 definition.presentation.trackMaximum >
                    (domain.hasContextMaximum
                        ? domain.contextMaximum
                        : domain.maximum) ||
                 definition.presentation.trackMinimum >
                    definition.presentation.trackMaximum))
            {
                return false;
            }
            for (std::size_t other = index + 1u;
                other < Size; ++other)
            {
                const UiSettingsCommandDefinition& candidate =
                    definitions[other];
                if (definition.name == candidate.name ||
                    (!action && candidate.kind !=
                        UiSettingsCommandKind::Action &&
                        definition.id == candidate.id) ||
                    (action && candidate.kind ==
                        UiSettingsCommandKind::Action &&
                        definition.actionId == candidate.actionId))
                {
                    return false;
                }
            }
        }

        for (const SettingId start : AllSettingIds)
        {
            std::array<SettingId, AllSettingIds.size() * 2u> pending{};
            std::array<SettingId, AllSettingIds.size()> visited{};
            std::size_t pendingCount = 0u;
            std::size_t visitedCount = 0u;
            for (const UiSettingsCommandDefinition& definition : definitions)
            {
                if (definition.id != start)
                    continue;
                if (definition.dependsOn != SettingId::Invalid)
                    pending[pendingCount++] = definition.dependsOn;
                if (definition.valueDependsOn != SettingId::Invalid)
                    pending[pendingCount++] = definition.valueDependsOn;
                break;
            }
            while (pendingCount != 0u)
            {
                const SettingId dependency = pending[--pendingCount];
                if (dependency == start)
                    return false;
                bool alreadyVisited = false;
                for (std::size_t index = 0u; index < visitedCount; ++index)
                {
                    if (visited[index] == dependency)
                    {
                        alreadyVisited = true;
                        break;
                    }
                }
                if (alreadyVisited)
                    continue;
                visited[visitedCount++] = dependency;
                for (const UiSettingsCommandDefinition& definition :
                    definitions)
                {
                    if (definition.id != dependency)
                        continue;
                    if (definition.dependsOn != SettingId::Invalid)
                        pending[pendingCount++] = definition.dependsOn;
                    if (definition.valueDependsOn != SettingId::Invalid)
                        pending[pendingCount++] = definition.valueDependsOn;
                    break;
                }
            }
        }
        return valueCount == AllSettingIds.size() &&
            actionCount == AllActionIds.size();
    }

    [[nodiscard]] constexpr bool ValidateCatalog() noexcept
    {
        return ValidateCatalog(CanonicalCatalog);
    }
    static_assert(ValidateCatalog(),
        "The typed settings schema must be complete and unique");

}

    constexpr auto CatalogFingerprint = BuildSettingsSnapshotSchemaFingerprint(CanonicalCatalog);
    constexpr auto CatalogVersion = ResolveSettingsSnapshotSchemaVersion(CatalogFingerprint);
    const UiSettingsCatalog UiSettingsCommandCatalog = CanonicalCatalog;
    const SettingsSnapshotSchemaFingerprint CurrentSettingsSnapshotSchemaFingerprint =
        CatalogFingerprint;
    const SettingsNumberHash CanonicalSettingsNumberHash = CatalogFingerprint;
    const EngineVersion CurrentEngineVersion = DeriveEngineVersion(CatalogFingerprint);
    const std::array<char, 33> CanonicalSettingsNumberHashText = BuildSettingsNumberHashText(CatalogFingerprint);
    const std::uint16_t SettingsSnapshotVersion = CatalogVersion;
    const std::array<char, 5> SettingsSnapshotVersionText = BuildSettingsSnapshotVersionText(CatalogVersion);

    bool ValidateCanonicalSettingsSchema(const UiSettingsCatalog& definitions) noexcept
    {
        return ValidateCatalog(definitions);
    }
    std::string FormatUiSettingsMetadataFloat(
        float value)
    {
        char buffer[64]{};
        const auto result = std::to_chars(
            buffer,
            buffer + sizeof(buffer),
            value,
            std::chars_format::general,
            std::numeric_limits<float>::max_digits10);
        return result.ec == std::errc{}
            ? std::string(buffer, result.ptr)
            : std::string{};
    }

    std::string_view UiSettingsDefaultPolicyName(
        UiSettingsDefaultPolicy policy) noexcept
    {
        switch (policy)
        {
        case UiSettingsDefaultPolicy::Literal: return "literal";
        case UiSettingsDefaultPolicy::HighestMemoryAdapter:
            return "highest-memory-adapter";
        case UiSettingsDefaultPolicy::RetainedBistro:
            return "retained-bistro";
        case UiSettingsDefaultPolicy::EnvironmentExposure:
            return "environment-exposure";
        case UiSettingsDefaultPolicy::FxaaQualityProfile:
            return "fxaa-quality-profile";
        case UiSettingsDefaultPolicy::SceneDefaultLight:
            return "scene-default-light";
        case UiSettingsDefaultPolicy::SceneAuthored:
            return "scene-authored";
        case UiSettingsDefaultPolicy::SelectedLightColor:
            return "selected-light-color";
        case UiSettingsDefaultPolicy::FlashlightDefault:
            return "flashlight-default";
        case UiSettingsDefaultPolicy::NoMaterial: return "no-material";
        case UiSettingsDefaultPolicy::MaterialAuthored:
            return "material-authored";
        }
        return {};
    }

    std::string FormatUiSettingsDomain(
        const UiSettingsCommandDefinition& definition)
    {
        const UiSettingsTypedDomain& domain = definition.typedDomain;
        if (domain.kind != UiSettingsDomainKind::Enumeration)
            return std::string(domain.presentation);
        std::string result;
        for (std::uint8_t index = 0u; index < domain.tokenCount; ++index)
        {
            if (!result.empty())
                result.push_back('|');
            result.append(domain.tokens[index]);
        }
        return result;
    }

    std::string FormatUiSettingsTokenLabel(
        SettingId id,
        std::size_t tokenIndex)
    {
        const UiSettingsCommandDefinition* definition = nullptr;
        for (const UiSettingsCommandDefinition& candidate :
            UiSettingsCommandCatalog)
        {
            if (candidate.id == id)
            {
                definition = &candidate;
                break;
            }
        }
        if (!definition || tokenIndex >= definition->typedDomain.tokenCount)
            return {};
        const std::string_view overrideLabel =
            definition->presentation.tokenLabels[tokenIndex];
        if (!overrideLabel.empty())
            return std::string(overrideLabel);
        std::string label(definition->typedDomain.tokens[tokenIndex]);
        bool capitalize = true;
        for (char& character : label)
        {
            if (character == '-')
            {
                character = ' ';
                capitalize = true;
            }
            else if (capitalize && character >= 'a' && character <= 'z')
            {
                character = static_cast<char>(
                    character - 'a' + 'A');
                capitalize = false;
            }
            else
            {
                capitalize = false;
            }
        }
        return label;
    }

    std::string FormatUiSettingsDefaultAnchor(
        const UiSettingsCommandDefinition& definition)
    {
        const UiSettingsDefaultValue& value = definition.typedDefault.value;
        switch (value.kind)
        {
        case UiSettingsDefaultValueKind::Boolean:
            return value.boolean ? "on" : "off";
        case UiSettingsDefaultValueKind::Integer:
            return std::to_string(value.integer);
        case UiSettingsDefaultValueKind::Float:
            return FormatUiSettingsMetadataFloat(value.scalar);
        case UiSettingsDefaultValueKind::Vector:
        {
            std::string result;
            for (std::uint8_t index = 0u;
                index < value.componentCount; ++index)
            {
                if (!result.empty())
                    result.push_back(' ');
                result += FormatUiSettingsMetadataFloat(value.vector[index]);
            }
            return result;
        }
        case UiSettingsDefaultValueKind::Token:
        case UiSettingsDefaultValueKind::Selector:
            return std::string(value.text);
        case UiSettingsDefaultValueKind::None:
            return std::string(UiSettingsDefaultPolicyName(
                definition.typedDefault.policy));
        }
        return {};
    }

    std::string FormatUiSettingsDefault(
        const UiSettingsCommandDefinition& definition)
    {
        const std::string anchor =
            FormatUiSettingsDefaultAnchor(definition);
        if (definition.typedDefault.policy ==
            UiSettingsDefaultPolicy::Literal)
        {
            return anchor;
        }
        std::string result(UiSettingsDefaultPolicyName(
            definition.typedDefault.policy));
        if (!anchor.empty())
        {
            result.push_back('(');
            result += anchor;
            result.push_back(')');
        }
        return result;
    }

}
