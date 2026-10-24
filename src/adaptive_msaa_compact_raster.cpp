/*
* Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

// Exact provenance:
// - UVSR's pass and draw-state implementation is a first-party sibling of
//   Donut GBufferFillPass/GeometryPasses at pinned Donut commit
//   bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 (MIT).
// - Its visibility-only target topology and opaque/alpha split are adapted
//   from The Forge Visibility_Buffer sample, commit
//   a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0), specifically
//   VisibilityBufferPass.{vert,frag}.fsl and
//   VisibilityBufferPassAlpha.{vert,frag}.fsl. The Forge's transient packed
//   primitive identity is not used.
// - Written for exact UVSR base 578496047c4753077e36f39a68df8c2c323cee58.

#include "adaptive_msaa_compact_raster.h"
#include "adaptive_msaa_texture_qualification_runtime.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/MaterialBindingCache.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/render/DrawStrategy.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace donut::math;
#include <donut/shaders/bindless.h>
#include <donut/shaders/gbuffer_cb.h>

namespace
{
    using donut::engine::BufferGroup;
    using donut::engine::IView;
    using donut::engine::MaterialDomain;
    using donut::engine::MeshGeometry;
    using donut::engine::MeshInfo;
    using donut::render::DrawItem;
    using namespace uvsr::adaptive_msaa;

    constexpr uint64_t MaximumGpuByteEnd =
        uint64_t(std::numeric_limits<uint32_t>::max()) + 1u;
    constexpr uint32_t IndexStride = 4u;
    constexpr uint32_t PositionStride = 12u;
    constexpr uint32_t TexCoordStride = 8u;
    constexpr uint32_t PackedDirectionStride = 4u;

    // Direct3D permits a fused instruction (including an implementation dot
    // or MAD) to differ from its infinitely precise result by one ULP. That
    // is twice the usual round-to-nearest unit roundoff. A shader compiler is
    // also free to retain the explicit serial multiply/add expansion. Bound
    // both legal paths independently and use the larger result: four one-ULP
    // fused dot steps versus seven half-ULP serial operations, and two
    // one-ULP fused/materialization steps versus the viewport's three serial
    // operations. This avoids the unsound assumption that contraction is
    // necessarily more accurate than Direct3D's permitted instruction result.
    constexpr double D3dHalfUlpRoundoff = 0x1p-24;
    constexpr double D3dOneUlpRelativeError = 0x1p-23;
    constexpr double Gamma(double operationCount, double unitError)
    {
        return (operationCount * unitError) /
            (1.0 - operationCount * unitError);
    }
    constexpr double D3dSerialDot4RelativeError =
        Gamma(7.0, D3dHalfUlpRoundoff);
    constexpr double D3dFusedDot4RelativeError =
        Gamma(4.0, D3dOneUlpRelativeError);
    constexpr double D3dDot4RelativeError =
        D3dSerialDot4RelativeError > D3dFusedDot4RelativeError
            ? D3dSerialDot4RelativeError
            : D3dFusedDot4RelativeError;
    constexpr double D3dSerialViewportRelativeError =
        Gamma(3.0, D3dHalfUlpRoundoff);
    constexpr double D3dFusedViewportRelativeError =
        Gamma(2.0, D3dOneUlpRelativeError);
    constexpr double D3dViewportRelativeError =
        D3dSerialViewportRelativeError >
            D3dFusedViewportRelativeError
            ? D3dSerialViewportRelativeError
            : D3dFusedViewportRelativeError;
    constexpr double D3dTriangleAreaRelativeError =
        Gamma(5.0, D3dOneUlpRelativeError);
    constexpr double D3dStrictDivideRelativeError =
        (1.0 + 2.0 * D3dHalfUlpRoundoff) *
            (1.0 + D3dHalfUlpRoundoff) - 1.0;
    constexpr double D3dFloatMinimum =
        static_cast<double>(AdaptiveMsaaCompactMinimumClipW);
    constexpr double D3dFloatMaximum =
        static_cast<double>(std::numeric_limits<float>::max());
    constexpr double D3dDot4AbsoluteError =
        (7.0 * D3dFloatMinimum) /
            (1.0 - 7.0 * D3dHalfUlpRoundoff);
    constexpr double D3dViewportAbsoluteError =
        (3.0 * D3dFloatMinimum) /
            (1.0 - 3.0 * D3dHalfUlpRoundoff);
    constexpr double D3dTriangleAreaAbsoluteError =
        (5.0 * D3dFloatMinimum) /
            (1.0 - 5.0 * D3dOneUlpRelativeError);
    constexpr uint32_t CompactRasterSubpixelScale = 256u;

    std::atomic<uint64_t> g_NextCompactProofRevision{ 1u };

    [[nodiscard]] uint64_t AllocateCompactProofRevision()
    {
        uint64_t revision = g_NextCompactProofRevision.load(
            std::memory_order_relaxed);
        while (revision != std::numeric_limits<uint64_t>::max())
        {
            if (g_NextCompactProofRevision.compare_exchange_weak(
                    revision,
                    revision + 1u,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed))
            {
                return revision;
            }
        }
        // Saturate permanently rather than wrapping and aliasing an earlier
        // exact-content identity.
        return 0u;
    }

    struct DoubleInterval
    {
        double lower = 0.0;
        double upper = 0.0;
    };

    [[nodiscard]] double RoundDown(double value)
    {
        return std::isfinite(value)
            ? std::nextafter(
                value,
                -std::numeric_limits<double>::infinity())
            : value;
    }

    [[nodiscard]] double RoundUp(double value)
    {
        return std::isfinite(value)
            ? std::nextafter(
                value,
                std::numeric_limits<double>::infinity())
            : value;
    }

    [[nodiscard]] double AddUp(double first, double second)
    {
        return RoundUp(first + second);
    }

    [[nodiscard]] double MultiplyUp(double first, double second)
    {
        return RoundUp(first * second);
    }

    [[nodiscard]] DoubleInterval PointInterval(double value)
    {
        return { value, value };
    }

    [[nodiscard]] bool IsFiniteInterval(const DoubleInterval& interval)
    {
        return std::isfinite(interval.lower) &&
            std::isfinite(interval.upper) &&
            interval.lower <= interval.upper;
    }

    [[nodiscard]] DoubleInterval AddIntervals(
        const DoubleInterval& first,
        const DoubleInterval& second)
    {
        return {
            RoundDown(first.lower + second.lower),
            RoundUp(first.upper + second.upper)
        };
    }

    [[nodiscard]] DoubleInterval MultiplyIntervals(
        const DoubleInterval& first,
        const DoubleInterval& second)
    {
        const std::array<double, 4> lowerProducts = {
            RoundDown(first.lower * second.lower),
            RoundDown(first.lower * second.upper),
            RoundDown(first.upper * second.lower),
            RoundDown(first.upper * second.upper)
        };
        const std::array<double, 4> upperProducts = {
            RoundUp(first.lower * second.lower),
            RoundUp(first.lower * second.upper),
            RoundUp(first.upper * second.lower),
            RoundUp(first.upper * second.upper)
        };
        return {
            *std::min_element(
                lowerProducts.begin(), lowerProducts.end()),
            *std::max_element(
                upperProducts.begin(), upperProducts.end())
        };
    }

    [[nodiscard]] DoubleInterval DivideIntervals(
        const DoubleInterval& numerator,
        const DoubleInterval& positiveDenominator)
    {
        if (!(positiveDenominator.lower > 0.0) ||
            !IsFiniteInterval(numerator) ||
            !IsFiniteInterval(positiveDenominator))
        {
            return {
                -std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()
            };
        }
        const std::array<double, 4> lowerQuotients = {
            RoundDown(numerator.lower / positiveDenominator.lower),
            RoundDown(numerator.lower / positiveDenominator.upper),
            RoundDown(numerator.upper / positiveDenominator.lower),
            RoundDown(numerator.upper / positiveDenominator.upper)
        };
        const std::array<double, 4> upperQuotients = {
            RoundUp(numerator.lower / positiveDenominator.lower),
            RoundUp(numerator.lower / positiveDenominator.upper),
            RoundUp(numerator.upper / positiveDenominator.lower),
            RoundUp(numerator.upper / positiveDenominator.upper)
        };
        return {
            *std::min_element(
                lowerQuotients.begin(), lowerQuotients.end()),
            *std::max_element(
                upperQuotients.begin(), upperQuotients.end())
        };
    }

    [[nodiscard]] DoubleInterval ExpandInterval(
        const DoubleInterval& interval,
        double error)
    {
        return {
            RoundDown(interval.lower - error),
            RoundUp(interval.upper + error)
        };
    }

    [[nodiscard]] double MaximumAbsolute(
        const DoubleInterval& interval)
    {
        return RoundUp(std::max(
            std::abs(interval.lower),
            std::abs(interval.upper)));
    }

    [[nodiscard]] float CanonicalizeD3dFloat(float value)
    {
        if (value != 0.f && std::isfinite(value) &&
            std::abs(value) < AdaptiveMsaaCompactMinimumClipW)
        {
            return std::copysign(0.f, value);
        }
        return value;
    }

    [[nodiscard]] float FloatLowerBound(double value)
    {
        float result = static_cast<float>(value);
        if (static_cast<double>(result) > value)
        {
            result = std::nextafter(
                result,
                -std::numeric_limits<float>::infinity());
        }
        return result;
    }

    [[nodiscard]] float FloatUpperBound(double value)
    {
        float result = static_cast<float>(value);
        if (static_cast<double>(result) < value)
        {
            result = std::nextafter(
                result,
                std::numeric_limits<float>::infinity());
        }
        return result;
    }

    [[nodiscard]] uint32_t FloatBits(float value)
    {
        uint32_t result = 0u;
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    [[nodiscard]] bool SameFloatBits(float first, float second)
    {
        return FloatBits(first) == FloatBits(second);
    }

    [[nodiscard]] bool IsCompactDirectionTransformFinite(
        const std::array<std::array<double, 4>, 3>& objectToWorld)
    {
        const double relativeError = RoundUp(D3dDot4RelativeError);
        const double absoluteError = RoundUp(D3dDot4AbsoluteError);
        for (uint32_t row = 0u; row < 3u; ++row)
        {
            // Packed RGB(A)8 SNORM direction components are finite and lie in
            // [-1, 1]. The homogeneous component is exactly zero, so the
            // translation column cannot affect the result.
            double magnitude = 0.0;
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                if (!std::isfinite(objectToWorld[row][component]))
                    return false;
                magnitude = AddUp(
                    magnitude,
                    std::abs(objectToWorld[row][component]));
            }
            const double error = AddUp(
                MultiplyUp(relativeError, magnitude),
                absoluteError);
            if (!std::isfinite(magnitude) || !std::isfinite(error) ||
                AddUp(magnitude, error) > D3dFloatMaximum)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] DoubleInterval SubtractIntervals(
        const DoubleInterval& first,
        const DoubleInterval& second)
    {
        return {
            RoundDown(first.lower - second.upper),
            RoundUp(first.upper - second.lower)
        };
    }

    struct ProjectedVertexProof
    {
        bool safe = false;
        std::array<DoubleInterval, 4> clip{};
        std::array<DoubleInterval, 2> window{};
        DoubleInterval deviceDepth{};
        std::array<int32_t, 2> snapped{};
        double minimumSnapTieDistance = 0.0;
    };

    [[nodiscard]] ProjectedVertexProof EvaluateProjectedVertexProof(
        const std::array<double, 3>& objectPosition,
        const std::array<std::array<double, 4>, 3>& objectToWorld,
        const std::array<std::array<double, 4>, 4>& worldToClip,
        const std::array<double, 2>& clipToWindowScale,
        const std::array<double, 2>& clipToWindowBias,
        double viewportMinimumDepth,
        double viewportMaximumDepth)
    {
        ProjectedVertexProof result;
        const double dotRelativeError = RoundUp(
            D3dDot4RelativeError);
        const double dotAbsoluteError = RoundUp(
            D3dDot4AbsoluteError);
        const double viewportRelativeError = RoundUp(
            D3dViewportRelativeError);
        const double viewportAbsoluteError = RoundUp(
            D3dViewportAbsoluteError);
        const double divideRelativeError = RoundUp(
            D3dStrictDivideRelativeError);

        std::array<double, 3> idealWorld{};
        std::array<double, 3> worldError{};
        std::array<DoubleInterval, 3> world{};
        for (uint32_t row = 0u; row < 3u; ++row)
        {
            double value = objectToWorld[row][3];
            double magnitude = std::abs(value);
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                value += objectToWorld[row][component] *
                    objectPosition[component];
                magnitude = AddUp(
                    magnitude,
                    MultiplyUp(
                        std::abs(objectToWorld[row][component]),
                        std::abs(objectPosition[component])));
            }
            worldError[row] = AddUp(
                MultiplyUp(dotRelativeError, magnitude),
                dotAbsoluteError);
            if (!std::isfinite(value) || !std::isfinite(magnitude) ||
                !std::isfinite(worldError[row]) ||
                AddUp(magnitude, worldError[row]) > D3dFloatMaximum)
            {
                return result;
            }
            idealWorld[row] = value;
            world[row] = ExpandInterval(
                PointInterval(value),
                worldError[row]);
        }

        std::array<double, 4> idealClip{};
        std::array<double, 4> clipError{};
        for (uint32_t component = 0u; component < 4u; ++component)
        {
            double value = worldToClip[3][component];
            double magnitude = std::abs(value);
            double propagatedWorldError = 0.0;
            for (uint32_t row = 0u; row < 3u; ++row)
            {
                value += idealWorld[row] *
                    worldToClip[row][component];
                const double coefficient =
                    std::abs(worldToClip[row][component]);
                magnitude = AddUp(
                    magnitude,
                    MultiplyUp(
                        coefficient,
                        MaximumAbsolute(world[row])));
                propagatedWorldError = AddUp(
                    propagatedWorldError,
                    MultiplyUp(coefficient, worldError[row]));
            }
            const double roundError = AddUp(
                MultiplyUp(dotRelativeError, magnitude),
                dotAbsoluteError);
            clipError[component] = AddUp(
                propagatedWorldError,
                roundError);
            if (!std::isfinite(value) || !std::isfinite(magnitude) ||
                !std::isfinite(clipError[component]) ||
                AddUp(magnitude, roundError) > D3dFloatMaximum)
            {
                return result;
            }
            idealClip[component] = value;
            result.clip[component] = ExpandInterval(
                PointInterval(value),
                clipError[component]);
            if (!IsFiniteInterval(result.clip[component]))
                return result;
        }

        const DoubleInterval& w = result.clip[3];
        if (w.lower < D3dFloatMinimum ||
            w.upper > static_cast<double>(
                AdaptiveMsaaCompactMaximumClipW))
        {
            return result;
        }

        // Direct3D's canonical homogeneous volume is -W<X<W, -W<Y<W,
        // 0<Z<W. Strict interior intervals deliberately reject contacts and
        // crossings: fixed-function clipping would synthesize vertices whose
        // post-snap plane is not represented by the original-three-vertex
        // compute reconstruction.
        if (AddIntervals(result.clip[0], w).lower <= 0.0 ||
            SubtractIntervals(w, result.clip[0]).lower <= 0.0 ||
            AddIntervals(result.clip[1], w).lower <= 0.0 ||
            SubtractIntervals(w, result.clip[1]).lower <= 0.0 ||
            result.clip[2].lower <= 0.0 ||
            SubtractIntervals(w, result.clip[2]).lower <= 0.0)
        {
            return result;
        }

        // UVSR currently uses the ordinary [0, 1] D3D viewport depth range.
        // Keeping that exact restriction avoids silently inventing an
        // implementation-specific viewport-Z arithmetic sequence.
        if (viewportMinimumDepth != 0.0 || viewportMaximumDepth != 1.0)
            return result;

        const double idealDeviceDepth = idealClip[2] / idealClip[3];
        if (!std::isfinite(idealDeviceDepth))
            return result;
        const double transformDepthError = RoundUp(
            AddUp(
                clipError[2],
                MultiplyUp(
                    std::abs(idealDeviceDepth),
                    clipError[3])) /
            w.lower);
        const double dividedDepthMagnitude = AddUp(
            std::abs(idealDeviceDepth),
            transformDepthError);
        const double depthDivideRoundError = AddUp(
            MultiplyUp(
                divideRelativeError,
                dividedDepthMagnitude),
            D3dFloatMinimum);
        result.deviceDepth = ExpandInterval(
            PointInterval(idealDeviceDepth),
            AddUp(transformDepthError, depthDivideRoundError));
        if (!IsFiniteInterval(result.deviceDepth) ||
            result.deviceDepth.lower <= 0.0 ||
            result.deviceDepth.upper >= 1.0)
        {
            return result;
        }

        result.minimumSnapTieDistance =
            std::numeric_limits<double>::infinity();
        for (uint32_t component = 0u; component < 2u; ++component)
        {
            const double idealRatio =
                idealClip[component] / idealClip[3];
            if (!std::isfinite(idealRatio))
                return result;
            const double transformRatioError = RoundUp(
                AddUp(
                    clipError[component],
                    MultiplyUp(
                        std::abs(idealRatio),
                        clipError[3])) /
                w.lower);
            const double dividedRatioMagnitude = AddUp(
                std::abs(idealRatio),
                transformRatioError);
            const double divideRoundError = AddUp(
                MultiplyUp(
                    divideRelativeError,
                    dividedRatioMagnitude),
                D3dFloatMinimum);
            const double ratioError = AddUp(
                transformRatioError,
                divideRoundError);
            const DoubleInterval ratio = ExpandInterval(
                PointInterval(idealRatio),
                ratioError);
            if (!IsFiniteInterval(ratio))
                return result;

            const DoubleInterval idealWindow = AddIntervals(
                MultiplyIntervals(
                    ratio,
                    PointInterval(clipToWindowScale[component])),
                PointInterval(clipToWindowBias[component]));
            const double viewportMagnitude = AddUp(
                MultiplyUp(
                    std::abs(clipToWindowScale[component]),
                    MaximumAbsolute(ratio)),
                std::abs(clipToWindowBias[component]));
            const double viewportError = AddUp(
                MultiplyUp(
                    viewportRelativeError,
                    viewportMagnitude),
                viewportAbsoluteError);
            result.window[component] = ExpandInterval(
                idealWindow,
                viewportError);
            if (!IsFiniteInterval(result.window[component]) ||
                result.window[component].lower <
                    AdaptiveMsaaCompactMinimumWindowCoordinate ||
                result.window[component].upper >
                    AdaptiveMsaaCompactMaximumWindowCoordinate)
            {
                return result;
            }

            const double idealWindowValue =
                idealRatio * clipToWindowScale[component] +
                clipToWindowBias[component];
            const double idealScaled = idealWindowValue *
                double(CompactRasterSubpixelScale);
            const double snapped = std::floor(idealScaled + 0.5);
            const double scaledLower = RoundDown(
                result.window[component].lower *
                    CompactRasterSubpixelScale);
            const double scaledUpper = RoundUp(
                result.window[component].upper *
                    CompactRasterSubpixelScale);
            if (!std::isfinite(snapped) ||
                snapped < -8388608.0 || snapped > 8388352.0 ||
                scaledLower <= snapped - 0.5 ||
                scaledUpper >= snapped + 0.5)
            {
                return result;
            }
            result.snapped[component] = static_cast<int32_t>(snapped);
            result.minimumSnapTieDistance = std::min(
                result.minimumSnapTieDistance,
                std::min(
                    scaledLower - (snapped - 0.5),
                    (snapped + 0.5) - scaledUpper) /
                    CompactRasterSubpixelScale);
        }

        result.safe = std::isfinite(result.minimumSnapTieDistance) &&
            result.minimumSnapTieDistance > 0.0;
        return result;
    }

    [[nodiscard]] bool TryAdd(
        uint64_t first,
        uint64_t second,
        uint64_t& result)
    {
        if (first > std::numeric_limits<uint64_t>::max() - second)
            return false;
        result = first + second;
        return true;
    }

    [[nodiscard]] bool TryMultiply(
        uint64_t first,
        uint64_t second,
        uint64_t& result)
    {
        if (first != 0u &&
            second > std::numeric_limits<uint64_t>::max() / first)
        {
            return false;
        }
        result = first * second;
        return true;
    }

    [[nodiscard]] bool TryNarrow(uint64_t value, uint32_t& result)
    {
        if (value > std::numeric_limits<uint32_t>::max())
            return false;
        result = static_cast<uint32_t>(value);
        return true;
    }

    [[nodiscard]] uint64_t MakeUint64(uint32_t low, uint32_t high)
    {
        return uint64_t(low) | (uint64_t(high) << 32u);
    }

    [[nodiscard]] CompactRasterDrawValidationResult FailDraw(
        FallbackReason reason)
    {
        CompactRasterDrawValidationResult result;
        result.reason = reason;
        return result;
    }

    [[nodiscard]] bool IsByteRangeValid(
        const CompactRasterByteRange& range,
        uint64_t firstElement,
        uint64_t elementCount,
        uint32_t expectedStride,
        uint64_t physicalByteSize,
        uint32_t expectedRecordOffset)
    {
        if (range.elementStride != expectedStride ||
            range.byteOffset > physicalByteSize ||
            range.byteSize > physicalByteSize - range.byteOffset)
        {
            return false;
        }

        uint64_t endElement = 0u;
        uint64_t requiredBytes = 0u;
        uint64_t firstElementBytes = 0u;
        uint64_t recordOffset = 0u;
        uint64_t recordEnd = 0u;
        if (!TryAdd(firstElement, elementCount, endElement) ||
            !TryMultiply(endElement, expectedStride, requiredBytes) ||
            requiredBytes > range.byteSize ||
            !TryMultiply(firstElement, expectedStride, firstElementBytes) ||
            !TryAdd(range.byteOffset, firstElementBytes, recordOffset) ||
            !TryMultiply(elementCount, expectedStride, requiredBytes) ||
            !TryAdd(recordOffset, requiredBytes, recordEnd) ||
            recordOffset >= MaximumGpuByteEnd ||
            recordEnd > MaximumGpuByteEnd)
        {
            return false;
        }

        return expectedRecordOffset == static_cast<uint32_t>(recordOffset);
    }

    [[nodiscard]] bool IsSourceByteRangeValid(
        const CompactRasterByteRange& range,
        uint64_t firstElement,
        uint64_t elementCount,
        uint32_t expectedStride,
        uint64_t physicalByteSize)
    {
        if (range.elementStride != expectedStride ||
            range.byteOffset > physicalByteSize ||
            range.byteSize > physicalByteSize - range.byteOffset)
        {
            return false;
        }

        uint64_t endElement = 0u;
        uint64_t requiredFromRangeStart = 0u;
        uint64_t firstElementBytes = 0u;
        uint64_t sourceOffset = 0u;
        uint64_t sourceBytes = 0u;
        uint64_t sourceEnd = 0u;
        return TryAdd(firstElement, elementCount, endElement) &&
            TryMultiply(
                endElement, expectedStride, requiredFromRangeStart) &&
            requiredFromRangeStart <= range.byteSize &&
            TryMultiply(
                firstElement, expectedStride, firstElementBytes) &&
            TryAdd(range.byteOffset, firstElementBytes, sourceOffset) &&
            TryMultiply(elementCount, expectedStride, sourceBytes) &&
            TryAdd(sourceOffset, sourceBytes, sourceEnd) &&
            sourceOffset < MaximumGpuByteEnd &&
            sourceEnd <= MaximumGpuByteEnd;
    }

    [[nodiscard]] bool IsCompactMapBuffer(const nvrhi::IBuffer* buffer)
    {
        if (!buffer)
            return false;
        const nvrhi::BufferDesc& desc = buffer->getDesc();
        return desc.structStride == sizeof(uint32_t) &&
            desc.byteSize >= sizeof(uint32_t) &&
            desc.byteSize % desc.structStride == 0u;
    }

    [[nodiscard]] bool IsD32DepthFormat(nvrhi::Format format)
    {
        return format == nvrhi::Format::D32 ||
            format == nvrhi::Format::D32S8;
    }

    [[nodiscard]] nvrhi::Format EffectiveAttachmentFormat(
        const nvrhi::FramebufferAttachment& attachment)
    {
        return attachment.format != nvrhi::Format::UNKNOWN
            ? attachment.format
            : attachment.texture
                ? attachment.texture->getDesc().format
                : nvrhi::Format::UNKNOWN;
    }

    [[nodiscard]] bool IsCompactFramebuffer(
        nvrhi::IFramebuffer* framebuffer,
        nvrhi::ITexture* expectedSurfaceKeyTarget,
        nvrhi::ITexture* expectedVisibilityClassTarget,
        nvrhi::ITexture* expectedDepthTarget)
    {
        if (!framebuffer || !expectedSurfaceKeyTarget ||
            !expectedVisibilityClassTarget || !expectedDepthTarget)
        {
            return false;
        }

        const nvrhi::FramebufferDesc& framebufferDesc =
            framebuffer->getDesc();
        if (framebufferDesc.colorAttachments.size() != 2u ||
            framebufferDesc.colorAttachments[0].texture !=
                expectedSurfaceKeyTarget ||
            framebufferDesc.colorAttachments[0].isReadOnly ||
            framebufferDesc.colorAttachments[1].texture !=
                expectedVisibilityClassTarget ||
            framebufferDesc.colorAttachments[1].isReadOnly ||
            framebufferDesc.depthAttachment.texture != expectedDepthTarget ||
            framebufferDesc.depthAttachment.isReadOnly ||
            framebufferDesc.shadingRateAttachment.valid())
        {
            return false;
        }

        const nvrhi::TextureDesc& key =
            expectedSurfaceKeyTarget->getDesc();
        const nvrhi::TextureDesc& visibilityClass =
            expectedVisibilityClassTarget->getDesc();
        const nvrhi::TextureDesc& depth = expectedDepthTarget->getDesc();
        const nvrhi::Format keyAttachmentFormat =
            EffectiveAttachmentFormat(
                framebufferDesc.colorAttachments[0]);
        const nvrhi::Format visibilityClassAttachmentFormat =
            EffectiveAttachmentFormat(
                framebufferDesc.colorAttachments[1]);
        const nvrhi::Format depthAttachmentFormat =
            EffectiveAttachmentFormat(framebufferDesc.depthAttachment);
        if (key.format != nvrhi::Format::RG32_UINT ||
            keyAttachmentFormat != nvrhi::Format::RG32_UINT ||
            key.dimension != nvrhi::TextureDimension::Texture2DMS ||
            !key.isRenderTarget || !key.isShaderResource ||
            !uvsr::adaptive_msaa::IsCompactVisibilityClassFormat(
                visibilityClass.format) ||
            visibilityClassAttachmentFormat != visibilityClass.format ||
            visibilityClass.dimension !=
                nvrhi::TextureDimension::Texture2DMS ||
            !visibilityClass.isRenderTarget ||
            !visibilityClass.isShaderResource ||
            !IsD32DepthFormat(depth.format) ||
            depthAttachmentFormat != depth.format ||
            depth.dimension != nvrhi::TextureDimension::Texture2DMS ||
            !depth.isRenderTarget || !depth.isShaderResource ||
            key.width == 0u || key.height == 0u ||
            key.width != visibilityClass.width ||
            key.height != visibilityClass.height ||
            key.depth != visibilityClass.depth ||
            key.arraySize != visibilityClass.arraySize ||
            key.mipLevels != visibilityClass.mipLevels ||
            key.sampleCount != visibilityClass.sampleCount ||
            key.sampleQuality != visibilityClass.sampleQuality ||
            key.width != depth.width || key.height != depth.height ||
            key.depth != depth.depth || key.arraySize != depth.arraySize ||
            key.mipLevels != depth.mipLevels ||
            key.sampleCount != depth.sampleCount ||
            key.sampleQuality != depth.sampleQuality ||
            !IsSupportedSampleCount(key.sampleCount))
        {
            return false;
        }

        return framebuffer->getFramebufferInfo().colorFormats.size() == 2u &&
            framebuffer->getFramebufferInfo().colorFormats[0] ==
                keyAttachmentFormat &&
            framebuffer->getFramebufferInfo().colorFormats[1] ==
                visibilityClassAttachmentFormat &&
            framebuffer->getFramebufferInfo().depthFormat ==
                depthAttachmentFormat &&
            framebuffer->getFramebufferInfo().sampleCount == key.sampleCount &&
            framebuffer->getFramebufferInfo().sampleQuality ==
                key.sampleQuality;
    }

    [[nodiscard]] bool HasExactFallbackTargetSet(
        const std::array<nvrhi::ITexture*, 5u>& targets)
    {
        return std::all_of(
            targets.begin(), targets.end(),
            [](const nvrhi::ITexture* texture) { return texture != nullptr; });
    }

    [[nodiscard]] bool HasSameMultisampleTopology(
        const nvrhi::TextureDesc& candidate,
        const nvrhi::TextureDesc& reference)
    {
        return candidate.dimension ==
                nvrhi::TextureDimension::Texture2DMS &&
            candidate.isRenderTarget && candidate.isShaderResource &&
            candidate.width == reference.width &&
            candidate.height == reference.height &&
            candidate.depth == reference.depth &&
            candidate.arraySize == reference.arraySize &&
            candidate.mipLevels == reference.mipLevels &&
            candidate.sampleCount == reference.sampleCount &&
            candidate.sampleQuality == reference.sampleQuality;
    }

    [[nodiscard]] bool IsMixedFallbackFramebuffer(
        nvrhi::IFramebuffer* framebuffer,
        const std::array<nvrhi::ITexture*, 5u>& expectedGBufferTargets,
        nvrhi::ITexture* expectedMotionTarget,
        nvrhi::ITexture* expectedSurfaceKeyTarget,
        nvrhi::ITexture* expectedVisibilityClassTarget,
        nvrhi::ITexture* expectedDepthTarget)
    {
        if (!framebuffer ||
            !HasExactFallbackTargetSet(expectedGBufferTargets) ||
            !expectedSurfaceKeyTarget ||
            !expectedVisibilityClassTarget || !expectedDepthTarget)
        {
            return false;
        }

        const nvrhi::FramebufferDesc& description =
            framebuffer->getDesc();
        const size_t motionCount = expectedMotionTarget ? 1u : 0u;
        const size_t keyIndex = expectedGBufferTargets.size() + motionCount;
        const size_t classIndex = keyIndex + 1u;
        if (description.colorAttachments.size() != classIndex + 1u ||
            description.depthAttachment.texture != expectedDepthTarget ||
            description.depthAttachment.isReadOnly ||
            description.shadingRateAttachment.valid())
        {
            return false;
        }

        const nvrhi::TextureDesc& key =
            expectedSurfaceKeyTarget->getDesc();
        for (size_t index = 0u;
            index < expectedGBufferTargets.size(); ++index)
        {
            const nvrhi::FramebufferAttachment& attachment =
                description.colorAttachments[index];
            const nvrhi::TextureDesc& target =
                expectedGBufferTargets[index]->getDesc();
            if (attachment.texture != expectedGBufferTargets[index] ||
                attachment.isReadOnly ||
                EffectiveAttachmentFormat(attachment) != target.format ||
                !HasSameMultisampleTopology(target, key))
            {
                return false;
            }
        }
        if (expectedMotionTarget)
        {
            const nvrhi::FramebufferAttachment& attachment =
                description.colorAttachments[expectedGBufferTargets.size()];
            const nvrhi::TextureDesc& target =
                expectedMotionTarget->getDesc();
            if (attachment.texture != expectedMotionTarget ||
                attachment.isReadOnly ||
                EffectiveAttachmentFormat(attachment) != target.format ||
                !HasSameMultisampleTopology(target, key))
            {
                return false;
            }
        }

        if (description.colorAttachments[keyIndex].texture !=
                expectedSurfaceKeyTarget ||
            description.colorAttachments[keyIndex].isReadOnly ||
            description.colorAttachments[classIndex].texture !=
                expectedVisibilityClassTarget ||
            description.colorAttachments[classIndex].isReadOnly ||
            EffectiveAttachmentFormat(description.colorAttachments[keyIndex]) !=
                nvrhi::Format::RG32_UINT ||
            EffectiveAttachmentFormat(
                description.colorAttachments[classIndex]) !=
                expectedVisibilityClassTarget->getDesc().format ||
            !uvsr::adaptive_msaa::IsCompactVisibilityClassFormat(
                EffectiveAttachmentFormat(
                    description.colorAttachments[classIndex])) ||
            EffectiveAttachmentFormat(description.depthAttachment) !=
                expectedDepthTarget->getDesc().format)
        {
            return false;
        }

        const nvrhi::FramebufferInfo& info =
            framebuffer->getFramebufferInfo();
        if (info.colorFormats.size() != description.colorAttachments.size() ||
            info.depthFormat != EffectiveAttachmentFormat(
                description.depthAttachment) ||
            info.sampleCount != key.sampleCount ||
            info.sampleQuality != key.sampleQuality)
        {
            return false;
        }
        for (size_t index = 0u;
            index < description.colorAttachments.size(); ++index)
        {
            if (info.colorFormats[index] != EffectiveAttachmentFormat(
                    description.colorAttachments[index]))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] uint32_t ResolveGeometryOrdinal(
        const MeshInfo* mesh,
        const MeshGeometry* geometry,
        FallbackReason& reason)
    {
        if (!mesh || !geometry)
        {
            reason = FallbackReason::ReconstructionFailure;
            return InvalidVisibilityWord;
        }

        uint32_t ordinal = InvalidVisibilityWord;
        for (size_t index = 0u; index < mesh->geometries.size(); ++index)
        {
            if (mesh->geometries[index].get() != geometry)
                continue;
            if (index >= InvalidVisibilityWord)
            {
                reason = FallbackReason::CapacityOverflow;
                return InvalidVisibilityWord;
            }
            if (ordinal != InvalidVisibilityWord)
            {
                reason = FallbackReason::TransientMappingCollision;
                return InvalidVisibilityWord;
            }
            ordinal = static_cast<uint32_t>(index);
        }

        if (ordinal == InvalidVisibilityWord)
            reason = FallbackReason::TransientMappingFailure;
        return ordinal;
    }

    [[nodiscard]] CompactRasterByteRange MakeRange(
        const nvrhi::BufferRange& range,
        uint32_t stride)
    {
        return { range.byteOffset, range.byteSize, stride };
    }

    [[nodiscard]] CompactClipBoundsConditioning
        EvaluateDrawViewConditioning(
            const DrawItem& item,
            const IView& view,
            const CompactRasterCpuIndexProof& proof,
            const CompactRasterCpuIndexProofs& cpuIndexProofs,
            CompactTriangleConditioning& triangleConditioning)
    {
        triangleConditioning = {};
        CompactClipBoundsConditioning result;
        if (!proof.positionBoundsValid || !item.instance ||
            !item.instance->GetNode())
        {
            return result;
        }

        CompactClipBoundsConditioningInput input;
        input.positionBoundsMin = proof.positionBoundsMin;
        input.positionBoundsMax = proof.positionBoundsMax;

        float objectToWorld[12]{};
        affineToColumnMajor(
            item.instance->GetNode()->GetLocalToWorldTransformFloat(),
            objectToWorld);
        for (uint32_t row = 0u; row < 3u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
                input.objectToWorld[row][column] =
                    objectToWorld[row * 4u + column];
        }

        PlanarViewConstants viewConstants{};
        view.FillPlanarViewConstants(viewConstants);
        for (uint32_t row = 0u; row < 4u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                input.worldToClip[row][column] =
                    viewConstants.matWorldToClip.m_data[
                        row * 4u + column];
            }
        }
        input.clipToWindowScale = {
            viewConstants.clipToWindowScale.x,
            viewConstants.clipToWindowScale.y
        };
        input.clipToWindowBias = {
            viewConstants.clipToWindowBias.x,
            viewConstants.clipToWindowBias.y
        };
        const nvrhi::ViewportState& viewport = view.GetViewportState();
        if (viewport.viewports.size() != 1u)
            return result;
        input.viewportMinimumDepth = viewport.viewports[0].minZ;
        input.viewportMaximumDepth = viewport.viewports[0].maxZ;
        result = EvaluateCompactClipBoundsConditioning(input);
        if (!result.safe)
            return result;
        if (!cpuIndexProofs.EvaluateTriangleConditioning(
                item.mesh,
                item.geometry,
                item.buffers,
                input,
                triangleConditioning))
        {
            result.safe = false;
        }
        return result;
    }

    [[nodiscard]] CompactRasterDrawPreflightDesc DescribeDraw(
        const DrawItem& item,
        const IView& view,
        uint32_t viewIndex,
        uint32_t drawIndex,
        const CompactSceneSnapshot& compactScene,
        const CompactRasterCpuIndexProofs& cpuIndexProofs,
        FallbackReason& constructionReason,
        const CompactRasterDrawPreflightDesc*
            reusableConditioning = nullptr)
    {
        CompactRasterDrawPreflightDesc result;
        result.sourceViewIndex = viewIndex;
        result.sourceDrawIndex = drawIndex;

        result.geometryPresent = item.instance && item.mesh && item.geometry &&
            item.material && item.buffers;
        if (!result.geometryPresent)
            return result;

        result.instanceMeshMatches =
            item.instance->GetMesh().get() == item.mesh;
        result.bufferGroupMatchesMesh = item.mesh->buffers.get() ==
            item.buffers;

        result.meshUsesTriangleList = item.mesh->type ==
            donut::engine::MeshType::Triangles;
        result.geometryUsesTriangleList = item.geometry->type ==
            donut::engine::MeshGeometryPrimitiveType::Triangles;

        const bool skinned = item.mesh->skinPrototype != nullptr;
        const bool morphTarget = item.mesh->isMorphTargetAnimationMesh ||
            (item.mesh->skinPrototype &&
                item.mesh->skinPrototype->isMorphTargetAnimationMesh);
        // Current conservative bounds prove only undeformed position streams.
        // Material eligibility, including texture semantics, is frozen in the
        // immutable snapshot after certificate validation. Do not re-invent a
        // pointer-presence texture gate here: disabled texture objects are not
        // shader-visible, while qualified enabled textures must remain
        // reachable. Exact material/fingerprint and authority checks repeat
        // before command recording.
        result.deformationSupported = !skinned && !morphTarget;
        result.materialSupported =
            item.material->domain == MaterialDomain::Opaque ||
            item.material->domain == MaterialDomain::AlphaTested;
        switch (item.material->domain)
        {
        case MaterialDomain::Opaque:
        case MaterialDomain::AlphaBlended:
        case MaterialDomain::Transmissive:
        case MaterialDomain::TransmissiveAlphaTested:
        case MaterialDomain::TransmissiveAlphaBlended:
        case MaterialDomain::AlphaTested:
            result.fullGBufferRasterSupported = true;
            break;
        default:
            break;
        }
        result.alphaClassificationMatches =
            (item.material->domain == MaterialDomain::AlphaTested) ==
            (item.geometry->material &&
                item.geometry->material->domain ==
                    MaterialDomain::AlphaTested) &&
            item.geometry->material.get() == item.material;
        result.cullModeMatches = item.cullMode ==
            (item.material->doubleSided
                ? nvrhi::RasterCullMode::None
                : nvrhi::RasterCullMode::Back);
        const nvrhi::VariableRateShadingState shadingRate =
            view.GetVariableRateShadingState();
        result.oneByOneShadingRate = !shadingRate.enabled ||
            (shadingRate.shadingRate ==
                    nvrhi::VariableShadingRate::e1x1 &&
                shadingRate.pipelinePrimitiveCombiner ==
                    nvrhi::ShadingRateCombiner::Passthrough &&
                shadingRate.imageCombiner ==
                    nvrhi::ShadingRateCombiner::Passthrough);

        const BufferGroup& buffers = *item.buffers;
        result.buffersPresent = buffers.indexBuffer && buffers.vertexBuffer &&
            buffers.instanceBuffer;
        if (!result.buffersPresent)
            return result;

        const nvrhi::BufferDesc& instanceBufferDesc =
            buffers.instanceBuffer->getDesc();
        const nvrhi::BufferDesc& indexBufferDesc =
            buffers.indexBuffer->getDesc();
        const nvrhi::BufferDesc& vertexBufferDesc =
            buffers.vertexBuffer->getDesc();
        result.instanceBufferValid =
            instanceBufferDesc.structStride == sizeof(InstanceData) &&
            instanceBufferDesc.byteSize >= instanceBufferDesc.structStride &&
            instanceBufferDesc.byteSize % instanceBufferDesc.structStride == 0u;
        result.indexBufferValid = indexBufferDesc.canHaveRawViews &&
            indexBufferDesc.format == nvrhi::Format::R32_UINT;
        result.vertexBufferValid = vertexBufferDesc.canHaveRawViews;
        result.instanceBufferByteSize = instanceBufferDesc.byteSize;
        result.instanceBufferStride = instanceBufferDesc.structStride;
        result.indexBufferByteSize = indexBufferDesc.byteSize;
        result.vertexBufferByteSize = vertexBufferDesc.byteSize;

        result.instanceIndex = item.instance->GetInstanceIndex();
        result.firstGeometryInstanceIndex =
            item.instance->GetGeometryInstanceIndex();
        result.instanceCount = result.instanceBufferValid
            ? instanceBufferDesc.byteSize / instanceBufferDesc.structStride
            : 0u;
        result.instanceGeometryCount = item.mesh->geometries.size();
        result.geometryLocalIndex = ResolveGeometryOrdinal(
            item.mesh,
            item.geometry,
            constructionReason);
        result.handleMapCount = compactScene.CpuInstanceHandles().size();

        uint64_t flattened = 0u;
        if (constructionReason == FallbackReason::None &&
            result.firstGeometryInstanceIndex >= 0 &&
            TryAdd(
                static_cast<uint64_t>(result.firstGeometryInstanceIndex),
                result.geometryLocalIndex,
                flattened) &&
            flattened < compactScene.CpuInstanceHandles().size())
        {
            result.mappedGeometryInstanceHandle =
                compactScene.CpuInstanceHandles()[
                    static_cast<size_t>(flattened)];
        }

        bool reconstructionConditioningRequired = false;
        const DecodedGeometryInstanceHandle conditioningHandle =
            DecodeGeometryInstanceHandle(
                result.mappedGeometryInstanceHandle);
        if (conditioningHandle.Succeeded() &&
            conditioningHandle.slot <
                compactScene.CpuGeometryInstanceRecords().size())
        {
            const CompactGeometryInstanceGpuRecord& conditioningRecord =
                compactScene.CpuGeometryInstanceRecords()[
                    conditioningHandle.slot];
            reconstructionConditioningRequired =
                static_cast<CompactGeometryPayloadClass>(
                    conditioningRecord.payloadClass) ==
                CompactGeometryPayloadClass::Reconstruct;
        }

        result.meshFirstIndex = item.mesh->indexOffset;
        result.geometryFirstIndex = item.geometry->indexOffsetInMesh;
        result.indexCount = item.geometry->numIndices;
        result.meshFirstVertex = item.mesh->vertexOffset;
        result.geometryFirstVertex = item.geometry->vertexOffsetInMesh;
        result.vertexCount = item.geometry->numVertices;
        result.positionRange = MakeRange(
            buffers.getVertexBufferRange(
                donut::engine::VertexAttribute::Position),
            PositionStride);
        result.previousPositionRange = skinned
            ? MakeRange(
                buffers.getVertexBufferRange(
                    donut::engine::VertexAttribute::PrevPosition),
                PositionStride)
            : result.positionRange;
        result.texCoordRange = MakeRange(
            buffers.getVertexBufferRange(
                donut::engine::VertexAttribute::TexCoord1),
            TexCoordStride);
        result.normalRange = MakeRange(
            buffers.getVertexBufferRange(
                donut::engine::VertexAttribute::Normal),
            PackedDirectionStride);
        result.tangentRange = MakeRange(
            buffers.getVertexBufferRange(
                donut::engine::VertexAttribute::Tangent),
            PackedDirectionStride);
        CompactRasterCpuIndexProof indexProof;
        result.immutableIndexProofPresent = cpuIndexProofs.Find(
            item.mesh,
            item.geometry,
            item.buffers,
            indexProof);
        if (result.immutableIndexProofPresent)
        {
            result.proofFirstIndex = indexProof.firstIndex;
            result.proofIndexCount = indexProof.indexCount;
            result.proofFirstVertex = indexProof.firstVertex;
            result.proofVertexCount = indexProof.vertexCount;
            result.proofContentRevision = indexProof.contentRevision;
            result.proofMaximumIndex = indexProof.maximumIndex;
            if (reconstructionConditioningRequired)
            {
                result.viewConditioningProofPresent =
                    indexProof.positionBoundsValid;
                if (reusableConditioning &&
                    reusableConditioning->proofContentRevision ==
                        indexProof.contentRevision)
                {
                    result.viewConditioningSafe =
                        reusableConditioning->viewConditioningSafe;
                    result.directionTransformFinite =
                        reusableConditioning->directionTransformFinite;
                    result.triangleConditioningProofPresent =
                        reusableConditioning->
                            triangleConditioningProofPresent;
                    result.triangleConditioningSafe =
                        reusableConditioning->triangleConditioningSafe;
                    result.minimumClipW =
                        reusableConditioning->minimumClipW;
                    result.maximumAbsoluteNdc =
                        reusableConditioning->maximumAbsoluteNdc;
                    result.minimumAbsoluteProjectedArea =
                        reusableConditioning->
                            minimumAbsoluteProjectedArea;
                    result.minimumAbsoluteSnappedArea =
                        reusableConditioning->
                            minimumAbsoluteSnappedArea;
                    result.minimumSnapTieDistance =
                        reusableConditioning->minimumSnapTieDistance;
                    result.minimumStoredDeviceDepth =
                        reusableConditioning->minimumStoredDeviceDepth;
                    result.maximumStoredDeviceDepth =
                        reusableConditioning->maximumStoredDeviceDepth;
                    result.storedDeviceDepthErrorMargin =
                        reusableConditioning->storedDeviceDepthErrorMargin;
                    result.maximumAbsoluteAffineBarycentric =
                        reusableConditioning->
                            maximumAbsoluteAffineBarycentric;
                    result.minimumPerspectiveDenominator =
                        reusableConditioning->minimumPerspectiveDenominator;
                    result.maximumAbsolutePerspectiveBarycentric =
                        reusableConditioning->
                            maximumAbsolutePerspectiveBarycentric;
                    result.maximumAbsoluteTexCoord =
                        reusableConditioning->maximumAbsoluteTexCoord;
                    result.maximumAbsoluteTexCoordGradient =
                        reusableConditioning->
                            maximumAbsoluteTexCoordGradient;
                }
                else
                {
                    CompactTriangleConditioning triangleConditioning;
                    const CompactClipBoundsConditioning conditioning =
                        EvaluateDrawViewConditioning(
                            item,
                            view,
                            indexProof,
                            cpuIndexProofs,
                            triangleConditioning);
                    result.viewConditioningSafe = conditioning.safe;
                    result.directionTransformFinite =
                        conditioning.directionTransformFinite;
                    result.triangleConditioningProofPresent =
                        triangleConditioning.triangleCount ==
                            indexProof.indexCount / 3u;
                    result.triangleConditioningSafe =
                        triangleConditioning.safe;
                    result.minimumClipW = conditioning.minimumClipW;
                    result.maximumAbsoluteNdc =
                        conditioning.maximumAbsoluteNdc;
                    result.minimumAbsoluteProjectedArea =
                        triangleConditioning.minimumAbsoluteProjectedArea;
                    result.minimumAbsoluteSnappedArea =
                        triangleConditioning.minimumAbsoluteSnappedArea;
                    result.minimumSnapTieDistance =
                        triangleConditioning.minimumSnapTieDistance;
                    result.minimumStoredDeviceDepth =
                        triangleConditioning.minimumStoredDeviceDepth;
                    result.maximumStoredDeviceDepth =
                        triangleConditioning.maximumStoredDeviceDepth;
                    result.storedDeviceDepthErrorMargin =
                        triangleConditioning.storedDeviceDepthErrorMargin;
                    result.maximumAbsoluteAffineBarycentric =
                        triangleConditioning.
                            maximumAbsoluteAffineBarycentric;
                    result.minimumPerspectiveDenominator =
                        triangleConditioning.minimumPerspectiveDenominator;
                    result.maximumAbsolutePerspectiveBarycentric =
                        triangleConditioning.
                            maximumAbsolutePerspectiveBarycentric;
                    result.maximumAbsoluteTexCoord =
                        triangleConditioning.maximumAbsoluteTexCoord;
                    result.maximumAbsoluteTexCoordGradient =
                        triangleConditioning.
                            maximumAbsoluteTexCoordGradient;
                }
            }
        }

        const CompactSceneGpuConstants& constants = compactScene.GpuConstants();
        result.geometryInstanceRecordCount =
            compactScene.CpuGeometryInstanceRecords().size();
        result.geometryRecordCount = compactScene.CpuGeometryRecords().size();
        result.materialRecordCount = constants.materialRecordCount;
        result.textureDescriptorCount = constants.textureDescriptorCount;
        result.rawBufferDescriptorCount = constants.rawBufferDescriptorCount;
        result.descriptorCount = constants.descriptorCount;
        result.expectedTopologyEpoch = MakeUint64(
            constants.topologyEpochLow,
            constants.topologyEpochHigh);
        result.expectedTopologyRevision = constants.topologyRevision;
        result.expectedLodRevision = constants.lodRevision;
        result.expectedDeformationRevision = constants.deformationRevision;
        result.expectedMaterialRevision = constants.materialRevision;
        result.expectedGeometryFlags = CompactGeometryFlag_None;
        if (skinned)
            result.expectedGeometryFlags |= CompactGeometryFlag_Skinned;
        if (item.material->domain == MaterialDomain::AlphaTested)
            result.expectedGeometryFlags |= CompactGeometryFlag_AlphaTested;
        if (item.material->doubleSided)
            result.expectedGeometryFlags |= CompactGeometryFlag_DoubleSided;

        const DecodedGeometryInstanceHandle decoded =
            DecodeGeometryInstanceHandle(
                result.mappedGeometryInstanceHandle);
        if (decoded.Succeeded() &&
            decoded.slot <
                compactScene.CpuGeometryInstanceRecords().size())
        {
            const CompactGeometryInstanceGpuRecord& record =
                compactScene.CpuGeometryInstanceRecords()[decoded.slot];
            result.geometryInstanceRecordValid =
                (record.flags & CompactGeometryInstanceFlag_Valid) != 0u;
            result.geometryInstanceDeformationSupported =
                (record.flags &
                    CompactGeometryInstanceFlag_DeformationSupported) != 0u;
            result.geometryInstanceMaterialSupported =
                (record.flags &
                    CompactGeometryInstanceFlag_MaterialSupported) != 0u;
            result.recordGeneration = record.generation;
            result.recordInstanceIndex = record.instanceIndex;
            result.recordGeometryIndex = record.geometryIndex;
            result.recordGeometryRecordIndex = record.geometryRecordIndex;
            result.recordPrimitiveCount = record.primitiveCount;
            result.recordStableInstanceHandle = MakeUint64(
                record.stableInstanceHandleLow,
                record.stableInstanceHandleHigh);
            result.recordPayloadClass =
                static_cast<CompactGeometryPayloadClass>(
                    record.payloadClass);
            result.recordPayloadFallbackReason =
                static_cast<FallbackReason>(
                    record.payloadFallbackReason);
            result.recordTopologyEpoch = MakeUint64(
                record.topologyEpochLow,
                record.topologyEpochHigh);
            result.recordTopologyRevision = record.topologyRevision;
            result.recordLodRevision = record.lodRevision;
            result.recordDeformationRevision = record.deformationRevision;
            result.recordMaterialRevision = record.materialRevision;

            if (record.geometryRecordIndex <
                compactScene.CpuGeometryRecords().size())
            {
                const CompactGeometryGpuRecord& geometry =
                    compactScene.CpuGeometryRecords()[
                        record.geometryRecordIndex];
                result.recordIndexBufferDescriptor =
                    geometry.indexBufferDescriptor;
                result.recordVertexBufferDescriptor =
                    geometry.vertexBufferDescriptor;
                result.recordIndexOffset = geometry.indexOffset;
                result.recordPositionOffset = geometry.positionOffset;
                result.recordPreviousPositionOffset =
                    geometry.previousPositionOffset;
                result.recordTexCoordOffset = geometry.texCoordOffset;
                result.recordNormalOffset = geometry.normalOffset;
                result.recordTangentOffset = geometry.tangentOffset;
                result.recordIndexCount = geometry.indexCount;
                result.recordVertexCount = geometry.vertexCount;
                result.recordMaterialIndex = geometry.materialIndex;
                if (geometry.materialIndex <
                    compactScene.CpuMaterialTextureDescriptors().size())
                {
                    result.recordMaterialTextureMask =
                        compactScene.CpuMaterialTextureDescriptors()[
                            geometry.materialIndex].enabledMask;
                }
                result.recordGeometryFlags = geometry.flags;
            }
        }

        return result;
    }

    struct CapturedView
    {
        uint32_t sourceViewIndex = InvalidVisibilityWord;
        const IView* view = nullptr;
        const IView* fallbackPreviousView = nullptr;
        nvrhi::FramebufferHandle framebuffer;
        nvrhi::FramebufferHandle fallbackFramebuffer;
        bool textureReconstructionQualified = true;
        std::vector<DrawItem> draws;
    };

    using RasterViewIdentity = CompactRasterViewTransformIdentity;

    [[nodiscard]] RasterViewIdentity CaptureRasterViewIdentity(
        const IView& view)
    {
        return CaptureCompactRasterViewTransformIdentity(view);
    }

    [[nodiscard]] bool SameRasterViewIdentity(
        const RasterViewIdentity& first,
        const RasterViewIdentity& second)
    {
        return SameCompactRasterViewTransformIdentity(first, second);
    }

    [[nodiscard]] std::array<float, 12> CaptureObjectToWorld(
        const DrawItem& item)
    {
        std::array<float, 12> result{};
        if (item.instance && item.instance->GetNode())
        {
            affineToColumnMajor(
                item.instance->GetNode()->GetLocalToWorldTransformFloat(),
                result.data());
        }
        return result;
    }

    [[nodiscard]] bool SameObjectToWorld(
        const std::array<float, 12>& first,
        const std::array<float, 12>& second)
    {
        for (size_t index = 0u; index < first.size(); ++index)
        {
            if (!SameFloatBits(first[index], second[index]))
                return false;
        }
        return true;
    }

    struct BufferIdentity
    {
        nvrhi::IBuffer* buffer = nullptr;
        nvrhi::GpuVirtualAddress gpuAddress = 0u;
        uint64_t byteSize = 0u;
        uint32_t structStride = 0u;
        nvrhi::Format format = nvrhi::Format::UNKNOWN;
        bool canHaveUavs = false;
        bool canHaveRawViews = false;
        bool isVertexBuffer = false;
        bool isIndexBuffer = false;
        bool isVolatile = false;
        bool isVirtual = false;
        nvrhi::CpuAccessMode cpuAccess = nvrhi::CpuAccessMode::None;
    };

    [[nodiscard]] BufferIdentity CaptureBufferIdentity(
        nvrhi::IBuffer* buffer)
    {
        BufferIdentity result;
        result.buffer = buffer;
        if (!buffer)
            return result;
        const nvrhi::BufferDesc& description = buffer->getDesc();
        result.gpuAddress = buffer->getGpuVirtualAddress();
        result.byteSize = description.byteSize;
        result.structStride = description.structStride;
        result.format = description.format;
        result.canHaveUavs = description.canHaveUAVs;
        result.canHaveRawViews = description.canHaveRawViews;
        result.isVertexBuffer = description.isVertexBuffer;
        result.isIndexBuffer = description.isIndexBuffer;
        result.isVolatile = description.isVolatile;
        result.isVirtual = description.isVirtual;
        result.cpuAccess = description.cpuAccess;
        return result;
    }

    [[nodiscard]] bool SameBufferIdentity(
        const BufferIdentity& first,
        const BufferIdentity& second)
    {
        return first.buffer == second.buffer &&
            first.gpuAddress == second.gpuAddress &&
            first.byteSize == second.byteSize &&
            first.structStride == second.structStride &&
            first.format == second.format &&
            first.canHaveUavs == second.canHaveUavs &&
            first.canHaveRawViews == second.canHaveRawViews &&
            first.isVertexBuffer == second.isVertexBuffer &&
            first.isIndexBuffer == second.isIndexBuffer &&
            first.isVolatile == second.isVolatile &&
            first.isVirtual == second.isVirtual &&
            first.cpuAccess == second.cpuAccess;
    }

    struct PreparedDraw
    {
        DrawItem sourceItem{};
        std::shared_ptr<donut::engine::SceneGraphLeaf>
            instanceKeepAlive;
        std::shared_ptr<donut::engine::MeshInfo> meshKeepAlive;
        std::shared_ptr<donut::engine::MeshGeometry>
            geometryKeepAlive;
        std::shared_ptr<donut::engine::Material> materialKeepAlive;
        std::shared_ptr<donut::engine::BufferGroup> buffersKeepAlive;
        uint32_t materialRecordIndex = InvalidVisibilityWord;
        CompactRasterDrawPreflightDesc description;
        CompactRasterValidatedDraw validated;
        CompactRasterDrawClass drawClass =
            CompactRasterDrawClass::Fatal;
        FallbackReason fallbackReason =
            FallbackReason::ReconstructionFailure;
        std::array<float, 12> objectToWorld{};
        BufferIdentity instanceBufferIdentity;
        BufferIdentity indexBufferIdentity;
        BufferIdentity vertexBufferIdentity;
        nvrhi::GraphicsState state;
        AdaptiveMsaaGBufferPushConstants push{};
        nvrhi::DrawArguments arguments;
        nvrhi::GraphicsPipelineHandle pipelineKeepAlive;
        std::vector<nvrhi::BindingSetHandle> bindingKeepAlive;
        nvrhi::BufferHandle indexBufferKeepAlive;
        nvrhi::BufferHandle vertexBufferKeepAlive;
        nvrhi::BufferHandle instanceBufferKeepAlive;
        uintptr_t materialIdentity = 0u;
        std::string materialName;
        uint64_t triangleCount = 0u;
    };

    struct PreparedView
    {
        uint32_t sourceViewIndex = InvalidVisibilityWord;
        const IView* view = nullptr;
        const IView* fallbackPreviousView = nullptr;
        nvrhi::FramebufferHandle framebufferKeepAlive;
        nvrhi::FramebufferHandle fallbackFramebufferKeepAlive;
        nvrhi::ViewportState viewport;
        nvrhi::VariableRateShadingState shadingRateState;
        bool mirrored = false;
        bool reverseDepth = false;
        RasterViewIdentity rasterIdentity;
        RasterViewIdentity fallbackPreviousRasterIdentity;
        std::vector<PreparedDraw> draws;
    };

    [[nodiscard]] bool SameViewportState(
        const nvrhi::ViewportState& first,
        const nvrhi::ViewportState& second)
    {
        return SameCompactRasterViewportState(first, second);
    }

    struct CompactTextureQualificationViewDecision
    {
        bool valid = false;
        bool textureReconstructionQualified = false;
        FallbackReason reason =
            FallbackReason::TextureQualificationMismatch;
    };

    [[nodiscard]] CompactTextureQualificationViewDecision
        EvaluateTextureQualificationForView(
        const CompactSceneSnapshot& snapshot,
        const TextureQualificationCertificateAuthority* authority,
        const CompactRasterViewProof& proof)
    {
        if (!snapshot.RequiresTextureQualification())
        {
            const bool valid = snapshot.TextureQualification().IsEmpty();
            return {
                valid,
                valid,
                valid
                    ? FallbackReason::None
                    : FallbackReason::TextureQualificationMismatch
            };
        }

        const CompactTextureQualificationIdentity identity =
            snapshot.TextureQualification();
        if (!identity.IsValid() || !authority ||
            !authority->IsActive(
                identity.qualificationToken,
                identity.contentRevision))
        {
            return {};
        }

        TextureQualificationValidationRequest request;
        if (BuildCompactRasterTextureQualificationRequest(proof, request) !=
            FallbackReason::None)
        {
            // Sample-quality, sample-count, or viewport-origin evidence is
            // view-local. The exact texture-bearing draws can still use the
            // ordinary local full-G-buffer path while untextured records keep
            // compact visibility ownership and reconstruction.
            return {
                true,
                false,
                FallbackReason::TextureQualificationMismatch
            };
        }
        const TextureQualificationValidationResult validation =
            authority->ValidateActive(request);
        const bool qualified = validation.Succeeded();
        if (!qualified &&
            !IsCompactTextureQualificationViewLocalFailure(
                validation.reason))
        {
            return {
                false,
                false,
                FallbackReason::TextureQualificationMismatch
            };
        }
        return {
            true,
            qualified,
            qualified
                ? FallbackReason::None
                : FallbackReason::TextureQualificationMismatch
        };
    }

    [[nodiscard]] bool IsValidAcceptedDepthInterval(
        const CompactRasterAcceptedDepthInterval& interval)
    {
        if (!interval.valid)
            return false;
        if (!interval.hasGeometry)
        {
            return SameFloatBits(
                    interval.minimumStoredDeviceDepth,
                    0.f) &&
                SameFloatBits(
                    interval.maximumStoredDeviceDepth,
                    0.f) &&
                SameFloatBits(interval.conservativeErrorMargin, 0.f);
        }
        return std::isfinite(interval.minimumStoredDeviceDepth) &&
            std::isfinite(interval.maximumStoredDeviceDepth) &&
            std::isfinite(interval.conservativeErrorMargin) &&
            interval.minimumStoredDeviceDepth > 0.f &&
            interval.minimumStoredDeviceDepth <=
                interval.maximumStoredDeviceDepth &&
            interval.maximumStoredDeviceDepth <= 1.f &&
            interval.conservativeErrorMargin >= 0.f;
    }

    [[nodiscard]] CompactRasterAcceptedDepthInterval
        MakeEmptyAcceptedDepthInterval()
    {
        CompactRasterAcceptedDepthInterval result;
        result.valid = true;
        return result;
    }

    [[nodiscard]] bool AccumulateAcceptedDepthInterval(
        CompactRasterAcceptedDepthInterval& interval,
        const CompactRasterDrawPreflightDesc& description)
    {
        if (!interval.valid ||
            !std::isfinite(description.minimumStoredDeviceDepth) ||
            !std::isfinite(description.maximumStoredDeviceDepth) ||
            !std::isfinite(description.storedDeviceDepthErrorMargin) ||
            !(description.minimumStoredDeviceDepth > 0.f) ||
            description.minimumStoredDeviceDepth >
                description.maximumStoredDeviceDepth ||
            description.maximumStoredDeviceDepth > 1.f ||
            description.storedDeviceDepthErrorMargin < 0.f ||
            !std::isfinite(
                description.maximumAbsoluteAffineBarycentric) ||
            description.maximumAbsoluteAffineBarycentric < 0.f ||
            description.maximumAbsoluteAffineBarycentric >
                AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric ||
            !std::isfinite(
                description.minimumPerspectiveDenominator) ||
            description.minimumPerspectiveDenominator <
                AdaptiveMsaaCompactMinimumPerspectiveDenominator ||
            !std::isfinite(
                description.maximumAbsolutePerspectiveBarycentric) ||
            description.maximumAbsolutePerspectiveBarycentric < 0.f ||
            description.maximumAbsolutePerspectiveBarycentric >
                AdaptiveMsaaCompactMaximumAbsolutePerspectiveBarycentric ||
            !std::isfinite(description.maximumAbsoluteTexCoord) ||
            description.maximumAbsoluteTexCoord < 0.f ||
            description.maximumAbsoluteTexCoord >
                AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient ||
            !std::isfinite(
                description.maximumAbsoluteTexCoordGradient) ||
            description.maximumAbsoluteTexCoordGradient < 0.f ||
            description.maximumAbsoluteTexCoordGradient >
                AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient)
        {
            interval = {};
            return false;
        }

        if (!interval.hasGeometry)
        {
            interval.hasGeometry = true;
            interval.minimumStoredDeviceDepth =
                description.minimumStoredDeviceDepth;
            interval.maximumStoredDeviceDepth =
                description.maximumStoredDeviceDepth;
            interval.conservativeErrorMargin =
                description.storedDeviceDepthErrorMargin;
        }
        else
        {
            interval.minimumStoredDeviceDepth = std::min(
                interval.minimumStoredDeviceDepth,
                description.minimumStoredDeviceDepth);
            interval.maximumStoredDeviceDepth = std::max(
                interval.maximumStoredDeviceDepth,
                description.maximumStoredDeviceDepth);
            interval.conservativeErrorMargin = std::max(
                interval.conservativeErrorMargin,
                description.storedDeviceDepthErrorMargin);
        }
        return IsValidAcceptedDepthInterval(interval);
    }

    [[nodiscard]] bool SameAcceptedDepthInterval(
        const CompactRasterAcceptedDepthInterval& first,
        const CompactRasterAcceptedDepthInterval& second)
    {
        return first.valid == second.valid &&
            first.hasGeometry == second.hasGeometry &&
            SameFloatBits(
                first.minimumStoredDeviceDepth,
                second.minimumStoredDeviceDepth) &&
            SameFloatBits(
                first.maximumStoredDeviceDepth,
                second.maximumStoredDeviceDepth) &&
            SameFloatBits(
                first.conservativeErrorMargin,
                second.conservativeErrorMargin);
    }

    [[nodiscard]] bool SameViewProofResources(
        const CompactRasterViewProof& proof,
        const PreparedView& prepared)
    {
        if (proof.sourceViewIndex != prepared.sourceViewIndex ||
            !proof.view || proof.view != prepared.view ||
            !prepared.framebufferKeepAlive ||
            !proof.surfaceKeyTarget || !proof.visibilityClassTarget ||
            !proof.depthTarget ||
            !IsValidAcceptedDepthInterval(proof.acceptedDepth) ||
            proof.fallbackMayStoreZeroDepth !=
                proof.fullGBufferFallbackPresent ||
            !SameViewportState(proof.viewport, prepared.viewport) ||
            !SameRasterViewIdentity(
                proof.transformIdentity,
                prepared.rasterIdentity) ||
            !SameViewportState(
                proof.viewport,
                proof.view->GetViewportState()) ||
            !SameRasterViewIdentity(
                proof.transformIdentity,
                CaptureRasterViewIdentity(*proof.view)) ||
            proof.view->GetSubresources() !=
                proof.surfaceKeySubresources ||
            proof.view->GetSubresources() !=
                proof.visibilityClassSubresources ||
            proof.view->GetSubresources() != proof.depthSubresources ||
            !IsCompactFramebuffer(
                prepared.framebufferKeepAlive.Get(),
                proof.surfaceKeyTarget.Get(),
                proof.visibilityClassTarget.Get(),
                proof.depthTarget.Get()))
        {
            return false;
        }

        const nvrhi::FramebufferDesc& framebuffer =
            prepared.framebufferKeepAlive->getDesc();
        const nvrhi::FramebufferAttachment& keyAttachment =
            framebuffer.colorAttachments[0];
        const nvrhi::FramebufferAttachment& classAttachment =
            framebuffer.colorAttachments[1];
        const nvrhi::FramebufferAttachment& depthAttachment =
            framebuffer.depthAttachment;
        const nvrhi::TextureDesc& key =
            proof.surfaceKeyTarget->getDesc();
        const nvrhi::TextureDesc& visibilityClass =
            proof.visibilityClassTarget->getDesc();
        const nvrhi::TextureDesc& depth = proof.depthTarget->getDesc();
        const bool compactResourcesMatch =
            keyAttachment.texture == proof.surfaceKeyTarget.Get() &&
            classAttachment.texture ==
                proof.visibilityClassTarget.Get() &&
            depthAttachment.texture == proof.depthTarget.Get() &&
            keyAttachment.subresources ==
                proof.surfaceKeySubresources &&
            classAttachment.subresources ==
                proof.visibilityClassSubresources &&
            depthAttachment.subresources == proof.depthSubresources &&
            EffectiveAttachmentFormat(keyAttachment) ==
                proof.surfaceKeyFormat &&
            EffectiveAttachmentFormat(classAttachment) ==
                proof.visibilityClassFormat &&
            EffectiveAttachmentFormat(depthAttachment) ==
                proof.depthFormat &&
            proof.surfaceKeyFormat == nvrhi::Format::RG32_UINT &&
            IsCompactVisibilityClassFormat(
                proof.visibilityClassFormat) &&
            IsD32DepthFormat(proof.depthFormat) &&
            key.sampleCount == proof.sampleCount &&
            visibilityClass.sampleCount == proof.sampleCount &&
            depth.sampleCount == proof.sampleCount &&
            key.sampleQuality == proof.sampleQuality &&
            visibilityClass.sampleQuality == proof.sampleQuality &&
            depth.sampleQuality == proof.sampleQuality;
        if (!compactResourcesMatch)
            return false;

        if (!proof.fullGBufferFallbackPresent)
        {
            if (prepared.fallbackFramebufferKeepAlive ||
                prepared.fallbackPreviousView ||
                proof.fallbackMotionTarget)
            {
                return false;
            }
            return std::all_of(
                proof.fallbackGBufferTargets.begin(),
                proof.fallbackGBufferTargets.end(),
                [](const nvrhi::TextureHandle& target)
                {
                    return !target;
                });
        }

        if (!prepared.fallbackFramebufferKeepAlive ||
            !prepared.fallbackPreviousView ||
            !SameRasterViewIdentity(
                prepared.fallbackPreviousRasterIdentity,
                CaptureRasterViewIdentity(
                    *prepared.fallbackPreviousView)))
            return false;
        std::array<nvrhi::ITexture*, 5u> expectedFallbackTargets{};
        for (size_t index = 0u;
            index < expectedFallbackTargets.size(); ++index)
        {
            expectedFallbackTargets[index] =
                proof.fallbackGBufferTargets[index].Get();
            if (!expectedFallbackTargets[index])
                return false;
        }
        if (!IsMixedFallbackFramebuffer(
                prepared.fallbackFramebufferKeepAlive.Get(),
                expectedFallbackTargets,
                proof.fallbackMotionTarget.Get(),
                proof.surfaceKeyTarget.Get(),
                proof.visibilityClassTarget.Get(),
                proof.depthTarget.Get()))
        {
            return false;
        }

        const nvrhi::FramebufferDesc& fallbackFramebuffer =
            prepared.fallbackFramebufferKeepAlive->getDesc();
        for (size_t index = 0u;
            index < proof.fallbackGBufferTargets.size(); ++index)
        {
            if (fallbackFramebuffer.colorAttachments[index].subresources !=
                    proof.fallbackGBufferSubresources[index] ||
                proof.fallbackGBufferSubresources[index] !=
                    proof.view->GetSubresources())
            {
                return false;
            }
        }
        const size_t motionCount = proof.fallbackMotionTarget ? 1u : 0u;
        const size_t keyIndex =
            proof.fallbackGBufferTargets.size() + motionCount;
        const size_t classIndex = keyIndex + 1u;
        if (proof.fallbackMotionTarget &&
            (fallbackFramebuffer.colorAttachments[
                    proof.fallbackGBufferTargets.size()].subresources !=
                    proof.fallbackMotionSubresources ||
                proof.fallbackMotionSubresources !=
                    proof.view->GetSubresources()))
        {
            return false;
        }
        return fallbackFramebuffer.colorAttachments[keyIndex].subresources ==
                proof.surfaceKeySubresources &&
            fallbackFramebuffer.colorAttachments[classIndex].subresources ==
                proof.visibilityClassSubresources &&
            fallbackFramebuffer.depthAttachment.subresources ==
                proof.depthSubresources;
    }

    [[nodiscard]] bool SameByteRange(
        const CompactRasterByteRange& first,
        const CompactRasterByteRange& second)
    {
        return first.byteOffset == second.byteOffset &&
            first.byteSize == second.byteSize &&
            first.elementStride == second.elementStride;
    }

    [[nodiscard]] bool SameDrawDescription(
        const CompactRasterDrawPreflightDesc& first,
        const CompactRasterDrawPreflightDesc& second)
    {
        return first.sourceViewIndex == second.sourceViewIndex &&
            first.sourceDrawIndex == second.sourceDrawIndex &&
            first.geometryPresent == second.geometryPresent &&
            first.instanceMeshMatches == second.instanceMeshMatches &&
            first.bufferGroupMatchesMesh ==
                second.bufferGroupMatchesMesh &&
            first.meshUsesTriangleList == second.meshUsesTriangleList &&
            first.geometryUsesTriangleList ==
                second.geometryUsesTriangleList &&
            first.deformationSupported == second.deformationSupported &&
            first.materialSupported == second.materialSupported &&
            first.buffersPresent == second.buffersPresent &&
            first.instanceBufferValid == second.instanceBufferValid &&
            first.indexBufferValid == second.indexBufferValid &&
            first.vertexBufferValid == second.vertexBufferValid &&
            first.fullGBufferRasterSupported ==
                second.fullGBufferRasterSupported &&
            first.alphaClassificationMatches ==
                second.alphaClassificationMatches &&
            first.cullModeMatches == second.cullModeMatches &&
            first.oneByOneShadingRate == second.oneByOneShadingRate &&
            first.viewConditioningProofPresent ==
                second.viewConditioningProofPresent &&
            first.viewConditioningSafe == second.viewConditioningSafe &&
            first.directionTransformFinite ==
                second.directionTransformFinite &&
            first.triangleConditioningProofPresent ==
                second.triangleConditioningProofPresent &&
            first.triangleConditioningSafe ==
                second.triangleConditioningSafe &&
            SameFloatBits(first.minimumClipW, second.minimumClipW) &&
            SameFloatBits(
                first.maximumAbsoluteNdc,
                second.maximumAbsoluteNdc) &&
            SameFloatBits(
                first.minimumAbsoluteProjectedArea,
                second.minimumAbsoluteProjectedArea) &&
            first.minimumAbsoluteSnappedArea ==
                second.minimumAbsoluteSnappedArea &&
            SameFloatBits(
                first.minimumSnapTieDistance,
                second.minimumSnapTieDistance) &&
            SameFloatBits(
                first.minimumStoredDeviceDepth,
                second.minimumStoredDeviceDepth) &&
            SameFloatBits(
                first.maximumStoredDeviceDepth,
                second.maximumStoredDeviceDepth) &&
            SameFloatBits(
                first.storedDeviceDepthErrorMargin,
                second.storedDeviceDepthErrorMargin) &&
            SameFloatBits(
                first.maximumAbsoluteAffineBarycentric,
                second.maximumAbsoluteAffineBarycentric) &&
            SameFloatBits(
                first.minimumPerspectiveDenominator,
                second.minimumPerspectiveDenominator) &&
            SameFloatBits(
                first.maximumAbsolutePerspectiveBarycentric,
                second.maximumAbsolutePerspectiveBarycentric) &&
            SameFloatBits(
                first.maximumAbsoluteTexCoord,
                second.maximumAbsoluteTexCoord) &&
            SameFloatBits(
                first.maximumAbsoluteTexCoordGradient,
                second.maximumAbsoluteTexCoordGradient) &&
            first.instanceIndex == second.instanceIndex &&
            first.firstGeometryInstanceIndex ==
                second.firstGeometryInstanceIndex &&
            first.instanceCount == second.instanceCount &&
            first.instanceGeometryCount == second.instanceGeometryCount &&
            first.geometryLocalIndex == second.geometryLocalIndex &&
            first.handleMapCount == second.handleMapCount &&
            first.mappedGeometryInstanceHandle ==
                second.mappedGeometryInstanceHandle &&
            first.instanceBufferByteSize ==
                second.instanceBufferByteSize &&
            first.instanceBufferStride == second.instanceBufferStride &&
            first.indexBufferByteSize == second.indexBufferByteSize &&
            first.vertexBufferByteSize == second.vertexBufferByteSize &&
            first.meshFirstIndex == second.meshFirstIndex &&
            first.geometryFirstIndex == second.geometryFirstIndex &&
            first.indexCount == second.indexCount &&
            first.meshFirstVertex == second.meshFirstVertex &&
            first.geometryFirstVertex == second.geometryFirstVertex &&
            first.vertexCount == second.vertexCount &&
            SameByteRange(first.positionRange, second.positionRange) &&
            SameByteRange(
                first.previousPositionRange,
                second.previousPositionRange) &&
            SameByteRange(first.texCoordRange, second.texCoordRange) &&
            SameByteRange(first.normalRange, second.normalRange) &&
            SameByteRange(first.tangentRange, second.tangentRange) &&
            first.immutableIndexProofPresent ==
                second.immutableIndexProofPresent &&
            first.proofFirstIndex == second.proofFirstIndex &&
            first.proofIndexCount == second.proofIndexCount &&
            first.proofFirstVertex == second.proofFirstVertex &&
            first.proofVertexCount == second.proofVertexCount &&
            first.proofContentRevision == second.proofContentRevision &&
            first.proofMaximumIndex == second.proofMaximumIndex &&
            first.geometryInstanceRecordCount ==
                second.geometryInstanceRecordCount &&
            first.geometryInstanceRecordValid ==
                second.geometryInstanceRecordValid &&
            first.geometryInstanceDeformationSupported ==
                second.geometryInstanceDeformationSupported &&
            first.geometryInstanceMaterialSupported ==
                second.geometryInstanceMaterialSupported &&
            first.recordGeneration == second.recordGeneration &&
            first.recordInstanceIndex == second.recordInstanceIndex &&
            first.recordGeometryIndex == second.recordGeometryIndex &&
            first.recordGeometryRecordIndex ==
                second.recordGeometryRecordIndex &&
            first.recordPrimitiveCount == second.recordPrimitiveCount &&
            first.recordStableInstanceHandle ==
                second.recordStableInstanceHandle &&
            first.recordPayloadClass == second.recordPayloadClass &&
            first.recordPayloadFallbackReason ==
                second.recordPayloadFallbackReason &&
            first.expectedTopologyEpoch == second.expectedTopologyEpoch &&
            first.recordTopologyEpoch == second.recordTopologyEpoch &&
            first.expectedTopologyRevision ==
                second.expectedTopologyRevision &&
            first.recordTopologyRevision ==
                second.recordTopologyRevision &&
            first.expectedLodRevision == second.expectedLodRevision &&
            first.recordLodRevision == second.recordLodRevision &&
            first.expectedDeformationRevision ==
                second.expectedDeformationRevision &&
            first.recordDeformationRevision ==
                second.recordDeformationRevision &&
            first.expectedMaterialRevision ==
                second.expectedMaterialRevision &&
            first.recordMaterialRevision == second.recordMaterialRevision &&
            first.geometryRecordCount == second.geometryRecordCount &&
            first.materialRecordCount == second.materialRecordCount &&
            first.textureDescriptorCount == second.textureDescriptorCount &&
            first.rawBufferDescriptorCount ==
                second.rawBufferDescriptorCount &&
            first.descriptorCount == second.descriptorCount &&
            first.recordIndexBufferDescriptor ==
                second.recordIndexBufferDescriptor &&
            first.recordVertexBufferDescriptor ==
                second.recordVertexBufferDescriptor &&
            first.recordIndexOffset == second.recordIndexOffset &&
            first.recordPositionOffset == second.recordPositionOffset &&
            first.recordPreviousPositionOffset ==
                second.recordPreviousPositionOffset &&
            first.recordTexCoordOffset == second.recordTexCoordOffset &&
            first.recordNormalOffset == second.recordNormalOffset &&
            first.recordTangentOffset == second.recordTangentOffset &&
            first.recordIndexCount == second.recordIndexCount &&
            first.recordVertexCount == second.recordVertexCount &&
            first.recordMaterialIndex == second.recordMaterialIndex &&
            first.recordMaterialTextureMask ==
                second.recordMaterialTextureMask &&
            first.textureReconstructionQualified ==
                second.textureReconstructionQualified &&
            first.expectedGeometryFlags == second.expectedGeometryFlags &&
            first.recordGeometryFlags == second.recordGeometryFlags;
    }

    [[nodiscard]] CompactRasterPreparedBindingDesc CapturePreparedBinding(
        const nvrhi::GraphicsState& state,
        const AdaptiveMsaaGBufferPushConstants& push,
        const nvrhi::DrawArguments& arguments)
    {
        CompactRasterPreparedBindingDesc result;
        result.indexBindingByteOffset = state.indexBuffer.offset;
        result.indexBindingIsR32Uint =
            state.indexBuffer.format == nvrhi::Format::R32_UINT;
        result.drawStartIndexLocation =
            arguments.startIndexLocation;
        result.drawStartVertexLocation =
            arguments.startVertexLocation;
        result.drawStartInstanceLocation =
            arguments.startInstanceLocation;
        result.pushStartInstanceLocation =
            push.startInstanceLocation;
        result.pushStartVertexLocation = push.startVertexLocation;
        result.pushPositionOffset = push.positionOffset;
        result.pushPreviousPositionOffset = push.prevPositionOffset;
        result.pushTexCoordOffset = push.texCoordOffset;
        result.pushNormalOffset = push.normalOffset;
        result.pushTangentOffset = push.tangentOffset;
        result.pushVisibilityClass = push.visibilityClass;
        return result;
    }

    [[nodiscard]] bool ValidateSnapshotContract(
        const CompactSceneSnapshot& compactScene,
        const AdaptiveMsaaCompactVisibilityPass& pass)
    {
        const CompactSceneGpuConstants& constants = compactScene.GpuConstants();
        const nvrhi::IBuffer* map = compactScene.InstanceHandleBuffer();
        const nvrhi::IBuffer* instances =
            compactScene.GeometryInstanceBuffer();
        const nvrhi::IBuffer* geometries = compactScene.GeometryBuffer();
        return compactScene.Eligibility().Succeeded() &&
            compactScene.ValidateReadResources() == FallbackReason::None &&
            pass.PreparationEpoch() != 0u &&
            compactScene.RequiredCommandQueue() ==
                CompactSceneCommandQueue &&
            map && instances && geometries && compactScene.MaterialBuffer() &&
            compactScene.SceneInstanceBuffer() && compactScene.BindlessLayout() &&
            compactScene.DescriptorTable() &&
            pass.GetGeometryInstanceHandleMap() == map &&
            IsCompactMapBuffer(map) &&
            map->getDesc().byteSize / sizeof(uint32_t) ==
                compactScene.CpuInstanceHandles().size() &&
            instances->getDesc().structStride ==
                sizeof(CompactGeometryInstanceGpuRecord) &&
            geometries->getDesc().structStride ==
                sizeof(CompactGeometryGpuRecord) &&
            constants.instanceHandleCount ==
                compactScene.CpuInstanceHandles().size() &&
            constants.geometryInstanceRecordCount ==
                compactScene.CpuGeometryInstanceRecords().size() &&
            constants.geometryRecordCount ==
                compactScene.CpuGeometryRecords().size() &&
            compactScene.TopologyEpoch() == MakeUint64(
                constants.topologyEpochLow,
                constants.topologyEpochHigh);
    }

    [[nodiscard]] bool ValidateFallbackPassContract(
        const CompactSceneSnapshot& compactScene,
        const uvsr::AdaptiveMsaaGBufferFillPass& pass,
        donut::engine::ViewType::Enum expectedViewTypes)
    {
        return pass.PreparationEpoch() != 0u &&
            pass.HasValidGeometryInstanceHandleMap() &&
            pass.GetGeometryInstanceHandleMap() ==
                compactScene.InstanceHandleBuffer() &&
            pass.GetSupportedViewTypes() == expectedViewTypes;
    }
}

namespace uvsr::adaptive_msaa
{
    bool IsCompactTextureQualificationViewLocalFailure(
        TextureQualificationReason reason)
    {
        switch (reason)
        {
        case TextureQualificationReason::Requested16Resolved8Unsupported:
        case TextureQualificationReason::Requested16Unsupported:
        case TextureQualificationReason::Resolved16Unsupported:
        case TextureQualificationReason::UnsupportedRequestedSampleCount:
        case TextureQualificationReason::UnsupportedResolvedSampleCount:
        case TextureQualificationReason::RequestedResolvedSampleCountMismatch:
        case TextureQualificationReason::InvalidViewportOriginParity:
            return true;
        default:
            return false;
        }
    }

    FallbackReason BuildCompactRasterTextureQualificationRequest(
        const CompactRasterViewProof& proof,
        TextureQualificationValidationRequest& request)
    {
        request = {};
        const auto IsSupportedSampleCount = [](uint32_t sampleCount)
        {
            return sampleCount == 2u || sampleCount == 4u ||
                sampleCount == 8u || sampleCount == 16u;
        };
        if (!IsSupportedSampleCount(proof.requestedSampleCount) ||
            !IsSupportedSampleCount(proof.sampleCount) ||
            proof.sampleQuality != 0u ||
            proof.viewport.viewports.size() != 1u)
        {
            return FallbackReason::TextureQualificationMismatch;
        }
#if UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC
        if (proof.requestedSampleCount != 4u || proof.sampleCount != 4u)
            return FallbackReason::TextureQualificationMismatch;
#endif

        const auto CaptureParity = [](float origin, uint32_t& parity)
        {
            if (!std::isfinite(origin) || origin < 0.f ||
                origin > float(std::numeric_limits<int32_t>::max()) ||
                std::floor(origin) != origin)
            {
                return false;
            }
            parity = static_cast<uint32_t>(origin) & 1u;
            return true;
        };
        const nvrhi::Viewport& viewport = proof.viewport.viewports[0];
        request.requestedSampleCount = proof.requestedSampleCount;
        request.resolvedSampleCount = proof.sampleCount;
        if (!CaptureParity(
                viewport.minX,
                request.viewportOriginXParity) ||
            !CaptureParity(
                viewport.minY,
                request.viewportOriginYParity))
        {
            request = {};
            return FallbackReason::TextureQualificationMismatch;
        }
        return FallbackReason::None;
    }

    struct CompactRasterCpuIndexProofs::Impl
    {
        using ObjectPosition = std::array<float, 3>;
        using ObjectTexCoord = std::array<float, 2>;
        struct ObjectTriangle
        {
            std::array<ObjectPosition, 3> positions{};
            std::array<ObjectTexCoord, 3> texCoords{};
        };

        struct Entry
        {
            struct ConditioningCacheRecord
            {
                CompactClipBoundsConditioningInput input{};
                CompactTriangleConditioning output{};
                bool occupied = false;
                bool succeeded = false;
            };

            const donut::engine::MeshInfo* mesh = nullptr;
            const donut::engine::BufferGroup* buffers = nullptr;
            CompactRasterCpuIndexProof proof;
            uint64_t sourceIndexDataSize = 0u;
            uint64_t sourcePositionDataSize = 0u;
            uint64_t sourceTexCoordDataSize = 0u;
            uint64_t sourceNormalDataSize = 0u;
            uint64_t sourceTangentDataSize = 0u;
            uint64_t meshFirstIndex = 0u;
            uint64_t meshFirstVertex = 0u;
            uint64_t geometryFirstIndex = 0u;
            uint64_t geometryFirstVertex = 0u;
            uint64_t geometryIndexCount = 0u;
            uint64_t geometryVertexCount = 0u;
            const donut::engine::Material* material = nullptr;
            std::vector<uint32_t> sourceIndices;
            std::vector<ObjectPosition> sourcePositions;
            std::vector<std::array<float, 2>> sourceTexCoords;
            std::vector<uint32_t> sourceNormals;
            std::vector<uint32_t> sourceTangents;
            std::vector<ObjectTriangle> triangles;
            // The exact per-triangle proof is view dependent and can cost
            // tens of milliseconds on a multi-million-triangle scene. Keep a
            // small, bounded cache for bit-identical transforms only. Any
            // camera jitter, object motion, viewport change, or proof rebuild
            // misses and recomputes conservatively.
            std::array<ConditioningCacheRecord, 4u> conditioningCache{};
            size_t nextConditioningCacheRecord = 0u;
        };

        const donut::engine::SceneGraph* sceneGraph = nullptr;
        uint64_t buildRevision = 0u;
        std::unordered_map<const donut::engine::MeshGeometry*, Entry>
            entries;
        std::mutex conditioningCacheMutex;
    };

    CompactRasterCpuIndexProofs::CompactRasterCpuIndexProofs()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    CompactRasterCpuIndexProofs::~CompactRasterCpuIndexProofs() = default;

    bool CompactRasterCpuIndexProofs::IsForSceneGraph(
        const donut::engine::SceneGraph* sceneGraph) const
    {
        return sceneGraph && m_Impl && m_Impl->sceneGraph == sceneGraph;
    }

    uint64_t CompactRasterCpuIndexProofs::BuildRevision() const
    {
        return m_Impl ? m_Impl->buildRevision : 0u;
    }

    size_t CompactRasterCpuIndexProofs::GeometryCount() const
    {
        return m_Impl ? m_Impl->entries.size() : 0u;
    }

    bool CompactRasterCpuIndexProofs::Find(
        const donut::engine::MeshInfo* mesh,
        const donut::engine::MeshGeometry* geometry,
        const donut::engine::BufferGroup* buffers,
        CompactRasterCpuIndexProof& output) const
    {
        output = {};
        if (!m_Impl || !mesh || !geometry || !buffers)
            return false;
        const auto found = m_Impl->entries.find(geometry);
        const Impl::Entry* entry = found == m_Impl->entries.end()
            ? nullptr
            : &found->second;
        if (found == m_Impl->entries.end() ||
            !entry || entry->mesh != mesh || entry->buffers != buffers ||
            entry->proof.contentRevision == 0u ||
            mesh->type != donut::engine::MeshType::Triangles ||
            geometry->type !=
                donut::engine::MeshGeometryPrimitiveType::Triangles ||
            mesh->buffers.get() != buffers ||
            mesh->indexOffset != entry->meshFirstIndex ||
            mesh->vertexOffset != entry->meshFirstVertex ||
            geometry->indexOffsetInMesh != entry->geometryFirstIndex ||
            geometry->vertexOffsetInMesh != entry->geometryFirstVertex ||
            geometry->numIndices != entry->geometryIndexCount ||
            geometry->numVertices != entry->geometryVertexCount ||
            geometry->material.get() != entry->material)
        {
            return false;
        }

        // Before upload, exact source arrays are compared value-for-value.
        // After Donut's one-time upload they are intentionally empty; at that
        // point only static, non-UAV, nonvolatile GPU buffers are accepted and
        // the prepared transaction snapshots their exact pointers, VAs,
        // descriptors, stream ranges, and proof revision again before Submit.
        if (!buffers->indexData.empty())
        {
            if (buffers->indexData.size() != entry->sourceIndexDataSize ||
                entry->sourceIndices.size() !=
                    entry->proof.indexCount)
            {
                return false;
            }
            for (size_t index = 0u;
                index < entry->sourceIndices.size();
                ++index)
            {
                if (buffers->indexData[static_cast<size_t>(
                        entry->proof.firstIndex) + index] !=
                    entry->sourceIndices[index])
                {
                    return false;
                }
            }
        }
        if (!buffers->positionData.empty())
        {
            if (buffers->positionData.size() !=
                    entry->sourcePositionDataSize ||
                entry->sourcePositions.size() !=
                    entry->proof.vertexCount)
            {
                return false;
            }
            for (size_t index = 0u;
                index < entry->sourcePositions.size();
                ++index)
            {
                const dm::float3 live = buffers->positionData[
                    static_cast<size_t>(entry->proof.firstVertex) + index];
                const std::array<float, 3> canonical = {
                    CanonicalizeD3dFloat(live.x),
                    CanonicalizeD3dFloat(live.y),
                    CanonicalizeD3dFloat(live.z)
                };
                for (uint32_t component = 0u;
                    component < 3u;
                    ++component)
                {
                    if (!std::isfinite(live[component]) ||
                        !SameFloatBits(
                            canonical[component],
                            entry->sourcePositions[index][component]))
                    {
                        return false;
                    }
                }
            }
        }
        if (!buffers->texcoord1Data.empty())
        {
            if (buffers->texcoord1Data.size() !=
                    entry->sourceTexCoordDataSize ||
                entry->sourceTexCoords.size() != entry->proof.vertexCount)
            {
                return false;
            }
            for (size_t index = 0u;
                index < entry->sourceTexCoords.size();
                ++index)
            {
                const dm::float2 live = buffers->texcoord1Data[
                    static_cast<size_t>(entry->proof.firstVertex) + index];
                if (!std::isfinite(live.x) || !std::isfinite(live.y) ||
                    !SameFloatBits(live.x,
                        entry->sourceTexCoords[index][0]) ||
                    !SameFloatBits(live.y,
                        entry->sourceTexCoords[index][1]))
                {
                    return false;
                }
            }
        }
        if (!buffers->normalData.empty())
        {
            if (buffers->normalData.size() != entry->sourceNormalDataSize ||
                entry->sourceNormals.size() != entry->proof.vertexCount)
            {
                return false;
            }
            for (size_t index = 0u;
                index < entry->sourceNormals.size();
                ++index)
            {
                if (buffers->normalData[static_cast<size_t>(
                        entry->proof.firstVertex) + index] !=
                    entry->sourceNormals[index])
                {
                    return false;
                }
            }
        }
        if (!buffers->tangentData.empty())
        {
            if (buffers->tangentData.size() !=
                    entry->sourceTangentDataSize ||
                entry->sourceTangents.size() != entry->proof.vertexCount)
            {
                return false;
            }
            for (size_t index = 0u;
                index < entry->sourceTangents.size();
                ++index)
            {
                if (buffers->tangentData[static_cast<size_t>(
                        entry->proof.firstVertex) + index] !=
                    entry->sourceTangents[index])
                {
                    return false;
                }
            }
        }
        const auto IsStaticGpuBuffer = [](const nvrhi::IBuffer* buffer)
        {
            if (!buffer)
                return true;
            const nvrhi::BufferDesc& description = buffer->getDesc();
            return !description.canHaveUAVs && !description.isVolatile &&
                !description.isVirtual &&
                description.cpuAccess == nvrhi::CpuAccessMode::None;
        };
        if (!IsStaticGpuBuffer(buffers->indexBuffer) ||
            !IsStaticGpuBuffer(buffers->vertexBuffer))
        {
            return false;
        }

        output = entry->proof;
        return true;
    }

    bool CompactRasterCpuIndexProofs::EvaluateTriangleConditioning(
        const donut::engine::MeshInfo* mesh,
        const donut::engine::MeshGeometry* geometry,
        const donut::engine::BufferGroup* buffers,
        const CompactClipBoundsConditioningInput& input,
        CompactTriangleConditioning& output) const
    {
        output = {};
        CompactRasterCpuIndexProof publicProof;
        if (!Find(mesh, geometry, buffers, publicProof) || !m_Impl)
            return false;
        const auto found = m_Impl->entries.find(geometry);
        if (found == m_Impl->entries.end() ||
            found->second.proof.contentRevision !=
                publicProof.contentRevision ||
            found->second.triangles.empty())
        {
            return false;
        }

        Impl::Entry& entry = found->second;
        const auto SameInput = [](
            const CompactClipBoundsConditioningInput& left,
            const CompactClipBoundsConditioningInput& right)
        {
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                if (!SameFloatBits(
                        left.positionBoundsMin[component],
                        right.positionBoundsMin[component]) ||
                    !SameFloatBits(
                        left.positionBoundsMax[component],
                        right.positionBoundsMax[component]))
                {
                    return false;
                }
            }
            return SameCompactRasterTransformIdentity(left, right);
        };
        {
            const std::lock_guard<std::mutex> lock(
                m_Impl->conditioningCacheMutex);
            for (const Impl::Entry::ConditioningCacheRecord& cached :
                entry.conditioningCache)
            {
                if (cached.occupied && SameInput(cached.input, input))
                {
                    output = cached.output;
                    return cached.succeeded;
                }
            }
        }

        const auto CacheResult = [this, &entry, &input, &output](
            bool succeeded)
        {
            const std::lock_guard<std::mutex> lock(
                m_Impl->conditioningCacheMutex);
            Impl::Entry::ConditioningCacheRecord& cached =
                entry.conditioningCache[
                    entry.nextConditioningCacheRecord];
            cached.input = input;
            cached.output = output;
            cached.succeeded = succeeded;
            cached.occupied = true;
            entry.nextConditioningCacheRecord =
                (entry.nextConditioningCacheRecord + 1u) %
                entry.conditioningCache.size();
        };

        output.safe = true;
        output.triangleCount = entry.triangles.size();
        output.minimumAbsoluteProjectedArea =
            std::numeric_limits<float>::infinity();
        output.minimumAbsoluteSnappedArea =
            std::numeric_limits<uint64_t>::max();
        output.minimumSnapTieDistance =
            std::numeric_limits<float>::infinity();
        output.minimumStoredDeviceDepth =
            std::numeric_limits<float>::infinity();
        output.maximumStoredDeviceDepth = 0.f;
        output.storedDeviceDepthErrorMargin = 0.f;
        output.maximumAbsoluteAffineBarycentric = 0.f;
        output.minimumPerspectiveDenominator =
            std::numeric_limits<float>::infinity();
        output.maximumAbsolutePerspectiveBarycentric = 0.f;
        output.maximumAbsoluteTexCoord = 0.f;
        output.maximumAbsoluteTexCoordGradient = 0.f;
        for (const Impl::ObjectTriangle& triangle : entry.triangles)
        {
            CompactTriangleConditioningInput triangleInput;
            triangleInput.objectPositions = triangle.positions;
            triangleInput.objectTexCoords = triangle.texCoords;
            triangleInput.objectToWorld = input.objectToWorld;
            triangleInput.worldToClip = input.worldToClip;
            triangleInput.clipToWindowScale =
                input.clipToWindowScale;
            triangleInput.clipToWindowBias = input.clipToWindowBias;
            triangleInput.viewportMinimumDepth =
                input.viewportMinimumDepth;
            triangleInput.viewportMaximumDepth =
                input.viewportMaximumDepth;
            const CompactTriangleConditioning triangleResult =
                EvaluateCompactTriangleConditioning(triangleInput);
            if (!triangleResult.safe)
            {
                output = {};
                CacheResult(false);
                return false;
            }
            output.minimumAbsoluteProjectedArea = std::min(
                output.minimumAbsoluteProjectedArea,
                triangleResult.minimumAbsoluteProjectedArea);
            output.minimumAbsoluteSnappedArea = std::min(
                output.minimumAbsoluteSnappedArea,
                triangleResult.minimumAbsoluteSnappedArea);
            output.minimumSnapTieDistance = std::min(
                output.minimumSnapTieDistance,
                triangleResult.minimumSnapTieDistance);
            output.minimumStoredDeviceDepth = std::min(
                output.minimumStoredDeviceDepth,
                triangleResult.minimumStoredDeviceDepth);
            output.maximumStoredDeviceDepth = std::max(
                output.maximumStoredDeviceDepth,
                triangleResult.maximumStoredDeviceDepth);
            output.storedDeviceDepthErrorMargin = std::max(
                output.storedDeviceDepthErrorMargin,
                triangleResult.storedDeviceDepthErrorMargin);
            output.maximumAbsoluteAffineBarycentric = std::max(
                output.maximumAbsoluteAffineBarycentric,
                triangleResult.maximumAbsoluteAffineBarycentric);
            output.minimumPerspectiveDenominator = std::min(
                output.minimumPerspectiveDenominator,
                triangleResult.minimumPerspectiveDenominator);
            output.maximumAbsolutePerspectiveBarycentric = std::max(
                output.maximumAbsolutePerspectiveBarycentric,
                triangleResult.maximumAbsolutePerspectiveBarycentric);
            output.maximumAbsoluteTexCoord = std::max(
                output.maximumAbsoluteTexCoord,
                triangleResult.maximumAbsoluteTexCoord);
            output.maximumAbsoluteTexCoordGradient = std::max(
                output.maximumAbsoluteTexCoordGradient,
                triangleResult.maximumAbsoluteTexCoordGradient);
        }
        CacheResult(true);
        return true;
    }

    CompactRasterCpuIndexProofBuildResult
        BuildCompactRasterCpuIndexProofs(
            const donut::engine::SceneGraph& sceneGraph)
    {
        CompactRasterCpuIndexProofBuildResult result;
        std::shared_ptr<CompactRasterCpuIndexProofs> proofs(
            new CompactRasterCpuIndexProofs());
        proofs->m_Impl->sceneGraph = &sceneGraph;
        proofs->m_Impl->buildRevision = AllocateCompactProofRevision();
        if (proofs->m_Impl->buildRevision == 0u)
        {
            result.reason = FallbackReason::CapacityOverflow;
            return result;
        }

        for (const std::shared_ptr<donut::engine::MeshInfo>& mesh :
            sceneGraph.GetMeshes())
        {
            if (!mesh || mesh->type != donut::engine::MeshType::Triangles)
                continue;
            if (!mesh->buffers)
                continue;

            const donut::engine::BufferGroup& buffers = *mesh->buffers;
            for (const std::shared_ptr<donut::engine::MeshGeometry>& geometry :
                mesh->geometries)
            {
                if (!geometry || geometry->type !=
                        donut::engine::MeshGeometryPrimitiveType::Triangles)
                {
                    continue;
                }
                if (geometry->numIndices == 0u ||
                    geometry->numIndices % 3u != 0u ||
                    geometry->numVertices == 0u)
                {
                    result.reason = FallbackReason::PrimitiveOverflow;
                    return result;
                }

                // Donut normally releases importer arrays after upload. A
                // missing CPU reconstruction proof is therefore expected for
                // some otherwise valid full-G-buffer fallback draws; omit
                // only that geometry's proof and let raster preflight prove
                // its exact GPU identity. Partial nonempty arrays remain a
                // corruption signal and still fail below.
                if (buffers.indexData.empty() ||
                    buffers.positionData.empty() ||
                    buffers.texcoord1Data.empty() ||
                    buffers.normalData.empty() ||
                    buffers.tangentData.empty())
                {
                    continue;
                }

                uint64_t firstIndex = 0u;
                uint64_t endIndex = 0u;
                if (!TryAdd(
                        mesh->indexOffset,
                        geometry->indexOffsetInMesh,
                        firstIndex) ||
                    !TryAdd(firstIndex, geometry->numIndices, endIndex) ||
                    endIndex > buffers.indexData.size())
                {
                    result.reason = FallbackReason::ReconstructionFailure;
                    return result;
                }

                uint32_t maximumIndex = 0u;
                uint64_t firstVertex = 0u;
                uint64_t endVertex = 0u;
                if (!TryAdd(
                        mesh->vertexOffset,
                        geometry->vertexOffsetInMesh,
                        firstVertex) ||
                    !TryAdd(
                        firstVertex,
                        geometry->numVertices,
                        endVertex) ||
                    endVertex > buffers.positionData.size() ||
                    endVertex > buffers.texcoord1Data.size() ||
                    endVertex > buffers.normalData.size() ||
                    endVertex > buffers.tangentData.size())
                {
                    result.reason = FallbackReason::ReconstructionFailure;
                    return result;
                }
                std::array<float, 3> boundsMin = {
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity()
                };
                std::array<float, 3> boundsMax = {
                    -std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity()
                };
                for (uint64_t index = firstIndex; index < endIndex; ++index)
                {
                    const uint32_t vertexIndex =
                        buffers.indexData[static_cast<size_t>(index)];
                    if (vertexIndex >= geometry->numVertices)
                    {
                        result.reason = FallbackReason::ReconstructionFailure;
                        return result;
                    }
                    maximumIndex = std::max(maximumIndex, vertexIndex);
                    const dm::float3 sourcePosition = buffers.positionData[
                        static_cast<size_t>(firstVertex + vertexIndex)];
                    if (!std::isfinite(sourcePosition.x) ||
                        !std::isfinite(sourcePosition.y) ||
                        !std::isfinite(sourcePosition.z))
                    {
                        result.reason =
                            FallbackReason::ReconstructionFailure;
                        return result;
                    }
                    const std::array<float, 3> position = {
                        CanonicalizeD3dFloat(sourcePosition.x),
                        CanonicalizeD3dFloat(sourcePosition.y),
                        CanonicalizeD3dFloat(sourcePosition.z)
                    };
                    for (uint32_t component = 0u;
                        component < 3u;
                        ++component)
                    {
                        boundsMin[component] = std::min(
                            boundsMin[component],
                            position[component]);
                        boundsMax[component] = std::max(
                            boundsMax[component],
                            position[component]);
                    }
                }

                CompactRasterCpuIndexProofs::Impl::Entry entry;
                entry.mesh = mesh.get();
                entry.buffers = mesh->buffers.get();
                entry.sourceIndexDataSize = buffers.indexData.size();
                entry.sourcePositionDataSize = buffers.positionData.size();
                entry.sourceTexCoordDataSize =
                    buffers.texcoord1Data.size();
                entry.sourceNormalDataSize = buffers.normalData.size();
                entry.sourceTangentDataSize = buffers.tangentData.size();
                entry.meshFirstIndex = mesh->indexOffset;
                entry.meshFirstVertex = mesh->vertexOffset;
                entry.geometryFirstIndex =
                    geometry->indexOffsetInMesh;
                entry.geometryFirstVertex =
                    geometry->vertexOffsetInMesh;
                entry.geometryIndexCount = geometry->numIndices;
                entry.geometryVertexCount = geometry->numVertices;
                entry.material = geometry->material.get();
                entry.proof.firstIndex = firstIndex;
                entry.proof.indexCount = geometry->numIndices;
                entry.proof.firstVertex = firstVertex;
                entry.proof.vertexCount = geometry->numVertices;
                entry.proof.contentRevision =
                    AllocateCompactProofRevision();
                if (entry.proof.contentRevision == 0u)
                {
                    result.reason = FallbackReason::CapacityOverflow;
                    return result;
                }
                entry.proof.maximumIndex = maximumIndex;
                entry.proof.positionBoundsMin = boundsMin;
                entry.proof.positionBoundsMax = boundsMax;
                entry.proof.positionBoundsValid = true;
                entry.sourceIndices.reserve(
                    static_cast<size_t>(geometry->numIndices));
                for (uint64_t index = firstIndex;
                    index < endIndex;
                    ++index)
                {
                    entry.sourceIndices.push_back(
                        buffers.indexData[static_cast<size_t>(index)]);
                }
                entry.sourcePositions.reserve(
                    static_cast<size_t>(geometry->numVertices));
                entry.sourceTexCoords.reserve(
                    static_cast<size_t>(geometry->numVertices));
                entry.sourceNormals.reserve(
                    static_cast<size_t>(geometry->numVertices));
                entry.sourceTangents.reserve(
                    static_cast<size_t>(geometry->numVertices));
                for (uint64_t vertex = 0u;
                    vertex < geometry->numVertices;
                    ++vertex)
                {
                    const dm::float3 sourcePosition =
                        buffers.positionData[static_cast<size_t>(
                            firstVertex + vertex)];
                    if (!std::isfinite(sourcePosition.x) ||
                        !std::isfinite(sourcePosition.y) ||
                        !std::isfinite(sourcePosition.z))
                    {
                        result.reason =
                            FallbackReason::ReconstructionFailure;
                        return result;
                    }
                    entry.sourcePositions.push_back({
                        CanonicalizeD3dFloat(sourcePosition.x),
                        CanonicalizeD3dFloat(sourcePosition.y),
                        CanonicalizeD3dFloat(sourcePosition.z) });
                    const dm::float2 sourceTexCoord =
                        buffers.texcoord1Data[static_cast<size_t>(
                            firstVertex + vertex)];
                    if (!std::isfinite(sourceTexCoord.x) ||
                        !std::isfinite(sourceTexCoord.y))
                    {
                        result.reason =
                            FallbackReason::ReconstructionFailure;
                        return result;
                    }
                    entry.sourceTexCoords.push_back({
                        sourceTexCoord.x,
                        sourceTexCoord.y });
                    entry.sourceNormals.push_back(buffers.normalData[
                        static_cast<size_t>(firstVertex + vertex)]);
                    entry.sourceTangents.push_back(buffers.tangentData[
                        static_cast<size_t>(firstVertex + vertex)]);
                }
                entry.triangles.reserve(
                    static_cast<size_t>(
                        geometry->numIndices / 3u));
                for (size_t primitive = 0u;
                    primitive < entry.sourceIndices.size() / 3u;
                    ++primitive)
                {
                    CompactRasterCpuIndexProofs::Impl::ObjectTriangle
                        triangle{};
                    for (uint32_t corner = 0u; corner < 3u; ++corner)
                    {
                        const uint32_t vertexIndex = entry.sourceIndices[
                            primitive * 3u + corner];
                        triangle.positions[corner] =
                            entry.sourcePositions[vertexIndex];
                        triangle.texCoords[corner] =
                            entry.sourceTexCoords[vertexIndex];
                    }
                    entry.triangles.push_back(triangle);
                }
                if (!proofs->m_Impl->entries.emplace(
                        geometry.get(), entry).second)
                {
                    result.reason =
                        FallbackReason::TransientMappingCollision;
                    return result;
                }
            }
        }

        result.proofs = std::move(proofs);
        result.reason = FallbackReason::None;
        return result;
    }

    namespace
    {
        [[nodiscard]] FallbackReason
            ValidateFullGBufferFallbackPreparedBinding(
                const CompactRasterDrawPreflightDesc& description,
                const CompactRasterValidatedDraw& validatedDraw,
                FallbackReason fallbackReason,
                CompactRasterFallbackProvenance fallbackProvenance,
                const CompactRasterPreparedBindingDesc& binding);
    }

    struct CompactRasterPreparedSubmission::Impl
    {
        AdaptiveMsaaCompactVisibilityPass* pass = nullptr;
        uvsr::AdaptiveMsaaGBufferFillPass* fallbackPass = nullptr;
        std::shared_ptr<const donut::engine::SceneGraph>
            sceneGraphKeepAlive;
        std::shared_ptr<donut::engine::SceneGraphNode> rootNodeKeepAlive;
        std::shared_ptr<const CompactSceneSnapshot>
            compactSceneKeepAlive;
        const TextureQualificationCertificateAuthority*
            textureQualificationAuthority = nullptr;
        nvrhi::BufferHandle geometryInstanceHandleMapKeepAlive;
        std::shared_ptr<const CompactRasterCpuIndexProofs>
            cpuIndexProofsKeepAlive;
        CompactSceneSnapshotVersion compactSceneVersion{};
        uint64_t cpuIndexProofBuildRevision = 0u;
        uint64_t passPreparationEpoch = 0u;
        uint64_t fallbackPassPreparationEpoch = 0u;
        std::vector<PreparedView> views;
        std::vector<CompactRasterViewProof> viewProofs;
        std::string passEvent;
        std::thread::id preparationThread;
        bool materialEvents = false;
        bool validatedForSubmission = false;
    };

    CompactRasterPreparedSubmission::CompactRasterPreparedSubmission() =
        default;
    CompactRasterPreparedSubmission::~CompactRasterPreparedSubmission() =
        default;
    CompactRasterPreparedSubmission::CompactRasterPreparedSubmission(
        CompactRasterPreparedSubmission&& other) noexcept = default;
    CompactRasterPreparedSubmission&
        CompactRasterPreparedSubmission::operator=(
            CompactRasterPreparedSubmission&& other) noexcept = default;

    bool CompactRasterPreparedSubmission::IsSubmittable() const
    {
        return m_Impl != nullptr && m_Preflight.Succeeded();
    }

    const CompactRasterPreflightResult&
        CompactRasterPreparedSubmission::Preflight() const
    {
        return m_Preflight;
    }

    const std::vector<CompactRasterViewProof>&
        CompactRasterPreparedSubmission::ViewProofs() const
    {
        static const std::vector<CompactRasterViewProof> Empty;
        return m_Impl ? m_Impl->viewProofs : Empty;
    }

    CompactRasterPreflightResult
        CompactRasterPreparedSubmission::ValidateForSubmission(
            const AdaptiveMsaaCompactVisibilityPass& pass) const
    {
        CompactRasterPreflightResult result = m_Preflight;
        Impl* implementation = m_Impl.get();
        if (implementation)
            implementation->validatedForSubmission = false;
        const auto Reject = [&result, implementation](FallbackReason reason)
        {
            if (implementation)
                implementation->validatedForSubmission = false;
            result.eligible = false;
            result.reason = reason;
            return result;
        };
        if (!implementation || !result.Succeeded())
        {
            return Reject(result.reason == FallbackReason::None
                ? FallbackReason::ReconstructionFailure
                : result.reason);
        }
        if (implementation->pass != &pass ||
            implementation->preparationThread !=
                std::this_thread::get_id() ||
            !implementation->sceneGraphKeepAlive ||
            !implementation->rootNodeKeepAlive ||
            !implementation->compactSceneKeepAlive ||
            !implementation->cpuIndexProofsKeepAlive ||
            implementation->sceneGraphKeepAlive->GetRootNode().get() !=
                implementation->rootNodeKeepAlive.get() ||
            !implementation->cpuIndexProofsKeepAlive->IsForSceneGraph(
                implementation->sceneGraphKeepAlive.get()) ||
            implementation->cpuIndexProofsKeepAlive->BuildRevision() !=
                implementation->cpuIndexProofBuildRevision ||
            implementation->compactSceneKeepAlive->Version().topologyEpoch !=
                implementation->compactSceneVersion.topologyEpoch ||
            implementation->compactSceneKeepAlive->Version().buildSerial !=
                implementation->compactSceneVersion.buildSerial ||
            !ValidateSnapshotContract(
                *implementation->compactSceneKeepAlive,
                pass) ||
            pass.GetGeometryInstanceHandleMap() !=
                implementation->geometryInstanceHandleMapKeepAlive.Get() ||
            pass.PreparationEpoch() !=
                implementation->passPreparationEpoch ||
            (result.fallbackDrawCount != 0u &&
                (!implementation->fallbackPass ||
                    !ValidateFallbackPassContract(
                        *implementation->compactSceneKeepAlive,
                        *implementation->fallbackPass,
                        pass.GetSupportedViewTypes()) ||
                    implementation->fallbackPass->PreparationEpoch() !=
                        implementation->fallbackPassPreparationEpoch)) ||
            (result.fallbackDrawCount == 0u &&
                implementation->fallbackPass != nullptr))
        {
            return Reject(FallbackReason::TransientMappingFailure);
        }

        uint64_t preparedDrawCount = 0u;
        uint64_t preparedTriangleCount = 0u;
        uint64_t preparedReconstructDrawCount = 0u;
        uint64_t preparedReconstructTriangleCount = 0u;
        uint64_t preparedFallbackDrawCount = 0u;
        uint64_t preparedFallbackTriangleCount = 0u;
        const auto& materialFingerprints =
            implementation->compactSceneKeepAlive->
                CpuMaterialFingerprints();
        const auto& materialDescriptorRecords =
            implementation->compactSceneKeepAlive->
                CpuMaterialTextureDescriptors();
        if (materialFingerprints.size() !=
                materialDescriptorRecords.size() ||
            materialFingerprints.size() !=
                implementation->compactSceneKeepAlive->
                    GpuConstants().materialRecordCount)
        {
            return Reject(FallbackReason::TransientMappingFailure);
        }
        std::vector<uint8_t> validatedMaterialRecords(
            materialFingerprints.size(),
            0u);
        if (implementation->views.size() !=
            implementation->viewProofs.size())
        {
            return Reject(FallbackReason::ReconstructionFailure);
        }
        for (size_t viewIndex = 0u;
            viewIndex < implementation->views.size();
            ++viewIndex)
        {
            const PreparedView& view = implementation->views[viewIndex];
            const CompactRasterViewProof& viewProof =
                implementation->viewProofs[viewIndex];
            const CompactTextureQualificationViewDecision
                textureQualification =
                    EvaluateTextureQualificationForView(
                    *implementation->compactSceneKeepAlive,
                    implementation->textureQualificationAuthority,
                    viewProof);
            if (!textureQualification.valid)
                return Reject(textureQualification.reason);
            if (!view.view || !view.framebufferKeepAlive ||
                view.sourceViewIndex !=
                    static_cast<uint32_t>(viewIndex) ||
                !SameViewProofResources(viewProof, view) ||
                view.view->IsMirrored() != view.mirrored ||
                view.view->IsReverseDepth() != view.reverseDepth ||
                !SameViewportState(
                    view.view->GetViewportState(),
                    view.viewport) ||
                view.view->GetVariableRateShadingState() !=
                    view.shadingRateState ||
                !SameRasterViewIdentity(
                    CaptureRasterViewIdentity(*view.view),
                    view.rasterIdentity))
            {
                return Reject(FallbackReason::TransientMappingFailure);
            }
            CompactRasterAcceptedDepthInterval currentAcceptedDepth =
                MakeEmptyAcceptedDepthInterval();
            bool currentViewHasFallback = false;
            for (const PreparedDraw& draw : view.draws)
            {
                if (!draw.instanceKeepAlive || !draw.meshKeepAlive ||
                    !draw.geometryKeepAlive || !draw.materialKeepAlive ||
                    !draw.buffersKeepAlive ||
                    draw.sourceItem.instance !=
                        draw.instanceKeepAlive.get() ||
                    draw.sourceItem.mesh != draw.meshKeepAlive.get() ||
                    draw.sourceItem.geometry !=
                        draw.geometryKeepAlive.get() ||
                    draw.sourceItem.material !=
                        draw.materialKeepAlive.get() ||
                    draw.sourceItem.buffers !=
                        draw.buffersKeepAlive.get() ||
                    !draw.sourceItem.instance->GetNode() ||
                    draw.sourceItem.instance->GetNode()->GetLeaf().get() !=
                        draw.instanceKeepAlive.get() ||
                    draw.sourceItem.instance->GetMesh().get() !=
                        draw.meshKeepAlive.get() ||
                    draw.sourceItem.mesh->buffers.get() !=
                        draw.buffersKeepAlive.get() ||
                    !SameObjectToWorld(
                        CaptureObjectToWorld(draw.sourceItem),
                        draw.objectToWorld) ||
                    !SameBufferIdentity(
                        CaptureBufferIdentity(
                            draw.sourceItem.buffers->instanceBuffer),
                        draw.instanceBufferIdentity) ||
                    !SameBufferIdentity(
                        CaptureBufferIdentity(
                            draw.sourceItem.buffers->indexBuffer),
                        draw.indexBufferIdentity) ||
                    !SameBufferIdentity(
                        CaptureBufferIdentity(
                            draw.sourceItem.buffers->vertexBuffer),
                        draw.vertexBufferIdentity) ||
                    !draw.pipelineKeepAlive ||
                    !draw.indexBufferKeepAlive ||
                    !draw.vertexBufferKeepAlive ||
                    !draw.instanceBufferKeepAlive ||
                    draw.indexBufferKeepAlive.Get() !=
                        draw.indexBufferIdentity.buffer ||
                    draw.vertexBufferKeepAlive.Get() !=
                        draw.vertexBufferIdentity.buffer ||
                    draw.instanceBufferKeepAlive.Get() !=
                        draw.instanceBufferIdentity.buffer ||
                    draw.state.pipeline != draw.pipelineKeepAlive.Get() ||
                    draw.state.framebuffer !=
                        (draw.drawClass ==
                            CompactRasterDrawClass::Reconstruct
                            ? view.framebufferKeepAlive.Get()
                            : view.fallbackFramebufferKeepAlive.Get()) ||
                    draw.state.indexBuffer.buffer !=
                        draw.indexBufferKeepAlive.Get() ||
                    draw.state.bindings.size() !=
                        draw.bindingKeepAlive.size())
                {
                    return Reject(FallbackReason::ReconstructionFailure);
                }
                const std::shared_ptr<const donut::engine::Material>
                    currentMaterial =
                        draw.sourceItem.geometry->material;
                if (draw.drawClass ==
                    CompactRasterDrawClass::Reconstruct)
                {
                    if (draw.materialRecordIndex >=
                            materialFingerprints.size() ||
                        draw.materialRecordIndex !=
                            draw.description.recordMaterialIndex)
                    {
                        return Reject(
                            FallbackReason::TransientMappingFailure);
                    }
                    const CompactMaterialFingerprint& fingerprint =
                        materialFingerprints[draw.materialRecordIndex];
                    if (fingerprint.materialKeepAlive.get() !=
                            draw.materialKeepAlive.get() ||
                        fingerprint.materialKeepAlive.get() !=
                            currentMaterial.get())
                    {
                        return Reject(
                            FallbackReason::MaterialRevisionMismatch);
                    }
                    if (validatedMaterialRecords[
                            draw.materialRecordIndex] == 0u)
                    {
                        const FallbackReason materialReason =
                            ValidateCompactMaterialFingerprint(
                                fingerprint,
                                currentMaterial,
                                materialDescriptorRecords[
                                    draw.materialRecordIndex],
                                implementation->compactSceneKeepAlive->
                                    TextureQualification());
                        if (materialReason != FallbackReason::None)
                            return Reject(materialReason);
                        validatedMaterialRecords[
                            draw.materialRecordIndex] = 1u;
                    }
                }
                else if (draw.materialRecordIndex !=
                        InvalidVisibilityWord ||
                    currentMaterial.get() !=
                        draw.materialKeepAlive.get())
                {
                    return Reject(FallbackReason::ReconstructionFailure);
                }
                FallbackReason currentDescriptionReason =
                    FallbackReason::None;
                CompactRasterDrawPreflightDesc currentDescription =
                    DescribeDraw(
                        draw.sourceItem,
                        *view.view,
                        draw.description.sourceViewIndex,
                        draw.description.sourceDrawIndex,
                        *implementation->compactSceneKeepAlive,
                        *implementation->cpuIndexProofsKeepAlive,
                        currentDescriptionReason,
                        &draw.description);
                currentDescription.textureReconstructionQualified =
                    textureQualification.textureReconstructionQualified;
                CompactRasterDrawClassificationResult currentClassification;
                if (currentDescriptionReason == FallbackReason::None)
                {
                    currentClassification =
                        ClassifyCompactRasterDraw(currentDescription);
                }
                else
                {
                    currentClassification.reason =
                        currentDescriptionReason;
                }
                const CompactRasterPreparedBindingDesc currentBinding =
                    CapturePreparedBinding(
                        draw.state,
                        draw.push,
                        draw.arguments);
                const FallbackReason bindingReason =
                    draw.drawClass ==
                        CompactRasterDrawClass::Reconstruct
                    ? ValidateCompactRasterPreparedBinding(
                        currentDescription,
                        currentClassification.draw,
                        currentBinding)
                    : ValidateFullGBufferFallbackPreparedBinding(
                        currentDescription,
                        currentClassification.draw,
                        currentClassification.reason,
                        currentClassification.fallbackProvenance,
                        currentBinding);
                const bool depthProofValid =
                    currentClassification.Reconstructs()
                    ? AccumulateAcceptedDepthInterval(
                        currentAcceptedDepth,
                        currentDescription)
                    : true;
                if (!currentClassification.Succeeded() ||
                    currentClassification.drawClass != draw.drawClass ||
                    currentClassification.reason != draw.fallbackReason ||
                    !SameDrawDescription(
                        currentDescription,
                        draw.description) ||
                    !depthProofValid ||
                    bindingReason != FallbackReason::None)
                {
                    return Reject(
                        currentDescriptionReason != FallbackReason::None
                            ? currentDescriptionReason
                            : FallbackReason::TransientMappingFailure);
                }
                for (size_t bindingIndex = 0u;
                    bindingIndex < draw.bindingKeepAlive.size();
                    ++bindingIndex)
                {
                    if (!draw.bindingKeepAlive[bindingIndex] ||
                        draw.state.bindings[bindingIndex] !=
                            draw.bindingKeepAlive[bindingIndex].Get())
                    {
                        return Reject(
                            FallbackReason::ReconstructionFailure);
                    }
                }
                if (!TryAdd(
                        preparedDrawCount,
                        1u,
                        preparedDrawCount) ||
                    !TryAdd(
                        preparedTriangleCount,
                        draw.triangleCount,
                        preparedTriangleCount))
                {
                    return Reject(FallbackReason::CapacityOverflow);
                }
                uint64_t& classDrawCount =
                    draw.drawClass ==
                        CompactRasterDrawClass::Reconstruct
                    ? preparedReconstructDrawCount
                    : preparedFallbackDrawCount;
                uint64_t& classTriangleCount =
                    draw.drawClass ==
                        CompactRasterDrawClass::Reconstruct
                    ? preparedReconstructTriangleCount
                    : preparedFallbackTriangleCount;
                if (!TryAdd(classDrawCount, 1u, classDrawCount) ||
                    !TryAdd(
                        classTriangleCount,
                        draw.triangleCount,
                        classTriangleCount))
                {
                    return Reject(FallbackReason::CapacityOverflow);
                }
                currentViewHasFallback = currentViewHasFallback ||
                    draw.drawClass ==
                        CompactRasterDrawClass::FullGBufferFallback;
            }
            if (!SameAcceptedDepthInterval(
                    currentAcceptedDepth,
                    viewProof.acceptedDepth) ||
                currentViewHasFallback !=
                    viewProof.fullGBufferFallbackPresent)
            {
                return Reject(FallbackReason::TransientMappingFailure);
            }
        }
        if (preparedDrawCount != result.drawCount ||
            preparedTriangleCount != result.triangleCount ||
            preparedReconstructDrawCount !=
                result.reconstructDrawCount ||
            preparedReconstructTriangleCount !=
                result.reconstructTriangleCount ||
            preparedFallbackDrawCount != result.fallbackDrawCount ||
            preparedFallbackTriangleCount !=
                result.fallbackTriangleCount)
        {
            return Reject(FallbackReason::ReconstructionFailure);
        }
        implementation->validatedForSubmission = true;
        return result;
    }

    CompactRasterViewTransformIdentity
        CaptureCompactRasterViewTransformIdentity(
            const donut::engine::IView& view)
    {
        PlanarViewConstants constants{};
        view.FillPlanarViewConstants(constants);
        CompactRasterViewTransformIdentity result;
        for (uint32_t index = 0u; index < 16u; ++index)
        {
            result.worldToClip[index] =
                constants.matWorldToClip.m_data[index];
        }
        result.clipToWindowScale = {
            constants.clipToWindowScale.x,
            constants.clipToWindowScale.y
        };
        result.clipToWindowBias = {
            constants.clipToWindowBias.x,
            constants.clipToWindowBias.y
        };
        return result;
    }

    bool SameCompactRasterViewTransformIdentity(
        const CompactRasterViewTransformIdentity& prepared,
        const CompactRasterViewTransformIdentity& current)
    {
        for (size_t index = 0u;
            index < prepared.worldToClip.size();
            ++index)
        {
            if (!SameFloatBits(
                    prepared.worldToClip[index],
                    current.worldToClip[index]))
            {
                return false;
            }
        }
        for (size_t index = 0u;
            index < prepared.clipToWindowScale.size();
            ++index)
        {
            if (!SameFloatBits(
                    prepared.clipToWindowScale[index],
                    current.clipToWindowScale[index]) ||
                !SameFloatBits(
                    prepared.clipToWindowBias[index],
                    current.clipToWindowBias[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool SameCompactRasterViewportState(
        const nvrhi::ViewportState& prepared,
        const nvrhi::ViewportState& current)
    {
        if (prepared.viewports.size() != current.viewports.size() ||
            prepared.scissorRects.size() != current.scissorRects.size())
        {
            return false;
        }
        for (size_t index = 0u;
            index < prepared.viewports.size();
            ++index)
        {
            const nvrhi::Viewport& first = prepared.viewports[index];
            const nvrhi::Viewport& second = current.viewports[index];
            if (!SameFloatBits(first.minX, second.minX) ||
                !SameFloatBits(first.maxX, second.maxX) ||
                !SameFloatBits(first.minY, second.minY) ||
                !SameFloatBits(first.maxY, second.maxY) ||
                !SameFloatBits(first.minZ, second.minZ) ||
                !SameFloatBits(first.maxZ, second.maxZ))
            {
                return false;
            }
        }
        for (size_t index = 0u;
            index < prepared.scissorRects.size();
            ++index)
        {
            if (prepared.scissorRects[index] !=
                current.scissorRects[index])
            {
                return false;
            }
        }
        return true;
    }

    CompactClipBoundsConditioning EvaluateCompactClipBoundsConditioning(
        const CompactClipBoundsConditioningInput& input)
    {
        const double dotRelativeError = RoundUp(
            D3dDot4RelativeError);
        const double viewportRelativeError = RoundUp(
            D3dViewportRelativeError);
        const double dotAbsoluteError = RoundUp(
            D3dDot4AbsoluteError);
        const double viewportAbsoluteError = RoundUp(
            D3dViewportAbsoluteError);
        const double divideRelativeError =
            RoundUp(D3dStrictDivideRelativeError);

        CompactClipBoundsConditioning result;
        std::array<DoubleInterval, 3> objectBounds;
        std::array<std::array<double, 4>, 3> objectToWorld;
        std::array<std::array<double, 4>, 4> worldToClip;
        std::array<double, 2> clipToWindowScale;
        std::array<double, 2> clipToWindowBias;

        for (uint32_t component = 0u; component < 3u; ++component)
        {
            if (!std::isfinite(input.positionBoundsMin[component]) ||
                !std::isfinite(input.positionBoundsMax[component]) ||
                input.positionBoundsMin[component] >
                    input.positionBoundsMax[component])
            {
                return result;
            }
            const float minimum = CanonicalizeD3dFloat(
                input.positionBoundsMin[component]);
            const float maximum = CanonicalizeD3dFloat(
                input.positionBoundsMax[component]);
            objectBounds[component] = {
                static_cast<double>(minimum),
                static_cast<double>(maximum)
            };
        }
        for (uint32_t row = 0u; row < 3u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                if (!std::isfinite(input.objectToWorld[row][column]))
                    return result;
                objectToWorld[row][column] = static_cast<double>(
                    CanonicalizeD3dFloat(
                        input.objectToWorld[row][column]));
            }
        }
        for (uint32_t row = 0u; row < 4u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                if (!std::isfinite(input.worldToClip[row][column]))
                    return result;
                worldToClip[row][column] = static_cast<double>(
                    CanonicalizeD3dFloat(
                        input.worldToClip[row][column]));
            }
        }
        for (uint32_t component = 0u; component < 2u; ++component)
        {
            if (!std::isfinite(input.clipToWindowScale[component]) ||
                !std::isfinite(input.clipToWindowBias[component]))
            {
                return result;
            }
            clipToWindowScale[component] = static_cast<double>(
                CanonicalizeD3dFloat(
                    input.clipToWindowScale[component]));
            clipToWindowBias[component] = static_cast<double>(
                CanonicalizeD3dFloat(
                    input.clipToWindowBias[component]));
            if (clipToWindowScale[component] == 0.0)
                return result;
        }
        if (!std::isfinite(input.viewportMinimumDepth) ||
            !std::isfinite(input.viewportMaximumDepth) ||
            CanonicalizeD3dFloat(input.viewportMinimumDepth) != 0.f ||
            CanonicalizeD3dFloat(input.viewportMaximumDepth) != 1.f)
        {
            return result;
        }

        result.directionTransformFinite =
            IsCompactDirectionTransformFinite(objectToWorld);
        if (!result.directionTransformFinite)
            return result;

        std::array<DoubleInterval, 3> idealWorldBounds = {
            DoubleInterval{
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity() },
            DoubleInterval{
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity() },
            DoubleInterval{
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity() }
        };
        std::array<double, 3> worldForwardError{};
        std::array<DoubleInterval, 3> actualWorldBounds;
        for (uint32_t row = 0u; row < 3u; ++row)
        {
            double magnitude = std::abs(objectToWorld[row][3]);
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                magnitude = AddUp(
                    magnitude,
                    MultiplyUp(
                        std::abs(objectToWorld[row][component]),
                        MaximumAbsolute(objectBounds[component])));
            }
            worldForwardError[row] = AddUp(
                MultiplyUp(dotRelativeError, magnitude),
                dotAbsoluteError);
            if (!std::isfinite(magnitude) ||
                !std::isfinite(worldForwardError[row]) ||
                AddUp(magnitude, worldForwardError[row]) >
                    D3dFloatMaximum)
            {
                return result;
            }
            result.maximumForwardError = std::max(
                result.maximumForwardError,
                FloatUpperBound(worldForwardError[row]));
        }

        std::array<std::array<DoubleInterval, 3>, 8> idealWorldCorners;
        for (uint32_t cornerIndex = 0u; cornerIndex < 8u; ++cornerIndex)
        {
            std::array<DoubleInterval, 3> position;
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                position[component] = PointInterval(
                    (cornerIndex & (1u << component)) != 0u
                        ? objectBounds[component].upper
                        : objectBounds[component].lower);
            }
            for (uint32_t row = 0u; row < 3u; ++row)
            {
                DoubleInterval value =
                    PointInterval(objectToWorld[row][3]);
                for (uint32_t component = 0u;
                    component < 3u;
                    ++component)
                {
                    value = AddIntervals(
                        value,
                        MultiplyIntervals(
                            PointInterval(
                                objectToWorld[row][component]),
                            position[component]));
                }
                if (!IsFiniteInterval(value))
                    return result;
                idealWorldCorners[cornerIndex][row] = value;
                idealWorldBounds[row].lower = std::min(
                    idealWorldBounds[row].lower,
                    value.lower);
                idealWorldBounds[row].upper = std::max(
                    idealWorldBounds[row].upper,
                    value.upper);
            }
        }
        for (uint32_t row = 0u; row < 3u; ++row)
        {
            actualWorldBounds[row] = ExpandInterval(
                idealWorldBounds[row],
                worldForwardError[row]);
            if (!IsFiniteInterval(actualWorldBounds[row]) ||
                MaximumAbsolute(actualWorldBounds[row]) >
                    D3dFloatMaximum)
            {
                return result;
            }
        }

        std::array<DoubleInterval, 4> idealClipBounds = {
            DoubleInterval{
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity() },
            DoubleInterval{
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity() },
            DoubleInterval{
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity() },
            DoubleInterval{
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity() }
        };
        std::array<std::array<DoubleInterval, 4>, 8> idealClipCorners;
        for (uint32_t cornerIndex = 0u; cornerIndex < 8u; ++cornerIndex)
        {
            for (uint32_t component = 0u; component < 4u; ++component)
            {
                DoubleInterval value =
                    PointInterval(worldToClip[3][component]);
                for (uint32_t row = 0u; row < 3u; ++row)
                {
                    value = AddIntervals(
                        value,
                        MultiplyIntervals(
                            idealWorldCorners[cornerIndex][row],
                            PointInterval(
                                worldToClip[row][component])));
                }
                if (!IsFiniteInterval(value))
                    return result;
                idealClipCorners[cornerIndex][component] = value;
                idealClipBounds[component].lower = std::min(
                    idealClipBounds[component].lower,
                    value.lower);
                idealClipBounds[component].upper = std::max(
                    idealClipBounds[component].upper,
                    value.upper);
            }
        }

        std::array<double, 4> clipForwardError{};
        std::array<DoubleInterval, 4> actualClipBounds;
        for (uint32_t component = 0u; component < 4u; ++component)
        {
            double propagatedWorldError = 0.0;
            double clipMagnitude =
                std::abs(worldToClip[3][component]);
            for (uint32_t row = 0u; row < 3u; ++row)
            {
                const double coefficient =
                    std::abs(worldToClip[row][component]);
                propagatedWorldError = AddUp(
                    propagatedWorldError,
                    MultiplyUp(coefficient, worldForwardError[row]));
                clipMagnitude = AddUp(
                    clipMagnitude,
                    MultiplyUp(
                        coefficient,
                        MaximumAbsolute(actualWorldBounds[row])));
            }
            const double clipRoundError = AddUp(
                MultiplyUp(dotRelativeError, clipMagnitude),
                dotAbsoluteError);
            clipForwardError[component] = AddUp(
                propagatedWorldError,
                clipRoundError);
            if (!std::isfinite(clipMagnitude) ||
                !std::isfinite(clipForwardError[component]) ||
                AddUp(clipMagnitude, clipRoundError) >
                    D3dFloatMaximum)
            {
                return result;
            }
            actualClipBounds[component] = ExpandInterval(
                idealClipBounds[component],
                clipForwardError[component]);
            if (!IsFiniteInterval(actualClipBounds[component]) ||
                MaximumAbsolute(actualClipBounds[component]) >
                    D3dFloatMaximum)
            {
                return result;
            }
            result.maximumForwardError = std::max(
                result.maximumForwardError,
                FloatUpperBound(clipForwardError[component]));
        }

        result.minimumClipW =
            FloatLowerBound(actualClipBounds[3].lower);
        result.maximumClipW =
            FloatUpperBound(actualClipBounds[3].upper);
        if (actualClipBounds[3].lower < D3dFloatMinimum ||
            actualClipBounds[3].upper > static_cast<double>(
                AdaptiveMsaaCompactMaximumClipW))
        {
            return result;
        }

        double maximumAbsoluteNdc = 0.0;
        double maximumAbsoluteWindow = 0.0;
        for (uint32_t component = 0u; component < 3u; ++component)
        {
            DoubleInterval idealRatio = {
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity()
            };
            for (uint32_t cornerIndex = 0u;
                cornerIndex < 8u;
                ++cornerIndex)
            {
                const DoubleInterval cornerRatio = DivideIntervals(
                    idealClipCorners[cornerIndex][component],
                    idealClipCorners[cornerIndex][3]);
                if (!IsFiniteInterval(cornerRatio))
                    return result;
                idealRatio.lower = std::min(
                    idealRatio.lower,
                    cornerRatio.lower);
                idealRatio.upper = std::max(
                    idealRatio.upper,
                    cornerRatio.upper);
            }

            const double idealRatioMagnitude =
                MaximumAbsolute(idealRatio);
            const double transformRatioError = RoundUp(
                AddUp(
                    clipForwardError[component],
                    MultiplyUp(
                        idealRatioMagnitude,
                        clipForwardError[3])) /
                actualClipBounds[3].lower);
            const double dividedRatioMagnitude = AddUp(
                idealRatioMagnitude,
                transformRatioError);
            const double divideRoundError = AddUp(
                MultiplyUp(
                    divideRelativeError,
                    dividedRatioMagnitude),
                D3dFloatMinimum);
            const double ratioError = AddUp(
                transformRatioError,
                divideRoundError);
            const DoubleInterval ratio = ExpandInterval(
                idealRatio,
                ratioError);
            if (!IsFiniteInterval(ratio))
                return result;
            maximumAbsoluteNdc = std::max(
                maximumAbsoluteNdc,
                MaximumAbsolute(ratio));
            result.maximumForwardError = std::max(
                result.maximumForwardError,
                FloatUpperBound(ratioError));

            if (component == 2u)
            {
                if (ratio.lower < -static_cast<double>(
                        AdaptiveMsaaCompactMaximumAbsoluteNdcZ) ||
                    ratio.upper > static_cast<double>(
                        AdaptiveMsaaCompactMaximumAbsoluteNdcZ))
                {
                    return result;
                }
                continue;
            }

            const DoubleInterval idealWindow = AddIntervals(
                MultiplyIntervals(
                    ratio,
                    PointInterval(clipToWindowScale[component])),
                PointInterval(clipToWindowBias[component]));
            const double viewportMagnitude = AddUp(
                MultiplyUp(
                    std::abs(clipToWindowScale[component]),
                    MaximumAbsolute(ratio)),
                std::abs(clipToWindowBias[component]));
            const double viewportError = AddUp(
                MultiplyUp(
                    viewportRelativeError,
                    viewportMagnitude),
                viewportAbsoluteError);
            if (!std::isfinite(viewportMagnitude) ||
                !std::isfinite(viewportError) ||
                AddUp(viewportMagnitude, viewportError) >
                    D3dFloatMaximum)
            {
                return result;
            }
            const DoubleInterval window = ExpandInterval(
                idealWindow,
                viewportError);
            if (!IsFiniteInterval(window) ||
                window.lower < static_cast<double>(
                    AdaptiveMsaaCompactMinimumWindowCoordinate) ||
                window.upper > static_cast<double>(
                    AdaptiveMsaaCompactMaximumWindowCoordinate))
            {
                return result;
            }
            maximumAbsoluteWindow = std::max(
                maximumAbsoluteWindow,
                MaximumAbsolute(window));
            result.maximumForwardError = std::max(
                result.maximumForwardError,
                FloatUpperBound(viewportError));
        }

        result.maximumAbsoluteNdc =
            FloatUpperBound(maximumAbsoluteNdc);
        result.maximumAbsoluteWindow =
            FloatUpperBound(maximumAbsoluteWindow);
        result.safe = true;
        return result;
    }

    bool SameCompactRasterTransformIdentity(
        const CompactClipBoundsConditioningInput& prepared,
        const CompactClipBoundsConditioningInput& current)
    {
        for (uint32_t row = 0u; row < 3u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                if (!SameFloatBits(
                        prepared.objectToWorld[row][column],
                        current.objectToWorld[row][column]))
                {
                    return false;
                }
            }
        }
        for (uint32_t row = 0u; row < 4u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                if (!SameFloatBits(
                        prepared.worldToClip[row][column],
                        current.worldToClip[row][column]))
                {
                    return false;
                }
            }
        }
        for (uint32_t component = 0u; component < 2u; ++component)
        {
            if (!SameFloatBits(
                    prepared.clipToWindowScale[component],
                    current.clipToWindowScale[component]) ||
                !SameFloatBits(
                    prepared.clipToWindowBias[component],
                    current.clipToWindowBias[component]))
            {
                return false;
            }
        }
        return SameFloatBits(
                prepared.viewportMinimumDepth,
                current.viewportMinimumDepth) &&
            SameFloatBits(
                prepared.viewportMaximumDepth,
                current.viewportMaximumDepth);
    }

    CompactTriangleConditioning EvaluateCompactTriangleConditioning(
        const CompactTriangleConditioningInput& input)
    {
        CompactTriangleConditioning result;
        std::array<std::array<double, 4>, 3> objectToWorld{};
        std::array<std::array<double, 4>, 4> worldToClip{};
        std::array<double, 2> clipToWindowScale{};
        std::array<double, 2> clipToWindowBias{};
        double viewportMinimumDepth = 0.0;
        double viewportMaximumDepth = 1.0;
        std::array<std::array<double, 3>, 3> objectPositions{};
        std::array<std::array<double, 2>, 3> objectTexCoords{};

        for (uint32_t vertex = 0u; vertex < 3u; ++vertex)
        {
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                if (!std::isfinite(
                        input.objectPositions[vertex][component]))
                {
                    return result;
                }
                objectPositions[vertex][component] =
                    CanonicalizeD3dFloat(
                        input.objectPositions[vertex][component]);
            }
            for (uint32_t component = 0u; component < 2u; ++component)
            {
                if (!std::isfinite(
                        input.objectTexCoords[vertex][component]))
                {
                    return result;
                }
                objectTexCoords[vertex][component] =
                    CanonicalizeD3dFloat(
                        input.objectTexCoords[vertex][component]);
            }
        }
        for (uint32_t row = 0u; row < 3u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                if (!std::isfinite(input.objectToWorld[row][column]))
                    return result;
                objectToWorld[row][column] = CanonicalizeD3dFloat(
                    input.objectToWorld[row][column]);
            }
        }
        for (uint32_t row = 0u; row < 4u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                if (!std::isfinite(input.worldToClip[row][column]))
                    return result;
                worldToClip[row][column] = CanonicalizeD3dFloat(
                    input.worldToClip[row][column]);
            }
        }
        for (uint32_t component = 0u; component < 2u; ++component)
        {
            if (!std::isfinite(input.clipToWindowScale[component]) ||
                !std::isfinite(input.clipToWindowBias[component]) ||
                input.clipToWindowScale[component] == 0.f)
            {
                return result;
            }
            clipToWindowScale[component] = CanonicalizeD3dFloat(
                input.clipToWindowScale[component]);
            clipToWindowBias[component] = CanonicalizeD3dFloat(
                input.clipToWindowBias[component]);
        }
        if (!std::isfinite(input.viewportMinimumDepth) ||
            !std::isfinite(input.viewportMaximumDepth))
        {
            return result;
        }
        viewportMinimumDepth = CanonicalizeD3dFloat(
            input.viewportMinimumDepth);
        viewportMaximumDepth = CanonicalizeD3dFloat(
            input.viewportMaximumDepth);
        if (!IsCompactDirectionTransformFinite(objectToWorld))
            return result;

        std::array<ProjectedVertexProof, 3> vertices;
        for (uint32_t vertex = 0u; vertex < 3u; ++vertex)
        {
            vertices[vertex] = EvaluateProjectedVertexProof(
                objectPositions[vertex],
                objectToWorld,
                worldToClip,
                clipToWindowScale,
                clipToWindowBias,
                viewportMinimumDepth,
                viewportMaximumDepth);
            if (!vertices[vertex].safe)
                return result;
        }

        double minimumVertexDepth =
            std::numeric_limits<double>::infinity();
        double maximumVertexDepth = 0.0;
        for (const ProjectedVertexProof& vertex : vertices)
        {
            minimumVertexDepth = std::min(
                minimumVertexDepth,
                vertex.deviceDepth.lower);
            maximumVertexDepth = std::max(
                maximumVertexDepth,
                vertex.deviceDepth.upper);
        }
        // Direct3D 11.3 Functional Specification sections 15.16.2, 16.3.1,
        // and 17.11 establish that interpolation is set up after 16.8 snap,
        // follows float32 rules, fixed-function Z exactly matches input Z, and
        // float32 depth storage performs no format conversion. An ideal
        // no-perspective Z value is therefore inside the vertex-depth gamut.
        // Add a full ULP beyond that required gamut bound (more conservative
        // than ordinary add/mul's 0.5 ULP), then use outward nextafter below
        // to cover the D32 materialization explicitly.
        // https://microsoft.github.io/DirectX-Specs/d3d/archive/
        // D3D11_3_FunctionalSpec.htm
        const double interpolationMagnitude = std::max(
            std::abs(minimumVertexDepth),
            std::abs(maximumVertexDepth));
        const double interpolationError = AddUp(
            MultiplyUp(
                D3dOneUlpRelativeError,
                interpolationMagnitude),
            D3dFloatMinimum);
        const DoubleInterval interpolatedDepth = ExpandInterval(
            { minimumVertexDepth, maximumVertexDepth },
            interpolationError);
        if (!IsFiniteInterval(interpolatedDepth) ||
            interpolatedDepth.lower <= 0.0)
        {
            return result;
        }
        float minimumStoredDepth = FloatLowerBound(
            interpolatedDepth.lower);
        float maximumStoredDepth = FloatUpperBound(
            interpolatedDepth.upper);
        minimumStoredDepth = std::nextafter(minimumStoredDepth, 0.f);
        maximumStoredDepth = maximumStoredDepth < 1.f
            ? std::nextafter(maximumStoredDepth, 1.f)
            : 1.f;
        if (!std::isfinite(minimumStoredDepth) ||
            !std::isfinite(maximumStoredDepth) ||
            !(minimumStoredDepth > 0.f) ||
            maximumStoredDepth > 1.f ||
            minimumStoredDepth > maximumStoredDepth)
        {
            return result;
        }
        const double storedDepthError = std::max(
            minimumVertexDepth - minimumStoredDepth,
            maximumStoredDepth - maximumVertexDepth);

        const DoubleInterval edge01X = SubtractIntervals(
            vertices[1].window[0], vertices[0].window[0]);
        const DoubleInterval edge01Y = SubtractIntervals(
            vertices[1].window[1], vertices[0].window[1]);
        const DoubleInterval edge02X = SubtractIntervals(
            vertices[2].window[0], vertices[0].window[0]);
        const DoubleInterval edge02Y = SubtractIntervals(
            vertices[2].window[1], vertices[0].window[1]);
        DoubleInterval area = SubtractIntervals(
            MultiplyIntervals(edge01X, edge02Y),
            MultiplyIntervals(edge01Y, edge02X));
        const double areaMagnitude = AddUp(
            MultiplyUp(
                MaximumAbsolute(edge01X),
                MaximumAbsolute(edge02Y)),
            MultiplyUp(
                MaximumAbsolute(edge01Y),
                MaximumAbsolute(edge02X)));
        const double areaArithmeticError = AddUp(
            MultiplyUp(
                RoundUp(D3dTriangleAreaRelativeError),
                areaMagnitude),
            RoundUp(D3dTriangleAreaAbsoluteError));
        area = ExpandInterval(area, areaArithmeticError);
        if (!IsFiniteInterval(area))
            return result;

        int areaSign = 0;
        double minimumAbsoluteArea = 0.0;
        if (area.lower >
            AdaptiveMsaaCompactMinimumProjectedTriangleArea)
        {
            areaSign = 1;
            minimumAbsoluteArea = area.lower;
        }
        else if (area.upper <
            -AdaptiveMsaaCompactMinimumProjectedTriangleArea)
        {
            areaSign = -1;
            minimumAbsoluteArea = -area.upper;
        }
        else
        {
            return result;
        }

        const int64_t edge01SnappedX =
            int64_t(vertices[1].snapped[0]) -
            int64_t(vertices[0].snapped[0]);
        const int64_t edge01SnappedY =
            int64_t(vertices[1].snapped[1]) -
            int64_t(vertices[0].snapped[1]);
        const int64_t edge02SnappedX =
            int64_t(vertices[2].snapped[0]) -
            int64_t(vertices[0].snapped[0]);
        const int64_t edge02SnappedY =
            int64_t(vertices[2].snapped[1]) -
            int64_t(vertices[0].snapped[1]);
        const int64_t snappedArea =
            edge01SnappedX * edge02SnappedY -
            edge01SnappedY * edge02SnappedX;
        const uint64_t absoluteSnappedArea = snappedArea < 0
            ? static_cast<uint64_t>(-snappedArea)
            : static_cast<uint64_t>(snappedArea);
        if (absoluteSnappedArea <
                AdaptiveMsaaCompactMinimumSnappedTriangleArea ||
            (snappedArea > 0 ? 1 : -1) != areaSign)
        {
            return result;
        }
        const double absoluteSnappedAreaPixels =
            double(absoluteSnappedArea) /
            double(CompactRasterSubpixelScale *
                CompactRasterSubpixelScale);
        minimumAbsoluteArea = std::min(
            minimumAbsoluteArea,
            absoluteSnappedAreaPixels);
        if (!std::isfinite(absoluteSnappedAreaPixels) ||
            minimumAbsoluteArea <
                AdaptiveMsaaCompactMinimumProjectedTriangleArea)
        {
            return result;
        }

        // A covered sample is inside the proven unsnapped triangle up to the
        // stable half-subpixel perturbation. Pixel-center gradient evaluation
        // is at most sqrt(2) pixels away. Bound the affine reciprocal-W plane
        // over that neighborhood so the shader's perspective denominator
        // cannot reach its zero/FTZ rejection after raster ownership exists.
        double minimumW = std::numeric_limits<double>::infinity();
        double maximumW = 0.0;
        double maximumEdgeLength = 0.0;
        for (uint32_t vertex = 0u; vertex < 3u; ++vertex)
        {
            minimumW = std::min(minimumW,
                vertices[vertex].clip[3].lower);
            maximumW = std::max(maximumW,
                vertices[vertex].clip[3].upper);
            const uint32_t next = (vertex + 1u) % 3u;
            const DoubleInterval edgeX = SubtractIntervals(
                vertices[next].window[0],
                vertices[vertex].window[0]);
            const DoubleInterval edgeY = SubtractIntervals(
                vertices[next].window[1],
                vertices[vertex].window[1]);
            maximumEdgeLength = std::max(
                maximumEdgeLength,
                std::hypot(
                    MaximumAbsolute(edgeX),
                    MaximumAbsolute(edgeY)));
            const double snappedEdgeX =
                double(vertices[next].snapped[0]) /
                    double(CompactRasterSubpixelScale) -
                double(vertices[vertex].snapped[0]) /
                    double(CompactRasterSubpixelScale);
            const double snappedEdgeY =
                double(vertices[next].snapped[1]) /
                    double(CompactRasterSubpixelScale) -
                double(vertices[vertex].snapped[1]) /
                    double(CompactRasterSubpixelScale);
            maximumEdgeLength = std::max(
                maximumEdgeLength,
                std::hypot(snappedEdgeX, snappedEdgeY));
        }
        if (!(minimumW > 0.0) || !(maximumW >= minimumW) ||
            !(maximumEdgeLength > 0.0))
        {
            return result;
        }
        const double minimumReciprocalW = 1.0 / maximumW;
        const double reciprocalWSpan =
            1.0 / minimumW - minimumReciprocalW;
        const double minimumAltitude =
            minimumAbsoluteArea / maximumEdgeLength;
        const double reciprocalWGradientBound =
            reciprocalWSpan / minimumAltitude;
        if (!std::isfinite(reciprocalWGradientBound) ||
            minimumReciprocalW <=
                reciprocalWGradientBound * std::sqrt(2.0) +
                    1.0e-20)
        {
            return result;
        }

        // Bound every float operation used by CompactInterpolateTriangle and
        // CompactGetAnalyticCoarseGradients. A covered sample is inside the snapped
        // primitive and its pixel center is within sqrt(2) pixels, so the
        // maximum edge length plus that neighborhood bounds every evaluation
        // delta. Each expression is inflated as though every operation had
        // Direct3D's one-ULP fused tolerance; this is stricter than the normal
        // 0.5-ULP add/multiply rule and covers legal contraction choices.
        const auto InflateFloatMagnitude = [](double magnitude,
            double operationCount)
        {
            if (!std::isfinite(magnitude) || magnitude < 0.0)
                return std::numeric_limits<double>::infinity();
            const double relativeError = Gamma(
                operationCount,
                D3dOneUlpRelativeError);
            return AddUp(
                MultiplyUp(1.0 + relativeError, magnitude),
                MultiplyUp(operationCount, D3dFloatMinimum));
        };
        const auto IsSafeFloatMagnitude = [](double magnitude)
        {
            return std::isfinite(magnitude) && magnitude >= 0.0 &&
                magnitude <= D3dFloatMaximum / 16.0;
        };

        const double evaluationDistance = AddUp(
            maximumEdgeLength,
            std::sqrt(2.0));
        const double affineNumeratorMagnitude = InflateFloatMagnitude(
            AddUp(
                MultiplyUp(maximumEdgeLength, evaluationDistance),
                MultiplyUp(maximumEdgeLength, evaluationDistance)),
            5.0);
        const double inverseAreaMagnitude = InflateFloatMagnitude(
            1.0 / minimumAbsoluteArea,
            2.0);
        const double affineXYMagnitude = InflateFloatMagnitude(
            MultiplyUp(
                affineNumeratorMagnitude,
                inverseAreaMagnitude),
            2.0);
        const double affineZMagnitude = InflateFloatMagnitude(
            AddUp(1.0, MultiplyUp(2.0, affineXYMagnitude)),
            3.0);
        const double maximumAffineMagnitude = std::max(
            affineXYMagnitude,
            affineZMagnitude);
        if (!IsSafeFloatMagnitude(affineNumeratorMagnitude) ||
            !IsSafeFloatMagnitude(inverseAreaMagnitude) ||
            !IsSafeFloatMagnitude(maximumAffineMagnitude) ||
            maximumAffineMagnitude >
                AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric)
        {
            return result;
        }

        const double maximumIdealReciprocalW = 1.0 / minimumW;
        const double maximumReciprocalW = InflateFloatMagnitude(
            maximumIdealReciprocalW,
            2.0);
        const double reciprocalWOperationError = RoundUp(
            maximumReciprocalW - maximumIdealReciprocalW);
        const double geometricMinimumPerspectiveDenominator = RoundDown(
            minimumReciprocalW -
                reciprocalWGradientBound * std::sqrt(2.0));
        const double perspectiveDotIdealMagnitude = MultiplyUp(
            3.0,
            MultiplyUp(maximumAffineMagnitude, maximumReciprocalW));
        const double maximumPerspectiveDenominator = InflateFloatMagnitude(
            perspectiveDotIdealMagnitude,
            5.0);
        const double perspectiveDotArithmeticError = RoundUp(
            maximumPerspectiveDenominator -
                perspectiveDotIdealMagnitude);
        const double reciprocalWInputError = MultiplyUp(
            3.0,
            MultiplyUp(
                maximumAffineMagnitude,
                reciprocalWOperationError));
        const double minimumPerspectiveDenominator = RoundDown(
            geometricMinimumPerspectiveDenominator -
                reciprocalWInputError -
                perspectiveDotArithmeticError);
        if (!IsSafeFloatMagnitude(maximumReciprocalW) ||
            !IsSafeFloatMagnitude(maximumPerspectiveDenominator) ||
            !std::isfinite(minimumPerspectiveDenominator) ||
            minimumPerspectiveDenominator <
                AdaptiveMsaaCompactMinimumPerspectiveDenominator)
        {
            return result;
        }

        const double weightedReciprocalWMagnitude = InflateFloatMagnitude(
            MultiplyUp(maximumAffineMagnitude, maximumReciprocalW),
            2.0);
        const double maximumPerspectiveBarycentric =
            InflateFloatMagnitude(
                weightedReciprocalWMagnitude /
                    minimumPerspectiveDenominator,
                2.0);
        if (!IsSafeFloatMagnitude(weightedReciprocalWMagnitude) ||
            !IsSafeFloatMagnitude(maximumPerspectiveBarycentric) ||
            maximumPerspectiveBarycentric >
                AdaptiveMsaaCompactMaximumAbsolutePerspectiveBarycentric)
        {
            return result;
        }

        double maximumAbsoluteTexCoordSource = 0.0;
        for (const auto& texCoord : objectTexCoords)
        {
            maximumAbsoluteTexCoordSource = std::max({
                maximumAbsoluteTexCoordSource,
                std::abs(texCoord[0]),
                std::abs(texCoord[1]) });
        }
        const double texCoordTermMagnitude = InflateFloatMagnitude(
            MultiplyUp(
                maximumAbsoluteTexCoordSource,
                maximumPerspectiveBarycentric),
            2.0);
        const double maximumTexCoordMagnitude = InflateFloatMagnitude(
            MultiplyUp(3.0, texCoordTermMagnitude),
            5.0);

        // Coarse gradients are direct differences of two bounded helper-lane
        // UV values. Unlike the retired analytic derivative, they never form
        // a squared perspective denominator. Two full UV magnitudes plus one
        // subtraction roundoff conservatively bound either component.
        const double maximumTexCoordGradientMagnitude =
            InflateFloatMagnitude(
                MultiplyUp(2.0, maximumTexCoordMagnitude),
                2.0);
        if (!IsSafeFloatMagnitude(maximumTexCoordMagnitude) ||
            !IsSafeFloatMagnitude(maximumTexCoordGradientMagnitude) ||
            maximumTexCoordMagnitude >
                AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient ||
            maximumTexCoordGradientMagnitude >
                AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient)
        {
            return result;
        }

        const float maximumAffineBound = FloatUpperBound(
            maximumAffineMagnitude);
        const float minimumPerspectiveBound = FloatLowerBound(
            minimumPerspectiveDenominator);
        const float maximumPerspectiveBarycentricBound = FloatUpperBound(
            maximumPerspectiveBarycentric);
        const float maximumTexCoordBound = FloatUpperBound(
            maximumTexCoordMagnitude);
        const float maximumTexCoordGradientBound = FloatUpperBound(
            maximumTexCoordGradientMagnitude);
        if (maximumAffineBound >
                AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric ||
            minimumPerspectiveBound <
                AdaptiveMsaaCompactMinimumPerspectiveDenominator ||
            maximumPerspectiveBarycentricBound >
                AdaptiveMsaaCompactMaximumAbsolutePerspectiveBarycentric ||
            maximumTexCoordBound >
                AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient ||
            maximumTexCoordGradientBound >
                AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient)
        {
            return result;
        }

        result.safe = true;
        result.triangleCount = 1u;
        result.minimumAbsoluteProjectedArea =
            FloatLowerBound(minimumAbsoluteArea);
        result.minimumAbsoluteSnappedArea = absoluteSnappedArea;
        result.minimumSnapTieDistance = FloatLowerBound(std::min({
            vertices[0].minimumSnapTieDistance,
            vertices[1].minimumSnapTieDistance,
            vertices[2].minimumSnapTieDistance }));
        result.minimumStoredDeviceDepth = minimumStoredDepth;
        result.maximumStoredDeviceDepth = maximumStoredDepth;
        result.storedDeviceDepthErrorMargin = FloatUpperBound(
            storedDepthError);
        result.maximumAbsoluteAffineBarycentric = maximumAffineBound;
        result.minimumPerspectiveDenominator = minimumPerspectiveBound;
        result.maximumAbsolutePerspectiveBarycentric =
            maximumPerspectiveBarycentricBound;
        result.maximumAbsoluteTexCoord = maximumTexCoordBound;
        result.maximumAbsoluteTexCoordGradient =
            maximumTexCoordGradientBound;
        return result;
    }

    CompactRasterDrawValidationResult ValidateCompactRasterDraw(
        const CompactRasterDrawPreflightDesc& description)
    {
        if (!description.geometryPresent)
            return FailDraw(FallbackReason::ReconstructionFailure);
        if (!description.instanceMeshMatches ||
            !description.bufferGroupMatchesMesh)
        {
            return FailDraw(FallbackReason::TransientMappingFailure);
        }
        if (!description.meshUsesTriangleList ||
            !description.geometryUsesTriangleList)
        {
            return FailDraw(FallbackReason::ReconstructionFailure);
        }
        if (!description.deformationSupported)
        {
            return FailDraw(FallbackReason::UnsupportedDeformation);
        }
        if (!description.materialSupported ||
            !description.alphaClassificationMatches ||
            !description.cullModeMatches)
        {
            return FailDraw(FallbackReason::UnsupportedMaterial);
        }
        if (!description.oneByOneShadingRate)
            return FailDraw(FallbackReason::ReconstructionFailure);
        if (!description.buffersPresent ||
            !description.instanceBufferValid ||
            description.instanceBufferStride != sizeof(InstanceData) ||
            description.instanceBufferByteSize <
                description.instanceBufferStride ||
            description.instanceBufferByteSize %
                description.instanceBufferStride != 0u ||
            description.instanceCount !=
                description.instanceBufferByteSize /
                    description.instanceBufferStride)
        {
            return FailDraw(FallbackReason::ReconstructionFailure);
        }
        if (!description.indexBufferValid ||
            !description.vertexBufferValid)
        {
            return FailDraw(FallbackReason::FormatUnsupported);
        }
        if (description.instanceIndex < 0 ||
            description.firstGeometryInstanceIndex < 0 ||
            uint64_t(description.instanceIndex) >= description.instanceCount ||
            description.geometryLocalIndex >=
                description.instanceGeometryCount)
        {
            return FailDraw(FallbackReason::TransientMappingFailure);
        }
        if (static_cast<uint64_t>(description.instanceIndex) >
                std::numeric_limits<uint32_t>::max() ||
            static_cast<uint64_t>(
                description.firstGeometryInstanceIndex) >
                std::numeric_limits<uint32_t>::max() ||
            description.geometryLocalIndex >= InvalidVisibilityWord)
        {
            return FailDraw(FallbackReason::CapacityOverflow);
        }

        uint64_t flattenedHandleIndex = 0u;
        if (!TryAdd(
                static_cast<uint64_t>(
                    description.firstGeometryInstanceIndex),
                description.geometryLocalIndex,
                flattenedHandleIndex) ||
            flattenedHandleIndex >= description.handleMapCount)
        {
            return FailDraw(FallbackReason::TransientMappingFailure);
        }
        if (flattenedHandleIndex >= InvalidVisibilityWord)
            return FailDraw(FallbackReason::CapacityOverflow);

        const DecodedGeometryInstanceHandle decoded =
            DecodeGeometryInstanceHandle(
                description.mappedGeometryInstanceHandle);
        if (!decoded.Succeeded())
            return FailDraw(decoded.reason);
        if (decoded.slot >= description.geometryInstanceRecordCount)
            return FailDraw(FallbackReason::SlotOutOfRange);
        if (!description.geometryInstanceRecordValid)
            return FailDraw(FallbackReason::InvalidHandleEncoding);
        if (description.recordGeneration != decoded.generation)
            return FailDraw(FallbackReason::GenerationMismatch);
        if (description.recordStableInstanceHandle == 0u)
            return FailDraw(FallbackReason::TransientMappingFailure);
        if (!description.geometryInstanceDeformationSupported)
            return FailDraw(FallbackReason::UnsupportedDeformation);
        if (!description.geometryInstanceMaterialSupported)
            return FailDraw(FallbackReason::UnsupportedMaterial);
        if (description.expectedTopologyEpoch == 0u ||
            description.recordTopologyEpoch !=
                description.expectedTopologyEpoch ||
            description.recordTopologyRevision !=
                description.expectedTopologyRevision)
        {
            return FailDraw(FallbackReason::TopologyEpochMismatch);
        }
        if (description.recordLodRevision !=
            description.expectedLodRevision)
        {
            return FailDraw(FallbackReason::LodRevisionMismatch);
        }
        if (description.recordDeformationRevision !=
            description.expectedDeformationRevision)
        {
            return FailDraw(FallbackReason::DeformationRevisionMismatch);
        }
        if (description.recordMaterialRevision !=
            description.expectedMaterialRevision)
        {
            return FailDraw(FallbackReason::MaterialRevisionMismatch);
        }
        if (description.recordInstanceIndex !=
                static_cast<uint32_t>(description.instanceIndex) ||
            description.recordGeometryIndex !=
                description.geometryLocalIndex)
        {
            return FailDraw(FallbackReason::TransientMappingFailure);
        }

        if (description.indexCount == 0u ||
            description.vertexCount == 0u ||
            description.indexCount % 3u != 0u)
        {
            return FailDraw(FallbackReason::PrimitiveOverflow);
        }

        uint64_t primitiveCount = description.indexCount / 3u;
        if (primitiveCount >= InvalidVisibilityWord ||
            primitiveCount != description.recordPrimitiveCount)
        {
            return FailDraw(FallbackReason::PrimitiveOverflow);
        }
        if (description.recordGeometryRecordIndex == InvalidVisibilityWord ||
            description.recordMaterialIndex == InvalidVisibilityWord ||
            description.recordGeometryRecordIndex >=
                description.geometryRecordCount ||
            description.recordMaterialIndex >=
                description.materialRecordCount)
        {
            return FailDraw(FallbackReason::ReconstructionFailure);
        }
        if (description.recordIndexBufferDescriptor ==
                InvalidVisibilityWord ||
            description.recordVertexBufferDescriptor ==
                InvalidVisibilityWord)
        {
            return FailDraw(FallbackReason::ReconstructionFailure);
        }
        if (description.textureDescriptorCount >
                description.descriptorCount ||
            description.rawBufferDescriptorCount !=
                description.descriptorCount -
                    description.textureDescriptorCount ||
            description.recordIndexBufferDescriptor <
                description.textureDescriptorCount ||
            description.recordIndexBufferDescriptor >=
                description.descriptorCount ||
            description.recordVertexBufferDescriptor <
                description.textureDescriptorCount ||
            description.recordVertexBufferDescriptor >=
                description.descriptorCount)
        {
            return FailDraw(FallbackReason::TransientMappingFailure);
        }
        if (description.rawBufferDescriptorCount < 2u)
            return FailDraw(FallbackReason::ReconstructionFailure);
        if (description.recordIndexCount != description.indexCount ||
            description.recordVertexCount != description.vertexCount ||
            description.recordGeometryFlags !=
                description.expectedGeometryFlags)
        {
            return FailDraw(FallbackReason::ReconstructionFailure);
        }

        uint64_t firstIndex = 0u;
        uint64_t firstVertex = 0u;
        uint64_t endIndex = 0u;
        uint64_t indexOffsetBytes = 0u;
        uint64_t endIndexBytes = 0u;
        if (!TryAdd(
                description.meshFirstIndex,
                description.geometryFirstIndex,
                firstIndex) ||
            !TryAdd(
                description.meshFirstVertex,
                description.geometryFirstVertex,
                firstVertex) ||
            !TryAdd(firstIndex, description.indexCount, endIndex) ||
            !TryMultiply(firstIndex, IndexStride, indexOffsetBytes) ||
            !TryMultiply(endIndex, IndexStride, endIndexBytes) ||
            endIndexBytes > description.indexBufferByteSize ||
            endIndexBytes > MaximumGpuByteEnd ||
            indexOffsetBytes >= MaximumGpuByteEnd ||
            description.recordIndexOffset !=
                static_cast<uint32_t>(indexOffsetBytes))
        {
            return FailDraw(FallbackReason::CapacityOverflow);
        }

        if (!IsByteRangeValid(
                description.positionRange,
                firstVertex,
                description.vertexCount,
                PositionStride,
                description.vertexBufferByteSize,
                description.recordPositionOffset) ||
            !IsByteRangeValid(
                description.previousPositionRange,
                firstVertex,
                description.vertexCount,
                PositionStride,
                description.vertexBufferByteSize,
                description.recordPreviousPositionOffset) ||
            !IsByteRangeValid(
                description.texCoordRange,
                firstVertex,
                description.vertexCount,
                TexCoordStride,
                description.vertexBufferByteSize,
                description.recordTexCoordOffset) ||
            !IsByteRangeValid(
                description.normalRange,
                firstVertex,
                description.vertexCount,
                PackedDirectionStride,
                description.vertexBufferByteSize,
                description.recordNormalOffset) ||
            !IsByteRangeValid(
                description.tangentRange,
                firstVertex,
                description.vertexCount,
                PackedDirectionStride,
                description.vertexBufferByteSize,
                description.recordTangentOffset))
        {
            return FailDraw(FallbackReason::ReconstructionFailure);
        }

        if (!description.immutableIndexProofPresent ||
            description.proofFirstIndex != firstIndex ||
            description.proofIndexCount != description.indexCount ||
            description.proofFirstVertex != firstVertex ||
            description.proofVertexCount != description.vertexCount ||
            description.proofContentRevision == 0u ||
            description.proofMaximumIndex >= description.vertexCount)
        {
            return FailDraw(FallbackReason::ReconstructionFailure);
        }
        if (!description.viewConditioningProofPresent ||
            !description.viewConditioningSafe ||
            !description.directionTransformFinite ||
            !description.triangleConditioningProofPresent ||
            !description.triangleConditioningSafe ||
            !std::isfinite(
                description.minimumAbsoluteProjectedArea) ||
            description.minimumAbsoluteProjectedArea <
                AdaptiveMsaaCompactMinimumProjectedTriangleArea ||
            description.minimumAbsoluteSnappedArea <
                AdaptiveMsaaCompactMinimumSnappedTriangleArea ||
            !(description.minimumSnapTieDistance > 0.f) ||
            !std::isfinite(description.minimumStoredDeviceDepth) ||
            !std::isfinite(description.maximumStoredDeviceDepth) ||
            !std::isfinite(
                description.storedDeviceDepthErrorMargin) ||
            !(description.minimumStoredDeviceDepth > 0.f) ||
            description.minimumStoredDeviceDepth >
                description.maximumStoredDeviceDepth ||
            description.maximumStoredDeviceDepth > 1.f ||
            description.storedDeviceDepthErrorMargin < 0.f ||
            !std::isfinite(
                description.maximumAbsoluteAffineBarycentric) ||
            description.maximumAbsoluteAffineBarycentric < 0.f ||
            description.maximumAbsoluteAffineBarycentric >
                AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric ||
            !std::isfinite(
                description.minimumPerspectiveDenominator) ||
            description.minimumPerspectiveDenominator <
                AdaptiveMsaaCompactMinimumPerspectiveDenominator ||
            !std::isfinite(
                description.maximumAbsolutePerspectiveBarycentric) ||
            description.maximumAbsolutePerspectiveBarycentric < 0.f ||
            description.maximumAbsolutePerspectiveBarycentric >
                AdaptiveMsaaCompactMaximumAbsolutePerspectiveBarycentric ||
            !std::isfinite(description.maximumAbsoluteTexCoord) ||
            description.maximumAbsoluteTexCoord < 0.f ||
            description.maximumAbsoluteTexCoord >
                AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient ||
            !std::isfinite(
                description.maximumAbsoluteTexCoordGradient) ||
            description.maximumAbsoluteTexCoordGradient < 0.f ||
            description.maximumAbsoluteTexCoordGradient >
                AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient)
        {
            return FailDraw(FallbackReason::ReconstructionFailure);
        }

        CompactRasterDrawValidationResult result;
        result.reason = FallbackReason::None;
        if (!TryNarrow(
                static_cast<uint64_t>(description.instanceIndex),
                result.draw.instanceIndex) ||
            !TryNarrow(
                description.geometryLocalIndex,
                result.draw.geometryLocalIndex) ||
            !TryNarrow(firstIndex, result.draw.startIndexLocation) ||
            !TryNarrow(firstVertex, result.draw.startVertexLocation) ||
            !TryNarrow(description.indexCount, result.draw.indexCount) ||
            !TryNarrow(primitiveCount, result.draw.primitiveCount))
        {
            return FailDraw(FallbackReason::CapacityOverflow);
        }
        result.draw.geometryInstanceHandle =
            description.mappedGeometryInstanceHandle;
        result.draw.geometryRecordIndex =
            description.recordGeometryRecordIndex;
        return result;
    }

    bool IsCompactVisibilityClassFormat(nvrhi::Format format)
    {
        return format == nvrhi::Format::R8_UINT ||
            format == nvrhi::Format::R32_UINT;
    }

    FallbackReason ValidateCompactRasterFallbackVisibilityClass(
        CompactGeometryPayloadClass payloadClass,
        FallbackReason payloadFallbackReason,
        uint32_t visibilityClass)
    {
        const AdaptiveMsaaVisibilityClassDecodeResult decoded =
            AdaptiveMsaaDecodeVisibilityClass(visibilityClass);
        if (!decoded.valid || !decoded.fallback)
            return FallbackReason::ReconstructionFailure;

        const FallbackReason markerReason =
            static_cast<FallbackReason>(decoded.fallbackReason);
        const bool payloadMatches = decoded.localFallback
            ? payloadClass == CompactGeometryPayloadClass::Reconstruct &&
                payloadFallbackReason == FallbackReason::None
            : payloadClass ==
                    CompactGeometryPayloadClass::FullGBufferFallback &&
                payloadFallbackReason == markerReason;
        return payloadMatches
            ? FallbackReason::None
            : FallbackReason::TransientMappingFailure;
    }

    namespace
    {
        [[nodiscard]] CompactRasterDrawValidationResult
            ValidateFullGBufferFallbackDraw(
                const CompactRasterDrawPreflightDesc& description)
        {
            if (!description.geometryPresent)
                return FailDraw(FallbackReason::ReconstructionFailure);
            if (!description.instanceMeshMatches ||
                !description.bufferGroupMatchesMesh)
            {
                return FailDraw(FallbackReason::TransientMappingFailure);
            }
            if (!description.meshUsesTriangleList ||
                !description.geometryUsesTriangleList)
            {
                return FailDraw(FallbackReason::ReconstructionFailure);
            }
            if (!description.fullGBufferRasterSupported ||
                !description.alphaClassificationMatches ||
                !description.cullModeMatches)
            {
                return FailDraw(FallbackReason::UnsupportedMaterial);
            }
            if (!description.buffersPresent ||
                !description.instanceBufferValid ||
                description.instanceBufferStride != sizeof(InstanceData) ||
                description.instanceBufferByteSize <
                    description.instanceBufferStride ||
                description.instanceBufferByteSize %
                    description.instanceBufferStride != 0u ||
                description.instanceCount !=
                    description.instanceBufferByteSize /
                        description.instanceBufferStride)
            {
                return FailDraw(FallbackReason::ReconstructionFailure);
            }
            if (!description.indexBufferValid ||
                !description.vertexBufferValid)
            {
                return FailDraw(FallbackReason::FormatUnsupported);
            }
            if (description.instanceIndex < 0 ||
                description.firstGeometryInstanceIndex < 0 ||
                uint64_t(description.instanceIndex) >=
                    description.instanceCount ||
                description.geometryLocalIndex >=
                    description.instanceGeometryCount)
            {
                return FailDraw(FallbackReason::TransientMappingFailure);
            }
            if (static_cast<uint64_t>(description.instanceIndex) >
                    std::numeric_limits<uint32_t>::max() ||
                static_cast<uint64_t>(
                    description.firstGeometryInstanceIndex) >
                    std::numeric_limits<uint32_t>::max() ||
                description.geometryLocalIndex >= InvalidVisibilityWord)
            {
                return FailDraw(FallbackReason::CapacityOverflow);
            }

            uint64_t flattenedHandleIndex = 0u;
            if (!TryAdd(
                    static_cast<uint64_t>(
                        description.firstGeometryInstanceIndex),
                    description.geometryLocalIndex,
                    flattenedHandleIndex) ||
                flattenedHandleIndex >= description.handleMapCount)
            {
                return FailDraw(FallbackReason::TransientMappingFailure);
            }
            if (flattenedHandleIndex >= InvalidVisibilityWord)
                return FailDraw(FallbackReason::CapacityOverflow);

            const DecodedGeometryInstanceHandle decoded =
                DecodeGeometryInstanceHandle(
                    description.mappedGeometryInstanceHandle);
            if (!decoded.Succeeded())
                return FailDraw(decoded.reason);
            if (decoded.slot >= description.geometryInstanceRecordCount)
                return FailDraw(FallbackReason::SlotOutOfRange);
            if (!description.geometryInstanceRecordValid)
                return FailDraw(FallbackReason::InvalidHandleEncoding);
            if (description.recordGeneration != decoded.generation)
                return FailDraw(FallbackReason::GenerationMismatch);
            if (description.recordStableInstanceHandle == 0u)
                return FailDraw(FallbackReason::TransientMappingFailure);
            if (description.expectedTopologyEpoch == 0u ||
                description.recordTopologyEpoch !=
                    description.expectedTopologyEpoch ||
                description.recordTopologyRevision !=
                    description.expectedTopologyRevision)
            {
                return FailDraw(FallbackReason::TopologyEpochMismatch);
            }
            if (description.recordLodRevision !=
                description.expectedLodRevision)
            {
                return FailDraw(FallbackReason::LodRevisionMismatch);
            }
            if (description.recordDeformationRevision !=
                description.expectedDeformationRevision)
            {
                return FailDraw(
                    FallbackReason::DeformationRevisionMismatch);
            }
            if (description.recordMaterialRevision !=
                description.expectedMaterialRevision)
            {
                return FailDraw(FallbackReason::MaterialRevisionMismatch);
            }
            if (description.recordInstanceIndex !=
                    static_cast<uint32_t>(description.instanceIndex) ||
                description.recordGeometryIndex !=
                    description.geometryLocalIndex)
            {
                return FailDraw(FallbackReason::TransientMappingFailure);
            }

            if (description.indexCount == 0u ||
                description.vertexCount == 0u ||
                description.indexCount % 3u != 0u)
            {
                return FailDraw(FallbackReason::PrimitiveOverflow);
            }
            const uint64_t primitiveCount = description.indexCount / 3u;
            if (primitiveCount >= InvalidVisibilityWord ||
                primitiveCount != description.recordPrimitiveCount)
            {
                return FailDraw(FallbackReason::PrimitiveOverflow);
            }

            uint64_t firstIndex = 0u;
            uint64_t firstVertex = 0u;
            uint64_t endIndex = 0u;
            uint64_t endIndexBytes = 0u;
            if (!TryAdd(
                    description.meshFirstIndex,
                    description.geometryFirstIndex,
                    firstIndex) ||
                !TryAdd(
                    description.meshFirstVertex,
                    description.geometryFirstVertex,
                    firstVertex) ||
                !TryAdd(firstIndex, description.indexCount, endIndex) ||
                !TryMultiply(endIndex, IndexStride, endIndexBytes) ||
                endIndexBytes > description.indexBufferByteSize ||
                endIndexBytes > MaximumGpuByteEnd ||
                !IsSourceByteRangeValid(
                    description.positionRange,
                    firstVertex,
                    description.vertexCount,
                    PositionStride,
                    description.vertexBufferByteSize) ||
                !IsSourceByteRangeValid(
                    description.previousPositionRange,
                    firstVertex,
                    description.vertexCount,
                    PositionStride,
                    description.vertexBufferByteSize) ||
                !IsSourceByteRangeValid(
                    description.texCoordRange,
                    firstVertex,
                    description.vertexCount,
                    TexCoordStride,
                    description.vertexBufferByteSize) ||
                !IsSourceByteRangeValid(
                    description.normalRange,
                    firstVertex,
                    description.vertexCount,
                    PackedDirectionStride,
                    description.vertexBufferByteSize) ||
                !IsSourceByteRangeValid(
                    description.tangentRange,
                    firstVertex,
                    description.vertexCount,
                    PackedDirectionStride,
                    description.vertexBufferByteSize))
            {
                return FailDraw(FallbackReason::CapacityOverflow);
            }

            CompactRasterDrawValidationResult result;
            result.reason = FallbackReason::None;
            if (!TryNarrow(
                    static_cast<uint64_t>(description.instanceIndex),
                    result.draw.instanceIndex) ||
                !TryNarrow(
                    description.geometryLocalIndex,
                    result.draw.geometryLocalIndex) ||
                !TryNarrow(firstIndex, result.draw.startIndexLocation) ||
                !TryNarrow(firstVertex, result.draw.startVertexLocation) ||
                !TryNarrow(
                    description.indexCount,
                    result.draw.indexCount) ||
                !TryNarrow(primitiveCount, result.draw.primitiveCount))
            {
                return FailDraw(FallbackReason::CapacityOverflow);
            }
            result.draw.geometryInstanceHandle =
                description.mappedGeometryInstanceHandle;
            result.draw.geometryRecordIndex =
                description.recordGeometryRecordIndex;
            return result;
        }

        [[nodiscard]] bool IsStableVisibilityFallbackReason(
            FallbackReason reason)
        {
            const uint32_t value = static_cast<uint32_t>(reason);
            return reason != FallbackReason::None &&
                value < static_cast<uint32_t>(FallbackReason::Count) &&
                value <= ADAPTIVE_MSAA_VISIBILITY_CLASS_MAX_REASON;
        }

        [[nodiscard]] bool HasReconstructionOnlyProofFailure(
            const CompactRasterDrawPreflightDesc& description)
        {
            if (!description.oneByOneShadingRate ||
                !description.immutableIndexProofPresent)
                return true;
            return !description.viewConditioningProofPresent ||
                !description.viewConditioningSafe ||
                !description.directionTransformFinite ||
                !description.triangleConditioningProofPresent ||
                !description.triangleConditioningSafe ||
                !std::isfinite(
                    description.minimumAbsoluteProjectedArea) ||
                description.minimumAbsoluteProjectedArea <
                    AdaptiveMsaaCompactMinimumProjectedTriangleArea ||
                description.minimumAbsoluteSnappedArea <
                    AdaptiveMsaaCompactMinimumSnappedTriangleArea ||
                !(description.minimumSnapTieDistance > 0.f) ||
                !std::isfinite(description.minimumStoredDeviceDepth) ||
                !std::isfinite(description.maximumStoredDeviceDepth) ||
                !std::isfinite(
                    description.storedDeviceDepthErrorMargin) ||
                !(description.minimumStoredDeviceDepth > 0.f) ||
                description.minimumStoredDeviceDepth >
                    description.maximumStoredDeviceDepth ||
                description.maximumStoredDeviceDepth > 1.f ||
                description.storedDeviceDepthErrorMargin < 0.f ||
                !std::isfinite(
                    description.maximumAbsoluteAffineBarycentric) ||
                description.maximumAbsoluteAffineBarycentric < 0.f ||
                description.maximumAbsoluteAffineBarycentric >
                    AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric ||
                !std::isfinite(
                    description.minimumPerspectiveDenominator) ||
                description.minimumPerspectiveDenominator <
                    AdaptiveMsaaCompactMinimumPerspectiveDenominator ||
                !std::isfinite(
                    description.maximumAbsolutePerspectiveBarycentric) ||
                description.maximumAbsolutePerspectiveBarycentric < 0.f ||
                description.maximumAbsolutePerspectiveBarycentric >
                    AdaptiveMsaaCompactMaximumAbsolutePerspectiveBarycentric ||
                !std::isfinite(description.maximumAbsoluteTexCoord) ||
                description.maximumAbsoluteTexCoord < 0.f ||
                description.maximumAbsoluteTexCoord >
                    AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient ||
                !std::isfinite(
                    description.maximumAbsoluteTexCoordGradient) ||
                description.maximumAbsoluteTexCoordGradient < 0.f ||
                description.maximumAbsoluteTexCoordGradient >
                    AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient;
        }

        [[nodiscard]] FallbackReason
            ValidateReconstructionRecordContract(
                const CompactRasterDrawPreflightDesc& description)
        {
            if (description.recordGeometryRecordIndex ==
                    InvalidVisibilityWord ||
                description.recordMaterialIndex ==
                    InvalidVisibilityWord ||
                description.recordGeometryRecordIndex >=
                    description.geometryRecordCount ||
                description.recordMaterialIndex >=
                    description.materialRecordCount ||
                description.recordIndexBufferDescriptor ==
                    InvalidVisibilityWord ||
                description.recordVertexBufferDescriptor ==
                    InvalidVisibilityWord)
            {
                return FallbackReason::ReconstructionFailure;
            }
            if (description.textureDescriptorCount >
                    description.descriptorCount ||
                description.rawBufferDescriptorCount !=
                    description.descriptorCount -
                        description.textureDescriptorCount ||
                description.recordIndexBufferDescriptor <
                    description.textureDescriptorCount ||
                description.recordIndexBufferDescriptor >=
                    description.descriptorCount ||
                description.recordVertexBufferDescriptor <
                    description.textureDescriptorCount ||
                description.recordVertexBufferDescriptor >=
                    description.descriptorCount)
            {
                return FallbackReason::TransientMappingFailure;
            }
            if (description.rawBufferDescriptorCount < 2u ||
                description.recordIndexCount != description.indexCount ||
                description.recordVertexCount != description.vertexCount ||
                description.recordGeometryFlags !=
                    description.expectedGeometryFlags)
            {
                return FallbackReason::ReconstructionFailure;
            }

            uint64_t firstIndex = 0u;
            uint64_t firstVertex = 0u;
            uint64_t endIndex = 0u;
            uint64_t indexOffsetBytes = 0u;
            uint64_t endIndexBytes = 0u;
            if (!TryAdd(
                    description.meshFirstIndex,
                    description.geometryFirstIndex,
                    firstIndex) ||
                !TryAdd(
                    description.meshFirstVertex,
                    description.geometryFirstVertex,
                    firstVertex) ||
                !TryAdd(firstIndex, description.indexCount, endIndex) ||
                !TryMultiply(firstIndex, IndexStride, indexOffsetBytes) ||
                !TryMultiply(endIndex, IndexStride, endIndexBytes) ||
                endIndexBytes > description.indexBufferByteSize ||
                endIndexBytes > MaximumGpuByteEnd ||
                indexOffsetBytes >= MaximumGpuByteEnd ||
                description.recordIndexOffset !=
                    static_cast<uint32_t>(indexOffsetBytes))
            {
                return FallbackReason::CapacityOverflow;
            }
            if (!IsByteRangeValid(
                    description.positionRange,
                    firstVertex,
                    description.vertexCount,
                    PositionStride,
                    description.vertexBufferByteSize,
                    description.recordPositionOffset) ||
                !IsByteRangeValid(
                    description.previousPositionRange,
                    firstVertex,
                    description.vertexCount,
                    PositionStride,
                    description.vertexBufferByteSize,
                    description.recordPreviousPositionOffset) ||
                !IsByteRangeValid(
                    description.texCoordRange,
                    firstVertex,
                    description.vertexCount,
                    TexCoordStride,
                    description.vertexBufferByteSize,
                    description.recordTexCoordOffset) ||
                !IsByteRangeValid(
                    description.normalRange,
                    firstVertex,
                    description.vertexCount,
                    PackedDirectionStride,
                    description.vertexBufferByteSize,
                    description.recordNormalOffset) ||
                !IsByteRangeValid(
                    description.tangentRange,
                    firstVertex,
                    description.vertexCount,
                    PackedDirectionStride,
                    description.vertexBufferByteSize,
                    description.recordTangentOffset))
            {
                return FallbackReason::ReconstructionFailure;
            }
            if (description.immutableIndexProofPresent &&
                (description.proofFirstIndex != firstIndex ||
                    description.proofIndexCount !=
                        description.indexCount ||
                    description.proofFirstVertex != firstVertex ||
                    description.proofVertexCount !=
                        description.vertexCount ||
                    description.proofContentRevision == 0u ||
                    description.proofMaximumIndex >=
                        description.vertexCount))
            {
                return FallbackReason::ReconstructionFailure;
            }
            return FallbackReason::None;
        }
    }

    CompactRasterDrawClassificationResult ClassifyCompactRasterDraw(
        const CompactRasterDrawPreflightDesc& description)
    {
        CompactRasterDrawClassificationResult result;
        const CompactRasterDrawValidationResult fallbackIdentity =
            ValidateFullGBufferFallbackDraw(description);
        if (!fallbackIdentity.Succeeded())
        {
            result.reason = fallbackIdentity.reason;
            return result;
        }

        if (description.recordPayloadClass ==
            CompactGeometryPayloadClass::FullGBufferFallback)
        {
            if (!IsStableVisibilityFallbackReason(
                    description.recordPayloadFallbackReason) ||
                description.recordGeometryRecordIndex !=
                    InvalidVisibilityWord)
            {
                result.reason = FallbackReason::ReconstructionFailure;
                return result;
            }
            result.draw = fallbackIdentity.draw;
            result.drawClass =
                CompactRasterDrawClass::FullGBufferFallback;
            result.fallbackProvenance =
                CompactRasterFallbackProvenance::ImmutableSnapshot;
            result.reason = description.recordPayloadFallbackReason;
            return result;
        }

        if (description.recordPayloadClass !=
            CompactGeometryPayloadClass::Reconstruct)
        {
            result.reason = FallbackReason::InvalidHandleEncoding;
            return result;
        }

        const FallbackReason reconstructionRecordReason =
            ValidateReconstructionRecordContract(description);
        if (reconstructionRecordReason != FallbackReason::None)
        {
            result.reason = reconstructionRecordReason;
            return result;
        }

        if (description.recordMaterialTextureMask != 0u &&
            !description.textureReconstructionQualified)
        {
            result.draw = fallbackIdentity.draw;
            result.drawClass =
                CompactRasterDrawClass::FullGBufferFallback;
            result.fallbackProvenance =
                CompactRasterFallbackProvenance::LocalPerView;
            result.reason =
                FallbackReason::TextureQualificationMismatch;
            return result;
        }

        const CompactRasterDrawValidationResult reconstruction =
            ValidateCompactRasterDraw(description);
        if (reconstruction.Succeeded())
        {
            result.draw = reconstruction.draw;
            result.drawClass = CompactRasterDrawClass::Reconstruct;
            result.reason = FallbackReason::None;
            return result;
        }

        const bool localFallbackAllowed =
            reconstruction.reason ==
                FallbackReason::UnsupportedDeformation ||
            reconstruction.reason == FallbackReason::UnsupportedMaterial ||
            (reconstruction.reason ==
                FallbackReason::ReconstructionFailure &&
                HasReconstructionOnlyProofFailure(description));
        if (!localFallbackAllowed)
        {
            result.reason = reconstruction.reason;
            return result;
        }

        result.draw = fallbackIdentity.draw;
        result.drawClass = CompactRasterDrawClass::FullGBufferFallback;
        result.fallbackProvenance =
            CompactRasterFallbackProvenance::LocalPerView;
        result.reason = reconstruction.reason;
        return result;
    }

    FallbackReason ValidateCompactRasterPreparedBinding(
        const CompactRasterDrawPreflightDesc& description,
        const CompactRasterValidatedDraw& validatedDraw,
        const CompactRasterPreparedBindingDesc& binding)
    {
        uint32_t positionOffset = 0u;
        uint32_t previousPositionOffset = 0u;
        uint32_t texCoordOffset = 0u;
        uint32_t normalOffset = 0u;
        uint32_t tangentOffset = 0u;
        if (!TryNarrow(description.positionRange.byteOffset,
                positionOffset) ||
            !TryNarrow(description.previousPositionRange.byteOffset,
                previousPositionOffset) ||
            !TryNarrow(description.texCoordRange.byteOffset,
                texCoordOffset) ||
            !TryNarrow(description.normalRange.byteOffset,
                normalOffset) ||
            !TryNarrow(description.tangentRange.byteOffset,
                tangentOffset))
        {
            return FallbackReason::CapacityOverflow;
        }

        uint64_t boundIndexByteOffset = 0u;
        uint64_t indexedByteOffset = 0u;
        if (!TryMultiply(
                binding.drawStartIndexLocation,
                IndexStride,
                indexedByteOffset) ||
            !TryAdd(
                binding.indexBindingByteOffset,
                indexedByteOffset,
                boundIndexByteOffset) ||
            boundIndexByteOffset != description.recordIndexOffset)
        {
            return FallbackReason::ReconstructionFailure;
        }

        return binding.indexBindingIsR32Uint &&
            binding.drawStartIndexLocation ==
                validatedDraw.startIndexLocation &&
            binding.drawStartVertexLocation == 0u &&
            binding.drawStartInstanceLocation == 0u &&
            binding.pushStartInstanceLocation ==
                validatedDraw.instanceIndex &&
            binding.pushStartVertexLocation ==
                validatedDraw.startVertexLocation &&
            binding.pushPositionOffset == positionOffset &&
            binding.pushPreviousPositionOffset ==
                previousPositionOffset &&
            binding.pushTexCoordOffset == texCoordOffset &&
            binding.pushNormalOffset == normalOffset &&
            binding.pushTangentOffset == tangentOffset &&
            binding.pushVisibilityClass ==
                ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT
                ? FallbackReason::None
                : FallbackReason::ReconstructionFailure;
    }

    namespace
    {
        [[nodiscard]] FallbackReason
            ValidateFullGBufferFallbackPreparedBinding(
                const CompactRasterDrawPreflightDesc& description,
                const CompactRasterValidatedDraw& validatedDraw,
                FallbackReason fallbackReason,
                CompactRasterFallbackProvenance fallbackProvenance,
                const CompactRasterPreparedBindingDesc& binding)
        {
            uint32_t positionOffset = 0u;
            uint32_t previousPositionOffset = 0u;
            uint32_t texCoordOffset = 0u;
            uint32_t normalOffset = 0u;
            uint32_t tangentOffset = 0u;
            if (!TryNarrow(
                    description.positionRange.byteOffset,
                    positionOffset) ||
                !TryNarrow(
                    description.previousPositionRange.byteOffset,
                    previousPositionOffset) ||
                !TryNarrow(
                    description.texCoordRange.byteOffset,
                    texCoordOffset) ||
                !TryNarrow(
                    description.normalRange.byteOffset,
                    normalOffset) ||
                !TryNarrow(
                    description.tangentRange.byteOffset,
                    tangentOffset) ||
                !IsStableVisibilityFallbackReason(fallbackReason))
            {
                return FallbackReason::CapacityOverflow;
            }

            uint64_t firstIndex = 0u;
            uint64_t expectedIndexByteOffset = 0u;
            uint64_t indexedByteOffset = 0u;
            uint64_t boundIndexByteOffset = 0u;
            if (!TryAdd(
                    description.meshFirstIndex,
                    description.geometryFirstIndex,
                    firstIndex) ||
                !TryMultiply(
                    firstIndex, IndexStride, expectedIndexByteOffset) ||
                !TryMultiply(
                    binding.drawStartIndexLocation,
                    IndexStride,
                    indexedByteOffset) ||
                !TryAdd(
                    binding.indexBindingByteOffset,
                    indexedByteOffset,
                    boundIndexByteOffset) ||
                boundIndexByteOffset != expectedIndexByteOffset)
            {
                return FallbackReason::ReconstructionFailure;
            }

            const bool localFallback = fallbackProvenance ==
                CompactRasterFallbackProvenance::LocalPerView;
            if ((!localFallback && fallbackProvenance !=
                    CompactRasterFallbackProvenance::ImmutableSnapshot) ||
                localFallback !=
                    (description.recordPayloadClass ==
                        CompactGeometryPayloadClass::Reconstruct))
            {
                return FallbackReason::ReconstructionFailure;
            }
            const uint32_t marker =
                AdaptiveMsaaEncodeFallbackVisibilityClass(
                    static_cast<uint32_t>(fallbackReason),
                    localFallback);
            if (marker == ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID)
                return FallbackReason::ReconstructionFailure;
            const FallbackReason payloadMarkerReason =
                ValidateCompactRasterFallbackVisibilityClass(
                    description.recordPayloadClass,
                    description.recordPayloadFallbackReason,
                    binding.pushVisibilityClass);
            if (payloadMarkerReason != FallbackReason::None)
                return payloadMarkerReason;
            return binding.indexBindingIsR32Uint &&
                binding.drawStartIndexLocation ==
                    validatedDraw.startIndexLocation &&
                binding.drawStartVertexLocation == 0u &&
                binding.drawStartInstanceLocation == 0u &&
                binding.pushStartInstanceLocation ==
                    validatedDraw.instanceIndex &&
                binding.pushStartVertexLocation ==
                    validatedDraw.startVertexLocation &&
                binding.pushPositionOffset == positionOffset &&
                binding.pushPreviousPositionOffset ==
                    previousPositionOffset &&
                binding.pushTexCoordOffset == texCoordOffset &&
                binding.pushNormalOffset == normalOffset &&
                binding.pushTangentOffset == tangentOffset &&
                binding.pushVisibilityClass == marker
                    ? FallbackReason::None
                    : FallbackReason::ReconstructionFailure;
        }
    }

    CompactRasterCoverageMetadata EvaluateCompactRasterCoverageMetadata(
        uint32_t sampleCount,
        uint32_t primitiveCoverageMask,
        uint32_t finalSurfaceMask)
    {
        CompactRasterCoverageMetadata result;
        if (!IsSupportedSampleCount(sampleCount))
            return result;
        const uint32_t fullMask = (1u << sampleCount) - 1u;
        primitiveCoverageMask &= fullMask;
        finalSurfaceMask &= fullMask;
        if (primitiveCoverageMask == 0u || finalSurfaceMask == 0u ||
            (finalSurfaceMask & ~primitiveCoverageMask) != 0u)
        {
            return result;
        }
        result.valid = true;
        if (primitiveCoverageMask == fullMask)
            return result;
        for (uint32_t sample = 0u; sample < sampleCount; ++sample)
        {
            if ((primitiveCoverageMask & (1u << sample)) != 0u)
            {
                result.centroidSample = sample;
                break;
            }
        }
        return result;
    }

    CompactRasterPreflightResult PreflightCompactRasterDraws(
        const CompactRasterDrawPreflightDesc* descriptions,
        size_t descriptionCount)
    {
        CompactRasterPreflightResult result;
        result.drawCount = descriptionCount;
        if (descriptionCount != 0u && !descriptions)
            return result;

        uint64_t triangleCount = 0u;
        uint64_t reconstructDrawCount = 0u;
        uint64_t reconstructTriangleCount = 0u;
        uint64_t fallbackDrawCount = 0u;
        uint64_t fallbackTriangleCount = 0u;
        for (size_t index = 0u; index < descriptionCount; ++index)
        {
            const CompactRasterDrawClassificationResult classification =
                ClassifyCompactRasterDraw(descriptions[index]);
            if (!classification.Succeeded())
            {
                result.reason = classification.reason;
                result.transientViewConditioningFailure =
                    descriptions[index].viewConditioningProofPresent &&
                    !descriptions[index].viewConditioningSafe &&
                    classification.reason ==
                        FallbackReason::ReconstructionFailure;
                result.failingView = descriptions[index].sourceViewIndex;
                result.failingDraw = descriptions[index].sourceDrawIndex;
                return result;
            }
            if (!TryAdd(
                    triangleCount,
                    classification.draw.primitiveCount,
                    triangleCount))
            {
                result.reason = FallbackReason::CapacityOverflow;
                result.failingView = descriptions[index].sourceViewIndex;
                result.failingDraw = descriptions[index].sourceDrawIndex;
                return result;
            }
            if (classification.Reconstructs())
            {
                if (!TryAdd(
                        reconstructDrawCount,
                        1u,
                        reconstructDrawCount) ||
                    !TryAdd(
                        reconstructTriangleCount,
                        classification.draw.primitiveCount,
                        reconstructTriangleCount))
                {
                    result.reason = FallbackReason::CapacityOverflow;
                    result.failingView =
                        descriptions[index].sourceViewIndex;
                    result.failingDraw =
                        descriptions[index].sourceDrawIndex;
                    return result;
                }
            }
            else
            {
                if (!TryAdd(
                        fallbackDrawCount,
                        1u,
                        fallbackDrawCount) ||
                    !TryAdd(
                        fallbackTriangleCount,
                        classification.draw.primitiveCount,
                        fallbackTriangleCount))
                {
                    result.reason = FallbackReason::CapacityOverflow;
                    result.failingView =
                        descriptions[index].sourceViewIndex;
                    result.failingDraw =
                        descriptions[index].sourceDrawIndex;
                    return result;
                }
                result.transientViewConditioningFailure =
                    result.transientViewConditioningFailure ||
                    (classification.reason ==
                        FallbackReason::ReconstructionFailure &&
                        descriptions[index].viewConditioningProofPresent &&
                        !descriptions[index].viewConditioningSafe);
            }
        }

        result.eligible = true;
        result.reason = FallbackReason::None;
        result.triangleCount = triangleCount;
        result.reconstructDrawCount = reconstructDrawCount;
        result.reconstructTriangleCount = reconstructTriangleCount;
        result.fallbackDrawCount = fallbackDrawCount;
        result.fallbackTriangleCount = fallbackTriangleCount;
        return result;
    }

    CompactRasterPreflightResult
        ValidateCompactRasterTopologyAvailability(
            const CompactRasterPreflightResult& preflight,
            bool mixedTopologyAvailable)
    {
        CompactRasterPreflightResult result = preflight;
        if (result.Succeeded() && result.fallbackDrawCount != 0u &&
            !mixedTopologyAvailable)
        {
            result.eligible = false;
            result.reason = FallbackReason::None;
            result.mixedTopologyRequired = true;
        }
        return result;
    }

    float EvaluateCompactRasterOpacity(
        float materialOpacity,
        bool useOpacityTexture,
        float opacityTextureValue,
        bool useBaseOrDiffuseTexture,
        float baseOrDiffuseAlpha)
    {
        float opacity = materialOpacity;
        if (useOpacityTexture)
            opacity *= opacityTextureValue;
        else if (useBaseOrDiffuseTexture)
            opacity *= baseOrDiffuseAlpha;
        return std::clamp(opacity, 0.f, 1.f);
    }

    bool CompactRasterAlphaTestPasses(
        bool materialDomainIsOpaque,
        float saturatedOpacity,
        float alphaCutoff)
    {
        return materialDomainIsOpaque ||
            !(saturatedOpacity - alphaCutoff < 0.f);
    }

    AdaptiveMsaaCompactVisibilityPass::AdaptiveMsaaCompactVisibilityPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<donut::engine::CommonRenderPasses>&
            commonPasses,
        nvrhi::IBuffer* geometryInstanceHandleMap)
        : GBufferFillPass(device, commonPasses)
        , m_GeometryInstanceHandleMap(geometryInstanceHandleMap)
    {
    }

    void AdaptiveMsaaCompactVisibilityPass::Init(
        donut::engine::ShaderFactory& shaderFactory,
        const CreateParameters& parameters)
    {
        CreateParameters compact = parameters;
        compact.useInputAssembler = false;
        compact.enableSinglePassCubemap = false;
        compact.enableMotionVectors = false;
        compact.enableDepthWrite = true;
        GBufferFillPass::Init(shaderFactory, compact);
        std::vector<donut::engine::ShaderMacro> alphaMacros;
        alphaMacros.emplace_back("ALPHA_TESTED", "1");
        m_AlphaTestedVertexShader = shaderFactory.CreateShader(
            "uvsr/adaptive_msaa_compact_visibility_vs.hlsl",
            "main",
            &alphaMacros,
            nvrhi::ShaderType::Vertex);
        m_Initialized = m_VertexShader && m_AlphaTestedVertexShader &&
            m_PixelShader && m_PixelShaderAlphaTested &&
            m_InputBindingLayout && m_ViewBindingLayout;
        AdvancePreparationEpoch();
    }

    void AdaptiveMsaaCompactVisibilityPass::SetGeometryInstanceHandleMap(
        nvrhi::IBuffer* geometryInstanceHandleMap)
    {
        if (m_GeometryInstanceHandleMap.Get() == geometryInstanceHandleMap)
            return;
        m_GeometryInstanceHandleMap = geometryInstanceHandleMap;
        if (m_Initialized)
            ResetBindingCache();
        else
            m_InputBindingSets.clear();
        AdvancePreparationEpoch();
    }

    nvrhi::IBuffer* AdaptiveMsaaCompactVisibilityPass::
        GetGeometryInstanceHandleMap() const
    {
        return m_GeometryInstanceHandleMap.Get();
    }

    bool AdaptiveMsaaCompactVisibilityPass::
        HasValidGeometryInstanceHandleMap() const
    {
        return IsCompactMapBuffer(m_GeometryInstanceHandleMap.Get());
    }

    uint64_t AdaptiveMsaaCompactVisibilityPass::PreparationEpoch() const
    {
        return m_PreparationEpoch;
    }

    void AdaptiveMsaaCompactVisibilityPass::AdvancePreparationEpoch()
    {
        if (m_PreparationEpoch == 0u)
            return;
        m_PreparationEpoch = m_PreparationEpoch ==
                std::numeric_limits<uint64_t>::max()
            ? 0u
            : m_PreparationEpoch + 1u;
    }

    bool AdaptiveMsaaCompactVisibilityPass::SetupMaterial(
        donut::render::GeometryPassContext& context,
        const donut::engine::Material* material,
        nvrhi::RasterCullMode cullMode,
        nvrhi::GraphicsState& state)
    {
        if (!HasValidGeometryInstanceHandleMap() || !material ||
            (material->domain != MaterialDomain::Opaque &&
                material->domain != MaterialDomain::AlphaTested))
        {
            return false;
        }
        if (!GBufferFillPass::SetupMaterial(
                context,
                material,
                cullMode,
                state))
        {
            return false;
        }
        return state.pipeline && !state.bindings.empty();
    }

    void AdaptiveMsaaCompactVisibilityPass::SetGeometryLocalIndex(
        Context& context,
        uint32_t geometryLocalIndex) const
    {
        context.geometryLocalIndex = geometryLocalIndex;
    }

    void AdaptiveMsaaCompactVisibilityPass::SetPushConstants(
        donut::render::GeometryPassContext& abstractContext,
        nvrhi::ICommandList* commandList,
        nvrhi::GraphicsState& state,
        nvrhi::DrawArguments& arguments)
    {
        (void)state;
        auto& context = static_cast<Context&>(abstractContext);
        AdaptiveMsaaGBufferPushConstants constants;
        constants.startInstanceLocation = arguments.startInstanceLocation;
        constants.startVertexLocation = arguments.startVertexLocation;
        constants.positionOffset = context.positionOffset;
        constants.prevPositionOffset = context.prevPositionOffset;
        constants.texCoordOffset = context.texCoordOffset;
        constants.normalOffset = context.normalOffset;
        constants.tangentOffset = context.tangentOffset;
        constants.geometryLocalIndex = context.geometryLocalIndex;
        constants.visibilityClass =
            ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT;
        commandList->setPushConstants(&constants, sizeof(constants));
        context.geometryLocalIndex = InvalidGeometryLocalIndex;
        arguments.startInstanceLocation = 0u;
        arguments.startVertexLocation = 0u;
    }

    nvrhi::ShaderHandle AdaptiveMsaaCompactVisibilityPass::
        CreateVertexShader(
            donut::engine::ShaderFactory& shaderFactory,
            const CreateParameters& parameters)
    {
        (void)parameters;
        std::vector<donut::engine::ShaderMacro> macros;
        macros.emplace_back("ALPHA_TESTED", "0");
        return shaderFactory.CreateShader(
            "uvsr/adaptive_msaa_compact_visibility_vs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Vertex);
    }

    nvrhi::ShaderHandle AdaptiveMsaaCompactVisibilityPass::
        CreatePixelShader(
            donut::engine::ShaderFactory& shaderFactory,
            const CreateParameters& parameters,
            bool alphaTested)
    {
        (void)parameters;
        std::vector<donut::engine::ShaderMacro> macros;
        macros.emplace_back("ALPHA_TESTED", alphaTested ? "1" : "0");
        return shaderFactory.CreateShader(
            "uvsr/adaptive_msaa_compact_visibility_ps.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Pixel);
    }

    nvrhi::BindingLayoutHandle AdaptiveMsaaCompactVisibilityPass::
        CreateInputBindingLayout()
    {
        const nvrhi::BindingLayoutDesc description =
            nvrhi::BindingLayoutDesc()
                .setVisibility(
                    nvrhi::ShaderType::Vertex |
                    nvrhi::ShaderType::Pixel)
                .setRegisterSpaceAndDescriptorSet(GBUFFER_SPACE_INPUT)
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    GBUFFER_BINDING_INSTANCE_BUFFER))
                .addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(
                    GBUFFER_BINDING_VERTEX_BUFFER))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    ADAPTIVE_MSAA_BINDING_GEOMETRY_INSTANCE_HANDLE_MAP))
                .addItem(nvrhi::BindingLayoutItem::PushConstants(
                    GBUFFER_BINDING_PUSH_CONSTANTS,
                    sizeof(AdaptiveMsaaGBufferPushConstants)));
        return m_Device->createBindingLayout(description);
    }

    nvrhi::BindingSetHandle AdaptiveMsaaCompactVisibilityPass::
        CreateInputBindingSet(const BufferGroup* bufferGroup)
    {
        if (!bufferGroup || !bufferGroup->instanceBuffer ||
            !bufferGroup->vertexBuffer ||
            !HasValidGeometryInstanceHandleMap())
        {
            return nullptr;
        }
        const nvrhi::BindingSetDesc description = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(
                GBUFFER_BINDING_INSTANCE_BUFFER,
                bufferGroup->instanceBuffer))
            .addItem(nvrhi::BindingSetItem::RawBuffer_SRV(
                GBUFFER_BINDING_VERTEX_BUFFER,
                bufferGroup->vertexBuffer))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(
                ADAPTIVE_MSAA_BINDING_GEOMETRY_INSTANCE_HANDLE_MAP,
                m_GeometryInstanceHandleMap))
            .addItem(nvrhi::BindingSetItem::PushConstants(
                GBUFFER_BINDING_PUSH_CONSTANTS,
                sizeof(AdaptiveMsaaGBufferPushConstants)));
        return m_Device->createBindingSet(
            description,
            m_InputBindingLayout);
    }

    nvrhi::GraphicsPipelineHandle AdaptiveMsaaCompactVisibilityPass::
        CreateGraphicsPipeline(
            PipelineKey key,
            const nvrhi::FramebufferInfo& framebufferInfo)
    {
        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.inputLayout = m_InputLayout;
        pipelineDesc.VS = key.bits.alphaTested
            ? m_AlphaTestedVertexShader
            : m_VertexShader;
        pipelineDesc.GS = m_GeometryShader;
        pipelineDesc.renderState.rasterState
            .setFrontCounterClockwise(key.bits.frontCounterClockwise)
            .setCullMode(key.bits.cullMode);
        pipelineDesc.renderState.blendState.disableAlphaToCoverage();
        pipelineDesc.bindingLayouts = {
            m_MaterialBindings->GetLayout(),
            m_ViewBindingLayout,
            m_InputBindingLayout
        };
        pipelineDesc.renderState.depthStencilState
            .setDepthWriteEnable(m_EnableDepthWrite)
            .setDepthFunc(key.bits.reverseDepth
                ? nvrhi::ComparisonFunc::GreaterOrEqual
                : nvrhi::ComparisonFunc::LessOrEqual);

        if (m_StencilWriteMask)
        {
            pipelineDesc.renderState.depthStencilState
                .enableStencil()
                .setStencilReadMask(0u)
                .setStencilWriteMask(uint8_t(m_StencilWriteMask))
                .setStencilRefValue(uint8_t(m_StencilWriteMask))
                .setFrontFaceStencil(
                    nvrhi::DepthStencilState::StencilOpDesc()
                        .setPassOp(nvrhi::StencilOp::Replace))
                .setBackFaceStencil(
                    nvrhi::DepthStencilState::StencilOpDesc()
                        .setPassOp(nvrhi::StencilOp::Replace));
        }

        if (key.bits.alphaTested)
        {
            pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.PS = m_PixelShaderAlphaTested;
        }
        else
        {
            pipelineDesc.PS = m_PixelShader;
        }
        if (!pipelineDesc.VS || !pipelineDesc.PS)
            return nullptr;
        return m_Device->createGraphicsPipeline(
            pipelineDesc,
            framebufferInfo);
    }

    CompactRasterPreparedSubmission
        PrepareAdaptiveMsaaCompactVisibilityCompositeView(
            const CompactRasterPrepareParameters& parameters)
    {
        CompactRasterPreparedSubmission prepared;
        const auto Reject = [&prepared](
            FallbackReason reason,
            uint32_t view = InvalidVisibilityWord,
            uint32_t draw = InvalidVisibilityWord)
        {
            prepared.m_Preflight.eligible = false;
            prepared.m_Preflight.reason = reason;
            prepared.m_Preflight.failingView = view;
            prepared.m_Preflight.failingDraw = draw;
            prepared.m_Impl.reset();
            return std::move(prepared);
        };

        if (!parameters.compositeView || !parameters.framebufferFactory ||
            !parameters.expectedSurfaceKeyTarget ||
            !parameters.expectedVisibilityClassTarget ||
            !parameters.expectedDepthTarget || !parameters.sceneGraph ||
            !parameters.rootNode ||
            !parameters.drawStrategy || !parameters.pass ||
            !parameters.compactScene || !parameters.cpuIndexProofs ||
            parameters.sceneGraph->GetRootNode().get() !=
                parameters.rootNode.get() ||
            !parameters.cpuIndexProofs->IsForSceneGraph(
                parameters.sceneGraph.get()) ||
            parameters.cpuIndexProofs->BuildRevision() == 0u ||
            !ValidateSnapshotContract(
                *parameters.compactScene,
                *parameters.pass))
        {
            return Reject(FallbackReason::ReconstructionFailure);
        }

        const bool mixedFactoryProvided =
            parameters.mixedFallbackFramebufferFactory != nullptr;
        const bool mixedPassProvided =
            parameters.mixedFallbackPass != nullptr;
        if (mixedFactoryProvided != mixedPassProvided)
            return Reject(FallbackReason::ReconstructionFailure);

        const donut::engine::ViewType::Enum supportedViewTypes =
            parameters.pass->GetSupportedViewTypes();
        const uint32_t viewCount =
            parameters.compositeView->GetNumChildViews(supportedViewTypes);
        if (viewCount >= InvalidVisibilityWord)
            return Reject(FallbackReason::CapacityOverflow);
        std::vector<CapturedView> capturedViews;
        capturedViews.reserve(viewCount);
        std::vector<CompactRasterDrawPreflightDesc> descriptions;

        for (uint32_t viewIndex = 0u; viewIndex < viewCount; ++viewIndex)
        {
            const IView* view = parameters.compositeView->GetChildView(
                supportedViewTypes,
                viewIndex);
            if (!view)
            {
                return Reject(
                    FallbackReason::ReconstructionFailure,
                    viewIndex);
            }
            nvrhi::IFramebuffer* framebuffer =
                parameters.framebufferFactory->GetFramebuffer(*view);
            if (!IsCompactFramebuffer(
                    framebuffer,
                    parameters.expectedSurfaceKeyTarget,
                    parameters.expectedVisibilityClassTarget,
                    parameters.expectedDepthTarget))
            {
                return Reject(
                    FallbackReason::FormatUnsupported,
                    viewIndex);
            }

            CapturedView capture;
            capture.sourceViewIndex = viewIndex;
            capture.view = view;
            capture.framebuffer = framebuffer;
            const nvrhi::FramebufferDesc& capturedFramebuffer =
                framebuffer->getDesc();
            CompactRasterViewProof qualificationProof;
            qualificationProof.sourceViewIndex = viewIndex;
            qualificationProof.view = view;
            qualificationProof.surfaceKeyTarget =
                capturedFramebuffer.colorAttachments[0].texture;
            qualificationProof.visibilityClassTarget =
                capturedFramebuffer.colorAttachments[1].texture;
            qualificationProof.depthTarget =
                capturedFramebuffer.depthAttachment.texture;
            qualificationProof.requestedSampleCount =
                parameters.requestedSampleCount;
            qualificationProof.sampleCount =
                qualificationProof.surfaceKeyTarget->getDesc().sampleCount;
            qualificationProof.sampleQuality =
                qualificationProof.surfaceKeyTarget->getDesc().sampleQuality;
            qualificationProof.viewport = view->GetViewportState();
            const CompactTextureQualificationViewDecision
                textureQualification =
                    EvaluateTextureQualificationForView(
                        *parameters.compactScene,
                        parameters.textureQualificationAuthority,
                        qualificationProof);
            if (!textureQualification.valid)
            {
                return Reject(
                    textureQualification.reason,
                    viewIndex);
            }
            capture.textureReconstructionQualified =
                textureQualification.textureReconstructionQualified;
            parameters.drawStrategy->PrepareForView(
                parameters.rootNode,
                *view);
            while (const DrawItem* item =
                parameters.drawStrategy->GetNextItem())
            {
                capture.draws.push_back(*item);
            }

            if (capture.draws.size() >= InvalidVisibilityWord)
            {
                return Reject(
                    FallbackReason::CapacityOverflow,
                    viewIndex);
            }
            for (size_t drawIndex = 0u;
                drawIndex < capture.draws.size();
                ++drawIndex)
            {
                FallbackReason constructionReason = FallbackReason::None;
                CompactRasterDrawPreflightDesc description = DescribeDraw(
                    capture.draws[drawIndex],
                    *view,
                    viewIndex,
                    static_cast<uint32_t>(drawIndex),
                    *parameters.compactScene,
                    *parameters.cpuIndexProofs,
                    constructionReason);
                if (constructionReason != FallbackReason::None)
                {
                    return Reject(
                        constructionReason,
                        viewIndex,
                        static_cast<uint32_t>(drawIndex));
                }
                description.textureReconstructionQualified =
                    capture.textureReconstructionQualified;
                descriptions.push_back(description);
            }
            capturedViews.push_back(std::move(capture));
        }

        prepared.m_Preflight = PreflightCompactRasterDraws(
            descriptions.data(),
            descriptions.size());
        if (!prepared.m_Preflight.Succeeded())
            return prepared;

        const bool requiresFallback =
            prepared.m_Preflight.fallbackDrawCount != 0u;
        prepared.m_Preflight =
            ValidateCompactRasterTopologyAvailability(
                prepared.m_Preflight,
                mixedFactoryProvided);
        if (prepared.m_Preflight.mixedTopologyRequired)
            return prepared;
        if (requiresFallback &&
            (!HasExactFallbackTargetSet(
                    parameters.expectedFallbackGBufferTargets) ||
                (parameters.expectedFallbackMotionTarget &&
                    (!parameters.compositeViewPrev ||
                        parameters.compositeViewPrev->GetNumChildViews(
                            supportedViewTypes) != viewCount)) ||
                !ValidateFallbackPassContract(
                    *parameters.compactScene,
                    *parameters.mixedFallbackPass,
                    supportedViewTypes)))
        {
            return Reject(FallbackReason::FormatUnsupported);
        }

        size_t topologyDescriptionIndex = 0u;
        for (CapturedView& capture : capturedViews)
        {
            bool viewUsesFallback = false;
            for (size_t drawIndex = 0u;
                drawIndex < capture.draws.size(); ++drawIndex)
            {
                const CompactRasterDrawClassificationResult classification =
                    ClassifyCompactRasterDraw(
                        descriptions[topologyDescriptionIndex++]);
                if (!classification.Succeeded())
                {
                    return Reject(
                        classification.reason,
                        capture.sourceViewIndex,
                        static_cast<uint32_t>(drawIndex));
                }
                viewUsesFallback = viewUsesFallback ||
                    classification.UsesFullGBufferFallback();
            }
            if (!viewUsesFallback)
                continue;

            capture.fallbackPreviousView = capture.view;
            if (parameters.expectedFallbackMotionTarget)
            {
                capture.fallbackPreviousView =
                    parameters.compositeViewPrev->GetChildView(
                        supportedViewTypes,
                        capture.sourceViewIndex);
                if (!capture.fallbackPreviousView)
                {
                    return Reject(
                        FallbackReason::ReconstructionFailure,
                        capture.sourceViewIndex);
                }
            }

            nvrhi::IFramebuffer* fallbackFramebuffer =
                parameters.mixedFallbackFramebufferFactory->GetFramebuffer(
                    *capture.view);
            if (!IsMixedFallbackFramebuffer(
                    fallbackFramebuffer,
                    parameters.expectedFallbackGBufferTargets,
                    parameters.expectedFallbackMotionTarget,
                    parameters.expectedSurfaceKeyTarget,
                    parameters.expectedVisibilityClassTarget,
                    parameters.expectedDepthTarget))
            {
                return Reject(
                    FallbackReason::FormatUnsupported,
                    capture.sourceViewIndex);
            }
            capture.fallbackFramebuffer = fallbackFramebuffer;
        }

        // All device-object creation ends here in Prepare. None of these APIs
        // accepts a command list; SetupView is intentionally deferred.
        std::unique_ptr<CompactRasterPreparedSubmission::Impl>
            implementation =
                std::make_unique<CompactRasterPreparedSubmission::Impl>();
        implementation->pass = parameters.pass;
        implementation->fallbackPass =
            requiresFallback ? parameters.mixedFallbackPass : nullptr;
        implementation->sceneGraphKeepAlive = parameters.sceneGraph;
        implementation->rootNodeKeepAlive = parameters.rootNode;
        implementation->compactSceneKeepAlive =
            parameters.compactScene;
        implementation->textureQualificationAuthority =
            parameters.textureQualificationAuthority;
        implementation->geometryInstanceHandleMapKeepAlive =
            parameters.compactScene->InstanceHandleBuffer();
        implementation->cpuIndexProofsKeepAlive =
            parameters.cpuIndexProofs;
        implementation->compactSceneVersion =
            parameters.compactScene->Version();
        implementation->cpuIndexProofBuildRevision =
            parameters.cpuIndexProofs->BuildRevision();
        implementation->passPreparationEpoch =
            parameters.pass->PreparationEpoch();
        implementation->fallbackPassPreparationEpoch =
            requiresFallback
            ? parameters.mixedFallbackPass->PreparationEpoch()
            : 0u;
        implementation->passEvent = parameters.passEvent
            ? parameters.passEvent
            : "";
        implementation->preparationThread =
            std::this_thread::get_id();
        implementation->materialEvents = parameters.materialEvents;
        implementation->views.reserve(capturedViews.size());
        implementation->viewProofs.reserve(capturedViews.size());
        size_t descriptionIndex = 0u;
        for (const CapturedView& capture : capturedViews)
        {
            PreparedView view;
            view.sourceViewIndex = capture.sourceViewIndex;
            view.view = capture.view;
            view.fallbackPreviousView = capture.fallbackPreviousView;
            view.framebufferKeepAlive = capture.framebuffer;
            view.fallbackFramebufferKeepAlive =
                capture.fallbackFramebuffer;
            view.viewport = capture.view->GetViewportState();
            view.shadingRateState =
                capture.view->GetVariableRateShadingState();
            view.mirrored = capture.view->IsMirrored();
            view.reverseDepth = capture.view->IsReverseDepth();
            view.rasterIdentity = CaptureRasterViewIdentity(
                *capture.view);
            if (capture.fallbackPreviousView)
            {
                view.fallbackPreviousRasterIdentity =
                    CaptureRasterViewIdentity(
                        *capture.fallbackPreviousView);
            }
            if (view.viewport.viewports.size() != 1u ||
                !SameFloatBits(view.viewport.viewports[0].minZ, 0.f) ||
                !SameFloatBits(view.viewport.viewports[0].maxZ, 1.f))
            {
                return Reject(
                    FallbackReason::ReconstructionFailure,
                    capture.sourceViewIndex);
            }

            const nvrhi::FramebufferDesc& framebuffer =
                capture.framebuffer->getDesc();
            CompactRasterViewProof viewProof;
            viewProof.sourceViewIndex = capture.sourceViewIndex;
            viewProof.view = capture.view;
            viewProof.surfaceKeyTarget =
                framebuffer.colorAttachments[0].texture;
            viewProof.visibilityClassTarget =
                framebuffer.colorAttachments[1].texture;
            viewProof.depthTarget = framebuffer.depthAttachment.texture;
            viewProof.surfaceKeySubresources =
                framebuffer.colorAttachments[0].subresources;
            viewProof.visibilityClassSubresources =
                framebuffer.colorAttachments[1].subresources;
            viewProof.depthSubresources =
                framebuffer.depthAttachment.subresources;
            viewProof.surfaceKeyFormat = EffectiveAttachmentFormat(
                framebuffer.colorAttachments[0]);
            viewProof.visibilityClassFormat = EffectiveAttachmentFormat(
                framebuffer.colorAttachments[1]);
            viewProof.depthFormat = EffectiveAttachmentFormat(
                framebuffer.depthAttachment);
            viewProof.requestedSampleCount =
                parameters.requestedSampleCount;
            viewProof.sampleCount =
                viewProof.surfaceKeyTarget->getDesc().sampleCount;
            viewProof.sampleQuality =
                viewProof.surfaceKeyTarget->getDesc().sampleQuality;
            viewProof.viewport = view.viewport;
            viewProof.transformIdentity = view.rasterIdentity;
            viewProof.acceptedDepth = MakeEmptyAcceptedDepthInterval();
            viewProof.fullGBufferFallbackPresent =
                bool(capture.fallbackFramebuffer);
            viewProof.fallbackMayStoreZeroDepth =
                viewProof.fullGBufferFallbackPresent;
            const CompactTextureQualificationViewDecision
                textureQualification =
                    EvaluateTextureQualificationForView(
                    *parameters.compactScene,
                    parameters.textureQualificationAuthority,
                    viewProof);
            if (!textureQualification.valid ||
                textureQualification.textureReconstructionQualified !=
                    capture.textureReconstructionQualified)
            {
                return Reject(
                    textureQualification.reason == FallbackReason::None
                        ? FallbackReason::TransientMappingFailure
                        : textureQualification.reason,
                    capture.sourceViewIndex);
            }
            if (capture.fallbackFramebuffer)
            {
                const nvrhi::FramebufferDesc& fallbackFramebuffer =
                    capture.fallbackFramebuffer->getDesc();
                for (size_t index = 0u;
                    index < viewProof.fallbackGBufferTargets.size();
                    ++index)
                {
                    viewProof.fallbackGBufferTargets[index] =
                        fallbackFramebuffer.colorAttachments[index].texture;
                    viewProof.fallbackGBufferSubresources[index] =
                        fallbackFramebuffer.colorAttachments[index].subresources;
                }
                if (parameters.expectedFallbackMotionTarget)
                {
                    const size_t motionIndex =
                        viewProof.fallbackGBufferTargets.size();
                    viewProof.fallbackMotionTarget =
                        fallbackFramebuffer.colorAttachments[
                            motionIndex].texture;
                    viewProof.fallbackMotionSubresources =
                        fallbackFramebuffer.colorAttachments[
                            motionIndex].subresources;
                }
            }
            AdaptiveMsaaCompactVisibilityPass::Context prepareContext;
            prepareContext.keyTemplate.bits.frontCounterClockwise =
                view.mirrored;
            prepareContext.keyTemplate.bits.reverseDepth =
                view.reverseDepth;
            uvsr::AdaptiveMsaaGBufferFillPass::Context
                fallbackPrepareContext;
            fallbackPrepareContext.keyTemplate.bits.frontCounterClockwise =
                view.mirrored;
            fallbackPrepareContext.keyTemplate.bits.reverseDepth =
                view.reverseDepth;

            for (size_t drawIndex = 0u;
                drawIndex < capture.draws.size();
                ++drawIndex, ++descriptionIndex)
            {
                const DrawItem& item = capture.draws[drawIndex];
                const CompactRasterDrawClassificationResult classification =
                    ClassifyCompactRasterDraw(
                        descriptions[descriptionIndex]);
                if (!classification.Succeeded())
                {
                    return Reject(
                        classification.reason,
                        descriptions[descriptionIndex].sourceViewIndex,
                        descriptions[descriptionIndex].sourceDrawIndex);
                }
                if (classification.Reconstructs() &&
                    !AccumulateAcceptedDepthInterval(
                        viewProof.acceptedDepth,
                        descriptions[descriptionIndex]))
                {
                    return Reject(
                        FallbackReason::ReconstructionFailure,
                        descriptions[descriptionIndex].sourceViewIndex,
                        descriptions[descriptionIndex].sourceDrawIndex);
                }

                PreparedDraw draw;
                draw.sourceItem = item;
                if (!item.instance->GetNode() ||
                    item.instance->GetNode()->GetLeaf().get() !=
                        item.instance ||
                    item.instance->GetMesh().get() != item.mesh ||
                    item.mesh->buffers.get() != item.buffers ||
                    item.geometry->material.get() != item.material)
                {
                    return Reject(
                        FallbackReason::TransientMappingFailure,
                        descriptions[descriptionIndex].sourceViewIndex,
                        descriptions[descriptionIndex].sourceDrawIndex);
                }
                draw.instanceKeepAlive =
                    item.instance->GetNode()->GetLeaf();
                draw.meshKeepAlive = item.instance->GetMesh();
                draw.buffersKeepAlive = item.mesh->buffers;
                draw.materialKeepAlive = item.geometry->material;
                for (const auto& geometry : item.mesh->geometries)
                {
                    if (geometry.get() == item.geometry)
                    {
                        draw.geometryKeepAlive = geometry;
                        break;
                    }
                }
                if (!draw.instanceKeepAlive || !draw.meshKeepAlive ||
                    !draw.geometryKeepAlive || !draw.materialKeepAlive ||
                    !draw.buffersKeepAlive)
                {
                    return Reject(
                        FallbackReason::TransientMappingFailure,
                        descriptions[descriptionIndex].sourceViewIndex,
                        descriptions[descriptionIndex].sourceDrawIndex);
                }
                draw.description = descriptions[descriptionIndex];
                if (classification.Reconstructs())
                {
                    draw.materialRecordIndex =
                        draw.description.recordMaterialIndex;
                    const auto& fingerprints =
                        parameters.compactScene->CpuMaterialFingerprints();
                    const auto& descriptorRecords =
                        parameters.compactScene->
                            CpuMaterialTextureDescriptors();
                    if (draw.materialRecordIndex >= fingerprints.size() ||
                        draw.materialRecordIndex >=
                            descriptorRecords.size())
                    {
                        return Reject(
                            FallbackReason::TransientMappingFailure,
                            descriptions[descriptionIndex].sourceViewIndex,
                            descriptions[descriptionIndex].sourceDrawIndex);
                    }
                    const std::shared_ptr<const donut::engine::Material>
                        material = draw.materialKeepAlive;
                    const FallbackReason fingerprintReason =
                        ValidateCompactMaterialFingerprint(
                            fingerprints[draw.materialRecordIndex],
                            material,
                            descriptorRecords[draw.materialRecordIndex],
                            parameters.compactScene->
                                TextureQualification());
                    if (fingerprintReason != FallbackReason::None)
                    {
                        return Reject(
                            fingerprintReason,
                            descriptions[descriptionIndex].sourceViewIndex,
                            descriptions[descriptionIndex].sourceDrawIndex);
                    }
                }
                draw.validated = classification.draw;
                draw.drawClass = classification.drawClass;
                draw.fallbackReason = classification.reason;
                draw.objectToWorld = CaptureObjectToWorld(item);
                draw.instanceBufferIdentity = CaptureBufferIdentity(
                    item.buffers->instanceBuffer);
                draw.indexBufferIdentity = CaptureBufferIdentity(
                    item.buffers->indexBuffer);
                draw.vertexBufferIdentity = CaptureBufferIdentity(
                    item.buffers->vertexBuffer);
                draw.state.viewport = view.viewport;
                draw.state.shadingRateState = view.shadingRateState;
                bool statePrepared = false;
                uint32_t positionOffset = 0u;
                uint32_t previousPositionOffset = 0u;
                uint32_t texCoordOffset = 0u;
                uint32_t normalOffset = 0u;
                uint32_t tangentOffset = 0u;
                nvrhi::IFramebuffer* expectedFramebuffer = nullptr;
                if (classification.Reconstructs())
                {
                    draw.state.framebuffer = capture.framebuffer.Get();
                    expectedFramebuffer = capture.framebuffer.Get();
                    parameters.pass->SetupInputBuffers(
                        prepareContext,
                        item.buffers,
                        draw.state);
                    statePrepared = prepareContext.inputBindingSet &&
                        parameters.pass->SetupMaterial(
                            prepareContext,
                            item.material,
                            item.cullMode,
                            draw.state);
                    positionOffset = prepareContext.positionOffset;
                    previousPositionOffset =
                        prepareContext.prevPositionOffset;
                    texCoordOffset = prepareContext.texCoordOffset;
                    normalOffset = prepareContext.normalOffset;
                    tangentOffset = prepareContext.tangentOffset;
                }
                else
                {
                    draw.state.framebuffer =
                        capture.fallbackFramebuffer.Get();
                    expectedFramebuffer =
                        capture.fallbackFramebuffer.Get();
                    parameters.mixedFallbackPass->SetupInputBuffers(
                        fallbackPrepareContext,
                        item.buffers,
                        draw.state);
                    statePrepared =
                        fallbackPrepareContext.inputBindingSet &&
                        parameters.mixedFallbackPass->SetupMaterial(
                            fallbackPrepareContext,
                            item.material,
                            item.cullMode,
                            draw.state);
                    positionOffset =
                        fallbackPrepareContext.positionOffset;
                    previousPositionOffset =
                        fallbackPrepareContext.prevPositionOffset;
                    texCoordOffset =
                        fallbackPrepareContext.texCoordOffset;
                    normalOffset = fallbackPrepareContext.normalOffset;
                    tangentOffset = fallbackPrepareContext.tangentOffset;
                }
                if (!statePrepared || !expectedFramebuffer ||
                    !draw.state.pipeline || draw.state.bindings.empty() ||
                    !draw.state.indexBuffer.buffer ||
                    draw.state.pipeline->getFramebufferInfo() !=
                        expectedFramebuffer->getFramebufferInfo())
                {
                    return Reject(
                        FallbackReason::ReconstructionFailure,
                        descriptions[descriptionIndex].sourceViewIndex,
                        descriptions[descriptionIndex].sourceDrawIndex);
                }

                draw.pipelineKeepAlive = draw.state.pipeline;
                draw.indexBufferKeepAlive = draw.state.indexBuffer.buffer;
                draw.vertexBufferKeepAlive = item.buffers->vertexBuffer;
                draw.instanceBufferKeepAlive = item.buffers->instanceBuffer;
                draw.bindingKeepAlive.reserve(draw.state.bindings.size());
                for (nvrhi::IBindingSet* binding : draw.state.bindings)
                {
                    if (!binding)
                    {
                        return Reject(
                            FallbackReason::ReconstructionFailure,
                            descriptions[descriptionIndex].sourceViewIndex,
                            descriptions[descriptionIndex].sourceDrawIndex);
                    }
                    draw.bindingKeepAlive.emplace_back(binding);
                }

                draw.push.startInstanceLocation =
                    classification.draw.instanceIndex;
                draw.push.startVertexLocation =
                    classification.draw.startVertexLocation;
                draw.push.positionOffset = positionOffset;
                draw.push.prevPositionOffset =
                    previousPositionOffset;
                draw.push.texCoordOffset = texCoordOffset;
                draw.push.normalOffset = normalOffset;
                draw.push.tangentOffset = tangentOffset;
                draw.push.geometryLocalIndex =
                    classification.draw.geometryLocalIndex;
                draw.push.visibilityClass = classification.Reconstructs()
                    ? ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT
                    : AdaptiveMsaaEncodeFallbackVisibilityClass(
                        static_cast<uint32_t>(classification.reason),
                        classification.UsesLocalFullGBufferFallback());
                draw.arguments.vertexCount =
                    classification.draw.indexCount;
                draw.arguments.instanceCount = 1u;
                draw.arguments.startIndexLocation =
                    classification.draw.startIndexLocation;
                draw.arguments.startVertexLocation = 0u;
                draw.arguments.startInstanceLocation = 0u;
                draw.materialIdentity = reinterpret_cast<uintptr_t>(
                    item.material);
                draw.materialName = item.material->name;
                draw.triangleCount =
                    classification.draw.primitiveCount;
                const CompactRasterPreparedBindingDesc preparedBinding =
                    CapturePreparedBinding(
                        draw.state,
                        draw.push,
                        draw.arguments);
                const FallbackReason preparedBindingReason =
                    classification.Reconstructs()
                    ? ValidateCompactRasterPreparedBinding(
                        draw.description,
                        draw.validated,
                        preparedBinding)
                    : ValidateFullGBufferFallbackPreparedBinding(
                        draw.description,
                        draw.validated,
                        classification.reason,
                        classification.fallbackProvenance,
                        preparedBinding);
                if (preparedBindingReason != FallbackReason::None)
                {
                    return Reject(
                        FallbackReason::ReconstructionFailure,
                        descriptions[descriptionIndex].sourceViewIndex,
                        descriptions[descriptionIndex].sourceDrawIndex);
                }
                view.draws.push_back(std::move(draw));
            }
            if (!SameViewProofResources(viewProof, view))
            {
                return Reject(
                    FallbackReason::TransientMappingFailure,
                    capture.sourceViewIndex);
            }
            implementation->views.push_back(std::move(view));
            implementation->viewProofs.push_back(std::move(viewProof));
        }

        if (parameters.pass->GetGeometryInstanceHandleMap() !=
                implementation->geometryInstanceHandleMapKeepAlive.Get() ||
            parameters.pass->PreparationEpoch() !=
                implementation->passPreparationEpoch ||
            parameters.compactScene->Version().topologyEpoch !=
                implementation->compactSceneVersion.topologyEpoch ||
            parameters.compactScene->Version().buildSerial !=
                implementation->compactSceneVersion.buildSerial ||
            parameters.cpuIndexProofs->BuildRevision() !=
                implementation->cpuIndexProofBuildRevision ||
            (implementation->fallbackPass &&
                (implementation->fallbackPass->PreparationEpoch() !=
                    implementation->fallbackPassPreparationEpoch ||
                    implementation->fallbackPass->
                        GetGeometryInstanceHandleMap() !=
                        implementation->geometryInstanceHandleMapKeepAlive
                            .Get())))
        {
            return Reject(FallbackReason::TransientMappingFailure);
        }

        prepared.m_Impl = std::move(implementation);
        return prepared;
    }

    CompactRasterSubmissionResult
        SubmitAdaptiveMsaaCompactVisibilityCompositeView(
            nvrhi::ICommandList* commandList,
            CompactRasterPreparedSubmission&& prepared,
            AdaptiveMsaaCompactVisibilityPass& pass,
            AdaptiveMsaaCompactVisibilityPass::Context& passContext)
    {
        CompactRasterSubmissionResult result;
        result.preflight = prepared.m_Preflight;

        // The renderer latches the complete command-free validation
        // immediately before opening its command list. Consume that exact
        // transaction here; recording performs no second recoverable
        // scene/view/resource validation or allocation that could strand a
        // partially recorded compact frame. SetupView only writes the exact
        // view constants proven at that boundary.
        std::unique_ptr<CompactRasterPreparedSubmission::Impl>
            implementation = std::move(prepared.m_Impl);
        if (!implementation || !result.preflight.Succeeded() ||
            !implementation->validatedForSubmission ||
            implementation->preparationThread !=
                std::this_thread::get_id() ||
            implementation->pass != &pass)
        {
            result.preflight.eligible = false;
            if (result.preflight.reason == FallbackReason::None)
                result.preflight.reason =
                    FallbackReason::ReconstructionFailure;
            return result;
        }
        if (!commandList)
        {
            result.preflight.eligible = false;
            result.preflight.reason = FallbackReason::ReconstructionFailure;
            return result;
        }

        if (!implementation->passEvent.empty())
            commandList->beginMarker(implementation->passEvent.c_str());
        for (const PreparedView& view : implementation->views)
        {
            pass.SetupView(
                passContext,
                commandList,
                view.view,
                view.view);
            uvsr::AdaptiveMsaaGBufferFillPass::Context fallbackContext;
            if (view.fallbackFramebufferKeepAlive)
            {
                implementation->fallbackPass->SetupView(
                    fallbackContext,
                    commandList,
                    view.view,
                    view.fallbackPreviousView);
            }
            uintptr_t eventMaterial = 0u;
            for (const PreparedDraw& draw : view.draws)
            {
                if (implementation->materialEvents &&
                    draw.materialIdentity != eventMaterial)
                {
                    if (eventMaterial)
                        commandList->endMarker();
                    eventMaterial = draw.materialIdentity;
                    if (eventMaterial && !draw.materialName.empty())
                        commandList->beginMarker(draw.materialName.c_str());
                    else
                        eventMaterial = 0u;
                }
                commandList->setGraphicsState(draw.state);
                commandList->setPushConstants(
                    &draw.push,
                    sizeof(draw.push));
                commandList->drawIndexed(draw.arguments);
                result.submittedTriangles += draw.triangleCount;
                if (draw.drawClass ==
                    CompactRasterDrawClass::Reconstruct)
                {
                    ++result.submittedReconstructDraws;
                    result.submittedReconstructTriangles +=
                        draw.triangleCount;
                }
                else
                {
                    ++result.submittedFallbackDraws;
                    result.submittedFallbackTriangles +=
                        draw.triangleCount;
                }
            }
            if (implementation->materialEvents && eventMaterial)
                commandList->endMarker();
        }
        if (!implementation->passEvent.empty())
            commandList->endMarker();
        return result;
    }
}
