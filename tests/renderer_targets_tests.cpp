#include "renderer_targets_nvrhi.h"
#include "renderer_target_layout.h"
#include "renderer_gpu_contract.h"

#include <stdio.h>
#include <stdlib.h>

namespace uvsr
{
    struct RenderTargetsTestAccess
    {
        static bool ReverseProjection(const RenderTargets& targets) noexcept
        {
            return targets.m_UseReverseProjection;
        }
    };
}

namespace
{
    using namespace uvsr;
    void Require(bool value, const char* message)
    {
        if (!value)
        {
            fprintf(stderr, "renderer target test failed: %s\n", message);
            exit(EXIT_FAILURE);
        }
    }

    void RequirePickingFlags(const RenderTargets& targets)
    {
        Require(targets.MaterialIDs && targets.MaterialIDs->getDesc().isUAV &&
            targets.MaterialIDs->getDesc().format == nvrhi::Format::RG32_UINT,
            "picking color requires an exact integer UAV clear");
        Require(targets.MaterialIDDepth && !targets.MaterialIDDepth->getDesc().isUAV,
            "picking depth must not inherit color UAV access");
    }

    struct Snapshot
    {
        const void* handles[15];
        DirectX::XMUINT2 size, presentation;
        bool raster, reverse, valid;
        explicit Snapshot(const RenderTargets& targets) :
            handles{ targets.Depth.Get(), targets.GBufferDiffuse.Get(), targets.GBufferSpecular.Get(),
                targets.GBufferNormals.Get(), targets.GBufferEmissive.Get(), targets.HdrColor.Get(),
                targets.LdrColor.Get(), targets.MaterialIDs.Get(), targets.MaterialIDDepth.Get(),
                targets.MaterialAmbientOcclusion.Get(), targets.GBufferFramebuffer.Get(),
                targets.HdrFramebuffer.Get(), targets.LdrFramebuffer.Get(), targets.MaterialIDFramebuffer.Get(),
                targets.Heap.Get() },
            size(targets.GetSize()), presentation(targets.GetPresentationSize()),
            raster(targets.RasterLightingEnabled), reverse(RenderTargetsTestAccess::ReverseProjection(targets)),
            valid(targets.IsValid()) {}

        void RequireUnchanged(const RenderTargets& targets) const
        {
            const Snapshot after(targets);
            for (size_t index = 0; index < sizeof(handles) / sizeof(handles[0]); ++index)
                Require(handles[index] == after.handles[index], "failed replacement changed an existing handle");
            Require(size.x == after.size.x && size.y == after.size.y &&
                presentation.x == after.presentation.x && presentation.y == after.presentation.y &&
                raster == after.raster && reverse == after.reverse && valid == after.valid,
                "failed replacement changed existing metadata");
        }
    };

    struct FailureScope
    {
        FailureScope(RendererTargetTestOperation operation, unsigned occurrence)
        {
            SetRendererTargetTestFailure(operation, occurrence);
        }
        ~FailureScope() { SetRendererTargetTestFailure(RendererTargetTestOperation::Texture, 0); }
        FailureScope(const FailureScope&) = delete;
        FailureScope& operator=(const FailureScope&) = delete;
    };

    class RetiredTargetReferences
    {
    public:
        explicit RetiredTargetReferences(const RenderTargets& targets)
        {
            // release framebuffers before their attachments, then the placed heap.
            Add(targets.GBufferFramebuffer); Add(targets.HdrFramebuffer);
            Add(targets.LdrFramebuffer); Add(targets.MaterialIDFramebuffer);
            Add(targets.Depth); Add(targets.GBufferDiffuse); Add(targets.GBufferSpecular);
            Add(targets.GBufferNormals); Add(targets.GBufferEmissive); Add(targets.HdrColor);
            Add(targets.LdrColor); Add(targets.MaterialIDs); Add(targets.MaterialIDDepth);
            Add(targets.MaterialAmbientOcclusion); Add(targets.Heap);
        }
        unsigned ReleaseAfterCompletion()
        {
            for (unsigned index = 0; index < m_Count; ++index)
            {
                Require(m_Resources[index]->GetRefCount() == 1,
                    "completed target retained an owner outside the probe");
                Require(m_Resources[index]->Release() == 0, "last target reference did not destroy its object");
                m_Resources[index] = nullptr;
            }
            return m_Count;
        }
        ~RetiredTargetReferences()
        {
            for (unsigned index = 0; index < m_Count; ++index)
                if (m_Resources[index]) m_Resources[index]->Release();
        }
        RetiredTargetReferences(const RetiredTargetReferences&) = delete;
        RetiredTargetReferences& operator=(const RetiredTargetReferences&) = delete;
    private:
        void Add(nvrhi::IResource* resource)
        {
            if (!resource) return;
            for (unsigned index = 0; index < m_Count; ++index)
                if (m_Resources[index] == resource) return;
            Require(m_Count < 15, "target probe capacity exceeded");
            resource->AddRef();
            m_Resources[m_Count++] = resource;
        }
        nvrhi::IResource* m_Resources[15]{};
        unsigned m_Count = 0;
    };

    void TestPlacement()
    {
        uint64_t capacity = 0, offset = 17;
        Require(AppendRendererTargetPlacement(64, 64, capacity, offset) &&
            offset == 0 && capacity == 64, "first aligned placement");
        capacity = 65;
        Require(AppendRendererTargetPlacement(64, 64, capacity, offset) &&
            offset == 128 && capacity == 192, "second placement padding or overlap");
        struct Invalid { uint64_t size, alignment, capacity; };
        const Invalid cases[] = {
            {0, 64, 0}, {UINT64_MAX, 64, 0}, {64, 0, 0}, {64, 3, 0},
            {1, 64, UINT64_MAX - 31}, {UINT64_MAX - 63, 64, 64}
        };
        for (const auto& test : cases)
        {
            capacity = test.capacity;
            offset = 17;
            Require(!AppendRendererTargetPlacement(test.size, test.alignment, capacity, offset) &&
                capacity == test.capacity && offset == 17, "invalid layout was accepted or mutated output");
        }
    }
}

