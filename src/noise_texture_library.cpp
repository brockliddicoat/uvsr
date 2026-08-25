#include "noise_texture_library.h"
#include "renderer_log.h"

#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace uvsr
{
    namespace
    {
        constexpr std::size_t InvalidCacheIndex =
            std::numeric_limits<std::size_t>::max();

        std::size_t GetResolutionIndex(NoiseResolution resolution)
        {
            switch (resolution)
            {
            case NoiseResolution::Size64:
                return 0u;
            case NoiseResolution::Size128:
                return 1u;
            case NoiseResolution::Size256:
                return 2u;
            case NoiseResolution::Size512:
                return 3u;
            default:
                return InvalidCacheIndex;
            }
        }

        std::size_t GetCacheIndex(
            NoisePattern pattern,
            NoiseResolution resolution)
        {
            if (!IsValidNoisePattern(pattern))
                return InvalidCacheIndex;
            const std::size_t resolutionIndex =
                GetResolutionIndex(resolution);
            if (resolutionIndex == InvalidCacheIndex)
                return InvalidCacheIndex;
            return std::size_t(static_cast<uint32_t>(pattern)) * 4u +
                resolutionIndex;
        }

        bool TryGetTextureByteCount(
            uint32_t resolution,
            uint32_t layers,
            uint64_t& result)
        {
            const uint64_t width = resolution;
            if (width == 0u || layers == 0u ||
                width > std::numeric_limits<uint64_t>::max() / width)
            {
                return false;
            }
            const uint64_t sliceBytes = width * width;
            if (uint64_t(layers) >
                std::numeric_limits<uint64_t>::max() / sliceBytes)
            {
                return false;
            }
            result = sliceBytes * uint64_t(layers);
            return true;
        }
    }

    NoiseTextureLibrary::NoiseTextureLibrary(
        nvrhi::IDevice* device,
        std::filesystem::path assetDirectory)
        : m_Device(device)
        , m_AssetDirectory(std::move(assetDirectory))
    {
    }

    NoiseTextureBinding NoiseTextureLibrary::Resolve(
        nvrhi::ICommandList* commandList,
        const NoiseSettings& settings)
    {
        if (!m_Device || !commandList)
        {
            log::error(
                "Noise texture resolution requires a device and command list.");
            return {};
        }
        if (!IsValidNoiseSettings(settings))
        {
            log::error(
                "Noise texture settings are invalid (pattern %u, resolution %u).",
                static_cast<uint32_t>(settings.pattern),
                static_cast<uint32_t>(settings.resolution));
            return {};
        }

        const std::size_t cacheIndex = GetCacheIndex(
            settings.pattern,
            settings.resolution);
        if (cacheIndex == InvalidCacheIndex ||
            cacheIndex >= m_Cache.size())
        {
            log::error(
                "Noise texture settings could not be mapped to a cache entry.");
            return {};
        }

        const uint32_t resolution =
            GetNoiseResolutionValue(settings.resolution);
        const uint32_t layers = GetNoiseLayerCount(settings.pattern);
        CacheEntry& entry = m_Cache[cacheIndex];
        if (entry.texture)
            return { entry.texture.Get(), resolution, layers };
        if (entry.attempted)
            return {};
        entry.attempted = true;

        uint64_t expectedBytes = 0u;
        if (!TryGetTextureByteCount(
                resolution,
                layers,
                expectedBytes) ||
            expectedBytes >
                uint64_t(std::numeric_limits<std::size_t>::max()) ||
            expectedBytes >
                uint64_t(std::numeric_limits<std::streamsize>::max()))
        {
            log::error(
                "Noise texture dimensions overflow the supported upload size "
                "(%u x %u x %u).",
                resolution,
                resolution,
                layers);
            return {};
        }

        const char* fileName = GetNoiseAssetFileName(
            settings.pattern,
            settings.resolution);
        const std::filesystem::path path = m_AssetDirectory / fileName;
        const std::string pathText = path.generic_string();
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            log::error(
                "Could not open noise texture asset '%s'. Verify that the "
                "UVSR noise assets were packaged.",
                pathText.c_str());
            return {};
        }

        const std::streamoff actualSize =
            static_cast<std::streamoff>(input.tellg());
        if (actualSize < 0 ||
            uint64_t(actualSize) != expectedBytes)
        {
            const unsigned long long actualBytes = actualSize < 0
                ? 0ull
                : static_cast<unsigned long long>(actualSize);
            log::error(
                "Noise texture asset '%s' has %llu bytes; expected exactly "
                "%llu bytes for %u x %u x %u R8.",
                pathText.c_str(),
                actualBytes,
                static_cast<unsigned long long>(expectedBytes),
                resolution,
                resolution,
                layers);
            return {};
        }

        std::vector<uint8_t> bytes(
            static_cast<std::size_t>(expectedBytes));
        input.seekg(0, std::ios::beg);
        if (!input.read(
                reinterpret_cast<char*>(bytes.data()),
                std::streamsize(expectedBytes)))
        {
            log::error(
                "Could not read the complete noise texture asset '%s'.",
                pathText.c_str());
            return {};
        }

        nvrhi::TextureDesc description;
        description.width = resolution;
        description.height = resolution;
        description.arraySize = layers;
        description.mipLevels = 1u;
        description.format = nvrhi::Format::R8_UNORM;
        description.dimension = nvrhi::TextureDimension::Texture2DArray;
        description.initialState = nvrhi::ResourceStates::CopyDest;
        description.keepInitialState = true;
        description.debugName = GetNoisePatternLabel(settings.pattern);
        nvrhi::TextureHandle texture =
            m_Device->createTexture(description);
        if (!texture)
        {
            log::error(
                "Could not create the %u x %u x %u R8 noise texture for '%s'.",
                resolution,
                resolution,
                layers,
                pathText.c_str());
            return {};
        }

        const std::size_t sliceBytes =
            std::size_t(resolution) * std::size_t(resolution);
        for (uint32_t layer = 0u; layer < layers; ++layer)
        {
            commandList->writeTexture(
                texture,
                layer,
                0u,
                bytes.data() + std::size_t(layer) * sliceBytes,
                std::size_t(resolution));
        }
        commandList->setPermanentTextureState(
            texture,
            nvrhi::ResourceStates::ShaderResource);

        entry.texture = texture;
        m_ResidentBytes += expectedBytes;
        return { entry.texture.Get(), resolution, layers };
    }
}
