#include "settings_snapshot_transaction.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace uvsr
{
    namespace
    {
        constexpr std::string_view UnavailableValue = "<unavailable>";

        [[nodiscard]] bool ParseExactDouble(
            std::string_view text,
            double& value)
        {
            if (text.empty())
                return false;
            const std::string owned(text);
            char* end = nullptr;
            errno = 0;
            value = std::strtod(owned.c_str(), &end);
            return errno != ERANGE && end == owned.c_str() + owned.size() &&
                std::isfinite(value);
        }

        [[nodiscard]] bool ParseExactFloat(
            std::string_view text,
            float& value)
        {
            if (text.empty())
                return false;
            const std::string owned(text);
            char* end = nullptr;
            errno = 0;
            value = std::strtof(owned.c_str(), &end);
            return errno != ERANGE && end == owned.c_str() + owned.size() &&
                std::isfinite(value);
        }

        [[nodiscard]] std::string FormatCanonicalFloat(float value)
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

        [[nodiscard]] bool ParseCanonicalFloat(
            std::string_view text,
            float& value)
        {
            return ParseExactFloat(text, value) &&
                FormatCanonicalFloat(value) == text;
        }

        [[nodiscard]] std::string_view Trim(std::string_view value)
        {
            while (!value.empty() &&
                (value.front() == ' ' || value.front() == '\t'))
            {
                value.remove_prefix(1u);
            }
            while (!value.empty() &&
                (value.back() == ' ' || value.back() == '\t'))
            {
                value.remove_suffix(1u);
            }
            return value;
        }

        [[nodiscard]] bool IsNumberCharacter(char character)
        {
            return (character >= '0' && character <= '9') ||
                character == '-' || character == '+' || character == '.' ||
                character == 'e' || character == 'E';
        }

        [[nodiscard]] bool ValueMatchesNumericDomain(
            double value,
            std::string_view domain)
        {
            bool constrained = false;
            const std::size_t alternative = domain.find(" or ");
            if (alternative != std::string_view::npos)
            {
                double exact = 0.0;
                if (ParseExactDouble(
                        Trim(domain.substr(0u, alternative)),
                        exact))
                {
                    constrained = true;
                    if (value == exact)
                        return true;
                }
            }

            for (std::size_t separator = domain.find("..");
                 separator != std::string_view::npos;
                 separator = domain.find("..", separator + 2u))
            {
                std::size_t minimumBegin = separator;
                while (minimumBegin > 0u &&
                    IsNumberCharacter(domain[minimumBegin - 1u]))
                {
                    --minimumBegin;
                }
                std::size_t maximumBegin = separator + 2u;
                while (maximumBegin < domain.size() &&
                    (domain[maximumBegin] == ' ' ||
                     domain[maximumBegin] == '\t'))
                {
                    ++maximumBegin;
                }
                std::size_t maximumEnd = maximumBegin;
                while (maximumEnd < domain.size() &&
                    IsNumberCharacter(domain[maximumEnd]))
                {
                    ++maximumEnd;
                }

                double minimum = 0.0;
                double maximum = 0.0;
                if (!ParseExactDouble(
                        domain.substr(
                            minimumBegin,
                            separator - minimumBegin),
                        minimum) ||
                    !ParseExactDouble(
                        domain.substr(
                            maximumBegin,
                            maximumEnd - maximumBegin),
                        maximum))
                {
                    continue;
                }
                constrained = true;
                minimum = static_cast<double>(
                    static_cast<float>(minimum));
                maximum = static_cast<double>(
                    static_cast<float>(maximum));
                if (value >= minimum && value <= maximum)
                    return true;
            }
            return !constrained;
        }

        [[nodiscard]] bool ValidateEnumValue(
            std::string_view value,
            std::string_view domain)
        {
            while (!domain.empty())
            {
                const std::size_t separator = domain.find('|');
                std::string_view candidate = Trim(
                    domain.substr(0u, separator));
                const std::size_t metadata = candidate.find(';');
                if (metadata != std::string_view::npos)
                    candidate = Trim(candidate.substr(0u, metadata));
                if (value == candidate)
                    return true;
                if (separator == std::string_view::npos)
                    break;
                domain.remove_prefix(separator + 1u);
            }
            return false;
        }

        [[nodiscard]] bool ReadTransactionValues(
            const std::vector<SettingsSnapshotTransactionEntry>& transaction,
            const SettingsSnapshotValueReader& readValue,
            std::vector<std::string>& values,
            std::string& error)
        {
            values.clear();
            values.reserve(transaction.size());
            for (const SettingsSnapshotTransactionEntry& entry : transaction)
            {
                std::string value;
                std::string readError;
                if (!readValue(entry.name, value, readError))
                {
                    error = "could not read '" + entry.name + "': " +
                        (readError.empty() ? "no reason was reported" : readError);
                    return false;
                }
                values.push_back(std::move(value));
            }
            return true;
        }

        [[nodiscard]] bool RestoreTransactionValues(
            const std::vector<SettingsSnapshotTransactionEntry>& transaction,
            const std::vector<std::string>& previousValues,
            const SettingsSnapshotValueReader& readValue,
            const SettingsSnapshotValueWriter& writeValue,
            std::string& error)
        {
            bool restored = true;
            std::string firstFailure;

            // Catalog order is deliberate: preset selectors precede the
            // custom axes they can update, so the later writes restore the
            // exact captured axes rather than a preset approximation.
            for (std::size_t index = 0u; index < transaction.size(); ++index)
            {
                const SettingsSnapshotTransactionEntry& entry =
                    transaction[index];
                if (entry.mode ==
                        SettingsSnapshotApplicationMode::Selector ||
                    previousValues[index] == UnavailableValue)
                    continue;

                std::string liveValue;
                std::string operationError;
                if (!readValue(entry.name, liveValue, operationError))
                {
                    restored = false;
                    if (firstFailure.empty())
                    {
                        firstFailure = "could not read '" + entry.name +
                            "' during rollback: " + operationError;
                    }
                    continue;
                }
                if (liveValue == previousValues[index])
                    continue;
                if (!writeValue(
                        entry.name,
                        previousValues[index],
                        operationError))
                {
                    restored = false;
                    if (firstFailure.empty())
                    {
                        firstFailure = "could not restore '" + entry.name +
                            "': " + operationError;
                    }
                }
            }

            std::vector<std::string> restoredValues;
            std::string verificationError;
            if (!ReadTransactionValues(
                    transaction,
                    readValue,
                    restoredValues,
                    verificationError))
            {
                restored = false;
                if (firstFailure.empty())
                    firstFailure = std::move(verificationError);
            }
            else
            {
                for (std::size_t index = 0u;
                     index < transaction.size();
                     ++index)
                {
                    if (restoredValues[index] == previousValues[index])
                        continue;
                    restored = false;
                    if (firstFailure.empty())
                    {
                        firstFailure = "rollback verification failed for '" +
                            transaction[index].name + "'";
                    }
                }
            }

            error = restored ? std::string{} : std::move(firstFailure);
            return restored;
        }

        [[nodiscard]] bool ParseCanonicalUnsigned(
            std::string_view token,
            std::uint64_t& value)
        {
            if (token.empty() ||
                (token.size() > 1u && token.front() == '0'))
            {
                return false;
            }
            std::uint64_t parsed = 0u;
            for (const char character : token)
            {
                if (character < '0' || character > '9')
                    return false;
                const std::uint64_t digit =
                    static_cast<std::uint64_t>(character - '0');
                if (parsed >
                    (std::numeric_limits<std::uint64_t>::max() - digit) /
                        10u)
                {
                    return false;
                }
                parsed = parsed * 10u + digit;
            }
            value = parsed;
            return true;
        }

        [[nodiscard]] bool IsLightDependent(std::string_view name)
        {
            constexpr std::string_view Prefix = "light.selected.";
            return name.size() > Prefix.size() &&
                name.compare(0u, Prefix.size(), Prefix) == 0u;
        }

        [[nodiscard]] bool IsMaterialDependent(std::string_view name)
        {
            constexpr std::string_view Prefix = "material.selected.";
            return name.size() > Prefix.size() &&
                name.compare(0u, Prefix.size(), Prefix) == 0u;
        }

        [[nodiscard]] bool IsSelectorName(std::string_view name)
        {
            return name == "gpu.adapter" || name == "scene.current" ||
                name == "light.selected" || name == "material.selected";
        }
    }

    bool ValidateSettingsSnapshotSelectorToken(
        std::string_view name,
        std::string_view token,
        std::string& error)
    {
        error.clear();
        std::uint64_t numeric = 0u;
        if (name == "gpu.adapter")
        {
            if (ParseCanonicalUnsigned(token, numeric) &&
                numeric <= static_cast<std::uint64_t>(
                    (std::numeric_limits<int>::max)()))
                return true;
            error = "gpu.adapter snapshot token must be a canonical "
                "non-negative adapter index";
            return false;
        }
        if (name == "scene.current")
        {
            if (token.empty() || token.front() == '/' ||
                token.find("./") == 0u ||
                token.find('\\') != std::string_view::npos ||
                token.find(':') != std::string_view::npos ||
                token.find("//") != std::string_view::npos ||
                token.find("/./") != std::string_view::npos ||
                token.find("../") != std::string_view::npos ||
                (token.size() >= 3u &&
                    token.substr(token.size() - 3u) == "/..") ||
                token == "." || token == ".." ||
                token.size() < std::string_view(".scene.json").size() ||
                token.substr(token.size() -
                    std::string_view(".scene.json").size()) != ".scene.json")
            {
                error = "scene.current snapshot token must be a normalized "
                    "relative .scene.json filename";
                return false;
            }
            return true;
        }
        if (name == "light.selected")
        {
            const std::size_t separator = token.find(':');
            if (separator != std::string_view::npos && separator > 0u &&
                separator + 1u < token.size() &&
                ParseCanonicalUnsigned(token.substr(0u, separator), numeric) &&
                numeric <= static_cast<std::uint64_t>(
                    (std::numeric_limits<std::size_t>::max)()) &&
                token.find('\n', separator + 1u) == std::string_view::npos &&
                token.find('\r', separator + 1u) == std::string_view::npos)
            {
                return true;
            }
            error = "light.selected snapshot token must be "
                "<zero-based-index>:<stable-identity>";
            return false;
        }
        if (name == "material.selected")
        {
            if (token == "none" ||
                (ParseCanonicalUnsigned(token, numeric) &&
                    numeric <= static_cast<std::uint64_t>(
                        (std::numeric_limits<std::uint32_t>::max)())))
                return true;
            error = "material.selected snapshot token must be a canonical "
                "runtime material id or none";
            return false;
        }
        error = "unknown settings selector '" + std::string(name) + "'";
        return false;
    }

    SettingsSnapshotApplicationMode ResolveSettingsSnapshotApplicationMode(
        const UiSettingsCommandDefinition& definition) noexcept
    {
        if (definition.kind == UiSettingsCommandKind::DynamicSelection)
            return SettingsSnapshotApplicationMode::Selector;
        if (definition.dynamic)
            return SettingsSnapshotApplicationMode::LiveDependent;
        return SettingsSnapshotApplicationMode::Mutable;
    }

    bool ValidateSettingsSnapshotCatalogValue(
        const UiSettingsCommandDefinition& definition,
        std::string_view value,
        std::string& error)
    {
        error.clear();
        if (definition.kind == UiSettingsCommandKind::Action)
        {
            error = "actions cannot appear in a settings snapshot";
            return false;
        }
        const auto reject = [&]()
        {
            error = "value '" + std::string(value) + "' is outside '" +
                std::string(definition.name) + "' domain " +
                std::string(definition.domain);
            return false;
        };

        switch (definition.kind)
        {
        case UiSettingsCommandKind::Boolean:
            return value == "on" || value == "off" ? true : reject();
        case UiSettingsCommandKind::Enum:
            return ValidateEnumValue(value, definition.domain)
                ? true
                : reject();
        case UiSettingsCommandKind::Integer:
        {
            if (value.empty())
                return reject();
            const std::string owned(value);
            char* end = nullptr;
            errno = 0;
            const long long parsed = std::strtoll(owned.c_str(), &end, 10);
            if (errno == ERANGE ||
                end != owned.c_str() + owned.size() ||
                !ValueMatchesNumericDomain(
                    static_cast<double>(parsed),
                    definition.domain))
            {
                return reject();
            }
            char canonical[32]{};
            const auto formatted = std::to_chars(
                canonical, canonical + sizeof(canonical), parsed);
            return formatted.ec == std::errc{} &&
                value == std::string_view(
                    canonical,
                    static_cast<std::size_t>(formatted.ptr - canonical))
                    ? true
                    : reject();
        }
        case UiSettingsCommandKind::Float:
        {
            float parsed = 0.f;
            return ParseCanonicalFloat(value, parsed) &&
                ValueMatchesNumericDomain(
                    static_cast<double>(parsed),
                    definition.domain)
                ? true
                : reject();
        }
        case UiSettingsCommandKind::Float3:
        case UiSettingsCommandKind::Float4:
        {
            const std::size_t expected =
                definition.kind == UiSettingsCommandKind::Float3 ? 3u : 4u;
            std::size_t count = 0u;
            std::size_t begin = 0u;
            for (;;)
            {
                const std::size_t separator = value.find(' ', begin);
                const std::string_view token = value.substr(
                    begin,
                    separator == std::string_view::npos
                        ? std::string_view::npos
                        : separator - begin);
                float parsed = 0.f;
                if (token.empty() || !ParseCanonicalFloat(token, parsed) ||
                    !ValueMatchesNumericDomain(
                        static_cast<double>(parsed),
                        definition.domain))
                {
                    return reject();
                }
                ++count;
                if (separator == std::string_view::npos)
                    break;
                begin = separator + 1u;
            }
            return count == expected ? true : reject();
        }
        case UiSettingsCommandKind::DynamicSelection:
            return ValidateSettingsSnapshotSelectorToken(
                definition.name, value, error);
        case UiSettingsCommandKind::Action:
            break;
        }
        return reject();
    }

    bool BuildSettingsSnapshotTransaction(
        const DecodedSettings& decoded,
        const std::vector<SettingsSnapshotCatalogEntry>& catalog,
        std::vector<SettingsSnapshotTransactionEntry>& transaction,
        std::string& error)
    {
        transaction.clear();
        error.clear();

        std::set<std::string, std::less<>> catalogNames;
        for (const SettingsSnapshotCatalogEntry& entry : catalog)
        {
            if (entry.name.empty() || !catalogNames.insert(entry.name).second)
            {
                error = "authoritative snapshot catalog contains an invalid "
                    "or duplicate setting name";
                return false;
            }
        }

        for (const auto& decodedEntry : decoded)
        {
            if (catalogNames.find(decodedEntry.first) == catalogNames.end())
            {
                error = "snapshot contains unknown setting '" +
                    decodedEntry.first + "'";
                return false;
            }
        }

        transaction.reserve(catalog.size());
        for (const SettingsSnapshotCatalogEntry& entry : catalog)
        {
            const auto decodedEntry = decoded.find(entry.name);
            if (decodedEntry == decoded.end())
            {
                error = "snapshot is missing required setting '" +
                    entry.name + "'";
                transaction.clear();
                return false;
            }
            transaction.push_back({
                entry.name,
                decodedEntry->second,
                entry.mode
            });
        }

        if (decoded.size() != transaction.size())
        {
            error = "snapshot membership does not match the authoritative "
                "snapshot catalog";
            transaction.clear();
            return false;
        }
        return true;
    }

    SettingsSnapshotTransactionResult ApplySettingsSnapshotTransaction(
        const std::vector<SettingsSnapshotTransactionEntry>& transaction,
        const SettingsSnapshotValueValidator& validateValue,
        const SettingsSnapshotValueReader& readValue,
        const SettingsSnapshotValueWriter& writeValue)
    {
        SettingsSnapshotTransactionResult result;
        if (!validateValue || !readValue || !writeValue)
        {
            result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            result.error = "settings transaction has no validator, reader, or writer";
            return result;
        }

        for (const SettingsSnapshotTransactionEntry& entry : transaction)
        {
            if (entry.mode == SettingsSnapshotApplicationMode::Mutable &&
                entry.requestedValue == UnavailableValue)
            {
                result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                result.error = "mutable setting '" + entry.name +
                    "' cannot use the unavailable sentinel";
                return result;
            }
            std::string validationError;
            if (entry.requestedValue != UnavailableValue &&
                !validateValue(
                    entry.name,
                    entry.requestedValue,
                    validationError))
            {
                result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                result.error = "snapshot preflight rejected '" + entry.name +
                    "': " + (validationError.empty()
                        ? "no reason was reported"
                        : validationError);
                return result;
            }
        }

        std::vector<std::string> previousValues;
        if (!ReadTransactionValues(
                transaction,
                readValue,
                previousValues,
                result.error))
        {
            result.failureStage =
                SettingsSnapshotTransactionFailureStage::Capture;
            return result;
        }

        for (std::size_t index = 0u; index < transaction.size(); ++index)
        {
            const SettingsSnapshotTransactionEntry& entry =
                transaction[index];
            if (previousValues[index] != UnavailableValue)
            {
                std::string validationError;
                const bool valid = entry.mode ==
                        SettingsSnapshotApplicationMode::Selector
                    ? ValidateSettingsSnapshotSelectorToken(
                        entry.name,
                        previousValues[index],
                        validationError)
                    : validateValue(
                        entry.name,
                        previousValues[index],
                        validationError);
                if (!valid)
                {
                    result.failureStage =
                        SettingsSnapshotTransactionFailureStage::Capture;
                    result.error = "captured value for '" + entry.name +
                        "' is not canonical: " + validationError;
                    return result;
                }
            }
            const bool requestedUnavailable =
                entry.requestedValue == UnavailableValue;
            const bool liveUnavailable =
                previousValues[index] == UnavailableValue;
            if (entry.mode ==
                    SettingsSnapshotApplicationMode::Selector &&
                entry.requestedValue != previousValues[index])
            {
                result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                result.error = "selector '" + entry.name +
                    "' requires the staged settings transaction coordinator";
                return result;
            }
            if (entry.mode ==
                    SettingsSnapshotApplicationMode::LiveDependent &&
                requestedUnavailable != liveUnavailable)
            {
                result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                result.error = "live-dependent setting '" + entry.name +
                    "' has incompatible requested and live availability";
                return result;
            }
            if (entry.mode == SettingsSnapshotApplicationMode::Mutable &&
                liveUnavailable)
            {
                result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                result.error = "mutable setting '" + entry.name +
                    "' cannot use the unavailable sentinel";
                return result;
            }
        }

        bool mutationStarted = false;
        const auto failWithRollback = [&]
            (SettingsSnapshotTransactionFailureStage stage,
             std::string failure)
        {
            result.failureStage = stage;
            result.error = std::move(failure);
            if (!mutationStarted)
                return;

            result.rollbackAttempted = true;
            std::string rollbackError;
            result.rollbackSucceeded = RestoreTransactionValues(
                transaction,
                previousValues,
                readValue,
                writeValue,
                rollbackError);
            if (!result.rollbackSucceeded)
            {
                result.error += "; rollback failed: " + rollbackError;
            }
        };

        for (const SettingsSnapshotTransactionEntry& entry : transaction)
        {
            if (entry.mode ==
                    SettingsSnapshotApplicationMode::Selector ||
                entry.requestedValue == UnavailableValue)
                continue;

            std::string liveValue;
            std::string operationError;
            if (!readValue(entry.name, liveValue, operationError))
            {
                failWithRollback(
                    SettingsSnapshotTransactionFailureStage::Apply,
                    "could not read '" + entry.name +
                    "' before applying it: " + operationError);
                return result;
            }
            if (liveValue == entry.requestedValue)
                continue;

            mutationStarted = true;
            if (!writeValue(
                    entry.name,
                    entry.requestedValue,
                    operationError))
            {
                failWithRollback(
                    SettingsSnapshotTransactionFailureStage::Apply,
                    "could not apply '" + entry.name + "': " +
                    (operationError.empty()
                        ? "no reason was reported"
                        : operationError));
                return result;
            }
            ++result.changedValueCount;
        }

        std::vector<std::string> appliedValues;
        std::string verificationError;
        if (!ReadTransactionValues(
                transaction,
                readValue,
                appliedValues,
                verificationError))
        {
            failWithRollback(
                SettingsSnapshotTransactionFailureStage::Readback,
                "could not verify applied snapshot: " + verificationError);
            return result;
        }
        for (std::size_t index = 0u; index < transaction.size(); ++index)
        {
            if (appliedValues[index] == transaction[index].requestedValue)
                continue;
            failWithRollback(
                SettingsSnapshotTransactionFailureStage::Readback,
                "applied value mismatch for '" +
                transaction[index].name + "'");
            return result;
        }

        result.succeeded = true;
        result.failureStage = SettingsSnapshotTransactionFailureStage::None;
        return result;
    }

    bool SettingsSnapshotTransactionCoordinator::IsActive() const noexcept
    {
        return m_Phase != Phase::Idle && m_Phase != Phase::Succeeded &&
            m_Phase != Phase::Failed;
    }

    void SettingsSnapshotTransactionCoordinator::Reset() noexcept
    {
        m_Transaction.clear();
        m_SourceValues.clear();
        m_TargetValues.clear();
        m_TargetCaptured.clear();
        m_Mutated.clear();
        m_Phase = Phase::Idle;
        m_Cursor = 0u;
        m_SelectorRequestIssued = false;
        m_MutationStarted = false;
        m_Result = {};
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotTransactionCoordinator::Begin(
        const std::vector<SettingsSnapshotTransactionEntry>& transaction,
        const SettingsSnapshotStagedRuntimeAccess& access)
    {
        Reset();
        if (!access.validateValue || !access.readValue || !access.writeValue)
        {
            m_Result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            m_Result.error =
                "staged settings transaction has no validator, reader, or writer";
            m_Phase = Phase::Failed;
            return { SettingsSnapshotTransactionProgress::Failed, m_Result, {} };
        }

        std::set<std::string, std::less<>> names;
        for (const SettingsSnapshotTransactionEntry& entry : transaction)
        {
            if (entry.name.empty() || !names.insert(entry.name).second ||
                (entry.mode == SettingsSnapshotApplicationMode::Selector) !=
                    IsSelectorName(entry.name))
            {
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Configuration;
                m_Result.error =
                    "staged settings transaction has an invalid or duplicate entry";
                m_Phase = Phase::Failed;
                return {
                    SettingsSnapshotTransactionProgress::Failed,
                    m_Result,
                    {}
                };
            }
            if ((entry.mode == SettingsSnapshotApplicationMode::Mutable ||
                    entry.mode == SettingsSnapshotApplicationMode::Selector) &&
                entry.requestedValue == UnavailableValue)
            {
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                m_Result.error = "setting '" + entry.name +
                    "' cannot use the unavailable sentinel";
                m_Phase = Phase::Failed;
                return {
                    SettingsSnapshotTransactionProgress::Failed,
                    m_Result,
                    {}
                };
            }

            std::string validationError;
            if (entry.mode == SettingsSnapshotApplicationMode::Selector &&
                !ValidateSettingsSnapshotSelectorToken(
                    entry.name, entry.requestedValue, validationError))
            {
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                m_Result.error = "snapshot preflight rejected '" +
                    entry.name + "': " + validationError;
                m_Phase = Phase::Failed;
                return {
                    SettingsSnapshotTransactionProgress::Failed,
                    m_Result,
                    {}
                };
            }
            if (entry.mode != SettingsSnapshotApplicationMode::Selector &&
                entry.requestedValue != UnavailableValue &&
                !access.validateValue(
                    entry.name, entry.requestedValue, validationError))
            {
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                m_Result.error = "snapshot preflight rejected '" +
                    entry.name + "': " +
                    (validationError.empty()
                        ? "no reason was reported"
                        : validationError);
                m_Phase = Phase::Failed;
                return {
                    SettingsSnapshotTransactionProgress::Failed,
                    m_Result,
                    {}
                };
            }
        }

        m_Transaction = transaction;
        m_SourceValues.reserve(transaction.size());
        for (const SettingsSnapshotTransactionEntry& entry : transaction)
        {
            std::string value;
            std::string readError;
            if (!access.readValue(entry.name, value, readError))
            {
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Capture;
                m_Result.error = "could not capture '" + entry.name +
                    "': " + (readError.empty()
                        ? "no reason was reported"
                        : readError);
                m_Phase = Phase::Failed;
                return {
                    SettingsSnapshotTransactionProgress::Failed,
                    m_Result,
                    {}
                };
            }
            if ((entry.mode == SettingsSnapshotApplicationMode::Mutable ||
                    entry.mode == SettingsSnapshotApplicationMode::Selector) &&
                value == UnavailableValue)
            {
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Capture;
                m_Result.error = "required setting '" + entry.name +
                    "' is unavailable";
                m_Phase = Phase::Failed;
                return {
                    SettingsSnapshotTransactionProgress::Failed,
                    m_Result,
                    {}
                };
            }
            if (entry.mode == SettingsSnapshotApplicationMode::Selector)
            {
                std::string tokenError;
                if (!ValidateSettingsSnapshotSelectorToken(
                        entry.name, value, tokenError))
                {
                    m_Result.failureStage =
                        SettingsSnapshotTransactionFailureStage::Capture;
                    m_Result.error = "selector GET returned a noncanonical "
                        "token for '" + entry.name + "': " + tokenError;
                    m_Phase = Phase::Failed;
                    return {
                        SettingsSnapshotTransactionProgress::Failed,
                        m_Result,
                        {}
                    };
                }
            }
            else if (value != UnavailableValue)
            {
                std::string validationError;
                if (!access.validateValue(
                        entry.name, value, validationError))
                {
                    m_Result.failureStage =
                        SettingsSnapshotTransactionFailureStage::Capture;
                    m_Result.error = "captured value for '" + entry.name +
                        "' is not canonical: " +
                        (validationError.empty()
                            ? "no reason was reported"
                            : validationError);
                    m_Phase = Phase::Failed;
                    return {
                        SettingsSnapshotTransactionProgress::Failed,
                        m_Result,
                        {}
                    };
                }
            }
            m_SourceValues.push_back(std::move(value));
        }

        m_TargetValues.resize(transaction.size());
        m_TargetCaptured.assign(transaction.size(), false);
        m_Mutated.assign(transaction.size(), false);
        m_Phase = Phase::ApplyAdapter;
        return Advance(access);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotTransactionCoordinator::Resume(
        const SettingsSnapshotRestartHandoff& handoff,
        const SettingsSnapshotStagedRuntimeAccess& access)
    {
        Reset();
        if (!access.validateValue || !access.readValue || !access.writeValue ||
            handoff.transaction.size() != handoff.sourceValues.size() ||
            handoff.changedValueCount > handoff.transaction.size() ||
            static_cast<unsigned int>(handoff.failureStage) >
                static_cast<unsigned int>(
                    SettingsSnapshotTransactionFailureStage::Rollback) ||
            (!handoff.rollingBack &&
                (handoff.changedValueCount != 1u ||
                 handoff.failureStage !=
                    SettingsSnapshotTransactionFailureStage::None ||
                 !handoff.failure.empty())) ||
            (handoff.rollingBack &&
                (handoff.failureStage ==
                    SettingsSnapshotTransactionFailureStage::None ||
                 handoff.failure.empty())))
        {
            m_Result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            m_Result.error = "invalid settings restart handoff";
            m_Phase = Phase::Failed;
            return { SettingsSnapshotTransactionProgress::Failed, m_Result, {} };
        }

        std::set<std::string, std::less<>> names;
        std::size_t differingValueCount = 0u;
        bool adapterTransition = false;
        std::size_t adapterIndex = handoff.transaction.size();
        for (std::size_t index = 0u;
             index < handoff.transaction.size();
             ++index)
        {
            const SettingsSnapshotTransactionEntry& entry =
                handoff.transaction[index];
            std::string validationError;
            if (entry.name.empty() || !names.insert(entry.name).second ||
                (entry.mode == SettingsSnapshotApplicationMode::Selector) !=
                    IsSelectorName(entry.name) ||
                ((entry.mode == SettingsSnapshotApplicationMode::Mutable ||
                     entry.mode == SettingsSnapshotApplicationMode::Selector) &&
                    (entry.requestedValue == UnavailableValue ||
                     handoff.sourceValues[index] == UnavailableValue)) ||
                (entry.mode == SettingsSnapshotApplicationMode::Selector &&
                    !ValidateSettingsSnapshotSelectorToken(
                        entry.name, entry.requestedValue, validationError)) ||
                (entry.mode == SettingsSnapshotApplicationMode::Selector &&
                    !ValidateSettingsSnapshotSelectorToken(
                        entry.name,
                        handoff.sourceValues[index],
                        validationError)) ||
                (entry.mode != SettingsSnapshotApplicationMode::Selector &&
                    entry.requestedValue != UnavailableValue &&
                    !access.validateValue(
                        entry.name,
                        entry.requestedValue,
                        validationError)))
            {
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                m_Result.error = "restart handoff preflight rejected '" +
                    entry.name + "': " + validationError;
                m_Phase = Phase::Failed;
                return {
                    SettingsSnapshotTransactionProgress::Failed,
                    m_Result,
                    {}
                };
            }
            if (entry.mode != SettingsSnapshotApplicationMode::Selector &&
                handoff.sourceValues[index] != UnavailableValue &&
                !access.validateValue(
                    entry.name,
                    handoff.sourceValues[index],
                    validationError))
            {
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::Preflight;
                m_Result.error = "restart handoff source rejected '" +
                    entry.name + "': " + validationError;
                m_Phase = Phase::Failed;
                return {
                    SettingsSnapshotTransactionProgress::Failed,
                    m_Result,
                    {}
                };
            }
            if (entry.requestedValue != handoff.sourceValues[index])
            {
                ++differingValueCount;
                if (entry.name == "gpu.adapter")
                {
                    adapterTransition = true;
                    adapterIndex = index;
                }
            }
        }
        if (!adapterTransition || handoff.changedValueCount == 0u ||
            handoff.changedValueCount > differingValueCount)
        {
            m_Result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            m_Result.error =
                "settings restart handoff has an impossible change count";
            m_Phase = Phase::Failed;
            return { SettingsSnapshotTransactionProgress::Failed, m_Result, {} };
        }

        std::string activeAdapter;
        std::string adapterError;
        const std::string& expectedAdapter = handoff.rollingBack
            ? handoff.sourceValues[adapterIndex]
            : handoff.transaction[adapterIndex].requestedValue;
        if (!access.readValue(
                "gpu.adapter", activeAdapter, adapterError) ||
            activeAdapter != expectedAdapter)
        {
            m_Result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            m_Result.error = adapterError.empty()
                ? "adapter restart did not activate the journal target"
                : "could not validate the adapter restart target: " +
                    adapterError;
            m_Phase = Phase::Failed;
            return { SettingsSnapshotTransactionProgress::Failed, m_Result, {} };
        }

        m_Transaction = handoff.transaction;
        m_SourceValues = handoff.sourceValues;
        m_TargetValues.resize(m_Transaction.size());
        m_TargetCaptured.assign(m_Transaction.size(), false);
        m_Mutated.assign(m_Transaction.size(), false);
        m_MutationStarted = true;
        m_Result.changedValueCount = handoff.changedValueCount;
        m_Result.failureStage = handoff.failureStage;
        m_Result.error = handoff.failure;
        m_Result.rollbackAttempted = handoff.rollingBack;
        m_Phase = handoff.rollingBack
            ? Phase::RollbackAdapter
            : Phase::ApplyAdapter;
        return Advance(access);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotTransactionCoordinator::Advance(
        const SettingsSnapshotStagedRuntimeAccess& access)
    {
        const auto step = [this](
            SettingsSnapshotTransactionProgress progress,
            std::string waitingFor = {})
        {
            return SettingsSnapshotTransactionStep{
                progress, m_Result, std::move(waitingFor) };
        };
        if (m_Phase == Phase::Succeeded)
            return step(SettingsSnapshotTransactionProgress::Succeeded);
        if (m_Phase == Phase::Failed || m_Phase == Phase::Idle)
            return step(SettingsSnapshotTransactionProgress::Failed);
        if (!access.validateValue || !access.readValue || !access.writeValue)
        {
            m_Result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            m_Result.error = "staged settings transaction lost runtime access";
            m_Phase = Phase::Failed;
            return step(SettingsSnapshotTransactionProgress::Failed);
        }

        const auto findEntry = [this](std::string_view name)
        {
            for (std::size_t index = 0u; index < m_Transaction.size(); ++index)
            {
                if (m_Transaction[index].name == name)
                    return index;
            }
            return m_Transaction.size();
        };
        const auto startFailure = [this](
            SettingsSnapshotTransactionFailureStage stage,
            std::string error)
        {
            m_Result.failureStage = stage;
            m_Result.error = std::move(error);
            if (!m_MutationStarted)
            {
                m_Phase = Phase::Failed;
                return;
            }
            m_Result.rollbackAttempted = true;
            m_Phase = Phase::RollbackTargetDependents;
            m_Cursor = m_Transaction.size();
            m_SelectorRequestIssued = false;
        };
        const auto rollbackFailure = [this](std::string error)
        {
            m_Result.rollbackAttempted = true;
            m_Result.rollbackSucceeded = false;
            m_Result.error += "; rollback failed: " + std::move(error);
            m_Phase = Phase::Failed;
        };
        const auto read = [&access](
            const SettingsSnapshotTransactionEntry& entry,
            std::string& value,
            std::string& error)
        {
            error.clear();
            return access.readValue(entry.name, value, error);
        };
        const auto writeIfDifferent = [&access, &read](
            const SettingsSnapshotTransactionEntry& entry,
            std::string_view desired,
            std::string& error)
        {
            std::string current;
            if (!read(entry, current, error))
                return false;
            if (current == desired)
                return true;
            if (!access.writeValue(entry.name, desired, error))
                return false;
            current.clear();
            if (!read(entry, current, error))
                return false;
            if (current != desired)
            {
                error = "readback mismatch for '" + entry.name + "'";
                return false;
            }
            return true;
        };

        for (;;)
        {
            const auto advanceSelector = [&](
                std::string_view name,
                std::string_view desired,
                bool rollback,
                Phase nextPhase)
                -> SettingsSnapshotTransactionStep
            {
                const std::size_t index = findEntry(name);
                if (index == m_Transaction.size())
                {
                    m_Phase = nextPhase;
                    m_Cursor = 0u;
                    m_SelectorRequestIssued = false;
                    return step(SettingsSnapshotTransactionProgress::Succeeded);
                }

                const SettingsSnapshotTransactionEntry& entry =
                    m_Transaction[index];
                std::string current;
                std::string operationError;
                if (!read(entry, current, operationError))
                {
                    if (rollback)
                        rollbackFailure("could not read selector '" +
                            entry.name + "': " + operationError);
                    else
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Selector,
                            "could not read selector '" + entry.name +
                                "': " + operationError);
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }
                if (!m_SelectorRequestIssued && current == desired)
                {
                    m_Phase = nextPhase;
                    m_Cursor = 0u;
                    m_SelectorRequestIssued = false;
                    return step(SettingsSnapshotTransactionProgress::Succeeded);
                }
                if (!access.driveSelector)
                {
                    if (rollback)
                        rollbackFailure("no selector driver can restore '" +
                            entry.name + "'");
                    else
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Selector,
                            "no selector driver can apply '" + entry.name + "'");
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }

                const bool begin = !m_SelectorRequestIssued;
                if (begin && name == "gpu.adapter" &&
                    !access.persistRestartHandoff)
                {
                    if (rollback)
                        rollbackFailure(
                            "no restart handoff writer can restore gpu.adapter");
                    else
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Selector,
                            "no restart handoff writer can apply gpu.adapter");
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }

                const bool mutationStartedBefore = m_MutationStarted;
                const bool mutatedBefore = m_Mutated[index];
                const std::size_t changedBefore = m_Result.changedValueCount;
                const SettingsSnapshotSelectorTransition transition =
                    access.driveSelector(
                        name, desired, begin, rollback, operationError);
                if (transition == SettingsSnapshotSelectorTransition::Failed)
                {
                    if (rollback)
                        rollbackFailure("selector rollback failed for '" +
                            entry.name + "': " + operationError);
                    else
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Selector,
                            "selector transition failed for '" + entry.name +
                                "': " + operationError);
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }

                m_SelectorRequestIssued = true;
                m_MutationStarted = true;
                m_Mutated[index] = true;
                if (!rollback && begin)
                    ++m_Result.changedValueCount;
                if (transition ==
                    SettingsSnapshotSelectorTransition::RestartRequired)
                {
                    if (name != "gpu.adapter")
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Selector,
                            "only gpu.adapter may require a process restart");
                        return step(SettingsSnapshotTransactionProgress::Failed);
                    }
                    if (begin)
                    {
                        SettingsSnapshotRestartHandoff handoff;
                        handoff.transaction = m_Transaction;
                        handoff.sourceValues = m_SourceValues;
                        handoff.rollingBack = rollback;
                        handoff.changedValueCount =
                            m_Result.changedValueCount;
                        handoff.failureStage = m_Result.failureStage;
                        handoff.failure = m_Result.error;
                        if (!access.persistRestartHandoff(
                                handoff, operationError))
                        {
                            std::string cancellationError;
                            const SettingsSnapshotSelectorTransition cancelled =
                                access.driveSelector(
                                    name,
                                    current,
                                    true,
                                    true,
                                    cancellationError);
                            m_MutationStarted = mutationStartedBefore;
                            m_Mutated[index] = mutatedBefore;
                            m_Result.changedValueCount = changedBefore;
                            const std::string failure =
                                "could not persist adapter " +
                                std::string(rollback ? "rollback" : "restart") +
                                " handoff: " + operationError +
                                (cancelled ==
                                        SettingsSnapshotSelectorTransition::Failed
                                    ? "; staged adapter cancellation failed: " +
                                        cancellationError
                                    : std::string{});
                            if (rollback)
                                rollbackFailure(failure);
                            else
                                startFailure(
                                    SettingsSnapshotTransactionFailureStage::Selector,
                                    failure);
                            return step(
                                SettingsSnapshotTransactionProgress::Failed);
                        }
                    }
                    return step(
                        SettingsSnapshotTransactionProgress::RestartRequired,
                        std::string(name));
                }
                if (transition == SettingsSnapshotSelectorTransition::Pending)
                {
                    return step(
                        SettingsSnapshotTransactionProgress::Pending,
                        std::string(name));
                }

                current.clear();
                operationError.clear();
                if (!read(entry, current, operationError) ||
                    current != desired)
                {
                    const std::string failure = operationError.empty()
                        ? "selector reported ready without publishing its token"
                        : operationError;
                    if (rollback)
                        rollbackFailure("selector rollback readback failed for '" +
                            entry.name + "': " + failure);
                    else
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Readback,
                            "selector readback failed for '" + entry.name +
                                "': " + failure);
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }
                m_Phase = nextPhase;
                m_Cursor = 0u;
                m_SelectorRequestIssued = false;
                return step(SettingsSnapshotTransactionProgress::Succeeded);
            };

            switch (m_Phase)
            {
            case Phase::ApplyAdapter:
            {
                const std::size_t index = findEntry("gpu.adapter");
                const std::string_view desired = index == m_Transaction.size()
                    ? std::string_view{}
                    : std::string_view(m_Transaction[index].requestedValue);
                const auto selectorStep = advanceSelector(
                    "gpu.adapter", desired, false, Phase::ApplyScene);
                if (m_Phase == Phase::ApplyScene)
                    continue;
                if (m_Phase == Phase::RollbackTargetDependents)
                    continue;
                return selectorStep;
            }
            case Phase::ApplyScene:
            {
                const std::size_t index = findEntry("scene.current");
                const std::string_view desired = index == m_Transaction.size()
                    ? std::string_view{}
                    : std::string_view(m_Transaction[index].requestedValue);
                const auto selectorStep = advanceSelector(
                    "scene.current", desired, false, Phase::ApplyLight);
                if (m_Phase == Phase::ApplyLight)
                    continue;
                if (m_Phase == Phase::RollbackTargetDependents)
                    continue;
                return selectorStep;
            }
            case Phase::ApplyLight:
            {
                const std::size_t index = findEntry("light.selected");
                const std::string_view desired = index == m_Transaction.size()
                    ? std::string_view{}
                    : std::string_view(m_Transaction[index].requestedValue);
                const auto selectorStep = advanceSelector(
                    "light.selected", desired, false, Phase::CaptureLight);
                if (m_Phase == Phase::CaptureLight)
                    continue;
                if (m_Phase == Phase::RollbackTargetDependents)
                    continue;
                return selectorStep;
            }
            case Phase::CaptureLight:
            case Phase::CaptureMaterial:
            {
                const bool light = m_Phase == Phase::CaptureLight;
                for (; m_Cursor < m_Transaction.size(); ++m_Cursor)
                {
                    const SettingsSnapshotTransactionEntry& entry =
                        m_Transaction[m_Cursor];
                    if (entry.mode !=
                            SettingsSnapshotApplicationMode::LiveDependent ||
                        (light
                            ? !IsLightDependent(entry.name)
                            : !IsMaterialDependent(entry.name)))
                    {
                        continue;
                    }
                    std::string current;
                    std::string operationError;
                    if (!read(entry, current, operationError))
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Capture,
                            "could not capture target value '" + entry.name +
                                "': " + operationError);
                        break;
                    }
                    if (current != UnavailableValue &&
                        !access.validateValue(
                            entry.name, current, operationError))
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Capture,
                            "target value for '" + entry.name +
                                "' is not canonical: " + operationError);
                        break;
                    }
                    m_TargetValues[m_Cursor] = current;
                    m_TargetCaptured[m_Cursor] = true;
                    const bool requestedUnavailable =
                        entry.requestedValue == UnavailableValue;
                    const bool targetUnavailable = current == UnavailableValue;
                    if (requestedUnavailable != targetUnavailable)
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Preflight,
                            "target object availability does not match '" +
                                entry.name + "'");
                        break;
                    }
                }
                if (m_Phase == Phase::RollbackTargetDependents ||
                    m_Phase == Phase::Failed)
                    continue;
                m_Cursor = 0u;
                m_Phase = light
                    ? Phase::ApplyMaterial
                    : Phase::ApplyDependents;
                continue;
            }
            case Phase::ApplyMaterial:
            {
                const std::size_t index = findEntry("material.selected");
                const std::string_view desired = index == m_Transaction.size()
                    ? std::string_view{}
                    : std::string_view(m_Transaction[index].requestedValue);
                const auto selectorStep = advanceSelector(
                    "material.selected", desired, false,
                    Phase::CaptureMaterial);
                if (m_Phase == Phase::CaptureMaterial)
                    continue;
                if (m_Phase == Phase::RollbackTargetDependents)
                    continue;
                return selectorStep;
            }
            case Phase::ApplyDependents:
            case Phase::ApplyRemaining:
            {
                const bool dependents = m_Phase == Phase::ApplyDependents;
                for (; m_Cursor < m_Transaction.size(); ++m_Cursor)
                {
                    const SettingsSnapshotTransactionEntry& entry =
                        m_Transaction[m_Cursor];
                    if ((entry.mode ==
                            SettingsSnapshotApplicationMode::LiveDependent) !=
                            dependents ||
                        entry.mode == SettingsSnapshotApplicationMode::Selector)
                    {
                        continue;
                    }

                    if (dependents && !m_TargetCaptured[m_Cursor])
                    {
                        std::string current;
                        std::string operationError;
                        if (!read(entry, current, operationError))
                        {
                            startFailure(
                                SettingsSnapshotTransactionFailureStage::Capture,
                                "could not capture target value '" +
                                    entry.name + "': " + operationError);
                            break;
                        }
                        if (current != UnavailableValue &&
                            !access.validateValue(
                                entry.name, current, operationError))
                        {
                            startFailure(
                                SettingsSnapshotTransactionFailureStage::Capture,
                                "target value for '" + entry.name +
                                    "' is not canonical: " + operationError);
                            break;
                        }
                        m_TargetValues[m_Cursor] = current;
                        m_TargetCaptured[m_Cursor] = true;
                        if ((entry.requestedValue == UnavailableValue) !=
                            (current == UnavailableValue))
                        {
                            startFailure(
                                SettingsSnapshotTransactionFailureStage::Preflight,
                                "target object availability does not match '" +
                                    entry.name + "'");
                            break;
                        }
                    }
                    if (entry.requestedValue == UnavailableValue)
                        continue;

                    std::string current;
                    std::string operationError;
                    if (!read(entry, current, operationError))
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Apply,
                            "could not read '" + entry.name +
                                "' before applying it: " + operationError);
                        break;
                    }
                    if (current == entry.requestedValue)
                        continue;
                    m_MutationStarted = true;
                    m_Mutated[m_Cursor] = true;
                    if (!access.writeValue(
                            entry.name,
                            entry.requestedValue,
                            operationError))
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Apply,
                            "could not apply '" + entry.name + "': " +
                                (operationError.empty()
                                    ? "no reason was reported"
                                    : operationError));
                        break;
                    }
                    ++m_Result.changedValueCount;
                }
                if (m_Phase == Phase::RollbackTargetDependents ||
                    m_Phase == Phase::Failed)
                    continue;
                m_Cursor = 0u;
                m_Phase = dependents ? Phase::ApplyRemaining : Phase::Verify;
                continue;
            }
            case Phase::Verify:
            {
                for (; m_Cursor < m_Transaction.size(); ++m_Cursor)
                {
                    const SettingsSnapshotTransactionEntry& entry =
                        m_Transaction[m_Cursor];
                    std::string current;
                    std::string operationError;
                    if (!read(entry, current, operationError) ||
                        current != entry.requestedValue)
                    {
                        startFailure(
                            SettingsSnapshotTransactionFailureStage::Readback,
                            "applied value mismatch for '" + entry.name +
                                "'" + (operationError.empty()
                                    ? std::string{}
                                    : ": " + operationError));
                        break;
                    }
                }
                if (m_Phase == Phase::RollbackTargetDependents ||
                    m_Phase == Phase::Failed)
                    continue;
                m_Result.succeeded = true;
                m_Result.failureStage =
                    SettingsSnapshotTransactionFailureStage::None;
                m_Phase = Phase::Succeeded;
                return step(SettingsSnapshotTransactionProgress::Succeeded);
            }
            case Phase::RollbackTargetDependents:
            {
                while (m_Cursor > 0u)
                {
                    --m_Cursor;
                    const SettingsSnapshotTransactionEntry& entry =
                        m_Transaction[m_Cursor];
                    if (entry.mode !=
                            SettingsSnapshotApplicationMode::LiveDependent ||
                        !m_Mutated[m_Cursor] ||
                        !m_TargetCaptured[m_Cursor] ||
                        m_TargetValues[m_Cursor] == UnavailableValue)
                    {
                        continue;
                    }
                    std::string operationError;
                    if (!writeIfDifferent(
                            entry, m_TargetValues[m_Cursor], operationError))
                    {
                        rollbackFailure(
                            "could not restore target object value '" +
                            entry.name + "': " + operationError);
                        break;
                    }
                }
                if (m_Phase == Phase::Failed)
                    continue;
                m_Phase = Phase::RollbackAdapter;
                m_Cursor = 0u;
                continue;
            }
            case Phase::RollbackAdapter:
            case Phase::RollbackScene:
            case Phase::RollbackLight:
            case Phase::RollbackMaterial:
            {
                std::string_view name;
                Phase next = Phase::Failed;
                if (m_Phase == Phase::RollbackAdapter)
                {
                    name = "gpu.adapter";
                    next = Phase::RollbackScene;
                }
                else if (m_Phase == Phase::RollbackScene)
                {
                    name = "scene.current";
                    next = Phase::RollbackLight;
                }
                else if (m_Phase == Phase::RollbackLight)
                {
                    name = "light.selected";
                    next = Phase::RollbackOriginalLightDependents;
                }
                else
                {
                    name = "material.selected";
                    next = Phase::RollbackOriginalMaterialDependents;
                }
                const std::size_t index = findEntry(name);
                const std::string_view desired = index == m_Transaction.size()
                    ? std::string_view{}
                    : std::string_view(m_SourceValues[index]);
                const auto selectorStep =
                    advanceSelector(name, desired, true, next);
                if (m_Phase == next)
                    continue;
                return selectorStep;
            }
            case Phase::RollbackOriginalLightDependents:
            case Phase::RollbackOriginalMaterialDependents:
            case Phase::RollbackRemaining:
            {
                const bool light =
                    m_Phase == Phase::RollbackOriginalLightDependents;
                const bool material =
                    m_Phase == Phase::RollbackOriginalMaterialDependents;
                for (; m_Cursor < m_Transaction.size(); ++m_Cursor)
                {
                    const SettingsSnapshotTransactionEntry& entry =
                        m_Transaction[m_Cursor];
                    const bool belongs = light
                        ? entry.mode ==
                                SettingsSnapshotApplicationMode::LiveDependent &&
                            IsLightDependent(entry.name)
                        : material
                            ? entry.mode ==
                                    SettingsSnapshotApplicationMode::LiveDependent &&
                                IsMaterialDependent(entry.name)
                            : entry.mode !=
                                    SettingsSnapshotApplicationMode::Selector &&
                                !(entry.mode ==
                                        SettingsSnapshotApplicationMode::LiveDependent &&
                                    (IsLightDependent(entry.name) ||
                                     IsMaterialDependent(entry.name)));
                    if (!belongs ||
                        m_SourceValues[m_Cursor] == UnavailableValue)
                    {
                        continue;
                    }
                    std::string operationError;
                    if (!writeIfDifferent(
                            entry, m_SourceValues[m_Cursor], operationError))
                    {
                        rollbackFailure("could not restore source value '" +
                            entry.name + "': " + operationError);
                        break;
                    }
                }
                if (m_Phase == Phase::Failed)
                    continue;
                m_Cursor = 0u;
                m_Phase = light
                    ? Phase::RollbackMaterial
                    : material
                        ? Phase::RollbackRemaining
                        : Phase::RollbackVerify;
                continue;
            }
            case Phase::RollbackVerify:
            {
                for (; m_Cursor < m_Transaction.size(); ++m_Cursor)
                {
                    std::string current;
                    std::string operationError;
                    if (!read(
                            m_Transaction[m_Cursor],
                            current,
                            operationError) ||
                        current != m_SourceValues[m_Cursor])
                    {
                        rollbackFailure(
                            "source-state verification failed for '" +
                            m_Transaction[m_Cursor].name + "'");
                        break;
                    }
                }
                if (m_Phase == Phase::Failed &&
                    !m_Result.rollbackSucceeded)
                {
                    return step(SettingsSnapshotTransactionProgress::Failed);
                }
                m_Result.rollbackSucceeded = true;
                m_Result.succeeded = false;
                m_Phase = Phase::Failed;
                return step(SettingsSnapshotTransactionProgress::Failed);
            }
            case Phase::Succeeded:
                return step(SettingsSnapshotTransactionProgress::Succeeded);
            case Phase::Failed:
            case Phase::Idle:
                return step(SettingsSnapshotTransactionProgress::Failed);
            }
        }
    }
}
