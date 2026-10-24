#include "settings_snapshot_selectors.h"
#include "settings_snapshot_internal.h"

#include <charconv>
#include <cerrno>
#include <cctype>
#include <limits>
#include <utility>

namespace uvsr
{
    namespace
    {
        bool Fail(SettingsSnapshotError& error, const char* message) noexcept
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, message, {}};
            return false;
        }
        void EmptySelector(UiSettingsValue& output) noexcept
        {
            SettingsSnapshotError unused;
            (void)output.SetSelector({}, unused);
        }
        bool IsDecimalToken(std::string_view text) noexcept
        {
            if (text.empty()) return false;
            for (const unsigned char c : text) if (c < '0' || c > '9') return false;
            return true;
        }
        template<class Unsigned> bool ParseUnsigned(std::string_view text, Unsigned& value) noexcept
        {
            if (text.empty()) return false;
            Unsigned parsed = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
            value = parsed;
            return true;
        }
        unsigned NextNormalized(std::string_view text, size_t& index, bool collapse) noexcept
        {
            while (index < text.size())
            {
                const unsigned char c = static_cast<unsigned char>(text[index++]);
                if (collapse && (c == ' ' || c == '-' || c == '_' || c == '+')) continue;
                return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
            }
            return 256;
        }
        bool ValidOption(const SettingsSnapshotAdapterOption& option) noexcept
        {
            return settings_snapshot_detail::ValidText(option.name);
        }
        bool ValidOption(const SettingsSnapshotSceneOption& option) noexcept
        {
            return settings_snapshot_detail::ValidText(option.fileName) && settings_snapshot_detail::ValidText(option.displayName);
        }
        bool ValidOption(const SettingsSnapshotLightOption& option) noexcept
        {
            return settings_snapshot_detail::ValidText(option.identity);
        }
        bool ValidOption(const SettingsSnapshotMaterialOption& option) noexcept
        {
            return !option.selectable || settings_snapshot_detail::ValidText(option.name);
        }
        template<class Option> bool ReadOption(const SettingsSnapshotOptionSource<Option>& source,
            size_t index, Option& output, SettingsSnapshotError& error) noexcept
        {
            output = {};
            if (!source.read) return Fail(error, "selector source has no reader");
            SettingsSnapshotError failure;
            if (!source.read(source.context, index, output, failure))
            {
                if (failure.code == SettingsSnapshotErrorCode::None)
                    return Fail(error, "selector source could not read an option");
                error = std::move(failure);
                return false;
            }
            return ValidOption(output) || Fail(error, "selector source returned an invalid text range");
        }
        template<class Integer> bool FormatNumber(Integer value, UiSettingsValue& output,
            SettingsSnapshotError& error) noexcept
        {
            char buffer[std::numeric_limits<Integer>::digits10 + 3];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            if (result.ec != std::errc{})
            {
                error = {SettingsSnapshotErrorCode::Format, 0, 0, "cannot format selector index", {}};
                return false;
            }
            return output.SetSelector({buffer, static_cast<size_t>(result.ptr - buffer)}, error);
        }
    }

    bool EqualNormalizedCommandAscii(std::string_view left, std::string_view right, bool collapseSeparators) noexcept
    {
        if (!settings_snapshot_detail::ValidText(left) || !settings_snapshot_detail::ValidText(right)) return false;
        size_t a = 0, b = 0;
        for (;;)
        {
            const unsigned x = NextNormalized(left, a, collapseSeparators);
            const unsigned y = NextNormalized(right, b, collapseSeparators);
            if (x != y) return false;
            if (x == 256) return true;
        }
    }

    bool ContainsNormalizedCommandAscii(std::string_view text, std::string_view part, bool collapseSeparators) noexcept
    {
        if (!settings_snapshot_detail::ValidText(text) || !settings_snapshot_detail::ValidText(part)) return false;
        size_t partStart = 0;
        const unsigned first = NextNormalized(part, partStart, collapseSeparators);
        if (first == 256) return true;
        size_t scan = 0;
        for (;;)
        {
            const unsigned next = NextNormalized(text, scan, collapseSeparators);
            if (next == 256) return false;
            if (next != first) continue;
            size_t a = scan, b = partStart;
            for (;;)
            {
                const unsigned expected = NextNormalized(part, b, collapseSeparators);
                if (expected == 256) return true;
                if (NextNormalized(text, a, collapseSeparators) != expected) break;
            }
        }
    }

    bool TryParseCommandInteger(std::string_view value, int64_t& parsed) noexcept
    {
        if (value.empty() || !settings_snapshot_detail::ValidText(value)) return false;
        // strtoll accepted locale whitespace and a leading plus. its empty-input
        // errno behavior and full-consumption rule remain part of this parser.
        errno = 0;
        size_t offset = 0;
        while (offset < value.size() && std::isspace(static_cast<unsigned char>(value[offset]))) ++offset;
        if (offset == value.size()) return false;
        if (value[offset] == '+')
        {
            ++offset;
            if (offset == value.size() || value[offset] < '0' || value[offset] > '9') return false;
        }
        int64_t candidate = 0;
        const auto result = std::from_chars(value.data() + offset, value.data() + value.size(), candidate, 10);
        if (result.ec == std::errc::result_out_of_range) errno = ERANGE;
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return false;
        parsed = candidate;
        return true;
    }

    bool FormatSettingsSnapshotAdapterToken(int64_t index, UiSettingsValue& output, SettingsSnapshotError& error) noexcept
    {
        error = {};
        if (index < 0 || index > (std::numeric_limits<int>::max)())
        {
            EmptySelector(output);
            return Fail(error, "adapter index is outside the supported range");
        }
        return FormatNumber(index, output, error);
    }
    bool FormatSettingsSnapshotSceneToken(std::string_view fileName, UiSettingsValue& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        if (!ValidateSettingsSnapshotSelectorToken(SettingId::SceneCurrent, fileName, error))
        {
            EmptySelector(output);
            return false;
        }
        return output.SetSelector(fileName, error);
    }
    bool FormatSettingsSnapshotLightToken(size_t index, std::string_view identity, UiSettingsValue& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        char number[std::numeric_limits<size_t>::digits10 + 2];
        const auto result = std::to_chars(number, number + sizeof(number), index);
        if (result.ec != std::errc{})
        {
            error = {SettingsSnapshotErrorCode::Format, 0, 0, "cannot format selector index", {}};
            return false;
        }
        UiSettingsValue candidate;
        if (!candidate.SetSelectorParts({{number, static_cast<size_t>(result.ptr - number)}, ":", identity}, error)) return false;
        if (!ValidateSettingsSnapshotSelectorToken(SettingId::LightSelected, candidate.Text(), error))
        {
            EmptySelector(output);
            return false;
        }
        output = std::move(candidate);
        return true;
    }
    bool FormatSettingsSnapshotMaterialToken(bool none, uint32_t id, UiSettingsValue& output, SettingsSnapshotError& error) noexcept
    {
        error = {};
        return none ? output.SetSelector("none", error) : FormatNumber(id, output, error);
    }

    bool ResolveSettingsSnapshotAdapterToken(std::string_view requested,
        const SettingsSnapshotOptionSource<SettingsSnapshotAdapterOption>& options,
        int64_t& index, UiSettingsValue& canonicalToken, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(requested)) return Fail(error, "invalid selector request range");
        uint64_t numeric = 0;
        const bool numericRequest = ParseUnsigned(requested, numeric) && numeric <= uint64_t((std::numeric_limits<int>::max)());
        if (IsDecimalToken(requested) && !numericRequest) return Fail(error, "adapter index is outside the supported range");
        bool found = false;
        SettingsSnapshotAdapterOption match;
        for (size_t i = 0; i < options.count; ++i)
        {
            SettingsSnapshotAdapterOption option;
            if (!ReadOption(options, i, option, error)) return false;
            if (!(numericRequest ? option.index == static_cast<int64_t>(numeric) : EqualNormalizedCommandAscii(option.name, requested, true))) continue;
            if (found && match.index != option.index) return Fail(error, "adapter name is ambiguous; use its numeric index");
            match = option;
            found = true;
        }
        if (!found) return Fail(error, "unknown adapter selection");
        UiSettingsValue candidate;
        if (!FormatSettingsSnapshotAdapterToken(match.index, candidate, error))
        {
            if (error.code == SettingsSnapshotErrorCode::InvalidInput)
            {
                index = match.index;
                canonicalToken = std::move(candidate);
                error = {};
            }
            return false;
        }
        index = match.index;
        canonicalToken = std::move(candidate);
        return true;
    }
    bool ResolveSettingsSnapshotSceneToken(std::string_view requested,
        const SettingsSnapshotOptionSource<SettingsSnapshotSceneOption>& options,
        size_t& ordinal, UiSettingsValue& canonicalToken, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(requested)) return Fail(error, "invalid selector request range");
        bool found = false;
        size_t selected = 0;
        SettingsSnapshotSceneOption match;
        for (size_t i = 0; i < options.count; ++i)
        {
            SettingsSnapshotSceneOption option;
            if (!ReadOption(options, i, option, error)) return false;
            if (option.fileName != requested) continue;
            match = option;
            selected = i;
            found = true;
            break;
        }
        if (!found)
        {
            for (size_t i = 0; i < options.count; ++i)
            {
                SettingsSnapshotSceneOption option;
                if (!ReadOption(options, i, option, error)) return false;
                if (!EqualNormalizedCommandAscii(option.fileName, requested, true) &&
                    !EqualNormalizedCommandAscii(option.displayName, requested, true)) continue;
                if (found && match.fileName != option.fileName) return Fail(error, "scene name is ambiguous; use its exact filename");
                match = option;
                selected = i;
                found = true;
            }
        }
        if (!found) return Fail(error, "unknown scene selection");
        UiSettingsValue candidate;
        if (!FormatSettingsSnapshotSceneToken(match.fileName, candidate, error))
        {
            if (error.code == SettingsSnapshotErrorCode::InvalidInput)
            {
                canonicalToken = std::move(candidate);
                return Fail(error, "scene catalog contains a noncanonical filename");
            }
            return false;
        }
        ordinal = selected;
        canonicalToken = std::move(candidate);
        return true;
    }
    bool ResolveSettingsSnapshotLightToken(std::string_view requested,
        const SettingsSnapshotOptionSource<SettingsSnapshotLightOption>& options,
        size_t& index, UiSettingsValue& canonicalToken, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(requested)) return Fail(error, "invalid selector request range");
        size_t numeric = 0;
        bool exactIdentity = false;
        bool numericRequest = false;
        std::string_view requestedIdentity;
        const size_t separator = requested.find(':');
        if (separator != std::string_view::npos)
        {
            exactIdentity = ParseUnsigned(requested.substr(0, separator), numeric);
            numericRequest = exactIdentity;
            requestedIdentity = requested.substr(separator + 1);
            if (!exactIdentity || requestedIdentity.empty()) return Fail(error, "invalid light selector token");
        }
        else
        {
            numericRequest = ParseUnsigned(requested, numeric);
            if (IsDecimalToken(requested) && !numericRequest) return Fail(error, "light index is outside the supported range");
        }
        bool found = false;
        SettingsSnapshotLightOption match;
        for (size_t i = 0; i < options.count; ++i)
        {
            SettingsSnapshotLightOption option;
            if (!ReadOption(options, i, option, error)) return false;
            const bool matches = exactIdentity ? option.index == numeric && option.identity == requestedIdentity
                : numericRequest ? option.index == numeric : EqualNormalizedCommandAscii(option.identity, requested, true);
            if (!matches) continue;
            if (found && match.index != option.index) return Fail(error, "light name is ambiguous; use its index:identity token");
            match = option;
            found = true;
        }
        if (!found) return Fail(error, "unknown light selection");
        UiSettingsValue candidate;
        if (!FormatSettingsSnapshotLightToken(match.index, match.identity, candidate, error))
        {
            if (error.code == SettingsSnapshotErrorCode::InvalidInput)
            {
                index = match.index;
                canonicalToken = std::move(candidate);
                return Fail(error, "light table contains a noncanonical identity");
            }
            return false;
        }
        index = match.index;
        canonicalToken = std::move(candidate);
        return true;
    }
    bool ResolveSettingsSnapshotMaterialToken(std::string_view requested,
        const SettingsSnapshotOptionSource<SettingsSnapshotMaterialOption>& options,
        bool& none, uint32_t& id, UiSettingsValue& canonicalToken, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(requested)) return Fail(error, "invalid selector request range");
        if (EqualNormalizedCommandAscii(requested, "none", true))
        {
            UiSettingsValue candidate;
            if (!FormatSettingsSnapshotMaterialToken(true, 0, candidate, error)) return false;
            none = true;
            id = 0;
            canonicalToken = std::move(candidate);
            return true;
        }
        uint64_t numeric = 0;
        const bool numericRequest = ParseUnsigned(requested, numeric) && numeric <= (std::numeric_limits<uint32_t>::max)();
        if (IsDecimalToken(requested) && !numericRequest) return Fail(error, "material id is outside the supported range");
        bool found = false;
        SettingsSnapshotMaterialOption match;
        for (size_t i = 0; i < options.count; ++i)
        {
            SettingsSnapshotMaterialOption option;
            if (!ReadOption(options, i, option, error)) return false;
            if (!option.selectable) continue;
            if (!(numericRequest ? option.id == static_cast<uint32_t>(numeric) : EqualNormalizedCommandAscii(option.name, requested, true))) continue;
            if (found && match.id != option.id) return Fail(error, "material name is ambiguous; use its runtime id");
            match = option;
            found = true;
        }
        if (!found) return Fail(error, "unknown material selection");
        UiSettingsValue candidate;
        if (!FormatSettingsSnapshotMaterialToken(false, match.id, candidate, error)) return false;
        none = false;
        id = match.id;
        canonicalToken = std::move(candidate);
        return true;
    }
}