void TestRendererTargets(nvrhi::IDevice* device)
{
    TestPlacement();
    Require(device->queryFeatureSupport(nvrhi::Feature::VirtualResources),
        "this DX12 fixture requires the production placed-resource path");
    unsigned failures = 0;
    for (unsigned mode = 0; mode != 2; ++mode)
    {
        const bool raster = mode != 0;
        const unsigned counts[] = { raster ? 8u : 1u, 1u, raster ? 2u : 1u, raster ? 3u : 1u };
        RenderTargets targets;
        Require(targets.Init(device, {32, 24}, {40, 30}, true, raster), "initial target creation");
        Require(targets.EnsureMaterialPickingTargets(device), "initial picking creation");
        RequirePickingFlags(targets);
        const Snapshot before(targets);
        for (unsigned kind = 0; kind != 4; ++kind)
            for (unsigned ordinal = 1; ordinal <= counts[kind]; ++ordinal)
            {
                FailureScope fail(static_cast<RendererTargetTestOperation>(kind), ordinal);
                Require(!targets.Init(device, {48, 40}, {56, 48}, false, raster),
                    "injected target allocation failure was ignored");
                Require(WasRendererTargetTestFailureReached(), "failure ordinal was not exercised");
                before.RequireUnchanged(targets);
                ++failures;
            }
        Require(targets.Init(device, {48, 40}, {56, 48}, false, !raster), "replacement after failures");
        Require(targets.IsValid() && targets.GetSize().x == 48 && targets.GetSize().y == 40 &&
            targets.GetPresentationSize().x == 56 && targets.GetPresentationSize().y == 48 &&
            targets.RasterLightingEnabled == !raster && !RenderTargetsTestAccess::ReverseProjection(targets) &&
            !targets.MaterialIDs && !targets.MaterialIDDepth && !targets.MaterialIDFramebuffer &&
            bool(targets.Depth) == !raster && bool(targets.HdrColor) == !raster,
            "replacement retained stale mode, metadata or picking resources");

        // lazy picking has its own transaction, including a depth target in path mode.
        const Snapshot beforePicking(targets);
        const unsigned pickingTextureCount = targets.RasterLightingEnabled ? 1u : 2u;
        for (unsigned ordinal = 1; ordinal <= pickingTextureCount + 1; ++ordinal)
        {
            const bool framebuffer = ordinal > pickingTextureCount;
            FailureScope fail(framebuffer ? RendererTargetTestOperation::Framebuffer :
                RendererTargetTestOperation::Texture, framebuffer ? 1u : ordinal);
            Require(!targets.EnsureMaterialPickingTargets(device), "injected picking failure was ignored");
            Require(WasRendererTargetTestFailureReached(), "picking failure ordinal was not exercised");
            beforePicking.RequireUnchanged(targets);
            ++failures;
        }
        Require(targets.EnsureMaterialPickingTargets(device), "picking retry after failures");
        RequirePickingFlags(targets);
        Require(targets.MaterialIDs && targets.MaterialIDDepth && targets.MaterialIDFramebuffer,
            "picking retry did not publish all resources");
        device->runGarbageCollection();
    }
    printf("renderer targets: %u partial-creation failures, 2 mode replacements, checked heap layout passed\n", failures);

    unsigned destroyed = 0, maximumRetiredObjects = 0;
    RenderTargets current;
    Require(current.Init(device, {32, 24}, {40, 30}, true, true), "transition baseline creation");
    for (unsigned cycle = 0; cycle < 16; ++cycle)
        for (unsigned step = 0; step < 4; ++step)
        {
            const Snapshot unchanged(current);
            Require(!current.IsUpdateRequired(current.GetSize(), current.GetPresentationSize(), current.RasterLightingEnabled),
                "stable target input requested replacement");
            unchanged.RequireUnchanged(current);
            Require(current.EnsureMaterialPickingTargets(device), "transition picking creation");
            RequirePickingFlags(current);
            auto commands = device->createCommandList();
            Require(bool(commands), "transition command creation");
            commands->open();
            commands->clearTextureFloat(current.LdrColor, nvrhi::AllSubresources, nvrhi::Color(float(cycle) / 16));
            commands->clearTextureUInt(current.MaterialIDs, nvrhi::AllSubresources, RendererInvalidPickId);
            commands->close();
            Require(device->executeCommandList(commands) != 0, "transition command submission");
            RetiredTargetReferences retired(current);
            const bool raster = step != 0;
            const DirectX::XMUINT2 size = step == 1 ? DirectX::XMUINT2{48, 40} : DirectX::XMUINT2{32, 24};
            Require(current.Init(device, size, {40, 30}, true, raster), "transition replacement creation");
            Require(device->waitForIdle(), "transition completion failed");
            commands = nullptr;
            device->runGarbageCollection();
            const unsigned released = retired.ReleaseAfterCompletion();
            destroyed += released;
            if (released > maximumRetiredObjects) maximumRetiredObjects = released;
        }
    RetiredTargetReferences finalGeneration(current);
    current = RenderTargets{};
    device->runGarbageCollection();
    destroyed += finalGeneration.ReleaseAfterCompletion();
    printf("renderer target lifetime: 64 submitted replacement steps, maximum%u retired objects, %u exact last-reference destructions\n",
        maximumRetiredObjects, destroyed);
}
