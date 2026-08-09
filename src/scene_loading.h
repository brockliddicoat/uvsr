#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace uvsr
{
    inline constexpr uint32_t MaximumSceneLoadWorkerCount = 8u;
    inline constexpr uint64_t SceneLoadCounterTickMilliseconds = 20u;

    struct SceneLoadTimingHistory
    {
        uint64_t totalMilliseconds = 0u;
        uint32_t completedLoadCount = 0u;
    };

    inline constexpr uint32_t SceneLoadTimingDatabaseVersion = 1u;
    inline constexpr size_t MaximumSceneLoadTimingEntries = 512u;
    inline constexpr size_t MaximumSceneLoadTimingKeyBytes = 4096u;

    struct SceneLoadTimingDatabase
    {
        SceneLoadTimingHistory allScenes;
        std::unordered_map<std::string, SceneLoadTimingHistory> byScene;
    };

    [[nodiscard]] inline bool IsValidSceneLoadTimingHistory(
        const SceneLoadTimingHistory& history)
    {
        return history.completedLoadCount != 0u ||
            history.totalMilliseconds == 0u;
    }

    [[nodiscard]] inline bool IsValidSceneLoadTimingKey(
        const std::string& key)
    {
        return !key.empty() &&
            key.size() <= MaximumSceneLoadTimingKeyBytes &&
            std::none_of(
                key.begin(),
                key.end(),
                [](unsigned char character)
                {
                    return character < 0x20u;
                });
    }

    [[nodiscard]] inline bool WriteSceneLoadTimingDatabase(
        std::ostream& output,
        const SceneLoadTimingDatabase& database)
    {
        if (!output.good() ||
            !IsValidSceneLoadTimingHistory(database.allScenes) ||
            database.byScene.size() > MaximumSceneLoadTimingEntries)
        {
            return false;
        }

        output << "UVSR_SCENE_LOAD_HISTORY "
            << SceneLoadTimingDatabaseVersion << '\n';
        output << "all " << database.allScenes.totalMilliseconds << ' '
            << database.allScenes.completedLoadCount << '\n';

        std::vector<std::string> keys;
        keys.reserve(database.byScene.size());
        for (const auto& [key, history] : database.byScene)
        {
            if (!IsValidSceneLoadTimingKey(key) ||
                !IsValidSceneLoadTimingHistory(history) ||
                history.completedLoadCount == 0u)
            {
                return false;
            }
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        for (const std::string& key : keys)
        {
            const SceneLoadTimingHistory& history =
                database.byScene.at(key);
            output << "scene " << std::quoted(key) << ' '
                << history.totalMilliseconds << ' '
                << history.completedLoadCount << '\n';
        }
        return output.good();
    }

    [[nodiscard]] inline bool ReadSceneLoadTimingDatabase(
        std::istream& input,
        SceneLoadTimingDatabase& database)
    {
        std::string magic;
        uint64_t version = 0u;
        if (!(input >> magic >> version) ||
            magic != "UVSR_SCENE_LOAD_HISTORY" ||
            version != SceneLoadTimingDatabaseVersion)
        {
            return false;
        }

        SceneLoadTimingDatabase candidate;
        bool readAllScenes = false;
        std::string recordType;
        while (input >> recordType)
        {
            uint64_t totalMilliseconds = 0u;
            uint64_t completedLoadCount = 0u;
            if (recordType == "all")
            {
                if (readAllScenes ||
                    !(input >> totalMilliseconds >> completedLoadCount) ||
                    completedLoadCount >
                        std::numeric_limits<uint32_t>::max())
                {
                    return false;
                }
                candidate.allScenes = {
                    totalMilliseconds,
                    static_cast<uint32_t>(completedLoadCount)
                };
                if (!IsValidSceneLoadTimingHistory(candidate.allScenes))
                    return false;
                readAllScenes = true;
                continue;
            }
            if (recordType != "scene" ||
                candidate.byScene.size() >=
                    MaximumSceneLoadTimingEntries)
            {
                return false;
            }

            std::string key;
            if (!(input >> std::quoted(key) >> totalMilliseconds >>
                    completedLoadCount) ||
                !IsValidSceneLoadTimingKey(key) ||
                completedLoadCount == 0u ||
                completedLoadCount >
                    std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            const SceneLoadTimingHistory history = {
                totalMilliseconds,
                static_cast<uint32_t>(completedLoadCount)
            };
            if (!IsValidSceneLoadTimingHistory(history) ||
                !candidate.byScene.emplace(key, history).second)
            {
                return false;
            }
        }

        if (!input.eof() || !readAllScenes)
            return false;
        database = std::move(candidate);
        return true;
    }

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

    [[nodiscard]] inline bool RecordBoundedSceneLoadDuration(
        std::unordered_map<std::string, SceneLoadTimingHistory>&
            historyByScene,
        const std::string& key,
        uint64_t elapsedMilliseconds)
    {
        if (!IsValidSceneLoadTimingKey(key))
            return false;

        auto entry = historyByScene.find(key);
        if (entry == historyByScene.end())
        {
            while (historyByScene.size() >=
                MaximumSceneLoadTimingEntries)
            {
                const auto eviction = std::min_element(
                    historyByScene.begin(),
                    historyByScene.end(),
                    [](const auto& left, const auto& right)
                    {
                        if (left.second.completedLoadCount !=
                            right.second.completedLoadCount)
                        {
                            return left.second.completedLoadCount <
                                right.second.completedLoadCount;
                        }
                        if (left.second.totalMilliseconds !=
                            right.second.totalMilliseconds)
                        {
                            return left.second.totalMilliseconds <
                                right.second.totalMilliseconds;
                        }
                        return left.first < right.first;
                    });
                if (eviction == historyByScene.end())
                    return false;
                historyByScene.erase(eviction);
            }

            entry = historyByScene.try_emplace(
                key,
                SceneLoadTimingHistory{}).first;
        }

        RecordSceneLoadDuration(entry->second, elapsedMilliseconds);
        return true;
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
