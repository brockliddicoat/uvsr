#include "hierarchical_depth.h"
#include <donut/core/math/math.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <algorithm>

using namespace donut::math;
#include "hierarchical_depth_cb.h"

static_assert(sizeof(HierarchicalDepthConstants) == 48u);

namespace uvsr
{
    HierarchicalDepthPass::HierarchicalDepthPass(nvrhi::IDevice* device,
        const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory)
        : m_Device(device)
    {
        for (auto& query : m_Queries) query = device->createTimerQuery();
        nvrhi::BindingLayoutDesc layout;
        layout.visibility = nvrhi::ShaderType::Compute;
        layout.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_Layout = device->createBindingLayout(layout);
        nvrhi::BufferDesc buffer;
        buffer.byteSize = sizeof(HierarchicalDepthConstants);
        buffer.debugName = "HierarchicalDepth/Constants";
        buffer.isConstantBuffer = true;
        buffer.isVolatile = true;
        buffer.maxVersions = 128;
        m_Constants = device->createBuffer(buffer);
        nvrhi::ComputePipelineDesc pipeline;
        pipeline.CS = shaderFactory->CreateShader(
            "uvsr/hierarchical_depth_cs.hlsl", "main", nullptr,
            nvrhi::ShaderType::Compute);
        pipeline.bindingLayouts = { m_Layout };
        m_Pipeline = device->createComputePipeline(pipeline);
    }

    nvrhi::ITexture* HierarchicalDepthPass::Render(
        nvrhi::ICommandList* commandList, nvrhi::ITexture* depth,
        const donut::engine::IView& view)
    {
        if (!commandList || !depth || !m_Pipeline ||
            depth->getDesc().sampleCount != 1u)
            return nullptr;
        const auto& source = depth->getDesc();
        if (!source.width || !source.height)
            return nullptr;
        if (!m_Bounds || m_Source != depth)
        {
            nvrhi::TextureDesc desc;
            desc.width = source.width;
            desc.height = source.height;
            desc.format = nvrhi::Format::RG32_FLOAT;
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = "HierarchicalDepth/MinMax";
            desc.mipLevels = 1u;
            for (uint32_t extent = std::max(desc.width, desc.height);
                extent > 1u; extent >>= 1u)
                ++desc.mipLevels;
            m_Bounds = m_Device->createTexture(desc);
            m_Source = depth;
            m_Bindings.clear();
            m_TextureBytes = 0u;
            for (uint32_t mip = 0u; mip < desc.mipLevels; ++mip)
            {
                m_TextureBytes += uint64_t(std::max(desc.width >> mip, 1u)) *
                    std::max(desc.height >> mip, 1u) * 8u;
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_Constants),
                    nvrhi::BindingSetItem::Texture_SRV(0, depth),
                    nvrhi::BindingSetItem::Texture_SRV(1,
                        mip ? m_Bounds.Get() : depth)
                        .setSubresources(nvrhi::TextureSubresourceSet(
                            mip ? mip - 1u : 0u, 1u, 0u, 1u)),
                    nvrhi::BindingSetItem::Texture_UAV(0, m_Bounds)
                        .setSubresources(nvrhi::TextureSubresourceSet(
                            mip, 1u, 0u, 1u))
                };
                m_Bindings.push_back(m_Device->createBindingSet(bindings, m_Layout));
            }
        }

        const auto projection = view.GetProjectionMatrix(true);
        HierarchicalDepthConstants constants{};
        constants.projectionDepth = float4(projection[2][2], projection[2][3],
            projection[3][2], projection[3][3]);
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        const uint32_t timerSlot = m_Frame++ % uint32_t(m_Queries.size());
        if (m_Pending[timerSlot] && m_Device->pollTimerQuery(m_Queries[timerSlot]))
        {
            m_Milliseconds = m_Device->getTimerQueryTime(m_Queries[timerSlot]) * 1000.f;
            m_HasTiming = true;
            m_Device->resetTimerQuery(m_Queries[timerSlot]);
            m_Pending[timerSlot] = false;
        }
        const bool measure = !m_Pending[timerSlot];
        commandList->beginMarker("Shared Hierarchical Depth");
        if (measure) commandList->beginTimerQuery(m_Queries[timerSlot]);
        for (uint32_t mip = 0u; mip < m_Bindings.size(); ++mip)
        {
            constants.initialize = mip == 0u ? 1u : 0u;
            const uint32_t previousMip = mip ? mip - 1u : 0u;
            constants.sourceSize = uint2(std::max(source.width >> previousMip, 1u),
                std::max(source.height >> previousMip, 1u));
            constants.outputSize = uint2(std::max(source.width >> mip, 1u),
                std::max(source.height >> mip, 1u));
            commandList->writeBuffer(m_Constants, &constants, sizeof(constants));
            nvrhi::ComputeState state;
            state.pipeline = m_Pipeline;
            state.bindings = { m_Bindings[mip] };
            commandList->setComputeState(state);
            commandList->dispatch((constants.outputSize.x + 7u) / 8u,
                (constants.outputSize.y + 7u) / 8u, 1u);
        }
        if (measure)
        {
            commandList->endTimerQuery(m_Queries[timerSlot]);
            m_Pending[timerSlot] = true;
        }
        commandList->endMarker();
        return m_Bounds;
    }
}
