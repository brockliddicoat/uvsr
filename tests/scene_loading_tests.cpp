#include "scene_loading.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
    bool ExpectCondition(bool condition, const char* contract)
    {
        if (condition)
            return true;

        std::cerr << "FAIL: " << contract << ".\n";
        return false;
    }

    bool ExpectWorkerCount(uint32_t processors, uint32_t expected)
    {
        const uint32_t actual =
            uvsr::ResolveSceneLoadWorkerCount(processors);
        if (actual == expected)
            return true;

        std::cerr << "FAIL: " << processors << " logical processors resolved to "
                  << actual << " scene workers; expected " << expected << ".\n";
        return false;
    }

    bool ExpectElapsedTicks(uint64_t milliseconds, uint64_t expected)
    {
        const uint64_t actual =
            uvsr::ResolveSceneLoadElapsedTicks(milliseconds);
        if (actual == expected)
            return true;

        std::cerr << "FAIL: " << milliseconds << " ms resolved to " << actual
                  << " loading ticks; expected " << expected << ".\n";
        return false;
    }

    bool ExpectAverageTicks(
        const uvsr::SceneLoadTimingHistory& history,
        uint64_t expected)
    {
        const uint64_t actual = uvsr::ResolveAverageSceneLoadTicks(history);
        if (actual == expected)
            return true;

        std::cerr << "FAIL: load history resolved to " << actual
                  << " average ticks; expected " << expected << ".\n";
        return false;
    }
}

