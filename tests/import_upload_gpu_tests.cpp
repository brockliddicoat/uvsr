#include "renderer_scene_descriptors_nvrhi.h"
#include "renderer_scene_resources_nvrhi.h"
#include "renderer_scene_gpu_nvrhi.h"
#include "renderer_common_passes_nvrhi.h"
#include "renderer_import_load.h"
#include "renderer_scene_load_worker.h"
#include "../cmake/RequireNoCppExceptions.h"
#include "renderer_gpu_fixture.h"
#include "renderer_upload_layout.h"
#include <stb_image_write.h>
#include <directx/d3d12.h>
#include <wrl/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void FailNextNvrhiRtvAllocation(nvrhi::IDevice* device);

namespace
{
    using namespace uvsr;
    using Microsoft::WRL::ComPtr;
    size_t referenceImages = 0, literalSubresources = 0, referenceSubresources = 0, rejected = 0;
    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "import upload GPU check failed: %s\n", reason); exit(1);
    }
    void Good(RendererUploadResult result, const char* reason)
    {
        if (result) return;
        fprintf(stderr, "import upload GPU check failed: %s, error %u, index %u\n", reason, unsigned(result.error), result.index); exit(1);
    }
    void Imported(ImportResult result)
    {
        if (result) return;
        fprintf(stderr, "import upload fixture: %s, object %u, index %zu\n", ImportErrorText(result.error), unsigned(result.object), result.index); exit(1);
    }
    uint32_t Axis(uint32_t axis, uint32_t mip) { const auto value = axis >> mip; return value ? value : 1; }
    struct FailedHealth
    {
        RendererUploadHealth real;
        uint32_t ordinal = 0, calls = 0;
        static bool Check(void* context) noexcept
        {
            auto& self = *static_cast<FailedHealth*>(context);
            return ++self.calls != self.ordinal && self.real.check(self.real.context);
        }
        RendererUploadHealth View() noexcept { return {Check, this}; }
    };
    struct Encoded
    {
        uint8_t data[65536]{};
        size_t size = 0;
        void Put(size_t at, uint32_t value) { Require(at + 4 <= sizeof(data), "fixture header range"); memcpy(data + at, &value, 4); }
        static void Write(void* context, void* data, int count)
        {
            auto& self = *static_cast<Encoded*>(context);
            Require(count > 0 && size_t(count) <= sizeof(self.data) - self.size, "fixture encoded capacity");
            memcpy(self.data + self.size, data, size_t(count)); self.size += size_t(count);
        }
        void Png(uint32_t width, uint32_t height, uint32_t channels, bool hdr = false)
        {
            size = 0;
            uint8_t pixels[1024]{};
            float floats[1024]{};
            Require(width * height * channels <= sizeof(pixels), "fixture pixel capacity");
            for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) for (uint32_t c = 0; c < channels; ++c)
            {
                const auto at = (y * width + x) * channels + c;
                pixels[at] = uint8_t(x * 29 + y * 73 + c * 47 + ((x + y) % 2) * 97);
                floats[at] = float(pixels[at]) * 0.0625f;
            }
            Require(hdr ? stbi_write_hdr_to_func(Write, this, int(width), int(height), int(channels), floats) != 0
                : stbi_write_png_to_func(Write, this, int(width), int(height), int(channels), pixels, int(width * channels)) != 0,
                "encoded pixel fixture");
        }
        void Dds(uint32_t code, uint32_t width, uint32_t height, uint32_t mips, uint32_t dimension = 3,
            uint32_t arrays = 1, uint32_t depth = 1, bool cube = false)
        {
            size = sizeof(data); memset(data, 0, size);
            Put(0, 0x20534444); Put(4, 124); Put(8, dimension == 4 ? 0x801007 : 0x1007);
            Put(12, height); Put(16, width); Put(24, depth); Put(28, mips);
            Put(76, 32); Put(80, 4); Put(84, 0x30315844); Put(108, 0x1000);
            Put(128, code); Put(132, dimension); Put(136, cube ? 4 : 0); Put(140, arrays);
            for (size_t i = 148; i < size; ++i) data[i] = uint8_t(i * 31 + 7);
        }
        ImportImageView View(const char* path) const
        { return {{path, strlen(path)}, {}, {data, size}, true}; }
    };

    void TextureScene(RendererScene& scene, uint32_t count)
    {
        RendererSceneCounts counts; counts.nodes = 1; counts.textures = count;
        Require(scene.Prepare(counts).Succeeded(), "texture scene storage");
        uint8_t workspace[64]{};
        Require(scene.SealWorkspaceBytes() <= sizeof(workspace) && scene.Seal(0, {workspace, sizeof(workspace)}).Succeeded() &&
            scene.Publish(97).Succeeded(), "sealed texture scene");
    }
    void Submit(RendererSceneResourcesNvrhi& owner, size_t budget, RendererCommonPasses* passes, RendererUploadHealth health)
    {
        for (uint32_t step = 0; step < 4096; ++step)
        {
            const auto before = owner.Progress();
            if (before.phase == RendererUploadPhase::Submitted || before.phase == RendererUploadPhase::Complete) break;
            Good(owner.Step(budget, passes, health), "upload step");
            const auto after = owner.Progress();
            Require(after.submittedBytes >= before.submittedBytes && after.submittedBytes <= after.totalBytes,
                "monotonic submitted progress");
            Require(after.texturesSubmitted >= before.texturesSubmitted && after.texturesSubmitted <= owner.TextureCount(),
                "copied texture readiness counts only completely submitted textures");
        }
        const auto done = owner.Progress();
        Require((done.phase == RendererUploadPhase::Submitted || done.phase == RendererUploadPhase::Complete) &&
            !done.cpuBorrows && done.submittedBytes == done.totalBytes && done.texturesSubmitted == owner.TextureCount(),
            "bounded complete submission");
    }
    nvrhi::StagingTextureHandle CopyTexture(nvrhi::IDevice* device, nvrhi::ICommandList* commands, nvrhi::ITexture* texture)
    {
        const auto& desc = texture->getDesc();
        auto readback = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
        Require(bool(readback), "texture readback storage");
        const bool compressed = nvrhi::getFormatInfo(desc.format).blockSize > 1;
        ID3D12Device* nativeDevice = device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
        ID3D12GraphicsCommandList* nativeCommands = commands->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
        ID3D12Resource* source = texture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        ID3D12Resource* destination = readback->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        Require(!compressed || (nativeDevice && nativeCommands && source && destination), "compressed readback native interfaces");
        uint64_t base = 0;
        for (uint32_t slice = 0; slice < desc.arraySize; ++slice) for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
        {
            const auto part = nvrhi::TextureSlice().setArraySlice(slice).setMipLevel(mip);
            if (!compressed) { commands->copyTexture(readback, part, texture, part); continue; }
            // the pin supplies an unaligned explicit box for mips smaller than a
            // compressed block. use a whole-subresource copy in this fixture.
            // both resource owners stay live through the caller's waitForIdle.
            commands->setTextureState(texture, nvrhi::TextureSubresourceSet(mip, 1, slice, 1), nvrhi::ResourceStates::CopySource);
            commands->commitBarriers();
            const auto resource = source->GetDesc();
            const uint32_t subresource = slice * desc.mipLevels + mip;
            uint64_t bytes = 0;
            D3D12_TEXTURE_COPY_LOCATION target{};
            target.pResource = destination; target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            nativeDevice->GetCopyableFootprints(&resource, subresource, 1, 0, &target.PlacedFootprint, nullptr, nullptr, &bytes);
            Require(base <= destination->GetDesc().Width && bytes <= destination->GetDesc().Width - base, "compressed readback footprint range");
            target.PlacedFootprint.Offset = base;
            D3D12_TEXTURE_COPY_LOCATION input{};
            input.pResource = source; input.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; input.SubresourceIndex = subresource;
            nativeCommands->CopyTextureRegion(&target, 0, 0, 0, &input, nullptr);
            base = (base + bytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~(uint64_t(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) - 1);
        }
        return readback;
    }
    void CompareReadbacks(nvrhi::IDevice* device, nvrhi::IStagingTexture* candidate, RendererGpuReference& reference)
    {
        const auto& a = candidate->getDesc();
        const uint32_t fields[]{a.width, a.height, a.depth, a.arraySize, uint32_t(a.dimension), a.mipLevels, uint32_t(a.format)};
        reference.Match(fields, sizeof(fields));
        const auto& format = nvrhi::getFormatInfo(a.format);
        for (uint32_t slice = 0; slice < a.arraySize; ++slice) for (uint32_t mip = 0; mip < a.mipLevels; ++mip)
        {
            const size_t rowBytes = ((Axis(a.width, mip) - 1) / format.blockSize + 1) * format.bytesPerBlock;
            const size_t rows = (Axis(a.height, mip) - 1) / format.blockSize + 1;
            const auto part = nvrhi::TextureSlice().setArraySlice(slice).setMipLevel(mip);
            size_t candidatePitch = 0;
            const auto* actual = static_cast<const uint8_t*>(device->mapStagingTexture(candidate, part, nvrhi::CpuAccessMode::Read, &candidatePitch));
            Require(actual && candidatePitch >= rowBytes, "captured comparison mapping");
            const uint32_t key[]{slice, mip}; reference.Match(key, sizeof(key));
            uint8_t expected[4096];
            const size_t referenceBytes = rowBytes * rows * Axis(a.depth, mip);
            Require(referenceBytes <= sizeof(expected), "bounded captured texture bytes");
            reference.Read(expected, referenceBytes);
            bool same = true;
            for (uint32_t z = 0; z < Axis(a.depth, mip); ++z) for (size_t row = 0; row < rows; ++row)
                same &= memcmp(actual + (z * rows + row) * candidatePitch, expected + (z * rows + row) * rowBytes, rowBytes) == 0;
            device->unmapStagingTexture(candidate);
            if (!same) fprintf(stderr, "captured mip mismatch: format %u, %ux%u, slice %u, mip %u\n", unsigned(a.format), a.width, a.height, slice, mip);
            Require(same, "captured uploaded and generated mip bytes"); ++referenceSubresources;
        }
        ++referenceImages;
    }
    void CompareDecoded(nvrhi::IDevice* device, nvrhi::IStagingTexture* readback, const ImportDecodedImage& image)
    {
        const auto info = image.Info(); const auto bytes = image.Bytes(); const auto layouts = image.Subresources();
        const auto& format = nvrhi::getFormatInfo(RendererImportImageFormat(info.format));
        for (uint32_t slice = 0; slice < info.arraySize; ++slice) for (uint32_t mip = 0; mip < info.mipLevels; ++mip)
        {
            const auto& layout = layouts.data[size_t(slice) * info.mipLevels + mip];
            const size_t rows = (Axis(info.height, mip) - 1) / format.blockSize + 1;
            const auto part = nvrhi::TextureSlice().setArraySlice(slice).setMipLevel(mip);
            size_t pitch = 0;
            const auto* mapped = static_cast<const uint8_t*>(device->mapStagingTexture(readback, part, nvrhi::CpuAccessMode::Read, &pitch));
            Require(mapped && pitch >= layout.rowPitch, "authored texture mapping");
            bool same = true;
            for (uint32_t z = 0; z < Axis(info.depth, mip); ++z) for (size_t row = 0; row < rows; ++row)
                same &= memcmp(mapped + (z * rows + row) * pitch, bytes.data + layout.offset + z * layout.depthPitch + row * layout.rowPitch, layout.rowPitch) == 0;
            device->unmapStagingTexture(readback); Require(same, "authored subresource bytes"); ++literalSubresources;
        }
    }

    void ImageCase(nvrhi::IDevice* device, RendererCommonPasses& passes, RendererUploadHealth health,
        RendererGpuReference& reference, Encoded& encoded, const char* path, bool srgb, bool mips,
        bool capturedControl = true)
    {
        const uint32_t flags[]{uint32_t(srgb), uint32_t(mips), uint32_t(capturedControl)};
        reference.Match(flags, sizeof(flags)); reference.Match(path, strlen(path)); reference.Match(encoded.data, encoded.size);
        ImportDecodedImage image;
        Imported(image.Decode(encoded.View(path), {srgb}));
        ImportGeometry geometry;
        RendererScene scene; TextureScene(scene, 1);
        RendererSceneResourcesNvrhi owner(device);
        RendererUploadOptions options; options.generateMips = mips;
        Good(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health, options), "prepare texture upload");
        scene.Reset();
        Submit(owner, 19, &passes, health);
        auto commands = device->createCommandList(); Require(bool(commands), "texture comparison commands");
        commands->open();
        auto candidate = CopyTexture(device, commands, owner.Texture(0));
        commands->close();
        Require(device->executeCommandList(commands) != 0 && device->waitForIdle(), "texture readback completion");
        Good(owner.PollCompletion(health), "texture completion query");
        Require(owner.Progress().phase == RendererUploadPhase::Complete && owner.Progress().gpuComplete, "actual texture completion");
        CompareDecoded(device, candidate, image);
        image.Reset(); memset(encoded.data, 0xee, encoded.size);
        if (capturedControl) CompareReadbacks(device, candidate, reference);
        const auto submissions = owner.Progress().submissions;
        Good(owner.Step(1, &passes, health), "completed upload idempotence");
        Require(owner.Progress().submissions == submissions && !owner.Progress().cpuBorrows, "completion does not resubmit");
        owner.Cancel(); Require(owner.Progress().phase == RendererUploadPhase::Complete, "cancel preserves completed result");
        owner.Reset(); device->runGarbageCollection();
    }

    void GeometryFixture(RendererScene& scene, ImportGeometry& geometry)
    {
        struct Bytes { float positions[9]; uint16_t indices[3]; };
        const Bytes input{{0,0,0, 1.25f,-0.5f,0.25f, -0.25f,1.5f,0.75f}, {2,0,1}};
        static_assert(sizeof(Bytes) == 44);
        const char text[] = R"({"asset":{"version":"2.0"},"scene":0,"buffers":[{"uri":"fixture.bin","byteLength":44}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}]})";
        ImportDocument document; Imported(document.Parse({reinterpret_cast<const uint8_t*>(text), sizeof(text) - 1}));
        Imported(document.SupplyBuffer(0, {reinterpret_cast<const uint8_t*>(&input), sizeof(input)}));
        ImportSceneOptions options; options.generation = 98;
        Imported(ConvertImportScene(document, options, scene, geometry)); document.Reset();
    }
    void ReadBuffer(nvrhi::IDevice* device, nvrhi::IBuffer* buffer, const uint8_t* expected, size_t bytes)
    {
        nvrhi::BufferDesc desc; desc.byteSize = bytes; desc.cpuAccess = nvrhi::CpuAccessMode::Read;
        auto staging = device->createBuffer(desc); auto commands = device->createCommandList();
        Require(staging && commands, "buffer readback allocation");
        commands->open(); commands->copyBuffer(staging, 0, buffer, 0, bytes); commands->close();
        Require(device->executeCommandList(commands) != 0 && device->waitForIdle(), "buffer readback completion");
        const auto* actual = device->mapBuffer(staging, nvrhi::CpuAccessMode::Read);
        Require(actual && memcmp(actual, expected, bytes) == 0, "uploaded geometry bytes after owner input destruction");
        device->unmapBuffer(staging);
    }
    struct LoadWork
    {
        Encoded png;
        struct Geometry { float positions[9]{0,0,0, 1.25f,-0.5f,0.25f, -0.25f,1.5f,0.75f}; uint16_t indices[3]{2,0,1}; } geometry;
        ImportLoadedScene output;
        ImportResult result;
        static bool Equal(ArrayView<const char> path, const char* value) noexcept
        { const size_t count = strlen(value); return path.count == count && !memcmp(path.data, value, count); }
        static ImportResult Read(void* context, ArrayView<const char> path, size_t limit,
            ImportFileData& output, ImportCancellation cancellation) noexcept
        {
            if (cancellation.IsRequested()) return {ImportError::Canceled};
            auto& self = *static_cast<LoadWork*>(context);
            const char json[] = R"({"asset":{"version":"2.0"},"scene":0,"buffers":[{"uri":"fixture.bin","byteLength":44}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"images":[{"uri":"fixture.png"}],"textures":[{"source":0}],"materials":[{"alphaMode":"BLEND","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}]})";
            ArrayView<const uint8_t> bytes;
            if (Equal(path, "C:/load/fixture.gltf")) bytes = {reinterpret_cast<const uint8_t*>(json), sizeof(json) - 1};
            else if (Equal(path, "C:/load/fixture.bin")) bytes = {reinterpret_cast<const uint8_t*>(&self.geometry), sizeof(self.geometry)};
            else if (Equal(path, "C:/load/fixture.png")) bytes = {self.png.data, self.png.size};
            else return {ImportError::FileUnavailable};
            if (bytes.count > limit) return {ImportError::Capacity};
            ImportFileData candidate;
            const auto result = candidate.Allocate(bytes.count);
            if (!result) return result;
            memcpy(candidate.WritableBytes().data, bytes.data, bytes.count);
            output = static_cast<ImportFileData&&>(candidate); return {};
        }
        static ImportResult Exists(void*, ArrayView<const char> path, bool& present) noexcept
        { present = Equal(path, "C:/load/fixture.png"); return {}; }
        static bool Canceled(void* context) noexcept
        { return static_cast<RendererSceneLoadCancellation*>(context)->IsRequested(); }
        static bool Work(void* context, const RendererSceneLoadCancellation& cancellation)
        {
            auto& self = *static_cast<LoadWork*>(context);
            ImportSceneLoadOptions options; options.generation = 109;
            const char path[] = "C:/load/fixture.gltf";
            self.result = LoadImportScene({Read, Exists, &self}, {path, sizeof(path) - 1}, options, self.output,
                {{Canceled, const_cast<RendererSceneLoadCancellation*>(&cancellation)}, nullptr, nullptr});
            return bool(self.result);
        }
    };
    ImportLoadedScene LoadOnWorker()
    {
        LoadWork context; context.png.Png(7, 5, 4);
        RendererSceneLoadWorker worker;
        Require(worker.Start(LoadWork::Work, &context) && worker.Join(), "complete loader worker join before GPU handoff");
        Imported(context.result);
        Require(context.output.progress.state == ImportLoadState::Ready && context.output.geometry.BufferCount() == 1 &&
            context.output.images.Images().count == 1, "joined complete CPU owner");
        memset(context.png.data, 0xc7, context.png.size); memset(&context.geometry, 0xa3, sizeof(context.geometry));
        return static_cast<ImportLoadedScene&&>(context.output);
    }
    void LoadedBindings(nvrhi::IDevice* device, ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue,
        RendererCommonPasses& passes, RendererUploadHealth health)
    {
        auto loaded = LoadOnWorker();
        const auto view = loaded.scene.View();
        const auto source = loaded.geometry.Buffer(0);
        const auto indexBytes = source.indices.count, vertexBytes = source.vertices.count;
        uint8_t indices[256]{}, vertices[1024]{};
        Require(indexBytes <= sizeof(indices) && vertexBytes <= sizeof(vertices), "binding fixture geometry bounds");
        memcpy(indices, source.indices.data, indexBytes); memcpy(vertices, source.vertices.data, vertexBytes);
        RendererSceneResourcesNvrhi resources(device);
        RendererSceneGpuTablesNvrhi tables(device);
        RendererUploadOptions options; options.rayTracing = true;
        Good(resources.Prepare(view, loaded.geometry, loaded.images.Images(), nullptr, health, options), "binding resource preparation");
        nvrhi::BindlessLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::All; layoutDesc.maxCapacity = 3;
        layoutDesc.registerSpaces = {nvrhi::BindingLayoutItem::RawBuffer_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2)};
        auto layout = device->createBindlessLayout(layoutDesc);
        Require(bool(layout), "binding descriptor layout");
        uvsr::RendererSceneDescriptorsNvrhi descriptors(device, layout);
        Require(descriptors.IsValid() && tables.Prepare(view, resources, &descriptors).error == RendererSceneError::InvalidState &&
            !tables.Generation(), "unsubmitted resources cannot enter render tables");
        Submit(resources, 13, &passes, health);
        Require(device->waitForIdle(), "binding source upload completion");
        loaded.geometry.Reset(); loaded.images.Reset();
        Require(resources.BufferCount() == 1 && resources.TextureCount() == 1 && resources.Buffer(0).indexOwner == 0,
            "resource identity survives upload payload destruction");
        auto stale = view; ++stale.generation;
        Require(tables.Prepare(stale, resources, &descriptors).error == RendererSceneError::Generation, "binding generation mismatch");
        auto malformed = view; malformed.bufferGroups.count = 0;
        Require(tables.Prepare(malformed, resources, &descriptors).error == RendererSceneError::Reference, "binding buffer count mismatch");
        malformed = view; malformed.textures.count = 0;
        Require(tables.Prepare(malformed, resources, &descriptors).error == RendererSceneError::Reference, "binding texture count mismatch");
        auto group = view.bufferGroups.data[0]; ++group.vertexBytes;
        malformed = view; malformed.bufferGroups = {&group, 1};
        Require(tables.Prepare(malformed, resources, &descriptors).error == RendererSceneError::Reference && !descriptors.GetLiveCount(),
            "binding byte extent mismatch leaves no slots");
        uint32_t failures = 0;
        for (uint32_t ordinal = 1; ordinal <= 7; ++ordinal)
        {
            SetRendererSceneGpuAllocationFailure(ordinal);
            const auto result = tables.Prepare(view, resources, &descriptors);
            SetRendererSceneGpuAllocationFailure(0);
            if (result.Succeeded()) { tables.Reset(); break; }
            Require(result.error == RendererSceneError::Allocation && !tables.Generation() && !descriptors.GetLiveCount(),
                "binding allocation failure is atomic");
            Require(tables.Prepare(view, resources, &descriptors).Succeeded() && descriptors.GetLiveCount() == 1,
                "binding allocation retry owns one texture slot");
            tables.Reset(); Require(!descriptors.GetLiveCount(), "binding retry releases its slot"); ++failures;
        }
        Require(failures == 6 && tables.Prepare(view, resources, &descriptors).Succeeded() && descriptors.GetLiveCount() == 1,
            "all binding allocation failures recovered, raw slots remain lazy");
        nvrhi::IBuffer* boundIndices = nullptr; nvrhi::IBuffer* boundVertices = nullptr;
        nvrhi::ITexture* boundTexture = nullptr;
        Require(tables.GetBuffers(0, boundIndices, boundVertices) && boundIndices == resources.Buffer(0).indices &&
            boundVertices == resources.Buffer(0).vertices && tables.GetTexture(0, boundTexture) && boundTexture == resources.Texture(0) &&
            !tables.GetBuffers(1, boundIndices, boundVertices) && !tables.GetTexture(1, boundTexture) &&
            tables.Prepare(view, resources, &descriptors).error == RendererSceneError::InvalidState,
            "draw consumers share exact uploaded resources and reject live replacement");
        nvrhi::BufferDesc sentinelDesc; sentinelDesc.byteSize = 16; sentinelDesc.canHaveRawViews = true;
        sentinelDesc.initialState = nvrhi::ResourceStates::ShaderResource; sentinelDesc.keepInitialState = true;
        auto sentinel = device->createBuffer(sentinelDesc);
        Require(bool(sentinel), "unrelated descriptor resource");
        const int32_t occupied = descriptors.CreateDescriptor(nvrhi::BindingSetItem::RawBuffer_SRV(0, sentinel));
        Require(occupied >= 0 && tables.PrepareRayGeometry(view).error == RendererSceneError::Capacity &&
            descriptors.GetLiveCount() == 2 && !tables.GeometryBuffer(), "one missing raw slot rejects without mutation");
        Require(descriptors.ReleaseDescriptor(occupied), "unrelated descriptor release");
        auto mesh = view.meshes.data[0]; ++mesh.geometries.count;
        malformed = view; malformed.meshes = {&mesh, 1};
        Require(tables.PrepareRayGeometry(malformed).error == RendererSceneError::Range && descriptors.GetLiveCount() == 1 &&
            !tables.GeometryBuffer(), "bad geometry rolls back newly owned raw slots");
        SetRendererSceneGpuAllocationFailure(1);
        Require(tables.PrepareRayGeometry(view).error == RendererSceneError::Allocation && descriptors.GetLiveCount() == 1,
            "ray scratch allocation failure preserves texture slots");
        SetRendererSceneGpuAllocationFailure(0);
        Require(tables.PrepareRayGeometry(view).Succeeded() && tables.PrepareRayGeometry(view).Succeeded() &&
            descriptors.GetLiveCount() == 3, "exact descriptor capacity and ray retry");
        int32_t textureSlot = -1, indexSlot = -1, vertexSlot = -1;
        for (uint32_t slot = 0; slot < 3; ++slot)
        {
            const auto descriptor = descriptors.GetDescriptor(int32_t(slot));
            if (descriptor.type == nvrhi::ResourceType::Texture_SRV && descriptor.resourceHandle == boundTexture) textureSlot = int32_t(slot);
            if (descriptor.type == nvrhi::ResourceType::RawBuffer_SRV && descriptor.resourceHandle == boundIndices) indexSlot = int32_t(slot);
            if (descriptor.type == nvrhi::ResourceType::RawBuffer_SRV && descriptor.resourceHandle == boundVertices) vertexSlot = int32_t(slot);
        }
        Require(textureSlot >= 0 && indexSlot >= 0 && vertexSlot >= 0, "descriptor kinds and resources agree");
        const auto selectionId = view.materials.data[0].selectionId;
        const size_t materialAt = indexBytes + vertexBytes;
        const size_t geometryAt = materialAt + sizeof(RendererMaterialTableEntry);
        const size_t instanceAt = geometryAt + sizeof(GeometryData);
        nvrhi::BufferDesc readbackDesc; readbackDesc.byteSize = instanceAt + sizeof(InstanceData);
        readbackDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        auto readback = device->createBuffer(readbackDesc); auto commands = device->createCommandList();
        auto completion = device->createEventQuery();
        Require(readback && commands && completion, "bound table readback and completion storage");
        ComPtr<ID3D12Fence> hold;
        Require(SUCCEEDED(nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&hold))) &&
            SUCCEEDED(nativeQueue->Wait(hold.Get(), 1)), "hold bound table GPU completion");
        commands->open(); tables.BeginRecording();
        bool valid = tables.RecordMaterials(commands, view).Succeeded() && tables.RecordInstances(commands, view).Succeeded() &&
            tables.RecordGeometry(commands);
        if (valid)
        {
            commands->copyBuffer(readback, 0, boundIndices, 0, indexBytes);
            commands->copyBuffer(readback, indexBytes, boundVertices, 0, vertexBytes);
            commands->copyBuffer(readback, materialAt, tables.MaterialBuffer(), 0, sizeof(RendererMaterialTableEntry));
            commands->copyBuffer(readback, geometryAt, tables.GeometryBuffer(), 0, sizeof(GeometryData));
            commands->copyBuffer(readback, instanceAt, tables.InstanceBuffer(), 0, sizeof(InstanceData));
        }
        commands->close(); valid &= device->executeCommandList(commands) != 0;
        device->setEventQuery(completion, nvrhi::CommandQueue::Graphics); tables.CommitRecording();
        resources.Reset(); loaded.scene.Reset(); commands = nullptr; device->runGarbageCollection();
        valid &= !device->pollEventQuery(completion) && descriptors.GetLiveCount() == 3 &&
            tables.GetBuffers(0, boundIndices, boundVertices) && tables.GetTexture(0, boundTexture);
        const bool released = SUCCEEDED(hold->Signal(1));
        Require(released && valid && device->waitForIdle() && device->pollEventQuery(completion),
            "table handles and owned slots survive actual blocked GPU completion");
        const auto* bytes = static_cast<const uint8_t*>(device->mapBuffer(readback, nvrhi::CpuAccessMode::Read));
        Require(bytes != nullptr, "bound table readback map");
        RendererMaterialTableEntry materialData{}; GeometryData geometryData{}; InstanceData instanceData{};
        memcpy(&materialData, bytes + materialAt, sizeof(materialData)); memcpy(&geometryData, bytes + geometryAt, sizeof(geometryData));
        memcpy(&instanceData, bytes + instanceAt, sizeof(instanceData));
        valid = !memcmp(bytes, indices, indexBytes) && !memcmp(bytes + indexBytes, vertices, vertexBytes);
        device->unmapBuffer(readback);
        Require(valid && materialData.material.baseOrDiffuseTextureIndex == textureSlot && materialData.material.roughness == 1 &&
            uint32_t(materialData.material.materialID) == selectionId && materialData.material.domain == MaterialDomain_AlphaBlended &&
            geometryData.indexBufferIndex == indexSlot && geometryData.vertexBufferIndex == vertexSlot && geometryData.materialIndex == 0 &&
            instanceData.firstGeometryIndex == 0 && instanceData.numGeometries == 1 && instanceData.transform.values[0] == 1 &&
            !memcmp(&instanceData.transform, &instanceData.prevTransform, sizeof(instanceData.transform)),
            "joined import reaches exact geometry, material, descriptor and instance GPU data");
        tables.Reset(); tables.Reset();
        Require(!descriptors.GetLiveCount() && !tables.GetBuffers(0, boundIndices, boundVertices) && !tables.GetTexture(0, boundTexture),
            "physical retirement releases slots exactly once and invalidates table views");

        loaded = LoadOnWorker();
        Good(resources.Prepare(loaded.scene.View(), loaded.geometry, loaded.images.Images(), nullptr, health), "texture exhaustion resource preparation");
        Submit(resources, 4096, &passes, health);
        layoutDesc.maxCapacity = 1;
        auto oneLayout = device->createBindlessLayout(layoutDesc);
        Require(bool(oneLayout), "single slot layout");
        uvsr::RendererSceneDescriptorsNvrhi oneSlot(device, oneLayout);
        const int32_t taken = oneSlot.CreateDescriptor(nvrhi::BindingSetItem::RawBuffer_SRV(0, sentinel));
        Require(taken >= 0 && tables.Prepare(loaded.scene.View(), resources, &oneSlot).error == RendererSceneError::Capacity &&
            !tables.Generation() && oneSlot.GetLiveCount() == 1, "texture exhaustion preserves unrelated descriptor");
        Require(oneSlot.ReleaseDescriptor(taken), "single slot descriptor release");
        Require(tables.Prepare(loaded.scene.View(), resources, &oneSlot).Succeeded() && oneSlot.GetLiveCount() == 1,
            "texture exhaustion retry uses exact capacity");
        Require(device->waitForIdle(), "texture retry retirement"); tables.Reset();
        Require(!oneSlot.GetLiveCount() && health.check(health.context), "all binding failures recover without backend errors");
        printf("imported GPU bindings: %u allocation failures, exact capacities, rollback, joined bytes and held GPU retirement passed\n", failures);
    }
    void LoadedHandoff(nvrhi::IDevice* device, ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue,
        RendererCommonPasses& passes, RendererUploadHealth health)
    {
        auto loaded = LoadOnWorker();
        const auto source = loaded.geometry.Buffer(0);
        const auto pixelBytes = loaded.images.Images().data[0].Bytes();
        uint8_t indices[256]{}, vertices[1024]{}, pixels[140]{};
        Require(source.indices.count <= sizeof(indices) && source.vertices.count <= sizeof(vertices) && pixelBytes.count == sizeof(pixels),
            "joined payload fixture bounds");
        memcpy(indices, source.indices.data, source.indices.count); memcpy(vertices, source.vertices.data, source.vertices.count);
        memcpy(pixels, pixelBytes.data, sizeof(pixels));
        RendererSceneResourcesNvrhi owner(device);
        Good(owner.Prepare(loaded.scene.View(), loaded.geometry, loaded.images.Images(), nullptr, health), "joined owner GPU preparation");
        Require(owner.Generation() == loaded.scene.View().generation && owner.Progress().cpuBorrows, "GPU owner generation and CPU borrows");
        loaded.scene.Reset();
        nvrhi::BufferDesc indexDesc; indexDesc.byteSize = source.indices.count; indexDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        nvrhi::BufferDesc vertexDesc = indexDesc; vertexDesc.byteSize = source.vertices.count;
        auto indexReadback = device->createBuffer(indexDesc), vertexReadback = device->createBuffer(vertexDesc);
        auto textureReadback = device->createStagingTexture(owner.Texture(0)->getDesc(), nvrhi::CpuAccessMode::Read);
        auto commands = device->createCommandList();
        Require(indexReadback && vertexReadback && textureReadback && commands, "joined payload readback storage");
        ComPtr<ID3D12Fence> hold;
        Require(SUCCEEDED(nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&hold))) &&
            SUCCEEDED(nativeQueue->Wait(hold.Get(), 1)), "hold joined payload GPU completion");
        bool valid = true;
        for (uint32_t step = 0; step < 512 && owner.Progress().phase != RendererUploadPhase::Submitted && valid; ++step)
            valid &= bool(owner.Step(13, &passes, health));
        valid &= owner.Progress().phase == RendererUploadPhase::Submitted && !owner.Progress().cpuBorrows &&
            bool(owner.PollCompletion(health)) && !owner.Progress().gpuComplete;
        if (valid)
        {
            commands->open();
            commands->copyBuffer(indexReadback, 0, owner.Buffer(0).indices, 0, source.indices.count);
            commands->copyBuffer(vertexReadback, 0, owner.Buffer(0).vertices, 0, source.vertices.count);
            commands->copyTexture(textureReadback, nvrhi::TextureSlice(), owner.Texture(0), nvrhi::TextureSlice());
            commands->close(); valid &= device->executeCommandList(commands) != 0;
        }
        owner.Cancel(); owner.Reset(); loaded.geometry.Reset(); loaded.images.Reset(); commands = nullptr;
        device->runGarbageCollection();
        const bool released = SUCCEEDED(hold->Signal(1));
        Require(released && valid && device->waitForIdle(), "joined CPU and GPU owners can die before physical completion");
        const auto* indexData = device->mapBuffer(indexReadback, nvrhi::CpuAccessMode::Read);
        Require(indexData && !memcmp(indexData, indices, source.indices.count), "joined loader index readback"); device->unmapBuffer(indexReadback);
        const auto* vertexData = device->mapBuffer(vertexReadback, nvrhi::CpuAccessMode::Read);
        Require(vertexData && !memcmp(vertexData, vertices, source.vertices.count), "joined loader vertex readback"); device->unmapBuffer(vertexReadback);
        size_t pitch = 0;
        const auto* textureData = static_cast<const uint8_t*>(device->mapStagingTexture(textureReadback, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
        Require(textureData && pitch >= 28, "joined loader texture mapping");
        bool same = true;
        for (size_t row = 0; row < 5; ++row) same &= !memcmp(textureData + row * pitch, pixels + row * 28, 28);
        device->unmapStagingTexture(textureReadback); Require(same, "joined loader RGBA readback after encoded and decoded owners die");

        loaded = LoadOnWorker();
        for (const auto failure : {RendererUploadFailure::Allocation, RendererUploadFailure::Creation})
        {
            const auto* scene = loaded.scene.View().nodes.data;
            const auto* image = loaded.images.Images().data[0].Bytes().data;
            SetRendererUploadFailure(failure, 1);
            const auto result = owner.Prepare(loaded.scene.View(), loaded.geometry, loaded.images.Images(), nullptr, health);
            SetRendererUploadFailure(RendererUploadFailure::None, 0);
            Require(!result && owner.Progress().phase == RendererUploadPhase::Empty && !owner.Progress().cpuBorrows &&
                loaded.scene.View().nodes.data == scene && loaded.images.Images().data[0].Bytes().data == image,
                "failed GPU preparation preserves complete joined CPU candidate");
            Good(owner.Prepare(loaded.scene.View(), loaded.geometry, loaded.images.Images(), nullptr, health), "retry joined GPU preparation");
            owner.Cancel(); owner.Reset(); ++rejected;
        }
        for (bool fail : {false, true})
        {
            Good(owner.Prepare(loaded.scene.View(), loaded.geometry, loaded.images.Images(), nullptr, health), "joined upload failure preparation");
            Good(owner.Step(1, &passes, health), "joined upload partial buffer");
            Require(owner.Progress().cpuBorrows && owner.Progress().submissions == 1, "joined partial upload still borrows");
            if (fail)
            {
                SetRendererUploadFailure(RendererUploadFailure::Submission, 1);
                const auto result = owner.Step(1, &passes, health);
                SetRendererUploadFailure(RendererUploadFailure::None, 0);
                Require(result.error == RendererUploadError::Gpu && owner.Progress().phase == RendererUploadPhase::Failed,
                    "joined partial submission failure");
            }
            else owner.Cancel();
            Require(!owner.Progress().cpuBorrows, "joined cancellation/failure releases CPU borrows");
            owner.Reset(); ++rejected;
        }
        loaded = {};
        Require(device->waitForIdle() && health.check(health.context), "joined failed/canceled resources retire");
        device->runGarbageCollection();
        printf("complete loader GPU handoff: joined worker, encoded input destruction, blocked physical completion, byte readbacks and failed/canceled upload retries passed\n");
    }
    void CheckedUploadLayout()
    {
        uint64_t bytes = 17, capacity = 16, offset = 19, size = 23;
        Require(!uvsr::GetRendererUploadByteCount(SIZE_MAX, 4, bytes) && bytes == 17,
            "overflowing count was accepted or changed output");
        Require(!uvsr::GetRendererUploadByteCount(1, 0, bytes), "zero element stride was accepted");
        Require(uvsr::AppendRendererUploadRange(3, 12, capacity, offset, size) &&
            capacity == 64 && offset == 16 && size == 48, "attribute alignment changed");
        capacity = UINT64_MAX - 15;
        Require(!uvsr::AppendRendererUploadRange(1, 16, capacity, offset, size) &&
            capacity == UINT64_MAX - 15 && offset == 16 && size == 48, "range addition overflow");
        Require(!uvsr::AppendRendererUploadRange(SIZE_MAX, 1, capacity, offset, size), "alignment overflow");
        Require(uvsr::IsRendererUploadRangeValid(48, 4, 64, 16) &&
            uvsr::IsRendererUploadRangeValid(48, 48, 64, 16) &&
            !uvsr::IsRendererUploadRangeValid(48, 49, 64, 16) &&
            !uvsr::IsRendererUploadRangeValid(48, 0, 63, 16) &&
            !uvsr::IsRendererUploadRangeValid(48, 0, UINT64_MAX, UINT64_MAX - 16),
            "source progress or destination range check failed");
    }

    void GeometryCase(nvrhi::IDevice* device, RendererUploadHealth health)
    {
        RendererScene scene; ImportGeometry geometry; GeometryFixture(scene, geometry);
        Require(geometry.BufferCount() == 1, "static upload group count");
        const auto source = geometry.Buffer(0);
        uint8_t indices[256]{}, vertices[1024]{};
        Require(source.indices.count <= sizeof(indices) && source.vertices.count <= sizeof(vertices), "expected geometry capacity");
        memcpy(indices, source.indices.data, source.indices.count); memcpy(vertices, source.vertices.data, source.vertices.count);
        RendererSceneResourcesNvrhi owner(device);
        Good(owner.Prepare(scene.View(), geometry, {}, nullptr, health), "prepare static geometry");
        const size_t storage = owner.StorageBytes(); const auto buffers = owner.BufferBytes();
        Require(buffers == source.indices.count + source.vertices.count && !owner.TextureBytes(), "unique geometry byte accounting");
        const auto before = owner.Progress();
        Require(owner.Step(0, nullptr, health).error == RendererUploadError::Input && owner.Progress().phase == before.phase,
            "zero budget leaves preparation unchanged");
        Require(owner.Prepare(scene.View(), geometry, {}, nullptr, health).error == RendererUploadError::InvalidState,
            "live owner cannot be replaced through preparation");
        scene.Reset(); Submit(owner, 1, nullptr, health); geometry.Reset();
        ReadBuffer(device, owner.Buffer(0).indices, indices, source.indices.count);
        ReadBuffer(device, owner.Buffer(0).vertices, vertices, source.vertices.count);
        Good(owner.PollCompletion(health), "geometry completion");
        Require(owner.Progress().phase == RendererUploadPhase::Complete, "geometry actual completion");
        owner.Reset(); owner.Reset();
        Require(!owner.Generation() && !owner.Buffer(0).indices && !owner.BufferBytes() && !owner.Progress().cpuBorrows,
            "idempotent owner reset invalidates resource views");

        GeometryFixture(scene, geometry);
        for (uint32_t test = 0; test < 2; ++test)
        {
            RendererUploadOptions options;
            if (test == 0) options.maxStorageBytes = storage - 1; else options.maxBufferBytes = buffers - 1;
            Require(owner.Prepare(scene.View(), geometry, {}, nullptr, health, options).error == RendererUploadError::Capacity &&
                owner.Progress().phase == RendererUploadPhase::Empty, "planned geometry capacity exhaustion"); ++rejected;
        }
        RendererUploadOptions exact; exact.maxStorageBytes = storage; exact.maxBufferBytes = buffers;
        Good(owner.Prepare(scene.View(), geometry, {}, nullptr, health, exact), "exact planned geometry capacity"); owner.Reset();
        exact.rayTracing = true;
        const auto ray = owner.Prepare(scene.View(), geometry, {}, nullptr, health, exact);
        if (device->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct))
        {
            Good(ray, "ray geometry capability");
            Require(owner.Buffer(0).indices->getDesc().isAccelStructBuildInput && owner.Buffer(0).vertices->getDesc().isAccelStructBuildInput,
                "ray geometry buffers retain acceleration-structure input capability"); owner.Reset();
        }
        else Require(ray.error == RendererUploadError::Unsupported && owner.Progress().phase == RendererUploadPhase::Empty,
            "unsupported ray capability is explicit");
        const RendererUploadFailure failures[]{RendererUploadFailure::Allocation, RendererUploadFailure::Creation};
        for (auto failure : failures)
        {
            bool reachedEnd = false;
            for (uint32_t ordinal = 1; ordinal < 20; ++ordinal)
            {
                SetRendererUploadFailure(failure, ordinal);
                const auto result = owner.Prepare(scene.View(), geometry, {}, nullptr, health);
                SetRendererUploadFailure(RendererUploadFailure::None, 0);
                if (result) { owner.Reset(); reachedEnd = true; break; }
                Require(result.error == (failure == RendererUploadFailure::Allocation ? RendererUploadError::Allocation : RendererUploadError::Gpu) &&
                    owner.Progress().phase == RendererUploadPhase::Empty && !owner.Progress().cpuBorrows, "failed preparation releases candidate");
                Good(owner.Prepare(scene.View(), geometry, {}, nullptr, health), "retry failed resource preparation"); owner.Reset(); ++rejected;
            }
            Require(reachedEnd, "bounded preparation fault coverage");
        }
        for (uint32_t ordinal = 1; ordinal <= 2; ++ordinal)
        {
            Good(owner.Prepare(scene.View(), geometry, {}, nullptr, health), "submission failure preparation");
            if (ordinal == 2) Good(owner.Step(1, nullptr, health), "partial upload before failure");
            SetRendererUploadFailure(RendererUploadFailure::Submission, 1);
            Require(owner.Step(1, nullptr, health).error == RendererUploadError::Gpu, "submission failure propagates");
            SetRendererUploadFailure(RendererUploadFailure::None, 0);
            const auto failed = owner.Progress();
            Require(failed.phase == RendererUploadPhase::Failed && !failed.cpuBorrows && failed.submissions == ordinal - 1,
                "failed submission is terminal and releases borrows");
            Require(owner.Step(1, nullptr, health).error == RendererUploadError::Gpu && owner.Progress().submissions == failed.submissions,
                "failed upload never retries implicitly");
            Require(device->waitForIdle(), "partial failure retirement"); Good(owner.PollCompletion(health), "failure completion query");
            Require(owner.Progress().phase == RendererUploadPhase::Failed && owner.Progress().gpuComplete, "failure remains failed after completion");
            owner.Cancel(); Require(owner.Progress().phase == RendererUploadPhase::Failed, "cancel preserves failed submission result");
            owner.Reset(); ++rejected;
        }
        for (uint32_t ordinal = 1; ordinal <= 5; ++ordinal)
        {
            Good(owner.Prepare(scene.View(), geometry, {}, nullptr, health), "void operation failure preparation");
            FailedHealth failure{health, ordinal};
            Require(owner.Step(1, nullptr, failure.View()).error == RendererUploadError::Gpu &&
                owner.Progress().phase == RendererUploadPhase::Failed && !owner.Progress().cpuBorrows,
                "void-operation health failure is terminal");
            Require(owner.Progress().submissions == (ordinal == 5 ? 1u : 0u) &&
                owner.Progress().submittedBytes == (ordinal == 5 ? 1u : 0u), "post-submit failure preserves queued-byte accounting");
            Require(device->waitForIdle(), "void-operation failure retirement");
            Good(owner.PollCompletion(health), "void-operation failure completion"); owner.Reset(); ++rejected;
        }
        for (uint32_t mode = 0; mode < 3; ++mode)
        {
            Good(owner.Prepare(scene.View(), geometry, {}, nullptr, health), "poll failure preparation");
            if (mode == 1) Good(owner.Step(1, nullptr, health), "partial upload before poll failure");
            else if (mode == 2) Submit(owner, 17, nullptr, health);
            FailedHealth failure{health, mode == 2 ? 2u : 1u};
            Require(owner.PollCompletion(failure.View()).error == RendererUploadError::Gpu &&
                owner.Progress().phase == RendererUploadPhase::Failed && !owner.Progress().cpuBorrows,
                "pre/post-query health failure ends loading");
            owner.Cancel(); Require(owner.Progress().phase == RendererUploadPhase::Failed, "cancel cannot erase query failure");
            Require(owner.Step(1, nullptr, health).error == RendererUploadError::Gpu, "query failure prevents further upload");
            Require(device->waitForIdle(), "query failure retirement"); Good(owner.PollCompletion(health), "query retry only observes retirement");
            Require(owner.Progress().phase == RendererUploadPhase::Failed && owner.Progress().gpuComplete, "retired query failure remains terminal");
            owner.Reset(); ++rejected;
        }
        Good(owner.Prepare(scene.View(), geometry, {}, nullptr, health), "cancel preparation");
        owner.Cancel(); scene.Reset(); geometry.Reset();
        Require(owner.Progress().phase == RendererUploadPhase::Canceled && !owner.Progress().cpuBorrows && owner.Progress().gpuComplete &&
            owner.Step(1, nullptr, health).error == RendererUploadError::InvalidState, "cancel before recording ends all borrows");
    }

    void BlockedRetirement(nvrhi::IDevice* device, ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue,
        RendererCommonPasses& passes, RendererUploadHealth health)
    {
        Encoded encoded;
        uint32_t pixels[35]; for (auto& pixel : pixels) pixel = 0x61ef7f1f;
        Require(stbi_write_png_to_func(Encoded::Write, &encoded, 7, 5, 4, pixels, 28) != 0, "constant pending texture");
        ImportDecodedImage image; Imported(image.Decode(encoded.View("pending.png")));
        RendererScene scene; TextureScene(scene, 1); ImportGeometry geometry;
        RendererSceneResourcesNvrhi owner(device);
        Good(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health), "pending texture preparation");
        uint8_t expected[140]{}; Require(image.Bytes().count == sizeof(expected), "pending pixel count");
        memcpy(expected, image.Bytes().data, sizeof(expected));
        RendererScene meshScene; ImportGeometry meshGeometry; GeometryFixture(meshScene, meshGeometry);
        RendererSceneResourcesNvrhi mesh(device);
        Good(mesh.Prepare(meshScene.View(), meshGeometry, {}, nullptr, health), "pending mesh preparation");
        const uint8_t expectedByte = meshGeometry.Buffer(0).indices.data[0];
        nvrhi::BufferDesc bufferDesc; bufferDesc.byteSize = 1; bufferDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        auto bufferReadback = device->createBuffer(bufferDesc); Require(bool(bufferReadback), "pending partial buffer readback");
        auto commands = device->createCommandList(); Require(bool(commands), "pending copy commands");
        ComPtr<ID3D12Fence> hold;
        Require(SUCCEEDED(nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&hold))) &&
            SUCCEEDED(nativeQueue->Wait(hold.Get(), 1)), "hold graphics queue before upload");
        // always release the native hold before reporting a failed assertion.
        bool valid = true;
        for (uint32_t step = 0; step < 8 && owner.Progress().phase != RendererUploadPhase::Submitted && valid; ++step)
            valid &= bool(owner.Step(1, &passes, health));
        valid &= bool(owner.PollCompletion(health)) && !owner.Progress().gpuComplete;
        valid &= owner.Progress().phase == RendererUploadPhase::Submitted && !owner.Progress().cpuBorrows;
        valid &= bool(mesh.Step(1, nullptr, health)) && mesh.Progress().phase == RendererUploadPhase::Uploading && mesh.Progress().cpuBorrows;
        commands->open(); auto readback = CopyTexture(device, commands, owner.Texture(0));
        commands->copyBuffer(bufferReadback, 0, mesh.Buffer(0).indices, 0, 1); commands->close();
        valid &= device->executeCommandList(commands) != 0;
        mesh.Cancel(); valid &= mesh.Progress().phase == RendererUploadPhase::Canceled && !mesh.Progress().cpuBorrows;
        mesh.Reset(); meshScene.Reset(); meshGeometry.Reset();
        owner.Cancel(); owner.Reset(); image.Reset(); scene.Reset(); geometry.Reset(); commands = nullptr;
        device->runGarbageCollection();
        const bool released = SUCCEEDED(hold->Signal(1));
        Require(released && valid && device->waitForIdle(), "pending resources and copied CPU bytes survive owner destruction");
        bool same = true;
        for (uint32_t mip = 0; mip < readback->getDesc().mipLevels; ++mip)
        {
            const size_t rowBytes = Axis(7, mip) * 4;
            size_t pitch = 0;
            const auto* mapped = static_cast<const uint8_t*>(device->mapStagingTexture(readback,
                nvrhi::TextureSlice().setMipLevel(mip), nvrhi::CpuAccessMode::Read, &pitch));
            Require(mapped && pitch >= rowBytes, "pending pixel readback");
            for (size_t row = 0; row < Axis(5, mip); ++row) same &= memcmp(mapped + row * pitch, expected, rowBytes) == 0;
            device->unmapStagingTexture(readback);
        }
        const auto* byte = static_cast<const uint8_t*>(device->mapBuffer(bufferReadback, nvrhi::CpuAccessMode::Read));
        same &= byte && *byte == expectedByte; device->unmapBuffer(bufferReadback);
        Require(same, "physical completion after buffer, texture and mip-binding owner destruction");
    }

    void ImageFailures(nvrhi::IDevice* device, RendererCommonPasses& passes, RendererUploadHealth health)
    {
        Encoded encoded; encoded.Png(7, 5, 4);
        ImportDecodedImage image; Imported(image.Decode(encoded.View("fixture.png")));
        RendererScene scene; TextureScene(scene, 1); ImportGeometry geometry;
        RendererSceneResourcesNvrhi owner(device);
        Good(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health), "texture limits preparation");
        RendererUploadOptions limits; limits.maxStorageBytes = owner.StorageBytes(); limits.maxTextureBytes = owner.TextureBytes(); owner.Reset();
        for (uint32_t limit = 0; limit < 2; ++limit)
        {
            auto options = limits;
            if (limit == 0) --options.maxStorageBytes; else --options.maxTextureBytes;
            Require(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health, options).error == RendererUploadError::Capacity &&
                owner.Progress().phase == RendererUploadPhase::Empty, "texture capacity fails transactionally"); ++rejected;
        }
        Good(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health, limits), "exact texture capacity"); owner.Reset();
        Require(owner.Prepare(scene.View(), geometry, {}, nullptr, health).error == RendererUploadError::Input &&
            owner.Progress().phase == RendererUploadPhase::Empty, "missing decoded texture is not ready"); ++rejected;
        for (uint32_t ordinal = 1; ordinal <= 3; ++ordinal)
        {
            SetRendererUploadFailure(RendererUploadFailure::Creation, ordinal);
            Require(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health).error == RendererUploadError::Gpu &&
                owner.Progress().phase == RendererUploadPhase::Empty, "texture creation failure releases resources");
            SetRendererUploadFailure(RendererUploadFailure::None, 0);
            Good(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health), "retry texture creation"); owner.Reset(); ++rejected;
        }
        SetRendererUploadFailure(RendererUploadFailure::Allocation, 2);
        Require(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health).error == RendererUploadError::Allocation &&
            owner.Progress().phase == RendererUploadPhase::Empty, "texture table allocation failure");
        SetRendererUploadFailure(RendererUploadFailure::None, 0); ++rejected;
        for (uint32_t mode = 0; mode < 3; ++mode)
        {
            Good(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health), "mip failure preparation");
            Good(owner.Step(1, &passes, health), "authored upload before mip failure");
            if (mode == 0) SetRendererUploadFailure(RendererUploadFailure::Creation, 1);
            if (mode == 2) FailNextNvrhiRtvAllocation(device);
            const auto result = owner.Step(1, mode != 1 ? &passes : nullptr, health);
            SetRendererUploadFailure(RendererUploadFailure::None, 0);
            Require(result.error == (mode != 1 ? RendererUploadError::Gpu : RendererUploadError::Input) &&
                owner.Progress().phase == RendererUploadPhase::Failed && !owner.Progress().cpuBorrows && owner.Progress().submissions == 1,
                "missing mip work cannot publish a texture");
            Require(device->waitForIdle(), "failed mip retirement"); Good(owner.PollCompletion(health), "failed mip completion"); owner.Reset(); ++rejected;
        }
        encoded.Dds(71, 9, 9, 2); Imported(image.Decode(encoded.View("short-physical-mip.dds")));
        Require(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health).error == RendererUploadError::Input &&
            owner.Progress().phase == RendererUploadPhase::Empty, "DDS mip bytes must cover the padded physical texture"); ++rejected;
    }
}

