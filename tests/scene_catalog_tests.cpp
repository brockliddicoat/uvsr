#include "scene_catalog.h"
#include "scene_light_names.h"
#include "scene_loading.h"
#include "renderer_scene_load_worker.h"
#include "renderer_scene_retirement.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace uvsr;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
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
        const auto catalog = BuildSceneCatalog(root, discovered);
        Require(catalog.size() == 4, "shared/nested components or duplicate discovery leaked into the picker");
        const auto* entry = FindSceneCatalogEntry(catalog, main.generic_string());
        Require(entry && entry->DisplayName == "Main" && entry->InitialCamera, "named descriptor lost its camera");
        const auto& camera = *entry->InitialCamera;
        Require(camera.Position == std::array<float, 3>{ 1, 2, 3 } &&
            camera.Direction == std::array<float, 3>{ 0, 0, -1 } &&
            camera.Up == std::array<float, 3>{ 0, 1, 0 } && camera.VerticalFovDegrees == 55,
            "descriptor camera normalization changed its pose");
        const auto* invalid = FindSceneCatalogEntry(catalog, fallback.generic_string());
        Require(invalid && invalid->DisplayName == "room/fallback.scene.json" && !invalid->InitialCamera,
            "blank name fallback or overflow-prone parallel camera rejection failed");
        Require(FindSceneCatalogEntry(catalog, alternate.generic_string()) &&
            FindSceneCatalogEntry(catalog, standalone.generic_string()) &&
            !FindSceneCatalogEntry(catalog, part.generic_string()) &&
            !FindSceneCatalogEntry(catalog, detail.generic_string()), "catalog membership changed");
        std::reverse(discovered.begin(), discovered.end());
        const auto reversed = BuildSceneCatalog(root, discovered);
        Require(catalog.size() == reversed.size(), "discovery order changed catalog size");
        for (size_t i = 0; i < catalog.size(); ++i)
            Require(catalog[i].DisplayName == reversed[i].DisplayName && catalog[i].FileName == reversed[i].FileName,
                "filesystem enumeration changed picker order");
        const auto external = root.parent_path() / (root.filename().string() + "_backup") / "external.glb";
        Require(MakeSceneDisplayName(root, standalone) == "standalone.glb" &&
            MakeSceneDisplayName(root.generic_string() + "/", main) == "room/main.scene.json" &&
            MakeSceneDisplayName(root, external) == external.generic_string(), "scene path boundary changed");
