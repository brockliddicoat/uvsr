#include "renderer_gpu_fixture.h"
#include "renderer_common_passes_nvrhi.h"
#include "renderer_shader_factory_nvrhi.h"
#include "renderer_pixel_readback_nvrhi.h"
#include "renderer_texture_bmp_nvrhi.h"
#include "renderer_scene_resources_nvrhi.h"

#include <nvrhi/d3d12.h>
#include <nvrhi/validation.h>

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdlib>
#include <cwchar>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

void TestRendererSceneRetirement(
    nvrhi::IDevice* device, ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue);
void TestRendererTargets(nvrhi::IDevice* device);
void TestImportSkinGpu(nvrhi::IDevice* device, const std::filesystem::path& appShaderDirectory, uvsr::RendererUploadHealth health);
void TestImportUploadGpu(nvrhi::IDevice* device, ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue,
    uvsr::RendererCommonPasses& passes, uvsr::RendererUploadHealth health);
void TestBlitCacheUploadFailure(nvrhi::IDevice* device, uvsr::RendererShaderFactory& factory,
    uvsr::RendererUploadHealth health);
void TestNvrhiDescriptorFailures(ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue);
void TestRendererSceneGpuTables(nvrhi::IDevice* device, const std::filesystem::path& instanceProbe,
    uvsr::RendererUploadHealth health);
void TestRendererUi(nvrhi::IDevice* device, ID3D12Device* nativeDevice,
    ID3D12CommandQueue* nativeQueue, const std::filesystem::path& shaderDirectory);
