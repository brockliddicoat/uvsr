#include "settings_snapshot_schema.h"
#include "settings_snapshot.h"
#include "engine_identity.h"

namespace uvsr
{
    namespace
    {
        [[nodiscard]] const char* GetKindName(
            UiSettingsCommandKind kind) noexcept
        {
            switch (kind)
            {
            case UiSettingsCommandKind::Boolean: return "Boolean";
            case UiSettingsCommandKind::Integer: return "Integer";
            case UiSettingsCommandKind::Float: return "Float";
            case UiSettingsCommandKind::Float3: return "Float3";
            case UiSettingsCommandKind::Enum: return "Enum";
            case UiSettingsCommandKind::DynamicSelection:
                return "DynamicSelection";
            case UiSettingsCommandKind::Action: return "Action";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* GetPersistenceName(
            UiSettingsPersistence persistence) noexcept
        {
            switch (persistence)
            {
            case UiSettingsPersistence::SnapshotCatalog:
                return "SnapshotCatalog";
            case UiSettingsPersistence::SessionOnly:
                return "SessionOnly";
            case UiSettingsPersistence::None:
                return "None";
            }
            return "Unknown";
        }

        json::TextView Text(std::string_view value) noexcept { return {value.data(), value.size()}; }
        struct ContractContext
        {
            const UiSettingsCommandDefinition* values[AllSettingIds.size() + AllActionIds.size()]{};
            size_t count = 0;
        };

        bool EmitContract(json::OutputWriter& output, const void* context) noexcept
        {
            const auto& contract = *static_cast<const ContractContext*>(context);
            const auto version = CurrentEngineVersion;
            if (!(output.Raw("{\"schemaVersion\":") && output.Unsigned(SettingsSnapshotVersion) &&
                output.Raw(",\"settingsHash\":") && output.String(Text(GetSettingsNumberHashText())) &&
                output.Raw(",\"engineVersion\":\"") && output.Unsigned(version.major) && output.Raw(".") &&
                output.Unsigned(version.minor) && output.Raw(".") && output.Unsigned(version.patch) && output.Raw(".") &&
                output.Unsigned(version.build) && output.Raw("\",\"serializationPolicy\":") &&
                output.String(Text(SettingsSnapshotSerializationPolicy)) && output.Raw(",\"entries\":["))) return false;
            for (size_t index = 0; index < contract.count; ++index)
            {
                const auto& definition = *contract.values[index];
                const auto defaultValue = FormatUiSettingsDefault(definition);
                const auto domain = FormatUiSettingsDomain(definition);
                if (!defaultValue.IsValid() || !domain.IsValid())
                    return output.Reject(json::ErrorCode::Capacity, "settings metadata exceeds its output capacity");
                if (index && !output.Raw(",")) return false;
                if (!(output.Raw("{\"name\":") && output.String(Text(definition.name)) &&
                    output.Raw(",\"kind\":") && output.String(GetKindName(definition.kind)) &&
                    output.Raw(",\"persistence\":") && output.String(GetPersistenceName(definition.persistence)) &&
                    output.Raw(",\"snapshotMember\":") && output.Boolean(IsSettingsSnapshotValue(definition)) &&
                    output.Raw(",\"defaultValue\":") && output.String({defaultValue.Data(), defaultValue.Size()}) &&
                    output.Raw(",\"domain\":") && output.String({domain.Data(), domain.Size()}) && output.Raw("}"))) return false;
            }
            return output.Raw("]}\n");
        }
    }

    json::EncodedText BuildSettingsContractJson() noexcept
    {
        ContractContext context;
        for (const auto& definition : UiSettingsCommandCatalog)
            if (definition.kind != UiSettingsCommandKind::Action) context.values[context.count++] = &definition;
        // the fixed catalog is small; insertion sorting needs no heap or recursion.
        for (size_t index = 1; index < context.count; ++index)
        {
            const auto* value = context.values[index];
            size_t position = index;
            while (position && value->name < context.values[position - 1]->name)
            {
                context.values[position] = context.values[position - 1];
                --position;
            }
            context.values[position] = value;
        }
        return json::EncodedText(EmitContract, &context);
    }
}
