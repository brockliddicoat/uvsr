#pragma once

#include "denoising_settings.h"
#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <array>
#include <memory>
#include <vector>

namespace donut::engine { class IView; }

namespace uvsr
{
    class RendererShaderFactory;

    // one current observation per texel. surface textures describe the exact
    // producer sample at min(texel * stride + stride / 2, surfaceExtent - 1).
    struct SvgfInputs
    {
        nvrhi::ITexture* radiance = nullptr;
        nvrhi::ITexture* deviceDepth = nullptr;
        nvrhi::ITexture* worldNormal = nullptr;
        // x is max(abs(ddx(linear view Z)), abs(ddy(linear view Z))).
        // y is length(fwidth(world normal)). produced on the receiving surface.
        nvrhi::ITexture* derivatives = nullptr;
        // engine motion: xy previous-minus-current in pixels, z device-depth
        // difference, w validity. conversion to the reference happens once.
        nvrhi::ITexture* pixelMotion = nullptr;
        nvrhi::ITexture* albedo = nullptr;
        nvrhi::ITexture* emission = nullptr;
        const donut::engine::IView* view = nullptr;
        const donut::engine::IView* previousView = nullptr;
        dm::uint2 sampleStride = dm::uint2(1u);
        uint64_t frame = 0;
    };

    struct SvgfResult { nvrhi::ITexture* texture = nullptr; bool denoised = false; };
    enum class SvgfDebugImage { Surface, Reprojection, Moments, Temporal, Feedback, Output };

    class SvgfPass final
    {
    public:
        SvgfPass(nvrhi::IDevice*, const std::shared_ptr<RendererShaderFactory>&);
        SvgfResult Process(DenoiserSignalType, nvrhi::ICommandList*, const DenoisingSignalSettings&, const SvgfInputs&);
        void DisableSignal(DenoiserSignalType) noexcept;
        void RequestHistoryReset() noexcept;
        void ReleaseResources() noexcept;
        bool IsOperational() const noexcept { return m_Ready; }
        nvrhi::ITexture* DebugImage(DenoiserSignalType, SvgfDebugImage) const noexcept;

    private:
        struct Guides
        {
            dm::uint2 extent = dm::uint2::zero();
            dm::uint2 surfaceExtent = dm::uint2::zero();
            dm::uint2 stride = dm::uint2::zero();
            std::array<nvrhi::ITexture*, 4> inputs{};
            std::array<nvrhi::TextureHandle, 2> surface;
            nvrhi::TextureHandle reprojection;
            uint64_t frame = ~uint64_t(0);
            uint32_t current = 0;
        };
        struct Signal
        {
            dm::uint2 extent = dm::uint2::zero();
            std::shared_ptr<Guides> guides;
            std::array<nvrhi::TextureHandle, 2> moments;
            std::array<nvrhi::TextureHandle, 3> filtered;
            nvrhi::TextureHandle temporal, output;
            std::array<nvrhi::ITexture*, 7> inputs{};
            struct Binding
            {
                std::array<nvrhi::ITexture*, 12> textures{};
                nvrhi::BindingSetHandle set;
            };
            std::vector<Binding> bindings;
            DenoisingSignalSettings settings;
            uint64_t frame = ~uint64_t(0);
            uint32_t current = 0;
            uint32_t feedback = 0;
            uint32_t result = 0;
            bool material = false;
            bool valid = false;
        };
        struct Pipeline { nvrhi::ComputePipelineHandle handle; };
        nvrhi::DeviceHandle m_Device;
        nvrhi::BufferHandle m_Constants;
        nvrhi::BindingLayoutHandle m_Layout;
        std::array<Pipeline, 5> m_Pipelines;
        std::array<Signal, size_t(DenoiserSignalType::Count)> m_Signals;
        std::vector<std::shared_ptr<Guides>> m_Guides;
        nvrhi::TextureHandle m_Zero;
        bool m_Ready = false;
        bool m_Initialized = false;
        nvrhi::TextureHandle Texture(dm::uint2, nvrhi::Format, const char*);
        bool Dispatch(Signal&, nvrhi::ICommandList*, uint32_t, const void*,
            const std::array<nvrhi::ITexture*, 10>&, nvrhi::ITexture*, nvrhi::ITexture*, dm::uint2);
    };
}
