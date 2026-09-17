#include "color_lut.h"
#include "settings_snapshot_storage.h"

#include <cmath>
#include <new>
#include <errno.h>
#include <locale.h>
#include <stdlib.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_COLOR_LUT_TEST_HOOKS)
        thread_local size_t allocationsLeft = SIZE_MAX;
#endif
        bool AllowAllocation() noexcept
        {
#if defined(UVSR_COLOR_LUT_TEST_HOOKS)
            if (!allocationsLeft) { allocationsLeft = SIZE_MAX; return false; }
            if (allocationsLeft != SIZE_MAX) --allocationsLeft;
#endif
            return true;
        }
        bool Fail(SettingsSnapshotError& error, const char* message,
            SettingsSnapshotErrorCode code = SettingsSnapshotErrorCode::InvalidInput) noexcept
        {
            error = {code, 0, 0, message, {}};
            return false;
        }
        bool Space(char value) noexcept
        {
            return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
                value == '\v' || value == '\f';
        }
        void SkipSpace(std::string_view& text) noexcept
        {
            while (!text.empty() && Space(text.front())) text.remove_prefix(1);
        }
        bool Finished(std::string_view text) noexcept { SkipSpace(text); return text.empty(); }
        std::string_view ReadKey(std::string_view& text) noexcept
        {
            SkipSpace(text);
            size_t length = 0;
            while (length < text.size() && !Space(text[length])) ++length;
            const auto key = text.substr(0, length); text.remove_prefix(length); return key;
        }
        bool Digit(char value, bool hex = false) noexcept
        {
            return (value >= '0' && value <= '9') ||
                (hex && ((value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F')));
        }
        bool ReadSize(std::string_view& text, uint32_t& result) noexcept
        {
            SkipSpace(text);
            bool negative = false;
            if (!text.empty() && (text.front() == '+' || text.front() == '-'))
            { negative = text.front() == '-'; text.remove_prefix(1); }
            uint32_t value = 0; size_t count = 0;
            while (count < text.size() && Digit(text[count]))
            {
                const uint32_t digit = uint32_t(text[count] - '0');
                if (value > (UINT32_MAX - digit) / 10) return false;
                value = value * 10 + digit; ++count;
            }
            if (!count) return false;
            text.remove_prefix(count);
            // the existing Windows unsigned extraction negates after conversion.
            result = negative ? uint32_t(0) - value : value;
            return true;
        }
        enum class NumberResult { Value, Invalid, AllocationFailed };
        struct NumericLocale
        {
#if defined(_WIN32)
            _locale_t value = nullptr;
            ~NumericLocale() noexcept { if (value) _free_locale(value); }
#else
            locale_t value = nullptr;
            ~NumericLocale() noexcept { if (value) freelocale(value); }
#endif
            NumberResult Read(std::string_view& input, float& output, SettingsSnapshotError& error) noexcept
            {
                SkipSpace(input);
                char text[792];
                size_t stored = 0, end = 0;
                if (end < input.size() && (input[end] == '+' || input[end] == '-')) text[stored++] = input[end++];
                const bool hex = input.size() - end >= 2 && input[end] == '0' &&
                    (input[end + 1] == 'x' || input[end + 1] == 'X');
                text[stored++] = '0';
                if (hex) { end += 2; text[stored++] = 'x'; }
                size_t significant = 0;
                ptrdiff_t power = 0;
                bool seen = false, discarded = false;
                while (end < input.size() && Digit(input[end], hex))
                {
                    const char digit = input[end++]; seen = true;
                    if (significant == 768) { ++power; discarded |= digit != '0'; }
                    else if (digit != '0' || significant) { text[stored++] = digit; ++significant; }
                }
                if (hex && seen && !significant) text[stored++] = '0';
                if (end < input.size() && input[end] == '.') { ++end; text[stored++] = '.'; }
                if (!significant)
                    while (end < input.size() && input[end] == '0') { ++end; --power; seen = true; }
                while (end < input.size() && Digit(input[end], hex))
                {
                    const char digit = input[end++]; seen = true;
                    if (significant < 768) { text[stored++] = digit; ++significant; }
                    else discarded |= digit != '0';
                }
                if (!seen) return NumberResult::Invalid;
                // match the former stream's 768 significant digits and sticky
                // tail. UCRT hex conversion otherwise loses directed rounding.
                if (discarded)
                {
                    char& last = text[stored - (text[stored - 1] == '.' ? 2 : 1)];
                    if (last == '0' || last == (hex ? '8' : '5')) ++last;
                }
                uint64_t exponent = 0;
                bool exponentNegative = false;
                if (end < input.size() && (hex ? (input[end] == 'p' || input[end] == 'P')
                    : (input[end] == 'e' || input[end] == 'E')))
                {
                    ++end;
                    if (end < input.size() && (input[end] == '+' || input[end] == '-'))
                    { exponentNegative = input[end] == '-'; ++end; }
                    const size_t first = end;
                    while (end < input.size() && Digit(input[end]))
                    {
                        const uint64_t digit = uint64_t(input[end++] - '0');
                        exponent = exponent > (uint64_t(PTRDIFF_MAX) - digit) / 10
                            ? uint64_t(PTRDIFF_MAX) : exponent * 10 + digit;
                    }
                    if (end == first) return NumberResult::Invalid;
                }
                const uint64_t bound = hex ? 4200 : 1100, scale = hex ? 4 : 1;
                const bool powerNegative = power < 0;
                uint64_t powerMagnitude = uint64_t(powerNegative ? -power : power);
                bool negative = exponentNegative;
                uint64_t magnitude = 0;
                // cancel opposite signed terms before clamping. a huge exponent
                // may balance a long mantissa, so neither term can clamp alone.
                if (powerMagnitude > (uint64_t(PTRDIFF_MAX) + bound) / scale)
                { magnitude = bound; negative = powerNegative; }
                else
                {
                    powerMagnitude *= scale;
                    if (exponentNegative == powerNegative)
                        magnitude = exponent >= bound || powerMagnitude >= bound ? bound : exponent + powerMagnitude;
                    else if (exponent >= powerMagnitude) magnitude = exponent - powerMagnitude;
                    else { magnitude = powerMagnitude - exponent; negative = powerNegative; }
                    if (magnitude > bound) magnitude = bound;
                }
                if (magnitude)
                {
                    text[stored++] = hex ? 'p' : 'e';
                    if (negative) text[stored++] = '-';
                    char digits[4]; size_t count = 0;
                    do { digits[count++] = char('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
                    while (count) text[stored++] = digits[--count];
                }
                text[stored] = '\0';
                if (!value)
                {
                    const int previous = errno;
                    if (AllowAllocation())
                    {
#if defined(_WIN32)
                        value = _create_locale(LC_NUMERIC, "C");
#else
                        value = newlocale(LC_NUMERIC_MASK, "C", nullptr);
#endif
                    }
                    errno = previous;
                    if (!value)
                    {
                        Fail(error, "Cannot allocate film LUT numeric locale", SettingsSnapshotErrorCode::OutOfMemory);
                        return NumberResult::AllocationFailed;
                    }
                }
                // the explicit C locale keeps conversion independent of process
                // locale while preserving the current floating rounding mode.
                const int previous = errno; errno = 0; char* stopped = nullptr;
#if defined(_WIN32)
                const float result = _strtof_l(text, &stopped, value);
#else
                const float result = strtof_l(text, &stopped, value);
#endif
                const bool valid = errno == 0 && stopped == text + stored && std::isfinite(result);
                errno = previous;
                if (!valid) return NumberResult::Invalid;
                input.remove_prefix(end); output = result; return NumberResult::Value;
            }
        };
    }

    ColorLutData::~ColorLutData() noexcept { Clear(); }
    void ColorLutData::Clear() noexcept
    {
        delete[] m_Values; m_Values = nullptr; m_Count = 0; m_Size = 0;
        m_DomainMin = {0.f, 0.f, 0.f}; m_DomainMax = {1.f, 1.f, 1.f};
    }
    ColorLutData::ColorLutData(ColorLutData&& other) noexcept { *this = static_cast<ColorLutData&&>(other); }
    ColorLutData& ColorLutData::operator=(ColorLutData&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            m_Size = other.m_Size; m_DomainMin = other.m_DomainMin; m_DomainMax = other.m_DomainMax;
            m_Values = other.m_Values; m_Count = other.m_Count;
            other.m_Values = nullptr; other.Clear();
        }
        return *this;
    }

    bool ReadColorLut(std::string_view text, ColorLutData& result, SettingsSnapshotError& error) noexcept
    {
        if (text.size() > size_t(PTRDIFF_MAX))
            return Fail(error, "Film LUT input is too large", SettingsSnapshotErrorCode::Capacity);
        ColorLutData candidate;
        NumericLocale locale;
        bool hasSize = false, hasMin = false, hasMax = false;
        size_t capacity = 0;
        const auto channels = [&](std::string_view& line, float* values, const char* message) noexcept {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                const auto status = locale.Read(line, values[channel], error);
                if (status == NumberResult::AllocationFailed) return false;
                if (status == NumberResult::Invalid) return Fail(error, message);
            }
            return true;
        };
        while (!text.empty())
        {
            const size_t end = text.find('\n');
            auto line = text.substr(0, end);
            if (end == std::string_view::npos) text = {};
            else text.remove_prefix(end + 1);
            line = line.substr(0, line.find('#'));
            auto fields = line;
            const auto key = ReadKey(fields);
            if (key.empty()) continue;
            if (key == "TITLE")
            {
                if (candidate.m_Count) return Fail(error, "LUT headers must precede table values");
                continue;
            }
            if (key == "LUT_3D_SIZE")
            {
                if (hasSize || candidate.m_Count || !ReadSize(fields, candidate.m_Size) ||
                    candidate.m_Size < 2 || candidate.m_Size > 128 || !Finished(fields))
                    return Fail(error, "LUT size must be a single integer from 2 to 128");
                static_assert(sizeof(ColorLutValue) == 16);
                static_assert(size_t(128) * 128 * 128 <= size_t(PTRDIFF_MAX) / sizeof(ColorLutValue));
                capacity = size_t(candidate.m_Size) * candidate.m_Size * candidate.m_Size;
                candidate.m_Values = AllowAllocation() ? new (std::nothrow) ColorLutValue[capacity] : nullptr;
                if (!candidate.m_Values)
                    return Fail(error, "Cannot allocate film LUT table", SettingsSnapshotErrorCode::OutOfMemory);
                hasSize = true;
                continue;
            }
            if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX")
            {
                bool& seen = key == "DOMAIN_MIN" ? hasMin : hasMax;
                auto& domain = key == "DOMAIN_MIN" ? candidate.m_DomainMin : candidate.m_DomainMax;
                if (seen || candidate.m_Count)
                    return Fail(error, "LUT domain headers must occur once before table values");
                if (!channels(fields, domain.data(), "LUT domain must contain three finite values")) return false;
                if (!Finished(fields)) return Fail(error, "LUT domain contains extra values");
                seen = true;
                continue;
            }
            if (!hasSize) return Fail(error, "LUT requires a 3D size before its table");
            ColorLutValue value{0.f, 0.f, 0.f, 1.f};
            if (!channels(line, value.data(), "LUT table must contain finite RGB triples")) return false;
            if (!Finished(line) || candidate.m_Count >= capacity)
                return Fail(error, "LUT table contains extra values");
            candidate.m_Values[candidate.m_Count++] = value;
        }
        if (!hasSize || candidate.m_Count != capacity) return Fail(error, "LUT table is incomplete");
        for (size_t channel = 0; channel < 3; ++channel)
            if (!(candidate.m_DomainMax[channel] > candidate.m_DomainMin[channel]))
                return Fail(error, "LUT domain maximum must exceed its minimum");
        result = static_cast<ColorLutData&&>(candidate); error = {}; return true;
    }
#if defined(UVSR_COLOR_LUT_TEST_HOOKS)
    void FailColorLutAllocationAfter(size_t count) noexcept { allocationsLeft = count; }
    void ClearColorLutAllocationFailure() noexcept { allocationsLeft = SIZE_MAX; }
#endif
}
