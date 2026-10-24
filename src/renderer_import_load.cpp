#include "renderer_import_load.h"
#include "renderer_import_path.h"
#include "import/renderer_import_allocation.h"

#include <new>
#include <stdlib.h>
#include <string.h>

namespace uvsr
{
    ImportFileData::~ImportFileData() noexcept { Reset(); }
    ImportFileData::ImportFileData(ImportFileData&& other) noexcept : m_Data(other.m_Data), m_Count(other.m_Count)
    { other.m_Data = nullptr; other.m_Count = 0; }
    ImportFileData& ImportFileData::operator=(ImportFileData&& other) noexcept
    {
        if (this != &other)
        {
            Reset(); m_Data = other.m_Data; m_Count = other.m_Count;
            other.m_Data = nullptr; other.m_Count = 0;
        }
        return *this;
    }
    ImportResult ImportFileData::Allocate(size_t count) noexcept
    {
        if (count > size_t(PTRDIFF_MAX)) return {ImportError::Capacity};
        auto* data = count ? static_cast<uint8_t*>(ImportAllocate(count)) : nullptr;
        if (count && !data) return {ImportError::OutOfMemory};
        Reset(); m_Data = data; m_Count = count; return {};
    }
    void ImportFileData::Reset() noexcept { free(m_Data); m_Data = nullptr; m_Count = 0; }

    ImportDecodedImages::~ImportDecodedImages() noexcept { Reset(); }
    ImportDecodedImages::ImportDecodedImages(ImportDecodedImages&& other) noexcept
        : m_Images(other.m_Images), m_Count(other.m_Count), m_StorageBytes(other.m_StorageBytes)
    { other.m_Images = nullptr; other.m_Count = other.m_StorageBytes = 0; }
    ImportDecodedImages& ImportDecodedImages::operator=(ImportDecodedImages&& other) noexcept
    {
        if (this != &other)
        {
            Reset(); m_Images = other.m_Images; m_Count = other.m_Count; m_StorageBytes = other.m_StorageBytes;
            other.m_Images = nullptr; other.m_Count = other.m_StorageBytes = 0;
        }
        return *this;
    }
    void ImportDecodedImages::Reset() noexcept
    {
        for (size_t i = 0; i < m_Count; ++i) m_Images[i].~ImportDecodedImage();
        free(m_Images); m_Images = nullptr; m_Count = m_StorageBytes = 0;
    }

    struct ImportSceneLoadAccess
    {
        static ImportResult PrepareImages(size_t count, size_t limit, ImportDecodedImages& output) noexcept
        {
            if (output.m_Images || output.m_Count) return {ImportError::InvalidState, ImportObject::Image};
            if (count > size_t(PTRDIFF_MAX) / sizeof(ImportDecodedImage) || count * sizeof(ImportDecodedImage) > limit)
                return {ImportError::Capacity, ImportObject::Image};
            if (!count) return {};
            auto* images = static_cast<ImportDecodedImage*>(ImportAllocate(count * sizeof(ImportDecodedImage)));
            if (!images) return {ImportError::OutOfMemory, ImportObject::Image};
            for (size_t i = 0; i < count; ++i) new (images + i) ImportDecodedImage{};
            output.m_Images = images; output.m_Count = count; output.m_StorageBytes = count * sizeof(ImportDecodedImage);
            return {};
        }
        static ImportResult Decode(size_t index, const ImportImageView& input,
            ImportImageDecodeOptions options, size_t limit, ImportDecodedImages& output) noexcept
        {
            if (index >= output.m_Count || output.m_Images[index].StorageBytes()) return {ImportError::InvalidState, ImportObject::Image, index};
            if (output.m_StorageBytes > limit) return {ImportError::Capacity, ImportObject::Image, index};
            options.maxStorageBytes = limit - output.m_StorageBytes;
            const auto result = output.m_Images[index].Decode(input, options);
            if (result) output.m_StorageBytes += output.m_Images[index].StorageBytes();
            return result;
        }
    };

