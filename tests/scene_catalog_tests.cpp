#include "scene_catalog.h"
#include "settings_snapshot_storage.h"
#include <vector>
#include "scene_light_names.h"
#include "scene_loading.h"
#include "renderer_scene_load_worker.h"
#include "renderer_scene_retirement.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

void TestRendererSceneLoadWorker();
bool TestImageBasedLightingOwnership(const wchar_t* directory);
bool TestSceneHistoryOwnership();
void TestSceneCatalogOwnership(const std::filesystem::path& root);

using namespace uvsr;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    SceneCatalog BuildCatalog(const std::filesystem::path& root, const std::vector<std::string>& files)
    {
        std::vector<std::string_view> views(files.begin(), files.end());
        SceneCatalog catalog;
        SettingsSnapshotError error;
        Require(BuildSceneCatalog(root.native(), {views.data(), views.size()}, catalog, error), error.Message());
        return catalog;
    }

    const SceneCatalogEntry* FindCatalog(const SceneCatalog& catalog, std::string_view file)
    {
        const SceneCatalogEntry* entry = nullptr;
        SettingsSnapshotError error;
        Require(FindSceneCatalogEntry(catalog, file, entry, error), error.Message());
        return entry;
    }

    std::string DisplayName(const std::filesystem::path& root, const std::filesystem::path& file)
    {
        json::EncodedText text;
        SettingsSnapshotError error;
        Require(MakeSceneDisplayName(root.native(), file.native(), text, error), error.Message());
        return std::string(text.Data(), text.Size());
    }

    void CheckCatalog(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root / "room");
        const auto main = root / "room/main.scene.json";
        const auto alternate = root / "room/alternate.scene.json";
        const auto fallback = root / "room/fallback.scene.json";
        const auto part = root / "room/parts/model.glb";
        const auto detail = root / "room/parts/detail/model.glb";
        const auto standalone = root / "standalone.glb";
        const auto write = [](const auto& path, const char* content)
        {
            std::ofstream output(path, std::ios::binary);
            output << content;
            Require(output.good(), "catalog fixture write failed");
        };
        write(main, R"({"displayName":"Main","initialCamera":{"position":[1,2,3],"direction":[0,0,-2],"up":[0,3,0],"verticalFovDegrees":55},"models":["parts/model.glb","parts/../parts/detail/model.glb"]})");
        write(alternate, R"({"displayName":"Alternate","models":["parts/model.glb"]})");
        write(fallback, R"({"displayName":" ","initialCamera":{"position":[0,0,0],"direction":[3e38,0,0],"up":[3e38,1,0]},"models":[]})");
        std::vector<std::string> discovered{ part.generic_string(), alternate.generic_string(),
            detail.generic_string(), standalone.generic_string(), main.generic_string(),
            fallback.generic_string(), part.generic_string() };
        const auto catalog = BuildCatalog(root, discovered);
        Require(catalog.Count() == 4, "shared/nested components or duplicate discovery leaked into the picker");
        const auto* entry = FindCatalog(catalog, main.generic_string());
        Require(entry && entry->DisplayName == "Main" && entry->InitialCamera, "named descriptor lost its camera");
        Require(entry->CommandName == "room/main.scene.json" && entry->FileName == main.generic_string(),
            "friendly scene metadata changed its command name or runtime path");
        const auto& camera = *entry->InitialCamera;
        Require(camera.Position == std::array<float, 3>{ 1, 2, 3 } &&
            camera.Direction == std::array<float, 3>{ 0, 0, -1 } &&
            camera.Up == std::array<float, 3>{ 0, 1, 0 } && camera.VerticalFovDegrees == 55,
            "descriptor camera normalization changed its pose");
        const auto* invalid = FindCatalog(catalog, fallback.generic_string());
        Require(invalid && invalid->DisplayName == "room/fallback.scene.json" && !invalid->InitialCamera,
            "blank name fallback or overflow-prone parallel camera rejection failed");
        Require(FindCatalog(catalog, alternate.generic_string()) &&
            FindCatalog(catalog, standalone.generic_string()) &&
            !FindCatalog(catalog, part.generic_string()) &&
            !FindCatalog(catalog, detail.generic_string()), "catalog membership changed");
        std::reverse(discovered.begin(), discovered.end());
        const auto reversed = BuildCatalog(root, discovered);
        Require(catalog.Count() == reversed.Count(), "discovery order changed catalog size");
        for (size_t i = 0; i < catalog.Count(); ++i)
            Require(catalog[i].DisplayName == reversed[i].DisplayName && catalog[i].FileName == reversed[i].FileName &&
                catalog[i].CommandName == reversed[i].CommandName &&
                catalog[i].CommandName == DisplayName(root, catalog[i].FileName),
                "filesystem enumeration changed picker order");
        const auto external = root.parent_path() / (root.filename().string() + "_backup") / "external.glb";
        Require(DisplayName(root, standalone) == "standalone.glb" &&
            DisplayName(root.generic_string() + "/", main) == "room/main.scene.json" &&
            DisplayName(root, external) == external.generic_string(), "scene path boundary changed");
