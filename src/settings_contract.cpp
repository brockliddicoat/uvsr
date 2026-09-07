#include "settings_snapshot_schema.h"
#include "settings_snapshot.h"
#include "engine_identity.h"
#include "json_document.h"

#include <algorithm>
#include <vector>

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
    }

    std::string BuildSettingsContractJson()
    {
        std::vector<const UiSettingsCommandDefinition*> values;
        values.reserve(UiSettingsCommandCatalog.size());
        for (const UiSettingsCommandDefinition& definition :
            UiSettingsCommandCatalog)
        {
            if (definition.kind != UiSettingsCommandKind::Action)
                values.push_back(&definition);
        }
        std::sort(
            values.begin(),
            values.end(),
            [](const UiSettingsCommandDefinition* left,
               const UiSettingsCommandDefinition* right)
            {
                return left->name < right->name;
            });

        std::string result =
            "{\"schemaVersion\":" +
            std::to_string(SettingsSnapshotVersion) +
            ",\"settingsHash\":\"" +
            json::Escape(GetSettingsNumberHashText()) +
            "\",\"engineVersion\":\"" +
            json::Escape(FormatEngineVersion(CurrentEngineVersion)) +
            "\",\"serializationPolicy\":\"" +
            json::Escape(SettingsSnapshotSerializationPolicy) +
            "\",\"entries\":[";
        for (size_t index = 0u; index < values.size(); ++index)
        {
            const UiSettingsCommandDefinition& definition =
                *values[index];
            if (index != 0u)
                result.push_back(',');
            result += "{\"name\":\"" + json::Escape(definition.name) +
                "\",\"kind\":\"" + GetKindName(definition.kind) +
                "\",\"persistence\":\"" +
                GetPersistenceName(definition.persistence) +
                "\",\"snapshotMember\":" +
                (IsSettingsSnapshotValue(definition) ? "true" : "false") +
                ",\"defaultValue\":\"" +
                json::Escape(FormatUiSettingsDefault(definition)) +
                "\",\"domain\":\"" +
                json::Escape(FormatUiSettingsDomain(definition)) +
                "\"}";
        }
        result += "]}\n";
        return result;
    }

}
