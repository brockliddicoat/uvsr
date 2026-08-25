#include "uvsr_settings_commands.h"

#include "renderer_log.h"
#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>

namespace uvsr
{
    namespace
    {
        thread_local bool g_PreciseFloatFormatting = false;

        [[nodiscard]] bool TryParseUnsignedToken(
            std::string_view token,
            std::uint64_t& value)
        {
            if (token.empty())
                return false;
            std::uint64_t parsed = 0u;
            const auto result = std::from_chars(
                token.data(), token.data() + token.size(), parsed, 10);
            if (result.ec != std::errc{} ||
                result.ptr != token.data() + token.size())
            {
                return false;
            }
            value = parsed;
            return true;
        }

        [[nodiscard]] bool IsDecimalToken(std::string_view token)
        {
            return !token.empty() && std::all_of(
                token.begin(), token.end(), [](unsigned char character)
                {
                    return character >= static_cast<unsigned char>('0') &&
                        character <= static_cast<unsigned char>('9');
                });
        }

        [[nodiscard]] const char* TransactionStageName(
            SettingsSnapshotTransactionFailureStage stage)
        {
            switch (stage)
            {
            case SettingsSnapshotTransactionFailureStage::None:
                return "unknown";
            case SettingsSnapshotTransactionFailureStage::Configuration:
                return "configuration";
            case SettingsSnapshotTransactionFailureStage::Preflight:
                return "preflight";
            case SettingsSnapshotTransactionFailureStage::Capture:
                return "pre-state capture";
            case SettingsSnapshotTransactionFailureStage::Selector:
                return "selector transition";
            case SettingsSnapshotTransactionFailureStage::Apply:
                return "apply";
            case SettingsSnapshotTransactionFailureStage::Readback:
                return "readback";
            case SettingsSnapshotTransactionFailureStage::Rollback:
                return "rollback";
            }
            return "unknown";
        }

        [[nodiscard]] std::vector<SettingsSnapshotCatalogEntry>
            BuildAuthoritativeSnapshotCatalog()
        {
            std::vector<SettingsSnapshotCatalogEntry> catalog;
            catalog.reserve(UiSettingsCommandCatalog.size());
            for (const UiSettingsCommandDefinition& definition :
                UiSettingsCommandCatalog)
            {
                if (!IsSettingsSnapshotValue(definition))
                    continue;
                catalog.push_back({
                    std::string(definition.name),
                    ResolveSettingsSnapshotApplicationMode(definition)
                });
            }
            return catalog;
        }

        constexpr std::array<std::uint8_t, 8> RestartHandoffMagic = {
            'U', 'V', 'S', 'R', 'S', 'H', '0', '1'
        };
        constexpr std::uintmax_t MaximumRestartHandoffBytes = 4u * 1024u * 1024u;

