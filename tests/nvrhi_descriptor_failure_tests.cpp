#include "../cmake/RequireNoCppExceptions.h"

// this object replaces only the heap object from the static library. the engine
// compiles the same source without these failure branches or test state.
#define UVSR_NVRHI_DESCRIPTOR_TEST_FAILURES 1
#include "d3d12-descriptor-heap.cpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using Heap = nvrhi::d3d12::StaticDescriptorHeap;
    using HeapType = nvrhi::d3d12::DescriptorHeapType;
    const Heap* allocationFailureHeap = nullptr;
    const Heap* creationFailureHeap = nullptr;
    unsigned allocationsBeforeFailure = 0;
    unsigned creationsBeforeFailure = 0;

    void Require(bool valid, const char* message)
    {
        if (valid) return;
        fprintf(stderr, "descriptor failure check: %s\n", message);
        exit(1);
    }

    class Callback final : public nvrhi::IMessageCallback
    {
    public:
        const char* expected = nullptr;
        void message(nvrhi::MessageSeverity severity, const char* text) override
        {
            if (severity != nvrhi::MessageSeverity::Error && severity != nvrhi::MessageSeverity::Fatal) return;
            if (expected && text && strcmp(expected, text) == 0) { expected = nullptr; return; }
            fprintf(stderr, "unexpected NVRHI error: %s\n", text ? text : "");
            exit(1);
        }
        void ExpectGrowthFailure() { expected = "Failed to grow a descriptor heap!"; }
        void Consumed() { Require(!expected && !allocationFailureHeap && !creationFailureHeap, "failure was not reached"); }
    };

    Heap* GetHeap(nvrhi::d3d12::IDevice* device, HeapType type)
    {
        auto* heap = static_cast<Heap*>(device->getDescriptorHeap(type));
        Require(heap && heap->getHeap(), "heap creation");
        return heap;
    }

    void RequireEmpty(Heap* heap)
    {
        const auto count = heap->getHeap()->GetDesc().NumDescriptors;
        auto* before = heap->getHeap();
        Require(heap->allocateDescriptors(count) == 0 && heap->getHeap() == before, "cleanup lost or aliased capacity");
        heap->releaseDescriptors(0, count);
    }
}

namespace nvrhi::d3d12
{
    bool UvsrFailDescriptorAllocation(const StaticDescriptorHeap* heap)
    {
        if (allocationFailureHeap != heap) return false;
        if (allocationsBeforeFailure) { --allocationsBeforeFailure; return false; }
        allocationFailureHeap = nullptr;
        return true;
    }

    bool UvsrFailDescriptorHeapCreation(const StaticDescriptorHeap* heap)
    {
        if (creationFailureHeap != heap) return false;
        if (creationsBeforeFailure) { --creationsBeforeFailure; return false; }
        creationFailureHeap = nullptr;
        return true;
    }
}

void FailNextNvrhiRtvAllocation(nvrhi::IDevice* device)
{
    auto* native = static_cast<nvrhi::d3d12::IDevice*>(device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device).pointer);
    Require(native && !allocationFailureHeap, "RTV injection device");
    allocationFailureHeap = GetHeap(native, HeapType::RenderTargetView);
    allocationsBeforeFailure = 0;
}

