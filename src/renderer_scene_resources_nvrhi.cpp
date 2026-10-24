#include "renderer_scene_resources_nvrhi.h"
#include "renderer_skinning_contract.h"
#include "renderer_common_passes_nvrhi.h"
#include <math.h>
#include <new>
#include <stdlib.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene resource ownership requires exception-disabled compilation
#endif

namespace uvsr
{
    nvrhi::Format RendererImportImageFormat(ImportImageFormat format) noexcept
    {
        switch (format)
        {
#define UVSR_FORMAT(name) case ImportImageFormat::name: return nvrhi::Format::name
            UVSR_FORMAT(R8_UINT); UVSR_FORMAT(R8_SINT); UVSR_FORMAT(R8_UNORM); UVSR_FORMAT(R8_SNORM);
            UVSR_FORMAT(RG8_UINT); UVSR_FORMAT(RG8_SINT); UVSR_FORMAT(RG8_UNORM); UVSR_FORMAT(RG8_SNORM);
            UVSR_FORMAT(R16_UINT); UVSR_FORMAT(R16_SINT); UVSR_FORMAT(R16_UNORM); UVSR_FORMAT(R16_SNORM); UVSR_FORMAT(R16_FLOAT);
            UVSR_FORMAT(BGRA4_UNORM); UVSR_FORMAT(B5G6R5_UNORM); UVSR_FORMAT(B5G5R5A1_UNORM);
            UVSR_FORMAT(RGBA8_UINT); UVSR_FORMAT(RGBA8_SINT); UVSR_FORMAT(RGBA8_UNORM); UVSR_FORMAT(RGBA8_SNORM);
            UVSR_FORMAT(BGRA8_UNORM); UVSR_FORMAT(BGRX8_UNORM); UVSR_FORMAT(SRGBA8_UNORM); UVSR_FORMAT(SBGRA8_UNORM); UVSR_FORMAT(SBGRX8_UNORM);
            UVSR_FORMAT(R10G10B10A2_UNORM); UVSR_FORMAT(R11G11B10_FLOAT);
            UVSR_FORMAT(RG16_UINT); UVSR_FORMAT(RG16_SINT); UVSR_FORMAT(RG16_UNORM); UVSR_FORMAT(RG16_SNORM); UVSR_FORMAT(RG16_FLOAT);
            UVSR_FORMAT(R32_UINT); UVSR_FORMAT(R32_SINT); UVSR_FORMAT(R32_FLOAT);
            UVSR_FORMAT(RGBA16_UINT); UVSR_FORMAT(RGBA16_SINT); UVSR_FORMAT(RGBA16_FLOAT); UVSR_FORMAT(RGBA16_UNORM); UVSR_FORMAT(RGBA16_SNORM);
            UVSR_FORMAT(RG32_UINT); UVSR_FORMAT(RG32_SINT); UVSR_FORMAT(RG32_FLOAT);
            UVSR_FORMAT(RGB32_UINT); UVSR_FORMAT(RGB32_SINT); UVSR_FORMAT(RGB32_FLOAT);
            UVSR_FORMAT(RGBA32_UINT); UVSR_FORMAT(RGBA32_SINT); UVSR_FORMAT(RGBA32_FLOAT);
            UVSR_FORMAT(D24S8); UVSR_FORMAT(X24G8_UINT); UVSR_FORMAT(D32S8); UVSR_FORMAT(X32G8_UINT);
            UVSR_FORMAT(BC1_UNORM); UVSR_FORMAT(BC1_UNORM_SRGB); UVSR_FORMAT(BC2_UNORM); UVSR_FORMAT(BC2_UNORM_SRGB);
            UVSR_FORMAT(BC3_UNORM); UVSR_FORMAT(BC3_UNORM_SRGB); UVSR_FORMAT(BC4_UNORM); UVSR_FORMAT(BC4_SNORM);
            UVSR_FORMAT(BC5_UNORM); UVSR_FORMAT(BC5_SNORM); UVSR_FORMAT(BC6H_UFLOAT); UVSR_FORMAT(BC6H_SFLOAT);
            UVSR_FORMAT(BC7_UNORM); UVSR_FORMAT(BC7_UNORM_SRGB);
#undef UVSR_FORMAT
        default: return nvrhi::Format::UNKNOWN;
        }
    }

