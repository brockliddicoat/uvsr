#pragma once

#include "json_output.h"
#include <stddef.h>
#include <stdint.h>
#include <initializer_list>
#include <string_view>

namespace uvsr
{
    enum class SettingsSnapshotErrorCode : uint8_t
    {
        None, InvalidInput, OutOfMemory, Capacity, Duplicate, Escape,
        Catalog, Collision, Fingerprint, Path, Format
    };

    struct SettingsSnapshotError
    {
        SettingsSnapshotErrorCode code = SettingsSnapshotErrorCode::None;
        uint32_t nativeCode = 0;
        uint32_t cleanupCode = 0;
        // fixed reasons have static storage. path diagnostics own their full text.
        const char* message = "";
        json::EncodedText detail;
        [[nodiscard]] const char* Message() const noexcept
        {
            return detail.IsValid() ? detail.Data() : message;
        }
        [[nodiscard]] std::string_view MessageView() const noexcept
        {
            return detail.IsValid() ? std::string_view(detail.Data(), detail.Size())
                : std::string_view(message);
        }
        // failure preserves both owners and their views. failure must be distinct;
        // an aliased failure argument returns false without changing any operand.
        [[nodiscard]] bool CloneTo(SettingsSnapshotError& output,
            SettingsSnapshotError& failure) const noexcept;
    };

    [[nodiscard]] SettingsSnapshotError ComposeSettingsSnapshotError(
        std::initializer_list<std::string_view> parts,
        SettingsSnapshotErrorCode code = SettingsSnapshotErrorCode::InvalidInput,
        uint32_t nativeCode = 0, uint32_t cleanupCode = 0) noexcept;

    struct DecodedSetting
    {
        std::string_view name;
        std::string_view value;
    };

    // entries own both byte strings and remain in unsigned byte name order.
    // mutations, moves and destruction invalidate borrowed entries and views;
    // failed operations preserve the owner and all existing views.
    class DecodedSettings
    {
    public:
        DecodedSettings() noexcept = default;
        ~DecodedSettings() noexcept;
        DecodedSettings(const DecodedSettings&) = delete;
        DecodedSettings& operator=(const DecodedSettings&) = delete;
        DecodedSettings(DecodedSettings&& other) noexcept;
        DecodedSettings& operator=(DecodedSettings&& other) noexcept;

        [[nodiscard]] size_t Count() const noexcept { return m_Count; }
        [[nodiscard]] const DecodedSetting* Entries() const noexcept { return m_Entries; }
        [[nodiscard]] const DecodedSetting* Find(std::string_view name) const noexcept;
        [[nodiscard]] bool Insert(std::string_view name, std::string_view value,
            SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool Set(std::string_view name, std::string_view value,
            SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool Erase(std::string_view name) noexcept;
        [[nodiscard]] bool CloneTo(DecodedSettings& output, SettingsSnapshotError& error) const noexcept;
        void Clear() noexcept;

    private:
        DecodedSetting* m_Entries = nullptr;
        size_t m_Count = 0;
        size_t m_Capacity = 0;
        [[nodiscard]] size_t LowerBound(std::string_view name) const noexcept;
        [[nodiscard]] bool Store(std::string_view name, std::string_view value,
            bool replace, SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool Reserve(size_t count, SettingsSnapshotError& error) noexcept;
    };

#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
    void FailSettingsSnapshotAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearSettingsSnapshotAllocationFailure() noexcept;
#endif
}
