#include "scene_loading.h"
#include "settings_snapshot_storage.h"
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

namespace uvsr
{
namespace
{
    constexpr size_t MaximumKeyStorage = MaximumSceneLoadTimingEntries * (MaximumSceneLoadTimingKeyBytes + 1);
#if defined(UVSR_SCENE_HISTORY_TEST_HOOKS)
    thread_local size_t AllocationsBeforeFailure = SIZE_MAX;
#endif
    bool Fail(SettingsSnapshotError& error, SettingsSnapshotErrorCode code, const char* message) noexcept
    {
        error = {code, 0, 0, message, {}};
        return false;
    }
    bool AllocationAllowed(SettingsSnapshotError& error) noexcept
    {
#if defined(UVSR_SCENE_HISTORY_TEST_HOOKS)
        if (AllocationsBeforeFailure != SIZE_MAX)
        {
            if (!AllocationsBeforeFailure)
                return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate scene loading history.");
            --AllocationsBeforeFailure;
        }
#else
        (void)error;
#endif
        return true;
    }
    bool ValidRange(std::string_view text) noexcept
    {
        return text.size() <= size_t(PTRDIFF_MAX) && (text.empty() || text.data()) &&
            text.size() <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(text.data());
    }
    int Compare(std::string_view left, std::string_view right) noexcept
    {
        const size_t common = left.size() < right.size() ? left.size() : right.size();
        const int prefix = common ? memcmp(left.data(), right.data(), common) : 0;
        return prefix ? prefix : left.size() < right.size() ? -1 : left.size() > right.size() ? 1 : 0;
    }
    bool Whitespace(char value) noexcept
    {
        return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' || value == '\f';
    }
    struct Parser
    {
        std::string_view input;
        size_t cursor = 0;
        char key[MaximumSceneLoadTimingKeyBytes + 1]{};
        void SkipWhitespace() noexcept
        {
            while (cursor < input.size() && Whitespace(input[cursor])) ++cursor;
        }
        bool Word(std::string_view& output) noexcept
        {
            SkipWhitespace();
            const size_t first = cursor;
            while (cursor < input.size() && !Whitespace(input[cursor])) ++cursor;
            if (cursor == first) return false;
            output = {input.data() + first, cursor - first};
            return true;
        }
        bool Number(uint64_t& output) noexcept
        {
            SkipWhitespace();
            bool negative = false;
            if (cursor < input.size() && (input[cursor] == '-' || input[cursor] == '+'))
                negative = input[cursor++] == '-';
            const size_t first = cursor;
            uint64_t value = 0;
            while (cursor < input.size() && input[cursor] >= '0' && input[cursor] <= '9')
            {
                const uint64_t digit = uint64_t(input[cursor] - '0');
                if (value > (UINT64_MAX - digit) / 10) return false;
                value = value * 10 + digit;
                ++cursor;
            }
            if (cursor == first) return false;
            // formatted unsigned extraction checks the magnitude, then applies
            // the sign modulo 2^64. it does not require a token delimiter.
            output = negative ? uint64_t(0) - value : value;
            return true;
        }
        bool Key(std::string_view& output) noexcept
        {
            SkipWhitespace();
            if (cursor == input.size()) return false;
            if (input[cursor] != '"') return Word(output) && IsValidSceneLoadTimingKey(output);
            ++cursor;
            size_t length = 0;
            while (cursor < input.size())
            {
                char value = input[cursor++];
                if (value == '"')
                {
                    key[length] = '\0';
                    output = {key, length};
                    return length != 0;
                }
                if (value == '\\')
                {
                    if (cursor == input.size()) return false;
                    value = input[cursor++];
                }
                if (static_cast<unsigned char>(value) < 0x20 || length == MaximumSceneLoadTimingKeyBytes)
                    return false;
                key[length++] = value;
            }
            return false;
        }
    };
}

