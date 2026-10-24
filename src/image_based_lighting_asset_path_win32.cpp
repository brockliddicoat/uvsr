#include "image_based_lighting_asset_path.h"

namespace uvsr
{
    namespace
    {
        constexpr size_t RelativeCapacity() noexcept
        {
            size_t maximum = 0;
            for (const auto& info : ImageBasedLightingSourceCatalog)
            {
                size_t size = 0;
                while (info.relativePath[size]) ++size;
                if (size > maximum) maximum = size;
            }
            return maximum + 1;
        }
        bool Fail(ImageBasedLightingPathResult& result, ImageBasedLightingPathError error,
            uint32_t code = 0) noexcept
        {
            result = {error, code}; return false;
        }
    }

    bool ImageBasedLightingAssetPath::Prepare(const wchar_t* directory, ImageBasedLightingSource source,
        ImageBasedLightingPathResult& result) noexcept
    {
        result = {};
        const char* relative = GetImageBasedLightingSourceInfo(source).relativePath;
        wchar_t wide[RelativeCapacity()];
        size_t size = 0;
        while (relative[size])
        {
            const auto value = static_cast<unsigned char>(relative[size]);
            if (value >= 128 || size + 1 >= RelativeCapacity())
                return Fail(result, ImageBasedLightingPathError::Path);
            wide[size++] = wchar_t(value);
        }
        wide[size] = L'\0';
        WindowsPath candidate;
        WindowsPathResult joined;
        if (!JoinWindowsRelativePath(directory, wide, candidate, joined))
            return Fail(result, joined.error == WindowsPathError::Allocation ?
                ImageBasedLightingPathError::Allocation : ImageBasedLightingPathError::Path, joined.nativeCode);
        WindowsPathText text;
        WindowsPathTextResult encoded;
        if (!text.Assign(candidate.Data(), candidate.Size(), WindowsPathTextForm::Native,
                WindowsPathTextEncoding::Filesystem, encoded))
            return Fail(result, encoded.error == WindowsPathTextError::Allocation ?
                ImageBasedLightingPathError::Allocation : ImageBasedLightingPathError::Path, encoded.nativeCode);
        m_Text = static_cast<WindowsPathText&&>(text);
        m_Path = static_cast<WindowsPath&&>(candidate);
        return true;
    }
    bool ImageBasedLightingAssetPath::MakeGeneric(ImageBasedLightingPathResult& result) noexcept
    {
        result = {};
        if (!m_Path.Size()) return Fail(result, ImageBasedLightingPathError::Path);
        WindowsPathTextResult encoded;
        if (!m_Text.Assign(m_Path.Data(), m_Path.Size(), WindowsPathTextForm::Generic,
                WindowsPathTextEncoding::Filesystem, encoded))
            return Fail(result, encoded.error == WindowsPathTextError::Allocation ?
                ImageBasedLightingPathError::Allocation : ImageBasedLightingPathError::Path, encoded.nativeCode);
        return true;
    }
}
