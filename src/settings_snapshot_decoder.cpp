#include "settings_snapshot_decoder.h"
#include "settings_snapshot_internal.h"

namespace uvsr
{
    namespace
    {
        bool Fail(SettingsSnapshotError& error, SettingsSnapshotErrorCode code,
            const char* message) noexcept
        {
            error = {};
            error.code = code;
            error.message = message;
            return false;
        }

        bool Publish(json::EncodedText&& text, json::EncodedText& output,
            SettingsSnapshotError& error) noexcept
        {
            if (!text.IsValid())
            {
                const auto code = text.Failure().code;
                return Fail(error, code == json::ErrorCode::OutOfMemory
                    ? SettingsSnapshotErrorCode::OutOfMemory
                    : code == json::ErrorCode::Capacity ? SettingsSnapshotErrorCode::Capacity
                    : SettingsSnapshotErrorCode::Format, text.Failure().message);
            }
            output = static_cast<json::EncodedText&&>(text);
            return true;
        }

        bool EmitUnescaped(json::OutputWriter& output, const void* context) noexcept
        {
            const auto value = *static_cast<const std::string_view*>(context);
            size_t start = 0;
            for (size_t index = 0; index < value.size(); ++index)
            {
                if (value[index] != '\\') continue;
                if (!output.Raw({value.data() + start, index - start})) return false;
                if (++index == value.size())
                    return output.Reject(json::ErrorCode::InvalidInput, "snapshot value has a trailing escape");
                char character = 0;
                switch (value[index])
                {
                case '\\': character = '\\'; break;
                case 'n': character = '\n'; break;
                case 'r': character = '\r'; break;
                case 't': character = '\t'; break;
                default:
                    return output.Reject(json::ErrorCode::InvalidInput,
                        "snapshot value contains an unknown escape");
                }
                if (!output.Raw({&character, 1})) return false;
                start = index + 1;
            }
            return output.Raw({start ? value.data() + start : value.data(), value.size() - start});
        }

        bool EmitEscaped(json::OutputWriter& output, std::string_view value) noexcept
        {
            size_t start = 0;
            for (size_t index = 0; index < value.size(); ++index)
            {
                const char* escape = nullptr;
                switch (value[index])
                {
                case '\\': escape = "\\\\"; break;
                case '\n': escape = "\\n"; break;
                case '\r': escape = "\\r"; break;
                case '\t': escape = "\\t"; break;
                default: continue;
                }
                if (!output.Raw({value.data() + start, index - start}) ||
                    !output.Raw({escape, 2})) return false;
                start = index + 1;
            }
            return output.Raw({start ? value.data() + start : value.data(), value.size() - start});
        }

        bool EmitCanonical(json::OutputWriter& output, const void* context) noexcept
        {
            const auto& settings = *static_cast<const DecodedSettings*>(context);
            for (size_t index = 0; index < settings.Count(); ++index)
            {
                const auto& setting = settings.Entries()[index];
                if (!output.Raw({setting.name.data(), setting.name.size()}) ||
                    !output.Raw("=") || !EmitEscaped(output, setting.value) ||
                    !output.Raw("\n")) return false;
            }
            return true;
        }

        bool EmitJson(json::OutputWriter& output, const void* context) noexcept
        {
            const auto& settings = *static_cast<const DecodedSettings*>(context);
            if (!output.Raw("{\n")) return false;
            for (size_t index = 0; index < settings.Count(); ++index)
            {
                const auto& setting = settings.Entries()[index];
                if (!output.Raw("  ") || !output.String({setting.name.data(), setting.name.size()}) ||
                    !output.Raw(": ") || !output.String({setting.value.data(), setting.value.size()}) ||
                    (index + 1 < settings.Count() && !output.Raw(",")) ||
                    !output.Raw("\n")) return false;
            }
            return output.Raw("}\n");
        }
    }

    bool UnescapeSettingsSnapshotValue(std::string_view value,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(value))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot text range");
        json::EncodedText candidate(EmitUnescaped, &value);
        if (!candidate.IsValid() && candidate.Failure().code == json::ErrorCode::InvalidInput)
            return Fail(error, SettingsSnapshotErrorCode::Escape, candidate.Failure().message);
        return Publish(static_cast<json::EncodedText&&>(candidate), output, error);
    }

    bool ParseSettingsSnapshot(std::string_view canonicalSettings,
        DecodedSettings& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(canonicalSettings))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot text range");
        DecodedSettings candidate;
        size_t offset = 0;
        while (offset < canonicalSettings.size())
        {
            const size_t newline = canonicalSettings.find('\n', offset);
            const size_t end = newline == std::string_view::npos ? canonicalSettings.size() : newline;
            const auto line = canonicalSettings.substr(offset, end - offset);
            if (line.empty())
                return Fail(error, SettingsSnapshotErrorCode::InvalidInput,
                    "snapshot contains an empty setting line");
            const size_t separator = line.find('=');
            if (separator == std::string_view::npos || !separator)
                return Fail(error, SettingsSnapshotErrorCode::InvalidInput,
                    "snapshot contains an invalid setting line");
            // validate the value before insertion to retain duplicate/escape error order.
            json::EncodedText value;
            if (!UnescapeSettingsSnapshotValue(line.substr(separator + 1), value, error) ||
                !candidate.Insert(line.substr(0, separator), {value.Data(), value.Size()}, error)) return false;
            if (newline == std::string_view::npos) break;
            offset = newline + 1;
        }
        output = static_cast<DecodedSettings&&>(candidate);
        return true;
    }

    bool FormatCanonicalSettingsSnapshot(const DecodedSettings& settings,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept
    {
        error = {};
        return Publish(json::EncodedText(EmitCanonical, &settings), output, error);
    }

    bool FormatDecodedSettingsJson(const DecodedSettings& settings,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept
    {
        error = {};
        return Publish(json::EncodedText(EmitJson, &settings), output, error);
    }

    bool FormatSettingsSnapshotCatalogSection(std::string_view code, std::string_view canonical,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(code) || !settings_snapshot_detail::ValidText(canonical))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot text range");
        struct SectionText { std::string_view code; std::string_view canonical; } text{code, canonical};
        return Publish(json::EncodedText([](json::OutputWriter& writer, const void* context) noexcept {
            const auto& section = *static_cast<const SectionText*>(context);
            return writer.Raw("[") && writer.Raw({section.code.data(), section.code.size()}) && writer.Raw("]\n") &&
                writer.Raw({section.canonical.data(), section.canonical.size()}) && writer.Raw("[/") &&
                writer.Raw({section.code.data(), section.code.size()}) && writer.Raw("]\n");
        }, &text), output, error);
    }
}