        void AppendU32(
            std::vector<std::uint8_t>& bytes,
            std::uint32_t value)
        {
            for (unsigned int shift = 0u; shift < 32u; shift += 8u)
            {
                bytes.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        }

        void AppendU64(
            std::vector<std::uint8_t>& bytes,
            std::uint64_t value)
        {
            for (unsigned int shift = 0u; shift < 64u; shift += 8u)
            {
                bytes.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        }

        [[nodiscard]] bool AppendString(
            std::vector<std::uint8_t>& bytes,
            std::string_view value)
        {
            if (value.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)()))
            {
                return false;
            }
            AppendU32(bytes, static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
            return true;
        }

        [[nodiscard]] bool ReadU32(
            const std::vector<std::uint8_t>& bytes,
            std::size_t limit,
            std::size_t& cursor,
            std::uint32_t& value)
        {
            if (cursor > limit || limit - cursor < 4u)
                return false;
            value = 0u;
            for (unsigned int shift = 0u; shift < 32u; shift += 8u)
            {
                value |= static_cast<std::uint32_t>(bytes[cursor++]) << shift;
            }
            return true;
        }

        [[nodiscard]] bool ReadU64(
            const std::vector<std::uint8_t>& bytes,
            std::size_t limit,
            std::size_t& cursor,
            std::uint64_t& value)
        {
            if (cursor > limit || limit - cursor < 8u)
                return false;
            value = 0u;
            for (unsigned int shift = 0u; shift < 64u; shift += 8u)
            {
                value |= static_cast<std::uint64_t>(bytes[cursor++]) << shift;
            }
            return true;
        }

        [[nodiscard]] bool ReadString(
            const std::vector<std::uint8_t>& bytes,
            std::size_t limit,
            std::size_t& cursor,
            std::string& value)
        {
            std::uint32_t length = 0u;
            if (!ReadU32(bytes, limit, cursor, length) ||
                cursor > limit || limit - cursor < length)
            {
                return false;
            }
            value.assign(
                reinterpret_cast<const char*>(bytes.data() + cursor),
                static_cast<std::size_t>(length));
            cursor += length;
            return true;
        }

        [[nodiscard]] std::uint32_t RestartHandoffCrc32(
            const std::vector<std::uint8_t>& bytes,
            std::size_t count)
        {
            std::uint32_t crc = 0xffffffffu;
            for (std::size_t index = 0u; index < count; ++index)
            {
                crc ^= bytes[index];
                for (unsigned int bit = 0u; bit < 8u; ++bit)
                {
                    const std::uint32_t mask =
                        0u - static_cast<std::uint32_t>(crc & 1u);
                    crc = (crc >> 1u) ^ (0xedb88320u & mask);
                }
            }
            return ~crc;
        }

        [[nodiscard]] bool WriteBytesAtomically(
            const std::filesystem::path& path,
            const std::vector<std::uint8_t>& bytes,
            std::string& error)
        {
            error.clear();
            if (path.empty())
            {
                error = "destination path is empty";
                return false;
            }
            std::error_code directoryError;
            std::filesystem::create_directories(
                path.parent_path(), directoryError);
            if (directoryError)
            {
                error = "could not create destination directory: " +
                    directoryError.message();
                return false;
            }

            std::filesystem::path temporary = path;
            temporary += L".tmp";
            DeleteFileW(temporary.c_str());
            HANDLE file = CreateFileW(
                temporary.c_str(),
                GENERIC_WRITE,
                0u,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                error = "could not create same-directory temporary file "
                    "(Win32 error " + std::to_string(GetLastError()) + ")";
                return false;
            }

            bool wrote = true;
            std::size_t offset = 0u;
            while (offset < bytes.size())
            {
                const std::size_t remaining = bytes.size() - offset;
                const DWORD requested = static_cast<DWORD>((std::min)(
                    remaining,
                    static_cast<std::size_t>(
                        (std::numeric_limits<DWORD>::max)())));
                DWORD written = 0u;
                if (!WriteFile(
                        file,
                        bytes.data() + offset,
                        requested,
                        &written,
                        nullptr) ||
                    written != requested)
                {
                    wrote = false;
                    error = "could not write temporary file (Win32 error " +
                        std::to_string(GetLastError()) + ")";
                    break;
                }
                offset += written;
            }
            if (wrote && !FlushFileBuffers(file))
            {
                wrote = false;
                error = "could not flush temporary file (Win32 error " +
                    std::to_string(GetLastError()) + ")";
            }
            if (!CloseHandle(file) && wrote)
            {
                wrote = false;
                error = "could not close temporary file (Win32 error " +
                    std::to_string(GetLastError()) + ")";
            }
            if (!wrote)
            {
                DeleteFileW(temporary.c_str());
                return false;
            }
            if (!MoveFileExW(
                    temporary.c_str(),
                    path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                error = "could not atomically publish file (Win32 error " +
                    std::to_string(GetLastError()) + ")";
                DeleteFileW(temporary.c_str());
                return false;
            }
            return true;
        }

        [[nodiscard]] bool WriteTextAtomically(
            const std::filesystem::path& path,
            std::string_view text,
            std::string& error)
        {
            const std::vector<std::uint8_t> bytes(text.begin(), text.end());
            return WriteBytesAtomically(path, bytes, error);
        }
    }

    SettingsCommandFloatPrecisionScope::
        SettingsCommandFloatPrecisionScope()
        : m_Previous(g_PreciseFloatFormatting)
    {
        g_PreciseFloatFormatting = true;
    }

    SettingsCommandFloatPrecisionScope::~SettingsCommandFloatPrecisionScope()
    {
        g_PreciseFloatFormatting = m_Previous;
    }

    std::string NormalizeCommandAscii(
        std::string_view value,
        bool collapseSeparators)
    {
        std::string normalized;
        normalized.reserve(value.size());
        for (const unsigned char character : value)
        {
            if (character >= static_cast<unsigned char>('A') &&
                character <= static_cast<unsigned char>('Z'))
            {
                normalized.push_back(static_cast<char>(
                    character - static_cast<unsigned char>('A') +
                    static_cast<unsigned char>('a')));
            }
            else if (collapseSeparators &&
                (character == static_cast<unsigned char>(' ') ||
                 character == static_cast<unsigned char>('-') ||
                 character == static_cast<unsigned char>('_') ||
                 character == static_cast<unsigned char>('+')))
            {
                continue;
            }
            else
            {
                normalized.push_back(static_cast<char>(character));
            }
        }
        return normalized;
    }

    bool StartsWithCommandPrefix(
        std::string_view value,
        std::string_view prefix)
    {
        return value.size() >= prefix.size() &&
            value.compare(0u, prefix.size(), prefix) == 0;
    }

    std::string JoinCommandArguments(
        const std::vector<std::string>& arguments)
    {
        std::string result;
        for (const std::string& argument : arguments)
        {
            if (!result.empty())
                result.push_back(' ');
            result += argument;
        }
        return result;
    }

    bool TryParseCommandBool(std::string_view value, bool& parsed)
    {
        const std::string normalized = NormalizeCommandAscii(value, true);
        if (normalized == "on" || normalized == "true" ||
            normalized == "yes" || normalized == "show" ||
            normalized == "shown" || normalized == "enabled" ||
            normalized == "1")
        {
            parsed = true;
            return true;
        }
        if (normalized == "off" || normalized == "false" ||
            normalized == "no" || normalized == "hide" ||
            normalized == "hidden" || normalized == "disabled" ||
            normalized == "0")
        {
            parsed = false;
            return true;
        }
        return false;
    }

    bool TryParseCommandFloat(std::string_view value, float& parsed)
    {
        if (value.empty())
            return false;
        const std::string owned(value);
        char* end = nullptr;
        const float candidate = std::strtof(owned.c_str(), &end);
        if (!end || end != owned.c_str() + owned.size() ||
            !std::isfinite(candidate))
        {
            return false;
        }
        parsed = candidate;
        return true;
    }

    bool TryParseCommandInteger(
        std::string_view value,
        std::int64_t& parsed)
    {
        if (value.empty())
            return false;
        const std::string owned(value);
        char* end = nullptr;
        errno = 0;
        const long long candidate = std::strtoll(owned.c_str(), &end, 10);
        if (errno == ERANGE || !end ||
            end != owned.c_str() + owned.size())
        {
            return false;
        }
        parsed = static_cast<std::int64_t>(candidate);
        return true;
    }

    std::string FormatCommandFloat(float value)
    {
        char buffer[64];
        if (g_PreciseFloatFormatting)
        {
            const auto result = std::to_chars(
                buffer,
                buffer + std::size(buffer),
                value,
                std::chars_format::general,
                std::numeric_limits<float>::max_digits10);
            if (result.ec == std::errc{})
                return std::string(buffer, result.ptr);
        }
        std::snprintf(buffer, std::size(buffer), "%.3f", value);
        return buffer;
    }

    bool RejectUnchangedCommandMutation(
        std::string_view path,
        std::string& error)
    {
        error = "No change: " + std::string(path) +
            " already has the requested value.";
        return false;
    }

    std::string FormatCommandUiColorRgb(const UiRgbaColor& color)
    {
        return FormatCommandFloat(color.red) + " " +
            FormatCommandFloat(color.green) + " " +
            FormatCommandFloat(color.blue);
    }

    std::string FormatCommandUiColorRgba(const UiRgbaColor& color)
    {
        return FormatCommandUiColorRgb(color) + " " +
            FormatCommandFloat(color.alpha);
    }

    bool ApplyCommandUiColorRgb(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        UiRgbaColor& current,
        const UiRgbaColor& defaultValue,
        std::string& value,
        std::string& error)
    {
        UiRgbaColor candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            if (arguments.size() != 3u ||
                !TryParseCommandFloat(arguments[0], candidate.red) ||
                !TryParseCommandFloat(arguments[1], candidate.green) ||
                !TryParseCommandFloat(arguments[2], candidate.blue) ||
                candidate.red < 0.f || candidate.red > 1.f ||
                candidate.green < 0.f || candidate.green > 1.f ||
                candidate.blue < 0.f || candidate.blue > 1.f)
            {
                error = std::string(path) +
                    " expects three finite numbers from 0.000 through 1.000.";
                return false;
            }
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get &&
            candidate == current)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        value = FormatCommandUiColorRgb(current);
        return true;
    }