    namespace
    {
        using Error = RendererUploadError;
        using Result = RendererUploadResult;
        using Phase = RendererUploadPhase;
        using Attribute = RendererSceneVertexAttribute;
        enum class Operation : uint8_t { Allocation, Creation, Submission };
#if defined(UVSR_BUILD_TESTING)
        RendererUploadFailure testFailure = RendererUploadFailure::None;
        uint32_t testOrdinal = 0, testCount = 0;
#endif
        bool Allowed(Operation operation) noexcept
        {
#if defined(UVSR_BUILD_TESTING)
            if (uint32_t(testFailure) == uint32_t(operation) + 1 && ++testCount == testOrdinal) return false;
#else
            (void)operation;
#endif
            return true;
        }
        bool Healthy(RendererUploadHealth health) noexcept
        { return health.check && health.check(health.context); }
        bool Add(uint64_t& total, uint64_t amount, uint64_t limit = UINT64_MAX) noexcept
        {
            if (total > limit || amount > limit - total) return false;
            total += amount; return true;
        }
        bool Multiply(uint64_t a, uint64_t b, uint64_t& output) noexcept
        {
            if (b && a > UINT64_MAX / b) return false;
            output = a * b; return true;
        }
        template<class T> bool Live(ArrayView<T> view) noexcept
        {
            return view.IsValid() && view.count <= size_t(PTRDIFF_MAX) / sizeof(T) &&
                (!view.count || reinterpret_cast<uintptr_t>(view.data) <= UINTPTR_MAX - view.count * sizeof(T));
        }
        template<class T> T* Allocate(size_t count) noexcept
        {
            if (!count || count > size_t(PTRDIFF_MAX) / sizeof(T) || !Allowed(Operation::Allocation)) return nullptr;
            auto* objects = static_cast<T*>(malloc(count * sizeof(T)));
            if (objects) for (size_t i = 0; i < count; ++i) new (objects + i) T{};
            return objects;
        }
        template<class T> void Destroy(T* objects, size_t count) noexcept
        {
            if (!objects) return;
            for (size_t i = 0; i < count; ++i) objects[i].~T();
            free(objects);
        }
        uint32_t MipAxis(uint32_t value, uint32_t mip) noexcept
        { const uint32_t axis = value >> mip; return axis ? axis : 1; }



        struct Group
        {
            nvrhi::BufferHandle indices, vertices, joints;
            nvrhi::BindingSetHandle skinBindings;
            ImportGeometryBufferView source;
            uint64_t vertexBytes = 0;
            uint32_t indexOwner = InvalidSceneIndex;
            uint32_t prototypeGroup = InvalidSceneIndex;
            RendererSkinningConstants skin{0, RendererSkinFirstFrame};
        };
        struct UploadedTexture
        {
            nvrhi::TextureHandle resource;
            ImportDecodedImageInfo info;
            ArrayView<const uint8_t> bytes;
            ArrayView<const ImportImageSubresource> layouts;
            uint32_t width = 0, height = 0, mipLevels = 0;
        };

        bool Footprint(const UploadedTexture& texture, uint32_t mip, uint64_t& row, uint64_t& rows,
            uint64_t& depth, uint64_t& bytes) noexcept
        {
            const uint64_t width = MipAxis(texture.width, mip), height = MipAxis(texture.height, mip);
            depth = MipAxis(texture.info.depth, mip);
            const auto& format = nvrhi::getFormatInfo(RendererImportImageFormat(texture.info.format));
            if (!format.blockSize || !format.bytesPerBlock) return false;
            row = ((width - 1) / format.blockSize + 1) * format.bytesPerBlock;
            rows = (height - 1) / format.blockSize + 1;
            return Multiply(row, rows, bytes) && Multiply(bytes, depth, bytes);
        }

        bool SkinOffset(const RendererSceneBufferGroup& group, Attribute attribute, uint32_t firstVertex,
            uint32_t vertexCount, uint32_t stride, uint32_t& result) noexcept
        {
            const auto range = group.attributes[uint32_t(attribute)];
            const uint64_t first = uint64_t(firstVertex) * stride, length = uint64_t(vertexCount) * stride;
            if (range.offset > group.vertexBytes || range.size > group.vertexBytes - range.offset ||
                first > range.size || length > range.size - first || range.offset + first > UINT32_MAX ||
                length > UINT32_MAX - (range.offset + first)) return false;
            result = uint32_t(range.offset + first); return true;
        }
    }

    struct RendererSceneResourcesNvrhi::State
    {
        Group* groups = nullptr;
        UploadedTexture* textures = nullptr;
        uint32_t groupCount = 0, textureCount = 0;
        uint64_t generation = 0, bufferBytes = 0, textureBytes = 0;
        size_t storageBytes = 0;
        RendererUploadProgress progress;
        RendererUploadResult failure;
        nvrhi::CommandListHandle commands;
        nvrhi::EventQueryHandle completion;
        nvrhi::BindingLayoutHandle skinLayout;
        nvrhi::ComputePipelineHandle skinPipeline;
        uint32_t uploadGroup = 0, uploadPart = 0, uploadTexture = 0, textureSubresource = 0, skinGroup = 0;
        size_t bufferOffset = 0;
        uint32_t generatedMip = 0;
        uint32_t skinVertex = 0;
        bool rayTracing = false;

        ~State() noexcept
        {
            Destroy(groups, groupCount);
            Destroy(textures, textureCount);
        }

