#include "noise_texture_asset.h"
#include "windows_executable_path.h"

#include <string.h>

namespace uvsr
{
    NoiseTextureAsset::NoiseTextureAsset(const wchar_t* directory, const char* fileName,
        uint64_t expectedBytes) noexcept
    {
        // capacity follows the longest retained filename; every catalog name is
        // checked by the asset tests. reject overflow without truncation.
        wchar_t filename[sizeof("spatiotemporal-blue-512x512x64-r8.bin")];
        const size_t length = fileName ? strlen(fileName) : 0;
        if (!length || length >= sizeof(filename) / sizeof(wchar_t))
        {
            m_Result.error = NoiseTextureAssetError::Path;
            return;
        }
        for (size_t index = 0; index < length; ++index)
        {
            const auto value = static_cast<unsigned char>(fileName[index]);
            if (value >= 128)
            {
                m_Result.error = NoiseTextureAssetError::Path;
                return;
            }
            filename[index] = wchar_t(value);
        }
        filename[length] = L'\0';
        WindowsPath path;
        WindowsPathResult pathResult;
        if (!JoinWindowsRelativePath(directory, filename, path, pathResult))
        {
            m_Result = {pathResult.error == WindowsPathError::Allocation ?
                NoiseTextureAssetError::Allocation : NoiseTextureAssetError::Path, pathResult.nativeCode, 0};
            return;
        }
        WindowsPathTextResult encoded;
        if (!m_PathText.Assign(path.Data(), path.Size(), WindowsPathTextForm::Generic,
                WindowsPathTextEncoding::Filesystem, encoded))
        {
            m_Result = {encoded.error == WindowsPathTextError::Allocation ?
                NoiseTextureAssetError::Allocation : NoiseTextureAssetError::Path, encoded.nativeCode, 0};
            return;
        }
        FileReadResult read;
        uint64_t measuredBytes = 0;
        if (!ReadFileBytesExact(path.Data(), expectedBytes, m_Bytes, read, measuredBytes))
        {
            NoiseTextureAssetError error = NoiseTextureAssetError::Open;
            switch (read.error)
            {
            case FileReadError::Measure:
            case FileReadError::TooLarge:
            case FileReadError::SizeMismatch: error = NoiseTextureAssetError::Size; break;
            case FileReadError::OutOfMemory: error = NoiseTextureAssetError::Allocation; break;
            case FileReadError::Read:
            case FileReadError::Close: error = NoiseTextureAssetError::Read; break;
            default: break;
            }
            m_Result = {error, read.systemCode, measuredBytes};
            return;
        }
        m_Result.measuredBytes = measuredBytes;
    }
}
