#pragma once

#include "file_bytes.h"
#include "windows_path_text.h"

namespace uvsr
{
    enum class NoiseTextureAssetError : uint8_t { None, Path, Allocation, Open, Size, Read };
    struct NoiseTextureAssetResult
    {
        NoiseTextureAssetError error = NoiseTextureAssetError::None;
        uint32_t systemCode = 0;
        uint64_t measuredBytes = 0;
    };

    // one lazy load owns its diagnostic text and CPU bytes through synchronous
    // upload. filenames come from the fixed ASCII noise asset catalog.
    class NoiseTextureAsset
    {
    public:
        NoiseTextureAsset(const wchar_t* directory, const char* fileName, uint64_t expectedBytes) noexcept;
        NoiseTextureAsset(const NoiseTextureAsset&) = delete;
        NoiseTextureAsset& operator=(const NoiseTextureAsset&) = delete;
        [[nodiscard]] const NoiseTextureAssetResult& Result() const noexcept { return m_Result; }
        [[nodiscard]] const char* PathText() const noexcept { return m_PathText.Data(); }
        [[nodiscard]] const char* Data() const noexcept { return m_Bytes.Data(); }
        [[nodiscard]] size_t Size() const noexcept { return m_Bytes.Size(); }
    private:
        FileBytes m_Bytes;
        WindowsPathText m_PathText;
        NoiseTextureAssetResult m_Result;
    };

}