void TestRendererBindingOwners(nvrhi::IDevice* device, ID3D12Device* nativeDevice,
    ID3D12CommandQueue* nativeQueue, uvsr::RendererShaderFactory& shaders);

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
                if (expectedDescriptorHeapFailures && text &&
                    std::strcmp(text, "Failed to grow a descriptor heap!") == 0)
                {
                    --expectedDescriptorHeapFailures;
                    return;
                }
                failed = true;
                std::cerr << "NVRHI: " << (text ? text : "") << '\n';
            }
        }
        bool failed = false;
        unsigned expectedDescriptorHeapFailures = 0;
    };

    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "Renderer common passes test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void CALLBACK ReportD3d12Message(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity,
        D3D12_MESSAGE_ID id, LPCSTR message, void*)
    {
        if (severity == D3D12_MESSAGE_SEVERITY_ERROR || severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            std::cerr << "D3D12 message " << unsigned(id) << ": " << message << '\n';
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

    void RequireNoD3d12Errors(ID3D12Device* device)
    {
        ComPtr<ID3D12InfoQueue> messages;
        RequireSucceeded(device->QueryInterface(IID_PPV_ARGS(&messages)), "D3D12 info queue");
        const UINT64 count = messages->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 index = 0; index < count; ++index)
        {
            SIZE_T bytes = 0;
            RequireSucceeded(messages->GetMessage(index, nullptr, &bytes), "D3D12 message size");
            void* storage = malloc(bytes);
            if (!storage)
                Fail("D3D12 diagnostic allocation failed");
            auto* message = static_cast<D3D12_MESSAGE*>(storage);
            const HRESULT result = messages->GetMessage(index, message, &bytes);
            const bool failed = SUCCEEDED(result) &&
                (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                    message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION);
            free(storage);
            RequireSucceeded(result, "D3D12 message");
            if (failed)
            {
                PrintD3d12Messages(device);
                Fail("D3D12 debug layer reported an error");
            }
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

    void TestProductionPixelReadback(
        nvrhi::IDevice* device,
        uvsr::RendererShaderFactory& factory,
        RecordingCallback& callback)
    {
        using uvsr::RendererReadbackError;
        using uvsr::RendererPixelReadbackNvrhi;
        uvsr::RendererPixelReadback empty;
        uvsr::RendererReadbackUint4 untouched{ 19, 23, 29, 31 };
        if (empty.IsValid() || empty.ReadUInts(untouched) != RendererReadbackError::Uninitialized ||
            untouched.x != 19 || untouched.y != 23 || untouched.z != 29 || untouched.w != 31 ||
            RendererPixelReadbackNvrhi::Initialize(empty, nullptr, nullptr, nullptr, nullptr) != RendererReadbackError::InvalidInput)
            Fail("uninitialized readback published output");
        const auto shader = factory.CreateShader(
            "uvsr/renderer_pixel_readback_cs.hlsl", "main", {}, nvrhi::ShaderType::Compute);
        if (!shader)
            Fail("readback production shader is unavailable");

        const uint16_t narrow[4][2] = { { 1, 17 }, { 7, 11 }, { 65534, 42 }, { 65535, 65535 } };
        const uint32_t wide[4][2] = {
            { 7, 11 }, { 65535, 65536 }, { 0x12345678, 0x7fffffff }, { UINT32_MAX, UINT32_MAX } };
        for (unsigned variant = 0; variant < 4; ++variant)
        {
            const bool wideFormat = (variant & 1) != 0;
            const bool immediate = variant < 2;
            nvrhi::TextureDesc description;
            description.width = description.height = 2;
            description.format = wideFormat ? nvrhi::Format::RG32_UINT : nvrhi::Format::RG16_UINT;
            description.initialState = nvrhi::ResourceStates::ShaderResource;
            description.keepInitialState = true;
            description.debugName = "Integer readback source";
            auto source = device->createTexture(description);
            auto commands = device->createCommandList(
                nvrhi::CommandListParameters().setEnableImmediateExecution(immediate));
            if (!source || !commands)
                Fail("readback fixture resources are unavailable");
            uvsr::RendererPixelReadback readback;
            if (RendererPixelReadbackNvrhi::Initialize(readback, device, commands, shader, source) != RendererReadbackError::None ||
                !readback.IsValid() ||
                RendererPixelReadbackNvrhi::Initialize(readback, device, commands, shader, source) != RendererReadbackError::Busy)
                Fail("readback initialization did not publish exactly once");
            commands->open();
            commands->writeTexture(source, 0, 0,
                wideFormat ? static_cast<const void*>(wide) : static_cast<const void*>(narrow),
                wideFormat ? sizeof(wide[0]) * 2 : sizeof(narrow[0]) * 2);
            commands->close();
            if (device->executeCommandList(commands) == 0)
                Fail("readback source upload was not submitted");

            commands->open();
            if (readback.Capture({ 2, 0 }) != RendererReadbackError::InvalidInput ||
                readback.Capture({ 0, 2 }) != RendererReadbackError::InvalidInput ||
                readback.Capture({ UINT32_MAX, 0 }) != RendererReadbackError::InvalidInput)
                Fail("readback coordinate validation changed");
            if (immediate)
            {
                commands->close();
                if (device->executeCommandList(commands) == 0)
                    Fail("immediate validation list was not submitted");
            }
            else
            {
                if (readback.Capture({ 0, 0 }) != RendererReadbackError::None)
                    Fail("deferred readback did not record");
                commands->close();
                readback.CancelRecorded();
            }

            const unsigned selected[4] = { 1, 2, 0, 3 };
            for (unsigned index : selected)
            {
                commands->open();
                if (readback.Capture({ index % 2, index / 2 }) != RendererReadbackError::None ||
                    readback.Capture({ 0, 0 }) != RendererReadbackError::Busy ||
                    readback.ReadUInts(untouched) != RendererReadbackError::NotSubmitted ||
                    untouched.x != 19 || untouched.y != 23 || untouched.z != 29 || untouched.w != 31 ||
                    readback.NotifySubmitted(0) != RendererReadbackError::SubmissionFailed)
                    Fail("readback accepted an overlapping or unsubmitted request");
                commands->close();
                const uint64_t submission = device->executeCommandList(commands);
                if (readback.NotifySubmitted(submission) != RendererReadbackError::None ||
                    readback.NotifySubmitted(submission) != RendererReadbackError::Busy ||
                    readback.Capture({ 0, 0 }) != RendererReadbackError::Busy)
                    Fail("readback lost its submitted transaction");

                // no external fence wait: this exercises the production blocking map.
                uvsr::RendererReadbackUint4 output{};
                if (readback.ReadUInts(output) != RendererReadbackError::None ||
                    output.x != (wideFormat ? wide[index][0] : narrow[index][0]) ||
                    output.y != (wideFormat ? wide[index][1] : narrow[index][1]) ||
                    readback.ReadUInts(untouched) != RendererReadbackError::NotSubmitted ||
                    callback.failed)
                    Fail("integer readback lost coordinates, full-width IDs or completion");
            }
        }
        std::cout << "2x2 integer readback, full-width IDs and submission lifetime passed\n";
    }

    void TestDescriptorBindingFailure(ID3D12Device* nativeDevice, ID3D12CommandQueue* nativeQueue)
    {
        RecordingCallback callback;
        nvrhi::d3d12::DeviceDesc description;
        description.errorCB = &callback;
        description.pDevice = nativeDevice;
        description.pGraphicsCommandQueue = nativeQueue;
        description.samplerHeapSize = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
        description.shaderResourceViewHeapSize = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;
        auto device = nvrhi::d3d12::createDevice(description);
        if (!device || callback.failed)
            Fail("descriptor exhaustion device creation failed");
        auto* samplers = device->getDescriptorHeap(nvrhi::d3d12::DescriptorHeapType::Sampler);
        auto* resources = device->getDescriptorHeap(nvrhi::d3d12::DescriptorHeapType::ShaderResourceView);
        auto sampler = device->createSampler(nvrhi::SamplerDesc());
        nvrhi::TextureDesc textureDescription;
        textureDescription.width = 1;
        textureDescription.height = 1;
        textureDescription.format = nvrhi::Format::RGBA8_UNORM;
        auto texture = device->createTexture(textureDescription);
        nvrhi::BindingLayoutDesc layoutDescription;
        layoutDescription.visibility = nvrhi::ShaderType::Pixel;
        layoutDescription.bindings = {nvrhi::BindingLayoutItem::Sampler(0), nvrhi::BindingLayoutItem::Texture_SRV(0)};
        auto layout = device->createBindingLayout(layoutDescription);
        nvrhi::BindingSetDesc bindings;
        bindings.bindings = {nvrhi::BindingSetItem::Sampler(0, sampler), nvrhi::BindingSetItem::Texture_SRV(0, texture)};
        if (!samplers || !resources || !sampler || !texture || !layout || callback.failed)
            Fail("descriptor exhaustion inputs failed");

        const uint32_t samplerCount = description.samplerHeapSize;
        const uint32_t resourceCount = description.shaderResourceViewHeapSize;
        auto* samplerHeap = samplers->getHeap();
        auto* shaderSamplerHeap = samplers->getShaderVisibleHeap();
        if (samplers->allocateDescriptors(samplerCount) != 0)
            Fail("sampler exhaustion reservation failed");
        callback.expectedDescriptorHeapFailures = 1;
        if (device->createBindingSet(bindings, layout) || callback.expectedDescriptorHeapFailures || callback.failed ||
            samplers->getHeap() != samplerHeap || samplers->getShaderVisibleHeap() != shaderSamplerHeap)
            Fail("failed sampler binding did not preserve its heap");
        samplers->releaseDescriptors(0, samplerCount);

        // the second table fails after the first table was acquired. cleanup
        // must release that first table and leave the reserved table untouched.
        auto* resourceHeap = resources->getHeap();
        auto* shaderResourceHeap = resources->getShaderVisibleHeap();
        if (resources->allocateDescriptors(resourceCount) != 0)
            Fail("resource exhaustion reservation failed");
        callback.expectedDescriptorHeapFailures = 1;
        if (device->createBindingSet(bindings, layout) || callback.expectedDescriptorHeapFailures || callback.failed ||
            resources->getHeap() != resourceHeap || resources->getShaderVisibleHeap() != shaderResourceHeap)
            Fail("failed resource binding did not preserve its heap");
        if (samplers->allocateDescriptors(samplerCount) != 0)
            Fail("failed binding leaked its acquired sampler table");
        samplers->releaseDescriptors(0, samplerCount);
        resources->releaseDescriptors(0, resourceCount);

        auto retry = device->createBindingSet(bindings, layout);
        if (!retry || callback.failed)
            Fail("binding retry after exhaustion failed");
        retry = nullptr;
        if (samplers->allocateDescriptors(samplerCount) != 0 || resources->allocateDescriptors(resourceCount) != 0)
            Fail("binding cleanup lost descriptor capacity");
        samplers->releaseDescriptors(0, samplerCount);
        resources->releaseDescriptors(0, resourceCount);
        RequireNoD3d12Errors(nativeDevice);
        std::cout << "2 descriptor heap exhaustion paths preserve owners and recover full capacity\n";
    }

    void TestBlitCacheBmpFailure(nvrhi::IDevice* device, uvsr::RendererShaderFactory& factory)
    {
        uvsr::RendererCommonPasses passes(device, &factory);
        if (!passes.IsValid() || passes.HasBlitPipelineFailure()) Fail("fresh BMP cache");
        nvrhi::TextureDesc description;
        description.width = description.height = 2;
        description.format = nvrhi::Format::R8_UNORM;
        description.initialState = nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        auto source = device->createTexture(description);
        if (!source) Fail("BMP conversion source");
        if (!CreateDirectoryW(L"blit-cache-failure", nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            Fail("BMP failure fixture root");
        wchar_t directory[96], path[128];
        for (uint32_t ordinal = 0;; ++ordinal)
        {
            if (swprintf_s(directory, L"blit-cache-failure/run-%lu-%u", GetCurrentProcessId(), ordinal) <= 0)
                Fail("BMP failure fixture directory name");
            if (CreateDirectoryW(directory, nullptr)) break;
            if (GetLastError() != ERROR_ALREADY_EXISTS || ordinal == UINT32_MAX)
                Fail("fresh BMP failure fixture directory");
        }
        if (swprintf_s(path, L"%s/absent.bmp", directory) <= 0) Fail("BMP failure destination name");
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND)
            Fail("BMP failure destination starts absent");
        uvsr::FailNextRendererBlitPipelineAllocation();
        if (uvsr::SaveRendererTextureBmp(device, &passes, source, nvrhi::ResourceStates::ShaderResource, path) ||
            !passes.HasBlitPipelineFailure() || uvsr::RendererBlitPipelineAllocationFailurePending() ||
            GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES ||
            GetLastError() != ERROR_FILE_NOT_FOUND)
            Fail("cache allocation failure published a BMP");
        if (!device->waitForIdle()) Fail("BMP failure resource retirement");
        std::cout << "blit cache allocation failure leaves the BMP destination absent\n";
    }

    void TestProductionBlitPipeline(const std::filesystem::path& packagedShaderDirectory,
        const std::filesystem::path& instanceProbe)
    {
        RecordingCallback callback;
        ComPtr<ID3D12Debug> debug;
        RequireSucceeded(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12 debug layer");
        debug->EnableDebugLayer();
        ComPtr<ID3D12Device> nativeDevice;
        RequireSucceeded(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&nativeDevice)),
            "D3D12CreateDevice");
        ComPtr<ID3D12InfoQueue1> immediateMessages;
        RequireSucceeded(nativeDevice.As(&immediateMessages), "D3D12 immediate diagnostics");
        DWORD messageCookie = 0;
        RequireSucceeded(immediateMessages->RegisterMessageCallback(ReportD3d12Message,
            D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &messageCookie), "D3D12 diagnostic callback");
        ComPtr<ID3D12CommandQueue> nativeQueue;
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        RequireSucceeded(nativeDevice->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&nativeQueue)),
            "CreateCommandQueue");
        TestDescriptorBindingFailure(nativeDevice.Get(), nativeQueue.Get());
        TestNvrhiDescriptorFailures(nativeDevice.Get(), nativeQueue.Get());
        nvrhi::d3d12::DeviceDesc description;
        description.errorCB = &callback;
        description.pDevice = nativeDevice.Get();
        description.pGraphicsCommandQueue = nativeQueue.Get();
        nvrhi::DeviceHandle device = nvrhi::d3d12::createDevice(description);
        if (!device)
            Fail("NVRHI D3D12 device creation failed");
        device = nvrhi::validation::createValidationLayer(device);
        if (!device)
            Fail("NVRHI validation layer creation failed");
        TestRendererSceneRetirement(device, nativeDevice.Get(), nativeQueue.Get());
        if (callback.failed)
            Fail("checked descriptor exhaustion produced an unexpected backend error");
        TestRendererTargets(device);
        const uvsr::RendererUploadHealth uploadHealth{[](void* context) noexcept
            { return !static_cast<RecordingCallback*>(context)->failed; }, &callback};
        TestImportSkinGpu(device, packagedShaderDirectory, uploadHealth);
        TestRendererSceneGpuTables(device, instanceProbe, uploadHealth);
        uvsr::RendererShaderFactory factory(device, packagedShaderDirectory.c_str());
        uvsr::RendererCommonPasses commonPasses(device, &factory);
        if (!commonPasses.IsValid())
            Fail("common resources did not initialize");

        callback.expectedDescriptorHeapFailures = 1;
        TestImportUploadGpu(device, nativeDevice.Get(), nativeQueue.Get(), commonPasses, uploadHealth);
        if (callback.expectedDescriptorHeapFailures || callback.failed)
            Fail("native mip descriptor failure was not reported exactly once");

        TestProductionPixelReadback(device, factory, callback);
        TestRendererBindingOwners(device, nativeDevice.Get(), nativeQueue.Get(), factory);
        TestRendererUi(device, nativeDevice.Get(), nativeQueue.Get(), packagedShaderDirectory);

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
        TestBlitCacheUploadFailure(device, factory, uploadHealth);
        TestBlitCacheBmpFailure(device, factory);
        if (callback.failed) Fail("cache failure controls emitted a backend error");
        RequireNoD3d12Errors(nativeDevice.Get());
        immediateMessages->UnregisterMessageCallback(messageCookie);
    }
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 4)
        Fail("expected the packaged shader directory, test instance probe and fixed reference directory");
    SetRendererGpuReferenceDirectory(argv[3]);
    TestFailureLatch();
    TestProductionBlitPipeline(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]));
    return EXIT_SUCCESS;
}
