#include "hierarchical_depth.h"

#include <donut/app/DeviceManager.h>
#include <donut/core/math/math.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct Bounds
    {
        float minimum = std::numeric_limits<float>::max();
        float maximum = -std::numeric_limits<float>::max();
    };

    struct Level
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        std::vector<Bounds> values;
    };

    struct TestCase
    {
        const char* name = nullptr;
        uint32_t width = 0u;
        uint32_t height = 0u;
        bool reverseDepth = false;
        bool orthographic = false;
        float nearPlane = 0.f;
        float farPlane = 0.f;
        donut::math::float4x4 projection;
        std::vector<float> deviceDepth;
    };

    // this is the exact 32-byte StructuredBuffer layout declared by the
    // wrapper shader's TraceRayRecord.
    struct GpuTraceRay
    {
        float beginPixel[2] = {};
        float endPixel[2] = {};
        float beginDepth = 0.f;
        float endDepth = 0.f;
        uint32_t orthographic = 0u;
        uint32_t padding = 0u;
    };

    struct alignas(16) TraceConstants
    {
        uint32_t fullSize[2] = {};
        uint32_t mipCount = 0u;
        float thickness = 0.f;
        uint32_t rayCount = 0u;
        uint32_t padding[3] = {};
    };

    struct TraceDataset
    {
        const char* name = nullptr;
        uint32_t width = 0u;
        uint32_t height = 0u;
        double thickness = 0.0;
        bool requireMoreThan512Steps = false;
        uint32_t expectedHitCount = 0u;
        std::vector<double> leafDepths;
        std::vector<GpuTraceRay> rays;
    };

    struct DenseTraceResult
    {
        bool hit = false;
        uint64_t steps = 0u;
    };

    static_assert(sizeof(GpuTraceRay) == 32u);
    static_assert(offsetof(GpuTraceRay, beginPixel) == 0u);
    static_assert(offsetof(GpuTraceRay, endPixel) == 8u);
    static_assert(offsetof(GpuTraceRay, beginDepth) == 16u);
    static_assert(offsetof(GpuTraceRay, endDepth) == 20u);
    static_assert(offsetof(GpuTraceRay, orthographic) == 24u);
    static_assert(offsetof(GpuTraceRay, padding) == 28u);
    static_assert(sizeof(TraceConstants) == 32u);
    static_assert(offsetof(TraceConstants, fullSize) == 0u);
    static_assert(offsetof(TraceConstants, mipCount) == 8u);
    static_assert(offsetof(TraceConstants, thickness) == 12u);
    static_assert(offsetof(TraceConstants, rayCount) == 16u);
    static_assert(offsetof(TraceConstants, padding) == 20u);

    bool Check(bool condition, const std::string& message)
    {
        if (!condition)
            std::cerr << "hierarchical depth GPU validation failed: "
                << message << '\n';
        return condition;
    }

    std::filesystem::path FindAppShaderDirectory(
        const std::filesystem::path& shaderRoot)
    {
        const std::filesystem::path candidates[] = {
            shaderRoot / "uvsr" / "dxil",
            shaderRoot / "core" / "dxil",
            shaderRoot / "dxil",
            shaderRoot
        };
        for (const auto& candidate : candidates)
        {
            if (std::filesystem::is_regular_file(
                    candidate / "hierarchical_depth_cs.bin"))
                return candidate;
        }
        return {};
    }

    Bounds Linearize(float depth, const TestCase& test)
    {
        Bounds result;
        const bool inDeviceRange = test.reverseDepth
            ? depth > 0.f && depth <= 1.f
            : depth >= 0.f && depth < 1.f;
        if (!std::isfinite(depth) || !inDeviceRange)
            return result;

        float z = 0.f;
        if (test.orthographic)
            z = std::abs(test.nearPlane +
                depth * (test.farPlane - test.nearPlane));
        else if (test.reverseDepth)
            z = test.nearPlane / depth;
        else
            z = test.nearPlane * test.farPlane /
                (test.farPlane - depth *
                    (test.farPlane - test.nearPlane));
        if (std::isfinite(z) && z > 0.f)
            result = { z, z };
        return result;
    }

    std::vector<Level> BuildReference(const TestCase& test)
    {
        Level leaves;
        leaves.width = test.width;
        leaves.height = test.height;
        leaves.values.reserve(test.deviceDepth.size());
        for (const float depth : test.deviceDepth)
            leaves.values.push_back(Linearize(depth, test));

        std::vector<Level> levels;
        levels.push_back(std::move(leaves));
        while (levels.back().width > 1u || levels.back().height > 1u)
        {
            const Level& source = levels.back();
            Level output;
            output.width = std::max(source.width >> 1u, 1u);
            output.height = std::max(source.height >> 1u, 1u);
            output.values.resize(size_t(output.width) * output.height);
            for (uint32_t y = 0u; y < output.height; ++y)
            {
                uint32_t endY = std::min(y * 2u + 2u, source.height);
                if (y + 1u == output.height)
                    endY = source.height;
                for (uint32_t x = 0u; x < output.width; ++x)
                {
                    uint32_t endX = std::min(x * 2u + 2u, source.width);
                    if (x + 1u == output.width)
                        endX = source.width;
                    Bounds& destination =
                        output.values[size_t(y) * output.width + x];
                    for (uint32_t childY = y * 2u;
                        childY < endY; ++childY)
                    {
                        for (uint32_t childX = x * 2u;
                            childX < endX; ++childX)
                        {
                            const Bounds& child = source.values[
                                size_t(childY) * source.width + childX];
                            destination.minimum = std::min(
                                destination.minimum, child.minimum);
                            destination.maximum = std::max(
                                destination.maximum, child.maximum);
                        }
                    }
                }
            }
            levels.push_back(std::move(output));
        }
        return levels;
    }

    bool NearlyEqual(float actual, float expected)
    {
        if (actual == expected)
            return true;
        const float scale = std::max({ 1.f, std::abs(actual),
            std::abs(expected) });
        return std::abs(actual - expected) <= 2e-5f * scale;
    }

    bool RunCase(nvrhi::IDevice* device, uvsr::HierarchicalDepthPass& pass,
        const TestCase& test)
    {
        nvrhi::TextureDesc depthDesc;
        depthDesc.width = test.width;
        depthDesc.height = test.height;
        depthDesc.format = nvrhi::Format::R32_FLOAT;
        depthDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        depthDesc.keepInitialState = true;
        depthDesc.debugName = std::string("HierarchyGpuTest/") + test.name;
        nvrhi::TextureHandle depth = device->createTexture(depthDesc);
        if (!Check(bool(depth), std::string(test.name) +
                ": create input texture"))
            return false;

        donut::engine::PlanarView view;
        view.SetViewport(nvrhi::Viewport(
            float(test.width), float(test.height)));
        view.SetMatrices(donut::math::affine3::identity(), test.projection);
        view.UpdateCache();
        const bool reverseDepth = view.IsReverseDepth();
        bool passed = Check(reverseDepth == test.reverseDepth,
            std::string(test.name) + ": reverse-depth view classification");
        passed &= Check(view.IsOrthographicProjection() == test.orthographic,
            std::string(test.name) + ": projection view classification");
        const std::vector<Level> expected = BuildReference(test);

        nvrhi::CommandListHandle commandList = device->createCommandList();
        if (!Check(bool(commandList), std::string(test.name) +
                ": create command list"))
            return false;
        commandList->open();
        commandList->writeTexture(depth, 0u, 0u, test.deviceDepth.data(),
            size_t(test.width) * sizeof(float));
        nvrhi::ITexture* hierarchy = pass.Render(commandList, depth, view);
        if (!Check(hierarchy != nullptr, std::string(test.name) +
                ": hierarchy pass returned a texture"))
        {
            commandList->close();
            return false;
        }
        const nvrhi::TextureDesc hierarchyDesc = hierarchy->getDesc();
        passed &= Check(hierarchyDesc.format == nvrhi::Format::RG32_FLOAT,
            std::string(test.name) + ": output format is RG32_FLOAT");
        passed &= Check(hierarchyDesc.mipLevels == expected.size(),
            std::string(test.name) + ": output mip count");

        nvrhi::StagingTextureHandle staging = device->createStagingTexture(
            hierarchyDesc, nvrhi::CpuAccessMode::Read);
        if (!Check(bool(staging), std::string(test.name) +
                ": create readback texture"))
        {
            commandList->close();
            return false;
        }
        for (uint32_t mip = 0u; mip < hierarchyDesc.mipLevels; ++mip)
        {
            const nvrhi::TextureSlice slice =
                nvrhi::TextureSlice().setMipLevel(mip);
            commandList->copyTexture(staging, slice, hierarchy, slice);
        }
        commandList->close();
        device->executeCommandList(commandList);
        if (!Check(device->waitForIdle(), std::string(test.name) +
                ": GPU completion"))
            return false;

        for (uint32_t mip = 0u;
            mip < hierarchyDesc.mipLevels && mip < expected.size(); ++mip)
        {
            const Level& level = expected[mip];
            const nvrhi::TextureSlice slice = nvrhi::TextureSlice()
                .setMipLevel(mip)
                .setSize(level.width, level.height, 1u);
            size_t rowPitch = 0u;
            const auto* mapped = static_cast<const uint8_t*>(
                device->mapStagingTexture(staging, slice,
                    nvrhi::CpuAccessMode::Read, &rowPitch));
            if (!Check(mapped != nullptr, std::string(test.name) +
                    ": map mip " + std::to_string(mip)))
                return false;
            for (uint32_t y = 0u; y < level.height; ++y)
            {
                for (uint32_t x = 0u; x < level.width; ++x)
                {
                    float actual[2] = {};
                    std::memcpy(actual,
                        mapped + size_t(y) * rowPitch + size_t(x) * 8u,
                        sizeof(actual));
                    const Bounds& reference =
                        level.values[size_t(y) * level.width + x];
                    if (!NearlyEqual(actual[0], reference.minimum) ||
                        !NearlyEqual(actual[1], reference.maximum))
                    {
                        std::cerr << "hierarchical depth GPU validation failed: "
                            << test.name << " mip " << mip << " pixel ("
                            << x << ", " << y << ") expected ["
                            << reference.minimum << ", " << reference.maximum
                            << "] but read [" << actual[0] << ", "
                            << actual[1] << "]\n";
                        passed = false;
                    }
                }
            }
            device->unmapStagingTexture(staging);
        }
        return passed;
    }

    std::vector<float> Pattern(uint32_t width, uint32_t height)
    {
        const float values[] = {
            0.f, 0.03f, 0.2f, 0.5f, 0.91f, 0.999f, 1.f,
            -0.2f, 1.2f, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(), 0.67f, 0.12f
        };
        std::vector<float> result(size_t(width) * height);
        for (size_t index = 0u; index < result.size(); ++index)
            result[index] = values[index % std::size(values)];
        return result;
    }

    uint32_t DenseCellCoordinate(double pixel, uint32_t size)
    {
        return std::min(uint32_t(std::max(pixel, 0.0)), size - 1u);
    }

    double DenseCellExit(const GpuTraceRay& ray, uint32_t cellX,
        uint32_t cellY)
    {
        const double deltaX =
            double(ray.endPixel[0]) - double(ray.beginPixel[0]);
        const double deltaY =
            double(ray.endPixel[1]) - double(ray.beginPixel[1]);
        const double boundaryX = deltaX > 0.0
            ? double(cellX + 1u) : double(cellX);
        const double boundaryY = deltaY > 0.0
            ? double(cellY + 1u) : double(cellY);
        const double tx = std::abs(deltaX) > 1e-12
            ? (boundaryX - double(ray.beginPixel[0])) / deltaX
            : 1.0;
        const double ty = std::abs(deltaY) > 1e-12
            ? (boundaryY - double(ray.beginPixel[1])) / deltaY
            : 1.0;
        return std::min(1.0, std::min(tx, ty));
    }

    double DenseRayDepth(const GpuTraceRay& ray, double t)
    {
        const double begin = ray.beginDepth;
        const double end = ray.endDepth;
        if (ray.orthographic != 0u)
            return begin + (end - begin) * t;
        return 1.0 / ((1.0 - t) / begin + t / end);
    }

    DenseTraceResult TraceDenseLeaves(const TraceDataset& dataset,
        const GpuTraceRay& ray)
    {
        DenseTraceResult result;
        const double deltaX =
            double(ray.endPixel[0]) - double(ray.beginPixel[0]);
        const double deltaY =
            double(ray.endPixel[1]) - double(ray.beginPixel[1]);
        if (!std::isfinite(ray.beginPixel[0]) ||
            !std::isfinite(ray.beginPixel[1]) ||
            !std::isfinite(ray.endPixel[0]) ||
            !std::isfinite(ray.endPixel[1]) ||
            !std::isfinite(ray.beginDepth) ||
            !std::isfinite(ray.endDepth) ||
            ray.beginDepth <= 0.f || ray.endDepth <= 0.f ||
            std::max(std::abs(deltaX), std::abs(deltaY)) <= 1e-5)
            return result;

        const uint32_t receiverX = DenseCellCoordinate(
            ray.beginPixel[0], dataset.width);
        const uint32_t receiverY = DenseCellCoordinate(
            ray.beginPixel[1], dataset.height);
        double t = std::max(0.0,
            DenseCellExit(ray, receiverX, receiverY));
        const double nudgeX = deltaX > 0.0
            ? 1e-9 : (deltaX < 0.0 ? -1e-9 : 0.0);
        const double nudgeY = deltaY > 0.0
            ? 1e-9 : (deltaY < 0.0 ? -1e-9 : 0.0);
        while (t < 1.0)
        {
            const double pixelX =
                double(ray.beginPixel[0]) + deltaX * t;
            const double pixelY =
                double(ray.beginPixel[1]) + deltaY * t;
            if (pixelX < 0.0 || pixelY < 0.0 ||
                pixelX >= dataset.width || pixelY >= dataset.height)
                return result;
            const uint32_t cellX = DenseCellCoordinate(
                pixelX + nudgeX, dataset.width);
            const uint32_t cellY = DenseCellCoordinate(
                pixelY + nudgeY, dataset.height);
            const double exitT = std::max(
                t, DenseCellExit(ray, cellX, cellY));
            ++result.steps;
            const double leaf = dataset.leafDepths[
                size_t(cellY) * dataset.width + cellX];
            if (std::isfinite(leaf) && leaf > 0.0 &&
                std::max(DenseRayDepth(ray, t),
                    DenseRayDepth(ray, exitT)) >= leaf &&
                std::min(DenseRayDepth(ray, t),
                    DenseRayDepth(ray, exitT)) <=
                    leaf + dataset.thickness)
            {
                result.hit = true;
                return result;
            }
            if (exitT >= 1.0)
                return result;
            const double nextT = std::nextafter(
                exitT, std::numeric_limits<double>::infinity());
            if (!(nextT > t))
                return result;
            t = nextT;
        }
        return result;
    }

    std::vector<uint8_t> ReadBinary(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
            return {};
        const std::streamoff byteCount = input.tellg();
        if (byteCount <= 0)
            return {};
        std::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char*>(bytes.data()),
            std::streamsize(bytes.size()));
        if (!input)
            return {};
        return bytes;
    }

    GpuTraceRay MakeRay(float beginX, float beginY, float endX,
        float endY, float beginDepth, float endDepth,
        bool orthographic)
    {
        GpuTraceRay ray;
        ray.beginPixel[0] = beginX;
        ray.beginPixel[1] = beginY;
        ray.endPixel[0] = endX;
        ray.endPixel[1] = endY;
        ray.beginDepth = beginDepth;
        ray.endDepth = endDepth;
        ray.orthographic = orthographic ? 1u : 0u;
        return ray;
    }

    TraceDataset MakeThinBlockerDataset(bool empty)
    {
        TraceDataset dataset;
        dataset.name = empty ? "empty-odd-17x13" :
            "thin-blockers-all-octants-17x13";
        dataset.width = 17u;
        dataset.height = 13u;
        dataset.thickness = 0.05;
        dataset.expectedHitCount = empty ? 0u : 8u;
        dataset.leafDepths.assign(size_t(dataset.width) * dataset.height,
            std::numeric_limits<double>::infinity());
        if (!empty)
        {
            const std::array<std::array<uint32_t, 2>, 8> blockers = {{
                {{ 12u, 6u }}, {{ 12u, 3u }}, {{ 8u, 2u }},
                {{ 4u, 3u }}, {{ 4u, 6u }}, {{ 4u, 9u }},
                {{ 8u, 10u }}, {{ 12u, 9u }}
            }};
            for (const auto& blocker : blockers)
            {
                dataset.leafDepths[
                    size_t(blocker[1]) * dataset.width + blocker[0]] = 9.99;
            }
        }

        constexpr float beginX = 8.37f;
        constexpr float beginY = 6.61f;
        const std::array<std::array<float, 2>, 8> endpoints = {{
            {{ 16.7f, 6.61f }}, {{ 16.7f, 0.2f }}, {{ 8.37f, 0.2f }},
            {{ 0.2f, 0.2f }}, {{ 0.2f, 6.61f }}, {{ 0.2f, 12.7f }},
            {{ 8.37f, 12.7f }}, {{ 16.7f, 12.7f }}
        }};
        for (uint32_t index = 0u; index < endpoints.size(); ++index)
        {
            dataset.rays.push_back(MakeRay(beginX, beginY,
                endpoints[index][0], endpoints[index][1], 10.f, 10.f,
                (index & 1u) == 0u));
        }
        return dataset;
    }

    TraceDataset MakeLongTraversalDataset()
    {
        TraceDataset dataset;
        dataset.name = "alternating-depth-over-512-steps-1025x3";
        dataset.width = 1025u;
        dataset.height = 3u;
        dataset.thickness = 0.05;
        dataset.requireMoreThan512Steps = true;
        dataset.expectedHitCount = 1u;
        dataset.leafDepths.resize(size_t(dataset.width) * dataset.height);
        for (uint32_t y = 0u; y < dataset.height; ++y)
        {
            for (uint32_t x = 0u; x < dataset.width; ++x)
            {
                dataset.leafDepths[size_t(y) * dataset.width + x] =
                    (x & 1u) == 0u ? 2.0 : 100.0;
            }
        }
        dataset.leafDepths[dataset.width + 900u] = 49.99;
        dataset.rays.push_back(MakeRay(
            0.37f, 1.41f, 1024.8f, 1.41f, 50.f, 50.f, false));
        return dataset;
    }

    TraceDataset MakeVaryingDepthDataset()
    {
        TraceDataset dataset = MakeThinBlockerDataset(false);
        dataset.name = "varying-depth-forward-backward-all-octants-17x13";
        dataset.leafDepths.assign(size_t(dataset.width) * dataset.height, 10.0);
        for (size_t index = 0u; index < dataset.rays.size(); ++index)
        {
            auto& ray = dataset.rays[index];
            ray.beginDepth = (index & 2u) != 0u ? 12.f : 8.f;
            ray.endDepth = (index & 2u) != 0u ? 8.f : 12.f;
        }
        return dataset;
    }

    bool RunTraceDataset(nvrhi::IDevice* device,
        uvsr::HierarchicalDepthPass& hierarchyPass,
        nvrhi::IComputePipeline* pipeline, nvrhi::IBindingLayout* layout,
        nvrhi::IBuffer* constants, const TraceDataset& dataset)
    {
        bool passed = Check(dataset.width > 0u && dataset.height > 0u &&
                dataset.leafDepths.size() ==
                    size_t(dataset.width) * dataset.height &&
                !dataset.rays.empty(),
            std::string(dataset.name) + ": valid dataset layout");
        std::vector<uint32_t> expectedHits;
        uint64_t maximumDenseSteps = 0u;
        uint32_t expectedHitCount = 0u;
        for (const GpuTraceRay& ray : dataset.rays)
        {
            const DenseTraceResult expected = TraceDenseLeaves(dataset, ray);
            expectedHits.push_back(expected.hit ? 1u : 0u);
            expectedHitCount += expected.hit ? 1u : 0u;
            maximumDenseSteps = std::max(maximumDenseSteps, expected.steps);
        }
        passed &= Check(expectedHitCount == dataset.expectedHitCount,
            std::string(dataset.name) + ": dense control hit coverage");
        if (dataset.requireMoreThan512Steps)
        {
            passed &= Check(maximumDenseSteps > 512u,
                std::string(dataset.name) +
                    ": dense control traverses more than 512 leaves");
        }

        constexpr float nearPlane = 1.f;
        constexpr float farPlane = 101.f;
        std::vector<float> deviceDepth(dataset.leafDepths.size(), 1.f);
        for (size_t index = 0u; index < dataset.leafDepths.size(); ++index)
        {
            const double depth = dataset.leafDepths[index];
            if (std::isfinite(depth) && depth > 0.0 && depth < farPlane)
                deviceDepth[index] = float((depth - nearPlane) /
                    (farPlane - nearPlane));
        }

        nvrhi::TextureDesc depthDesc;
        depthDesc.width = dataset.width;
        depthDesc.height = dataset.height;
        depthDesc.format = nvrhi::Format::R32_FLOAT;
        depthDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        depthDesc.keepInitialState = true;
        depthDesc.debugName = std::string("HierarchyTraceGpuTest/") +
            dataset.name;
        nvrhi::TextureHandle depthTexture = device->createTexture(depthDesc);
        nvrhi::CommandListHandle hierarchyCommands =
            device->createCommandList();
        if (!Check(bool(depthTexture) && bool(hierarchyCommands),
                std::string(dataset.name) + ": create hierarchy inputs"))
            return false;

        donut::engine::PlanarView view;
        view.SetViewport(nvrhi::Viewport(
            float(dataset.width), float(dataset.height)));
        view.SetMatrices(donut::math::affine3::identity(),
            donut::math::orthoProjD3DStyle(-1.f, 1.f, -1.f, 1.f,
                nearPlane, farPlane));
        view.UpdateCache();
        hierarchyCommands->open();
        hierarchyCommands->writeTexture(depthTexture, 0u, 0u,
            deviceDepth.data(), size_t(dataset.width) * sizeof(float));
        nvrhi::ITexture* hierarchy = hierarchyPass.Render(
            hierarchyCommands, depthTexture, view);
        hierarchyCommands->close();
        device->executeCommandList(hierarchyCommands);
        const bool hierarchyComplete = device->waitForIdle();
        if (!Check(hierarchy != nullptr,
                std::string(dataset.name) + ": build GPU hierarchy") ||
            !Check(hierarchyComplete,
                std::string(dataset.name) + ": hierarchy GPU completion"))
            return false;

        const uint64_t rayBytes =
            uint64_t(dataset.rays.size()) * sizeof(GpuTraceRay);
        const uint64_t hitBytes =
            uint64_t(dataset.rays.size()) * sizeof(uint32_t);
        nvrhi::BufferDesc rayDesc;
        rayDesc.byteSize = rayBytes;
        rayDesc.structStride = sizeof(GpuTraceRay);
        rayDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        rayDesc.keepInitialState = true;
        rayDesc.debugName = "HierarchyTraceGpuTest/Rays";
        nvrhi::BufferHandle rayBuffer = device->createBuffer(rayDesc);

        nvrhi::BufferDesc hitDesc;
        hitDesc.byteSize = hitBytes;
        hitDesc.structStride = sizeof(uint32_t);
        hitDesc.canHaveUAVs = true;
        hitDesc.initialState = nvrhi::ResourceStates::CopySource;
        hitDesc.keepInitialState = true;
        hitDesc.debugName = "HierarchyTraceGpuTest/Hits";
        nvrhi::BufferHandle hitBuffer = device->createBuffer(hitDesc);
        hitDesc.canHaveUAVs = false;
        hitDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        hitDesc.debugName = "HierarchyTraceGpuTest/Readback";
        nvrhi::BufferHandle readback = device->createBuffer(hitDesc);
        if (!Check(bool(rayBuffer) && bool(hitBuffer) && bool(readback),
                std::string(dataset.name) + ": create trace buffers"))
            return false;

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, constants),
            nvrhi::BindingSetItem::Texture_SRV(0, hierarchy),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, rayBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, hitBuffer)
        };
        nvrhi::BindingSetHandle bindingSet =
            device->createBindingSet(bindingSetDesc, layout);
        nvrhi::CommandListHandle traceCommands = device->createCommandList();
        if (!Check(bool(bindingSet) && bool(traceCommands),
                std::string(dataset.name) + ": create trace dispatch"))
            return false;

        TraceConstants traceConstants;
        traceConstants.fullSize[0] = dataset.width;
        traceConstants.fullSize[1] = dataset.height;
        traceConstants.mipCount = hierarchy->getDesc().mipLevels;
        traceConstants.thickness = float(dataset.thickness);
        traceConstants.rayCount = uint32_t(dataset.rays.size());
        traceCommands->open();
        traceCommands->writeBuffer(rayBuffer, dataset.rays.data(), rayBytes);
        traceCommands->writeBuffer(constants, &traceConstants,
            sizeof(traceConstants));
        nvrhi::ComputeState state;
        state.pipeline = pipeline;
        state.bindings = { bindingSet };
        traceCommands->setComputeState(state);
        traceCommands->dispatch(
            (traceConstants.rayCount + 63u) / 64u, 1u, 1u);
        traceCommands->copyBuffer(
            readback, 0u, hitBuffer, 0u, hitBytes);
        traceCommands->close();
        device->executeCommandList(traceCommands);
        if (!Check(device->waitForIdle(), std::string(dataset.name) +
                ": trace GPU completion"))
            return false;

        const auto* actualHits = static_cast<const uint32_t*>(
            device->mapBuffer(readback, nvrhi::CpuAccessMode::Read));
        if (!Check(actualHits != nullptr, std::string(dataset.name) +
                ": map hit readback"))
            return false;
        for (size_t index = 0u; index < expectedHits.size(); ++index)
        {
            if (actualHits[index] != expectedHits[index])
            {
                std::cerr << "hierarchical depth GPU validation failed: "
                    << dataset.name << " ray " << index << " expected hit "
                    << expectedHits[index] << " but read "
                    << actualHits[index] << '\n';
                passed = false;
            }
        }
        device->unmapBuffer(readback);
        return passed;
    }

    bool RunTraceTests(nvrhi::IDevice* device,
        uvsr::HierarchicalDepthPass& hierarchyPass,
        const std::filesystem::path& wrapperDxilPath)
    {
        const std::vector<uint8_t> shaderBytes = ReadBinary(wrapperDxilPath);
        if (!Check(!shaderBytes.empty(),
                "read hierarchical depth trace wrapper DXIL"))
            return false;
        nvrhi::ShaderDesc shaderDesc;
        shaderDesc.shaderType = nvrhi::ShaderType::Compute;
        shaderDesc.debugName = "HierarchyTraceGpuTest";
        nvrhi::ShaderHandle shader = device->createShader(shaderDesc,
            shaderBytes.data(), shaderBytes.size());

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0)
        };
        nvrhi::BindingLayoutHandle layout =
            device->createBindingLayout(layoutDesc);

        nvrhi::BufferDesc constantsDesc;
        constantsDesc.byteSize = sizeof(TraceConstants);
        constantsDesc.isConstantBuffer = true;
        constantsDesc.isVolatile = true;
        constantsDesc.maxVersions = 4u;
        constantsDesc.debugName = "HierarchyTraceGpuTest/Constants";
        nvrhi::BufferHandle constants = device->createBuffer(constantsDesc);

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = shader;
        pipelineDesc.bindingLayouts = { layout };
        nvrhi::ComputePipelineHandle pipeline = shader && layout
            ? device->createComputePipeline(pipelineDesc) : nullptr;
        if (!Check(bool(shader) && bool(layout) && bool(constants) &&
                bool(pipeline), "create hierarchical depth trace pipeline"))
            return false;

        std::vector<TraceDataset> datasets;
        datasets.push_back(MakeThinBlockerDataset(false));
        datasets.push_back(MakeThinBlockerDataset(true));
        datasets.push_back(MakeVaryingDepthDataset());
        datasets.push_back(MakeLongTraversalDataset());
        bool passed = true;
        for (const TraceDataset& dataset : datasets)
        {
            const bool datasetPassed = RunTraceDataset(device, hierarchyPass,
                pipeline, layout, constants, dataset);
            std::cout << dataset.name << ": "
                << (datasetPassed ? "pass" : "fail") << '\n';
            passed &= datasetPassed;
        }
        return passed;
    }
}

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4)
    {
        std::cerr << "usage: hierarchical_depth_gpu_tests "
            "<source-root> <build-shader-root> [trace-wrapper.dxil]\n";
        return 2;
    }
    const std::filesystem::path sourceRoot = argv[1];
    const std::filesystem::path shaderRoot = argv[2];
    if (!Check(std::filesystem::is_regular_file(
            sourceRoot / "src" / "hierarchical_depth_cs.hlsl"),
            "source root contains hierarchical_depth_cs.hlsl"))
        return 2;
    const std::filesystem::path appShaderDirectory =
        FindAppShaderDirectory(shaderRoot);
    if (!Check(!appShaderDirectory.empty(),
            "build shader root contains hierarchical_depth_cs.bin"))
        return 2;

    std::unique_ptr<donut::app::DeviceManager> manager(
        donut::app::DeviceManager::Create(nvrhi::GraphicsAPI::D3D12));
    if (!Check(bool(manager), "create DX12 device manager"))
        return 1;
    donut::app::DeviceCreationParameters parameters;
    parameters.enableNvrhiValidationLayer = true;
    parameters.enableDebugRuntime = true;
    if (!Check(manager->CreateHeadlessDevice(parameters),
            "create headless DX12 device"))
        return 1;

    std::cout << "adapter: " << manager->GetRendererString() << '\n';
    bool passed = true;
    {
        nvrhi::IDevice* device = manager->GetDevice();
        auto rootFs = std::make_shared<donut::vfs::RootFileSystem>();
        rootFs->mount("/shaders/uvsr", appShaderDirectory);
        auto shaderFactory = std::make_shared<donut::engine::ShaderFactory>(
            device, rootFs, "/shaders");
        uvsr::HierarchicalDepthPass pass(device, shaderFactory);

        std::vector<TestCase> cases;
        cases.push_back({ "forward-perspective-7x5", 7u, 5u, false, false,
            0.25f, 50.f,
            donut::math::perspProjD3DStyle(
                1.1f, 7.f / 5.f, 0.25f, 50.f), Pattern(7u, 5u) });
        cases.push_back({ "reverse-perspective-5x3", 5u, 3u, true, false,
            0.4f, std::numeric_limits<float>::infinity(),
            donut::math::perspProjD3DStyleReverse(
                0.9f, 5.f / 3.f, 0.4f), Pattern(5u, 3u) });
        cases.push_back({ "forward-orthographic-3x7", 3u, 7u, false, true,
            0.5f, 30.f,
            donut::math::orthoProjD3DStyle(
                -2.f, 2.f, -3.f, 3.f, 0.5f, 30.f), Pattern(3u, 7u) });
        cases.push_back({ "reverse-orthographic-9x3", 9u, 3u, true, true,
            30.f, 0.5f,
            donut::math::orthoProjD3DStyle(
                -2.f, 2.f, -1.f, 1.f, 30.f, 0.5f), Pattern(9u, 3u) });
        cases.push_back({ "all-invalid-5x1", 5u, 1u, false, false,
            0.1f, 100.f,
            donut::math::perspProjD3DStyle(
                1.f, 5.f, 0.1f, 100.f),
            { 1.f, -1.f, 2.f,
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity() } });

        for (const TestCase& test : cases)
        {
            const bool casePassed = RunCase(device, pass, test);
            std::cout << test.name << ": "
                << (casePassed ? "pass" : "fail") << '\n';
            passed &= casePassed;
        }
        if (argc == 4)
        {
            passed &= RunTraceTests(
                device, pass, std::filesystem::path(argv[3]));
        }
        else
        {
            std::cout << "hierarchical depth trace GPU validation: "
                "skipped, no wrapper DXIL path supplied\n";
        }
        device->waitForIdle();
    }
    manager->Shutdown();
    return passed ? 0 : 1;
}
