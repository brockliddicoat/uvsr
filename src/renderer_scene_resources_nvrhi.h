#pragma once

#include "renderer_import_image.h"
#include <nvrhi/nvrhi.h>

namespace uvsr
{
    class RendererCommonPasses;

    enum class RendererUploadPhase : uint8_t
    { Empty, Prepared, Uploading, Submitted, Complete, Failed, Canceled };
    enum class RendererUploadError : uint8_t
    { None, InvalidState, Input, Capacity, Allocation, Unsupported, Gpu };

    struct RendererUploadResult
    {
        RendererUploadError error = RendererUploadError::None;
        uint32_t index = InvalidSceneIndex;
        [[nodiscard]] explicit operator bool() const noexcept { return error == RendererUploadError::None; }
    };

    struct RendererUploadHealth
    {
        // synchronous error-counter/device-health check. no callback is retained.
        bool (*check)(void* context) noexcept = nullptr;
        void* context = nullptr;
    };

    struct RendererUploadOptions
    {
        bool rayTracing = false;
        bool generateMips = true;
        size_t maxStorageBytes = SIZE_MAX;
        uint64_t maxBufferBytes = UINT64_MAX;
        // format footprints, excluding driver alignment and private allocations.
        uint64_t maxTextureBytes = UINT64_MAX;
    };

    struct RendererUploadProgress
    {
        RendererUploadPhase phase = RendererUploadPhase::Empty;
        uint64_t submittedBytes = 0;
        uint64_t totalBytes = 0;
        uint64_t submissions = 0;
        uint32_t texturesSubmitted = 0;
        bool cpuBorrows = false;
        bool gpuComplete = true;
    };

    struct RendererSceneBufferResources
    {
        nvrhi::IBuffer* indices = nullptr;
        nvrhi::IBuffer* vertices = nullptr;
        // stable after CPU upload borrows end, including a later owner ordinal.
        uint32_t indexOwner = InvalidSceneIndex;
    };

    // private concrete backend owner. preparation consumes the sealed scene view;
    // geometry/image bytes and layouts stay borrowed until submission or Cancel.
    // resource views remain valid until Reset/destruction. draw consumers require
    // Submitted or Complete and the same generation on the graphics queue.
    // NVRHI retains submitted resources until actual completion. descriptor slots
    // and published scene replacement still follow the outer retirement boundary.
    class RendererSceneResourcesNvrhi final
    {
    public:
        explicit RendererSceneResourcesNvrhi(nvrhi::IDevice* device) noexcept;
        ~RendererSceneResourcesNvrhi() noexcept;
        RendererSceneResourcesNvrhi(const RendererSceneResourcesNvrhi&) = delete;
        RendererSceneResourcesNvrhi& operator=(const RendererSceneResourcesNvrhi&) = delete;
        // requires an empty owner. failure leaves it empty. the skin shader is
        // required only for derived skin groups; its layout is private to this owner.
        [[nodiscard]] RendererUploadResult Prepare(const RendererSceneView& scene,
            const ImportGeometry& geometry, ArrayView<const ImportDecodedImage> images,
            nvrhi::IShader* skinShader, RendererUploadHealth health,
            const RendererUploadOptions& options = {}) noexcept;
        // one whole texture subresource, mip draw or skin dispatch may exceed the
        // positive byte budget. buffer writes split at the exact remaining budget.
        [[nodiscard]] RendererUploadResult Step(size_t byteBudget, RendererCommonPasses* passes,
            RendererUploadHealth health) noexcept;
        [[nodiscard]] RendererUploadResult PollCompletion(RendererUploadHealth health) noexcept;
        void Cancel() noexcept;
        void Reset() noexcept;
        [[nodiscard]] RendererUploadProgress Progress() const noexcept;
        [[nodiscard]] uint64_t Generation() const noexcept;
        [[nodiscard]] uint32_t BufferCount() const noexcept;
        [[nodiscard]] uint32_t TextureCount() const noexcept;
        [[nodiscard]] RendererSceneBufferResources Buffer(uint32_t index) const noexcept;
        [[nodiscard]] nvrhi::ITexture* Texture(uint32_t index) const noexcept;
        [[nodiscard]] ImportDecodedImageInfo TextureInfo(uint32_t index) const noexcept;
        [[nodiscard]] size_t StorageBytes() const noexcept;
        [[nodiscard]] uint64_t BufferBytes() const noexcept;
        [[nodiscard]] uint64_t TextureBytes() const noexcept;

    private:
        struct State;
        nvrhi::DeviceHandle m_Device;
        State* m_State = nullptr;
    };

    [[nodiscard]] nvrhi::Format RendererImportImageFormat(ImportImageFormat format) noexcept;

#if defined(UVSR_BUILD_TESTING)
    enum class RendererUploadFailure : uint8_t { None, Allocation, Creation, Submission };
    void SetRendererUploadFailure(RendererUploadFailure failure, uint32_t ordinal) noexcept;
#endif
}