void TestImportUploadGpu(nvrhi::IDevice* device, ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue,
    RendererCommonPasses& passes, RendererUploadHealth health)
{
    CheckedUploadLayout();
    RendererGpuReference reference("renderer_upload_gpu_fixture.bin");
    Encoded encoded;
    const uint32_t dimensions[][2]{{7,5}, {16,8}, {1,13}};
    for (const auto& size : dimensions) for (uint32_t channels = 1; channels <= 4; ++channels)
        for (uint32_t flags = 0; flags < 4; ++flags)
        {
            encoded.Png(size[0], size[1], channels);
            ImageCase(device, passes, health, reference, encoded, "fixture.png", (flags & 1) != 0, (flags & 2) != 0);
        }
    for (uint32_t mips = 0; mips < 2; ++mips)
    {
        encoded.Png(7, 5, 3, true); ImageCase(device, passes, health, reference, encoded, "fixture.hdr", false, mips != 0);
    }
    struct DdsCase { uint32_t code, width, height, mips, dimension, arrays, depth; bool cube, native; };
    const DdsCase cases[]{
        {28,7,5,3,3,2,1,false,true}, {28,8,8,4,3,1,1,true,true}, {28,8,8,3,3,2,1,true,true},
        {28,8,4,4,4,1,3,false,true}, {28,8,1,4,2,1,1,false,true}, {28,8,1,4,2,3,1,false,false},
        {71,7,5,3,3,1,1,false,true}, {98,16,8,5,3,1,1,false,true},
        {10,7,5,3,3,2,1,false,true}, {2,7,5,3,3,1,1,false,true}, {61,7,5,3,3,2,1,false,true}};
    for (const auto& item : cases)
    {
        encoded.Dds(item.code, item.width, item.height, item.mips, item.dimension, item.arrays, item.depth, item.cube);
        ImageCase(device, passes, health, reference, encoded, "fixture.dds", false, true, item.native);
    }
    reference.Finish(621);
    GeometryCase(device, health);
    LoadedBindings(device, nativeDevice, nativeQueue, passes, health);
    LoadedHandoff(device, nativeDevice, nativeQueue, passes, health);
    ImageFailures(device, passes, health);
    BlockedRetirement(device, nativeDevice, nativeQueue, passes, health);
    Require(health.check(health.context), "no NVRHI errors in upload fixtures");
    printf("owned upload GPU: %zu captured images, %zu captured subresources, %zu authored subresources, %zu rejected/faulted operations recovered\n",
        referenceImages, referenceSubresources, literalSubresources, rejected);
}

