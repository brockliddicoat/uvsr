#pragma once

#include "array_view.h"
#include <array>
#include <optional>
#include <string_view>

namespace uvsr
{
    namespace json { class EncodedText; }
    struct SettingsSnapshotError;
    struct SceneInitialCamera
    {
        std::array<float, 3> Position{};
        std::array<float, 3> Direction{0.f, 0.f, -1.f};
        std::array<float, 3> Up{0.f, 1.f, 0.f};
        float VerticalFovDegrees = 60.f;
    };
    struct SceneCatalogEntry
    {
        // paths locate assets, command names identify snapshots, and friendly
        // labels affect only the UI. all three borrow the catalog's text arena.
        std::string_view FileName;
        std::string_view CommandName;
        std::string_view DisplayName;
        std::optional<SceneInitialCamera> InitialCamera;
    };
    class SceneCatalog
    {
    public:
        SceneCatalog() noexcept = default;
        ~SceneCatalog() noexcept;
        SceneCatalog(const SceneCatalog&) = delete;
        SceneCatalog& operator=(const SceneCatalog&) = delete;
        SceneCatalog(SceneCatalog&& other) noexcept;
        SceneCatalog& operator=(SceneCatalog&& other) noexcept;
        [[nodiscard]] size_t Count() const noexcept { return m_Count; }
        [[nodiscard]] const SceneCatalogEntry* Entries() const noexcept { return m_Entries; }
        [[nodiscard]] const SceneCatalogEntry& operator[](size_t index) const noexcept { return m_Entries[index]; }
        [[nodiscard]] const SceneCatalogEntry* begin() const noexcept { return m_Entries; }
        [[nodiscard]] const SceneCatalogEntry* end() const noexcept { return m_Count ? m_Entries + m_Count : m_Entries; }
        void Clear() noexcept;
    private:
        SceneCatalogEntry* m_Entries = nullptr;
        char* m_Text = nullptr;
        size_t m_Count = 0;
        friend struct SceneCatalogBuilder;
    };

    // inputs are borrowed synchronously. failed operations preserve output and
    // its borrowed entries/text. successful publication, moves and Clear invalidate views.
    [[nodiscard]] bool BuildSceneCatalog(std::wstring_view sceneDirectory,
        ArrayView<const std::string_view> discoveredSceneFiles,
        SceneCatalog& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool MakeSceneDisplayName(std::wstring_view sceneDirectory,
        std::wstring_view fileName, json::EncodedText& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool MakeSceneDisplayName(std::wstring_view sceneDirectory,
        std::string_view fileName, json::EncodedText& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool FindSceneCatalogEntry(const SceneCatalog& catalog,
        std::string_view fileName, const SceneCatalogEntry*& output, SettingsSnapshotError& error) noexcept;

    // a request borrows matched catalog metadata; the catalog must outlive it.
    // external names and labels, and every import filename, have one text owner.
    // a worker may borrow ImportFileName only while its request remains unchanged.
    class SceneLoadRequest
    {
    public:
        SceneLoadRequest() noexcept = default;
        ~SceneLoadRequest() noexcept;
        SceneLoadRequest(const SceneLoadRequest&) = delete;
        SceneLoadRequest& operator=(const SceneLoadRequest&) = delete;
        SceneLoadRequest(SceneLoadRequest&& other) noexcept;
        SceneLoadRequest& operator=(SceneLoadRequest&& other) noexcept;
        [[nodiscard]] std::string_view FileName() const noexcept { return m_FileName; }
        [[nodiscard]] std::string_view DisplayName() const noexcept { return m_DisplayName; }
        [[nodiscard]] std::string_view ImportFileName() const noexcept { return m_ImportFileName; }
        [[nodiscard]] const SceneCatalogEntry* CatalogEntry() const noexcept { return m_Entry; }
        void Clear() noexcept;
    private:
        char* m_Text = nullptr;
        std::string_view m_FileName = "", m_DisplayName = "", m_ImportFileName = "";
        const SceneCatalogEntry* m_Entry = nullptr;
        friend bool PrepareSceneLoadRequest(std::wstring_view, std::string_view,
            const SceneCatalogEntry*, SceneLoadRequest&, SettingsSnapshotError&) noexcept;
    };
    // entry is the checked catalog match, or null for an external path. callers
    // compare the resolved filename with the current request before preparing it.
    [[nodiscard]] bool PrepareSceneLoadRequest(std::wstring_view sceneDirectory,
        std::string_view fileName, const SceneCatalogEntry* entry,
        SceneLoadRequest& output, SettingsSnapshotError& error) noexcept;

#if defined(UVSR_SCENE_CATALOG_TEST_HOOKS)
    void FailSceneCatalogAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearSceneCatalogAllocationFailure() noexcept;
#endif
}
