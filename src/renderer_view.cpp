/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "renderer_view.h"
#include <math.h>

namespace uvsr
{
gpu_contract::Float4x4 RendererPerspectiveReverseDepth(float verticalFovRadians, float aspect, float nearPlane) noexcept
{
    const float yScale = 1.f / tanf(.5f * verticalFovRadians);
    const float xScale = yScale / aspect;
    return {{xScale, 0, 0, 0, 0, yScale, 0, 0, 0, 0, 0, 1, 0, 0, nearPlane, 0}};
}

namespace
{
    using Matrix = gpu_contract::Float4x4;

    Matrix Identity() noexcept
    {
        return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    }

    bool Finite(const Matrix& matrix) noexcept
    {
        for (float value : matrix.values)
            if (!isfinite(value)) return false;
        return true;
    }

    Matrix Multiply(const Matrix& a, const Matrix& b) noexcept
    {
        Matrix result{};
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column)
                for (unsigned index = 0; index < 4; ++index)
                    result.values[row * 4 + column] += a.values[row * 4 + index] * b.values[index * 4 + column];
        return result;
    }

    // retain the existing pivot threshold and operation order for 3x3 affine
    // and 4x4 projection inverses. supported camera inputs are nonsingular.
    bool Inverse(Matrix a, unsigned size, Matrix& result) noexcept
    {
        Matrix b = Identity();
        for (unsigned column = 0; column < size; ++column)
        {
            unsigned pivot = column;
            for (unsigned row = column + 1; row < size; ++row)
                if (fabsf(a.values[row * 4 + column]) > fabsf(a.values[pivot * 4 + column])) pivot = row;
            if (fabsf(a.values[pivot * 4 + column]) < 1e-6f) return false;
            if (pivot != column)
            {
                for (unsigned index = 0; index < size; ++index)
                {
                    const float x = a.values[column * 4 + index];
                    a.values[column * 4 + index] = a.values[pivot * 4 + index];
                    a.values[pivot * 4 + index] = x;
                    const float y = b.values[column * 4 + index];
                    b.values[column * 4 + index] = b.values[pivot * 4 + index];
                    b.values[pivot * 4 + index] = y;
                }
            }
            if (a.values[column * 4 + column] != 1.f)
            {
                const float scale = a.values[column * 4 + column];
                for (unsigned index = 0; index < size; ++index)
                {
                    a.values[column * 4 + index] /= scale;
                    b.values[column * 4 + index] /= scale;
                }
            }
            for (unsigned row = 0; row < size; ++row)
            {
                if (row == column || fabsf(a.values[row * 4 + column]) <= 1e-6f) continue;
                const float scale = -a.values[row * 4 + column];
                for (unsigned index = 0; index < size; ++index)
                {
                    a.values[row * 4 + index] += a.values[column * 4 + index] * scale;
                    b.values[row * 4 + index] += b.values[column * 4 + index] * scale;
                }
            }
        }
        result = b;
        return Finite(result);
    }

    RendererScenePlane Plane(float x, float y, float z, float distance) noexcept
    {
        const float lengthSquared = x * x + y * y + z * z;
        const float scale = lengthSquared > 0.f ? 1.f / sqrtf(lengthSquared) : 0.f;
        return {{x * scale, y * scale, z * scale}, distance * scale};
    }

    RendererSceneFrustum Frustum(const Matrix& matrix, bool reverseDepth) noexcept
    {
        const float* m = matrix.values;
        RendererSceneFrustum result;
        result.planes[reverseDepth ? 1 : 0] = Plane(-m[2], -m[6], -m[10], m[14]);
        result.planes[reverseDepth ? 0 : 1] = Plane(-m[3] + m[2], -m[7] + m[6], -m[11] + m[10], m[15] - m[14]);
        result.planes[2] = Plane(-m[3] - m[0], -m[7] - m[4], -m[11] - m[8], m[15] + m[12]);
        result.planes[3] = Plane(-m[3] + m[0], -m[7] + m[4], -m[11] + m[8], m[15] - m[12]);
        result.planes[4] = Plane(-m[3] + m[1], -m[7] + m[5], -m[11] + m[9], m[15] - m[13]);
        result.planes[5] = Plane(-m[3] - m[1], -m[7] - m[5], -m[11] - m[9], m[15] + m[13]);
        return result;
    }
}

