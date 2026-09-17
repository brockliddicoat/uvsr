#pragma once

#include "windows_executable_path.h"
#include "windows_path_text.h"
#include "image_based_lighting_sources.h"

namespace uvsr
{
    enum class ImageBasedLightingPathError : uint8_t { None, Path, Allocation };
    struct ImageBasedLightingPathResult
    {
        ImageBasedLightingPathError error = ImageBasedLightingPathError::None;
        uint32_t nativeCode = 0;
    };

    // one local preparation owns the native filename. generic text is requested
    // only on a diagnostic branch; failed conversion preserves the current text.
    class ImageBasedLightingAssetPath
    {
    public:
        ImageBasedLightingAssetPath() noexcept = default;
        ImageBasedLightingAssetPath(const ImageBasedLightingAssetPath&) = delete;
        ImageBasedLightingAssetPath& operator=(const ImageBasedLightingAssetPath&) = delete;
        [[nodiscard]] bool Prepare(const wchar_t* directory, ImageBasedLightingSource source,
            ImageBasedLightingPathResult& result) noexcept;
        [[nodiscard]] bool MakeGeneric(ImageBasedLightingPathResult& result) noexcept;
        [[nodiscard]] const char* Text() const noexcept { return m_Text.Data(); }
        [[nodiscard]] const WindowsPath& NativePath() const noexcept { return m_Path; }
    private:
        WindowsPath m_Path;
        WindowsPathText m_Text;
    };

}
