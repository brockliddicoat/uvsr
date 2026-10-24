#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct Float2
    {
        double x = 0.0;
        double y = 0.0;
    };

    struct DepthBounds
    {
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
    };

    struct DepthLevel
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        std::vector<DepthBounds> bounds;

        const DepthBounds& At(uint32_t x, uint32_t y) const
        {
            return bounds[size_t(y) * width + x];
        }
    };

    struct ScreenRay
    {
        Float2 beginPixel;
        Float2 endPixel;
        double beginDepth = 0.0;
        double endDepth = 0.0;
        bool orthographic = false;
    };

    struct TraceResult
    {
        bool hit = false;
        uint64_t steps = 0u;
    };

    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "hierarchical depth validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    bool Near(double actual, double expected, double tolerance = 1e-10)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool IsValid(const DepthBounds& bounds)
    {
        return bounds.minimum <= bounds.maximum;
    }

    void Include(DepthBounds& destination, const DepthBounds& source)
    {
        if (!IsValid(source))
            return;
        destination.minimum = std::min(destination.minimum, source.minimum);
        destination.maximum = std::max(destination.maximum, source.maximum);
    }

    std::vector<DepthLevel> BuildHierarchy(
        uint32_t width,
        uint32_t height,
        const std::vector<double>& depths)
    {
        Require(width > 0u && height > 0u &&
                depths.size() == size_t(width) * height,
            "hierarchy input dimensions are valid");

        std::vector<DepthLevel> hierarchy;
        DepthLevel leaves;
        leaves.width = width;
        leaves.height = height;
        leaves.bounds.resize(depths.size());
        for (size_t index = 0u; index < depths.size(); ++index)
        {
            const double depth = depths[index];
            if (std::isfinite(depth) && depth > 0.0)
                leaves.bounds[index] = { depth, depth };
        }
        hierarchy.push_back(std::move(leaves));

        while (hierarchy.back().width > 1u || hierarchy.back().height > 1u)
        {
            const DepthLevel& source = hierarchy.back();
            DepthLevel output;
            output.width = std::max(source.width >> 1u, 1u);
            output.height = std::max(source.height >> 1u, 1u);
            output.bounds.resize(size_t(output.width) * output.height);
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
                    DepthBounds& destination =
                        output.bounds[size_t(y) * output.width + x];
                    for (uint32_t childY = y * 2u;
                        childY < endY;
                        ++childY)
                    {
                        for (uint32_t childX = x * 2u;
                            childX < endX;
                            ++childX)
                        {
                            Include(destination, source.At(childX, childY));
                        }
                    }
                }
            }
            hierarchy.push_back(std::move(output));
        }
        return hierarchy;
    }

    double RayDepth(const ScreenRay& ray, double t)
    {
        if (ray.orthographic)
            return ray.beginDepth + (ray.endDepth - ray.beginDepth) * t;
        return 1.0 / ((1.0 - t) / ray.beginDepth + t / ray.endDepth);
    }

    uint32_t CellCoordinate(double pixel, uint32_t mip, uint32_t size)
    {
        const uint32_t mipSize = std::max(size >> mip, 1u);
        const double nonnegative = std::max(pixel, 0.0);
        const uint64_t leaf = uint64_t(nonnegative);
        return std::min(uint32_t(leaf >> mip), mipSize - 1u);
    }

    void CellBounds(
        uint32_t cell,
        uint32_t mip,
        uint32_t fullSize,
        double& low,
        double& high)
    {
        const uint32_t mipSize = std::max(fullSize >> mip, 1u);
        const uint64_t scale = uint64_t{ 1 } << mip;
        low = double(uint64_t(cell) * scale);
        high = cell + 1u == mipSize
            ? double(fullSize)
            : double(uint64_t(cell + 1u) * scale);
    }

    double CellExit(
        const ScreenRay& ray,
        double lowX,
        double lowY,
        double highX,
        double highY)
    {
        const Float2 delta = {
            ray.endPixel.x - ray.beginPixel.x,
            ray.endPixel.y - ray.beginPixel.y
        };
        const double tx = std::abs(delta.x) > 1e-12
            ? ((delta.x > 0.0 ? highX : lowX) - ray.beginPixel.x) / delta.x
            : 1.0;
        const double ty = std::abs(delta.y) > 1e-12
            ? ((delta.y > 0.0 ? highY : lowY) - ray.beginPixel.y) / delta.y
            : 1.0;
        return std::min(1.0, std::min(tx, ty));
    }

    bool Overlaps(
        const DepthBounds& bounds,
        double firstDepth,
        double lastDepth,
        double thickness)
    {
        return IsValid(bounds) &&
            std::max(firstDepth, lastDepth) >= bounds.minimum &&
            std::min(firstDepth, lastDepth) <= bounds.maximum + thickness;
    }

    bool RayIsValid(const ScreenRay& ray)
    {
        return std::isfinite(ray.beginPixel.x) &&
            std::isfinite(ray.beginPixel.y) &&
            std::isfinite(ray.endPixel.x) &&
            std::isfinite(ray.endPixel.y) &&
            std::isfinite(ray.beginDepth) &&
            std::isfinite(ray.endDepth) &&
            ray.beginDepth > 0.0 && ray.endDepth > 0.0;
    }

    double ReceiverCellExit(
        const ScreenRay& ray,
        uint32_t width,
        uint32_t height)
    {
        const uint32_t cellX = CellCoordinate(ray.beginPixel.x, 0u, width);
        const uint32_t cellY = CellCoordinate(ray.beginPixel.y, 0u, height);
        double lowX, lowY, highX, highY;
        CellBounds(cellX, 0u, width, lowX, highX);
        CellBounds(cellY, 0u, height, lowY, highY);
        return CellExit(ray, lowX, lowY, highX, highY);
    }

    bool IsOnScreen(const Float2& pixel, uint32_t width, uint32_t height)
    {
        return pixel.x >= 0.0 && pixel.y >= 0.0 &&
            pixel.x < double(width) && pixel.y < double(height);
    }

    Float2 PointOnRay(const ScreenRay& ray, double t)
    {
        return {
            ray.beginPixel.x + (ray.endPixel.x - ray.beginPixel.x) * t,
            ray.beginPixel.y + (ray.endPixel.y - ray.beginPixel.y) * t
        };
    }

    double AdvancePast(double t)
    {
        return std::nextafter(t, std::numeric_limits<double>::infinity());
    }

    TraceResult TraceDenseLeaves(
        const DepthLevel& leaves,
        const ScreenRay& ray,
        double thickness)
    {
        TraceResult result;
        if (!RayIsValid(ray))
            return result;
        const double pixelLength = std::max(
            std::abs(ray.endPixel.x - ray.beginPixel.x),
            std::abs(ray.endPixel.y - ray.beginPixel.y));
        if (!(pixelLength > 1e-8))
            return result;

        double t = ReceiverCellExit(ray, leaves.width, leaves.height);
        const Float2 directionNudge = {
            std::copysign(1e-9, ray.endPixel.x - ray.beginPixel.x),
            std::copysign(1e-9, ray.endPixel.y - ray.beginPixel.y)
        };
        while (t < 1.0)
        {
            const Float2 pixel = PointOnRay(ray, t);
            if (!IsOnScreen(pixel, leaves.width, leaves.height))
                return result;
            const uint32_t cellX = CellCoordinate(
                pixel.x + directionNudge.x, 0u, leaves.width);
            const uint32_t cellY = CellCoordinate(
                pixel.y + directionNudge.y, 0u, leaves.height);
            double lowX, lowY, highX, highY;
            CellBounds(cellX, 0u, leaves.width, lowX, highX);
            CellBounds(cellY, 0u, leaves.height, lowY, highY);
            const double exitT = std::max(
                t, CellExit(ray, lowX, lowY, highX, highY));
            ++result.steps;
            if (Overlaps(leaves.At(cellX, cellY),
                    RayDepth(ray, t), RayDepth(ray, exitT), thickness))
            {
                result.hit = true;
                return result;
            }
            if (exitT >= 1.0)
                return result;
            const double nextT = AdvancePast(exitT);
            Require(nextT > t,
                "dense DDA makes finite progress at every cell boundary");
            t = nextT;
        }
        return result;
    }

    TraceResult TraceHierarchy(
        const std::vector<DepthLevel>& hierarchy,
        const ScreenRay& ray,
        double thickness)
    {
        TraceResult result;
        Require(!hierarchy.empty(), "hierarchical trace has at least one mip");
        const uint32_t width = hierarchy.front().width;
        const uint32_t height = hierarchy.front().height;
        if (!RayIsValid(ray))
            return result;
        const double pixelLength = std::max(
            std::abs(ray.endPixel.x - ray.beginPixel.x),
            std::abs(ray.endPixel.y - ray.beginPixel.y));
        if (!(pixelLength > 1e-8))
            return result;

        double t = ReceiverCellExit(ray, width, height);
        const Float2 directionNudge = {
            std::copysign(1e-9, ray.endPixel.x - ray.beginPixel.x),
            std::copysign(1e-9, ray.endPixel.y - ray.beginPixel.y)
        };
        uint32_t mip = 0u;
        while (t < 1.0)
        {
            const Float2 pixel = PointOnRay(ray, t);
            if (!IsOnScreen(pixel, width, height))
                return result;
            const DepthLevel& level = hierarchy[mip];
            const uint32_t cellX = CellCoordinate(
                pixel.x + directionNudge.x, mip, width);
            const uint32_t cellY = CellCoordinate(
                pixel.y + directionNudge.y, mip, height);
            Require(cellX < level.width && cellY < level.height,
                "hierarchical cell mapping stays inside its mip");
            double lowX, lowY, highX, highY;
            CellBounds(cellX, mip, width, lowX, highX);
            CellBounds(cellY, mip, height, lowY, highY);
            const double exitT = std::max(
                t, CellExit(ray, lowX, lowY, highX, highY));
            ++result.steps;
            if (Overlaps(level.At(cellX, cellY),
                    RayDepth(ray, t), RayDepth(ray, exitT), thickness))
            {
                if (mip > 0u)
                {
                    --mip;
                    continue;
                }
                result.hit = true;
                return result;
            }
            if (exitT >= 1.0)
                return result;
            const double nextT = AdvancePast(exitT);
            Require(nextT > t,
                "hierarchical DDA makes finite progress at every cell boundary");
            t = nextT;
            mip = std::min(mip + 1u,
                uint32_t(hierarchy.size() - 1u));
        }
        return result;
    }

    bool RejectForSampleRadius(
        const std::vector<DepthLevel>& hierarchy,
        const Float2& pixel,
        double sampleDistance,
        double receiverDepth,
        double radius)
    {
        uint32_t mip = 0u;
        if (sampleDistance > 1.0)
        {
            mip = uint32_t(std::max(
                std::floor(std::log2(sampleDistance)) - 1.0, 0.0));
        }
        mip = std::min(mip, uint32_t(hierarchy.size() - 1u));
        for (;;)
        {
            const uint32_t cellX = CellCoordinate(
                pixel.x, mip, hierarchy.front().width);
            const uint32_t cellY = CellCoordinate(
                pixel.y, mip, hierarchy.front().height);
            const DepthBounds& node = hierarchy[mip].At(cellX, cellY);
            if (!IsValid(node) ||
                node.minimum > receiverDepth + radius ||
                node.maximum < receiverDepth - radius)
            {
                return true;
            }
            if (mip == 0u)
                return false;
            --mip;
        }
    }

    void TestOddMipReductionAndPositiveDepthRules()
    {
        constexpr uint32_t width = 5u;
        constexpr uint32_t height = 3u;
        std::vector<double> depths(size_t(width) * height,
            std::numeric_limits<double>::quiet_NaN());
        depths[0u] = 2.0;
        depths[size_t(1u) * width + 2u] = 11.0;
        depths[size_t(2u) * width + 4u] = 7.0;
        depths[1u] = 0.0;
        depths[2u] = -4.0;

        const std::vector<DepthLevel> hierarchy =
            BuildHierarchy(width, height, depths);
        Require(hierarchy.size() == 3u,
            "a 5x3 hierarchy reaches one cell in three mips");
        Require(hierarchy[1].width == 2u && hierarchy[1].height == 1u,
            "floor-sized mip dimensions match the GPU texture chain");
        Require(Near(hierarchy[1].At(0u, 0u).minimum, 2.0) &&
                Near(hierarchy[1].At(0u, 0u).maximum, 2.0),
            "the first reduced cell excludes the odd x tail");
        Require(Near(hierarchy[1].At(1u, 0u).minimum, 7.0) &&
                Near(hierarchy[1].At(1u, 0u).maximum, 11.0),
            "the last reduced cell absorbs odd x and y tails");
        Require(Near(hierarchy[2].At(0u, 0u).minimum, 2.0) &&
                Near(hierarchy[2].At(0u, 0u).maximum, 11.0),
            "the root conservatively contains every positive leaf depth");
        Require(!IsValid(hierarchy[0].At(1u, 0u)) &&
                !IsValid(hierarchy[0].At(2u, 0u)),
            "zero and negative view depths remain empty leaves");

        Require(!RejectForSampleRadius(
                hierarchy, { 4.5, 2.5 }, 8.0, 6.0, 1.0),
            "a depth on the positive radius boundary cannot be rejected");
        Require(RejectForSampleRadius(
                hierarchy, { 4.5, 2.5 }, 8.0, 5.9, 1.0),
            "a positive node beyond the full radius is rejected");
        Require(RejectForSampleRadius(
                hierarchy, { 1.5, 0.5 }, 1.0, 1.0, 10.0),
            "an empty nonpositive-depth leaf is rejected at mip zero");
    }

    void TestPerspectiveDepthAndDenseTraversal()
    {
        Require(Near(RayDepth(
                { {}, {}, 1.0, 10.0, false }, 0.5), 20.0 / 11.0),
            "perspective view depth uses reciprocal interpolation");
        Require(Near(RayDepth(
                { {}, {}, 1.0, 10.0, true }, 0.5), 5.5),
            "orthographic view depth remains linear");

        std::vector<double> depths(6u, 30.0);
        depths[2u] = 1.55;
        const std::vector<DepthLevel> hierarchy =
            BuildHierarchy(6u, 1u, depths);
        const ScreenRay ray = {
            { 0.2, 0.5 }, { 5.8, 0.5 }, 1.0, 10.0, false
        };
        const TraceResult dense = TraceDenseLeaves(
            hierarchy.front(), ray, 0.02);
        const TraceResult accelerated = TraceHierarchy(
            hierarchy, ray, 0.02);
        Require(dense.hit && accelerated.hit,
            "perspective interpolation finds a blocker in the dense and hierarchical walks");

        depths[2u] = 4.0;
        const std::vector<DepthLevel> linearTrap =
            BuildHierarchy(6u, 1u, depths);
        Require(!TraceDenseLeaves(linearTrap.front(), ray, 0.0).hit,
            "the dense oracle does not substitute linear perspective depth");
    }

    void TestShiftedReceiverCellExit()
    {
        std::vector<double> depths(5u,
            std::numeric_limits<double>::quiet_NaN());
        depths[1u] = 1.1;
        const std::vector<DepthLevel> hierarchy =
            BuildHierarchy(5u, 1u, depths);
        const ScreenRay ray = {
            { 0.95, 0.5 }, { 4.95, 0.5 }, 1.0, 5.0, true
        };
        Require(TraceDenseLeaves(hierarchy.front(), ray, 0.0).hit,
            "dense traversal tests the first adjacent cell after a shifted origin");
        Require(TraceHierarchy(hierarchy, ray, 0.0).hit,
            "exact receiver-cell exit retains the shifted-origin blocker");

        std::vector<double> reverseDepths(5u,
            std::numeric_limits<double>::quiet_NaN());
        reverseDepths[3u] = 4.9;
        const std::vector<DepthLevel> reverseHierarchy =
            BuildHierarchy(5u, 1u, reverseDepths);
        const ScreenRay reverseRay = {
            { 4.05, 0.5 }, { 0.05, 0.5 }, 5.0, 1.0, true
        };
        Require(TraceDenseLeaves(
                reverseHierarchy.front(), reverseRay, 0.0).hit &&
                TraceHierarchy(reverseHierarchy, reverseRay, 0.0).hit,
            "exact receiver-cell exit also retains a reverse-direction blocker");
    }

    void TestLongTraversalHasNoArbitraryStepCap()
    {
        constexpr uint32_t width = 1025u;
        std::vector<double> depths(width, 1.0);
        for (uint32_t x = 2u; x + 1u < width; x += 2u)
            depths[x] = 20.0;
        depths.back() = 10.0;
        const std::vector<DepthLevel> hierarchy =
            BuildHierarchy(width, 1u, depths);
        const ScreenRay ray = {
            { 0.25, 0.5 }, { 1024.75, 0.5 }, 10.0, 10.0, true
        };
        const TraceResult dense = TraceDenseLeaves(
            hierarchy.front(), ray, 0.0);
        const TraceResult accelerated = TraceHierarchy(
            hierarchy, ray, 0.0);
        Require(dense.hit && accelerated.hit,
            "a blocker beyond 512 pixels remains discoverable");
        Require(accelerated.steps > 512u,
            "the deterministic long ray exercises more than the retired step cap");
    }

    void TestRandomizedHierarchyNeverMissesDenseHits()
    {
        std::mt19937 random(0x485a4244u);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        std::uniform_real_distribution<double> depth(0.25, 24.0);
        uint32_t denseHitCount = 0u;
        constexpr uint32_t trialCount = 2000u;
        for (uint32_t trial = 0u; trial < trialCount; ++trial)
        {
            const uint32_t width = 3u + random() % 45u;
            const uint32_t height = 3u + random() % 31u;
            std::vector<double> depths(size_t(width) * height);
            for (double& sample : depths)
            {
                sample = unit(random) < 0.18
                    ? std::numeric_limits<double>::quiet_NaN()
                    : depth(random);
            }
            const std::vector<DepthLevel> hierarchy =
                BuildHierarchy(width, height, depths);
            ScreenRay ray;
            ray.beginPixel = {
                0.05 + unit(random) * (double(width) - 0.1),
                0.05 + unit(random) * (double(height) - 0.1)
            };
            ray.endPixel = {
                0.05 + unit(random) * (double(width) - 0.1),
                0.05 + unit(random) * (double(height) - 0.1)
            };
            ray.beginDepth = depth(random);
            ray.endDepth = depth(random);
            ray.orthographic = (random() & 1u) != 0u;
            const double thickness = unit(random) * 0.2;
            const TraceResult dense = TraceDenseLeaves(
                hierarchy.front(), ray, thickness);
            const TraceResult accelerated = TraceHierarchy(
                hierarchy, ray, thickness);
            if (dense.hit)
            {
                ++denseHitCount;
                Require(accelerated.hit,
                    "hierarchical traversal never rejects a randomized dense hit");
            }
        }
        Require(denseHitCount > trialCount / 4u,
            "randomized coverage contains a substantial set of dense hits");
    }
}

int main()
{
    TestOddMipReductionAndPositiveDepthRules();
    TestPerspectiveDepthAndDenseTraversal();
    TestShiftedReceiverCellExit();
    TestLongTraversalHasNoArbitraryStepCap();
    TestRandomizedHierarchyNeverMissesDenseHits();

    // this CPU model validates interval conservatism and traversal topology. it
    // does not execute HLSL, reproduce GPU float precision, or prove resource
    // binding and synchronization behavior.
    std::cout << "uvsr hierarchical depth reference validation passed\n";
    return EXIT_SUCCESS;
}
