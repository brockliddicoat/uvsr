#include "renderer_scene_descriptors_nvrhi.h"
#include "pbr_binding_sets_nvrhi.h"
#include "renderer_shader_factory_nvrhi.h"
#include "renderer_pixel_readback_cb.h"
#include "../cmake/RequireNoCppExceptions.h"
#include <directx/d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;
    void Require(bool value, const char* message)
    {
        if (value) return;
        fprintf(stderr, "binding owner: %s\n", message);
        exit(1);
    }

    void Descriptors(nvrhi::IDevice* device)
    {
        nvrhi::BindlessLayoutDesc desc;
        desc.maxCapacity = 130;
        desc.visibility = nvrhi::ShaderType::Compute;
        desc.registerSpaces = {nvrhi::BindingLayoutItem::RawBuffer_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2)};
        auto layout = device->createBindlessLayout(desc);
        Require(layout, "scene descriptor layout");
        RendererSceneDescriptorsNvrhi invalid(nullptr, layout);
        Require(!invalid.IsValid() && invalid.Error() == RendererDescriptorError::Input, "null device accepted");
        SetRendererDescriptorFailure(RendererDescriptorFailure::Create);
        RendererSceneDescriptorsNvrhi failed(device, layout);
        Require(!failed.IsValid() && failed.Error() == RendererDescriptorError::Gpu, "table creation failure published");
        RendererSceneDescriptorsNvrhi table(device, layout);
        Require(table.IsValid(), "table creation retry");
        auto* identity = table.GetDescriptorTable();
        nvrhi::BufferDesc bufferDesc;
        bufferDesc.byteSize = 131 * 16;
        bufferDesc.canHaveRawViews = true;
        bufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        bufferDesc.keepInitialState = true;
        auto buffer = device->createBuffer(bufferDesc);
        Require(buffer, "descriptor source");
        auto item = [&](unsigned i) { return nvrhi::BindingSetItem::RawBuffer_SRV(0, buffer, nvrhi::BufferRange(i * 16, 16)); };
        Require(table.CreateDescriptor(nvrhi::BindingSetItem::None(0)) < 0 && !table.GetLiveCount(), "empty descriptor accepted");
        SetRendererDescriptorFailure(RendererDescriptorFailure::Allocation);
        Require(table.CreateDescriptor(item(0)) < 0 && table.Error() == RendererDescriptorError::Allocation &&
            table.GetDescriptorTable()->getCapacity() == 0 && !table.GetLiveCount(), "initial allocation failure mutated table");
        const int32_t first = table.CreateDescriptor(item(0));
        Require(first == 0 && table.GetLiveCount() == 1, "allocation retry");
        const unsigned referenceCount = buffer->GetRefCount();
        auto alias = item(0); alias.slot = 777;
        Require(table.CreateDescriptor(alias) == first && table.GetLiveCount() == 1 && buffer->GetRefCount() == referenceCount,
            "duplicate descriptor gained ownership or changed its slot");
        alias.arrayElement = 1;
        Require(table.CreateDescriptor(alias) < 0 && table.Error() == RendererDescriptorError::Input, "bindless array element accepted");
        for (unsigned index = 1; index < 64; ++index)
            Require(table.CreateDescriptor(item(index)) == int32_t(index), "descriptor ordering");
        const size_t storage = table.StorageBytes();
        const RendererDescriptorFailure growthFailures[]{RendererDescriptorFailure::Allocation, RendererDescriptorFailure::Resize};
        for (const auto operation : growthFailures)
        {
            SetRendererDescriptorFailure(operation);
            Require(table.CreateDescriptor(item(64)) < 0 && table.GetLiveCount() == 64 && table.StorageBytes() == storage &&
                table.GetDescriptorTable() == identity && identity->getCapacity() == 64, "failed growth changed owner");
            for (unsigned i = 0; i < 64; ++i) Require(table.CreateDescriptor(item(i)) == int32_t(i), "failed growth lost a view");
        }
        SetRendererDescriptorFailure(RendererDescriptorFailure::Write);
        Require(table.CreateDescriptor(item(64)) < 0 && table.GetLiveCount() == 64 && identity->getCapacity() == 128,
            "failed write published a slot");
        Require(table.CreateDescriptor(item(64)) == 64, "write retry");
        for (unsigned index = 65; index < 130; ++index)
            Require(table.CreateDescriptor(item(index)) == int32_t(index), "later descriptor ordering");
        Require(identity == table.GetDescriptorTable() && identity->getCapacity() == 130 && table.GetPeakLiveCount() == 130,
            "bounded non-power-of-two growth");
        for (unsigned index = 0; index < 130; ++index)
        {
            auto expected = item(index); expected.slot = index;
            Require(table.GetDescriptor(int32_t(index)) == expected && table.CreateDescriptor(item(index)) == int32_t(index),
                "growth changed descriptor identity");
        }
        Require(table.CreateDescriptor(item(130)) < 0 && table.Error() == RendererDescriptorError::Capacity &&
            table.GetLiveCount() == 130, "capacity exhaustion published");
        SetRendererDescriptorFailure(RendererDescriptorFailure::Write);
        Require(!table.ReleaseDescriptor(17) && table.Error() == RendererDescriptorError::Gpu && table.GetLiveCount() == 130 &&
            table.CreateDescriptor(item(17)) == 17, "failed release lost ownership");
        Require(table.ReleaseDescriptor(17) && table.CreateDescriptor(item(130)) == 17, "release retry and reuse");
        for (int32_t index = 129; index >= 0; --index) Require(table.ReleaseDescriptor(index), "table drain");
        Require(!table.GetLiveCount() && !table.ReleaseDescriptor(17) && table.Error() == RendererDescriptorError::Input,
            "released slot remained live");

        nvrhi::TextureDesc textureDesc;
        textureDesc.width = textureDesc.height = 4;
        textureDesc.mipLevels = 2;
        textureDesc.format = nvrhi::Format::RGBA8_UNORM;
        textureDesc.isTypeless = true;
        textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        textureDesc.keepInitialState = true;
        auto texture = device->createTexture(textureDesc);
        Require(texture, "descriptor texture");
        const auto base = nvrhi::BindingSetItem::Texture_SRV(0, texture, nvrhi::Format::RGBA8_UNORM, nvrhi::TextureSubresourceSet(0,1,0,1));
        const int32_t baseIndex = table.CreateDescriptor(base);
        const int32_t mipIndex = table.CreateDescriptor(nvrhi::BindingSetItem::Texture_SRV(0, texture,
            nvrhi::Format::RGBA8_UNORM, nvrhi::TextureSubresourceSet(1,1,0,1)));
        const int32_t formatIndex = table.CreateDescriptor(nvrhi::BindingSetItem::Texture_SRV(0, texture,
            nvrhi::Format::SRGBA8_UNORM, nvrhi::TextureSubresourceSet(0,1,0,1)));
        Require(baseIndex == 0 && mipIndex == 1 && formatIndex == 2, "texture format or mip aliased");
        for (unsigned i = 0; i < 2; ++i)
        {
            auto changed = base;
            if (i == 0) changed.type = nvrhi::ResourceType::RawBuffer_SRV;
            else changed.dimension = nvrhi::TextureDimension::Texture3D;
            SetRendererDescriptorFailure(RendererDescriptorFailure::Write);
            Require(table.CreateDescriptor(changed) < 0 && table.Error() == RendererDescriptorError::Gpu && table.GetLiveCount() == 3,
                "different descriptor identity reused a slot before the injected write");
        }
        for (int32_t index = 0; index < 3; ++index) Require(table.ReleaseDescriptor(index), "texture descriptor drain");
        printf("scene descriptor owner: 130 ranges, three texture views, deduplicated borrows, bounded growth and seven fault retries passed; owner storage %zu bytes\n", table.StorageBytes());
    }

    void BindingSets(nvrhi::IDevice* device, ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue,
        RendererShaderFactory& shaders)
    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, 16),
            nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::TypedBuffer_UAV(0)};
        auto layout = device->createBindingLayout(layoutDesc);
        auto secondLayout = device->createBindingLayout(layoutDesc);
        auto shader = shaders.CreateShader("uvsr/renderer_pixel_readback_cs.hlsl", "main", {}, nvrhi::ShaderType::Compute);
        Require(layout && secondLayout && layout != secondLayout && shader, "binding fixture layout or shader");
        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = shader; pipelineDesc.bindingLayouts = {layout};
        auto pipeline = device->createComputePipeline(pipelineDesc);
        nvrhi::TextureDesc textureDesc;
        textureDesc.width = textureDesc.height = 2; textureDesc.mipLevels = 2;
        textureDesc.format = nvrhi::Format::RGBA32_UINT;
        textureDesc.initialState = nvrhi::ResourceStates::ShaderResource; textureDesc.keepInitialState = true;
        auto texture = device->createTexture(textureDesc);
        nvrhi::BufferDesc bufferDesc;
        bufferDesc.byteSize = 16; bufferDesc.canHaveTypedViews = true; bufferDesc.canHaveUAVs = true;
        bufferDesc.format = nvrhi::Format::RGBA32_UINT;
        bufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess; bufferDesc.keepInitialState = true;
        auto output = device->createBuffer(bufferDesc);
        nvrhi::BufferDesc readbackDesc; readbackDesc.byteSize = 16; readbackDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        auto readback = device->createBuffer(readbackDesc);
        Require(pipeline && texture && output && readback, "binding fixture resources");
        const uint32_t pixels[16] = {123,77,92,501, 23,25,26,27, 61,62,63,64, 81,82,83,84};
        const uint32_t mip[4] = {999,1234,591,123456};
        auto commands = device->createCommandList(); Require(commands, "binding fixture commands");
        commands->open();
        commands->writeTexture(texture, 0, 0, pixels, 32);
        commands->writeTexture(texture, 0, 1, mip, 16);
        commands->close(); Require(device->executeCommandList(commands) != 0 && device->waitForIdle(), "binding fixture upload");
        nvrhi::BindingSetDesc desc;
        desc.bindings = {nvrhi::BindingSetItem::PushConstants(0,16),
            nvrhi::BindingSetItem::Texture_SRV(0, texture, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(0,1,0,1)),
            nvrhi::BindingSetItem::TypedBuffer_UAV(0, output)};
        PbrBindingSetsNvrhi cache(device);
        auto first = cache.GetOrCreateBindingSet(desc, layout);
        Require(first && cache.GetSize() == 1, "first binding set");
        SetPbrBindingFailure(PbrBindingFailure::Allocation);
        for (unsigned index = 0; index < 1000; ++index)
            Require(cache.GetOrCreateBindingSet(desc, layout) == first && cache.GetSize() == 1, "warmed binding hit allocated");
        Require(!cache.GetOrCreateBindingSet(desc, secondLayout) && cache.GetSize() == 1, "layout identity or allocation failure");
        Require(cache.GetOrCreateBindingSet(desc, secondLayout) != first && cache.GetSize() == 2, "layout retry");
        auto changed = desc;
        changed.bindings[1].subresources.baseMipLevel = 1;
        SetPbrBindingFailure(PbrBindingFailure::Create);
        Require(!cache.GetOrCreateBindingSet(changed, layout) && cache.GetSize() == 2 && cache.GetOrCreateBindingSet(desc, layout) == first,
            "failed binding creation changed the cache");
        auto second = cache.GetOrCreateBindingSet(changed, layout);
        Require(second && second != first && cache.GetSize() == 3, "mip view retry");
        changed = desc; changed.trackLiveness = false;
        Require(!cache.GetOrCreateBindingSet(changed, layout) && cache.GetSize() == 3, "untracked binding accepted");
        for (unsigned index = 0; index < 3; ++index)
        {
            changed = desc;
            if (index == 0) changed.bindings[1].arrayElement = 1;
            if (index == 1) changed.bindings[1].format = nvrhi::Format::RGBA32_FLOAT;
            if (index == 2) changed.bindings[1].dimension = nvrhi::TextureDimension::Texture3D;
            SetPbrBindingFailure(PbrBindingFailure::Create);
            Require(!cache.GetOrCreateBindingSet(changed, layout) && cache.GetSize() == 3,
                "different array/format/dimension reused the prior binding");
        }
        for (unsigned index = 0; index < 2; ++index)
        {
            commands->open();
            nvrhi::ComputeState state; state.pipeline = pipeline; state.bindings = {index == 0 ? first : second};
            commands->setComputeState(state);
            const RendererPixelReadbackConstants constants{};
            commands->setPushConstants(&constants, sizeof(constants)); commands->dispatch(1);
            commands->copyBuffer(readback, 0, output, 0, 16);
            commands->close(); Require(device->executeCommandList(commands) != 0 && device->waitForIdle(), "binding dispatch");
            const void* data = device->mapBuffer(readback, nvrhi::CpuAccessMode::Read);
            Require(data && !memcmp(data, index == 0 ? pixels : mip, 16), "cached view GPU values");
            device->unmapBuffer(readback);
        }
        ID3D12Fence* gate = nullptr;
        Require(SUCCEEDED(nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))), "binding lifetime gate");
        Require(SUCCEEDED(nativeQueue->Wait(gate,1)), "binding lifetime wait");
        commands->open();
        nvrhi::ComputeState state; state.pipeline = pipeline; state.bindings = {first};
        commands->setComputeState(state);
        const RendererPixelReadbackConstants constants{};
        commands->setPushConstants(&constants, sizeof(constants)); commands->dispatch(1);
        commands->close();
        const uint64_t submitted = device->executeCommandList(commands);
        auto* probe = first.Get(); probe->AddRef();
        first = nullptr; second = nullptr; state.bindings = {}; cache.Clear();
        const bool held = submitted && probe->GetRefCount() > 1 && gate->GetCompletedValue() == 0;
        const HRESULT released = gate->Signal(1); gate->Release();
        Require(SUCCEEDED(released) && device->waitForIdle(), "binding lifetime release");
        commands = nullptr; device->runGarbageCollection();
        const bool retired = probe->GetRefCount() == 1;
        const unsigned remaining = probe->Release();
        Require(held && retired && !remaining && !cache.GetSize() && cache.GetPeakSize() == 3, "binding references retired before completion or remained stale");
        printf("PBR binding owner: layout, mip, array, format and dimension identity; 1000 allocation-free hits; five failed misses; two exact GPU readbacks; pending lifetime passed\n");
    }
}

void TestRendererBindingOwners(nvrhi::IDevice* device, ID3D12Device* nativeDevice,
    ID3D12CommandQueue* nativeQueue, uvsr::RendererShaderFactory& shaders)
{
    Descriptors(device);
    BindingSets(device, nativeDevice, nativeQueue, shaders);
}
