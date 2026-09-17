#include "settings_value.h"
#include "settings_snapshot_internal.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_SETTINGS_VALUE_TEST_HOOKS)
        thread_local size_t allocationsLeft = SIZE_MAX;
#endif
        bool Fail(SettingsSnapshotError& error, SettingsSnapshotErrorCode code,
            const char* message) noexcept
        {
            error = {};
            error.code = code;
            error.message = message;
            return false;
        }
        bool TextFailure(SettingsSnapshotError& error, const json::Error& failure) noexcept
        {
            return Fail(error, failure.code == json::ErrorCode::OutOfMemory
                ? SettingsSnapshotErrorCode::OutOfMemory
                : failure.code == json::ErrorCode::Capacity ? SettingsSnapshotErrorCode::Capacity
                : SettingsSnapshotErrorCode::Format, failure.message);
        }
        bool PublishText(json::EncodedText&& candidate, json::EncodedText& output,
            SettingsSnapshotError& error) noexcept
        {
            if (!candidate.IsValid()) return TextFailure(error, candidate.Failure());
            output = static_cast<json::EncodedText&&>(candidate);
            return true;
        }
        bool ParseCanonicalUnsigned(std::string_view token, uint64_t& value) noexcept
        {
            if (token.empty()) return false;
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) return false;
            char text[21];
            const auto formatted = std::to_chars(text, text + sizeof(text), value);
            return formatted.ec == std::errc{} && std::string_view(text, size_t(formatted.ptr - text)) == token;
        }
        bool InDomain(double value, const UiSettingsTypedDomain& domain,
            SettingsSnapshotValidationContext context) noexcept
        {
            const double maximum = domain.hasContextMaximum && context.hasMaterialBaseTexture &&
                context.materialHasBaseTexture ? domain.contextMaximum : domain.maximum;
            return std::isfinite(value) && ((domain.hasAlternative && value == domain.alternative) ||
                !domain.hasRange || (value >= domain.minimum && value <= maximum));
        }
        struct CanonicalText
        {
            // the shared float formatter writes at most 64 bytes per component.
            char bytes[4 * 64 + 3];
            size_t length = 0;
            bool inlineBytes = false;
            std::string_view borrowed;
            std::string_view View() const noexcept
            {
                return inlineBytes ? std::string_view(bytes, length) : borrowed;
            }
        };
        bool PrepareCanonical(const UiSettingsValue& value, CanonicalText& text,
            SettingsSnapshotError& error) noexcept
        {
            switch (value.kind)
            {
            case UiSettingsValueKind::Boolean: text.borrowed = value.boolean ? "on" : "off"; return true;
            case UiSettingsValueKind::Token:
            case UiSettingsValueKind::Selector: text.borrowed = value.Text(); return true;
            case UiSettingsValueKind::Integer:
            {
                const auto formatted = std::to_chars(text.bytes, text.bytes + sizeof(text.bytes), value.integer);
                if (formatted.ec != std::errc{}) return Fail(error, SettingsSnapshotErrorCode::Format, "cannot format setting integer");
                text.inlineBytes = true; text.length = size_t(formatted.ptr - text.bytes); return true;
            }
            case UiSettingsValueKind::Float:
            case UiSettingsValueKind::Vector:
            {
                const size_t count = value.kind == UiSettingsValueKind::Float ? 1 : value.componentCount;
                if (count > value.vector.size()) return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid setting vector size");
                text.inlineBytes = true;
                for (size_t index = 0; index < count; ++index)
                {
                    const auto component = FormatUiSettingsMetadataFloat(value.kind == UiSettingsValueKind::Float ? value.scalar : value.vector[index]);
                    if (!component.IsValid() || component.Size() > 64)
                        return Fail(error, SettingsSnapshotErrorCode::Format, "cannot format setting number");
                    if (index) text.bytes[text.length++] = ' ';
                    if (component.Size() > sizeof(text.bytes) - text.length)
                        return Fail(error, SettingsSnapshotErrorCode::Capacity, "setting vector text exceeds its proven capacity");
                    memcpy(text.bytes + text.length, component.View().data(), component.Size());
                    text.length += component.Size();
                }
                return true;
            }
            }
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid setting value kind");
        }
        bool EmitCanonical(json::OutputWriter& writer, const void* context) noexcept
        {
            const auto text = static_cast<const CanonicalText*>(context)->View();
            return writer.Raw({text.data(), text.size()});
        }
        bool RejectDomain(const UiSettingsCommandDefinition& definition,
            SettingsSnapshotError& error) noexcept
        {
            json::EncodedText text([](json::OutputWriter& writer, const void* context) noexcept {
                const auto& setting = *static_cast<const UiSettingsCommandDefinition*>(context);
                return writer.Raw("setting '") && writer.Raw({setting.name.data(), setting.name.size()}) &&
                    writer.Raw("' is outside its typed domain ") &&
                    writer.Raw({setting.typedDomain.presentation.data(), setting.typedDomain.presentation.size()});
            }, &definition);
            if (!text.IsValid()) return TextFailure(error, text.Failure());
            error.code = SettingsSnapshotErrorCode::InvalidInput;
            error.detail = static_cast<json::EncodedText&&>(text);
            return false;
        }
        bool RejectCanonical(const UiSettingsCommandDefinition& definition, std::string_view canonical,
            SettingsSnapshotError& error) noexcept
        {
            struct Rejection { const UiSettingsCommandDefinition& definition; std::string_view canonical; } rejection{definition, canonical};
            json::EncodedText text([](json::OutputWriter& writer, const void* context) noexcept {
                const auto& failure = *static_cast<const Rejection*>(context);
                const auto& setting = failure.definition;
                return writer.Raw("snapshot value '") && writer.Raw({failure.canonical.data(), failure.canonical.size()}) &&
                    writer.Raw("' is outside '") && writer.Raw({setting.name.data(), setting.name.size()}) &&
                    writer.Raw("' canonical domain ") &&
                    writer.Raw({setting.typedDomain.presentation.data(), setting.typedDomain.presentation.size()});
            }, &rejection);
            if (!text.IsValid()) return TextFailure(error, text.Failure());
            error = {};
            error.code = SettingsSnapshotErrorCode::InvalidInput;
            error.detail = static_cast<json::EncodedText&&>(text);
            return false;
        }
    }

    SettingsSnapshotText::~SettingsSnapshotText() noexcept { free(m_LongText); }
    SettingsSnapshotText::SettingsSnapshotText(SettingsSnapshotText&& other) noexcept
    {
        *this = static_cast<SettingsSnapshotText&&>(other);
    }
    SettingsSnapshotText& SettingsSnapshotText::operator=(SettingsSnapshotText&& other) noexcept
    {
        if (this == &other) return *this;
        free(m_LongText);
        memcpy(m_ShortText, other.m_ShortText, sizeof(m_ShortText));
        m_LongText = other.m_LongText; m_TextSize = other.m_TextSize;
        other.m_LongText = nullptr; other.m_TextSize = 0; other.m_ShortText[0] = '\0';
        return *this;
    }
    bool SettingsSnapshotText::StoreParts(std::initializer_list<std::string_view> parts, SettingsSnapshotError& error) noexcept
    {
        // only unpublished candidates call this helper; the old output stays live.
        size_t size = 0;
        for (const auto part : parts)
        {
            if (!settings_snapshot_detail::ValidText(part))
                return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid setting value text range");
            if (part.size() >= size_t(PTRDIFF_MAX) - size)
                return Fail(error, SettingsSnapshotErrorCode::Capacity, "setting value text exceeds addressable storage");
            size += part.size();
        }
        char* destination = m_ShortText;
        if (size >= sizeof(m_ShortText))
        {
#if defined(UVSR_SETTINGS_VALUE_TEST_HOOKS)
            if (!allocationsLeft) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "cannot allocate setting value text");
            if (allocationsLeft != SIZE_MAX) --allocationsLeft;
#endif
            m_LongText = static_cast<char*>(malloc(size + 1));
            if (!m_LongText) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "cannot allocate setting value text");
            destination = m_LongText;
        }
        size_t offset = 0;
        for (const auto part : parts)
        {
            if (!part.empty()) memcpy(destination + offset, part.data(), part.size());
            offset += part.size();
        }
        destination[size] = '\0'; m_TextSize = size;
        return true;
    }
    bool SettingsSnapshotText::Assign(std::string_view text, SettingsSnapshotError& error) noexcept
    {
        return AssignParts({text}, error);
    }
    bool SettingsSnapshotText::AssignParts(std::initializer_list<std::string_view> parts, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        SettingsSnapshotText candidate;
        if (!candidate.StoreParts(parts, error)) return false;
        *this = static_cast<SettingsSnapshotText&&>(candidate);
        return true;
    }
    bool SettingsSnapshotText::CloneTo(SettingsSnapshotText& output, SettingsSnapshotError& error) const noexcept
    {
        if (this == &output)
        {
            error = {};
            return true;
        }
        return output.Assign(View(), error);
    }
    UiSettingsValue::UiSettingsValue(UiSettingsValue&& other) noexcept { *this = static_cast<UiSettingsValue&&>(other); }
    UiSettingsValue& UiSettingsValue::operator=(UiSettingsValue&& other) noexcept
    {
        if (this == &other) return *this;
        kind = other.kind; boolean = other.boolean; integer = other.integer; scalar = other.scalar;
        vector = other.vector; componentCount = other.componentCount;
        m_Text = static_cast<SettingsSnapshotText&&>(other.m_Text);
        return *this;
    }
    bool UiSettingsValue::SetText(UiSettingsValueKind type, std::string_view text, SettingsSnapshotError& error) noexcept
    {
        UiSettingsValue candidate;
        candidate.kind = type;
        if (!candidate.m_Text.Assign(text, error)) return false;
        *this = static_cast<UiSettingsValue&&>(candidate);
        return true;
    }
    bool UiSettingsValue::SetToken(std::string_view text, SettingsSnapshotError& error) noexcept
    {
        return SetText(UiSettingsValueKind::Token, text, error);
    }
    bool UiSettingsValue::SetSelector(std::string_view text, SettingsSnapshotError& error) noexcept
    {
        return SetText(UiSettingsValueKind::Selector, text, error);
    }
    bool UiSettingsValue::SetSelectorParts(std::initializer_list<std::string_view> parts, SettingsSnapshotError& error) noexcept
    {
        UiSettingsValue candidate;
        candidate.kind = UiSettingsValueKind::Selector;
        if (!candidate.m_Text.AssignParts(parts, error)) return false;
        *this = static_cast<UiSettingsValue&&>(candidate);
        return true;
    }
    bool UiSettingsValue::CloneTo(UiSettingsValue& output, SettingsSnapshotError& error) const noexcept
    {
        error = {};
        if (this == &output) return true;
        UiSettingsValue candidate;
        candidate.kind = kind; candidate.boolean = boolean; candidate.integer = integer; candidate.scalar = scalar;
        candidate.vector = vector; candidate.componentCount = componentCount;
        if (!m_Text.CloneTo(candidate.m_Text, error)) return false;
        output = static_cast<UiSettingsValue&&>(candidate);
        return true;
    }
    bool UiSettingsValue::operator==(const UiSettingsValue& other) const noexcept
    {
        if (kind != other.kind) return false;
        switch (kind)
        {
        case UiSettingsValueKind::Boolean: return boolean == other.boolean;
        case UiSettingsValueKind::Integer: return integer == other.integer;
        case UiSettingsValueKind::Float: return scalar == other.scalar;
        case UiSettingsValueKind::Vector:
            if (componentCount != other.componentCount || componentCount > vector.size()) return false;
            for (size_t index = 0; index < componentCount; ++index) if (vector[index] != other.vector[index]) return false;
            return true;
        case UiSettingsValueKind::Token:
        case UiSettingsValueKind::Selector: return Text() == other.Text();
        }
        return false;
    }
    bool GetDeclaredUiSettingsDefaultValue(const UiSettingsCommandDefinition& definition,
        UiSettingsValue& value, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        const auto& declared = definition.typedDefault.value;
        switch (declared.kind)
        {
        case UiSettingsDefaultValueKind::Boolean: value = UiSettingsValue::Boolean(declared.boolean); return true;
        case UiSettingsDefaultValueKind::Integer: value = UiSettingsValue::Integer(declared.integer); return true;
        case UiSettingsDefaultValueKind::Float: value = UiSettingsValue::Float(declared.scalar); return true;
        case UiSettingsDefaultValueKind::Vector: value = UiSettingsValue::Vector(declared.vector, declared.componentCount); return true;
        case UiSettingsDefaultValueKind::Token: return value.SetToken(declared.text, error);
        case UiSettingsDefaultValueKind::Selector: return value.SetSelector(declared.text, error);
        case UiSettingsDefaultValueKind::None: return false;
        }
        return false;
    }
    bool ValidateSettingsSnapshotSelectorToken(SettingId id, std::string_view token,
        SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(token))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid setting selector text range");
        uint64_t numeric = 0;
        if (id == SettingId::GpuAdapter)
        {
            if (ParseCanonicalUnsigned(token, numeric) && numeric <= uint64_t((std::numeric_limits<int>::max)())) return true;
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput,
                "gpu.adapter snapshot token must be a canonical non-negative adapter index");
        }
        if (id == SettingId::SceneCurrent)
        {
            if (token.empty() || token.front() == '/' || token.find("./") == 0 ||
                token.find('\\') != std::string_view::npos || token.find(':') != std::string_view::npos ||
                token.find("//") != std::string_view::npos || token.find("/./") != std::string_view::npos ||
                token.find("../") != std::string_view::npos ||
                (token.size() >= 3 && token.substr(token.size() - 3) == "/..") || token == "." || token == ".." ||
                token.size() < sizeof(".scene.json") - 1 || token.substr(token.size() - (sizeof(".scene.json") - 1)) != ".scene.json")
                return Fail(error, SettingsSnapshotErrorCode::InvalidInput,
                    "scene.current snapshot token must be a normalized relative .scene.json filename");
            return true;
        }
        if (id == SettingId::LightSelected)
        {
            const size_t separator = token.find(':');
            if (separator != std::string_view::npos && separator && separator + 1 < token.size() &&
                ParseCanonicalUnsigned(token.substr(0, separator), numeric) && numeric <= uint64_t(SIZE_MAX) &&
                token.find('\n', separator + 1) == std::string_view::npos && token.find('\r', separator + 1) == std::string_view::npos)
                return true;
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput,
                "light.selected snapshot token must be <zero-based-index>:<stable-identity>");
        }
        if (id == SettingId::MaterialSelected)
        {
            if (token == "none" || (ParseCanonicalUnsigned(token, numeric) && numeric <= UINT32_MAX)) return true;
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput,
                "material.selected snapshot token must be a canonical runtime material id or none");
        }
        return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "unknown settings selector id");
    }
    bool ParseCanonicalSettingsFloat(std::string_view text, float& value, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (text.empty()) return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "empty canonical setting number");
        UiSettingsValue terminated;
        if (!terminated.SetToken(text, error)) return false;
        const auto input = terminated.Text();
        char* end = nullptr;
        errno = 0;
        value = strtof(input.data(), &end);
        if (errno != ERANGE && end == input.data() + input.size() && std::isfinite(value) &&
            FormatUiSettingsMetadataFloat(value).View() == text) return true;
        return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid canonical setting number");
    }
    namespace
    {
        bool ValueInDomain(const UiSettingsCommandDefinition& definition, const UiSettingsValue& value,
            SettingsSnapshotValidationContext context) noexcept
        {
            const auto& domain = definition.typedDomain;
            bool valid = false;
            switch (domain.kind)
            {
            case UiSettingsDomainKind::Boolean: valid = value.kind == UiSettingsValueKind::Boolean; break;
            case UiSettingsDomainKind::Integer: valid = value.kind == UiSettingsValueKind::Integer && InDomain(double(value.integer), domain, context); break;
            case UiSettingsDomainKind::Float: valid = value.kind == UiSettingsValueKind::Float && InDomain(value.scalar, domain, context); break;
            case UiSettingsDomainKind::Float3:
                valid = value.kind == UiSettingsValueKind::Vector && value.componentCount == 3;
                for (uint8_t index = 0; valid && index < 3; ++index) valid = InDomain(value.vector[index], domain, context);
                break;
            case UiSettingsDomainKind::Enumeration:
                if (domain.tokenCount > domain.tokens.size())
                    return false;
                if (value.kind == UiSettingsValueKind::Token)
                    for (size_t index = 0; index < domain.tokenCount; ++index) if (domain.tokens[index] == value.Text()) { valid = true; break; }
                break;
            case UiSettingsDomainKind::Selector: valid = value.kind == UiSettingsValueKind::Selector && !value.Text().empty(); break;
            case UiSettingsDomainKind::Action: break;
            }
            return valid;
        }
    }
    bool ValidateUiSettingsValue(const UiSettingsCommandDefinition& definition,
        const UiSettingsValue& value, SettingsSnapshotError& error, SettingsSnapshotValidationContext context) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        return ValueInDomain(definition, value, context) || RejectDomain(definition, error);
    }
    bool ParseCanonicalUiSettingsValue(const UiSettingsCommandDefinition& definition, std::string_view canonical,
        UiSettingsValue& value, SettingsSnapshotError& error, SettingsSnapshotValidationContext context) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(canonical))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid canonical setting text range");
        const auto reject = [&]() noexcept { return RejectCanonical(definition, canonical, error); };
        if (canonical.empty()) return reject();
        UiSettingsValue parsed;
        switch (definition.typedDomain.kind)
        {
        case UiSettingsDomainKind::Boolean: parsed = UiSettingsValue::Boolean(canonical == "on"); break;
        case UiSettingsDomainKind::Integer:
        {
            int64_t integer = 0;
            const auto result = std::from_chars(canonical.data(), canonical.data() + canonical.size(), integer);
            if (result.ec != std::errc{} || result.ptr != canonical.data() + canonical.size()) return reject();
            parsed = UiSettingsValue::Integer(integer); break;
        }
        case UiSettingsDomainKind::Float:
        {
            float scalar = 0;
            if (!ParseCanonicalSettingsFloat(canonical, scalar, error))
                return error.code == SettingsSnapshotErrorCode::OutOfMemory || error.code == SettingsSnapshotErrorCode::Capacity ? false : reject();
            parsed = UiSettingsValue::Float(scalar); break;
        }
        case UiSettingsDomainKind::Float3:
        {
            std::array<float, 4> components{};
            size_t begin = 0;
            for (uint8_t index = 0; index < 3; ++index)
            {
                const auto end = canonical.find(' ', begin);
                if (!ParseCanonicalSettingsFloat(canonical.substr(begin, end == std::string_view::npos
                        ? std::string_view::npos : end - begin), components[index], error))
                    return error.code == SettingsSnapshotErrorCode::OutOfMemory || error.code == SettingsSnapshotErrorCode::Capacity ? false : reject();
                begin = end == std::string_view::npos ? canonical.size() : end + 1;
            }
            parsed = UiSettingsValue::Vector(components, 3); break;
        }
        case UiSettingsDomainKind::Enumeration:
            if (!parsed.SetToken(canonical, error)) return false;
            break;
        case UiSettingsDomainKind::Selector:
            if (!ValidateSettingsSnapshotSelectorToken(definition.id, canonical, error) || !parsed.SetSelector(canonical, error)) return false;
            break;
        case UiSettingsDomainKind::Action: return reject();
        }
        if (!ValueInDomain(definition, parsed, context)) return reject();
        CanonicalText formatted;
        if (!PrepareCanonical(parsed, formatted, error)) return false;
        if (formatted.View() != canonical) return reject();
        value = static_cast<UiSettingsValue&&>(parsed);
        return true;
    }
    bool ValidateSettingsSnapshotCatalogValue(const UiSettingsCommandDefinition& definition,
        std::string_view canonical, SettingsSnapshotError& error, SettingsSnapshotValidationContext context) noexcept
    {
        UiSettingsValue parsed;
        return ParseCanonicalUiSettingsValue(definition, canonical, parsed, error, context);
    }
    bool FormatUiSettingsValue(const UiSettingsCommandDefinition& definition, const UiSettingsValue& value,
        json::EncodedText& canonical, SettingsSnapshotError& error) noexcept
    {
        if (!ValidateUiSettingsValue(definition, value, error)) return false;
        CanonicalText prepared;
        if (!PrepareCanonical(value, prepared, error)) return false;
        return PublishText(json::EncodedText(EmitCanonical, &prepared), canonical, error);
    }
    bool FormatUiSettingsValue(const UiSettingsCommandDefinition& definition, const UiSettingsValue& value,
        SettingsSnapshotText& canonical, SettingsSnapshotError& error) noexcept
    {
        if (!ValidateUiSettingsValue(definition, value, error)) return false;
        CanonicalText prepared;
        if (!PrepareCanonical(value, prepared, error)) return false;
        return canonical.Assign(prepared.View(), error);
    }
#if defined(UVSR_SETTINGS_VALUE_TEST_HOOKS)
    void FailUiSettingsValueAllocationAfter(size_t count) noexcept { allocationsLeft = count; }
    void ClearUiSettingsValueAllocationFailure() noexcept { allocationsLeft = SIZE_MAX; }
#endif
}
