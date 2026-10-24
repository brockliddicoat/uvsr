#include "noise_texture_library_nvrhi.h"
#include "renderer_log.h"
#include "noise_texture_asset.h"
#if defined(UVSR_BUILD_TESTING)
#include "uvsr_runtime.h"
#endif


namespace uvsr
{
    namespace
    {
        constexpr size_t InvalidCacheIndex =
            SIZE_MAX;

        size_t GetResolutionIndex(NoiseResolution resolution)
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

        size_t GetCacheIndex(
            NoisePattern pattern,
            NoiseResolution resolution)
        {
            if (!IsValidNoisePattern(pattern))
                return InvalidCacheIndex;
            const size_t resolutionIndex =
                GetResolutionIndex(resolution);
            if (resolutionIndex == InvalidCacheIndex)
                return InvalidCacheIndex;
            return size_t(static_cast<uint32_t>(pattern)) * 4u +
                resolutionIndex;
        }

        bool TryGetTextureByteCount(
            uint32_t resolution,
            uint32_t layers,
            uint64_t& result)
        {
            const uint64_t width = resolution;
            if (width == 0u || layers == 0u ||
                width > UINT64_MAX / width)
            {
                return false;
            }
            const uint64_t sliceBytes = width * width;
            if (uint64_t(layers) >
                UINT64_MAX / sliceBytes)
            {
                return false;
            }
            result = sliceBytes * uint64_t(layers);
            return true;
        }
    }

    NoiseTextureLibrary::NoiseTextureLibrary(
        nvrhi::IDevice* device,
        WindowsPath assetDirectory)
        : m_Device(device)
        , m_AssetDirectory(static_cast<WindowsPath&&>(assetDirectory))
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

        const size_t cacheIndex = GetCacheIndex(
            settings.pattern,
            settings.resolution);
        if (cacheIndex == InvalidCacheIndex ||
            cacheIndex >= CacheEntryCount)
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
                uint64_t(SIZE_MAX) ||
            expectedBytes >
                uint64_t(INT64_MAX))
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
        NoiseTextureAsset asset(m_AssetDirectory.Data(), fileName, expectedBytes);
        const auto& read = asset.Result();
        switch (read.error)
        {
        case NoiseTextureAssetError::None: break;
        case NoiseTextureAssetError::Open:
            log::error(
                "Could not open noise texture asset '%s'. Verify that the "
                "UVSR noise assets were packaged.", asset.PathText());
            return {};
        case NoiseTextureAssetError::Size:
            log::error(
                "Noise texture asset '%s' has %llu bytes; expected exactly "
                "%llu bytes for %u x %u x %u R8.", asset.PathText(),
                static_cast<unsigned long long>(read.measuredBytes),
                static_cast<unsigned long long>(expectedBytes), resolution, resolution, layers);
            return {};
        case NoiseTextureAssetError::Read:
            log::error("Could not read the complete noise texture asset '%s'.", asset.PathText());
            return {};
        case NoiseTextureAssetError::Allocation:
            log::error("Could not allocate noise texture asset storage for '%s'.", fileName);
            return {};
        case NoiseTextureAssetError::Path:
            log::error("Could not prepare noise texture asset path for '%s' (error %u).",
                fileName, read.systemCode);
            return {};
        }

        nvrhi::TextureDesc description;
        description.width = resolution;
        description.height = resolution;
        description.arraySize = layers;
        description.mipLevels = 1u;
        description.format = nvrhi::Format::R8_UNORM;
        description.dimension = nvrhi::TextureDimension::Texture2DArray;
#if defined(UVSR_BUILD_TESTING)
        const bool mutableForRuntimeProbe = g_VerifyRetainedRuntimeRequested;
#else
        constexpr bool mutableForRuntimeProbe = false;
#endif
        description.initialState = mutableForRuntimeProbe
            ? nvrhi::ResourceStates::ShaderResource : nvrhi::ResourceStates::CopyDest;
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
                asset.PathText());
            return {};
        }

        const size_t sliceBytes =
            size_t(resolution) * size_t(resolution);
        for (uint32_t layer = 0u; layer < layers; ++layer)
        {
            commandList->writeTexture(
                texture,
                layer,
                0u,
                asset.Data() + size_t(layer) * sliceBytes,
                size_t(resolution));
        }
        // diagnostic copies need tracked transitions, including enhanced texture layouts.
        if (!mutableForRuntimeProbe)
            commandList->setPermanentTextureState(texture, nvrhi::ResourceStates::ShaderResource);

        entry.texture = texture;
        m_ResidentBytes += expectedBytes;
        return { entry.texture.Get(), resolution, layers };
    }
}
