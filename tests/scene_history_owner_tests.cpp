#include "scene_loading.h"
#include "settings_snapshot_storage.h"
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
    size_t Assertions = 0;
    bool Passed = true;
    bool Require(bool value, const char* message)
    {
        ++Assertions;
        if (!value) { Passed = false; fprintf(stderr, "history: %s\n", message); }
        return value;
    }
    void ClearFailures()
    {
        uvsr::ClearSceneHistoryAllocationFailure();
        uvsr::json::ClearAllocationFailure();
    }
    void Detail(uvsr::SettingsSnapshotError& error, std::string_view text)
    {
        error.detail = uvsr::json::EncodedText([](uvsr::json::OutputWriter& writer, const void* context) noexcept {
            const auto input = *static_cast<const std::string_view*>(context);
            return writer.Raw({input.data(), input.size()});
        }, &text);
        Require(error.detail.IsValid(), "could not prepare error-owned input");
    }
    std::string Snapshot(const uvsr::SceneLoadTimingDatabase& database)
    {
        uvsr::json::EncodedText text; uvsr::SettingsSnapshotError error;
        if (!Require(uvsr::WriteSceneLoadTimingDatabase(database, text, error), "could not capture database")) return {};
        return {text.Data(), text.Size()};
    }
    constexpr std::string_view Seed = "UVSR_SCENE_LOAD_HISTORY 1\nall 90 3\nscene \"retained\" 60 2\n";
}