        void ReleaseBorrows() noexcept
        {
            for (uint32_t i = 0; i < groupCount; ++i) groups[i].source = {};
            for (uint32_t i = 0; i < textureCount; ++i) { textures[i].bytes = {}; textures[i].layouts = {}; }
            progress.cpuBorrows = false;
        }
        void ReleaseLoadingResources() noexcept
        {
            for (uint32_t i = 0; i < groupCount; ++i) { groups[i].skinBindings = nullptr; groups[i].joints = nullptr; }
            skinPipeline = nullptr; skinLayout = nullptr; commands = nullptr;
        }
        Result PlanSkin(uint32_t index, const RendererSceneView& scene) noexcept
        {
            auto& group = groups[index];
            const auto instanceIndex = group.source.skinInstanceIndex;
            if (instanceIndex >= scene.instances.count || !group.source.jointMatrices.count) return {Error::Input, index};
            const auto& instance = scene.instances.data[instanceIndex];
            if (instance.meshIndex >= scene.meshes.count || instance.joints.count != group.source.jointMatrices.count)
                return {Error::Input, index};
            const auto& mesh = scene.meshes.data[instance.meshIndex];
            if (mesh.bufferGroupIndex != index || mesh.skinPrototypeIndex >= scene.meshes.count || !mesh.vertexCount || mesh.vertexOffset)
                return {Error::Input, index};
            const auto& prototype = scene.meshes.data[mesh.skinPrototypeIndex];
            if (prototype.bufferGroupIndex >= groupCount || prototype.skinPrototypeIndex != InvalidSceneIndex ||
                prototype.vertexCount != mesh.vertexCount || prototype.bufferGroupIndex == index) return {Error::Input, index};
            group.prototypeGroup = prototype.bufferGroupIndex;
            const auto& input = scene.bufferGroups.data[prototype.bufferGroupIndex];
            const auto& output = scene.bufferGroups.data[index];
            auto& constants = group.skin;
            constants.vertexCount = mesh.vertexCount;
            if (!SkinOffset(input, Attribute::Position, prototype.vertexOffset, mesh.vertexCount, 12, constants.inputPosition) ||
                !SkinOffset(input, Attribute::JointIndices, prototype.vertexOffset, mesh.vertexCount, 8, constants.inputJoints) ||
                !SkinOffset(input, Attribute::JointWeights, prototype.vertexOffset, mesh.vertexCount, 16, constants.inputWeights) ||
                !SkinOffset(output, Attribute::Position, 0, mesh.vertexCount, 12, constants.outputPosition) ||
                !SkinOffset(output, Attribute::PreviousPosition, 0, mesh.vertexCount, 12, constants.outputPrevious))
                return {Error::Input, index};
            struct Optional { Attribute attribute; uint32_t stride, flag; uint32_t* in; uint32_t* out; };
            const Optional attributes[]{
                {Attribute::Normal, 4, 2, &constants.inputNormal, &constants.outputNormal},
                {Attribute::Tangent, 4, 4, &constants.inputTangent, &constants.outputTangent},
                {Attribute::TexCoord0, 8, 8, &constants.inputUV0, &constants.outputUV0},
                {Attribute::TexCoord1, 8, 16, &constants.inputUV1, &constants.outputUV1}};
            for (const auto& attribute : attributes)
            {
                const bool exists = input.attributes[uint32_t(attribute.attribute)].size != 0;
                if (exists != (output.attributes[uint32_t(attribute.attribute)].size != 0)) return {Error::Input, index};
                if (!exists) continue;
                if (!SkinOffset(input, attribute.attribute, prototype.vertexOffset, mesh.vertexCount, attribute.stride, *attribute.in) ||
                    !SkinOffset(output, attribute.attribute, 0, mesh.vertexCount, attribute.stride, *attribute.out)) return {Error::Input, index};
                constants.flags |= attribute.flag;
            }
            return {};
        }