int main()
{
    bool passed = true;
    passed &= ExpectWorkerCount(0u, 1u);
    passed &= ExpectWorkerCount(1u, 1u);
    passed &= ExpectWorkerCount(2u, 1u);
    passed &= ExpectWorkerCount(3u, 1u);
    passed &= ExpectWorkerCount(4u, 2u);
    passed &= ExpectWorkerCount(8u, 6u);
    passed &= ExpectWorkerCount(9u, 7u);
    passed &= ExpectWorkerCount(10u, 8u);
    passed &= ExpectWorkerCount(16u, 8u);
    passed &= ExpectWorkerCount(128u, 8u);

    uint32_t previousWorkerCount = 0u;
    for (uint32_t processors = 2u; processors <= 256u; ++processors)
    {
        const uint32_t workers =
            uvsr::ResolveSceneLoadWorkerCount(processors);
        passed &= workers >= 1u;
        passed &= workers <= uvsr::MaximumSceneLoadWorkerCount;
        passed &= workers < processors;
        passed &= workers >= previousWorkerCount;
        previousWorkerCount = workers;
    }

    passed &= ExpectElapsedTicks(0u, 0u);
    passed &= ExpectElapsedTicks(19u, 0u);
    passed &= ExpectElapsedTicks(20u, 1u);
    passed &= ExpectElapsedTicks(39u, 1u);
    passed &= ExpectElapsedTicks(40u, 2u);
    passed &= ExpectElapsedTicks(
        std::numeric_limits<uint64_t>::max(),
        std::numeric_limits<uint64_t>::max() /
            uvsr::SceneLoadCounterTickMilliseconds);

    uvsr::SceneLoadTimingHistory history;
    passed &= ExpectAverageTicks(history, 0u);
    uvsr::RecordSceneLoadDuration(history, 0u);
    passed &= ExpectAverageTicks(history, 1u);
    uvsr::RecordSceneLoadDuration(history, 40u);
    passed &= history.totalMilliseconds == 40u;
    passed &= history.completedLoadCount == 2u;
    passed &= ExpectAverageTicks(history, 1u);
    uvsr::RecordSceneLoadDuration(history, 560u);
    passed &= history.totalMilliseconds == 600u;
    passed &= history.completedLoadCount == 3u;
    passed &= ExpectAverageTicks(history, 10u);

    passed &= ExpectAverageTicks({ 9u, 1u }, 1u);
    passed &= ExpectAverageTicks({ 10u, 1u }, 1u);
    passed &= ExpectAverageTicks({ 29u, 1u }, 1u);
    passed &= ExpectAverageTicks({ 30u, 1u }, 2u);
    passed &= ExpectAverageTicks({ 399u, 2u }, 10u);
    passed &= ExpectAverageTicks({ 400u, 2u }, 10u);
    passed &= ExpectAverageTicks({ 401u, 2u }, 10u);
    passed &= ExpectAverageTicks(
        { std::numeric_limits<uint64_t>::max(), 1u },
        std::numeric_limits<uint64_t>::max() /
            uvsr::SceneLoadCounterTickMilliseconds + 1u);

    uvsr::SceneLoadTimingHistory overflowHistory = {
        std::numeric_limits<uint64_t>::max() - 4u,
        2u
    };
    uvsr::RecordSceneLoadDuration(overflowHistory, 5u);
    passed &= overflowHistory.totalMilliseconds == 5u;
    passed &= overflowHistory.completedLoadCount == 1u;

    uvsr::SceneLoadTimingHistory saturatedHistory = {
        100u,
        std::numeric_limits<uint32_t>::max()
    };
    uvsr::RecordSceneLoadDuration(saturatedHistory, 40u);
    passed &= saturatedHistory.totalMilliseconds == 40u;
    passed &= saturatedHistory.completedLoadCount == 1u;

    std::unordered_map<std::string, uvsr::SceneLoadTimingHistory>
        boundedHistory;
    for (size_t index = 0u;
         index < uvsr::MaximumSceneLoadTimingEntries + 32u;
         ++index)
    {
        const std::string key = "scene-" + std::to_string(index);
        passed &= ExpectCondition(
            uvsr::RecordBoundedSceneLoadDuration(
                boundedHistory,
                key,
                20u),
            "bounded scene history accepts a valid scene key");
        passed &= ExpectCondition(
            boundedHistory.size() <=
                uvsr::MaximumSceneLoadTimingEntries,
            "bounded scene history never exceeds its serialized capacity");
    }

    uvsr::SceneLoadTimingDatabase boundedDatabase;
    boundedDatabase.byScene = boundedHistory;
    std::ostringstream boundedSerialized;
    passed &= ExpectCondition(
        uvsr::WriteSceneLoadTimingDatabase(
            boundedSerialized,
            boundedDatabase),
        "bounded scene history remains serializable after more than 512 keys");

    boundedHistory.clear();
    for (size_t index = 0u;
         index < uvsr::MaximumSceneLoadTimingEntries - 2u;
         ++index)
    {
        boundedHistory.emplace(
            "retained-" + std::to_string(index),
            uvsr::SceneLoadTimingHistory{ 1'000u, 10u });
    }
    boundedHistory.emplace(
        "evict-a",
        uvsr::SceneLoadTimingHistory{ 20u, 1u });
    boundedHistory.emplace(
        "evict-b",
        uvsr::SceneLoadTimingHistory{ 20u, 1u });
    passed &= ExpectCondition(
        uvsr::RecordBoundedSceneLoadDuration(
            boundedHistory,
            "replacement",
            40u),
        "bounded scene history inserts a replacement at capacity");
    passed &= ExpectCondition(
        boundedHistory.find("evict-a") == boundedHistory.end() &&
            boundedHistory.find("evict-b") != boundedHistory.end(),
        "bounded scene history uses deterministic lexical tie-breaking");
    const size_t existingHistorySize = boundedHistory.size();
    passed &= ExpectCondition(
        uvsr::RecordBoundedSceneLoadDuration(
            boundedHistory,
            "evict-b",
            40u) &&
            boundedHistory.size() == existingHistorySize &&
            boundedHistory.at("evict-b").completedLoadCount == 2u,
        "updating an existing scene history does not evict another entry");
    passed &= ExpectCondition(
        !uvsr::RecordBoundedSceneLoadDuration(
            boundedHistory,
            std::string(),
            20u) &&
            boundedHistory.size() == existingHistorySize,
        "invalid scene keys are rejected without changing bounded history");

    uvsr::SceneLoadTimingDatabase database;
    database.allScenes = { 600u, 3u };
    database.byScene.emplace(
        "media/bistro interior.gltf",
        uvsr::SceneLoadTimingHistory{ 400u, 2u });
    database.byScene.emplace(
        "C:/external/scene.gltf",
        uvsr::SceneLoadTimingHistory{ 200u, 1u });
    std::ostringstream serialized;
    passed &= uvsr::WriteSceneLoadTimingDatabase(serialized, database);
    passed &= serialized.str().find("UVSR_SCENE_LOAD_HISTORY 1") == 0u;
    passed &= serialized.str().find("C:/external/scene.gltf") <
        serialized.str().find("media/bistro interior.gltf");

    uvsr::SceneLoadTimingDatabase restored;
    std::istringstream serializedInput(serialized.str());
    passed &= uvsr::ReadSceneLoadTimingDatabase(
        serializedInput,
        restored);
    passed &= restored.allScenes.totalMilliseconds == 600u;
    passed &= restored.allScenes.completedLoadCount == 3u;
    passed &= restored.byScene.size() == 2u;
    passed &= restored.byScene.at("media/bistro interior.gltf")
        .completedLoadCount == 2u;

    for (const std::string invalidDatabase : {
            "UVSR_SCENE_LOAD_HISTORY 2\nall 1 1\n",
            "UVSR_SCENE_LOAD_HISTORY 1\nall 1 0\n",
            "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nall 1 1\n",
            "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\n"
                "scene \"duplicate\" 1 1\n"
                "scene \"duplicate\" 1 1\n",
            "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\n"
                "scene \"overflow\" 1 4294967296\n",
            "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nunknown 1 1\n" })
    {
        uvsr::SceneLoadTimingDatabase rejected;
        std::istringstream invalidInput(invalidDatabase);
        passed &= !uvsr::ReadSceneLoadTimingDatabase(
            invalidInput,
            rejected);
    }

    if (!passed)
        return 1;

    std::cout << "UVSR scene-loading policy tests passed.\n";
    return 0;
}
