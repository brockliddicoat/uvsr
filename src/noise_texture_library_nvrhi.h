#pragma once

#include "noise_settings.h"

#include <nvrhi/nvrhi.h>

#include <stddef.h>
#include <stdint.h>
#include "windows_executable_path.h"

namespace uvsr
{
    struct NoiseTextureBinding
    {
        nvrhi::ITexture* texture = nullptr;
        uint32_t resolution = 0u;
        uint32_t layers = 0u;

        [[nodiscard]] explicit operator bool() const
        {
            return texture != nullptr;
        }
    };

    class NoiseTextureLibrary final
    {
    public:
        NoiseTextureLibrary(
            nvrhi::IDevice* device,
            WindowsPath assetDirectory);

        [[nodiscard]] NoiseTextureBinding Resolve(
            nvrhi::ICommandList* commandList,
            const NoiseSettings& settings);

        [[nodiscard]] uint64_t GetResidentBytes() const
        {
            return m_ResidentBytes;
        }

    private:
        struct CacheEntry
        {
            nvrhi::TextureHandle texture;
            bool attempted = false;
        };

        static constexpr size_t CacheEntryCount = 12u;

        nvrhi::DeviceHandle m_Device;
        WindowsPath m_AssetDirectory;
        CacheEntry m_Cache[CacheEntryCount];
        uint64_t m_ResidentBytes = 0u;
    };
}
