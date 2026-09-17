#pragma once

#include "renderer_import_composition.h"
#include "renderer_import_image.h"

namespace uvsr
{
    struct ImportCancellation
    {
        bool (*requested)(void* context) noexcept = nullptr;
        void* context = nullptr;
        [[nodiscard]] bool IsRequested() const noexcept { return requested && requested(context); }
    };

    // one synchronous file read owns its bytes. allocation failure preserves an
    // existing buffer. readers write through WritableBytes before handing it off.
    class ImportFileData final
    {
    public:
        ImportFileData() noexcept = default;
        ~ImportFileData() noexcept;
        ImportFileData(const ImportFileData&) = delete;
        ImportFileData& operator=(const ImportFileData&) = delete;
        ImportFileData(ImportFileData&& other) noexcept;
        ImportFileData& operator=(ImportFileData&& other) noexcept;
        [[nodiscard]] ImportResult Allocate(size_t count) noexcept;
        [[nodiscard]] ArrayView<const uint8_t> Bytes() const noexcept { return {m_Data, m_Count}; }
        [[nodiscard]] ArrayView<uint8_t> WritableBytes() noexcept { return {m_Data, m_Count}; }
        void Reset() noexcept;
    private:
        uint8_t* m_Data = nullptr;
        size_t m_Count = 0;
    };

    struct ImportFileSource
    {
        // paths, cancellation and context are borrowed only during each call.
        // read checks the byte limit before allocation and preserves output on
        // any failure. success returns the entire file, including an empty file.
        ImportResult (*read)(void* context, ArrayView<const char> path, size_t byteLimit,
            ImportFileData& output, ImportCancellation cancellation) noexcept = nullptr;
        ImportResult (*exists)(void* context, ArrayView<const char> path, bool& present) noexcept = nullptr;
        void* context = nullptr;
    };

    enum class ImportLoadState : uint8_t { Idle, Running, Ready, Failed, Canceled };
    enum class ImportLoadOperation : uint8_t
    {
        None, ReadDescription, ParseDescription, ReadModel, ParseModel,
        ReadBuffer, ConvertModel, ComposeScene, PrepareImages, DecodeImage, Finalize
    };

    struct ImportLoadProgress
    {
        ImportLoadState state = ImportLoadState::Idle;
        ImportLoadOperation operation = ImportLoadOperation::None;
        uint32_t objectsTotal = 0, objectsCompleted = 0, objectsUnavailable = 0;
        uint32_t texturesTotal = 0, texturesDecoded = 0;
        uint32_t modelIndex = InvalidSceneIndex, textureIndex = InvalidSceneIndex;
        uint64_t importStepsTotal = 0, importStepsCompleted = 0;
        uint64_t filesRead = 0, fileBytesRead = 0;
        bool pathTruncated = false;
        ImportResult result;
    };

    struct ImportLoadCallbacks
    {
        ImportCancellation cancellation;
        // copied progress plus a synchronous diagnostic path (at most511 bytes,
        // with pathTruncated set). the receiver provides
        // synchronization if another thread reads its copy. no callback may
        // reenter this load or retain a view into its working owners.
        void (*report)(void* context, const ImportLoadProgress& progress,
            ArrayView<const char> path) noexcept = nullptr;
        void* context = nullptr;
        // exactly once per unavailable description model. the full model path
        // is borrowed during this call; log/copy it here. progress snapshots
        // retain the count, not a diagnostic history.
        void (*modelUnavailable)(void* context, uint32_t modelIndex,
            ArrayView<const char> modelPath, ImportLoadOperation operation, ImportResult result) noexcept = nullptr;
    };

    struct ImportSceneLoadOptions
    {
        uint64_t generation = 0;
        size_t maxFileBytes = size_t(PTRDIFF_MAX);
        size_t maxPathBytes = size_t(PTRDIFF_MAX);
        uint32_t maxModels = UINT32_MAX - 1;
        // model slot/availability arrays and one resolved path. child scene,
        // parser, file and codec owners have separate budgets and measurements.
        size_t maxLoadingBytes = SIZE_MAX;
        ImportDescriptionLimits description;
        size_t maxConversionScratchBytes = SIZE_MAX;
        // per-model and final composed owners, not their simultaneous sum.
        size_t maxGeometryBytes = SIZE_MAX;
        size_t maxEncodedImageBytes = SIZE_MAX;
        // the final decoded array plus every contained decoded-image allocation.
        size_t maxDecodedBytes = SIZE_MAX;
        size_t maxTemporaryPixelBytes = SIZE_MAX;
        ImportRuntimeLightOptions runtimeLights;
    };

    struct ImportSceneLoadAccess;
    class ImportDecodedImages final
    {
    public:
        ImportDecodedImages() noexcept = default;
        ~ImportDecodedImages() noexcept;
        ImportDecodedImages(const ImportDecodedImages&) = delete;
        ImportDecodedImages& operator=(const ImportDecodedImages&) = delete;
        ImportDecodedImages(ImportDecodedImages&& other) noexcept;
        ImportDecodedImages& operator=(ImportDecodedImages&& other) noexcept;
        [[nodiscard]] ArrayView<const ImportDecodedImage> Images() const noexcept { return {m_Images, m_Count}; }
        [[nodiscard]] size_t StorageBytes() const noexcept { return m_StorageBytes; }
        // all upload recording borrows must end before reset or replacement.
        void Reset() noexcept;
    private:
        ImportDecodedImage* m_Images = nullptr;
        size_t m_Count = 0, m_StorageBytes = 0;
        friend struct ImportSceneLoadAccess;
    };

    struct ImportLoadedScene
    {
        RendererScene scene;
        ImportGeometry geometry;
        ImportDecodedImages images;
        ImportCompositionStats composition;
        ImportRuntimeLightIds runtimeLights;
        ImportLoadProgress progress;
        size_t peakLoadingBytes = 0;
    };

    // exclusive, synchronous CPU transaction. success replaces output; failure
    // or cancellation preserves it. output must have no external live borrows.
    // parser, file, description and encoded-image owners die before return.
    // Ready means CPU-ready, not uploaded or visible. a worker caller joins before
    // handing this aggregate to render/collision consumers.
    [[nodiscard]] ImportResult LoadImportScene(ImportFileSource files, ArrayView<const char> path,
        const ImportSceneLoadOptions& options, ImportLoadedScene& output,
        ImportLoadCallbacks callbacks = {}) noexcept;

    // private platform implementation; paths are UTF-8, returned values are plain.
    [[nodiscard]] ImportFileSource NativeImportFileSource() noexcept;
}
