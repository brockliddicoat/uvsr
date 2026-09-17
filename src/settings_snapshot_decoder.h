#pragma once

#include "settings_snapshot_code.h"
#include "settings_snapshot_storage.h"

namespace uvsr
{
    enum class SettingsSnapshotCatalogLocation : uint8_t { ExecutableState, Installed };

    // paths own terminated UTF-16 text. successful mutations invalidate views.
    class SettingsSnapshotCatalogPaths
    {
    public:
        SettingsSnapshotCatalogPaths() noexcept = default;
        ~SettingsSnapshotCatalogPaths() noexcept;
        SettingsSnapshotCatalogPaths(const SettingsSnapshotCatalogPaths&) = delete;
        SettingsSnapshotCatalogPaths& operator=(const SettingsSnapshotCatalogPaths&) = delete;
        SettingsSnapshotCatalogPaths(SettingsSnapshotCatalogPaths&& other) noexcept;
        SettingsSnapshotCatalogPaths& operator=(SettingsSnapshotCatalogPaths&& other) noexcept;
        [[nodiscard]] size_t Count() const noexcept { return m_Count; }
        [[nodiscard]] const wchar_t* Path(size_t index) const noexcept;
        [[nodiscard]] bool Append(const wchar_t* path, SettingsSnapshotError& error) noexcept;
        void Sort() noexcept;
        void Clear() noexcept;
    private:
        wchar_t** m_Paths = nullptr;
        size_t m_Count = 0;
        size_t m_Capacity = 0;
    };

    // matching payloads remain owned after the catalog file closes or disappears.
    class SettingsSnapshotMatches
    {
    public:
        SettingsSnapshotMatches() noexcept = default;
        ~SettingsSnapshotMatches() noexcept;
        SettingsSnapshotMatches(const SettingsSnapshotMatches&) = delete;
        SettingsSnapshotMatches& operator=(const SettingsSnapshotMatches&) = delete;
        SettingsSnapshotMatches(SettingsSnapshotMatches&& other) noexcept;
        SettingsSnapshotMatches& operator=(SettingsSnapshotMatches&& other) noexcept;
        [[nodiscard]] size_t Count() const noexcept { return m_Count; }
        [[nodiscard]] std::string_view Text(size_t index) const noexcept;
        [[nodiscard]] bool Append(json::EncodedText&& text, SettingsSnapshotError& error) noexcept;
        void Clear() noexcept;
    private:
        json::EncodedText* m_Text = nullptr;
        size_t m_Count = 0;
        size_t m_Capacity = 0;
    };

    // output owners are replaced only on success. error owns any dynamic detail.
    [[nodiscard]] bool GetDefaultSettingsSnapshotCatalogPaths(std::string_view version,
        SettingsSnapshotCatalogLocation location, SettingsSnapshotCatalogPaths& output,
        SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool ReadMatchingSettingsSnapshots(const wchar_t* catalogPath,
        std::string_view code, SettingsSnapshotMatches& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool UnescapeSettingsSnapshotValue(std::string_view value,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool ParseSettingsSnapshot(std::string_view canonicalSettings,
        DecodedSettings& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool DecodeSettingsSnapshot(std::string_view code,
        const SettingsSnapshotCatalogPaths& catalogPaths, DecodedSettings& output,
        SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool FormatCanonicalSettingsSnapshot(const DecodedSettings& settings,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool FormatDecodedSettingsJson(const DecodedSettings& settings,
        json::EncodedText& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool FormatSettingsSnapshotCatalogSection(std::string_view code,
        std::string_view canonical, json::EncodedText& output, SettingsSnapshotError& error) noexcept;

#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
    // path is borrowed until a matching probe or an explicit clear.
    void FailSettingsSnapshotCatalogProbeOnce(const wchar_t* path, uint32_t nativeCode) noexcept;
    void ClearSettingsSnapshotCatalogProbeFailure() noexcept;
#endif
}