bool TestSceneHistoryOwnership()
{
    using namespace uvsr;
    static_assert(!std::is_copy_constructible_v<SceneLoadTimingDatabase>);
    static_assert(std::is_nothrow_move_constructible_v<SceneLoadTimingDatabase>);
    static_assert(std::is_nothrow_move_assignable_v<SceneLoadTimingDatabase>);
    Assertions = 0; Passed = true; ClearFailures();
    SettingsSnapshotError error;
    SceneLoadTimingDatabase database;
    if (!Require(ReadSceneLoadTimingDatabase(Seed, database, error), "seed failed")) return false;
    const auto* retained = database.Find("retained");
    if (!Require(retained != nullptr, "seed key missing")) return false;
    const auto before = Snapshot(database);
    const auto unchanged = [&]() {
        return database.Find("retained") == retained && retained->totalMilliseconds == 60 &&
            retained->completedLoadCount == 2 && Snapshot(database) == before;
    };
    for (std::string_view invalid : {std::string_view{}, std::string_view("bad\nkey"), std::string_view("bad\0key", 7)})
    {
        Require(!database.Record(invalid, 12, error) && error.code == SettingsSnapshotErrorCode::InvalidInput,
            "invalid key was accepted");
        Require(unchanged(), "invalid record changed published history");
    }
    std::string oversized(MaximumSceneLoadTimingKeyBytes + 1, 'x');
    Require(!database.Record(oversized, 12, error) && unchanged(), "oversized key changed history");
    Require(!ReadSceneLoadTimingDatabase("UVSR_SCENE_LOAD_HISTORY 1\nall 20 1\nscene \"a\" 10 1\nscene \"a\" 20 2", database, error)
        && unchanged(), "duplicate parse changed published history");

    std::string full = "UVSR_SCENE_LOAD_HISTORY 1\nall 10240 512\n";
    std::string first(MaximumSceneLoadTimingKeyBytes, 'x');
    for (size_t index = 0; index < MaximumSceneLoadTimingEntries; ++index)
    {
        std::string key(MaximumSceneLoadTimingKeyBytes, 'x');
        key[0] = char('0' + index / 100); key[1] = char('0' + index / 10 % 10); key[2] = char('0' + index % 10);
        if (!index) first = key;
        full += "scene \""; full += key; full += "\" 20 1\n";
    }
    size_t parseFailures = 0;
    bool parsed = false;
    for (size_t count = 0; count < 32; ++count)
    {
        FailSceneHistoryAllocationAfter(count);
        parsed = ReadSceneLoadTimingDatabase(full, database, error);
        ClearFailures();
        if (parsed) break;
        ++parseFailures;
        Require(error.code == SettingsSnapshotErrorCode::OutOfMemory && unchanged(),
            "parse allocation failure changed published history or its borrow");
    }
    Require(parsed && database.Count() == MaximumSceneLoadTimingEntries && database.Find(first),
        "bounded maximum history did not parse");
    Require(parseFailures > 2, "builder growth and publication allocation points were not exercised");
    if (!parsed) return false;
    const auto fullSnapshot = Snapshot(database);
    const auto* victim = database.Find(first);
    size_t evictionFailures = 0;
    for (size_t count = 0; count < 2; ++count)
    {
        FailSceneHistoryAllocationAfter(count);
        const bool recorded = database.Record("new", 40, error);
        ClearFailures(); ++evictionFailures;
        Require(!recorded && error.code == SettingsSnapshotErrorCode::OutOfMemory && database.Find(first) == victim &&
            victim->totalMilliseconds == 20 && Snapshot(database) == fullSnapshot,
            "eviction allocation failure lost the victim or changed a borrow");
    }
    Require(database.Record("new", 40, error) && database.Count() == MaximumSceneLoadTimingEntries &&
        !database.Find(first) && database.Find("new") && database.Find("new")->totalMilliseconds == 40,
        "successful eviction did not retain the exact bound and smallest-key victim");
    const auto* existing = database.Find("new");
    FailSceneHistoryAllocationAfter(0);
    const bool updated = database.Record("new", 10, error);
    ClearFailures();
    Require(updated && database.Find("new") == existing && existing->totalMilliseconds == 50 &&
        existing->completedLoadCount == 2, "existing record allocated, moved or lost its update");

    SceneLoadTimingDatabase empty;
    size_t insertFailures = 0;
    for (size_t count = 0; count < 2; ++count)
    {
        FailSceneHistoryAllocationAfter(count);
        const bool recorded = empty.Record("new", 0, error);
        ClearFailures(); ++insertFailures;
        Require(!recorded && error.code == SettingsSnapshotErrorCode::OutOfMemory && !empty.Count() &&
            !empty.allScenes.completedLoadCount, "empty insertion failure published partial storage");
    }
    Require(empty.Record("new", 0, error) && empty.Find("new")->completedLoadCount == 1,
        "zero-time sample did not become a completed record");
    FailSceneHistoryAllocationAfter(0);
    const bool clearedByRead = ReadSceneLoadTimingDatabase("UVSR_SCENE_LOAD_HISTORY 1 all 0 0", empty, error);
    ClearFailures();
    Require(clearedByRead && !empty.Count() && !empty.Find("new"), "empty parse required storage or retained old entries");

    Detail(error, Seed);
    const std::string_view borrowedInput(error.detail.Data(), error.detail.Size());
    Require(ReadSceneLoadTimingDatabase(borrowedInput, database, error) && Snapshot(database) == Seed,
        "parse released its error-owned input before copying it");
    Detail(error, "an error-owned history key");
    const std::string_view borrowedKey(error.detail.Data(), error.detail.Size());
    Require(database.Record(borrowedKey, 7, error) && database.Find("an error-owned history key"),
        "record released its error-owned key before copying it");
    const auto aliasSnapshot = Snapshot(database);
    Detail(error, Seed);
    FailSceneHistoryAllocationAfter(0);
    const bool aliasFailed = ReadSceneLoadTimingDatabase({error.detail.Data(), error.detail.Size()}, database, error);
    ClearFailures();
    Require(!aliasFailed && Snapshot(database) == aliasSnapshot, "failed aliased-input parse changed output");

    json::EncodedText output;
    Require(WriteSceneLoadTimingDatabase(database, output, error), "initial serialization failed");
    const char* outputBorrow = output.Data(); const std::string outputBefore(output.Data(), output.Size());
    json::FailAllocationAfter(0);
    const bool written = WriteSceneLoadTimingDatabase(database, output, error);
    ClearFailures();
    Require(!written && error.code == SettingsSnapshotErrorCode::OutOfMemory && output.Data() == outputBorrow &&
        std::string_view(output.Data(), output.Size()) == outputBefore && Snapshot(database) == aliasSnapshot,
        "serialization allocation failure changed output or database");
    database.allScenes = {1, 0};
    Require(!WriteSceneLoadTimingDatabase(database, output, error) && output.Data() == outputBorrow &&
        std::string_view(output.Data(), output.Size()) == outputBefore, "invalid global timing changed output");
    database.allScenes = {90, 3};
    Detail(error, "preserve this error output");
    error.code = SettingsSnapshotErrorCode::Path;
    const auto* aliasedOutput = error.detail.Data();
    Require(!WriteSceneLoadTimingDatabase(database, error.detail, error) && error.detail.Data() == aliasedOutput &&
        error.code == SettingsSnapshotErrorCode::Path && error.MessageView() == "preserve this error output",
        "output/error alias was mutated");

    const auto movedSnapshot = Snapshot(database);
    const auto* movedBorrow = database.Find("retained");
    SceneLoadTimingDatabase moved(std::move(database));
    Require(!database.Count() && !database.allScenes.completedLoadCount && moved.Find("retained") == movedBorrow &&
        Snapshot(moved) == movedSnapshot, "move construction did not transfer both owners");
    database = std::move(moved);
    Require(!moved.Count() && !moved.allScenes.completedLoadCount && database.Find("retained") == movedBorrow &&
        Snapshot(database) == movedSnapshot, "move assignment did not transfer both owners");
    auto& self = database; database = std::move(self);
    Require(database.Find("retained") == movedBorrow && Snapshot(database) == movedSnapshot, "self move changed history");
    database.Clear(); database.Clear();
    Require(!database.Count() && !database.allScenes.completedLoadCount && !database.Find("retained"), "clear retained history");
    ClearFailures();
    printf("history ownership: %zu assertions, %zu parse, %zu insert, %zu eviction and 1 JSON allocation failures\n",
        Assertions, parseFailures, insertFailures, evictionFailures);
    return Passed;
}

#if defined(UVSR_SCENE_HISTORY_STANDALONE_PROBE)
int main() { return TestSceneHistoryOwnership() ? 0 : 1; }
#endif
