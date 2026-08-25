#include "renderer_common_passes.h"
#include "renderer_shader_factory.h"

#include <nvrhi/d3d12.h>

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = 619u;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

namespace
{
    using Microsoft::WRL::ComPtr;

    class RecordingCallback final : public nvrhi::IMessageCallback
    {
    public:
        void message(
            nvrhi::MessageSeverity severity,
            const char* messageText) override
        {
            if (severity == nvrhi::MessageSeverity::Error ||
                severity == nvrhi::MessageSeverity::Fatal)
            {
                errors.emplace_back(messageText ? messageText : "");
            }
        }

        std::vector<std::string> errors;
    };

    [[noreturn]] void Fail(
        const char* message,
        const RecordingCallback* callback = nullptr)
    {
        std::cerr << "Renderer common passes test failed: " << message << '\n';
        if (callback)
        {
            for (const std::string& error : callback->errors)
                std::cerr << "NVRHI: " << error << '\n';
        }
        std::exit(EXIT_FAILURE);
    }

    void PrintD3d12Messages(ID3D12Device* device)
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
            return;
        const UINT64 messageCount =
            infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 index = 0u; index < messageCount; ++index)
        {
            SIZE_T messageSize = 0u;
            if (FAILED(infoQueue->GetMessage(index, nullptr, &messageSize)) ||
                messageSize == 0u)
            {
                continue;
            }
            std::vector<unsigned char> storage(messageSize);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(infoQueue->GetMessage(
                    index, message, &messageSize)))
            {
                std::cerr << "D3D12: " << message->pDescription << '\n';
            }
        }
    }

    void RequireSucceeded(HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            std::cerr << operation << " failed with HRESULT 0x"
                << std::hex << static_cast<unsigned long>(result) << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    nvrhi::d3d12::DeviceHandle CreateDevice(
        RecordingCallback& callback,
        ComPtr<ID3D12Device>& nativeDevice,
        ComPtr<ID3D12CommandQueue>& nativeQueue)
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            debug->EnableDebugLayer();
        RequireSucceeded(
            D3D12CreateDevice(
                nullptr,
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&nativeDevice)),
            "D3D12CreateDevice");

        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        RequireSucceeded(
            nativeDevice->CreateCommandQueue(
                &queueDescription,
                IID_PPV_ARGS(&nativeQueue)),
            "CreateCommandQueue");

        nvrhi::d3d12::DeviceDesc description;
        description.errorCB = &callback;
        description.pDevice = nativeDevice.Get();
        description.pGraphicsCommandQueue = nativeQueue.Get();
        return nvrhi::d3d12::createDevice(description);
    }

    void WaitForNativeQueue(
        ID3D12Device* device,
        ID3D12CommandQueue* queue)
    {
        ComPtr<ID3D12Fence> fence;
        RequireSucceeded(device->CreateFence(
            0u,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&fence)),
            "CreateFence");
        HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!eventHandle)
            Fail("CreateEventW failed");
        RequireSucceeded(queue->Signal(fence.Get(), 1u), "Queue::Signal");
        RequireSucceeded(
            fence->SetEventOnCompletion(1u, eventHandle),
            "SetEventOnCompletion");
        const DWORD waitResult = WaitForSingleObject(eventHandle, 30000u);
        CloseHandle(eventHandle);
        if (waitResult != WAIT_OBJECT_0)
            Fail("native D3D12 queue did not reach the test fence");
    }

    void TestFailureLatch()
    {
        uvsr::RendererBlitPipelineFailureLatch latch;
        if (!latch.CanAttempt() || latch.HasFailed())
            Fail("fresh pipeline latch was terminal");
        latch.RecordResult(true);
        if (!latch.CanAttempt() || latch.HasFailed())
            Fail("successful pipeline creation poisoned the latch");
        latch.RecordResult(false);
        if (latch.CanAttempt() || !latch.HasFailed())
            Fail("failed pipeline creation was not terminal");
        latch.RecordResult(true);
        if (latch.CanAttempt() || !latch.HasFailed())
            Fail("later success cleared a terminal pipeline failure");
    }

    void TestProductionBlitPipeline(
        const std::filesystem::path& packagedShaderDirectory)
    {
        RecordingCallback callback;
        ComPtr<ID3D12Device> nativeDevice;
        ComPtr<ID3D12CommandQueue> nativeQueue;
        nvrhi::d3d12::DeviceHandle device = CreateDevice(
            callback, nativeDevice, nativeQueue);
        if (!device)
            Fail("NVRHI D3D12 device creation failed", &callback);

        auto shaderFactory = std::make_shared<uvsr::RendererShaderFactory>(
            device, packagedShaderDirectory);
        auto commonPasses = std::make_shared<uvsr::RendererCommonPasses>(
            device, shaderFactory);
        if (!commonPasses->IsValid())
            Fail("common resources did not initialize", &callback);

        nvrhi::TextureDesc sourceDescription;
        sourceDescription.width = 16u;
        sourceDescription.height = 16u;
        sourceDescription.format = nvrhi::Format::RGBA8_UNORM;
        sourceDescription.initialState = nvrhi::ResourceStates::ShaderResource;
        sourceDescription.keepInitialState = true;
        sourceDescription.debugName = "Common passes test source";
        nvrhi::TextureHandle source = device->createTexture(sourceDescription);

        nvrhi::TextureDesc targetDescription;
        targetDescription.width = 16u;
        targetDescription.height = 16u;
        targetDescription.format = nvrhi::Format::SRGBA8_UNORM;
        targetDescription.isRenderTarget = true;
        targetDescription.initialState = nvrhi::ResourceStates::RenderTarget;
        targetDescription.keepInitialState = true;
        targetDescription.debugName = "Common passes test target";
        nvrhi::TextureHandle target = device->createTexture(targetDescription);
        nvrhi::FramebufferHandle framebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(target));
        nvrhi::StagingTextureHandle readback = device->createStagingTexture(
            targetDescription, nvrhi::CpuAccessMode::Read);
        nvrhi::CommandListHandle commandList = device->createCommandList();
        if (!source || !target || !framebuffer || !readback || !commandList)
            Fail("test render resources did not initialize", &callback);

        std::vector<std::uint32_t> redPixels(
            sourceDescription.width * sourceDescription.height,
            0xff0000ffu);
        commandList->open();
        commandList->writeTexture(
            source,
            0u,
            0u,
            redPixels.data(),
            sourceDescription.width * sizeof(std::uint32_t));
        if (!commonPasses->BlitTexture(
                commandList, framebuffer, source))
        {
            commandList->close();
            PrintD3d12Messages(nativeDevice.Get());
            Fail("production SRGBA8 blit pipeline did not dispatch", &callback);
        }
        commandList->copyTexture(
            readback,
            nvrhi::TextureSlice(),
            target,
            nvrhi::TextureSlice());
        commandList->close();
        device->executeCommandList(commandList);
        WaitForNativeQueue(nativeDevice.Get(), nativeQueue.Get());
        if (!callback.errors.empty())
            Fail("production blit emitted an error", &callback);

        std::size_t rowPitch = 0u;
        const void* mapped = device->mapStagingTexture(
            readback,
            nvrhi::TextureSlice(),
            nvrhi::CpuAccessMode::Read,
            &rowPitch);
        if (!mapped || rowPitch < sizeof(std::uint32_t))
            Fail("blit output could not be read back", &callback);
        const std::uint32_t pixel =
            *static_cast<const std::uint32_t*>(mapped);
        device->unmapStagingTexture(readback);
        if (pixel != 0xff0000ffu)
            Fail("blit changed the endpoint red sample", &callback);

        nvrhi::TextureDesc resolveSourceDescription;
        resolveSourceDescription.width = 2u;
        resolveSourceDescription.height = 2u;
        resolveSourceDescription.format = nvrhi::Format::RGBA8_UNORM;
        resolveSourceDescription.initialState =
            nvrhi::ResourceStates::ShaderResource;
        resolveSourceDescription.keepInitialState = true;
        resolveSourceDescription.debugName =
            "Common passes exact 2x2 resolve source";
        nvrhi::TextureHandle resolveSource =
            device->createTexture(resolveSourceDescription);

        nvrhi::TextureDesc resolveTargetDescription;
        resolveTargetDescription.width = 1u;
        resolveTargetDescription.height = 1u;
        resolveTargetDescription.format = nvrhi::Format::RGBA8_UNORM;
        resolveTargetDescription.isRenderTarget = true;
        resolveTargetDescription.initialState =
            nvrhi::ResourceStates::RenderTarget;
        resolveTargetDescription.keepInitialState = true;
        resolveTargetDescription.debugName =
            "Common passes exact 2x2 resolve target";
        nvrhi::TextureHandle resolveTarget =
            device->createTexture(resolveTargetDescription);
        nvrhi::FramebufferHandle resolveFramebuffer =
            device->createFramebuffer(
                nvrhi::FramebufferDesc().addColorAttachment(resolveTarget));
        nvrhi::StagingTextureHandle resolveReadback =
            device->createStagingTexture(
                resolveTargetDescription,
                nvrhi::CpuAccessMode::Read);
        nvrhi::CommandListHandle resolveCommandList =
            device->createCommandList();
        if (!resolveSource || !resolveTarget || !resolveFramebuffer ||
            !resolveReadback || !resolveCommandList)
        {
            Fail("2x2 resolve resources did not initialize", &callback);
        }

        constexpr std::uint32_t ResolvePixels[] = {
            0xff000000u,
            0xffffffffu,
            0xff000000u,
            0xffffffffu
        };
        resolveCommandList->open();
        resolveCommandList->writeTexture(
            resolveSource,
            0u,
            0u,
            ResolvePixels,
            2u * sizeof(std::uint32_t));
        if (!commonPasses->BlitTexture(
                resolveCommandList,
                resolveFramebuffer,
                resolveSource))
        {
            resolveCommandList->close();
            PrintD3d12Messages(nativeDevice.Get());
            Fail("exact 2x2 scene-linear resolve did not dispatch", &callback);
        }
        resolveCommandList->copyTexture(
            resolveReadback,
            nvrhi::TextureSlice(),
            resolveTarget,
            nvrhi::TextureSlice());
        resolveCommandList->close();
        device->executeCommandList(resolveCommandList);
        WaitForNativeQueue(nativeDevice.Get(), nativeQueue.Get());
        if (!callback.errors.empty())
            Fail("exact 2x2 resolve emitted an error", &callback);

        rowPitch = 0u;
        mapped = device->mapStagingTexture(
            resolveReadback,
            nvrhi::TextureSlice(),
            nvrhi::CpuAccessMode::Read,
            &rowPitch);
        if (!mapped || rowPitch < sizeof(std::uint32_t))
            Fail("exact 2x2 resolve could not be read back", &callback);
        const std::uint32_t resolvedPixel =
            *static_cast<const std::uint32_t*>(mapped);
        device->unmapStagingTexture(resolveReadback);
        if (resolvedPixel != 0xff808080u)
            Fail("2x2 linear resolve did not average all four pixels", &callback);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
        Fail("expected the packaged shader directory argument");
    TestFailureLatch();
    TestProductionBlitPipeline(std::filesystem::u8path(argv[1]));
    return EXIT_SUCCESS;
}