    namespace
    {
        using Text = ArrayView<const char>;
        using Error = ImportError;
        using Operation = ImportLoadOperation;
        constexpr uint32_t invalid = InvalidSceneIndex;
        bool Equal(Text text, const char* literal) noexcept
        { const size_t count = strlen(literal); return text.count == count && (!count || memcmp(text.data, literal, count) == 0); }

        struct ImageFileQuery
        {
            ImportFileSource files;
            size_t pathLimit = SIZE_MAX;
            ImportCancellation cancellation;
            ImportResult failure;
            static bool Exists(void* context, Text path) noexcept
            {
                auto& self = *static_cast<ImageFileQuery*>(context);
                if (!self.failure) return false;
                if (path.count > self.pathLimit) { self.failure = {Error::Capacity, ImportObject::Image}; return false; }
                if (self.cancellation.IsRequested()) { self.failure = {Error::Canceled}; return false; }
                bool present = false;
                const auto result = self.files.exists(self.files.context, path, present);
                if (!result) { self.failure = result; self.failure.object = ImportObject::Image; }
                return bool(result) && present;
            }
        };

        struct LoadingBudget
        {
            size_t current = 0, peak = 0, limit = SIZE_MAX;
            ImportResult Acquire(size_t bytes) noexcept
            {
                if (current > limit || bytes > limit - current) return {Error::Capacity};
                current += bytes; if (peak < current) peak = current; return {};
            }
        };
        struct ModelSlots
        {
            ImportModel* models = nullptr;
            ImportModelAvailability* availability = nullptr;
            uint32_t count = 0;
            size_t bytes = 0;
            LoadingBudget& budget;
            explicit ModelSlots(LoadingBudget& owner) noexcept : budget(owner) {}
            ~ModelSlots() noexcept
            {
                for (uint32_t i = 0; i < count; ++i) models[i].~ImportModel();
                free(models); budget.current -= bytes;
            }
            ImportResult Prepare(size_t requested) noexcept
            {
                static_assert(alignof(ImportModelAvailability) == 1);
                constexpr size_t stride = sizeof(ImportModel) + sizeof(ImportModelAvailability);
                if (requested >= invalid || requested > size_t(PTRDIFF_MAX) / stride) return {Error::Capacity};
                const size_t required = requested * stride;
                const auto result = budget.Acquire(required);
                if (!result) return result;
                bytes = required;
                if (!requested) return {};
                models = static_cast<ImportModel*>(ImportAllocate(required));
                if (!models) return {Error::OutOfMemory};
                availability = reinterpret_cast<ImportModelAvailability*>(reinterpret_cast<uint8_t*>(models) + requested * sizeof(ImportModel));
                count = uint32_t(requested);
                for (uint32_t i = 0; i < count; ++i)
                {
                    new (models + i) ImportModel{};
                    new (availability + i) ImportModelAvailability{ImportModelAvailability::Pending};
                }
                return {};
            }
        };
        struct ResolvedPath
        {
            char* data = nullptr;
            size_t count = 0, bytes = 0;
            LoadingBudget& budget;
            explicit ResolvedPath(LoadingBudget& owner) noexcept : budget(owner) {}
            ~ResolvedPath() noexcept { free(data); budget.current -= bytes; }
            ImportResult Prepare(Text file, Text reference, size_t pathLimit) noexcept
            {
                auto result = MeasureImportPath(file, reference, count);
                if (!result) return result;
                if (count > pathLimit || count >= size_t(PTRDIFF_MAX)) return {Error::Capacity};
                result = budget.Acquire(count + 1);
                if (!result) return result;
                bytes = count + 1;
                data = static_cast<char*>(ImportAllocate(bytes));
                if (!data) return {Error::OutOfMemory};
                return ResolveImportPath(file, reference, {data, bytes}, count);
            }
            Text View() const noexcept { return {data, count}; }
        };

        struct Loader
        {
            ImportFileSource files;
            const ImportSceneLoadOptions& options;
            ImportLoadCallbacks callbacks;
            ImportLoadProgress progress;
            LoadingBudget budget;
            char operationPath[512]{};
            size_t operationPathLength = 0;