#ifdef _WIN32
        std::string uppercase = main.generic_string();
        std::transform(uppercase.begin(), uppercase.end(), uppercase.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        Require(FindCatalog(catalog, uppercase), "Windows scene lookup became case sensitive");
#endif
        write(main, R"({"displayName":"Main","displayName":"duplicate","models":["parts/model.glb"]})");
        const auto malformed = BuildCatalog(root, { main.generic_string(), part.generic_string() });
        const auto* rejected = FindCatalog(malformed, main.generic_string());
        Require(malformed.Count() == 2 && rejected && rejected->DisplayName == "room/main.scene.json" &&
            !rejected->InitialCamera, "malformed descriptor hid a component or published metadata");
        for (const auto& names : { std::pair{ "HDRI_SKY", "hdri_sky_1" }, { "HdRi_SkY_1", "hdri_sky_1" },
                 { "SUN", "sun_1" }, { "Sun_1", "sun_1" }, { "lamp_light_1st_floor_12", "lamp_light_1st_floor_12" } })
        {
            const std::string_view input(names.first);
            const auto normalized = NormalizeSceneLightName({input.data(), input.size()});
            Require(std::string_view(normalized.data, normalized.count) == names.second, "scene light identity changed");
        }
    }

    struct RetirementFixture
    {
        int arms = 0, polls = 0, waits = 0;
        bool armSucceeds = true, idleSucceeds = true, pollFails = false;
        unsigned pendingPolls = 1;
        static bool Arm(void* context)
        {
            auto& self = *static_cast<RetirementFixture*>(context);
            ++self.arms;
            return self.armSucceeds;
        }
        static RendererSceneQueryStatus Poll(void* context)
        {
            auto& self = *static_cast<RetirementFixture*>(context);
            ++self.polls;
            if (self.pollFails)
                return RendererSceneQueryStatus::Failed;
            return unsigned(self.polls) <= self.pendingPolls
                ? RendererSceneQueryStatus::Pending : RendererSceneQueryStatus::Complete;
        }
        static bool Wait(void* context)
        {
            auto& self = *static_cast<RetirementFixture*>(context);
            ++self.waits;
            return self.idleSucceeds;
        }
        RendererSceneRetirementOperations Operations()
        {
            return {this, Arm, Poll, Wait};
        }
    };

    void CheckRetirement()
    {
        using Status = RendererSceneRetirementStatus;
        RendererSceneRetirement invalid;
        Require(!invalid.IsValid() && !invalid.Begin() && invalid.Poll() == Status::Idle &&
            !invalid.Consume() && !invalid.UsedBlockingFallback(), "empty operations armed retirement");
        RetirementFixture events;
        RendererSceneRetirement normal(events.Operations());
        Require(normal.Begin() && events.arms == 0 && !normal.Begin() && !normal.Consume(),
            "retirement signaled before render submissions or released an unproven scene");
        Require(normal.Poll() == Status::Pending && events.arms == 1 && events.polls == 0 &&
            normal.Poll() == Status::Pending && events.polls == 1 && !normal.Consume(),
            "retirement lost its arm-before-query boundary");
        Require(normal.Poll() == Status::Ready && events.polls == 2 && events.waits == 0 &&
            normal.Poll() == Status::Ready && events.polls == 2 && normal.Consume() && !normal.Consume(),
            "completed query was not published and consumed exactly once");

        for (unsigned configuration = 0; configuration < 4; ++configuration)
        {
            RetirementFixture fallbackEvents;
            fallbackEvents.armSucceeds = (configuration & 1) != 0;
            fallbackEvents.idleSucceeds = (configuration & 2) != 0;
            fallbackEvents.pollFails = true;
            RendererSceneRetirement fallback(fallbackEvents.Operations());
            Require(fallback.Begin(), "fallback retirement did not start");
            if (fallbackEvents.armSucceeds)
                Require(fallback.Poll() == Status::Pending, "query failure bypassed the publication boundary");
            Require(fallback.Poll() == (fallbackEvents.idleSucceeds ? Status::Ready : Status::Failed) &&
                fallback.UsedBlockingFallback() && fallbackEvents.waits == 1, "query failure lost its one idle fallback");
            if (!fallbackEvents.idleSucceeds)
            {
                Require(!fallback.Consume(), "failed idle proof released scene ownership");
                fallbackEvents.idleSucceeds = true;
                Require(fallback.CompleteBlocking() == Status::Ready && fallbackEvents.waits == 2,
                    "shutdown recovery did not retry the retained scene");
            }
            Require(fallback.Consume(), "proven idle scene was not consumable");
        }
        RetirementFixture shutdownEvents;
        RendererSceneRetirement shutdown(shutdownEvents.Operations());
        Require(shutdown.Begin() && shutdown.Poll() == Status::Pending &&
            shutdown.CompleteBlocking() == Status::Ready && shutdown.UsedBlockingFallback() &&
            shutdownEvents.waits == 1 && shutdown.Consume(), "shutdown did not complete pending retirement");
    }

    void CheckLoadingHistory()
    {
        Require(ResolveSceneLoadWorkerCount(0) > 0, "unknown CPU count disabled scene loading");
        constexpr auto Maximum = std::numeric_limits<uint64_t>::max();
        Require(ResolveSceneLoadElapsedTicks(19) == 0 && ResolveSceneLoadElapsedTicks(20) == 1 &&
            ResolveSceneLoadElapsedTicks(Maximum) == 922337203685477580ull, "loading tick boundary overflowed");
        SceneLoadTimingHistory history;
        Require(ResolveAverageSceneLoadTicks(history) == 0, "empty history invented an estimate");
        for (uint64_t duration : { 0ull, 40ull, 560ull })
            RecordSceneLoadDuration(history, duration);
        Require(history.totalMilliseconds == 600 && history.completedLoadCount == 3 &&
            ResolveAverageSceneLoadTicks(history) == 10 && ResolveAverageSceneLoadTicks({ 0, 1 }) == 1 &&
            ResolveAverageSceneLoadTicks({ 30, 1 }) == 2 &&
            ResolveAverageSceneLoadTicks({ 29, 1 }) == 1 &&
            ResolveAverageSceneLoadTicks({ Maximum, 1 }) == 922337203685477581ull, "loading average lost rounding");
        for (auto overflow : { SceneLoadTimingHistory{ Maximum - 4, 2 },
                 SceneLoadTimingHistory{ 100, std::numeric_limits<uint32_t>::max() } })
        {
            RecordSceneLoadDuration(overflow, 5);
            Require(overflow.totalMilliseconds == 5 && overflow.completedLoadCount == 1, "history saturation overflowed");
        }
        SceneLoadTimingDatabase database;
        SettingsSnapshotError error;
        database.allScenes = history;
        for (size_t i = 0; i < MaximumSceneLoadTimingEntries + 32; ++i)
            Require(database.Record("scene-" + std::to_string(i), 20, error) &&
                database.Count() <= MaximumSceneLoadTimingEntries, "history escaped its persistence bound");
        json::EncodedText bounded;
        Require(WriteSceneLoadTimingDatabase(database, bounded, error), "bounded history became unserializable");
        database.Clear();
        database.allScenes = history;
        for (size_t i = 0; i < MaximumSceneLoadTimingEntries - 2; ++i)
            for (size_t sample = 0; sample < 10; ++sample)
                Require(database.Record("retained-" + std::to_string(i), 100, error), error.Message());
        for (const auto* key : { "evict-a", "evict-b" })
            Require(database.Record(key, 20, error), error.Message());
        Require(database.Record("replacement", 40, error) && !database.Find("evict-a") && database.Find("evict-b"),
            "history eviction lost deterministic tie breaking");
        Require(database.Record("evict-b", 40, error) && database.Find("evict-b")->completedLoadCount == 2 &&
            !database.Record("", 20, error) && database.Count() == MaximumSceneLoadTimingEntries,
            "history update evicted or accepted an invalid key");
        Require(ReadSceneLoadTimingDatabase(
            "UVSR_SCENE_LOAD_HISTORY 1\nall 600 3\nscene \"media/room interior.gltf\" 400 2\nscene \"C:/external/scene.gltf\" 200 1\n",
            database, error), "history roundtrip seed failed");
        json::EncodedText output;
        Require(WriteSceneLoadTimingDatabase(database, output, error), error.Message());
        const std::string_view serialized(output.Data(), output.Size());
        Require(serialized.find("C:/external/scene.gltf") < serialized.find("media/room interior.gltf"),
            "history serialization order changed");
        SceneLoadTimingDatabase restored;
        Require(ReadSceneLoadTimingDatabase(serialized, restored, error) && restored.allScenes.totalMilliseconds == 600 &&
            restored.allScenes.completedLoadCount == 3 && restored.Count() == 2 &&
            restored.Find("media/room interior.gltf")->completedLoadCount == 2, "history roundtrip lost state");
        for (const auto* invalid : {
                 "UVSR_SCENE_LOAD_HISTORY 2\nall 1 1\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 0\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nall 1 1\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nscene \"duplicate\" 1 1\nscene \"duplicate\" 1 1\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nscene \"overflow\" 1 4294967296\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nunknown 1 1\n" })
        {
            Require(!ReadSceneLoadTimingDatabase(invalid, restored, error) &&
                restored.allScenes.totalMilliseconds == 600 && restored.Count() == 2,
                "malformed history was accepted or replaced published state");
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "expected a scratch directory");
        CheckCatalog(std::filesystem::absolute(std::filesystem::u8path(argv[1])) / "catalog");
        TestSceneCatalogOwnership(std::filesystem::absolute(std::filesystem::u8path(argv[1])) / "ownership");
        TestRendererSceneLoadWorker();
        Require(TestImageBasedLightingOwnership((std::filesystem::absolute(std::filesystem::u8path(argv[1])) / "ibl").c_str()),
            "IBL ownership checks failed");
        CheckRetirement();
        CheckLoadingHistory();
        Require(TestSceneHistoryOwnership(), "history ownership checks failed");
        std::cout << "scene acceptance passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "scene acceptance failed: " << error.what() << '\n';
        return 1;
    }
}
