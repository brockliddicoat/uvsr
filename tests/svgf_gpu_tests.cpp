#include "svgf_pass.h"
#include "renderer_shader_factory.h"
#include <donut/engine/View.h>
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
            const bool scalar = texture->getDesc().format == nvrhi::Format::RG32_FLOAT;
            auto bytes = Read(texture, scalar ? 8u : 16u);
            std::vector<float> values(bytes.size() / sizeof(float));
            std::memcpy(values.data(), bytes.data(), bytes.size());
            if (!scalar) return values;
            std::vector<float> result(values.size() * 2);
            for (size_t i = 0; i < values.size() / 2; ++i)
            {
                result[i * 4] = result[i * 4 + 1] = result[i * 4 + 2] = values[i * 2];
                result[i * 4 + 3] = values[i * 2 + 1];
            }
            return result;
        }
    };
    struct Surface
    {
        Gpu& gpu;
        uint32_t width, height;
        nvrhi::TextureHandle radiance, depth, normal, motion, derivatives, albedo, emission;
        donut::engine::PlanarView view;
        SvgfInputs inputs;
        Surface(Gpu& g, uint32_t w = 33, uint32_t h = 25, uint32_t stride = 1)
            : gpu(g), width((w + stride - 1) / stride), height((h + stride - 1) / stride)
        {
            radiance = gpu.Texture(width, height, nvrhi::Format::RGBA32_FLOAT);
            depth = gpu.Texture(w, h, nvrhi::Format::R32_FLOAT);
            normal = gpu.Texture(w, h, nvrhi::Format::RGBA32_FLOAT);
            motion = gpu.Texture(w, h, nvrhi::Format::RGBA32_FLOAT);
            derivatives = gpu.Texture(w, h, nvrhi::Format::RGBA32_FLOAT);
            albedo = gpu.Texture(w, h, nvrhi::Format::RGBA32_FLOAT);
            emission = gpu.Texture(w, h, nvrhi::Format::RGBA32_FLOAT);
            view.SetViewport(nvrhi::Viewport(float(w), float(h)));
            view.SetMatrices(dm::affine3::identity(), dm::perspProjD3DStyleReverse(dm::radians(60.f), float(w) / float(h), 1.f));
            view.UpdateCache();
            inputs.radiance = radiance;
            inputs.deviceDepth = depth;
            inputs.worldNormal = normal;
            inputs.derivatives = derivatives;
            inputs.pixelMotion = motion;
            inputs.view = inputs.previousView = &view;
            inputs.sampleStride = {stride, stride};
            auto c = gpu.device->createCommandList(); c->open();
            c->clearTextureFloat(depth, nvrhi::AllSubresources, nvrhi::Color(0.5f));
            c->clearTextureFloat(normal, nvrhi::AllSubresources, nvrhi::Color(0, 0, -1, 1));
            c->clearTextureFloat(motion, nvrhi::AllSubresources, nvrhi::Color(0, 0, 0, 1));
            c->clearTextureFloat(derivatives, nvrhi::AllSubresources, nvrhi::Color(0));
            c->clearTextureFloat(albedo, nvrhi::AllSubresources, nvrhi::Color(1));
            c->clearTextureFloat(emission, nvrhi::AllSubresources, nvrhi::Color(0));
            gpu.Submit(c);
        }
        nvrhi::ITexture* Run(SvgfPass& pass, uint64_t frame, const std::vector<dm::float4>& values,
            DenoiserSignalType type = DenoiserSignalType::DiffuseGi)
        {
            inputs.frame = frame;
            auto c = gpu.device->createCommandList(); c->open();
            c->writeTexture(radiance, 0, 0, values.data(), width * sizeof(dm::float4));
            auto result = pass.Process(type, c, DenoisingSignalSettings{}, inputs);
            Require(result.denoised && result.texture, "SVGF did not publish its output");
            gpu.Submit(c);
            return result.texture;
        }
        std::vector<dm::float4> Constant(float value) const { return std::vector<dm::float4>(size_t(width) * height, dm::float4(value, value, value, 1)); }
    };

    void Near(float actual, float expected, float tolerance, const char* message)
    {
        if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
        { std::cerr << message << ": actual " << actual << ", expected " << expected << '\n'; throw std::runtime_error(message); }
    }

    void TemporalAnswers(Gpu& gpu)
    {
        Surface surface(gpu); SvgfPass pass(gpu.device, gpu.shaders);
        for (uint64_t frame = 1; frame <= 4; ++frame)
        {
            surface.Run(pass, frame, surface.Constant(float(2 * frame - 1)));
            const auto temporal = gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Temporal));
            const auto moments = gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Moments));
            for (size_t i = 0; i < moments.size(); i += 4)
            {
                Near(temporal[i], float(frame), 2e-4f, "equal-weight early illumination mean");
                Near(moments[i], float(frame), 2e-4f, "first luminance moment");
                Near(moments[i + 1], float(4 * frame * frame - 1) / 3.f, 5e-4f, "second luminance moment");
                Near(moments[i + 2], float(frame), 1e-5f, "history count including borders");
            }
        }
        for (uint64_t frame : {4u, 9u})
        {
            surface.Run(pass, frame, surface.Constant(11));
            auto moments = gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Moments));
            Near(moments[0], 11, 1e-4f, "repeated or skipped frame restarts history");
            Near(moments[2], 1, 1e-5f, "frame discontinuity resets count");
        }
        pass.RequestHistoryReset();
        surface.Run(pass, 10, surface.Constant(17));
        Near(gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Moments))[2], 1, 1e-5f, "explicit reset");
        surface.radiance = gpu.Texture(surface.width, surface.height, nvrhi::Format::RGBA32_FLOAT);
        surface.inputs.radiance = surface.radiance;
        surface.Run(pass, 11, surface.Constant(29));
        const auto replaced = gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Moments));
        Near(replaced[0], 29, 1e-4f, "replacement producer inherited stale radiance");
        Near(replaced[2], 1, 1e-5f, "replacement producer must restart history");
        std::cout << "analytic temporal moments, border taps, and reset ownership passed\n";
    }

    void MotionAnswers(Gpu& gpu)
    {
        Surface surface(gpu); SvgfPass pass(gpu.device, gpu.shaders);
        surface.Run(pass, 1, surface.Constant(1));
        auto c = gpu.device->createCommandList(); c->open();
        c->clearTextureFloat(surface.depth, nvrhi::AllSubresources, nvrhi::Color(0.25f));
        c->clearTextureFloat(surface.motion, nvrhi::AllSubresources, nvrhi::Color(0, 0, 0.25f, 1));
        gpu.Submit(c);
        surface.Run(pass, 2, surface.Constant(3));
        const auto temporal = gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Temporal));
        Near(temporal[0], 2, 2e-4f, "depth motion must compare in the previous frame");
        c = gpu.device->createCommandList(); c->open();
        c->clearTextureFloat(surface.motion, nvrhi::AllSubresources, nvrhi::Color(100, 0, 0, 1));
        gpu.Submit(c);
        surface.Run(pass, 3, surface.Constant(7));
        Near(gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Moments))[2], 1, 1e-5f, "offscreen history rejection");
        std::cout << "previous-depth conversion and offscreen motion passed\n";
    }

    void DerivativeGuides(Gpu& gpu)
    {
        for (uint32_t stride : {1u, 2u, 4u})
        {
            Surface surface(gpu, 65, 49, stride); SvgfPass pass(gpu.device, gpu.shaders);
            std::vector<float> depth(65u * 49u);
            for (uint32_t y = 0; y < 49; ++y)
            for (uint32_t x = 0; x < 65; ++x) depth[y * 65 + x] = 1.f / (2.f + 0.02f * float(x));
            auto c = gpu.device->createCommandList(); c->open();
            c->writeTexture(surface.depth, 0, 0, depth.data(), 65u * sizeof(float));
            c->clearTextureFloat(surface.derivatives, nvrhi::AllSubresources, nvrhi::Color(0.02f, 0, 0, 0));
            gpu.Submit(c);
            auto raw = surface.Constant(1);
            uint32_t random = 7919;
            double rawError = 0, filteredError = 0;
            for (auto& pixel : raw)
            {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                const float noise = float(random & 65535u) / 65535.f * 0.6f - 0.3f;
                pixel = dm::float4(1 + noise, 1 + noise, 1 + noise, 1);
                rawError += noise * noise;
            }
            const auto output = gpu.Floats(surface.Run(pass, 1, raw));
            for (size_t i = 0; i < raw.size(); ++i) filteredError += std::pow(output[i * 4] - 1, 2);
            Require(filteredError < rawError * 0.2, "native-grid slope derivatives failed to connect one plane");
        }
        Surface surface(gpu); SvgfPass pass(gpu.device, gpu.shaders);
        surface.Run(pass, 1, surface.Constant(1));
        auto c = gpu.device->createCommandList(); c->open();
        c->clearTextureFloat(surface.normal, nvrhi::AllSubresources, nvrhi::Color(0.3f, 0, -0.9539392f, 1));
        c->clearTextureFloat(surface.derivatives, nvrhi::AllSubresources, nvrhi::Color(0, 0.1f, 0, 0));
        gpu.Submit(c);
        surface.Run(pass, 2, surface.Constant(3));
        Near(gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Moments))[2],
            2, 1e-5f, "normal derivative must scale reprojection tolerance");
        c = gpu.device->createCommandList(); c->open();
        c->clearTextureFloat(surface.normal, nvrhi::AllSubresources, nvrhi::Color(0, 0, -1, 1));
        c->clearTextureFloat(surface.derivatives, nvrhi::AllSubresources, nvrhi::Color(0));
        gpu.Submit(c);
        surface.Run(pass, 3, surface.Constant(7));
        Near(gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Moments))[2],
            1, 1e-5f, "zero normal derivative must reject the same normal change");
        std::cout << "slope derivatives at native strides 1/2/4 and normal rejection passed\n";
    }

    void NoiseAndEdges(Gpu& gpu)
    {
        Surface surface(gpu, 65, 49); SvgfPass pass(gpu.device, gpu.shaders);
        std::vector<float> depths(size_t(surface.width) * surface.height);
        std::vector<dm::float4> normals(depths.size()), raw(depths.size());
        uint32_t random = 0x927ad4u;
        double rawError = 0, outputError = 0, bias = 0;
        for (uint64_t frame = 1; frame <= 32; ++frame)
        {
            const uint32_t edge = frame < 17 ? 32u : 34u;
            for (uint32_t y = 0; y < surface.height; ++y)
            for (uint32_t x = 0; x < surface.width; ++x)
            {
                const size_t i = size_t(y) * surface.width + x;
                const float truth = x < edge ? 0.2f : 0.8f;
                depths[i] = x < edge ? 0.5f : 0.25f;
                normals[i] = x < edge ? dm::float4(0, 0, -1, 1) : dm::float4(0.8f, 0, -0.6f, 1);
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                const float sample = truth + (float(random & 0xffff) / 65535.f - 0.5f) * 0.35f;
                raw[i] = dm::float4(sample, sample, sample, 1);
            }
            auto c = gpu.device->createCommandList(); c->open();
            c->writeTexture(surface.depth, 0, 0, depths.data(), surface.width * sizeof(float));
            c->writeTexture(surface.normal, 0, 0, normals.data(), surface.width * sizeof(dm::float4));
            gpu.Submit(c);
            auto output = gpu.Floats(surface.Run(pass, frame, raw));
            for (uint32_t y = 0; y < surface.height; ++y)
            for (uint32_t x = 0; x < surface.width; ++x)
            {
                const size_t i = size_t(y) * surface.width + x;
                const double truth = x < edge ? 0.2 : 0.8;
                Require(std::isfinite(output[i * 4]), "nonfinite filtered observation");
                if (frame == 17 && (x == 32 || x == 33))
                    Near(output[i * 4], float(truth), 0.09f, "newly visible surface retained bright history");
                if (frame == 32)
                {
                    rawError += (raw[i].x - truth) * (raw[i].x - truth);
                    outputError += (output[i * 4] - truth) * (output[i * 4] - truth);
                    bias += output[i * 4] - truth;
                }
            }
        }
        Require(outputError < rawError * 0.08, "insufficient noise reduction against analytic surface truth");
        Require(std::abs(bias / double(raw.size())) < 0.01, "mean illumination drift");
        std::cout << "noise MSE ratio " << outputError / rawError << ", mean bias " << bias / double(raw.size()) << "; moving edge passed\n";
    }

    void MaterialIdentityAndSignals(Gpu& gpu)
    {
        Surface surface(gpu, 17, 19); SvgfPass pass(gpu.device, gpu.shaders);
        surface.inputs.albedo = surface.albedo;
        surface.inputs.emission = surface.emission;
        std::vector<dm::float4> albedo(size_t(surface.width) * surface.height), emission(albedo.size()), radiance(albedo.size());
        for (size_t i = 0; i < albedo.size(); ++i)
        {
            const float a = i % 2 ? 0.001f : 0.8f;
            albedo[i] = dm::float4(a, 0.2f, 0.4f, 1);
            emission[i] = dm::float4(i % 3 ? 0 : 4.f, 0, 0, 1);
            radiance[i] = dm::float4(a * 2 + emission[i].x, 0.4f, 0.8f, 1);
        }
        auto c = gpu.device->createCommandList(); c->open();
        c->writeTexture(surface.albedo, 0, 0, albedo.data(), surface.width * sizeof(dm::float4));
        c->writeTexture(surface.emission, 0, 0, emission.data(), surface.width * sizeof(dm::float4));
        gpu.Submit(c);
        auto output = gpu.Floats(surface.Run(pass, 1, radiance, DenoiserSignalType::PathTracing));
        for (size_t i = 0; i < radiance.size(); ++i)
        for (size_t channel = 0; channel < 3; ++channel)
            Near(output[i * 4 + channel], radiance[i][int(channel)], 5e-4f, "material/emission identity");
        const std::vector<dm::float4> failed(radiance.size(), dm::float4(0.f));
        output = gpu.Floats(surface.Run(pass, 2, failed, DenoiserSignalType::PathTracing));
        const auto moments = gpu.Floats(pass.DebugImage(DenoiserSignalType::PathTracing, SvgfDebugImage::Moments));
        for (size_t i = 0; i < radiance.size(); ++i)
        {
            for (size_t channel = 0; channel < 3; ++channel)
                Near(output[i * 4 + channel], radiance[i][int(channel)], 5e-4f, "failed transport must preserve valid illumination and emission");
            Near(moments[i * 4 + 2], 1, 1e-5f, "failed transport must not advance history");
        }
        surface.inputs.albedo = surface.inputs.emission = nullptr;
        for (uint64_t frame = 2; frame <= 3; ++frame)
        for (uint32_t signal = 0; signal < uint32_t(DenoiserSignalType::Count); ++signal)
        {
            const auto type = DenoiserSignalType(signal);
            const float value = float(signal + 1) * 0.1f;
            output = gpu.Floats(surface.Run(pass, frame, surface.Constant(value), type));
            // the path signal already has a distinct prior observation; reset it
            // explicitly so this check isolates cross-signal ownership.
            if (frame == 2 && type == DenoiserSignalType::PathTracing) { pass.DisableSignal(type); continue; }
            Near(output[0], value, 2e-4f, "histories leaked between effects");
        }
        Require(pass.DebugImage(DenoiserSignalType::AmbientOcclusion, SvgfDebugImage::Surface) ==
                pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Surface) &&
            pass.DebugImage(DenoiserSignalType::AmbientOcclusion, SvgfDebugImage::Moments) !=
                pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Moments),
            "identical surface guides must share storage while signal history remains independent");
        std::cout << "material reconstruction identity and independent signal histories passed\n";
    }

    void RareVisibility(Gpu& gpu)
    {
        Surface surface(gpu, 97, 65); SvgfPass pass(gpu.device, gpu.shaders);
        auto raw = surface.Constant(0);
        uint32_t random = 0x592789fu;
        nvrhi::ITexture* result = nullptr;
        constexpr double truth = 0.02;
        for (uint64_t frame = 1; frame <= 64; ++frame)
        {
            for (auto& sample : raw)
            {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                const float value = double(random) / 4294967296.0 < truth ? 1.f : 0.f;
                sample = dm::float4(value, value, value, 1);
            }
            result = surface.Run(pass, frame, raw, DenoiserSignalType::SkyVisibility);
        }
        const auto output = gpu.Floats(result);
        double mean = 0, error = 0;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            mean += output[i * 4] / double(raw.size());
            error += std::pow(output[i * 4] - truth, 2) / double(raw.size());
        }
        Require(std::abs(mean - truth) < 0.002 && error < 0.000025,
            "rare visible sky samples lost energy or formed persistent low-frequency noise");
        std::cout << "two-percent visibility mean " << mean << ", MSE " << error << '\n';
    }

    void NativeGrid(Gpu& gpu)
    {
        for (uint32_t stride : {1u, 2u, 4u})
        {
            Surface surface(gpu, 35, 29, stride); SvgfPass pass(gpu.device, gpu.shaders);
            for (uint64_t frame = 1; frame <= 2; ++frame)
                surface.Run(pass, frame, surface.Constant(0.6f));
            const auto correspondence = gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Reprojection));
            const auto moments = gpu.Floats(pass.DebugImage(DenoiserSignalType::DiffuseGi, SvgfDebugImage::Moments));
            for (uint32_t y = 0; y < surface.height; ++y)
            for (uint32_t x = 0; x < surface.width; ++x)
            {
                size_t i = (size_t(y) * surface.width + x) * 4;
                Near(correspondence[i], float(x), 1e-5f, "native producer x correspondence");
                Near(correspondence[i + 1], float(y), 1e-5f, "native producer y correspondence");
                Near(moments[i + 2], 2, 1e-5f, "odd native-grid history");
            }
        }
        std::cout << "full, half, and quarter producer grids with odd boundaries passed\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "usage: uvsr_denoising_gpu_tests shader-directory");
        Gpu gpu(argv[1]);
        TemporalAnswers(gpu);
        MotionAnswers(gpu);
        DerivativeGuides(gpu);
        NoiseAndEdges(gpu);
        MaterialIdentityAndSignals(gpu);
        RareVisibility(gpu);
        NativeGrid(gpu);
        std::cout << "SVGF GPU checks passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
