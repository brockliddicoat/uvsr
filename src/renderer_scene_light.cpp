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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "renderer_scene_light.h"
#include "renderer_vector_math.h"
#include <math.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene light commands require exception-disabled compilation
#endif

namespace uvsr
{
    namespace
    {
        using Vector3 = RendererSceneLightDirection;

        double Dot(Vector3 a, Vector3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
        Vector3 Cross(Vector3 a, Vector3 b) noexcept
        { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
        bool Finite(Vector3 value) noexcept { return isfinite(value.x) && isfinite(value.y) && isfinite(value.z); }
        Vector3 Read(const double* values) noexcept { return {values[0], values[1], values[2]}; }
        bool Normalize(Vector3& value) noexcept
        {
            const double length = sqrt(Dot(value, value));
            if (!isfinite(length) || length <= 0) return false;
            value = {value.x / length, value.y / length, value.z / length};
            return Finite(value);
        }

        Vector3 Orthogonal(Vector3 value) noexcept
        {
            // Sam Hocevar's orthogonal-vector rule, retained for pose parity.
            return fabs(value.x) > fabs(value.z) ? Vector3{-value.y, value.x, 0} : Vector3{0, -value.z, value.y};
        }

        // preserve the existing pivot threshold and operation order. this command
        // reports singular input instead of passing NaNs into a scene mutation.
        bool Inverse(const double* source, double* output) noexcept
        {
            double a[9], b[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
            for (uint32_t lane = 0; lane < 9; ++lane)
            {
                if (!isfinite(source[lane])) return false;
                a[lane] = source[lane];
            }
            constexpr double epsilon = double(1e-6f);
            for (uint32_t column = 0; column < 3; ++column)
            {
                uint32_t pivot = column;
                for (uint32_t row = column + 1; row < 3; ++row)
                    if (fabs(a[row * 3 + column]) > fabs(a[pivot * 3 + column])) pivot = row;
                if (fabs(a[pivot * 3 + column]) < epsilon) return false;
                if (pivot != column)
                    for (uint32_t lane = 0; lane < 3; ++lane)
                    {
                        const double left = a[column * 3 + lane], right = b[column * 3 + lane];
                        a[column * 3 + lane] = a[pivot * 3 + lane]; a[pivot * 3 + lane] = left;
                        b[column * 3 + lane] = b[pivot * 3 + lane]; b[pivot * 3 + lane] = right;
                    }
                if (a[column * 3 + column] != 1)
                {
                    const double scale = a[column * 3 + column];
                    for (uint32_t lane = 0; lane < 3; ++lane)
                    { a[column * 3 + lane] /= scale; b[column * 3 + lane] /= scale; }
                }
                for (uint32_t row = 0; row < 3; ++row)
                    if (row != column && fabs(a[row * 3 + column]) > epsilon)
                    {
                        const double scale = -a[row * 3 + column];
                        for (uint32_t lane = 0; lane < 3; ++lane)
                        {
                            a[row * 3 + lane] += a[column * 3 + lane] * scale;
                            b[row * 3 + lane] += b[column * 3 + lane] * scale;
                        }
                    }
            }
            for (uint32_t lane = 0; lane < 9; ++lane)
                if (!isfinite(b[lane])) return false;
            for (uint32_t lane = 0; lane < 9; ++lane) output[lane] = b[lane];
            return true;
        }

        double QuaternionMagnitude(double value) noexcept { return sqrt(value > 0 ? value : 0) * 0.5; }

        bool Decompose(const double* linear, RendererSceneTransform& output) noexcept
        {
            Vector3 columns[3];
            for (uint32_t column = 0; column < 3; ++column)
            {
                auto& value = columns[column];
                value = {linear[column], linear[3 + column], linear[6 + column]};
                const double scale = sqrt(Dot(value, value));
                if (!isfinite(scale)) return false;
                output.scaling[column] = scale;
                if (scale > 0) value = {value.x / scale, value.y / scale, value.z / scale};
            }
            auto& x = columns[0];
            const auto y = columns[1], z = columns[2];
            if (Dot(Cross(x, y), z) < 0)
            { output.scaling[0] = -output.scaling[0]; x = {-x.x, -x.y, -x.z}; }
            output.rotation[3] = QuaternionMagnitude(1 + x.x + y.y + z.z);
            output.rotation[0] = copysign(QuaternionMagnitude(1 + x.x - y.y - z.z), z.y - y.z);
            output.rotation[1] = copysign(QuaternionMagnitude(1 - x.x + y.y - z.z), x.z - z.x);
            output.rotation[2] = copysign(QuaternionMagnitude(1 - x.x - y.y + z.z), y.x - x.y);
            return true;
        }

        const RendererSceneNode* LightNode(const RendererSceneView& scene, RendererSceneHandle handle) noexcept
        {
            const auto* light = FindRendererSceneLight(scene, handle);
            if (!light || light->kind >= RendererSceneLightKind::Count || !scene.nodes.IsValid() || light->nodeIndex >= scene.nodes.count)
                return nullptr;
            const auto& node = scene.nodes.data[light->nodeIndex];
            return node.leafKind == RendererSceneLeafKind::Light && node.leafIndex == handle.index ? &node : nullptr;
        }
    }

    RendererSceneLightAngles GetRendererSceneLightAngles(RendererSceneLightDirection direction, bool directional) noexcept
    {
        const double magnitude = sqrt(Dot(direction, direction));
        direction = {direction.x / magnitude, direction.y / magnitude, direction.z / magnitude};
        if (directional) direction = {-direction.x, -direction.y, -direction.z};
        const double vertical = direction.y < -1.0 ? -1.0 : direction.y > 1.0 ? 1.0 : direction.y;
        return {Degrees(float(atan2(direction.z, direction.x))), Degrees(float(asin(vertical)))};
    }

    RendererSceneLightDirection MakeRendererSceneLightDirection(
        float azimuthDegrees, float elevationDegrees, bool directional) noexcept
    {
        const double azimuth = Radians(double(azimuthDegrees)), elevation = Radians(double(elevationDegrees));
        const double horizontal = cos(elevation);
        RendererSceneLightDirection direction{cos(azimuth) * horizontal, sin(elevation), sin(azimuth) * horizontal};
        if (directional) direction = {-direction.x, -direction.y, -direction.z};
        const double magnitude = sqrt(Dot(direction, direction));
        return {direction.x / magnitude, direction.y / magnitude, direction.z / magnitude};
    }

    RendererSceneResult GetRendererSceneLightFrame(const RendererSceneView& scene,
        RendererSceneHandle light, RendererSceneLightFrame& output) noexcept
    {
        if (!light || light.generation != scene.generation) return {RendererSceneError::Generation};
        const auto* node = LightNode(scene, light);
        if (!node) return {RendererSceneError::Reference, light.index};
        if (!Finite(Read(node->world.translation))) return {RendererSceneError::Value, light.index};
        RendererSceneLightFrame frame;
        for (uint32_t lane = 0; lane < 3; ++lane) frame.position[lane] = node->world.translation[lane];
        Vector3 direction = Read(node->world.linear + 6);
        if (Normalize(direction))
        { frame.direction[0] = -direction.x; frame.direction[1] = -direction.y; frame.direction[2] = -direction.z; }
        else if (scene.lights.data[light.index].kind != RendererSceneLightKind::Point)
            return {RendererSceneError::Value, light.index};
        output = frame;
        return {};
    }

    RendererSceneResult ResolveRendererSceneLightTransform(const RendererSceneTransform& current,
        const RendererSceneAffine& parent, const RendererSceneLightPose& pose,
        RendererSceneTransform& output) noexcept
    {
        if (pose.setRight && !pose.setDirection) return {RendererSceneError::Reference};
        auto candidate = current;
        if (pose.setPosition)
        {
            if (!Finite(Read(pose.position))) return {RendererSceneError::Value};
            double inverse[9];
            if (!Inverse(parent.linear, inverse)) return {RendererSceneError::Value};
            for (uint32_t lane = 0; lane < 3; ++lane)
            {
                const double translation = -parent.translation[0] * inverse[lane] +
                    -parent.translation[1] * inverse[3 + lane] + -parent.translation[2] * inverse[6 + lane];
                candidate.translation[lane] = pose.position[0] * inverse[lane] + pose.position[1] * inverse[3 + lane] +
                    pose.position[2] * inverse[6 + lane] + translation;
            }
        }
        if (pose.setDirection)
        {
            Vector3 direction = Read(pose.direction), left, up;
            if (!Normalize(direction)) return {RendererSceneError::Value};
            if (pose.setRight)
            {
                Vector3 right = Read(pose.right);
                if (!Finite(right)) return {RendererSceneError::Value};
                const double projection = Dot(right, direction);
                right = {right.x - direction.x * projection, right.y - direction.y * projection, right.z - direction.z * projection};
                if (Dot(right, right) <= 1e-20) right = Orthogonal(direction);
                if (!Normalize(right)) return {RendererSceneError::Value};
                up = Cross(right, direction);
                if (!Normalize(up) || !Normalize(direction)) return {RendererSceneError::Value};
                left = Cross(up, direction);
            }
            else left = Orthogonal(direction);
            if (!Normalize(left)) return {RendererSceneError::Value};
            up = Cross(direction, left);
            const double worldToLocal[]{-left.x, up.x, -direction.x, -left.y, up.y, -direction.y, -left.z, up.z, -direction.z};
            double product[9]{}, localToParent[9];
            for (uint32_t row = 0; row < 3; ++row)
                for (uint32_t column = 0; column < 3; ++column)
                    for (uint32_t lane = 0; lane < 3; ++lane)
                        product[row * 3 + column] += worldToLocal[row * 3 + lane] * parent.linear[lane * 3 + column];
            if (!Inverse(product, localToParent) || !Decompose(localToParent, candidate))
                return {RendererSceneError::Value};
        }
        output = candidate;
        return {};
    }

    RendererSceneResult SetRendererSceneLightPose(RendererScene& scene,
        RendererSceneHandle light, const RendererSceneLightPose& pose) noexcept
    {
        if (!scene.IsPublished()) return {RendererSceneError::InvalidState};
        const auto view = scene.View();
        if (!light || light.generation != view.generation) return {RendererSceneError::Generation};
        const auto* node = LightNode(view, light);
        if (!node || (pose.setRight && !pose.setDirection)) return {RendererSceneError::Reference, light.index};
        if (!pose.setPosition && !pose.setDirection) return {};
        RendererSceneAffine parent;
        if (node->parentIndex != InvalidSceneIndex)
        {
            if (node->parentIndex >= view.nodes.count) return {RendererSceneError::Reference};
            parent = view.nodes.data[node->parentIndex].world;
        }
        RendererSceneTransform candidate;
        auto result = ResolveRendererSceneLightTransform(node->transform, parent, pose, candidate);
        if (!result.Succeeded()) { result.index = light.index; return result; }
        return scene.SetTransform({view.generation, uint32_t(node - view.nodes.data)}, candidate);
    }

    bool RendererSceneLightRange::IsValid() const noexcept
    {
        return scene.generation && scene.lights.IsValid() && scene.lights.count <= UINT32_MAX && scene.nodes.IsValid();
    }

    uint32_t RendererSceneLightRange::Count() const noexcept
    {
        if (!IsValid()) return 0;
        return uint32_t(scene.lights.count) - uint32_t(!includeFlashlight && FindRendererSceneLight(scene, flashlight));
    }

    RendererSceneHandle RendererSceneLightRange::At(uint32_t ordinal) const noexcept
    {
        if (ordinal >= Count()) return {};
        if (FindRendererSceneLight(scene, flashlight))
        {
            if (includeFlashlight)
            {
                if (!ordinal) return flashlight;
                --ordinal;
            }
            if (ordinal >= flashlight.index) ++ordinal;
        }
        return {scene.generation, ordinal};
    }

    uint32_t RendererSceneLightRange::Ordinal(RendererSceneHandle light) const noexcept
    {
        if (!IsValid() || !FindRendererSceneLight(scene, light)) return InvalidSceneIndex;
        if (!FindRendererSceneLight(scene, flashlight)) return light.index;
        if (light == flashlight) return includeFlashlight ? 0 : InvalidSceneIndex;
        return light.index - uint32_t(light.index > flashlight.index) + uint32_t(includeFlashlight);
    }
}