    bool IsValidSceneLoadTimingKey(std::string_view key) noexcept
    {
        if (key.empty() || key.size() > MaximumSceneLoadTimingKeyBytes || !ValidRange(key)) return false;
        for (unsigned char value : key) if (value < 0x20) return false;
        return true;
    }
    struct SceneLoadTimingDatabase::Entry
    {
        std::string_view key;
        SceneLoadTimingHistory history;
    };
    SceneLoadTimingDatabase::~SceneLoadTimingDatabase() noexcept { Clear(); }
    SceneLoadTimingDatabase::SceneLoadTimingDatabase(SceneLoadTimingDatabase&& other) noexcept
        : allScenes(other.allScenes), m_Entries(other.m_Entries), m_Text(other.m_Text),
          m_Count(other.m_Count), m_KeyBytes(other.m_KeyBytes)
    {
        other.allScenes = {}; other.m_Entries = nullptr; other.m_Text = nullptr;
        other.m_Count = 0; other.m_KeyBytes = 0;
    }
    SceneLoadTimingDatabase& SceneLoadTimingDatabase::operator=(SceneLoadTimingDatabase&& other) noexcept
    {
        if (this != &other)
        {
            Clear(); allScenes = other.allScenes; m_Entries = other.m_Entries; m_Text = other.m_Text;
            m_Count = other.m_Count; m_KeyBytes = other.m_KeyBytes;
            other.allScenes = {}; other.m_Entries = nullptr; other.m_Text = nullptr;
            other.m_Count = 0; other.m_KeyBytes = 0;
        }
        return *this;
    }
    void SceneLoadTimingDatabase::Clear() noexcept
    {
        delete[] m_Entries; free(m_Text);
        m_Entries = nullptr; m_Text = nullptr; m_Count = 0; m_KeyBytes = 0; allScenes = {};
    }
    size_t SceneLoadTimingDatabase::LowerBound(std::string_view key) const noexcept
    {
        size_t first = 0, count = m_Count;
        while (count)
        {
            const size_t step = count / 2, index = first + step;
            if (Compare(m_Entries[index].key, key) < 0) { first = index + 1; count -= step + 1; }
            else count = step;
        }
        return first;
    }
    const SceneLoadTimingHistory* SceneLoadTimingDatabase::Find(std::string_view key) const noexcept
    {
        if (!IsValidSceneLoadTimingKey(key)) return nullptr;
        const size_t index = LowerBound(key);
        return index < m_Count && Compare(m_Entries[index].key, key) == 0 ? &m_Entries[index].history : nullptr;
    }
    bool SceneLoadTimingDatabase::Allocate(size_t count, size_t keyBytes, SettingsSnapshotError& error) noexcept
    {
        if (count > MaximumSceneLoadTimingEntries || keyBytes > MaximumKeyStorage ||
            (count == 0) != (keyBytes == 0))
            return Fail(error, SettingsSnapshotErrorCode::Capacity, "Scene loading history exceeds its storage bounds.");
        if (!count) return true;
        if (!AllocationAllowed(error)) return false;
        m_Entries = new(std::nothrow) Entry[count];
        if (!m_Entries) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate scene loading history entries.");
        m_Count = count;
        if (!AllocationAllowed(error)) return false;
        m_Text = static_cast<char*>(malloc(keyBytes));
        if (!m_Text) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not allocate scene loading history keys.");
        m_KeyBytes = keyBytes;
        return true;
    }
    void SceneLoadTimingDatabase::Store(size_t index, size_t& offset,
        std::string_view key, SceneLoadTimingHistory history) noexcept
    {
        char* destination = m_Text + offset;
        memcpy(destination, key.data(), key.size());
        destination[key.size()] = '\0';
        m_Entries[index] = {{destination, key.size()}, history};
        offset += key.size() + 1;
    }
    bool SceneLoadTimingDatabase::Record(std::string_view key, uint64_t elapsedMilliseconds,
        SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail)); error = {};
        if (!IsValidSceneLoadTimingKey(key))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "Scene loading history rejected the key.");
        const size_t insert = LowerBound(key);
        if (insert < m_Count && Compare(m_Entries[insert].key, key) == 0)
        {
            RecordSceneLoadDuration(m_Entries[insert].history, elapsedMilliseconds);
            return true;
        }
        size_t victim = m_Count;
        if (m_Count == MaximumSceneLoadTimingEntries)
        {
            victim = 0;
            for (size_t index = 1; index < m_Count; ++index)
            {
                const auto left = m_Entries[index].history, right = m_Entries[victim].history;
                // sorted keys make the first entry win an equal count/time tie.
                if (left.completedLoadCount < right.completedLoadCount ||
                    (left.completedLoadCount == right.completedLoadCount && left.totalMilliseconds < right.totalMilliseconds))
                    victim = index;
            }
        }
        const size_t bytes = m_KeyBytes + key.size() + 1 -
            (victim < m_Count ? m_Entries[victim].key.size() + 1 : 0);
        SceneLoadTimingDatabase candidate;
        if (!candidate.Allocate(m_Count + (victim == m_Count ? 1 : 0), bytes, error)) return false;
        candidate.allScenes = allScenes;
        size_t written = 0, offset = 0;
        for (size_t index = 0; index <= m_Count; ++index)
        {
            if (index == insert) candidate.Store(written++, offset, key, {elapsedMilliseconds, 1});
            if (index < m_Count && index != victim)
                candidate.Store(written++, offset, m_Entries[index].key, m_Entries[index].history);
        }
        *this = std::move(candidate);
        return true;
    }

    struct SceneHistoryBuilder
    {
        struct Pending
        {
            size_t offset = 0, size = 0;
            SceneLoadTimingHistory history;
        };
        Pending entries[MaximumSceneLoadTimingEntries]{};
        size_t count = 0, used = 0, capacity = 0;
        char* keys = nullptr;
        SceneLoadTimingHistory allScenes;
        ~SceneHistoryBuilder() noexcept { free(keys); }
        std::string_view Key(size_t index) const noexcept { return {keys + entries[index].offset, entries[index].size}; }
        bool Add(std::string_view key, SceneLoadTimingHistory history, SettingsSnapshotError& error) noexcept
        {
            if (count == MaximumSceneLoadTimingEntries)
                return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "Scene loading history contains too many entries.");
            size_t first = 0, remaining = count;
            while (remaining)
            {
                const size_t step = remaining / 2, index = first + step;
                if (Compare(Key(index), key) < 0) { first = index + 1; remaining -= step + 1; }
                else remaining = step;
            }
            if (first < count && Compare(Key(first), key) == 0)
                return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "Scene loading history contains a duplicate key.");
            const size_t required = used + key.size() + 1;
            if (required > MaximumKeyStorage)
                return Fail(error, SettingsSnapshotErrorCode::Capacity, "Scene loading history exceeds its key storage bound.");
            if (required > capacity)
            {
                size_t grown = capacity ? capacity : 256;
                while (grown < required)
                    grown = grown > MaximumKeyStorage / 2 ? MaximumKeyStorage : grown * 2;
                if (!AllocationAllowed(error)) return false;
                void* allocation = realloc(keys, grown);
                if (!allocation) return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "Could not prepare scene loading history keys.");
                keys = static_cast<char*>(allocation); capacity = grown;
            }
            memcpy(keys + used, key.data(), key.size()); keys[used + key.size()] = '\0';
            for (size_t index = count; index > first; --index) entries[index] = entries[index - 1];
            entries[first] = {used, key.size(), history};
            used = required; ++count;
            return true;
        }
        bool Parse(std::string_view input, SettingsSnapshotError& error) noexcept
        {
            Parser parser{input}; std::string_view token; uint64_t version = 0;
            const auto invalid = [&]() noexcept {
                return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "Scene loading history is invalid.");
            };
            if (!ValidRange(input) || !parser.Word(token) || token != "UVSR_SCENE_LOAD_HISTORY" ||
                !parser.Number(version) || version != SceneLoadTimingDatabaseVersion) return invalid();
            bool readAll = false;
            while (parser.Word(token))
            {
                uint64_t total = 0, completed = 0;
                if (token == "all")
                {
                    if (readAll || !parser.Number(total) || !parser.Number(completed) || completed > UINT32_MAX)
                        return invalid();
                    allScenes = {total, static_cast<uint32_t>(completed)};
                    if (!IsValidSceneLoadTimingHistory(allScenes)) return invalid();
                    readAll = true;
                    continue;
                }
                std::string_view key;
                if (token != "scene" || count == MaximumSceneLoadTimingEntries || !parser.Key(key) ||
                    !parser.Number(total) || !parser.Number(completed) || !completed || completed > UINT32_MAX)
                    return invalid();
                if (!Add(key, {total, static_cast<uint32_t>(completed)}, error)) return false;
            }
            return readAll || invalid();
        }
        bool Publish(SceneLoadTimingDatabase& output, SettingsSnapshotError& error) const noexcept
        {
            SceneLoadTimingDatabase candidate;
            if (!candidate.Allocate(count, used, error)) return false;
            candidate.allScenes = allScenes;
            size_t offset = 0;
            for (size_t index = 0; index < count; ++index)
                candidate.Store(index, offset, Key(index), entries[index].history);
            output = std::move(candidate);
            return true;
        }
    };
    bool ReadSceneLoadTimingDatabase(std::string_view input, SceneLoadTimingDatabase& output,
        SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(std::move(error.detail)); error = {};
        SceneHistoryBuilder candidate;
        return candidate.Parse(input, error) && candidate.Publish(output, error);
    }
    bool WriteSceneLoadTimingDatabase(const SceneLoadTimingDatabase& database,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept
    {
        if (&output == &error.detail) return false;
        json::EncodedText previousDetail(std::move(error.detail)); error = {};
        if (!IsValidSceneLoadTimingHistory(database.allScenes))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "Scene loading history has invalid global timing.");
        json::EncodedText candidate([](json::OutputWriter& writer, const void* context) noexcept {
            const auto& value = *static_cast<const SceneLoadTimingDatabase*>(context);
            if (!writer.Raw("UVSR_SCENE_LOAD_HISTORY ") || !writer.Unsigned(SceneLoadTimingDatabaseVersion) ||
                !writer.Raw("\nall ") || !writer.Unsigned(value.allScenes.totalMilliseconds) || !writer.Raw(" ") ||
                !writer.Unsigned(value.allScenes.completedLoadCount) || !writer.Raw("\n")) return false;
            for (size_t index = 0; index < value.m_Count; ++index)
            {
                const auto& entry = value.m_Entries[index];
                if (!writer.Raw("scene ") || !writer.String({entry.key.data(), entry.key.size()}) || !writer.Raw(" ") ||
                    !writer.Unsigned(entry.history.totalMilliseconds) || !writer.Raw(" ") ||
                    !writer.Unsigned(entry.history.completedLoadCount) || !writer.Raw("\n")) return false;
            }
            return true;
        }, &database);
        if (!candidate.IsValid())
        {
            const auto failure = candidate.Failure();
            return Fail(error, failure.code == json::ErrorCode::OutOfMemory ? SettingsSnapshotErrorCode::OutOfMemory :
                failure.code == json::ErrorCode::Capacity ? SettingsSnapshotErrorCode::Capacity : SettingsSnapshotErrorCode::InvalidInput,
                failure.message);
        }
        output = std::move(candidate);
        return true;
    }
#if defined(UVSR_SCENE_HISTORY_TEST_HOOKS)
    void FailSceneHistoryAllocationAfter(size_t count) noexcept { AllocationsBeforeFailure = count; }
    void ClearSceneHistoryAllocationFailure() noexcept { AllocationsBeforeFailure = SIZE_MAX; }
#endif
}