            Loader(ImportFileSource source, const ImportSceneLoadOptions& limits, ImportLoadCallbacks feedback) noexcept
                : files(source), options(limits), callbacks(feedback), budget{0, 0, limits.maxLoadingBytes} {}
            void Report() noexcept
            {
                if (callbacks.report) callbacks.report(callbacks.context, progress, {operationPath, operationPathLength});
            }
            ImportResult Terminal(ImportResult result) noexcept
            {
                progress.result = result;
                progress.state = result.error == Error::Canceled ? ImportLoadState::Canceled : ImportLoadState::Failed;
                Report(); return result;
            }
            template<class Work> ImportResult Perform(Operation operation, Text path, Work work) noexcept
            {
                progress.operation = operation; progress.result = {};
                operationPathLength = path.count < sizeof(operationPath) ? path.count : sizeof(operationPath) - 1;
                progress.pathTruncated = operationPathLength != path.count;
                if (operationPathLength) memcpy(operationPath, path.data, operationPathLength);
                operationPath[operationPathLength] = '\0';
                if (progress.importStepsTotal == UINT64_MAX) return {Error::Overflow};
                ++progress.importStepsTotal; Report();
                if (callbacks.cancellation.IsRequested()) return {Error::Canceled};
                const auto result = work();
                ++progress.importStepsCompleted; progress.result = result; Report();
                return callbacks.cancellation.IsRequested() ? ImportResult{Error::Canceled} : result;
            }
            ImportResult Read(Text path, ImportFileData& output) noexcept
            {
                if (path.count > options.maxPathBytes) return {Error::Capacity};
                ImportFileData candidate;
                auto result = files.read(files.context, path, options.maxFileBytes, candidate, callbacks.cancellation);
                if (!result) return result;
                const size_t bytes = candidate.Bytes().count;
                if (bytes > options.maxFileBytes) return {Error::Capacity};
                if (progress.filesRead == UINT64_MAX || bytes > UINT64_MAX - progress.fileBytesRead) return {Error::Overflow};
                ++progress.filesRead; progress.fileBytesRead += bytes;
                output = static_cast<ImportFileData&&>(candidate); return {};
            }
            ImportResult Model(Text path, ImportModel& output, ImportRuntimeLightIds* runtimeLights = nullptr) noexcept
            {
                ImportFileData input;
                auto result = Perform(Operation::ReadModel, path, [&]() noexcept { return Read(path, input); });
                if (!result) return result;
                ImportDocument document;
                result = Perform(Operation::ParseModel, path, [&]() noexcept
                {
                    // an empty file is malformed content, not a bad caller span.
                    if (!input.Bytes().count) return ImportResult{Error::InvalidData};
                    return document.Parse(input.Bytes());
                });
                if (!result) return result;
                input.Reset();
                for (size_t index = 0; index < document.BufferCount(); ++index)
                {
                    ImportBufferInfo info;
                    result = document.BufferInfo(index, info);
                    if (!result) return result;
                    if (info.resident) continue;
                    ResolvedPath resolved(budget);
                    result = resolved.Prepare(path, info.uri, options.maxPathBytes);
                    if (!result) return result;
                    result = Perform(Operation::ReadBuffer, resolved.View(), [&]() noexcept
                    {
                        ImportFileData buffer;
                        auto read = Read(resolved.View(), buffer);
                        if (!read) { read.object = ImportObject::Buffer; read.index = index; return read; }
                        if (callbacks.cancellation.IsRequested()) return ImportResult{Error::Canceled};
                        return document.SupplyBuffer(index, buffer.Bytes());
                    });
                    if (!result) return result;
                }
                return Perform(Operation::ConvertModel, path, [&]() noexcept
                {
                    ImportSceneOptions conversion;
                    auto name = ReadImportPathFilename(path, conversion.modelName);
                    if (!name) return name;
                    conversion.generation = options.generation; conversion.modelPath = path;
                    conversion.maxScratchBytes = options.maxConversionScratchBytes;
                    conversion.maxGeometryBytes = options.maxGeometryBytes;
                    conversion.maxImageBytes = options.maxEncodedImageBytes;
                    if (runtimeLights) conversion.runtimeLights = options.runtimeLights;
                    ImageFileQuery query{files, options.maxPathBytes, callbacks.cancellation, {}};
                    conversion.fileExists = ImageFileQuery::Exists; conversion.fileContext = &query;
                    const auto converted = ConvertImportScene(document, conversion, output.scene, output.geometry, &output.textures, runtimeLights);
                    return query.failure ? converted : query.failure;
                });
            }
            bool Unavailable(ImportResult result) const noexcept
            {
                if (progress.operation != Operation::ReadModel && progress.operation != Operation::ParseModel && progress.operation != Operation::ReadBuffer)
                    return false;
                switch (result.error)
                {
                case Error::FileUnavailable: case Error::Io:
                case Error::InvalidJson: case Error::InvalidContainer: case Error::UnsupportedVersion:
                case Error::UnsupportedExtension: case Error::InvalidData: case Error::InvalidUri:
                case Error::InvalidIndex: case Error::InvalidAccessor: case Error::InvalidRange:
                case Error::InvalidSparse: case Error::BufferUnavailable: case Error::NonFiniteValue:
                case Error::UnsupportedData: case Error::InvalidHierarchy: case Error::Cycle: return true;
                default: return false;
                }
            }
            ImportResult Build(Text path, ImportModel& output, ImportCompositionStats& composition, ImportRuntimeLightIds& runtimeLights) noexcept
            {
                Text extension;
                auto result = ReadImportPathExtension(path, extension);
                if (!result) return result;
                if (Equal(extension, ".gltf") || Equal(extension, ".glb"))
                {
                    if (!options.maxModels) return {Error::Capacity};
                    progress.objectsTotal = 1; progress.modelIndex = 0;
                    result = Model(path, output, &runtimeLights);
                    ++progress.objectsCompleted; Report();
                    return result;
                }
                ImportSceneDescription description;
                ImportFileData json;
                result = Perform(Operation::ReadDescription, path, [&]() noexcept { return Read(path, json); });
                if (!result) return result;
                result = Perform(Operation::ParseDescription, path, [&]() noexcept { return description.Parse(json.Bytes(), path, options.description); });
                if (!result) return result;
                json.Reset();
                if (description.ModelCount() > options.maxModels || description.ModelCount() >= invalid) return {Error::Capacity};
                progress.objectsTotal = uint32_t(description.ModelCount());
                ModelSlots slots(budget);
                result = slots.Prepare(description.ModelCount());
                if (!result) return result;
                for (uint32_t m = 0; m < slots.count; ++m)
                {
                    progress.modelIndex = m;
                    result = Model(description.ModelPath(m), slots.models[m]);
                    ++progress.objectsCompleted;
                    if (result) slots.availability[m] = ImportModelAvailability::Available;
                    else if (Unavailable(result))
                    {
                        slots.availability[m] = ImportModelAvailability::Unavailable;
                        ++progress.objectsUnavailable;
                        if (callbacks.modelUnavailable)
                            callbacks.modelUnavailable(callbacks.context, m, description.ModelPath(m), progress.operation, result);
                    }
                    else { Report(); return result; }
                    Report();
                }
                progress.modelIndex = invalid;
                return Perform(Operation::ComposeScene, path, [&]() noexcept
                {
                    ImportCompositionOptions compose;
                    compose.generation = options.generation; compose.maxScratchBytes = options.maxConversionScratchBytes;
                    compose.maxGeometryBytes = options.maxGeometryBytes; compose.maxImageBytes = options.maxEncodedImageBytes;
                    compose.modelAvailability = {slots.availability, slots.count};
                    compose.runtimeLights = options.runtimeLights;
                    return ComposeImportScene(description, {slots.models, slots.count}, compose,
                        output.scene, output.geometry, output.textures, &composition, &runtimeLights);
                });
            }
            ImportResult Decode(ImportModel& model, ImportLoadedScene& output) noexcept
            {
                progress.modelIndex = invalid;
                auto result = Perform(Operation::PrepareImages, {}, [&]() noexcept
                {
                    if (model.textures.TextureCount() != model.scene.View().textures.count || model.textures.TextureCount() >= invalid)
                        return ImportResult{Error::InvalidState, ImportObject::Texture};
                    progress.texturesTotal = uint32_t(model.textures.TextureCount());
                    return ImportSceneLoadAccess::PrepareImages(progress.texturesTotal, options.maxDecodedBytes, output.images);
                });
                if (!result) return result;
                for (uint32_t t = 0; t < progress.texturesTotal; ++t)
                {
                    progress.textureIndex = t;
                    const auto texture = model.textures.Texture(t);
                    auto image = texture.imageIndex < model.textures.ImageCount() ? model.textures.Image(texture.imageIndex) : ImportImageView{};
                    const auto path = texture.imageIndex < model.textures.ImageCount() ? image.path :
                        RendererSceneText(model.scene.View(), model.scene.View().textures.data[t].path);
                    result = Perform(Operation::DecodeImage, path, [&]() noexcept
                    {
                        if (texture.imageIndex >= model.textures.ImageCount()) return ImportResult{Error::UnsupportedData, ImportObject::Texture, t};
                        ImportFileData file;
                        if (!image.embedded)
                        {
                            auto read = Read(image.path, file);
                            if (!read) { read.object = ImportObject::Image; read.index = texture.imageIndex; return read; }
                            image.bytes = file.Bytes();
                        }
                        if (callbacks.cancellation.IsRequested()) return ImportResult{Error::Canceled};
                        ImportImageDecodeOptions decode;
                        decode.forceSRGB = texture.forceSRGB; decode.maxEncodedBytes = options.maxFileBytes;
                        decode.maxTemporaryPixelBytes = options.maxTemporaryPixelBytes;
                        auto decoded = ImportSceneLoadAccess::Decode(t, image, decode, options.maxDecodedBytes, output.images);
                        if (!decoded) return decoded;
                        const auto info = output.images.Images().data[t].Info();
                        const auto metadata = model.scene.SetTextureMetadata({options.generation, t}, info.alpha, info.originalBitsPerPixel);
                        if (!metadata.Succeeded()) return ImportResult{Error::InvalidState, ImportObject::Texture, t};
                        ++progress.texturesDecoded; return ImportResult{};
                    });
                    if (!result) return result;
                }
                progress.textureIndex = invalid;
                return {};
            }
            ImportResult Run(Text path, ImportLoadedScene& output) noexcept
            {
                progress.state = ImportLoadState::Running;
                Text filename;
                auto result = ReadImportPathFilename(path, filename);
                if (!result || !filename.count || !files.read || !files.exists || !options.generation)
                    return Terminal({Error::InvalidInput});
                if (path.count > options.maxPathBytes) return Terminal({Error::Capacity});
                result = ValidateImportRuntimeLights(options.runtimeLights);
                if (!result) return Terminal(result);
                ImportLoadedScene candidate;
                ImportModel model;
                result = Build(path, model, candidate.composition, candidate.runtimeLights);
                if (!result) return Terminal(result);
                result = Decode(model, candidate);
                if (!result) return Terminal(result);
                result = Perform(Operation::Finalize, path, [&]() noexcept
                {
                    model.textures.Reset();
                    candidate.scene = static_cast<RendererScene&&>(model.scene);
                    candidate.geometry = static_cast<ImportGeometry&&>(model.geometry);
                    return ImportResult{};
                });
                if (!result) return Terminal(result);
                candidate.peakLoadingBytes = budget.peak;
                progress.state = ImportLoadState::Ready; progress.result = {};
                candidate.progress = progress;
                output = static_cast<ImportLoadedScene&&>(candidate);
                Report(); return {};
            }
        };
    }

    ImportResult LoadImportScene(ImportFileSource files, ArrayView<const char> path,
        const ImportSceneLoadOptions& options, ImportLoadedScene& output, ImportLoadCallbacks callbacks) noexcept
    {
        Loader loader(files, options, callbacks);
        return loader.Run(path, output);
    }
}