    bool ApplyCommandUiColorRgba(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        UiRgbaColor& current,
        const UiRgbaColor& defaultValue,
        std::string& value,
        std::string& error)
    {
        UiRgbaColor candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            if (arguments.size() != 4u ||
                !TryParseCommandFloat(arguments[0], candidate.red) ||
                !TryParseCommandFloat(arguments[1], candidate.green) ||
                !TryParseCommandFloat(arguments[2], candidate.blue) ||
                !TryParseCommandFloat(arguments[3], candidate.alpha) ||
                candidate.red < 0.f || candidate.red > 1.f ||
                candidate.green < 0.f || candidate.green > 1.f ||
                candidate.blue < 0.f || candidate.blue > 1.f ||
                candidate.alpha < 0.f || candidate.alpha > 1.f)
            {
                error = std::string(path) +
                    " expects four finite numbers from 0.000 through 1.000.";
                return false;
            }
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get &&
            candidate == current)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        value = FormatCommandUiColorRgba(current);
        return true;
    }

    const UiSettingsCommandDefinition* FindSettingsCommandDefinition(
        std::string_view rawName)
    {
        const std::string name = NormalizeCommandAscii(rawName);
        const auto definition = std::find_if(
            UiSettingsCommandCatalog.begin(),
            UiSettingsCommandCatalog.end(),
            [&name](const UiSettingsCommandDefinition& candidate)
            {
                return candidate.name == name;
            });
        return definition != UiSettingsCommandCatalog.end()
            ? &*definition
            : nullptr;
    }

    UiSettingsCommandVerb GetSettingsCommandVerb(
        CommandValueOperation operation)
    {
        switch (operation)
        {
        case CommandValueOperation::Get:
            return UiSettingsCommandVerb::Get;
        case CommandValueOperation::Set:
            return UiSettingsCommandVerb::Set;
        case CommandValueOperation::Toggle:
            return UiSettingsCommandVerb::Toggle;
        case CommandValueOperation::Reset:
            return UiSettingsCommandVerb::Reset;
        }
        return UiSettingsCommandVerb::Get;
    }

    std::string GetSettingsCommandVerbList(
        const UiSettingsCommandDefinition& definition)
    {
        std::string result;
        const auto append = [&]
        (UiSettingsCommandVerb verb, std::string_view label)
        {
            if (!definition.Supports(verb))
                return;
            if (!result.empty())
                result += "|";
            result += label;
        };
        append(UiSettingsCommandVerb::Get, "get");
        append(UiSettingsCommandVerb::Set, "set");
        append(UiSettingsCommandVerb::Toggle, "toggle");
        append(UiSettingsCommandVerb::Reset, "reset");
        append(UiSettingsCommandVerb::Run, "run");
        return result;
    }

    bool ApplyCommandBool(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        bool& current,
        bool defaultValue,
        std::string& value,
        std::string& error)
    {
        bool candidate = current;
        switch (operation)
        {
        case CommandValueOperation::Get:
            break;
        case CommandValueOperation::Set:
            if (arguments.size() != 1u ||
                !TryParseCommandBool(arguments.front(), candidate))
            {
                error = std::string(path) + " expects on or off.";
                return false;
            }
            break;
        case CommandValueOperation::Toggle:
            candidate = !candidate;
            break;
        case CommandValueOperation::Reset:
            candidate = defaultValue;
            break;
        }
        if (operation != CommandValueOperation::Get && candidate == current)
            return RejectUnchangedCommandMutation(path, error);
        current = candidate;
        value = current ? "on" : "off";
        return true;
    }

