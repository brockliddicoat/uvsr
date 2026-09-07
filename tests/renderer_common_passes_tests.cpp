#include "renderer_common_passes.h"
#include "renderer_shader_factory.h"

#include <nvrhi/d3d12.h>

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
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
        void message(nvrhi::MessageSeverity severity, const char* text) override
        {
            if (severity == nvrhi::MessageSeverity::Error || severity == nvrhi::MessageSeverity::Fatal)
            {
                failed = true;
                std::cerr << "NVRHI: " << (text ? text : "") << '\n';
            }
        }
        bool failed = false;
    };

    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "Renderer common passes test failed: " << message << '\n';
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

    void TestProductionBlitPipeline(const std::filesystem::path& packagedShaderDirectory)
    {
        RecordingCallback callback;
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            debug->EnableDebugLayer();
        ComPtr<ID3D12Device> nativeDevice;
        RequireSucceeded(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&nativeDevice)),
            "D3D12CreateDevice");
        ComPtr<ID3D12CommandQueue> nativeQueue;
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        RequireSucceeded(nativeDevice->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&nativeQueue)),
            "CreateCommandQueue");
        nvrhi::d3d12::DeviceDesc description;
        description.errorCB = &callback;
        description.pDevice = nativeDevice.Get();
        description.pGraphicsCommandQueue = nativeQueue.Get();
        auto device = nvrhi::d3d12::createDevice(description);
        if (!device)
            Fail("NVRHI D3D12 device creation failed");
        auto factory = std::make_shared<uvsr::RendererShaderFactory>(device, packagedShaderDirectory);
        uvsr::RendererCommonPasses commonPasses(device, factory);
        if (!commonPasses.IsValid())
            Fail("common resources did not initialize");

        ComPtr<ID3D12Fence> fence;
        RequireSucceeded(nativeDevice->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
        HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!completed)
            Fail("CreateEventW failed");
        UINT64 sequence = 0;
        struct Case
        {
            const char* name;
            uint32_t sourceSize, targetSize;
            nvrhi::Format targetFormat;
            std::vector<uint32_t> pixels;
            uint32_t expected;
        };
        const Case cases[] = {
            {"SRGBA8 endpoint red", 16, 16, nvrhi::Format::SRGBA8_UNORM,
                std::vector<uint32_t>(256, 0xff0000ffu), 0xff0000ffu},
            {"2x2 scene-linear average", 2, 1, nvrhi::Format::RGBA8_UNORM,
                {0xff000000u, 0xffffffffu, 0xff000000u, 0xffffffffu}, 0xff808080u}
        };
        for (const auto& test : cases)
        {
            std::cout << test.name << '\n';
            nvrhi::TextureDesc sourceDescription;
            sourceDescription.width = sourceDescription.height = test.sourceSize;
            sourceDescription.format = nvrhi::Format::RGBA8_UNORM;
            sourceDescription.initialState = nvrhi::ResourceStates::ShaderResource;
            sourceDescription.keepInitialState = true;
            sourceDescription.debugName = "Common passes test source";
            auto targetDescription = sourceDescription;
            targetDescription.width = targetDescription.height = test.targetSize;
            targetDescription.format = test.targetFormat;
            targetDescription.isRenderTarget = true;
            targetDescription.initialState = nvrhi::ResourceStates::RenderTarget;
            targetDescription.debugName = test.name;
            auto source = device->createTexture(sourceDescription);
            auto target = device->createTexture(targetDescription);
            if (!source || !target)
                Fail("test textures did not initialize");
            auto framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
            auto readback = device->createStagingTexture(targetDescription, nvrhi::CpuAccessMode::Read);
            auto commands = device->createCommandList();
            if (!framebuffer || !readback || !commands)
                Fail("test render resources did not initialize");
            commands->open();
            commands->writeTexture(source, 0u, 0u, test.pixels.data(), test.sourceSize * sizeof(uint32_t));
            if (!commonPasses.BlitTexture(commands, framebuffer, source))
            {
                commands->close();
                PrintD3d12Messages(nativeDevice.Get());
                Fail("production blit did not dispatch");
            }
            commands->copyTexture(readback, nvrhi::TextureSlice(), target, nvrhi::TextureSlice());
            commands->close();
            device->executeCommandList(commands);
            RequireSucceeded(nativeQueue->Signal(fence.Get(), ++sequence), "Queue::Signal");
            RequireSucceeded(fence->SetEventOnCompletion(sequence, completed), "SetEventOnCompletion");
            if (WaitForSingleObject(completed, 30000u) != WAIT_OBJECT_0)
                Fail("native D3D12 queue did not reach the test fence");
            if (callback.failed)
                Fail("production blit emitted an error");

            size_t rowPitch = 0;
            const auto* mapped = static_cast<const uint8_t*>(device->mapStagingTexture(
                readback, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
            if (!mapped || rowPitch < test.targetSize * sizeof(uint32_t))
                Fail("blit output could not be read back");
            bool matches = true;
            for (uint32_t row = 0; row < test.targetSize; ++row)
                for (uint32_t column = 0; column < test.targetSize; ++column)
                {
                    uint32_t pixel = 0;
                    std::memcpy(&pixel, mapped + row * rowPitch + column * sizeof(pixel), sizeof(pixel));
                    if (pixel != test.expected)
                        std::cerr << "pixel " << column << ',' << row << ": 0x" << std::hex
                            << pixel << ", expected 0x" << test.expected << std::dec << '\n';
                    matches &= pixel == test.expected;
                }
            device->unmapStagingTexture(readback);
            if (!matches)
                Fail("blit pixels changed the known answer");
        }
        CloseHandle(completed);
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
