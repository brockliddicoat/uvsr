#include "engine_diagnostics.h"

#include "build_identity.h"
#include "settings_snapshot.h"
#include "settings_snapshot_schema.h"
#include "ui_settings_command_catalog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr
{
    namespace
    {
        [[nodiscard]] std::string EscapeJson(std::string_view value)
        {
            constexpr char HexDigits[] = "0123456789abcdef";
            std::string escaped;
            escaped.reserve(value.size());
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\b': escaped += "\\b"; break;
                case '\f': escaped += "\\f"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default:
                    if (character < 0x20u)
                    {
                        escaped += "\\u00";
                        escaped.push_back(HexDigits[character >> 4u]);
                        escaped.push_back(HexDigits[character & 0x0fu]);
                    }
                    else
                    {
                        escaped.push_back(static_cast<char>(character));
                    }
                }
            }
            return escaped;
        }

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
            case UiSettingsCommandKind::Float4: return "Float4";
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

    std::string BuildIdentityJson()
    {
        return
            "{\"executable\":\"uvsr-engine.exe\","
            "\"source_commit\":\"" +
            EscapeJson(GetBuiltSourceCommit()) +
            "\",\"source_identity\":\"" +
            EscapeJson(GetBuiltSourceIdentity()) +
            "\",\"source_tree_clean\":" +
            (IsBuiltSourceTreeClean() ? "true" : "false") +
            ",\"production\":" +
            (IsBuiltProduction() ? "true" : "false") +
            ",\"configuration\":\"" +
            EscapeJson(GetBuiltConfiguration()) +
            "\",\"settings_hash\":\"" +
            EscapeJson(GetBuiltSettingsNumberHash()) +
            "\",\"engine_version\":\"" +
            EscapeJson(GetBuiltEngineVersion()) +
            "\",\"product_version\":\"" +
            EscapeJson(GetBuiltEngineProductVersion()) + "\"}\n";
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

        std::string json =
            "{\"schemaVersion\":" +
            std::to_string(SettingsSnapshotVersion) +
            ",\"settingsHash\":\"" +
            EscapeJson(GetBuiltSettingsNumberHash()) +
            "\",\"engineVersion\":\"" +
            EscapeJson(GetBuiltEngineVersion()) +
            "\",\"serializationPolicy\":\"" +
            EscapeJson(SettingsSnapshotSerializationPolicy) +
            "\",\"entries\":[";
        for (size_t index = 0u; index < values.size(); ++index)
        {
            const UiSettingsCommandDefinition& definition =
                *values[index];
            if (index != 0u)
                json.push_back(',');
            json += "{\"name\":\"" + EscapeJson(definition.name) +
                "\",\"kind\":\"" + GetKindName(definition.kind) +
                "\",\"persistence\":\"" +
                GetPersistenceName(definition.persistence) +
                "\",\"snapshotMember\":" +
                (IsSettingsSnapshotValue(definition) ? "true" : "false") +
                ",\"defaultValue\":\"" +
                EscapeJson(definition.defaultValue) +
                "\",\"domain\":\"" + EscapeJson(definition.domain) +
                "\"}";
        }
        json += "]}\n";
        return json;
    }

    std::optional<int> TryRunEngineDiagnosticCommand(
        int argumentCount,
        const char* const* arguments)
    {
        if (argumentCount != 2)
        {
            return std::nullopt;
        }

        std::string json;
        if (std::strcmp(arguments[1], "--identity-json") == 0)
        {
            json = BuildIdentityJson();
        }
        else if (std::strcmp(
                arguments[1],
                "--settings-contract-json") == 0)
        {
            json = BuildSettingsContractJson();
        }
        else
        {
            return std::nullopt;
        }
        const std::size_t written = std::fwrite(
            json.data(),
            1u,
            json.size(),
            stdout);
        std::fflush(stdout);
        return written == json.size() ? 0 : 1;
    }
}
