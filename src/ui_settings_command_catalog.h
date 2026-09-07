#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace uvsr
{
    [[nodiscard]] constexpr std::uint64_t StableUiSettingsIdValue(
        std::string_view name) noexcept
    {
        std::uint64_t value = 14695981039346656037ull;
        for (const unsigned char byte : name)
        {
            value ^= byte;
            value *= 1099511628211ull;
        }
        return value;
    }

    enum class SettingId : std::uint64_t
    {
        Invalid = 0u,
#define UVSR_SETTING(symbol, name, metadata) symbol = StableUiSettingsIdValue(name),
#include "ui_settings_catalog.def"
#undef UVSR_SETTING
    };

    inline constexpr auto AllSettingIds = std::array{
#define UVSR_SETTING(symbol, name, metadata) SettingId::symbol,
#include "ui_settings_catalog.def"
#undef UVSR_SETTING
    };

    [[nodiscard]] constexpr std::string_view SettingName(SettingId id) noexcept
    {
        switch (id)
        {
#define UVSR_SETTING(symbol, name, metadata) case SettingId::symbol: return name;
#include "ui_settings_catalog.def"
#undef UVSR_SETTING
        default: return {};
        }
    }

    #define UVSR_ACTION_ID_LIST(X) \
        X(OpenSceneFolder, "open-scene-folder", General, \
            "open the active scene directory") \
        X(ResetSettings, "reset-settings", Footer, \
            "restore renderer and interface factory settings") \
        X(Capture, "capture", Footer, \
            "copy the current frame to the clipboard") \
        X(Restart, "restart", Footer, "restart UVSR")

    enum class ActionId : std::uint64_t
    {
        Invalid = 0u,
    #define UVSR_DECLARE_ACTION_ID(symbol, name, section, presentation) \
        symbol = StableUiSettingsIdValue(name),
        UVSR_ACTION_ID_LIST(UVSR_DECLARE_ACTION_ID)
    #undef UVSR_DECLARE_ACTION_ID
    };

    inline constexpr auto AllActionIds = std::array{
    #define UVSR_INVENTORY_ACTION_ID(symbol, name, section, presentation) \
        ActionId::symbol,
        UVSR_ACTION_ID_LIST(UVSR_INVENTORY_ACTION_ID)
    #undef UVSR_INVENTORY_ACTION_ID
    };

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
        Noise = 3,
        Debug = 7,
        Sky,
        Lights,
        DirectionalShadows,
        Materials,
        Footer,
        Developer,
        Postprocess,
        Pathing,
        Count
    };

    [[nodiscard]] constexpr bool IsUiSettingsRuntimeMutationLocked(
        UiSettingsCommandSection section,
        bool sceneBusy) noexcept
    {
        return sceneBusy && section != UiSettingsCommandSection::Ui;
    }

    enum class UiSettingsCommandVerb : std::uint8_t
    {
        Get = 1u << 0u,
        Set = 1u << 1u,
        Toggle = 1u << 2u,
        Reset = 1u << 3u,
        Run = 1u << 4u
    };

    enum class UiSettingsPersistence : std::uint8_t
    {
        SnapshotCatalog,
        SessionOnly,
        None
    };

    enum class UiSettingsDomainKind : std::uint8_t
    {
        Boolean,
        Integer,
        Float,
        Float3,
        Enumeration = 5,
        Selector,
        Action
    };

    enum class UiSettingsSelectorKind : std::uint8_t
    {
        None,
        Adapter,
        Scene,
        Light,
        Material
    };

    enum class UiSettingsDefaultPolicy : std::uint8_t
    {
        Literal,
        HighestMemoryAdapter,
        RetainedBistro,
        EnvironmentExposure = 4,
        FxaaQualityProfile = 6,
        SceneDefaultLight,
        SceneAuthored,
        SelectedLightColor,
        FlashlightDefault,
        NoMaterial,
        MaterialAuthored
    };

    enum class UiSettingsApplicationRole : std::uint8_t
    {
        Mutable,
        LiveDependent,
        Selector,
        StartupPrecondition,
        Action
    };

    enum class UiSettingsAvailability : std::uint8_t
    {
        Always,
        SelectedLight = 2,
        DirectionalOrSpotLight,
        DirectionalLight,
        PointOrSpotLight,
        SpotLight,
        SelectedFlashlight,
        SelectedMaterial,
        BaseTexture,
        MetalSpecularTexture,
        SpecularGlossMaterial,
        MetalRoughMaterial,
        AlphaBlendedMaterial,
        AlphaTestedMaterial,
        NormalTexture,
        OcclusionTexture,
        EmissiveTexture,
        TransmissiveMaterial,
        TransmissionTexture,
        OpacityTexture
    };

    enum class UiSettingsStoragePolicy : std::uint8_t
    {
        ContextOnly,
        Latent
    };

    enum class UiSettingsSnapshotReadPolicy : std::uint8_t
    {
        AvailableValue,
        LatentValue
    };

    enum class UiSettingsMutationEffect : std::uint32_t
    {
        None = 0u,
        RendererHistory = 1u << 0u,
        Material = 1u << 1u
    };

    enum class UiSettingsFactoryResetPolicy : std::uint8_t
    {
        Preserve,
        Reset,
        GlobalFlashlight
    };

    [[nodiscard]] constexpr UiSettingsMutationEffect operator|(
        UiSettingsMutationEffect left,
        UiSettingsMutationEffect right) noexcept
    {
        return static_cast<UiSettingsMutationEffect>(
            static_cast<std::uint32_t>(left) |
            static_cast<std::uint32_t>(right));
    }

    struct UiSettingsTypedDomain
    {
        static constexpr std::size_t MaximumTokenCount = 16u;

        UiSettingsDomainKind kind = UiSettingsDomainKind::Enumeration;
        UiSettingsSelectorKind selector = UiSettingsSelectorKind::None;
        bool hasRange = false;
        double minimum = 0.0;
        double maximum = 0.0;
        bool hasAlternative = false;
        double alternative = 0.0;
        bool hasContextMaximum = false;
        double contextMaximum = 0.0;
        std::array<std::string_view, MaximumTokenCount> tokens{};
        std::uint8_t tokenCount = 0u;
        std::string_view presentation;
    };

    enum class UiSettingsDefaultValueKind : std::uint8_t
    {
        None,
        Boolean,
        Integer,
        Float,
        Vector,
        Token,
        Selector
    };

    struct UiSettingsDefaultValue
    {
        UiSettingsDefaultValueKind kind =
            UiSettingsDefaultValueKind::None;
        bool boolean = false;
        std::int64_t integer = 0;
        float scalar = 0.f;
        std::array<float, 4> vector{};
        std::uint8_t componentCount = 0u;
        std::string_view text;
    };

    struct UiSettingsTypedDefault
    {
        UiSettingsDefaultPolicy policy = UiSettingsDefaultPolicy::Literal;
        UiSettingsDefaultValue value;

        [[nodiscard]] constexpr bool HasValue() const noexcept
        {
            return value.kind != UiSettingsDefaultValueKind::None;
        }
    };

    enum class UiSettingsDisplayUnit : std::uint8_t
    {
        Native,
        Percent,
        Centimeters
    };

    struct UiSettingsPresentation
    {
        bool hasTrackRange = false;
        double trackMinimum = 0.0;
        double trackMaximum = 0.0;
        float displayScale = 1.f;
        UiSettingsDisplayUnit displayUnit = UiSettingsDisplayUnit::Native;
        std::array<std::string_view,
            UiSettingsTypedDomain::MaximumTokenCount> tokenLabels{};
    };

    struct UiSettingsCommandDefinition
    {
        std::string_view name;
        UiSettingsCommandKind kind = UiSettingsCommandKind::Enum;
        UiSettingsCommandSection section = UiSettingsCommandSection::General;
        std::uint8_t supportedVerbs = 0u;
        bool dynamic = false;
        UiSettingsPersistence persistence =
            UiSettingsPersistence::SnapshotCatalog;
        SettingId id = SettingId::Invalid;
        ActionId actionId = ActionId::Invalid;
        UiSettingsTypedDomain typedDomain;
        UiSettingsTypedDefault typedDefault;
        UiSettingsPresentation presentation;
        UiSettingsApplicationRole applicationRole =
            UiSettingsApplicationRole::Mutable;
        SettingId dependsOn = SettingId::Invalid;
        SettingId valueDependsOn = SettingId::Invalid;
        UiSettingsAvailability availability = UiSettingsAvailability::Always;
        UiSettingsStoragePolicy storage = UiSettingsStoragePolicy::ContextOnly;
        UiSettingsSnapshotReadPolicy snapshotRead =
            UiSettingsSnapshotReadPolicy::AvailableValue;
        UiSettingsFactoryResetPolicy factoryReset =
            UiSettingsFactoryResetPolicy::Preserve;
        UiSettingsMutationEffect effects = UiSettingsMutationEffect::None;
        std::uint16_t bindingIndex = 0xffffu;

        [[nodiscard]] constexpr bool Supports(UiSettingsCommandVerb verb) const
        {
            return (supportedVerbs & static_cast<std::uint8_t>(verb)) != 0u;
        }
    };

    using Kind = UiSettingsCommandKind;
    using Section = UiSettingsCommandSection;
    using UiSettingsCatalog = std::array<UiSettingsCommandDefinition,
        AllSettingIds.size() + AllActionIds.size()>;
    extern const UiSettingsCatalog UiSettingsCommandCatalog;

    [[nodiscard]] bool ValidateCanonicalSettingsSchema(
        const UiSettingsCatalog& definitions = UiSettingsCommandCatalog) noexcept;
    [[nodiscard]] std::string FormatUiSettingsMetadataFloat(float value);
    [[nodiscard]] std::string_view UiSettingsDefaultPolicyName(UiSettingsDefaultPolicy policy) noexcept;
    [[nodiscard]] std::string FormatUiSettingsDomain(const UiSettingsCommandDefinition& definition);
    [[nodiscard]] std::string FormatUiSettingsTokenLabel(SettingId id, std::size_t tokenIndex);
    [[nodiscard]] std::string FormatUiSettingsDefaultAnchor(const UiSettingsCommandDefinition& definition);
    [[nodiscard]] std::string FormatUiSettingsDefault(const UiSettingsCommandDefinition& definition);
}