void TestBlitCacheUploadFailure(nvrhi::IDevice* device, uvsr::RendererShaderFactory& factory,
    RendererUploadHealth health)
{
    RendererCommonPasses passes(device, &factory);
    Require(passes.IsValid() && !passes.HasBlitPipelineFailure(), "fresh mip cache");
    Encoded encoded; encoded.Png(7, 5, 4);
    ImportDecodedImage image; Imported(image.Decode(encoded.View("fixture.png")));
    RendererScene scene; TextureScene(scene, 1); ImportGeometry geometry;
    RendererSceneResourcesNvrhi owner(device);
    Good(owner.Prepare(scene.View(), geometry, {&image, 1}, nullptr, health), "cache failure upload preparation");
    Good(owner.Step(1, &passes, health), "authored mip before cache failure");
    const auto before = owner.Progress();
    Require(before.submissions == 1 && before.texturesSubmitted == 0 &&
        before.phase == RendererUploadPhase::Uploading, "one prior authored submission and no published texture");
    FailNextRendererBlitPipelineAllocation();
    const auto result = owner.Step(1, &passes, health);
    const auto failed = owner.Progress();
    Require(result.error == RendererUploadError::Gpu && result.index == 0 && passes.HasBlitPipelineFailure() &&
        !RendererBlitPipelineAllocationFailurePending() &&
        failed.phase == RendererUploadPhase::Failed && !failed.cpuBorrows &&
        failed.submissions == before.submissions && failed.submittedBytes == before.submittedBytes &&
        failed.texturesSubmitted == before.texturesSubmitted, "cache failure cannot submit or publish the generated mip");
    const auto repeated = owner.Step(1, &passes, health);
    const auto terminal = owner.Progress();
    Require(repeated.error == result.error && repeated.index == result.index &&
        terminal.phase == RendererUploadPhase::Failed && !terminal.cpuBorrows &&
        terminal.submissions == failed.submissions && terminal.submittedBytes == failed.submittedBytes &&
        terminal.texturesSubmitted == failed.texturesSubmitted, "cache failure remains terminal without upload progress");
    Require(device->waitForIdle(), "prior authored mip retirement after cache failure");
    Good(owner.PollCompletion(health), "failed cache upload completion");
    Require(owner.Progress().phase == RendererUploadPhase::Failed && owner.Progress().gpuComplete,
        "physical completion retains the failed upload phase");
    owner.Reset();
    Require(owner.Progress().phase == RendererUploadPhase::Empty && health.check(health.context), "failed cache upload reset");
    printf("blit cache allocation failure preserves one prior upload submission and prevents mip publication\n");
}