    bool ApplyCommandInteger(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        int& current,
        int defaultValue,
        int minimum,
        int maximum,
        std::string& value,
        std::string& error)
    {
        int candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            std::int64_t parsed = 0;
            if (arguments.size() != 1u ||
                !TryParseCommandInteger(arguments.front(), parsed) ||
                parsed < minimum || parsed > maximum)
            {
                error = std::string(path) + " expects an integer from " +
                    std::to_string(minimum) + " through " +
                    std::to_string(maximum) + ".";
                return false;
            }
            candidate = static_cast<int>(parsed);
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get && candidate == current)
            return RejectUnchangedCommandMutation(path, error);
        current = candidate;
        value = std::to_string(current);
        return true;
    }

    bool ApplyCommandUnsigned(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        std::uint32_t& current,
        std::uint32_t defaultValue,
        std::uint32_t minimum,
        std::uint32_t maximum,
        std::string& value,
        std::string& error)
    {
        std::uint32_t candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            std::int64_t parsed = 0;
            if (arguments.size() != 1u ||
                !TryParseCommandInteger(arguments.front(), parsed) ||
                parsed < static_cast<std::int64_t>(minimum) ||
                parsed > static_cast<std::int64_t>(maximum))
            {
                error = std::string(path) + " expects an integer from " +
                    std::to_string(minimum) + " through " +
                    std::to_string(maximum) + ".";
                return false;
            }
            candidate = static_cast<std::uint32_t>(parsed);
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get && candidate == current)
            return RejectUnchangedCommandMutation(path, error);
        current = candidate;
        value = std::to_string(current);
        return true;
    }

    bool ApplyCommandFloat(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        float& current,
        float defaultValue,
        float minimum,
        float maximum,
        std::string& value,
        std::string& error)
    {
        float candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            if (arguments.size() != 1u ||
                !TryParseCommandFloat(arguments.front(), candidate) ||
                candidate < minimum || candidate > maximum)
            {
                error = std::string(path) +
                    " expects a finite number from " +
                    FormatCommandFloat(minimum) + " through " +
                    FormatCommandFloat(maximum) + ".";
                return false;
            }
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get && candidate == current)
            return RejectUnchangedCommandMutation(path, error);
        current = candidate;
        value = FormatCommandFloat(current);
        return true;
    }

    std::vector<std::string> BuildSettingsSnapshotCommandArguments(
        const UiSettingsCommandDefinition& definition,
        std::string_view requestedValue)
    {
        std::vector<std::string> arguments;
        if (definition.kind == UiSettingsCommandKind::Float3 ||
            definition.kind == UiSettingsCommandKind::Float4)
        {
            std::istringstream values{ std::string(requestedValue) };
            for (std::string token; values >> token; )
                arguments.push_back(std::move(token));
        }
        else
        {
            arguments.emplace_back(requestedValue);
        }
        return arguments;
    }

    std::filesystem::path GetSettingsSnapshotCatalogPath()
    {
        PWSTR localAppData = nullptr;
        const HRESULT result = SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &localAppData);
        if (FAILED(result) || !localAppData || localAppData[0] == L'\0')
        {
            CoTaskMemFree(localAppData);
            return {};
        }
        const std::string version(SettingsSnapshotVersionText.data(), 4u);
        const std::wstring wideVersion(version.begin(), version.end());
        const std::filesystem::path path =
            std::filesystem::path(localAppData) / L"UVSR" /
            (L"settings-snapshots-v" + wideVersion + L".txt");
        CoTaskMemFree(localAppData);
        return path;
    }

    std::filesystem::path GetSettingsSnapshotRestartHandoffPath()
    {
        const std::filesystem::path catalog =
            GetSettingsSnapshotCatalogPath();
        return catalog.empty()
            ? std::filesystem::path{}
            : catalog.parent_path() / L"settings-snapshot-restart-v1.bin";
    }

    bool RemoveSettingsSnapshotRestartHandoff(
        const std::filesystem::path& path,
        std::string& error)
    {
        error.clear();
        if (path.empty())
        {
            error = "restart handoff path is empty";
            return false;
        }
        std::error_code fileError;
        std::filesystem::remove(path, fileError);
        if (fileError)
        {
            error = "could not remove restart handoff: " +
                fileError.message();
            return false;
        }
        std::filesystem::path temporary = path;
        temporary += L".tmp";
        std::filesystem::remove(temporary, fileError);
        if (fileError)
        {
            error = "could not remove stale restart handoff temporary file: " +
                fileError.message();
            return false;
        }
        return true;
    }

    bool PersistSettingsSnapshotRestartHandoff(
        const std::filesystem::path& path,
        const SettingsSnapshotRestartHandoff& handoff,
        std::string& error)
    {
        error.clear();
        const std::uint32_t failureStage =
            static_cast<std::uint32_t>(handoff.failureStage);
        if (path.empty() || handoff.transaction.empty() ||
            handoff.transaction.size() != handoff.sourceValues.size() ||
            handoff.transaction.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)()) ||
            handoff.changedValueCount == 0u ||
            handoff.changedValueCount > handoff.transaction.size() ||
            (!handoff.rollingBack && handoff.changedValueCount != 1u) ||
            failureStage > static_cast<std::uint32_t>(
                SettingsSnapshotTransactionFailureStage::Rollback) ||
            (handoff.rollingBack &&
                (handoff.failureStage ==
                    SettingsSnapshotTransactionFailureStage::None ||
                 handoff.failure.empty())) ||
            (!handoff.rollingBack &&
                (handoff.failureStage !=
                    SettingsSnapshotTransactionFailureStage::None ||
                 !handoff.failure.empty())))
        {
            error = "restart handoff is incomplete or inconsistent";
            return false;
        }

        std::vector<std::uint8_t> bytes(
            RestartHandoffMagic.begin(), RestartHandoffMagic.end());
        AppendU32(
            bytes,
            static_cast<std::uint32_t>(handoff.transaction.size()));
        bool adapterTransition = false;
        for (std::size_t index = 0u;
             index < handoff.transaction.size();
             ++index)
        {
            const SettingsSnapshotTransactionEntry& entry =
                handoff.transaction[index];
            const std::uint32_t mode = static_cast<std::uint32_t>(entry.mode);
            if (mode > static_cast<std::uint32_t>(
                    SettingsSnapshotApplicationMode::Selector) ||
                !AppendString(bytes, entry.name) ||
                !AppendString(bytes, entry.requestedValue) ||
                !AppendString(bytes, handoff.sourceValues[index]))
            {
                error = "restart handoff contains an invalid entry";
                return false;
            }
            AppendU32(bytes, mode);
            if (entry.name == "gpu.adapter" &&
                entry.requestedValue != handoff.sourceValues[index])
            {
                adapterTransition = true;
            }
        }
        if (!adapterTransition)
        {
            error = "restart handoff contains no adapter transition";
            return false;
        }
        bytes.push_back(handoff.rollingBack ? 1u : 0u);
        AppendU64(
            bytes,
            static_cast<std::uint64_t>(handoff.changedValueCount));
        AppendU32(bytes, failureStage);
        if (!AppendString(bytes, handoff.failure) ||
            bytes.size() + sizeof(std::uint32_t) >
                MaximumRestartHandoffBytes)
        {
            error = "restart handoff exceeds its durable format limit";
            return false;
        }
        AppendU32(bytes, RestartHandoffCrc32(bytes, bytes.size()));
        if (WriteBytesAtomically(path, bytes, error))
            return true;

        std::string cleanupError;
        if (!RemoveSettingsSnapshotRestartHandoff(path, cleanupError))
            error += "; stale handoff cleanup failed: " + cleanupError;
        return false;
    }

    bool LoadSettingsSnapshotRestartHandoff(
        const std::filesystem::path& path,
        SettingsSnapshotRestartHandoff& handoff,
        bool& found,
        std::string& error)
    {
        handoff = {};
        found = false;
        error.clear();
        if (path.empty())
        {
            error = "restart handoff path is empty";
            return false;
        }

        std::filesystem::path temporary = path;
        temporary += L".tmp";
        std::error_code fileError;
        std::filesystem::remove(temporary, fileError);
        if (fileError)
        {
            error = "could not remove stale restart handoff temporary file: " +
                fileError.message();
            return false;
        }
        if (!std::filesystem::exists(path, fileError))
        {
            if (fileError)
            {
                error = "could not inspect restart handoff: " +
                    fileError.message();
                return false;
            }
            return true;
        }

        const std::uintmax_t fileSize =
            std::filesystem::file_size(path, fileError);
        if (fileError || fileSize > MaximumRestartHandoffBytes ||
            fileSize < RestartHandoffMagic.size() + 4u + 4u)
        {
            error = "restart handoff has an invalid size";
            return false;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            error = "could not open restart handoff";
            return false;
        }
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount() !=
                static_cast<std::streamsize>(bytes.size()))
        {
            error = "could not read complete restart handoff";
            return false;
        }

        const std::size_t payloadSize = bytes.size() - 4u;
        std::size_t checksumCursor = payloadSize;
        std::uint32_t storedChecksum = 0u;
        if (!ReadU32(
                bytes,
                bytes.size(),
                checksumCursor,
                storedChecksum) ||
            checksumCursor != bytes.size() ||
            storedChecksum != RestartHandoffCrc32(bytes, payloadSize) ||
            !std::equal(
                RestartHandoffMagic.begin(),
                RestartHandoffMagic.end(),
                bytes.begin()))
        {
            error = "restart handoff integrity check failed";
            return false;
        }

        std::size_t cursor = RestartHandoffMagic.size();
        std::uint32_t entryCount = 0u;
        if (!ReadU32(bytes, payloadSize, cursor, entryCount) ||
            entryCount == 0u ||
            entryCount > UiSettingsCommandCatalog.size())
        {
            error = "restart handoff entry count is invalid";
            return false;
        }
        handoff.transaction.reserve(entryCount);
        handoff.sourceValues.reserve(entryCount);
        bool adapterTransition = false;
        for (std::uint32_t index = 0u; index < entryCount; ++index)
        {
            SettingsSnapshotTransactionEntry entry;
            std::string source;
            std::uint32_t mode = 0u;
            if (!ReadString(bytes, payloadSize, cursor, entry.name) ||
                !ReadString(
                    bytes, payloadSize, cursor, entry.requestedValue) ||
                !ReadString(bytes, payloadSize, cursor, source) ||
                !ReadU32(bytes, payloadSize, cursor, mode) ||
                mode > static_cast<std::uint32_t>(
                    SettingsSnapshotApplicationMode::Selector))
            {
                error = "restart handoff entry is corrupt";
                return false;
            }
            entry.mode = static_cast<SettingsSnapshotApplicationMode>(mode);
            if (entry.name == "gpu.adapter" &&
                entry.requestedValue != source)
            {
                adapterTransition = true;
            }
            handoff.transaction.push_back(std::move(entry));
            handoff.sourceValues.push_back(std::move(source));
        }

        if (cursor >= payloadSize || bytes[cursor] > 1u)
        {
            error = "restart handoff rollback flag is corrupt";
            return false;
        }
        handoff.rollingBack = bytes[cursor++] != 0u;
        std::uint64_t changedValueCount = 0u;
        std::uint32_t failureStage = 0u;
        if (!ReadU64(
                bytes,
                payloadSize,
                cursor,
                changedValueCount) ||
            changedValueCount >
                static_cast<std::uint64_t>(
                    (std::numeric_limits<std::size_t>::max)()) ||
            !ReadU32(bytes, payloadSize, cursor, failureStage) ||
            failureStage > static_cast<std::uint32_t>(
                SettingsSnapshotTransactionFailureStage::Rollback) ||
            !ReadString(bytes, payloadSize, cursor, handoff.failure) ||
            cursor != payloadSize)
        {
            error = "restart handoff trailer is corrupt";
            return false;
        }
        handoff.changedValueCount =
            static_cast<std::size_t>(changedValueCount);
        handoff.failureStage =
            static_cast<SettingsSnapshotTransactionFailureStage>(failureStage);
        if (!adapterTransition || handoff.changedValueCount == 0u ||
            handoff.changedValueCount > handoff.transaction.size() ||
            (handoff.rollingBack &&
                (handoff.failureStage ==
                    SettingsSnapshotTransactionFailureStage::None ||
                 handoff.failure.empty())) ||
            (!handoff.rollingBack &&
                (handoff.changedValueCount != 1u ||
                 handoff.failureStage !=
                    SettingsSnapshotTransactionFailureStage::None ||
                 !handoff.failure.empty())))
        {
            error = "restart handoff trailer is inconsistent";
            return false;
        }
        found = true;
        return true;
    }

    std::string FormatSettingsSnapshotAdapterToken(std::int64_t index)
    {
        return index < 0 ||
            index > static_cast<std::int64_t>(
                (std::numeric_limits<int>::max)())
            ? std::string{}
            : std::to_string(index);
    }

    std::string FormatSettingsSnapshotSceneToken(std::string_view fileName)
    {
        std::string error;
        return ValidateSettingsSnapshotSelectorToken(
                "scene.current", fileName, error)
            ? std::string(fileName)
            : std::string{};
    }

    std::string FormatSettingsSnapshotLightToken(
        std::size_t index,
        std::string_view identity)
    {
        const std::string token =
            std::to_string(index) + ":" + std::string(identity);
        std::string error;
        return ValidateSettingsSnapshotSelectorToken(
                "light.selected", token, error)
            ? token
            : std::string{};
    }

    std::string FormatSettingsSnapshotMaterialToken(
        bool none,
        std::uint32_t id)
    {
        return none ? "none" : std::to_string(id);
    }

    bool ResolveSettingsSnapshotAdapterToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotAdapterOption>& options,
        std::int64_t& index,
        std::string& canonicalToken,
        std::string& error)
    {
        error.clear();
        std::uint64_t numeric = 0u;
        const bool numericRequest = TryParseUnsignedToken(
                requested, numeric) &&
            numeric <= static_cast<std::uint64_t>(
                (std::numeric_limits<int>::max)());
        if (IsDecimalToken(requested) && !numericRequest)
        {
            error = "adapter index is outside the supported range";
            return false;
        }
        const std::string normalized =
            NormalizeCommandAscii(requested, true);
        const SettingsSnapshotAdapterOption* match = nullptr;
        for (const SettingsSnapshotAdapterOption& option : options)
        {
            const bool matches = numericRequest
                ? option.index == static_cast<std::int64_t>(numeric)
                : NormalizeCommandAscii(option.name, true) == normalized;
            if (!matches)
                continue;
            if (match && match->index != option.index)
            {
                error = "adapter name is ambiguous; use its numeric index";
                return false;
            }
            match = &option;
        }
        if (!match)
        {
            error = "unknown adapter selection";
            return false;
        }
        index = match->index;
        canonicalToken = FormatSettingsSnapshotAdapterToken(index);
        return !canonicalToken.empty();
    }

    bool ResolveSettingsSnapshotSceneToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotSceneOption>& options,
        std::string& fileName,
        std::string& canonicalToken,
        std::string& error)
    {
        error.clear();
        const SettingsSnapshotSceneOption* match = nullptr;
        for (const SettingsSnapshotSceneOption& option : options)
        {
            if (option.fileName != requested)
                continue;
            canonicalToken = FormatSettingsSnapshotSceneToken(
                option.fileName);
            if (canonicalToken.empty())
            {
                error = "scene catalog contains a noncanonical filename";
                return false;
            }
            fileName = option.runtimeFileName.empty()
                ? option.fileName
                : option.runtimeFileName;
            return true;
        }

        const std::string normalized =
            NormalizeCommandAscii(requested, true);
        for (const SettingsSnapshotSceneOption& option : options)
        {
            const bool matches =
                NormalizeCommandAscii(option.fileName, true) == normalized ||
                NormalizeCommandAscii(option.displayName, true) == normalized;
            if (!matches)
                continue;
            if (match && match->fileName != option.fileName)
            {
                error = "scene name is ambiguous; use its exact filename";
                return false;
            }
            match = &option;
        }
        if (!match)
        {
            error = "unknown scene selection";
            return false;
        }
        canonicalToken = FormatSettingsSnapshotSceneToken(match->fileName);
        if (canonicalToken.empty())
        {
            error = "scene catalog contains a noncanonical filename";
            return false;
        }
        fileName = match->runtimeFileName.empty()
            ? match->fileName
            : match->runtimeFileName;
        return true;
    }

    bool ResolveSettingsSnapshotLightToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotLightOption>& options,
        std::size_t& index,
        std::string& canonicalToken,
        std::string& error)
    {
        error.clear();
        std::uint64_t numeric = 0u;
        bool exactIdentity = false;
        bool numericRequest = false;
        std::string_view requestedIdentity;
        const std::size_t separator = requested.find(':');
        if (separator != std::string_view::npos)
        {
            exactIdentity = TryParseUnsignedToken(
                    requested.substr(0u, separator), numeric) &&
                numeric <= static_cast<std::uint64_t>(
                    (std::numeric_limits<std::size_t>::max)());
            numericRequest = exactIdentity;
            requestedIdentity = requested.substr(separator + 1u);
            if (!exactIdentity || requestedIdentity.empty())
            {
                error = "invalid light selector token";
                return false;
            }
        }
        else
        {
            numericRequest = TryParseUnsignedToken(requested, numeric) &&
                numeric <= static_cast<std::uint64_t>(
                    (std::numeric_limits<std::size_t>::max)());
            if (IsDecimalToken(requested) && !numericRequest)
            {
                error = "light index is outside the supported range";
                return false;
            }
        }
        const std::string normalized =
            NormalizeCommandAscii(requested, true);
        const SettingsSnapshotLightOption* match = nullptr;
        for (const SettingsSnapshotLightOption& option : options)
        {
            const bool matches = exactIdentity
                ? option.index == static_cast<std::size_t>(numeric) &&
                    option.identity == requestedIdentity
                : numericRequest
                    ? option.index == static_cast<std::size_t>(numeric)
                    : NormalizeCommandAscii(option.identity, true) ==
                        normalized;
            if (!matches)
                continue;
            if (match && match->index != option.index)
            {
                error = "light name is ambiguous; use its index:identity token";
                return false;
            }
            match = &option;
        }
        if (!match)
        {
            error = "unknown light selection";
            return false;
        }
        index = match->index;
        canonicalToken = FormatSettingsSnapshotLightToken(
            match->index, match->identity);
        if (canonicalToken.empty())
        {
            error = "light table contains a noncanonical identity";
            return false;
        }
        return true;
    }

    bool ResolveSettingsSnapshotMaterialToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotMaterialOption>& options,
        bool& none,
        std::uint32_t& id,
        std::string& canonicalToken,
        std::string& error)
    {
        error.clear();
        if (NormalizeCommandAscii(requested, true) == "none")
        {
            none = true;
            id = 0u;
            canonicalToken = "none";
            return true;
        }

        std::uint64_t numeric = 0u;
        const bool numericRequest = TryParseUnsignedToken(
                requested, numeric) &&
            numeric <=
                (std::numeric_limits<std::uint32_t>::max)();
        if (IsDecimalToken(requested) && !numericRequest)
        {
            error = "material id is outside the supported range";
            return false;
        }
        const std::string normalized =
            NormalizeCommandAscii(requested, true);
        const SettingsSnapshotMaterialOption* match = nullptr;
        for (const SettingsSnapshotMaterialOption& option : options)
        {
            const bool matches = numericRequest
                ? option.id == static_cast<std::uint32_t>(numeric)
                : NormalizeCommandAscii(option.name, true) == normalized;
            if (!matches)
                continue;
            if (match && match->id != option.id)
            {
                error = "material name is ambiguous; use its runtime id";
                return false;
            }
            match = &option;
        }
        if (!match)
        {
            error = "unknown material selection";
            return false;
        }
        none = false;
        id = match->id;
        canonicalToken = FormatSettingsSnapshotMaterialToken(false, id);
        return true;
    }

    SettingsSnapshotController::SettingsSnapshotController()
        : m_Code(BuildSettingsSnapshotCode({}))
    {
    }

    void SettingsSnapshotController::Refresh(
        const SettingsSnapshotValueReader& readValue)
    {
        DecodedSettings settings;
        SettingsCommandFloatPrecisionScope precision;
        for (const UiSettingsCommandDefinition& definition :
            UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;

            std::string value;
            std::string error;
            if (!readValue || !readValue(definition.name, value, error))
                value = "<unavailable>";
            settings.emplace(std::string(definition.name), std::move(value));
        }
        m_Canonical = FormatCanonicalSettingsSnapshot(settings);
        m_Code = BuildSettingsSnapshotCode(m_Canonical);
    }

    std::string SettingsSnapshotController::BuildCatalogSection() const
    {
        return "[" + m_Code + "]\n" + m_Canonical +
            "[/" + m_Code + "]\n";
    }

    bool SettingsSnapshotController::Persist(
        const std::filesystem::path& path) const
    {
        if (path.empty())
            return false;

        std::string existing;
        {
            std::ifstream input(path, std::ios::binary);
            if (input.is_open())
            {
                existing.assign(
                    std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
                if (input.bad())
                {
                    log::warning(
                        "Could not read the settings snapshot catalog at %s",
                        path.generic_string().c_str());
                    return false;
                }
            }
        }

        const std::string section = BuildCatalogSection();
        if (existing.find(section) != std::string::npos)
            return true;
        const std::string opening = "[" + m_Code + "]\n";
        if (existing.find(opening) != std::string::npos)
        {
            log::warning(
                "Settings snapshot catalog contains a conflicting entry "
                "for %s",
                m_Code.c_str());
            return false;
        }

        std::string updated = std::move(existing);
        if (updated.empty())
        {
            updated = "# UVSR Settings Snapshot Catalog v" +
                std::string(SettingsSnapshotVersionText.data(), 4u) + '\n';
        }
        updated += section;
        std::string writeError;
        if (!WriteTextAtomically(path, updated, writeError))
        {
            log::warning(
                "Could not atomically write the settings snapshot catalog "
                "at %s: %s",
                path.generic_string().c_str(),
                writeError.c_str());
            return false;
        }
        return true;
    }

    bool SettingsSnapshotController::PersistToLocalCatalog() const
    {
        return Persist(GetSettingsSnapshotCatalogPath());
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::FinalizeStagedStep(
        SettingsSnapshotTransactionStep step,
        const SettingsSnapshotRuntimeAccess& access)
    {
        if (step.progress == SettingsSnapshotTransactionProgress::Succeeded)
            Refresh(access.readValue);
        return step;
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::BeginDecodedStaged(
        const DecodedSettings& decoded,
        const SettingsSnapshotRuntimeAccess& access)
    {
        SettingsSnapshotTransactionStep rejected;
        rejected.progress = SettingsSnapshotTransactionProgress::Failed;
        if (!access.sceneReady)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                "settings.load requires a fully loaded scene";
            return rejected;
        }

        std::vector<SettingsSnapshotTransactionEntry> transaction;
        std::string error;
        if (!BuildSettingsSnapshotTransaction(
                decoded,
                BuildAuthoritativeSnapshotCatalog(),
                transaction,
                error))
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                "snapshot membership rejected: " + error;
            return rejected;
        }

        SettingsCommandFloatPrecisionScope precision;
        SettingsSnapshotStagedRuntimeAccess stagedAccess{
            access.validateValue,
            access.readValue,
            access.writeValue,
            access.driveSelector,
            access.persistRestartHandoff
        };
        return FinalizeStagedStep(
            m_TransactionCoordinator.Begin(transaction, stagedAccess),
            access);
    }

    bool SettingsSnapshotController::ApplyDecodedImmediate(
        const DecodedSettings& decoded,
        const SettingsSnapshotRuntimeAccess& access,
        std::size_t& changedValueCount,
        std::string& error)
    {
        changedValueCount = 0u;
        error.clear();
        if (HasStagedApply())
        {
            error = "another staged settings transaction is active";
            return false;
        }
        if (!access.sceneReady)
        {
            error = "settings.load requires a fully loaded scene";
            return false;
        }
        std::vector<SettingsSnapshotTransactionEntry> transaction;
        if (!BuildSettingsSnapshotTransaction(
                decoded,
                BuildAuthoritativeSnapshotCatalog(),
                transaction,
                error))
        {
            error = "snapshot membership rejected: " + error;
            return false;
        }
        SettingsCommandFloatPrecisionScope precision;
        const SettingsSnapshotTransactionResult result =
            ApplySettingsSnapshotTransaction(
                transaction,
                access.validateValue,
                access.readValue,
                access.writeValue);
        changedValueCount = result.changedValueCount;
        if (!result.succeeded)
        {
            error = std::string("settings transaction ") +
                TransactionStageName(result.failureStage) + " failed: " +
                result.error;
            return false;
        }
        Refresh(access.readValue);
        return true;
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::ContinueStagedApply(
        const SettingsSnapshotRuntimeAccess& access)
    {
        SettingsCommandFloatPrecisionScope precision;
        SettingsSnapshotStagedRuntimeAccess stagedAccess{
            access.validateValue,
            access.readValue,
            access.writeValue,
            access.driveSelector,
            access.persistRestartHandoff
        };
        return FinalizeStagedStep(
            m_TransactionCoordinator.Advance(stagedAccess),
            access);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::ResumeStagedApply(
        const SettingsSnapshotRestartHandoff& handoff,
        const SettingsSnapshotRuntimeAccess& access)
    {
        if (HasStagedApply())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.progress = SettingsSnapshotTransactionProgress::Failed;
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error =
                "another staged settings transaction is active";
            return rejected;
        }
        const std::vector<SettingsSnapshotCatalogEntry> catalog =
            BuildAuthoritativeSnapshotCatalog();
        if (handoff.transaction.size() != catalog.size())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.progress = SettingsSnapshotTransactionProgress::Failed;
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error =
                "restart handoff does not match the authoritative catalog";
            return rejected;
        }
        for (std::size_t index = 0u; index < catalog.size(); ++index)
        {
            if (handoff.transaction[index].name != catalog[index].name ||
                handoff.transaction[index].mode != catalog[index].mode)
            {
                SettingsSnapshotTransactionStep rejected;
                rejected.progress =
                    SettingsSnapshotTransactionProgress::Failed;
                rejected.result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Configuration;
                rejected.result.error =
                    "restart handoff catalog order, name, or mode mismatch";
                return rejected;
            }
        }
        SettingsCommandFloatPrecisionScope precision;
        SettingsSnapshotStagedRuntimeAccess stagedAccess{
            access.validateValue,
            access.readValue,
            access.writeValue,
            access.driveSelector,
            access.persistRestartHandoff
        };
        return FinalizeStagedStep(
            m_TransactionCoordinator.Resume(handoff, stagedAccess),
            access);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::BeginApplyCanonicalStaged(
        std::string_view canonical,
        const SettingsSnapshotRuntimeAccess& access)
    {
        if (HasStagedApply())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.progress = SettingsSnapshotTransactionProgress::Failed;
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error =
                "another staged settings transaction is active";
            return rejected;
        }
        m_TransactionCoordinator.Reset();
        SettingsSnapshotTransactionStep rejected;
        rejected.progress = SettingsSnapshotTransactionProgress::Failed;
        DecodedSettings decoded;
        try
        {
            decoded = ParseSettingsSnapshot(canonical);
        }
        catch (const std::exception& exception)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = "snapshot payload decode failed: " +
                std::string(exception.what());
            return rejected;
        }
        if (FormatCanonicalSettingsSnapshot(decoded) != canonical)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                "snapshot payload is not complete canonical text";
            return rejected;
        }
        return BeginDecodedStaged(decoded, access);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::BeginLoadCodeStaged(
        std::string_view code,
        const SettingsSnapshotRuntimeAccess& access)
    {
        if (HasStagedApply())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.progress = SettingsSnapshotTransactionProgress::Failed;
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error =
                "another staged settings transaction is active";
            return rejected;
        }
        m_TransactionCoordinator.Reset();
        SettingsSnapshotTransactionStep rejected;
        rejected.progress = SettingsSnapshotTransactionProgress::Failed;
        std::string error;
        if (!ValidateSettingsSnapshotLoadCode(code, error))
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = std::move(error);
            return rejected;
        }

        DecodedSettings decoded;
        try
        {
            decoded = DecodeSettingsSnapshot(
                code,
                GetDefaultSettingsSnapshotCatalogPaths(
                    code.substr(0u, 4u)));
        }
        catch (const std::exception& exception)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = "snapshot decode failed: " +
                std::string(exception.what());
            return rejected;
        }

        const std::string canonical =
            FormatCanonicalSettingsSnapshot(decoded);
        if (BuildSettingsSnapshotCode(canonical) != std::string(code))
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                "snapshot payload is not in canonical command-name order";
            return rejected;
        }
        return BeginDecodedStaged(decoded, access);
    }

    bool SettingsSnapshotController::ApplyCanonical(
        std::string_view canonical,
        const SettingsSnapshotRuntimeAccess& access,
        std::size_t& changedValueCount,
        std::string& error)
    {
        changedValueCount = 0u;
        error.clear();
        DecodedSettings decoded;
        try
        {
            decoded = ParseSettingsSnapshot(canonical);
        }
        catch (const std::exception& exception)
        {
            error = "snapshot payload decode failed: " +
                std::string(exception.what());
            return false;
        }
        if (FormatCanonicalSettingsSnapshot(decoded) != canonical)
        {
            error = "snapshot payload is not complete canonical text";
            return false;
        }
        return ApplyDecodedImmediate(
            decoded, access, changedValueCount, error);
    }

    bool SettingsSnapshotController::LoadCode(
        std::string_view code,
        const SettingsSnapshotRuntimeAccess& access,
        std::size_t& changedValueCount,
        std::string& error)
    {
        changedValueCount = 0u;
        error.clear();
        if (!ValidateSettingsSnapshotLoadCode(code, error))
            return false;
        DecodedSettings decoded;
        try
        {
            decoded = DecodeSettingsSnapshot(
                code,
                GetDefaultSettingsSnapshotCatalogPaths(
                    code.substr(0u, 4u)));
        }
        catch (const std::exception& exception)
        {
            error = "snapshot decode failed: " +
                std::string(exception.what());
            return false;
        }
        const std::string canonical =
            FormatCanonicalSettingsSnapshot(decoded);
        if (BuildSettingsSnapshotCode(canonical) != std::string(code))
        {
            error = "snapshot payload is not in canonical command-name order";
            return false;
        }
        return ApplyDecodedImmediate(
            decoded, access, changedValueCount, error);
    }
}