void TestNvrhiDescriptorFailures(ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue)
{
    Callback callback;
    nvrhi::d3d12::DeviceDesc desc;
    desc.errorCB = &callback;
    desc.pDevice = nativeDevice;
    desc.pGraphicsCommandQueue = nativeQueue;
    desc.renderTargetViewHeapSize = 4;
    desc.depthStencilViewHeapSize = 4;
    desc.samplerHeapSize = 4;
    desc.shaderResourceViewHeapSize = 16;
    auto device = nvrhi::d3d12::createDevice(desc);
    Require(bool(device), "device creation");
    auto* samplers = GetHeap(device, HeapType::Sampler);
    auto* resources = GetHeap(device, HeapType::ShaderResourceView);
    auto* colors = GetHeap(device, HeapType::RenderTargetView);
    auto* depths = GetHeap(device, HeapType::DepthStencilView);

    // both native creation stages fail after the old range is fully occupied.
    // an unrelated live range must keep the same handles and remain writable.
    Require(samplers->allocateDescriptors(4) == 0, "initial sampler range");
    D3D12_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    for (unsigned index = 0; index < 4; ++index)
        nativeDevice->CreateSampler(&samplerDesc, samplers->getCpuHandle(index));
    samplers->copyToShaderVisibleHeap(0, 4);
    auto* oldCpu = samplers->getHeap();
    auto* oldGpu = samplers->getShaderVisibleHeap();
    const auto oldCpuHandle = samplers->getCpuHandle(0);
    const auto oldGpuHandle = samplers->getGpuHandle(0);
    for (unsigned stage = 0; stage < 2; ++stage)
    {
        creationFailureHeap = samplers; creationsBeforeFailure = stage;
        callback.ExpectGrowthFailure();
        Require(samplers->allocateDescriptor() == nvrhi::d3d12::c_InvalidDescriptorIndex, "native creation failure result");
        callback.Consumed();
        Require(samplers->getHeap() == oldCpu && samplers->getShaderVisibleHeap() == oldGpu &&
            samplers->getHeap()->GetDesc().NumDescriptors == 4 &&
            samplers->getCpuHandle(0).ptr == oldCpuHandle.ptr && samplers->getGpuHandle(0).ptr == oldGpuHandle.ptr,
            "native creation failure changed the live heap");
        nativeDevice->CreateSampler(&samplerDesc, samplers->getCpuHandle(3));
        samplers->copyToShaderVisibleHeap(3);
    }
    const auto grown = samplers->allocateDescriptor();
    Require(grown == 4 && samplers->getHeap()->GetDesc().NumDescriptors == 8, "growth retry");
    samplers->releaseDescriptor(grown);
    callback.expected = "Invalid descriptor release range";
    samplers->releaseDescriptor(nvrhi::d3d12::c_InvalidDescriptorIndex);
    callback.Consumed();
    callback.expected = "Invalid descriptor release range";
    samplers->releaseDescriptors(3, 6);
    callback.Consumed();
    samplers->releaseDescriptors(0, 4); RequireEmpty(samplers);

    nvrhi::TextureDesc textureDesc;
    textureDesc.width = 4; textureDesc.height = 4;
    textureDesc.format = nvrhi::Format::RGBA8_UNORM;
    textureDesc.isRenderTarget = true;
    auto color = device->createTexture(textureDesc);
    textureDesc.format = nvrhi::Format::D32;
    auto depth = device->createTexture(textureDesc);
    Require(color && depth, "framebuffer textures");
    nvrhi::FramebufferDesc framebuffer;
    framebuffer.addColorAttachment(color).addColorAttachment(color).setDepthAttachment(depth);
    for (unsigned stage = 0; stage < 3; ++stage)
    {
        allocationFailureHeap = stage == 2 ? depths : colors;
        allocationsBeforeFailure = stage == 1 ? 1 : 0;
        callback.ExpectGrowthFailure();
        Require(!device->createFramebuffer(framebuffer), "partial framebuffer must fail");
        callback.Consumed();
        RequireEmpty(colors); RequireEmpty(depths);
        auto retry = device->createFramebuffer(framebuffer);
        Require(bool(retry), "framebuffer retry");
        retry = nullptr;
        RequireEmpty(colors); RequireEmpty(depths);
    }

    nvrhi::BindlessLayoutDesc bindless;
    bindless.visibility = nvrhi::ShaderType::Pixel;
    bindless.maxCapacity = 16;
    bindless.addRegisterSpace(nvrhi::BindingLayoutItem::Texture_SRV(0));
    auto layout = device->createBindlessLayout(bindless);
    Require(bool(layout), "bindless layout");
    for (unsigned mode = 0; mode < 2; ++mode)
    {
        auto table = device->createDescriptorTable(layout);
        Require(bool(table), "descriptor table");
        device->resizeDescriptorTable(table, 2, false);
        Require(table->getCapacity() == 2, "initial table capacity");
        for (unsigned slot = 0; slot < 2; ++slot)
            Require(device->writeDescriptorTable(table, nvrhi::BindingSetItem::Texture_SRV(slot, color)), "initial table write");
        const auto first = table->getFirstDescriptorIndexInHeap();
        const auto firstCpu = resources->getCpuHandle(first);
        const auto firstGpu = resources->getGpuHandle(first);
        allocationFailureHeap = resources; allocationsBeforeFailure = 0;
        callback.ExpectGrowthFailure();
        device->resizeDescriptorTable(table, 4, mode != 0);
        callback.Consumed();
        Require(table->getCapacity() == 2 && table->getFirstDescriptorIndexInHeap() == first &&
            resources->getCpuHandle(first).ptr == firstCpu.ptr && resources->getGpuHandle(first).ptr == firstGpu.ptr,
            "failed table growth discarded the old range");
        Require(device->writeDescriptorTable(table, nvrhi::BindingSetItem::Texture_SRV(1, color)), "old table write after failure");
        device->resizeDescriptorTable(table, 4, mode != 0);
        Require(table->getCapacity() == 4, "table growth retry");
        table = nullptr; RequireEmpty(resources);
    }
    textureDesc.format = nvrhi::Format::RGBA8_UNORM;
    textureDesc.isRenderTarget = false; textureDesc.isUAV = true; textureDesc.mipLevels = 3;
    auto unusedClearMips = device->createTexture(textureDesc);
    Require(bool(unusedClearMips), "unused clear mip texture");
    unusedClearMips = nullptr; RequireEmpty(resources);
    printf("2 native heap creation failures, 3 framebuffer failures, 2 bindless growth failures and sparse clear ownership recover\n");
}
