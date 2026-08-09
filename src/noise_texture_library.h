#pragma once

#include "noise_settings.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

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
            std::filesystem::path assetDirectory);

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

        static constexpr std::size_t CacheEntryCount = 12u;

        nvrhi::DeviceHandle m_Device;
        std::filesystem::path m_AssetDirectory;
        std::array<CacheEntry, CacheEntryCount> m_Cache;
        uint64_t m_ResidentBytes = 0u;
    };
}
