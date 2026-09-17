#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace uvsr
{
    namespace json { class EncodedText; }
    struct SettingsSnapshotError;
    inline constexpr uint32_t MaximumSceneLoadWorkerCount = 8u;
    inline constexpr uint64_t SceneLoadCounterTickMilliseconds = 20u;
    inline constexpr uint32_t SceneLoadTimingDatabaseVersion = 1u;
    inline constexpr size_t MaximumSceneLoadTimingEntries = 512u;
    inline constexpr size_t MaximumSceneLoadTimingKeyBytes = 4096u;
    struct SceneLoadTimingHistory
    {
        uint64_t totalMilliseconds = 0u;
        uint32_t completedLoadCount = 0u;
    };

    [[nodiscard]] inline bool IsValidSceneLoadTimingHistory(const SceneLoadTimingHistory& history) noexcept
    {
        return history.completedLoadCount != 0u || history.totalMilliseconds == 0u;
    }
    [[nodiscard]] bool IsValidSceneLoadTimingKey(std::string_view key) noexcept;

    class SceneLoadTimingDatabase
    {
    public:
        SceneLoadTimingDatabase() noexcept = default;
        ~SceneLoadTimingDatabase() noexcept;
        SceneLoadTimingDatabase(const SceneLoadTimingDatabase&) = delete;
        SceneLoadTimingDatabase& operator=(const SceneLoadTimingDatabase&) = delete;
        SceneLoadTimingDatabase(SceneLoadTimingDatabase&& other) noexcept;
        SceneLoadTimingDatabase& operator=(SceneLoadTimingDatabase&& other) noexcept;
        SceneLoadTimingHistory allScenes;
        [[nodiscard]] size_t Count() const noexcept { return m_Count; }
        // returned history borrows this owner until Record, Clear, move or destruction.
        [[nodiscard]] const SceneLoadTimingHistory* Find(std::string_view key) const noexcept;
        // the caller records the global sample separately, as before. failure
        // preserves all per-scene records and their views, including the victim.
        [[nodiscard]] bool Record(std::string_view key, uint64_t elapsedMilliseconds,
            SettingsSnapshotError& error) noexcept;
        void Clear() noexcept;
    private:
        struct Entry;
        Entry* m_Entries = nullptr;
        char* m_Text = nullptr;
        size_t m_Count = 0;
        size_t m_KeyBytes = 0;
        [[nodiscard]] size_t LowerBound(std::string_view key) const noexcept;
        [[nodiscard]] bool Allocate(size_t count, size_t keyBytes, SettingsSnapshotError& error) noexcept;
        void Store(size_t index, size_t& offset, std::string_view key, SceneLoadTimingHistory history) noexcept;
        friend struct SceneHistoryBuilder;
        friend bool WriteSceneLoadTimingDatabase(const SceneLoadTimingDatabase&,
            json::EncodedText&, SettingsSnapshotError&) noexcept;
    };

    // input is synchronous and immutable. malformed input and allocation failure
    // preserve the published database. the saved format uses its original C locale.
    [[nodiscard]] bool ReadSceneLoadTimingDatabase(std::string_view input,
        SceneLoadTimingDatabase& output, SettingsSnapshotError& error) noexcept;
    // output must differ from error.detail; aliasing returns false without mutation.
    [[nodiscard]] bool WriteSceneLoadTimingDatabase(const SceneLoadTimingDatabase& database,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept;

#if defined(UVSR_SCENE_HISTORY_TEST_HOOKS)
    void FailSceneHistoryAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearSceneHistoryAllocationFailure() noexcept;
#endif

    [[nodiscard]] constexpr uint64_t ResolveSceneLoadElapsedTicks(
        uint64_t elapsedMilliseconds)
    {
        return elapsedMilliseconds / SceneLoadCounterTickMilliseconds;
    }

    [[nodiscard]] constexpr uint64_t ResolveAverageSceneLoadTicks(
        const SceneLoadTimingHistory& history)
    {
        if (history.completedLoadCount == 0u)
            return 0u;

        const uint64_t completedLoadCount =
            uint64_t(history.completedLoadCount);
        const uint64_t quotient =
            history.totalMilliseconds / completedLoadCount;
        const uint64_t remainder =
            history.totalMilliseconds % completedLoadCount;
        const uint64_t averageMilliseconds = quotient +
            (remainder >= (completedLoadCount + 1u) / 2u ? 1u : 0u);
        const uint64_t roundedTicks =
            averageMilliseconds / SceneLoadCounterTickMilliseconds +
            (averageMilliseconds % SceneLoadCounterTickMilliseconds >=
                SceneLoadCounterTickMilliseconds / 2u
                    ? 1u
                    : 0u);
        return roundedTicks > 0u ? roundedTicks : 1u;
    }

    inline void RecordSceneLoadDuration(
        SceneLoadTimingHistory& history,
        uint64_t elapsedMilliseconds) noexcept
    {
        constexpr uint64_t MaximumTotal =
            std::numeric_limits<uint64_t>::max();
        constexpr uint32_t MaximumCount =
            std::numeric_limits<uint32_t>::max();
        if (history.completedLoadCount == MaximumCount ||
            elapsedMilliseconds > MaximumTotal - history.totalMilliseconds)
        {
            history.totalMilliseconds = elapsedMilliseconds;
            history.completedLoadCount = 1u;
            return;
        }

        history.totalMilliseconds += elapsedMilliseconds;
        ++history.completedLoadCount;
    }

    // Keep the renderer, compositor, and operating system schedulable while
    // CPU-heavy image decoding and glTF conversion run in the background.
    [[nodiscard]] constexpr uint32_t ResolveSceneLoadWorkerCount(
        uint32_t logicalProcessorCount)
    {
        if (logicalProcessorCount <= 2u)
            return 1u;

        const uint32_t workersWithReservedCores =
            logicalProcessorCount - 2u;
        return workersWithReservedCores < MaximumSceneLoadWorkerCount
            ? workersWithReservedCores
            : MaximumSceneLoadWorkerCount;
    }
}
