#include "settings_value.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace uvsr
{
    namespace
    {
        bool ParseCanonicalUnsigned(std::string_view token, std::uint64_t& value)
        {
            if (token.empty())
                return false;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            return result.ec == std::errc{} && result.ptr == token.data() + token.size() &&
                std::to_string(value) == token;
        }
    }
    bool UiSettingsValue::operator==(const UiSettingsValue& other) const noexcept
    {
        if (kind != other.kind)
            return false;
        switch (kind)
        {
        case UiSettingsValueKind::Boolean: return boolean == other.boolean;
        case UiSettingsValueKind::Integer: return integer == other.integer;
        case UiSettingsValueKind::Float: return scalar == other.scalar;
        case UiSettingsValueKind::Vector:
            return componentCount == other.componentCount && componentCount <= vector.size() &&
                std::equal(vector.begin(), vector.begin() + componentCount, other.vector.begin());
        case UiSettingsValueKind::Token:
        case UiSettingsValueKind::Selector: return text == other.text;
        }
        return false;
    }

    bool GetDeclaredUiSettingsDefaultValue(
        const UiSettingsCommandDefinition& definition,
        UiSettingsValue& value) noexcept
    {
        const UiSettingsDefaultValue& declared =
            definition.typedDefault.value;
        switch (declared.kind)
        {
        case UiSettingsDefaultValueKind::Boolean:
            value = UiSettingsValue::Boolean(declared.boolean);
            return true;
        case UiSettingsDefaultValueKind::Integer:
            value = UiSettingsValue::Integer(declared.integer);
            return true;
        case UiSettingsDefaultValueKind::Float:
            value = UiSettingsValue::Float(declared.scalar);
            return true;
        case UiSettingsDefaultValueKind::Vector:
            value = UiSettingsValue::Vector(
                declared.vector, declared.componentCount);
            return true;
        case UiSettingsDefaultValueKind::Token:
            value = UiSettingsValue::Token(std::string(declared.text));
            return true;
        case UiSettingsDefaultValueKind::Selector:
            value = UiSettingsValue::Selector(std::string(declared.text));
            return true;
        case UiSettingsDefaultValueKind::None:
            return false;
        }
        return false;
    }

    bool ValidateSettingsSnapshotSelectorToken(
        SettingId id,
        std::string_view token,
        std::string& error)
    {
        error.clear();
        std::uint64_t numeric = 0u;
        if (id == SettingId::GpuAdapter)
        {
            if (ParseCanonicalUnsigned(token, numeric) &&
                numeric <= static_cast<std::uint64_t>(
                    (std::numeric_limits<int>::max)()))
                return true;
            error = "gpu.adapter snapshot token must be a canonical "
                "non-negative adapter index";
            return false;
        }
        if (id == SettingId::SceneCurrent)
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
        if (id == SettingId::LightSelected)
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
        if (id == SettingId::MaterialSelected)
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
        error = "unknown settings selector id";
        return false;
    }

    namespace
    {
        bool InDomain(double value, const UiSettingsTypedDomain& domain,
            SettingsSnapshotValidationContext context)
        {
            const double maximum = domain.hasContextMaximum && context.hasMaterialBaseTexture &&
                context.materialHasBaseTexture ? domain.contextMaximum : domain.maximum;
            return std::isfinite(value) && ((domain.hasAlternative && value == domain.alternative) ||
                !domain.hasRange || (value >= domain.minimum && value <= maximum));
        }

        std::string CanonicalText(const UiSettingsValue& value)
        {
            switch (value.kind)
            {
            case UiSettingsValueKind::Boolean: return value.boolean ? "on" : "off";
            case UiSettingsValueKind::Integer: return std::to_string(value.integer);
            case UiSettingsValueKind::Float: return FormatUiSettingsMetadataFloat(value.scalar);
            case UiSettingsValueKind::Token:
            case UiSettingsValueKind::Selector: return value.text;
            case UiSettingsValueKind::Vector:
            {
                std::string canonical;
                for (std::uint8_t index = 0u; index < value.componentCount; ++index)
                {
                    if (index != 0u)
                        canonical.push_back(' ');
                    canonical += FormatUiSettingsMetadataFloat(value.vector[index]);
                }
                return canonical;
            }
            }
            return {};
        }
    }

    bool ParseCanonicalSettingsFloat(std::string_view text, float& value)
    {
        if (text.empty())
            return false;
        const std::string owned(text);
        char* end = nullptr;
        errno = 0;
        value = std::strtof(owned.c_str(), &end);
        return errno != ERANGE && end == owned.c_str() + owned.size() &&
            std::isfinite(value) && FormatUiSettingsMetadataFloat(value) == text;
    }

    bool ValidateUiSettingsValue(const UiSettingsCommandDefinition& definition,
        const UiSettingsValue& value, std::string& error, SettingsSnapshotValidationContext context)
    {
        const auto& domain = definition.typedDomain;
        bool valid = false;
        switch (domain.kind)
        {
        case UiSettingsDomainKind::Boolean:
            valid = value.kind == UiSettingsValueKind::Boolean;
            break;
        case UiSettingsDomainKind::Integer:
            valid = value.kind == UiSettingsValueKind::Integer &&
                InDomain(static_cast<double>(value.integer), domain, context);
            break;
        case UiSettingsDomainKind::Float:
            valid = value.kind == UiSettingsValueKind::Float && InDomain(value.scalar, domain, context);
            break;
        case UiSettingsDomainKind::Float3:
        {
            constexpr std::uint8_t count = 3u;
            valid = value.kind == UiSettingsValueKind::Vector && value.componentCount == count;
            for (std::uint8_t index = 0u; valid && index < count; ++index)
                valid = InDomain(value.vector[index], domain, context);
            break;
        }
        case UiSettingsDomainKind::Enumeration:
            valid = value.kind == UiSettingsValueKind::Token && std::find(domain.tokens.begin(),
                domain.tokens.begin() + domain.tokenCount, value.text) != domain.tokens.begin() + domain.tokenCount;
            break;
        case UiSettingsDomainKind::Selector:
            valid = value.kind == UiSettingsValueKind::Selector && !value.text.empty();
            break;
        case UiSettingsDomainKind::Action:
            break;
        }
        if (!valid)
            error = "setting '" + std::string(definition.name) + "' is outside its typed domain " +
                std::string(domain.presentation);
        return valid;
    }

    bool ParseCanonicalUiSettingsValue(const UiSettingsCommandDefinition& definition,
        std::string_view canonical, UiSettingsValue& value, std::string& error,
        SettingsSnapshotValidationContext context)
    {
        error.clear();
        const auto reject = [&] {
            error = "snapshot value '" + std::string(canonical) + "' is outside '" +
                std::string(definition.name) + "' canonical domain " + std::string(definition.typedDomain.presentation);
            return false;
        };
        if (canonical.empty())
            return reject();
        UiSettingsValue parsed;
        switch (definition.typedDomain.kind)
        {
        case UiSettingsDomainKind::Boolean:
            parsed = UiSettingsValue::Boolean(canonical == "on");
            break;
        case UiSettingsDomainKind::Integer:
        {
            std::int64_t integer = 0;
            const auto result = std::from_chars(canonical.data(), canonical.data() + canonical.size(), integer);
            if (result.ec != std::errc{} || result.ptr != canonical.data() + canonical.size())
                return reject();
            parsed = UiSettingsValue::Integer(integer);
            break;
        }
        case UiSettingsDomainKind::Float:
        {
            float scalar = 0.f;
            if (!ParseCanonicalSettingsFloat(canonical, scalar))
                return reject();
            parsed = UiSettingsValue::Float(scalar);
            break;
        }
        case UiSettingsDomainKind::Float3:
        {
            constexpr std::uint8_t count = 3u;
            std::array<float, 4> components{};
            std::size_t begin = 0u;
            for (std::uint8_t index = 0u; index < count; ++index)
            {
                const auto end = canonical.find(' ', begin);
                if (!ParseCanonicalSettingsFloat(canonical.substr(begin, end == std::string_view::npos
                        ? std::string_view::npos : end - begin), components[index]))
                    return reject();
                begin = end == std::string_view::npos ? canonical.size() : end + 1u;
            }
            parsed = UiSettingsValue::Vector(components, count);
            break;
        }
        case UiSettingsDomainKind::Enumeration:
            parsed = UiSettingsValue::Token(std::string(canonical));
            break;
        case UiSettingsDomainKind::Selector:
            if (!ValidateSettingsSnapshotSelectorToken(definition.id, canonical, error))
                return false;
            parsed = UiSettingsValue::Selector(std::string(canonical));
            break;
        case UiSettingsDomainKind::Action:
            return reject();
        }
        if (!ValidateUiSettingsValue(definition, parsed, error, context) || CanonicalText(parsed) != canonical)
            return reject();
        value = std::move(parsed);
        return true;
    }

    bool ValidateSettingsSnapshotCatalogValue(const UiSettingsCommandDefinition& definition,
        std::string_view canonical, std::string& error, SettingsSnapshotValidationContext context)
    {
        UiSettingsValue parsed;
        return ParseCanonicalUiSettingsValue(definition, canonical, parsed, error, context);
    }

    bool FormatUiSettingsValue(const UiSettingsCommandDefinition& definition,
        const UiSettingsValue& value, std::string& canonical, std::string& error)
    {
        if (!ValidateUiSettingsValue(definition, value, error))
            return false;
        canonical = CanonicalText(value);
        return true;
    }

}
