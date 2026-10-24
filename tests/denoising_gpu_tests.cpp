#include "denoising_pass.h"
#include "renderer_shader_factory.h"
#include "ray_visibility_pass.h"
#include <donut/engine/View.h>
#include <donut/engine/SceneGraph.h>
#include <nvrhi/d3d12.h>
#include <nvrhi/validation.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = 619u;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

namespace
{
    using namespace uvsr;
    using Microsoft::WRL::ComPtr;
    void Require(bool success, const char* message)
    {
        if (!success) throw std::runtime_error(message);
    }
    struct Callback : nvrhi::IMessageCallback
    {
        bool failed = false;
        void message(nvrhi::MessageSeverity severity, const char* value) override
        {
            if (severity == nvrhi::MessageSeverity::Error || severity == nvrhi::MessageSeverity::Fatal)
            { failed = true; std::cerr << value << '\n'; }
        }
    };
    struct Gpu
    {
        Callback callback;
        ComPtr<ID3D12Device> native;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D12Fence> fence;
        ComPtr<ID3D12InfoQueue> messages;
        nvrhi::DeviceHandle device;
        std::shared_ptr<RendererShaderFactory> shaders;
        HANDLE event = nullptr;
        UINT64 sequence = 0;
        explicit Gpu(const std::filesystem::path& path)
        {
            ComPtr<ID3D12Debug> debug;
            Require(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))), "D3D12 debug layer unavailable");
            debug->EnableDebugLayer();
            ComPtr<ID3D12Debug1> validation;
            if (SUCCEEDED(debug.As(&validation))) validation->SetEnableGPUBasedValidation(TRUE);
            Require(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&native))), "create device");
            Require(SUCCEEDED(native.As(&messages)), "debug message queue");
            D3D12_COMMAND_QUEUE_DESC desc{};
            Require(SUCCEEDED(native->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))), "create queue");
            Require(SUCCEEDED(native->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))), "create fence");
            event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            Require(event != nullptr, "create fence event");
            nvrhi::d3d12::DeviceDesc d;
            d.errorCB = &callback; d.pDevice = native.Get(); d.pGraphicsCommandQueue = queue.Get();
            device = nvrhi::validation::createValidationLayer(nvrhi::d3d12::createDevice(d));
            shaders = std::make_shared<RendererShaderFactory>(device, path);
        }
        ~Gpu() { if (event) CloseHandle(event); }
        void Submit(nvrhi::ICommandList* command)
        {
            command->close(); device->executeCommandList(command);
            Require(SUCCEEDED(queue->Signal(fence.Get(), ++sequence)), "signal fence");
            Require(SUCCEEDED(fence->SetEventOnCompletion(sequence, event)), "fence event");
            Require(WaitForSingleObject(event, 60000) == WAIT_OBJECT_0, "GPU timeout");
            for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
            {
                SIZE_T size = 0; messages->GetMessage(i, nullptr, &size);
                std::vector<unsigned char> storage(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (SUCCEEDED(messages->GetMessage(i, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                { std::cerr << message->pDescription << '\n'; callback.failed = true; }
            }
            messages->ClearStoredMessages();
            Require(!callback.failed, "GPU validation error");
            device->runGarbageCollection();
        }
        nvrhi::TextureHandle Texture(uint32_t width, uint32_t height, nvrhi::Format format)
        {
            nvrhi::TextureDesc d;
            d.width = width; d.height = height; d.format = format; d.isUAV = true;
            d.initialState = nvrhi::ResourceStates::ShaderResource; d.keepInitialState = true;
            auto texture = device->createTexture(d);
            Require(bool(texture), "create test texture"); return texture;
        }
        std::vector<unsigned char> Read(nvrhi::ITexture* texture, uint32_t stride)
        {
            Require(texture != nullptr, "null readback texture");
            const auto& d = texture->getDesc();
            auto staging = device->createStagingTexture(d, nvrhi::CpuAccessMode::Read);
            auto command = device->createCommandList(); command->open();
            command->copyTexture(staging, {}, texture, {}); Submit(command);
            size_t pitch = 0;
            const auto* mapped = static_cast<const unsigned char*>(device->mapStagingTexture(staging, {}, nvrhi::CpuAccessMode::Read, &pitch));
            Require(mapped && pitch >= d.width * stride, "map test result");
            std::vector<unsigned char> result(size_t(d.width) * d.height * stride);
            for (uint32_t y = 0; y < d.height; ++y)
                std::memcpy(result.data() + size_t(y) * d.width * stride, mapped + y * pitch, size_t(d.width) * stride);
            device->unmapStagingTexture(staging); return result;
        }
        std::vector<float> Floats(nvrhi::ITexture* texture)
        {
            auto bytes = Read(texture, 16); std::vector<float> result(bytes.size() / sizeof(float));
            std::memcpy(result.data(), bytes.data(), bytes.size()); return result;
        }
    };
    struct Fixture
    {
        Gpu& gpu;
        uint32_t width, height;
        nvrhi::TextureHandle raw, depth, normal, motion, gradient, material;
        donut::engine::PlanarView view;
        DenoisingInputs inputs;
        Fixture(Gpu& g, uint32_t w, uint32_t h, nvrhi::Format format = nvrhi::Format::RGBA32_FLOAT) : gpu(g), width(w), height(h)
        {
            raw = g.Texture(w, h, format); depth = g.Texture(w, h, nvrhi::Format::R32_FLOAT);
            normal = g.Texture(w, h, nvrhi::Format::RGBA32_FLOAT); motion = g.Texture(w, h, nvrhi::Format::RGBA32_FLOAT);
            material = g.Texture(w, h, nvrhi::Format::RGBA32_FLOAT);
            gradient = g.Texture((w + 2) / 3, (h + 2) / 3, nvrhi::Format::RGBA32_FLOAT);
            view.SetViewport(nvrhi::Viewport(float(w), float(h)));
            view.SetMatrices(dm::affine3::identity(), dm::perspProjD3DStyleReverse(dm::radians(60.f), float(w) / float(h), 1.f));
            view.UpdateCache();
            inputs.rawSignal = raw; inputs.depth = depth; inputs.normalRoughness = normal; inputs.motionVectors = motion;
            inputs.currentView = inputs.previousView = &view; inputs.signalSize = inputs.sourceSize = { w, h };
            auto c = g.device->createCommandList(); c->open();
            c->clearTextureFloat(raw, nvrhi::AllSubresources, nvrhi::Color(0.f));
            c->clearTextureFloat(depth, nvrhi::AllSubresources, nvrhi::Color(.5f));
            c->clearTextureFloat(normal, nvrhi::AllSubresources, nvrhi::Color(0.f, 0.f, -1.f, 1.f));
            c->clearTextureFloat(motion, nvrhi::AllSubresources, nvrhi::Color(0.f, 0.f, 0.f, 1.f));
            c->clearTextureFloat(gradient, nvrhi::AllSubresources, nvrhi::Color(0.f, 1.f, 1.f, 0.f));
            c->clearTextureFloat(material, nvrhi::AllSubresources, nvrhi::Color(1.f, 1.f, 0.f, 0.f));
            g.Submit(c);
        }
        nvrhi::ITexture* Filter(DenoisingPass& pass, const DenoisingSignalSettings& settings, float value, uint64_t frame)
        {
            inputs.frameIndex = frame;
            auto c = gpu.device->createCommandList(); c->open();
            c->clearTextureFloat(raw, nvrhi::AllSubresources, nvrhi::Color(value, value, value, 1.f));
            const auto result = pass.ProcessDiffuseGi(c, settings, inputs);
            Require(result.denoised, "filter unexpectedly returned raw"); gpu.Submit(c); return result.texture;
        }
    };
    void Compare(const std::vector<float>& a, const std::vector<float>& b, const char* name)
    {
        Require(a.size() == b.size(), "comparison extent");
        float error = 0.f, scale = 1.f;
        for (size_t i = 0; i < a.size(); ++i)
        {
            Require(std::isfinite(a[i]) && std::isfinite(b[i]), "nonfinite filter result");
            error = std::max(error, std::abs(a[i] - b[i])); scale = std::max(scale, std::abs(a[i]));
        }
        if (error > 2e-5f * scale)
        { std::cerr << name << " max error " << error << " scale " << scale << '\n'; Require(false, "optimization changed filter result"); }
    }
    void Temporal(Gpu& gpu)
    {
        Fixture f(gpu, 17, 19); DenoisingPass pass(gpu.device, gpu.shaders);
        DenoisingSignalSettings s; s.method = DenoisingMethodChoice::Svgf; s.atrous = s.prefilter = false;
        s.alpha = .1f; s.adaptive = true; s.gradientIterations = 5;
        for (uint32_t kind = 0; kind < 7; ++kind)
        {
            pass.ReleaseResources(); f.inputs.temporalGradient = kind < 2 ? f.gradient.Get() : nullptr;
            f.inputs.motionVectors = f.motion;
            auto c = gpu.device->createCommandList(); c->open();
            c->clearTextureFloat(f.depth, nvrhi::AllSubresources, nvrhi::Color(.5f));
            c->clearTextureFloat(f.normal, nvrhi::AllSubresources, nvrhi::Color(0.f, 0.f, -1.f, 1.f));
            c->clearTextureFloat(f.motion, nvrhi::AllSubresources, nvrhi::Color(0.f, 0.f, 0.f, 1.f));
            c->clearTextureFloat(f.gradient, nvrhi::AllSubresources, nvrhi::Color(kind == 1 ? 1.f : 0.f, 1.f, 1.f, 0.f)); gpu.Submit(c);
            for (uint64_t frame = 1; frame <= 4; ++frame) f.Filter(pass, s, 0.f, frame);
            c->open();
            if (kind == 2) c->clearTextureFloat(f.depth, nvrhi::AllSubresources, nvrhi::Color(.25f));
            if (kind == 3) c->clearTextureFloat(f.normal, nvrhi::AllSubresources, nvrhi::Color(0.f, 1.f, 0.f, 1.f));
            if (kind == 4) f.inputs.motionVectors = nullptr;
            if (kind == 5) c->clearTextureFloat(f.motion, nvrhi::AllSubresources, nvrhi::Color(1000.f, 0.f, 0.f, 1.f));
            if (kind == 6) pass.RequestHistoryReset();
            gpu.Submit(c);
            auto pixels = gpu.Floats(f.Filter(pass, s, 1.f, 5));
            const float expected = kind == 0 ? .2f : 1.f;
            for (size_t i = 0; i < pixels.size(); ++i)
                Require(std::abs(pixels[i] - (i % 4 == 3 ? 1.f : expected)) < 1e-5f, "temporal alpha or surface rejection");
        }
        std::cout << "temporal accumulation, correlated gradient response, five reset paths passed\n";
    }
    void Bilateral(Gpu& gpu)
    {
        Fixture f(gpu, 5, 1); DenoisingPass pass(gpu.device, gpu.shaders);
        DenoisingSignalSettings s; s.method = DenoisingMethodChoice::Bilateral;
        s.guidance = BilateralGuidance::Signal; s.gaussian = false; s.rangeSigma = 1.f; s.radius = 1;
        std::vector<float> input(20, 0.f); input[8] = input[9] = input[10] = 1.f;
        auto c = gpu.device->createCommandList(); c->open(); c->writeTexture(f.raw, 0, 0, input.data(), 80);
        f.inputs.frameIndex = 1;
        auto output = pass.ProcessDiffuseGi(c, s, f.inputs); Require(output.denoised, "bilateral dispatch"); gpu.Submit(c);
        const auto pixels = gpu.Floats(output.texture);
        const float k = std::exp(-0.5f);
        const float expected[] = { 0.f, k / (2.f + k), 1.f / (1.f + 2.f * k), k / (2.f + k), 0.f };
        for (uint32_t x = 0; x < 5; ++x) Require(std::abs(pixels[x * 4] - expected[x]) < 1e-5f, "bilateral analytic impulse");
        std::cout << "bilateral analytic impulse passed\n";
        s.rangeSigma = 0.001f;
        c->open();
        output = pass.ProcessSkyVisibility(c, s, f.inputs);
        Require(output.denoised, "visibility guidance dispatch"); gpu.Submit(c);
        const auto visibility = gpu.Floats(output.texture);
        for (uint32_t x = 0; x < 5; ++x)
            Require(std::abs(visibility[x * 4] - (x > 0 && x < 4 ? 1.f / 3.f : 0.f)) < 1e-5f,
                "visibility ray outcomes were treated as signal edges");
        std::cout << "sky visibility gates signal guidance and preserves surface averaging\n";
    }
    void Formats(Gpu& gpu)
    {
        const nvrhi::Format formats[] = { nvrhi::Format::R8_UNORM, nvrhi::Format::R16_FLOAT,
            nvrhi::Format::R32_FLOAT, nvrhi::Format::RGBA16_FLOAT, nvrhi::Format::RGBA32_FLOAT };
        const uint32_t strides[] = { 1, 2, 4, 8, 16 };
        DenoisingPass pass(gpu.device, gpu.shaders);
        for (uint32_t i = 0; i < 5; ++i)
        {
            pass.ReleaseResources(); Fixture f(gpu, 33, 35, formats[i]);
            DenoisingSignalSettings s; s.method = DenoisingMethodChoice::Svgf; s.resolution = DenoisingResolution::Quarter;
            auto result = f.Filter(pass, s, 1.f, 1); auto bytes = gpu.Read(result, strides[i]);
            const uint32_t component = i == 0 ? 1 : (i == 1 || i == 3) ? 2 : 4;
            const uint32_t expected = component == 1 ? 255 : component == 2 ? 0x3c00 : 0x3f800000;
            for (size_t j = 0; j < bytes.size(); j += component)
            {
                uint32_t value = 0; std::memcpy(&value, bytes.data() + j, component);
                float number = 0.f; if (component == 4) std::memcpy(&number, &value, 4);
                // typed half stores can round a sub-ULP float accumulation below one
                // to the adjacent half value. allow that single conversion step.
                const bool equal = component == 4 ? std::abs(number - 1.f) < 2e-6f :
                    component == 2 ? (value == expected || value + 1u == expected) : value == expected;
                if (!equal) std::cerr << "format " << i << " component " << j << " value " << value << " expected " << expected << '\n';
                Require(equal, "constant preservation or output format");
            }
        }
        std::cout << "five output formats and quarter-resolution constant preservation passed\n";
    }
    void Replay(Gpu& gpu)
    {
        Fixture f(gpu, 33, 35); DenoisingPass pass(gpu.device, gpu.shaders);
        LightingSurfaceView surface; surface.depth = f.depth; surface.normals = f.normal;
        surface.material = f.material; surface.motionVectors = f.motion;
        for (uint64_t frame = 1; frame <= 3; ++frame)
        {
            auto c = gpu.device->createCommandList(); c->open();
            if (frame == 3) c->clearTextureFloat(f.depth, nvrhi::AllSubresources, nvrhi::Color(.25f));
            Require(pass.PrepareReplay(c, surface, f.view, &f.view, frame, 7), "surface replay dispatch"); gpu.Submit(c);
            for (uint32_t scale : { 1u, 2u, 4u })
            {
                auto replay = pass.GetReplay(scale); auto bytes = gpu.Read(replay.selection, 4);
                std::vector<uint32_t> actual(bytes.size() / 4), expected(actual.size(), 0xffffffffu);
                std::memcpy(actual.data(), bytes.data(), bytes.size());
                if (frame == 2)
                {
                    const uint32_t w = (f.width + scale - 1) / scale, h = (f.height + scale - 1) / scale;
                    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x)
                    {
                        const uint32_t px = std::min(x * scale + scale / 2, f.width - 1);
                        const uint32_t py = std::min(y * scale + scale / 2, f.height - 1);
                        auto& value = expected[(py / 3) * ((f.width + 2) / 3) + px / 3];
                        value = std::min(value, (y * w + x) * 0x9e3779b1u + uint32_t(frame));
                    }
                }
                Require(actual == expected, "replay selection, first frame, or disocclusion rejection");
            }
        }
        std::cout << "forward replay selection at three sample scales and disocclusion rejection passed\n";
    }
    void RayReplay(Gpu& gpu)
    {
        Fixture f(gpu, 17, 19); DenoisingPass denoising(gpu.device, gpu.shaders);
        LightingSurfaceView surface; surface.depth = f.depth; surface.normals = f.normal;
        surface.material = f.material; surface.motionVectors = f.motion;
        nvrhi::BindlessLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute; layoutDesc.maxCapacity = 16;
        layoutDesc.registerSpaces = { nvrhi::BindingLayoutItem::RawBuffer_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2) };
        std::cout << "ray fixture: layouts\n";
        auto layout = gpu.device->createBindlessLayout(layoutDesc);
        auto table = gpu.device->createDescriptorTable(layout); gpu.device->resizeDescriptorTable(table, 16);
        RayVisibilityPass visibility(gpu.device, gpu.shaders, layout, RayVisibilityPass::Kind::Sun);
        Require(visibility.IsSupported(), "ray replay pipeline unavailable");
        std::cout << "ray fixture: pipelines ready\n";
        const float vertices[] = { -100.f, -100.f, 1.f, 100.f, -100.f, 1.f, 0.f, 100.f, 1.f };
        nvrhi::BufferDesc vbDesc;
        vbDesc.byteSize = sizeof(vertices); vbDesc.isAccelStructBuildInput = true;
        vbDesc.initialState = nvrhi::ResourceStates::AccelStructBuildInput; vbDesc.keepInitialState = true;
        auto verticesGpu = gpu.device->createBuffer(vbDesc);
        const uint32_t indices[] = { 0, 1, 2 };
        vbDesc.byteSize = sizeof(indices);
        auto indicesGpu = gpu.device->createBuffer(vbDesc);
        nvrhi::rt::GeometryDesc geometry;
        geometry.setFlags(nvrhi::rt::GeometryFlags::Opaque).setTriangles(nvrhi::rt::GeometryTriangles()
            .setIndexBuffer(indicesGpu).setIndexFormat(nvrhi::Format::R32_UINT).setIndexCount(3)
            .setVertexBuffer(verticesGpu).setVertexCount(3).setVertexStride(12).setVertexFormat(nvrhi::Format::RGB32_FLOAT));
        auto blas = gpu.device->createAccelStruct(nvrhi::rt::AccelStructDesc().addBottomLevelGeometry(geometry));
        std::cout << "ray fixture: BLAS allocated\n";
        auto tlas = gpu.device->createAccelStruct(nvrhi::rt::AccelStructDesc().setTopLevelMaxInstances(1));
        std::cout << "ray fixture: TLAS allocated\n";
        const auto instance = nvrhi::rt::InstanceDesc().setBLAS(blas).setInstanceMask(255);
        const auto structured = [&](uint32_t stride)
        {
            nvrhi::BufferDesc d; d.byteSize = d.structStride = stride;
            d.initialState = nvrhi::ResourceStates::ShaderResource; d.keepInitialState = true;
            return gpu.device->createBuffer(d);
        };
        auto geometries = structured(64), materials = structured(208), map = structured(4);
        RaySceneView scene; scene.tlas = tlas; scene.geometryBuffer = geometries; scene.materialBuffer = materials;
        scene.geometryIndexMap = map; scene.descriptorTable = table; scene.generation = 1;
        auto command = gpu.device->createCommandList(); command->open();
        command->writeBuffer(verticesGpu, vertices, sizeof(vertices));
        command->writeBuffer(indicesGpu, indices, sizeof(indices));
        std::cout << "ray fixture: build BLAS\n";
        command->buildBottomLevelAccelStruct(blas, &geometry, 1);
        std::cout << "ray fixture: build TLAS\n";
        command->buildTopLevelAccelStruct(tlas, &instance, 1); gpu.Submit(command);
        std::cout << "ray fixture: acceleration structures ready\n";
        auto graph = std::make_shared<donut::engine::SceneGraph>();
        auto node = std::make_shared<donut::engine::SceneGraphNode>();
        auto light = std::make_shared<donut::engine::DirectionalLight>();
        graph->SetRootNode(node); node->SetLeaf(light); graph->Refresh(0);
        std::cout << "ray fixture: light ready\n";
        DirectionalShadowSettings settings;
        for (uint32_t frame = 1; frame <= 3; ++frame)
        {
            if (frame == 3) { light->SetDirection(dm::double3(0., 0., 1.)); graph->Refresh(frame); }
            std::cout << "ray fixture: frame " << frame << '\n';
            command->open();
            Require(denoising.PrepareReplay(command, surface, f.view, &f.view, frame, 1), "ray surface capture");
            auto result = visibility.RenderDirectional(command, settings, f.view, surface, scene, light.get(), 1000.f, denoising.GetReplay());
            Require(bool(result), "directional replay dispatch"); gpu.Submit(command);
            auto raw = gpu.Read(result.visibility, 1);
            for (auto value : raw) Require(value == (frame == 3 ? 0 : 255), "occluder and light direction known answer");
            const auto gradient = gpu.Floats(visibility.GetTemporalGradient());
            for (size_t i = 0; i < gradient.size(); i += 4)
            {
                Require(std::abs(gradient[i] - (frame == 3 ? 1.f : 0.f)) < 1e-5f, "correlated ray replay lighting change");
                Require(gradient[i + 2] == (frame == 1 ? 0.f : 1.f), "ray replay confidence");
            }
        }
        std::cout << "actual DXR replay: static zero gradient and changed-light unit gradient passed\n";
    }
    void SharedSignals(Gpu& gpu)
    {
        Fixture f(gpu, 33, 35); DenoisingPass pass(gpu.device, gpu.shaders, 1);
        DenoisingSignalSettings s; s.method = DenoisingMethodChoice::Svgf; s.adaptive = true;
        s.iterations = s.gradientIterations = 7;
        f.inputs.temporalGradient = f.gradient;
        for (uint64_t frame = 1; frame <= 3; ++frame)
        {
            f.inputs.frameIndex = frame; auto command = gpu.device->createCommandList(); command->open();
            command->clearTextureFloat(f.raw, nvrhi::AllSubresources, nvrhi::Color(1.f));
            const DenoisingResult results[] = { pass.ProcessAmbientOcclusion(command, s, f.inputs),
                pass.ProcessDiffuseGi(command, s, f.inputs), pass.ProcessSunShadow(command, s, f.inputs),
                pass.ProcessFlashlightShadow(command, s, f.inputs), pass.ProcessSkyVisibility(command, s, f.inputs),
                pass.ProcessPathTracing(command, s, f.inputs) };
            gpu.Submit(command);
            for (const auto result : results)
            {
                Require(result.denoised, "six simultaneous signals");
                for (const auto value : gpu.Floats(result.texture)) Require(std::abs(value - 1.f) < 2e-6f, "shared constants or guide history overwritten");
            }
        }
        std::cout << "six simultaneous signals with maximum pass counts passed\n";
    }
#include "denoising_quality_gpu_tests.h"
}
int main(int argc, char** argv)
{
    try
    {
        std::cout << std::unitbuf;
        Require(argc == 2 || argc == 3, "expected packaged shader directory"); Gpu gpu(std::filesystem::u8path(argv[1]));
        if (argc == 3)
        {
            if (std::string_view(argv[2]) == "reduced") ReducedFilterQuality(gpu);
            else { Require(std::string_view(argv[2]) == "quality", "unknown focused test"); DenoisingQuality(gpu); }
            return 0;
        }
        Bilateral(gpu); Temporal(gpu); Formats(gpu); Replay(gpu); RayReplay(gpu); SharedSignals(gpu); DenoisingQuality(gpu);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "denoising GPU test failed: " << error.what() << '\n'; return 1; }
}