        Result Plan(nvrhi::IDevice* device, const RendererSceneView& scene, const ImportGeometry& geometry,
            ArrayView<const ImportDecodedImage> images, const RendererUploadOptions& options) noexcept
        {
            if (!scene.generation || !Live(scene.bufferGroups) || !Live(scene.textures) || !Live(scene.meshes) ||
                !Live(scene.instances) || !Live(images) || scene.bufferGroups.count > UINT32_MAX ||
                scene.textures.count > UINT32_MAX || geometry.BufferCount() != scene.bufferGroups.count ||
                images.count != scene.textures.count) return {Error::Input};
            if (options.rayTracing && !device->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct)) return {Error::Unsupported};
            uint64_t storage = sizeof(State);
            if (!Add(storage, scene.bufferGroups.count * sizeof(Group), options.maxStorageBytes) ||
                !Add(storage, images.count * sizeof(UploadedTexture), options.maxStorageBytes) || storage > PTRDIFF_MAX) return {Error::Capacity};
            storageBytes = size_t(storage); generation = scene.generation; rayTracing = options.rayTracing;
            groupCount = uint32_t(scene.bufferGroups.count); textureCount = uint32_t(images.count);
            groups = Allocate<Group>(groupCount); textures = Allocate<UploadedTexture>(textureCount);
            if ((groupCount && !groups) || (textureCount && !textures)) return {Error::Allocation};
            for (uint32_t i = 0; i < groupCount; ++i)
            {
                auto& group = groups[i];
                group.source = geometry.Buffer(i); group.vertexBytes = scene.bufferGroups.data[i].vertexBytes;
                const auto& source = group.source;
                if (!Live(source.indices) || !Live(source.vertices) || !Live(source.jointMatrices) ||
                    source.indices.count != scene.bufferGroups.data[i].indexBytes || (source.indices.count % 4) ||
                    (source.indexOwner != InvalidSceneIndex && source.indexOwner >= groupCount) ||
                    (source.indices.count && source.indexOwner == InvalidSceneIndex) || group.vertexBytes > PTRDIFF_MAX)
                    return {Error::Input, i};
                if (source.indices.count)
                {
                    const auto owner = geometry.Buffer(source.indexOwner);
                    if (owner.indexOwner != source.indexOwner || owner.skinInstanceIndex != InvalidSceneIndex || owner.indices.data != source.indices.data ||
                        owner.indices.count != source.indices.count) return {Error::Input, i};
                    group.indexOwner = source.indexOwner;
                }
                const bool skin = source.skinInstanceIndex != InvalidSceneIndex;
                if ((skin && source.vertices.count) || (!skin && (source.vertices.count != group.vertexBytes || source.jointMatrices.count)))
                    return {Error::Input, i};
                if (skin) { const auto result = PlanSkin(i, scene); if (!result) return result; }
                uint64_t joints = 0;
                if (!Multiply(source.jointMatrices.count, sizeof(gpu_contract::Float4x4), joints) ||
                    !Add(bufferBytes, group.vertexBytes, options.maxBufferBytes) || !Add(bufferBytes, joints, options.maxBufferBytes) ||
                    !Add(progress.totalBytes, source.vertices.count) || !Add(progress.totalBytes, joints)) return {Error::Capacity, i};
                if (source.indexOwner == i && (!Add(bufferBytes, source.indices.count, options.maxBufferBytes) ||
                    !Add(progress.totalBytes, source.indices.count))) return {Error::Capacity, i};
                if (source.indexOwner != i) group.source.indices = {};
                group.source.morphs = {};
            }
            for (uint32_t i = 0; i < textureCount; ++i)
            {
                auto& texture = textures[i];
                texture.info = images.data[i].Info(); texture.bytes = images.data[i].Bytes(); texture.layouts = images.data[i].Subresources();
                const auto& info = texture.info;
                const auto format = RendererImportImageFormat(info.format);
                if (format == nvrhi::Format::UNKNOWN || !info.width || !info.height || !info.depth || !info.arraySize ||
                    !info.mipLevels || info.mipLevels > 32 || !Live(texture.bytes) || !Live(texture.layouts) ||
                    !texture.bytes.count || texture.layouts.count > UINT32_MAX ||
                    uint64_t(info.arraySize) * info.mipLevels != texture.layouts.count) return {Error::Input, i};
                texture.width = info.width; texture.height = info.height;
                if (ImportImageBlockBytes(info.format))
                {
                    if (info.width > UINT32_MAX - 3 || info.height > UINT32_MAX - 3) return {Error::Capacity, i};
                    texture.width = (info.width + 3) & ~3u; texture.height = (info.height + 3) & ~3u;
                }
                texture.mipLevels = info.mipLevels;
                if (info.allowGeneratedMips)
                {
                    if (info.dimension != ImportImageDimension::Texture2D || info.arraySize != 1 || info.depth != 1 || info.mipLevels != 1)
                        return {Error::Input, i};
                    if (options.generateMips)
                        texture.mipLevels = uint32_t(logf(float(info.width < info.height ? info.width : info.height)) / logf(2.f)) + 1;
                }
                nvrhi::FormatSupport required = nvrhi::FormatSupport::Texture;
                if (info.allowGeneratedMips) required = required | nvrhi::FormatSupport::RenderTarget | nvrhi::FormatSupport::ShaderSample;
                if ((device->queryFormatSupport(format) & required) != required) return {Error::Unsupported, i};
                for (uint32_t mip = 0; mip < texture.mipLevels; ++mip)
                {
                    uint64_t row = 0, rows = 0, depth = 0, bytes = 0;
                    if (!Footprint(texture, mip, row, rows, depth, bytes) || !Multiply(bytes, info.arraySize, bytes) ||
                        !Add(textureBytes, bytes, options.maxTextureBytes)) return {Error::Capacity, i};
                }
                for (size_t subresource = 0; subresource < texture.layouts.count; ++subresource)
                {
                    const auto& layout = texture.layouts.data[subresource];
                    uint64_t row = 0, rows = 0, depth = 0, bytes = 0, plane = 0, span = 0;
                    if (!Footprint(texture, uint32_t(subresource % info.mipLevels), row, rows, depth, bytes) ||
                        !Multiply(layout.rowPitch, rows, plane) || !Multiply(layout.depthPitch, depth, span)) return {Error::Capacity, i};
                    if (layout.offset > texture.bytes.count || layout.size > texture.bytes.count - layout.offset ||
                        layout.rowPitch < row || layout.depthPitch < plane || layout.size < span) return {Error::Input, i};
                    if (!Add(progress.totalBytes, layout.size)) return {Error::Capacity, i};
                }
            }
            progress.cpuBorrows = progress.totalBytes != 0;
            return {};
        }

