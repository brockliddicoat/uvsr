#include "renderer_scene_retirement_nvrhi.h"

#include "renderer_scene_descriptors_nvrhi.h"
#include <directx/d3d12.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            fprintf(stderr, "scene retirement: %s\n", message);
            exit(1);
        }
    }
}

void TestRendererSceneRetirement(
    nvrhi::IDevice* device, ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue)
{
    using Status = uvsr::RendererSceneRetirementStatus;
    uvsr::RendererSceneRetirementNvrhi invalid(nullptr);
    Require(!invalid.IsValid() && !invalid.Begin(), "null device armed retirement");
    uvsr::RendererSceneRetirementNvrhi retirement(device);
    nvrhi::BindlessLayoutDesc description;
    description.visibility = nvrhi::ShaderType::Compute;
    description.maxCapacity = 64;
    description.registerSpaces = { nvrhi::BindingLayoutItem::RawBuffer_SRV(1) };
    auto layout = device->createBindlessLayout(description);
    Require(layout, "bindless layout creation failed");
    uvsr::RendererSceneDescriptorsNvrhi descriptors(device, layout);
    nvrhi::BufferDesc bufferDescription;
    bufferDescription.byteSize = 16;
    bufferDescription.canHaveRawViews = true;
    bufferDescription.initialState = nvrhi::ResourceStates::ShaderResource;
    bufferDescription.keepInitialState = true;
    auto first = device->createBuffer(bufferDescription);
    auto second = device->createBuffer(bufferDescription);
    auto third = device->createBuffer(bufferDescription);
    Require(first && second && third, "source buffers were not created");
    nvrhi::BufferDesc readbackDescription;
    readbackDescription.byteSize = 16;
    readbackDescription.cpuAccess = nvrhi::CpuAccessMode::Read;
    auto readback = device->createBuffer(readbackDescription);
    auto commands = device->createCommandList();
    Require(readback && commands, "readback or command creation failed");

    for (uint32_t cycle = 0; cycle < 3; ++cycle)
    {
        const auto oldIndex = descriptors.CreateDescriptor(nvrhi::BindingSetItem::RawBuffer_SRV(0, first));
        const uint32_t capacity = descriptors.GetDescriptorTable()->getCapacity();
        Require(oldIndex >= 0 && capacity == 64, "initial descriptor capacity changed");
        ID3D12Fence* gate = nullptr;
        Require(SUCCEEDED(nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))),
            "native queue gate creation failed");
        Require(SUCCEEDED(nativeQueue->Wait(gate, 1)), "native queue wait failed");
        // never leave a deliberately blocked queue behind a fixture failure.
        const auto requireWhileBlocked = [gate](bool condition, const char* message)
        {
            if (!condition)
            {
                (void)gate->Signal(1);
                Require(false, message);
            }
        };
        const uint32_t expected[4] = { 117 + cycle, 23, 31, 43 };
        commands->open();
        commands->writeBuffer(first, expected, sizeof(expected));
        commands->copyBuffer(readback, 0, first, 0, sizeof(expected));
        commands->close();
        requireWhileBlocked(device->executeCommandList(commands) != 0, "upload submission failed");
        requireWhileBlocked(retirement.Begin() && retirement.Poll() == Status::Pending &&
            retirement.Poll() == Status::Pending && !retirement.Consume(),
            "scene became reusable before native completion");
        const auto concurrentIndex = descriptors.CreateDescriptor(nvrhi::BindingSetItem::RawBuffer_SRV(0, second));
        requireWhileBlocked(concurrentIndex >= 0 && concurrentIndex != oldIndex &&
            descriptors.GetDescriptorTable()->getCapacity() == capacity,
            "in-flight descriptor slot was reused or table grew");
        requireWhileBlocked(SUCCEEDED(gate->Signal(1)), "native queue release failed");
        gate->Release();

        const uint64_t deadline = GetTickCount64() + 3000;
        Status status;
        do
        {
            status = retirement.Poll();
            if (status != Status::Pending)
                break;
            Sleep(1);
        } while (GetTickCount64() < deadline);
        Require(status == Status::Ready && !retirement.UsedBlockingFallback(),
            "native completion did not satisfy the asynchronous retirement query");
        const auto* output = static_cast<const uint32_t*>(device->mapBuffer(readback, nvrhi::CpuAccessMode::Read));
        Require(output, "completed upload did not map");
        bool matches = true;
        for (unsigned index = 0; index < 4; ++index)
            matches = matches && output[index] == expected[index];
        device->unmapBuffer(readback);
        Require(matches, "completed copy changed uploaded bytes");

        // this is the actual scene-owner rule: release mutable slots only after
        // Ready, then consume the gate. no second native lifetime tracker exists.
        Require(descriptors.ReleaseDescriptor(oldIndex), "descriptor release failed");
        Require(retirement.Consume(), "ready retirement was not consumed");
        const auto reusedIndex = descriptors.CreateDescriptor(nvrhi::BindingSetItem::RawBuffer_SRV(0, third));
        Require(reusedIndex == oldIndex && descriptors.GetDescriptorTable()->getCapacity() == capacity,
            "completed descriptor slot was not reusable without growth");
        Require(descriptors.ReleaseDescriptor(concurrentIndex), "descriptor release failed");
        Require(descriptors.ReleaseDescriptor(reusedIndex), "descriptor release failed");
        device->runGarbageCollection();
    }
    Require(descriptors.GetLiveCount() == 0 && descriptors.GetPeakLiveCount() == 2 &&
        descriptors.GetPeakCapacity() == 64, "retirement live or peak accounting changed");
    bufferDescription.byteSize = 65 * 16;
    auto ranges = device->createBuffer(bufferDescription);
    Require(bool(ranges), "exhaustion source creation failed");
    int32_t indices[64];
    for (unsigned index = 0; index < 64; ++index)
    {
        indices[index] = descriptors.CreateDescriptor(nvrhi::BindingSetItem::RawBuffer_SRV(
            0, ranges, nvrhi::BufferRange(index * 16, 16)));
        Require(indices[index] >= 0, "descriptor table exhausted before its declared limit");
    }
    Require(descriptors.GetLiveCount() == 64 && descriptors.GetPeakLiveCount() == 64 &&
        descriptors.CreateDescriptor(nvrhi::BindingSetItem::RawBuffer_SRV(
            0, ranges, nvrhi::BufferRange(64 * 16, 16))) < 0 &&
        descriptors.GetLiveCount() == 64 && descriptors.GetDescriptorTable()->getCapacity() == 64,
        "descriptor exhaustion mutated the table or exceeded its declared limit");
    Require(descriptors.ReleaseDescriptor(indices[17]), "descriptor release failed");
    const auto reused = descriptors.CreateDescriptor(nvrhi::BindingSetItem::RawBuffer_SRV(
        0, ranges, nvrhi::BufferRange(64 * 16, 16)));
    Require(reused == indices[17] && descriptors.GetLiveCount() == 64, "exhausted table did not reuse a released slot");
    for (const auto index : indices) Require(descriptors.ReleaseDescriptor(index), "descriptor release failed");
    Require(descriptors.GetLiveCount() == 0, "descriptor table did not drain");
    printf("scene retirement: 3 native-gated cycles, maximum2 live slots; capacity64 exhaustion, unchanged failure and reuse passed\n");
    // the copy proves native completion, not dynamic bindless shader contents.
}