#ifdef _WIN32
        std::string uppercase = main.generic_string();
        std::transform(uppercase.begin(), uppercase.end(), uppercase.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        Require(FindSceneCatalogEntry(catalog, uppercase), "Windows scene lookup became case sensitive");
#endif
        write(main, R"({"displayName":"Main","displayName":"duplicate","models":["parts/model.glb"]})");
        const auto malformed = BuildSceneCatalog(root, { main.generic_string(), part.generic_string() });
        const auto* rejected = FindSceneCatalogEntry(malformed, main.generic_string());
        Require(malformed.size() == 2 && rejected && rejected->DisplayName == "room/main.scene.json" &&
            !rejected->InitialCamera, "malformed descriptor hid a component or published metadata");
        for (const auto& names : { std::pair{ "HDRI_SKY", "hdri_sky_1" }, { "HdRi_SkY_1", "hdri_sky_1" },
                 { "SUN", "sun_1" }, { "Sun_1", "sun_1" }, { "lamp_light_1st_floor_12", "lamp_light_1st_floor_12" } })
            Require(NormalizeSceneLightName(names.first) == names.second, "scene light identity changed");
    }

    void CheckWorker()
    {
        using State = RendererSceneLoadWorkerState;
        RendererSceneLoadWorker worker;
        Require(!worker.Start({}) && worker.GetState() == State::Idle, "empty task changed the worker");
        int published = 0;
        Require(worker.Start([&] { published = 42; return true; }), "scene task did not start");
        while (worker.GetState() == State::Running)
            std::this_thread::yield();
        Require(worker.GetState() == State::Succeeded && published == 42,
            "terminal state did not publish the CPU handoff before Join");
        Require(!worker.Start([] { return true; }) && worker.Join(), "unjoined task was replaced");
        Require(worker.Start([] { return false; }) && !worker.Join() &&
            worker.GetState() == State::Failed && !worker.GetException(),
            "false task result lost failure or invented an exception");
        Require(worker.Start([]() -> bool { throw std::runtime_error("scene import failed"); }) && !worker.Join(),
            "throwing task succeeded");
        bool diagnosticPreserved = false;
        if (const auto failure = worker.GetException())
        {
            try { std::rethrow_exception(failure); }
            catch (const std::runtime_error& error) { diagnosticPreserved = std::string(error.what()) == "scene import failed"; }
            catch (...) {}
        }
        Require(diagnosticPreserved, "scene import exception type or diagnostic changed");
        worker.Reset();
        Require(worker.GetState() == State::Idle && !worker.GetException(), "reset retained terminal state");

        std::promise<void> entered, release;
        const auto released = release.get_future().share();
        Require(worker.Start([&entered, released] { entered.set_value(); released.wait(); return true; }),
            "blocking task did not start");
        entered.get_future().wait();
        const bool refusedReplacement = worker.GetState() == State::Running && !worker.Start([] { return true; });
        release.set_value();
        Require(worker.Join() && refusedReplacement, "concurrent task replaced the active loader");
        std::atomic_bool finished = false;
        {
            RendererSceneLoadWorker joining;
            Require(joining.Start([&] { finished.store(true); return true; }), "destructor task did not start");
        }
        Require(finished.load(), "worker destruction returned before its task");
    }

    void CheckRetirement()
    {
        using Status = RendererSceneRetirementStatus;
        using Query = RendererSceneQueryStatus;
        RendererSceneRetirement invalid(nullptr);
        Require(!invalid.IsValid() && !invalid.Begin() && invalid.Poll() == Status::Idle &&
            !invalid.Consume() && !invalid.UsedBlockingFallback(), "null device armed scene retirement");
        int arms = 0, polls = 0, waits = 0;
        RendererSceneRetirement normal({
            [&] { ++arms; return true; },
            [&] { return ++polls == 1 ? Query::Pending : Query::Complete; },
            [&] { ++waits; return true; } });
        Require(normal.Begin() && arms == 0 && !normal.Begin() && !normal.Consume(),
            "retirement signaled before render submissions or released an unproven scene");
        Require(normal.Poll() == Status::Pending && arms == 1 && polls == 0 &&
            normal.Poll() == Status::Pending && polls == 1 && !normal.Consume(),
            "retirement lost its arm-before-query boundary");
        Require(normal.Poll() == Status::Ready && polls == 2 && waits == 0 &&
            normal.Poll() == Status::Ready && polls == 2 && normal.Consume() && !normal.Consume(),
            "completed query was not published and consumed exactly once");

        for (const bool armSucceeds : { false, true })
        for (const bool idleSucceeds : { false, true })
        {
            int fallbackWaits = 0;
            bool idle = idleSucceeds;
            RendererSceneRetirement fallback({
                [=] { return armSucceeds; }, [] { return Query::Failed; },
                [&] { ++fallbackWaits; return idle; } });
            Require(fallback.Begin(), "fallback retirement did not start");
            if (armSucceeds)
                Require(fallback.Poll() == Status::Pending, "query failure bypassed the publication boundary");
            Require(fallback.Poll() == (idleSucceeds ? Status::Ready : Status::Failed) &&
                fallback.UsedBlockingFallback() && fallbackWaits == 1, "query failure lost its one idle fallback");
            if (!idleSucceeds)
            {
                Require(!fallback.Consume(), "failed idle proof released scene ownership");
                idle = true;
                Require(fallback.CompleteBlocking() == Status::Ready && fallbackWaits == 2,
                    "shutdown recovery did not retry the retained scene");
            }
            Require(fallback.Consume(), "proven idle scene was not consumable");
        }
        int shutdownWaits = 0;
        RendererSceneRetirement shutdown({
            [] { return true; }, [] { return Query::Pending; }, [&] { ++shutdownWaits; return true; } });
        Require(shutdown.Begin() && shutdown.Poll() == Status::Pending &&
            shutdown.CompleteBlocking() == Status::Ready && shutdown.UsedBlockingFallback() &&
            shutdownWaits == 1 && shutdown.Consume(), "shutdown did not complete pending retirement");
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
        database.allScenes = history;
        for (size_t i = 0; i < MaximumSceneLoadTimingEntries + 32; ++i)
            Require(RecordBoundedSceneLoadDuration(database.byScene, "scene-" + std::to_string(i), 20) &&
                database.byScene.size() <= MaximumSceneLoadTimingEntries, "history escaped its persistence bound");
        std::ostringstream bounded;
        Require(WriteSceneLoadTimingDatabase(bounded, database), "bounded history became unserializable");
        database.byScene.clear();
        for (size_t i = 0; i < MaximumSceneLoadTimingEntries - 2; ++i)
            database.byScene.emplace("retained-" + std::to_string(i), SceneLoadTimingHistory{ 1000, 10 });
        for (const auto* key : { "evict-a", "evict-b" })
            database.byScene.emplace(key, SceneLoadTimingHistory{ 20, 1 });
        Require(RecordBoundedSceneLoadDuration(database.byScene, "replacement", 40) &&
            database.byScene.count("evict-a") == 0 && database.byScene.count("evict-b") == 1,
            "history eviction lost deterministic tie breaking");
        Require(RecordBoundedSceneLoadDuration(database.byScene, "evict-b", 40) &&
            database.byScene.at("evict-b").completedLoadCount == 2 &&
            !RecordBoundedSceneLoadDuration(database.byScene, "", 20) &&
            database.byScene.size() == MaximumSceneLoadTimingEntries, "history update evicted or accepted an invalid key");
        database.byScene = { { "media/room interior.gltf", { 400, 2 } }, { "C:/external/scene.gltf", { 200, 1 } } };
        std::ostringstream output;
        Require(WriteSceneLoadTimingDatabase(output, database) &&
            output.str().find("C:/external/scene.gltf") < output.str().find("media/room interior.gltf"),
            "history serialization order changed");
        SceneLoadTimingDatabase restored;
        std::istringstream input(output.str());
        Require(ReadSceneLoadTimingDatabase(input, restored) && restored.allScenes.totalMilliseconds == 600 &&
            restored.allScenes.completedLoadCount == 3 && restored.byScene.size() == 2 &&
            restored.byScene.at("media/room interior.gltf").completedLoadCount == 2, "history roundtrip lost state");
        for (const auto* invalid : {
                 "UVSR_SCENE_LOAD_HISTORY 2\nall 1 1\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 0\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nall 1 1\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nscene \"duplicate\" 1 1\nscene \"duplicate\" 1 1\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nscene \"overflow\" 1 4294967296\n",
                 "UVSR_SCENE_LOAD_HISTORY 1\nall 1 1\nunknown 1 1\n" })
        {
            std::istringstream malformed(invalid);
            Require(!ReadSceneLoadTimingDatabase(malformed, restored) &&
                restored.allScenes.totalMilliseconds == 600 && restored.byScene.size() == 2,
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
        CheckWorker();
        CheckRetirement();
        CheckLoadingHistory();
        std::cout << "scene acceptance passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "scene acceptance failed: " << error.what() << '\n';
        return 1;
    }
}