        Result Create(nvrhi::IDevice* device, nvrhi::IShader* skinShader, RendererUploadHealth health) noexcept
        {
            commands = Allowed(Operation::Creation) ? device->createCommandList() : nullptr;
            completion = Allowed(Operation::Creation) ? device->createEventQuery() : nullptr;
            if (!commands || !completion || !Healthy(health)) return {Error::Gpu};
            bool skins = false;
            for (uint32_t i = 0; i < groupCount; ++i)
            {
                auto& group = groups[i];
                nvrhi::BufferDesc desc;
                desc.canHaveRawViews = true; desc.canHaveTypedViews = true; desc.keepInitialState = true;
                desc.isAccelStructBuildInput = rayTracing;
                if (group.source.indices.count)
                {
                    desc.byteSize = group.source.indices.count; desc.isIndexBuffer = true; desc.format = nvrhi::Format::R32_UINT;
                    desc.initialState = nvrhi::ResourceStates::IndexBuffer | nvrhi::ResourceStates::ShaderResource;
                    if (rayTracing) desc.initialState = desc.initialState | nvrhi::ResourceStates::AccelStructBuildInput;
                    desc.debugName = "scene indices";
                    group.indices = Allowed(Operation::Creation) ? device->createBuffer(desc) : nullptr;
                    if (!group.indices || !Healthy(health)) return {Error::Gpu, i};
                }
                if (group.vertexBytes)
                {
                    desc.byteSize = group.vertexBytes; desc.isIndexBuffer = false; desc.isVertexBuffer = true;
                    desc.format = nvrhi::Format::UNKNOWN; desc.canHaveUAVs = group.prototypeGroup != InvalidSceneIndex;
                    desc.initialState = nvrhi::ResourceStates::VertexBuffer | nvrhi::ResourceStates::ShaderResource;
                    if (rayTracing) desc.initialState = desc.initialState | nvrhi::ResourceStates::AccelStructBuildInput;
                    desc.debugName = "scene vertices";
                    group.vertices = Allowed(Operation::Creation) ? device->createBuffer(desc) : nullptr;
                    if (!group.vertices || !Healthy(health)) return {Error::Gpu, i};
                }
                if (group.source.jointMatrices.count)
                {
                    skins = true;
                    nvrhi::BufferDesc joints;
                    joints.byteSize = group.source.jointMatrices.count * sizeof(gpu_contract::Float4x4);
                    joints.canHaveRawViews = true; joints.keepInitialState = true; joints.initialState = nvrhi::ResourceStates::ShaderResource;
                    joints.debugName = "loading skin joints";
                    group.joints = Allowed(Operation::Creation) ? device->createBuffer(joints) : nullptr;
                    if (!group.joints || !Healthy(health)) return {Error::Gpu, i};
                }
            }
            // the canonical index owner can occur after its consumers.
            for (uint32_t i = 0; i < groupCount; ++i)
            {
                const auto owner = groups[i].source.indexOwner;
                if (owner != InvalidSceneIndex && owner != i) groups[i].indices = groups[owner].indices;
            }
            if (skins)
            {
                if (!skinShader || skinShader->getDesc().shaderType != nvrhi::ShaderType::Compute) return {Error::Input};
                nvrhi::BindingLayoutDesc layout;
                layout.visibility = nvrhi::ShaderType::Compute;
                layout.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(RendererSkinningConstants)),
                    nvrhi::BindingLayoutItem::RawBuffer_SRV(0), nvrhi::BindingLayoutItem::RawBuffer_SRV(1), nvrhi::BindingLayoutItem::RawBuffer_UAV(0)};
                skinLayout = Allowed(Operation::Creation) ? device->createBindingLayout(layout) : nullptr;
                if (!skinLayout || !Healthy(health)) return {Error::Gpu};
                nvrhi::ComputePipelineDesc pipeline; pipeline.CS = skinShader; pipeline.bindingLayouts = {skinLayout};
                skinPipeline = Allowed(Operation::Creation) ? device->createComputePipeline(pipeline) : nullptr;
                if (!skinPipeline || !Healthy(health)) return {Error::Gpu};
                for (uint32_t i = 0; i < groupCount; ++i)
                {
                    auto& group = groups[i];
                    if (group.prototypeGroup == InvalidSceneIndex) continue;
                    if (!groups[group.prototypeGroup].vertices) return {Error::Input, i};
                    nvrhi::BindingSetDesc bindings;
                    bindings.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(RendererSkinningConstants)),
                        nvrhi::BindingSetItem::RawBuffer_SRV(0, groups[group.prototypeGroup].vertices),
                        nvrhi::BindingSetItem::RawBuffer_SRV(1, group.joints), nvrhi::BindingSetItem::RawBuffer_UAV(0, group.vertices)};
                    group.skinBindings = Allowed(Operation::Creation) ? device->createBindingSet(bindings, skinLayout) : nullptr;
                    if (!group.skinBindings || !Healthy(health)) return {Error::Gpu, i};
                }
            }
            for (uint32_t i = 0; i < textureCount; ++i)
            {
                auto& texture = textures[i];
                const auto& info = texture.info;
                nvrhi::TextureDesc desc;
                desc.width = texture.width; desc.height = texture.height; desc.depth = info.depth;
                desc.arraySize = info.arraySize; desc.mipLevels = texture.mipLevels; desc.format = RendererImportImageFormat(info.format);
                desc.isRenderTarget = info.allowGeneratedMips;
                desc.initialState = nvrhi::ResourceStates::ShaderResource; desc.keepInitialState = true;
                switch (info.dimension)
                {
                case ImportImageDimension::Texture1D:
                    desc.dimension = info.arraySize > 1 ? nvrhi::TextureDimension::Texture1DArray : nvrhi::TextureDimension::Texture1D; break;
                case ImportImageDimension::Texture2D:
                    desc.dimension = info.arraySize > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D; break;
                case ImportImageDimension::Texture3D: desc.dimension = nvrhi::TextureDimension::Texture3D; break;
                case ImportImageDimension::Cube:
                    desc.dimension = info.arraySize > 6 ? nvrhi::TextureDimension::TextureCubeArray : nvrhi::TextureDimension::TextureCube; break;
                default: return {Error::Input, i};
                }
                desc.debugName = "scene texture";
                texture.resource = Allowed(Operation::Creation) ? device->createTexture(desc) : nullptr;
                if (!texture.resource || !Healthy(health)) return {Error::Gpu, i};
            }
            progress.phase = Phase::Prepared;
            return {};
        }
    };

    RendererSceneResourcesNvrhi::RendererSceneResourcesNvrhi(nvrhi::IDevice* device) noexcept : m_Device(device) {}
    RendererSceneResourcesNvrhi::~RendererSceneResourcesNvrhi() noexcept { Reset(); }

    RendererUploadResult RendererSceneResourcesNvrhi::Prepare(const RendererSceneView& scene, const ImportGeometry& geometry,
        ArrayView<const ImportDecodedImage> images, nvrhi::IShader* skinShader, RendererUploadHealth health,
        const RendererUploadOptions& options) noexcept
    {
        if (m_State) return {Error::InvalidState};
        if (!m_Device || !health.check) return {Error::Input};
        if (!Healthy(health)) return {Error::Gpu};
        if (options.maxStorageBytes < sizeof(State)) return {Error::Capacity};
        auto* candidate = Allocate<State>(1);
        if (!candidate) return {Error::Allocation};
        auto result = candidate->Plan(m_Device, scene, geometry, images, options);
        if (result) result = candidate->Create(m_Device, skinShader, health);
        if (!result) { Destroy(candidate, 1); return result; }
        m_State = candidate; return {};
    }

    RendererUploadResult RendererSceneResourcesNvrhi::Step(size_t byteBudget, RendererCommonPasses* passes,
        RendererUploadHealth health) noexcept
    {
        if (!m_State) return {Error::InvalidState};
        auto& state = *m_State;
        if (state.progress.phase == Phase::Failed) return state.failure;
        if (state.progress.phase == Phase::Submitted || state.progress.phase == Phase::Complete) return {};
        if (state.progress.phase != Phase::Prepared && state.progress.phase != Phase::Uploading) return {Error::InvalidState};
        if (!byteBudget || !health.check) return {Error::Input};
        bool open = false;
        const auto fail = [&](Result result) noexcept
        {
            if (open) state.commands->close();
            state.ReleaseBorrows(); state.ReleaseLoadingResources();
            state.progress.phase = Phase::Failed; state.failure = result; return result;
        };
        if (!Healthy(health)) return fail({Error::Gpu});
        state.commands->open(); open = true;
        if (!Healthy(health)) return fail({Error::Gpu});
        size_t cost = 0;
        uint64_t copied = 0;
        bool recorded = false;
        while (cost < byteBudget)
        {
            if (state.uploadGroup < state.groupCount)
            {
                auto& group = state.groups[state.uploadGroup];
                ArrayView<const uint8_t> source;
                nvrhi::IBuffer* destination = nullptr;
                if (state.uploadPart == 0) { source = group.source.indices; destination = group.indices; }
                else if (state.uploadPart == 1) { source = group.source.vertices; destination = group.vertices; }
                else if (state.uploadPart == 2)
                {
                    source = {reinterpret_cast<const uint8_t*>(group.source.jointMatrices.data),
                        group.source.jointMatrices.count * sizeof(gpu_contract::Float4x4)};
                    destination = group.joints;
                }
                else { ++state.uploadGroup; state.uploadPart = 0; state.bufferOffset = 0; continue; }
                if (state.bufferOffset == source.count) { ++state.uploadPart; state.bufferOffset = 0; continue; }
                size_t bytes = source.count - state.bufferOffset;
                if (bytes > byteBudget - cost) bytes = byteBudget - cost;
                state.commands->writeBuffer(destination, source.data + state.bufferOffset, bytes, state.bufferOffset);
                if (!Healthy(health)) return fail({Error::Gpu, state.uploadGroup});
                recorded = true; cost += bytes; copied += bytes; state.bufferOffset += bytes;
                if (state.bufferOffset == source.count)
                {
                    if (state.uploadPart == 0) group.source.indices = {};
                    else if (state.uploadPart == 1) group.source.vertices = {};
                    else group.source.jointMatrices = {};
                    ++state.uploadPart; state.bufferOffset = 0;
                }
                continue;
            }
            if (state.uploadTexture < state.textureCount)
            {
                auto& texture = state.textures[state.uploadTexture];
                const uint64_t count = uint64_t(texture.info.arraySize) * texture.info.mipLevels;
                if (state.textureSubresource < count)
                {
                    const auto& layout = texture.layouts.data[state.textureSubresource];
                    if (cost && layout.size > byteBudget - cost) break;
                    const uint32_t slice = state.textureSubresource / texture.info.mipLevels;
                    const uint32_t mip = state.textureSubresource % texture.info.mipLevels;
                    state.commands->writeTexture(texture.resource, slice, mip, texture.bytes.data + layout.offset, layout.rowPitch, layout.depthPitch);
                    if (!Healthy(health)) return fail({Error::Gpu, state.uploadTexture});
                    recorded = true; copied += layout.size;
                    cost = layout.size > byteBudget - cost ? byteBudget : cost + layout.size;
                    ++state.textureSubresource;
                    if (state.textureSubresource == count) { texture.bytes = {}; texture.layouts = {}; }
                    continue;
                }
                if (!state.generatedMip) state.generatedMip = texture.info.mipLevels;
                if (state.generatedMip < texture.mipLevels)
                {
                    uint64_t row = 0, rows = 0, depth = 0, bytes = 0;
                    if (!Footprint(texture, state.generatedMip, row, rows, depth, bytes)) return fail({Error::Capacity, state.uploadTexture});
                    if (cost && bytes > byteBudget - cost) break;
                    if (!passes || !passes->IsValid()) return fail({Error::Input, state.uploadTexture});
                    nvrhi::FramebufferDesc desc;
                    desc.addColorAttachment(nvrhi::FramebufferAttachment().setTexture(texture.resource).setMipLevel(state.generatedMip));
                    auto framebuffer = Allowed(Operation::Creation) ? m_Device->createFramebuffer(desc) : nullptr;
                    if (!framebuffer || !Healthy(health) || !passes->BlitTextureMip(state.commands, framebuffer, texture.resource, state.generatedMip - 1) ||
                        !Healthy(health)) return fail({Error::Gpu, state.uploadTexture});
                    recorded = true; cost = bytes > byteBudget - cost ? byteBudget : cost + size_t(bytes);
                    ++state.generatedMip; continue;
                }
                ++state.uploadTexture; state.textureSubresource = 0; state.generatedMip = 0; continue;
            }
            if (state.skinGroup < state.groupCount)
            {
                auto& group = state.groups[state.skinGroup];
                if (group.prototypeGroup == InvalidSceneIndex) { ++state.skinGroup; continue; }
                RendererSkinningConstants constants = group.skin;
                const uint32_t remaining = constants.vertexCount - state.skinVertex;
                const uint32_t stride = 24 + ((constants.flags & 2) ? 4 : 0) + ((constants.flags & 4) ? 4 : 0) +
                    ((constants.flags & 8) ? 8 : 0) + ((constants.flags & 16) ? 8 : 0);
                const size_t available = (byteBudget - cost) / stride;
                if (!available && cost) break;
                constexpr uint32_t dispatchVertices = 65535u * 256u;
                constants.vertexCount = remaining < dispatchVertices ? remaining : dispatchVertices;
                if (!available) constants.vertexCount = 1;
                else if (available < constants.vertexCount) constants.vertexCount = uint32_t(available);
                const uint32_t first = state.skinVertex;
                constants.inputPosition += first * 12; constants.inputJoints += first * 8; constants.inputWeights += first * 16;
                constants.outputPosition += first * 12; constants.outputPrevious += first * 12;
                if (constants.flags & 2) { constants.inputNormal += first * 4; constants.outputNormal += first * 4; }
                if (constants.flags & 4) { constants.inputTangent += first * 4; constants.outputTangent += first * 4; }
                if (constants.flags & 8) { constants.inputUV0 += first * 8; constants.outputUV0 += first * 8; }
                if (constants.flags & 16) { constants.inputUV1 += first * 8; constants.outputUV1 += first * 8; }
                nvrhi::ComputeState compute; compute.pipeline = state.skinPipeline; compute.bindings = {group.skinBindings};
                state.commands->setComputeState(compute);
                state.commands->setPushConstants(&constants, sizeof(constants));
                state.commands->dispatch((constants.vertexCount + 255) / 256);
                if (!Healthy(health)) return fail({Error::Gpu, state.skinGroup});
                const uint64_t bytes = uint64_t(constants.vertexCount) * stride;
                recorded = true; cost = bytes > byteBudget - cost ? byteBudget : cost + size_t(bytes);
                state.skinVertex += constants.vertexCount;
                if (state.skinVertex == group.skin.vertexCount) { ++state.skinGroup; state.skinVertex = 0; }
                continue;
            }
            break;
        }
        state.commands->close(); open = false;
        if (!Healthy(health)) return fail({Error::Gpu});
        if (recorded)
        {
            const uint64_t submission = Allowed(Operation::Submission) ? m_Device->executeCommandList(state.commands) : 0;
            if (submission)
            {
                m_Device->setEventQuery(state.completion, nvrhi::CommandQueue::Graphics);
                state.progress.gpuComplete = false;
                ++state.progress.submissions;
                state.progress.submittedBytes += copied;
            }
            if (!submission || !Healthy(health)) return fail({Error::Gpu});
        }
        const bool finished = state.uploadGroup == state.groupCount && state.uploadTexture == state.textureCount && state.skinGroup == state.groupCount;
        state.progress.texturesSubmitted = state.uploadTexture;
        state.progress.phase = finished ? Phase::Submitted : Phase::Uploading;
        if (finished)
        {
            state.ReleaseBorrows();
            if (state.progress.gpuComplete) { state.progress.phase = Phase::Complete; state.ReleaseLoadingResources(); }
        }
        return {};
    }

    RendererUploadResult RendererSceneResourcesNvrhi::PollCompletion(RendererUploadHealth health) noexcept
    {
        if (!m_State) return {Error::InvalidState};
        if (!health.check) return {Error::Input};
        auto& state = *m_State;
        const auto fail = [&]() noexcept
        {
            state.ReleaseBorrows(); state.ReleaseLoadingResources();
            state.progress.phase = Phase::Failed; state.failure = {Error::Gpu}; return state.failure;
        };
        if (!Healthy(health)) return fail();
        if (!state.progress.gpuComplete)
        {
            const bool complete = m_Device->pollEventQuery(state.completion);
            if (!Healthy(health)) return fail();
            state.progress.gpuComplete = complete;
        }
        if (state.progress.gpuComplete)
        {
            if (state.progress.phase == Phase::Submitted) state.progress.phase = Phase::Complete;
            if (state.progress.phase == Phase::Complete || state.progress.phase == Phase::Failed || state.progress.phase == Phase::Canceled)
                state.ReleaseLoadingResources();
        }
        return {};
    }

    RendererUploadProgress RendererSceneResourcesNvrhi::Progress() const noexcept { return m_State ? m_State->progress : RendererUploadProgress{}; }
    uint64_t RendererSceneResourcesNvrhi::Generation() const noexcept { return m_State ? m_State->generation : 0; }
    uint32_t RendererSceneResourcesNvrhi::BufferCount() const noexcept { return m_State ? m_State->groupCount : 0; }
    uint32_t RendererSceneResourcesNvrhi::TextureCount() const noexcept { return m_State ? m_State->textureCount : 0; }
    RendererSceneBufferResources RendererSceneResourcesNvrhi::Buffer(uint32_t index) const noexcept
    {
        if (!m_State || index >= m_State->groupCount) return {};
        return {m_State->groups[index].indices.Get(), m_State->groups[index].vertices.Get(), m_State->groups[index].indexOwner};
    }
    nvrhi::ITexture* RendererSceneResourcesNvrhi::Texture(uint32_t index) const noexcept
    { return m_State && index < m_State->textureCount ? m_State->textures[index].resource.Get() : nullptr; }
    ImportDecodedImageInfo RendererSceneResourcesNvrhi::TextureInfo(uint32_t index) const noexcept
    { return m_State && index < m_State->textureCount ? m_State->textures[index].info : ImportDecodedImageInfo{}; }
    size_t RendererSceneResourcesNvrhi::StorageBytes() const noexcept { return m_State ? m_State->storageBytes : 0; }
    uint64_t RendererSceneResourcesNvrhi::BufferBytes() const noexcept { return m_State ? m_State->bufferBytes : 0; }
    uint64_t RendererSceneResourcesNvrhi::TextureBytes() const noexcept { return m_State ? m_State->textureBytes : 0; }
    void RendererSceneResourcesNvrhi::Cancel() noexcept
    {
        if (!m_State) return;
        const auto phase = m_State->progress.phase;
        if (phase != Phase::Prepared && phase != Phase::Uploading && phase != Phase::Submitted) return;
        m_State->ReleaseBorrows();
        m_State->ReleaseLoadingResources();
        m_State->progress.phase = Phase::Canceled;
    }
    void RendererSceneResourcesNvrhi::Reset() noexcept { Destroy(m_State, 1); m_State = nullptr; }

#if defined(UVSR_BUILD_TESTING)
    void SetRendererUploadFailure(RendererUploadFailure failure, uint32_t ordinal) noexcept
    { testFailure = failure; testOrdinal = ordinal; testCount = 0; }
#endif
}
