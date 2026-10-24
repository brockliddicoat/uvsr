#include "settings_snapshot_decoder.h"
#include "settings_snapshot_internal.h"

#include <new>
#include <utility>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
        thread_local size_t allocationsBeforeFailure = SIZE_MAX;
#endif
        bool MayAllocate() noexcept
        {
#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
            if (allocationsBeforeFailure == 0) return false;
            if (allocationsBeforeFailure != SIZE_MAX) --allocationsBeforeFailure;
#endif
            return true;
        }
        int Compare(std::string_view left, std::string_view right) noexcept
        {
            const size_t common = left.size() < right.size() ? left.size() : right.size();
            const int order = common ? memcmp(left.data(), right.data(), common) : 0;
            if (order) return order;
            return left.size() < right.size() ? -1 : left.size() > right.size() ? 1 : 0;
        }
        bool Fail(SettingsSnapshotError& error, SettingsSnapshotErrorCode code,
            const char* message) noexcept
        {
            error = {};
            error.code = code;
            error.message = message;
            return false;
        }
    }

    SettingsSnapshotError ComposeSettingsSnapshotError(
        std::initializer_list<std::string_view> parts, SettingsSnapshotErrorCode code,
        uint32_t nativeCode, uint32_t cleanupCode) noexcept
    {
        SettingsSnapshotError error;
        error.code = code;
        error.nativeCode = nativeCode;
        error.cleanupCode = cleanupCode;
        error.detail = json::EncodedText([](json::OutputWriter& output,
            const void* context) noexcept {
            for (const auto part : *static_cast<const std::initializer_list<std::string_view>*>(context))
                if (!output.Raw({part.data(), part.size()})) return false;
            return true;
        }, &parts);
        if (!error.detail.IsValid())
        {
            error.code = error.detail.Failure().code == json::ErrorCode::OutOfMemory
                ? SettingsSnapshotErrorCode::OutOfMemory
                : error.detail.Failure().code == json::ErrorCode::Capacity
                    ? SettingsSnapshotErrorCode::Capacity : SettingsSnapshotErrorCode::Format;
            error.message = "snapshot operation failed; diagnostic text could not be allocated";
        }
        return error;
    }

    bool SettingsSnapshotError::CloneTo(SettingsSnapshotError& output,
        SettingsSnapshotError& failure) const noexcept
    {
        if (&failure == this || &failure == &output) return false;
        failure = {};
        if (this == &output) return true;
        SettingsSnapshotError candidate;
        candidate.code = code;
        candidate.nativeCode = nativeCode;
        candidate.cleanupCode = cleanupCode;
        candidate.message = message;
        if (detail.IsValid())
        {
            const auto view = detail.View();
            candidate.detail = json::EncodedText([](json::OutputWriter& writer,
                const void* context) noexcept {
                return writer.Raw(*static_cast<const json::TextView*>(context));
            }, &view);
            if (!candidate.detail.IsValid())
                return Fail(failure,
                    candidate.detail.Failure().code == json::ErrorCode::OutOfMemory
                        ? SettingsSnapshotErrorCode::OutOfMemory
                        : candidate.detail.Failure().code == json::ErrorCode::Capacity
                            ? SettingsSnapshotErrorCode::Capacity : SettingsSnapshotErrorCode::Format,
                    "cannot copy snapshot error text");
        }
        output = std::move(candidate);
        return true;
    }

    DecodedSettings::~DecodedSettings() noexcept { Clear(); }

    DecodedSettings::DecodedSettings(DecodedSettings&& other) noexcept
        : m_Entries(other.m_Entries), m_Count(other.m_Count), m_Capacity(other.m_Capacity)
    {
        other.m_Entries = nullptr;
        other.m_Count = other.m_Capacity = 0;
    }

    DecodedSettings& DecodedSettings::operator=(DecodedSettings&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            m_Entries = other.m_Entries;
            m_Count = other.m_Count;
            m_Capacity = other.m_Capacity;
            other.m_Entries = nullptr;
            other.m_Count = other.m_Capacity = 0;
        }
        return *this;
    }

    void DecodedSettings::Clear() noexcept
    {
        for (size_t index = 0; index < m_Count; ++index)
            free(const_cast<char*>(m_Entries[index].name.data()));
        delete[] m_Entries;
        m_Entries = nullptr;
        m_Count = m_Capacity = 0;
    }

    size_t DecodedSettings::LowerBound(std::string_view name) const noexcept
    {
        size_t first = 0;
        size_t last = m_Count;
        while (first < last)
        {
            const size_t middle = first + (last - first) / 2;
            if (Compare(m_Entries[middle].name, name) < 0) first = middle + 1;
            else last = middle;
        }
        return first;
    }

    const DecodedSetting* DecodedSettings::Find(std::string_view name) const noexcept
    {
        if (!settings_snapshot_detail::ValidText(name)) return nullptr;
        const size_t index = LowerBound(name);
        return index < m_Count && Compare(m_Entries[index].name, name) == 0
            ? m_Entries + index : nullptr;
    }

    bool DecodedSettings::Reserve(size_t count, SettingsSnapshotError& error) noexcept
    {
        if (count <= m_Capacity) return true;
        constexpr size_t maximum = size_t(PTRDIFF_MAX) / sizeof(DecodedSetting);
        if (count > maximum)
            return Fail(error, SettingsSnapshotErrorCode::Capacity, "snapshot entry capacity overflow");
        size_t capacity = m_Capacity ? m_Capacity : 8;
        while (capacity < count)
            capacity = capacity > maximum / 2 ? maximum : capacity * 2;
        auto* entries = MayAllocate() ? new (std::nothrow) DecodedSetting[capacity] : nullptr;
        if (!entries)
            return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "cannot allocate snapshot entries");
        for (size_t index = 0; index < m_Count; ++index) entries[index] = m_Entries[index];
        delete[] m_Entries;
        m_Entries = entries;
        m_Capacity = capacity;
        return true;
    }

    bool DecodedSettings::Store(std::string_view name, std::string_view value,
        bool replace, SettingsSnapshotError& error) noexcept
    {
        // inputs may borrow the previous diagnostic until copying finishes.
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(name) || !settings_snapshot_detail::ValidText(value))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot text range");
        const size_t index = LowerBound(name);
        const bool found = index < m_Count && Compare(m_Entries[index].name, name) == 0;
        if (found && !replace)
            return Fail(error, SettingsSnapshotErrorCode::Duplicate, "snapshot contains a duplicate setting");
        constexpr size_t maximum = size_t(PTRDIFF_MAX);
        if (name.size() > maximum - 2 || value.size() > maximum - 2 - name.size())
            return Fail(error, SettingsSnapshotErrorCode::Capacity, "snapshot text capacity overflow");
        const size_t size = name.size() + value.size() + 2;
        char* bytes = MayAllocate() ? static_cast<char*>(malloc(size)) : nullptr;
        if (!bytes)
            return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "cannot allocate snapshot text");
        if (name.size()) memcpy(bytes, name.data(), name.size());
        bytes[name.size()] = '\0';
        char* storedValue = bytes + name.size() + 1;
        if (value.size()) memcpy(storedValue, value.data(), value.size());
        storedValue[value.size()] = '\0';
        // copy before growing or freeing, since either input may borrow this owner.
        if (!found && !Reserve(m_Count + 1, error))
        {
            free(bytes);
            return false;
        }
        if (found) free(const_cast<char*>(m_Entries[index].name.data()));
        else
        {
            for (size_t destination = m_Count; destination > index; --destination)
                m_Entries[destination] = m_Entries[destination - 1];
            ++m_Count;
        }
        m_Entries[index] = {{bytes, name.size()}, {storedValue, value.size()}};
        return true;
    }

    bool DecodedSettings::Insert(std::string_view name, std::string_view value,
        SettingsSnapshotError& error) noexcept
    {
        return Store(name, value, false, error);
    }

    bool DecodedSettings::Set(std::string_view name, std::string_view value,
        SettingsSnapshotError& error) noexcept
    {
        return Store(name, value, true, error);
    }

    bool DecodedSettings::Erase(std::string_view name) noexcept
    {
        if (!settings_snapshot_detail::ValidText(name)) return false;
        const size_t index = LowerBound(name);
        if (index == m_Count || Compare(m_Entries[index].name, name) != 0) return false;
        free(const_cast<char*>(m_Entries[index].name.data()));
        for (size_t destination = index; destination + 1 < m_Count; ++destination)
            m_Entries[destination] = m_Entries[destination + 1];
        --m_Count;
        m_Entries[m_Count] = {};
        return true;
    }

    bool DecodedSettings::CloneTo(DecodedSettings& output, SettingsSnapshotError& error) const noexcept
    {
        error = {};
        if (&output == this) return true;
        DecodedSettings candidate;
        if (!candidate.Reserve(m_Count, error)) return false;
        for (size_t index = 0; index < m_Count; ++index)
            if (!candidate.Insert(m_Entries[index].name, m_Entries[index].value, error)) return false;
        output = static_cast<DecodedSettings&&>(candidate);
        return true;
    }

    SettingsSnapshotCatalogPaths::~SettingsSnapshotCatalogPaths() noexcept { Clear(); }
    SettingsSnapshotCatalogPaths::SettingsSnapshotCatalogPaths(SettingsSnapshotCatalogPaths&& other) noexcept
        : m_Paths(other.m_Paths), m_Count(other.m_Count), m_Capacity(other.m_Capacity)
    {
        other.m_Paths = nullptr;
        other.m_Count = other.m_Capacity = 0;
    }
    SettingsSnapshotCatalogPaths& SettingsSnapshotCatalogPaths::operator=(SettingsSnapshotCatalogPaths&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            m_Paths = other.m_Paths;
            m_Count = other.m_Count;
            m_Capacity = other.m_Capacity;
            other.m_Paths = nullptr;
            other.m_Count = other.m_Capacity = 0;
        }
        return *this;
    }
    void SettingsSnapshotCatalogPaths::Clear() noexcept
    {
        for (size_t index = 0; index < m_Count; ++index) free(m_Paths[index]);
        delete[] m_Paths;
        m_Paths = nullptr;
        m_Count = m_Capacity = 0;
    }
    const wchar_t* SettingsSnapshotCatalogPaths::Path(size_t index) const noexcept
    {
        return index < m_Count ? m_Paths[index] : L"";
    }
    bool SettingsSnapshotCatalogPaths::Append(const wchar_t* path, SettingsSnapshotError& error) noexcept
    {
        error = {};
        if (!path) return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot catalog path");
        const size_t length = wcslen(path);
        if (length >= size_t(PTRDIFF_MAX) / sizeof(wchar_t))
            return Fail(error, SettingsSnapshotErrorCode::Capacity, "snapshot catalog path capacity overflow");
        wchar_t* copy = MayAllocate() ? static_cast<wchar_t*>(malloc((length + 1) * sizeof(wchar_t))) : nullptr;
        if (!copy) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "cannot allocate snapshot catalog path");
        wmemcpy(copy, path, length + 1);
        if (m_Count == m_Capacity)
        {
            constexpr size_t maximum = size_t(PTRDIFF_MAX) / sizeof(wchar_t*);
            if (m_Count == maximum)
            {
                free(copy);
                return Fail(error, SettingsSnapshotErrorCode::Capacity, "snapshot catalog count overflow");
            }
            const size_t capacity = !m_Capacity ? 8 : m_Capacity > maximum / 2 ? maximum : m_Capacity * 2;
            auto** paths = MayAllocate() ? new (std::nothrow) wchar_t*[capacity] : nullptr;
            if (!paths)
            {
                free(copy);
                return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "cannot allocate snapshot catalog paths");
            }
            for (size_t index = 0; index < m_Count; ++index) paths[index] = m_Paths[index];
            delete[] m_Paths;
            m_Paths = paths;
            m_Capacity = capacity;
        }
        m_Paths[m_Count++] = copy;
        return true;
    }
    void SettingsSnapshotCatalogPaths::Sort() noexcept
    {
        // iterative heapsort bounds work even for a large package directory.
        const auto sift = [this](size_t root, size_t count) noexcept
        {
            while (root < count / 2)
            {
                size_t child = root * 2 + 1;
                if (child + 1 < count && wcscmp(m_Paths[child], m_Paths[child + 1]) < 0) ++child;
                if (wcscmp(m_Paths[root], m_Paths[child]) >= 0) break;
                wchar_t* temporary = m_Paths[root];
                m_Paths[root] = m_Paths[child];
                m_Paths[child] = temporary;
                root = child;
            }
        };
        for (size_t parent = m_Count / 2; parent > 0; --parent) sift(parent - 1, m_Count);
        for (size_t end = m_Count; end > 1; --end)
        {
            wchar_t* temporary = m_Paths[0];
            m_Paths[0] = m_Paths[end - 1];
            m_Paths[end - 1] = temporary;
            sift(0, end - 1);
        }
    }

    SettingsSnapshotMatches::~SettingsSnapshotMatches() noexcept { Clear(); }
    SettingsSnapshotMatches::SettingsSnapshotMatches(SettingsSnapshotMatches&& other) noexcept
        : m_Text(other.m_Text), m_Count(other.m_Count), m_Capacity(other.m_Capacity)
    {
        other.m_Text = nullptr;
        other.m_Count = other.m_Capacity = 0;
    }
    SettingsSnapshotMatches& SettingsSnapshotMatches::operator=(SettingsSnapshotMatches&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            m_Text = other.m_Text;
            m_Count = other.m_Count;
            m_Capacity = other.m_Capacity;
            other.m_Text = nullptr;
            other.m_Count = other.m_Capacity = 0;
        }
        return *this;
    }
    void SettingsSnapshotMatches::Clear() noexcept
    {
        delete[] m_Text;
        m_Text = nullptr;
        m_Count = m_Capacity = 0;
    }
    std::string_view SettingsSnapshotMatches::Text(size_t index) const noexcept
    {
        return index < m_Count ? std::string_view(m_Text[index].Data(), m_Text[index].Size()) : std::string_view{};
    }
    bool SettingsSnapshotMatches::Append(json::EncodedText&& text, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        auto& source = &text == &error.detail ? previousDetail : text;
        error = {};
        if (!source.IsValid()) return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot payload");
        if (m_Count == m_Capacity)
        {
            constexpr size_t maximum = size_t(PTRDIFF_MAX) / sizeof(json::EncodedText);
            if (m_Count == maximum)
                return Fail(error, SettingsSnapshotErrorCode::Capacity, "snapshot payload count overflow");
            const size_t capacity = !m_Capacity ? 4 : m_Capacity > maximum / 2 ? maximum : m_Capacity * 2;
            auto* values = MayAllocate() ? new (std::nothrow) json::EncodedText[capacity] : nullptr;
            if (!values) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "cannot allocate snapshot payloads");
            for (size_t index = 0; index < m_Count; ++index)
                values[index] = static_cast<json::EncodedText&&>(m_Text[index]);
            delete[] m_Text;
            m_Text = values;
            m_Capacity = capacity;
        }
        m_Text[m_Count++] = static_cast<json::EncodedText&&>(source);
        return true;
    }

#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
    void FailSettingsSnapshotAllocationAfter(size_t successfulAllocations) noexcept
    {
        allocationsBeforeFailure = successfulAllocations;
    }
    void ClearSettingsSnapshotAllocationFailure() noexcept { allocationsBeforeFailure = SIZE_MAX; }
#endif
}