bool BuildRendererView(const RendererViewport& viewport, const Matrix& worldToView,
    const Matrix& viewToClip, gpu_contract::Float2 pixelOffset, RendererView& result) noexcept
{
    if (!Finite(worldToView) || !Finite(viewToClip) ||
        worldToView.values[3] != 0.f || worldToView.values[7] != 0.f ||
        worldToView.values[11] != 0.f || worldToView.values[15] != 1.f ||
        !isfinite(pixelOffset.x) || !isfinite(pixelOffset.y) ||
        !isfinite(viewport.minX) || !isfinite(viewport.minY) || !isfinite(viewport.minZ) ||
        !isfinite(viewport.maxX) || !isfinite(viewport.maxY) || !isfinite(viewport.maxZ) ||
        viewport.minX >= viewport.maxX || viewport.minY >= viewport.maxY ||
        viewport.minZ < 0.f || viewport.maxZ > 1.f || viewport.minZ > viewport.maxZ)
        return false;
    const double minX = floor(viewport.minX), minY = floor(viewport.minY);
    const double maxX = ceil(viewport.maxX), maxY = ceil(viewport.maxY);
    if (minX < INT32_MIN || minY < INT32_MIN || maxX > INT32_MAX || maxY > INT32_MAX ||
        maxX - minX > INT32_MAX || maxY - minY > INT32_MAX)
        return false;

    RendererView candidate;
    auto& c = candidate.constants;
    const float width = viewport.maxX - viewport.minX;
    const float height = viewport.maxY - viewport.minY;
    Matrix offset = Identity(), inverseOffset, inverseView;
    offset.values[12] = 2.f * pixelOffset.x / width;
    offset.values[13] = -2.f * pixelOffset.y / height;
    if (!Inverse(offset, 4, inverseOffset) || !Inverse(worldToView, 3, inverseView) ||
        !Inverse(viewToClip, 4, c.matClipToViewNoOffset))
        return false;
    // affine inverse translation uses the original three-term vector product.
    for (unsigned column = 0; column < 3; ++column)
        inverseView.values[12 + column] = -worldToView.values[12] * inverseView.values[column] +
            -worldToView.values[13] * inverseView.values[4 + column] +
            -worldToView.values[14] * inverseView.values[8 + column];
    c.matWorldToView = worldToView;
    c.matViewToWorld = inverseView;
    c.matViewToClipNoOffset = viewToClip;
    c.matWorldToClipNoOffset = Multiply(worldToView, viewToClip);
    c.matWorldToClip = Multiply(c.matWorldToClipNoOffset, offset);
    c.matClipToWorldNoOffset = Multiply(c.matClipToViewNoOffset, inverseView);
    c.matClipToWorld = Multiply(inverseOffset, c.matClipToWorldNoOffset);
    c.matViewToClip = Multiply(viewToClip, offset);
    c.matClipToView = Multiply(inverseOffset, c.matClipToViewNoOffset);
    if (!Finite(c.matWorldToClip) || !Finite(c.matClipToWorld) || !Finite(c.matViewToClip) ||
        !Finite(c.matClipToView) || !Finite(c.matClipToWorldNoOffset)) return false;

    c.viewportOrigin = {viewport.minX, viewport.minY};
    c.viewportSize = {width, height};
    c.viewportSizeInv = {1.f / width, 1.f / height};
    c.pixelOffset = pixelOffset;
    c.clipToWindowScale = {0.5f * width, -0.5f * height};
    c.clipToWindowBias = {viewport.minX + width * 0.5f, viewport.minY + height * 0.5f};
    c.windowToClipScale = {1.f / c.clipToWindowScale.x, 1.f / c.clipToWindowScale.y};
    c.windowToClipBias = {-c.clipToWindowBias.x * c.windowToClipScale.x, -c.clipToWindowBias.y * c.windowToClipScale.y};
    if (!isfinite(c.windowToClipScale.x) || !isfinite(c.windowToClipScale.y) ||
        !isfinite(c.windowToClipBias.x) || !isfinite(c.windowToClipBias.y)) return false;
    const bool orthographic = viewToClip.values[11] == 0.f;
    const unsigned cameraRow = orthographic ? 8 : 12;
    c.cameraDirectionOrPosition = {inverseView.values[cameraRow], inverseView.values[cameraRow + 1],
        inverseView.values[cameraRow + 2], orthographic ? 0.f : 1.f};
    candidate.reverseDepth = viewToClip.values[10] <= 0.f;
    candidate.frustum = Frustum(c.matWorldToClipNoOffset, candidate.reverseDepth);
    for (const auto& plane : candidate.frustum.planes)
        if (!isfinite(plane.normal.x) || !isfinite(plane.normal.y) || !isfinite(plane.normal.z) || !isfinite(plane.distance)) return false;
    const float* m = worldToView.values;
    candidate.mirrored = (m[0]*m[5]*m[10] + m[1]*m[6]*m[8] + m[2]*m[4]*m[9]) -
        (m[8]*m[5]*m[2] + m[9]*m[6]*m[0] + m[10]*m[4]*m[1]) < 0.f;
    candidate.viewport = viewport;
    candidate.extent = {int32_t(minX), int32_t(maxX), int32_t(minY), int32_t(maxY)};
    candidate.valid = true;
    result = candidate;
    return true;
}

gpu_contract::Float4x4 RendererClipToTranslatedWorld(const RendererView& view) noexcept
{
    Matrix translation = Identity();
    const auto& inverse = view.constants.matViewToWorld;
    for (unsigned index = 0; index < 3; ++index) translation.values[12 + index] = -inverse.values[12 + index];
    return Multiply(view.constants.matClipToWorld, translation);
}
}
