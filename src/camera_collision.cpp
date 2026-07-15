#include "camera_collision.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace donut::math;

namespace uvsr
{
    namespace
    {
        constexpr uint32_t MaxTrianglesPerLeaf = 8;
        constexpr uint32_t MaxPenetrationIterations = 8;
        constexpr uint32_t MaxSlideIterations = 4;

        box3 GetTriangleBounds(const CameraCollisionWorld::Triangle& triangle)
        {
            box3 bounds = box3::empty();
            bounds |= triangle.a;
            bounds |= triangle.b;
            bounds |= triangle.c;
            return bounds;
        }

        float3 GetTriangleCentroid(const CameraCollisionWorld::Triangle& triangle)
        {
            return (triangle.a + triangle.b + triangle.c) / 3.f;
        }

        bool IsUsableTriangle(const CameraCollisionWorld::Triangle& triangle)
        {
            if (!all(dm::isfinite(triangle.a)) ||
                !all(dm::isfinite(triangle.b)) ||
                !all(dm::isfinite(triangle.c)))
            {
                return false;
            }

            // Degenerate importer output cannot provide a stable separation
            // direction and should not become an invisible collision spike.
            return lengthSquared(cross(triangle.b - triangle.a, triangle.c - triangle.a)) > 1e-20f;
        }

        float3 ClosestPointOnTriangle(
            float3 point,
            const CameraCollisionWorld::Triangle& triangle)
        {
            // Voronoi-region test from Real-Time Collision Detection. Unlike a
            // plane-only test, this handles triangle edges and vertices, which
            // is what gives the camera sphere a continuous rounded hitbox at
            // mesh seams and corners.
            const float3 ab = triangle.b - triangle.a;
            const float3 ac = triangle.c - triangle.a;
            const float3 ap = point - triangle.a;
            const float d1 = dot(ab, ap);
            const float d2 = dot(ac, ap);
            if (d1 <= 0.f && d2 <= 0.f)
                return triangle.a;

            const float3 bp = point - triangle.b;
            const float d3 = dot(ab, bp);
            const float d4 = dot(ac, bp);
            if (d3 >= 0.f && d4 <= d3)
                return triangle.b;

            const float vc = d1 * d4 - d3 * d2;
            if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
            {
                const float v = d1 / (d1 - d3);
                return triangle.a + v * ab;
            }

            const float3 cp = point - triangle.c;
            const float d5 = dot(ab, cp);
            const float d6 = dot(ac, cp);
            if (d6 >= 0.f && d5 <= d6)
                return triangle.c;

            const float vb = d5 * d2 - d1 * d6;
            if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
            {
                const float w = d2 / (d2 - d6);
                return triangle.a + w * ac;
            }

            const float va = d3 * d6 - d5 * d4;
            if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
            {
                const float3 bc = triangle.c - triangle.b;
                const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                return triangle.b + w * bc;
            }

            const float denominator = 1.f / (va + vb + vc);
            const float v = vb * denominator;
            const float w = vc * denominator;
            return triangle.a + ab * v + ac * w;
        }

        bool PointInTriangle(
            float3 point,
            const CameraCollisionWorld::Triangle& triangle,
            float3 normal)
        {
            constexpr float EdgeTolerance = -1e-5f;
            return dot(cross(triangle.b - triangle.a, point - triangle.a), normal) >= EdgeTolerance &&
                dot(cross(triangle.c - triangle.b, point - triangle.b), normal) >= EdgeTolerance &&
                dot(cross(triangle.a - triangle.c, point - triangle.c), normal) >= EdgeTolerance;
        }

        bool FindSmallestQuadraticRoot(
            float a,
            float b,
            float c,
            float maximumTime,
            float& root)
        {
            if (a <= 1e-20f)
                return false;

            const float discriminant = b * b - 4.f * a * c;
            if (discriminant < 0.f)
                return false;

            const float sqrtDiscriminant = std::sqrt(discriminant);
            const float inverseDenominator = 0.5f / a;
            float first = (-b - sqrtDiscriminant) * inverseDenominator;
            float second = (-b + sqrtDiscriminant) * inverseDenominator;
            if (first > second)
                std::swap(first, second);

            if (first >= 0.f && first <= maximumTime)
            {
                root = first;
                return true;
            }
            if (second >= 0.f && second <= maximumTime)
            {
                root = second;
                return true;
            }
            return false;
        }

        bool SegmentIntersectsExpandedBox(
            float3 start,
            float3 movement,
            const box3& bounds,
            float radius,
            float maximumTime)
        {
            float entryTime = 0.f;
            float exitTime = maximumTime;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const float minimum = bounds.m_mins[axis] - radius;
                const float maximum = bounds.m_maxs[axis] + radius;
                if (std::abs(movement[axis]) <= 1e-12f)
                {
                    if (start[axis] < minimum || start[axis] > maximum)
                        return false;
                    continue;
                }

                const float inverseDirection = 1.f / movement[axis];
                float first = (minimum - start[axis]) * inverseDirection;
                float second = (maximum - start[axis]) * inverseDirection;
                if (first > second)
                    std::swap(first, second);

                entryTime = std::max(entryTime, first);
                exitTime = std::min(exitTime, second);
                if (entryTime > exitTime)
                    return false;
            }
            return exitTime >= 0.f && entryTime <= maximumTime;
        }

        void ConsiderSweepNormal(
            float time,
            float3 normal,
            float3 movement,
            float& bestTime,
            float3& bestNormal,
            bool& foundHit)
        {
            const float normalLengthSquared = lengthSquared(normal);
            if (time < 0.f || time > bestTime || normalLengthSquared <= 1e-20f)
                return;

            normal /= std::sqrt(normalLengthSquared);
            if (dot(movement, normal) >= -1e-7f)
                return;

            bestTime = time;
            bestNormal = normal;
            foundHit = true;
        }

        void SweepSphereAgainstTriangle(
            float3 start,
            float3 movement,
            float radius,
            const CameraCollisionWorld::Triangle& triangle,
            float& bestTime,
            float3& bestNormal,
            bool& foundHit)
        {
            const float3 ab = triangle.b - triangle.a;
            const float3 ac = triangle.c - triangle.a;
            const float3 unnormalizedNormal = cross(ab, ac);
            const float normalLengthSquared = lengthSquared(unnormalizedNormal);
            if (normalLengthSquared <= 1e-20f)
                return;

            const float3 triangleNormal = unnormalizedNormal / std::sqrt(normalLengthSquared);
            const float signedDistance = dot(start - triangle.a, triangleNormal);
            const float normalVelocity = dot(movement, triangleNormal);
            if (std::abs(normalVelocity) > 1e-12f)
            {
                const float contactDistance = normalVelocity < 0.f ? radius : -radius;
                const float faceTime = (contactDistance - signedDistance) / normalVelocity;
                if (faceTime >= 0.f && faceTime <= bestTime)
                {
                    const float3 faceNormal = normalVelocity < 0.f
                        ? triangleNormal
                        : -triangleNormal;
                    const float3 sphereCenter = start + movement * faceTime;
                    const float3 contactPoint = sphereCenter - faceNormal * radius;
                    if (PointInTriangle(contactPoint, triangle, triangleNormal))
                    {
                        ConsiderSweepNormal(
                            faceTime, faceNormal, movement,
                            bestTime, bestNormal, foundHit);
                    }
                }
            }

            const float movementLengthSquared = lengthSquared(movement);
            const float radiusSquared = radius * radius;
            const std::array<float3, 3> vertices = {
                triangle.a, triangle.b, triangle.c
            };
            for (const float3& vertex : vertices)
            {
                const float3 relativeStart = start - vertex;
                float vertexTime = 0.f;
                if (FindSmallestQuadraticRoot(
                    movementLengthSquared,
                    2.f * dot(relativeStart, movement),
                    lengthSquared(relativeStart) - radiusSquared,
                    bestTime,
                    vertexTime))
                {
                    const float3 centerAtHit = start + movement * vertexTime;
                    ConsiderSweepNormal(
                        vertexTime, centerAtHit - vertex, movement,
                        bestTime, bestNormal, foundHit);
                }
            }

            const std::array<std::pair<float3, float3>, 3> edges = {{
                { triangle.a, triangle.b },
                { triangle.b, triangle.c },
                { triangle.c, triangle.a }
            }};
            for (const auto& [edgeStart, edgeEnd] : edges)
            {
                const float3 edge = edgeEnd - edgeStart;
                const float edgeLengthSquared = lengthSquared(edge);
                if (edgeLengthSquared <= 1e-20f)
                    continue;

                const float3 relativeStart = start - edgeStart;
                const float3 perpendicularStart = relativeStart -
                    edge * (dot(relativeStart, edge) / edgeLengthSquared);
                const float3 perpendicularMovement = movement -
                    edge * (dot(movement, edge) / edgeLengthSquared);

                float edgeTime = 0.f;
                if (!FindSmallestQuadraticRoot(
                    lengthSquared(perpendicularMovement),
                    2.f * dot(perpendicularStart, perpendicularMovement),
                    lengthSquared(perpendicularStart) - radiusSquared,
                    bestTime,
                    edgeTime))
                {
                    continue;
                }

                const float3 centerAtHit = start + movement * edgeTime;
                const float edgeParameter = dot(centerAtHit - edgeStart, edge) /
                    edgeLengthSquared;
                if (edgeParameter <= 0.f || edgeParameter >= 1.f)
                    continue;

                const float3 closestOnEdge = edgeStart + edge * edgeParameter;
                ConsiderSweepNormal(
                    edgeTime, centerAtHit - closestOnEdge, movement,
                    bestTime, bestNormal, foundHit);
            }
        }
    }

    void CameraCollisionWorld::Clear()
    {
        m_Triangles.clear();
        m_Nodes.clear();
    }

    void CameraCollisionWorld::Build(std::vector<Triangle> triangles)
    {
        Clear();

        triangles.erase(
            std::remove_if(triangles.begin(), triangles.end(),
                [](const Triangle& triangle) { return !IsUsableTriangle(triangle); }),
            triangles.end());

        if (triangles.empty())
            return;

        m_Triangles = std::move(triangles);
        // Leaves hold up to eight triangles, so a balanced binary tree needs
        // far fewer than two nodes per triangle. Reserving that pessimistic
        // amount would waste roughly 200 MB for BistroExterior alone.
        m_Nodes.reserve(m_Triangles.size() / 2 + 1);
        BuildNode(0, uint32_t(m_Triangles.size()));
    }

    uint32_t CameraCollisionWorld::BuildNode(uint32_t firstTriangle, uint32_t triangleCount)
    {
        const uint32_t nodeIndex = uint32_t(m_Nodes.size());
        m_Nodes.emplace_back();

        box3 bounds = box3::empty();
        box3 centroidBounds = box3::empty();
        for (uint32_t index = firstTriangle; index < firstTriangle + triangleCount; ++index)
        {
            bounds |= GetTriangleBounds(m_Triangles[index]);
            centroidBounds |= GetTriangleCentroid(m_Triangles[index]);
        }

        m_Nodes[nodeIndex].bounds = bounds;
        m_Nodes[nodeIndex].firstTriangle = firstTriangle;
        m_Nodes[nodeIndex].triangleCount = triangleCount;

        if (triangleCount <= MaxTrianglesPerLeaf)
            return nodeIndex;

        const float3 centroidExtent = centroidBounds.diagonal();
        uint32_t splitAxis = 0;
        if (centroidExtent.y > centroidExtent.x)
            splitAxis = 1;
        if (centroidExtent.z > centroidExtent[splitAxis])
            splitAxis = 2;

        // If all centroids are coincident, subdivision would only manufacture
        // a deep tree with no spatial rejection benefit.
        if (centroidExtent[splitAxis] <= 1e-6f)
            return nodeIndex;

        const uint32_t leftCount = triangleCount / 2;
        const uint32_t middle = firstTriangle + leftCount;
        std::nth_element(
            m_Triangles.begin() + firstTriangle,
            m_Triangles.begin() + middle,
            m_Triangles.begin() + firstTriangle + triangleCount,
            [splitAxis](const Triangle& left, const Triangle& right)
            {
                return GetTriangleCentroid(left)[splitAxis] < GetTriangleCentroid(right)[splitAxis];
            });

        const uint32_t leftChild = BuildNode(firstTriangle, leftCount);
        const uint32_t rightChild = BuildNode(middle, triangleCount - leftCount);

        // Re-fetch by index after recursion because m_Nodes may have reallocated.
        m_Nodes[nodeIndex].triangleCount = 0;
        m_Nodes[nodeIndex].leftChild = leftChild;
        m_Nodes[nodeIndex].rightChild = rightChild;
        return nodeIndex;
    }

    float3 CameraCollisionWorld::ResolvePenetration(
        float3 center,
        float3 movement,
        float radius) const
    {
        const float radiusSquared = radius * radius;
        const float separationSkin = std::max(radius * 1e-4f, 1e-5f);

        // Resolving only the deepest overlap per iteration is independent of
        // mesh traversal order and converges reliably at wall/floor corners.
        for (uint32_t iteration = 0; iteration < MaxPenetrationIterations; ++iteration)
        {
            float deepestPenetration = 0.f;
            float3 deepestNormal = 0.f;

            // Median subdivision keeps the tree shallow (about 20 levels for
            // the largest bundled GLB), so a fixed traversal stack avoids two
            // small heap allocations on every stationary camera frame.
            std::array<uint32_t, 64> stack{};
            size_t stackSize = 0;
            stack[stackSize++] = 0;

            const box3 sphereBounds(center - float3(radius), center + float3(radius));
            while (stackSize > 0)
            {
                const uint32_t nodeIndex = stack[--stackSize];

                const BvhNode& node = m_Nodes[nodeIndex];
                if (!node.bounds.intersects(sphereBounds))
                    continue;

                if (node.triangleCount == 0)
                {
                    stack[stackSize++] = node.leftChild;
                    stack[stackSize++] = node.rightChild;
                    continue;
                }

                for (uint32_t offset = 0; offset < node.triangleCount; ++offset)
                {
                    const Triangle& triangle = m_Triangles[node.firstTriangle + offset];
                    const box3 triangleBounds = GetTriangleBounds(triangle);
                    if (lengthSquared(center - triangleBounds.clamp(center)) >= radiusSquared)
                        continue;

                    const float3 closest = ClosestPointOnTriangle(center, triangle);
                    const float3 separation = center - closest;
                    const float distanceSquaredToTriangle = lengthSquared(separation);
                    if (distanceSquaredToTriangle >= radiusSquared)
                        continue;

                    const float distanceToTriangle = std::sqrt(std::max(distanceSquaredToTriangle, 0.f));
                    const float penetration = radius - distanceToTriangle;
                    if (penetration <= deepestPenetration)
                        continue;

                    float3 normal;
                    if (distanceToTriangle > 1e-6f)
                    {
                        normal = separation / distanceToTriangle;
                    }
                    else
                    {
                        normal = normalize(cross(triangle.b - triangle.a, triangle.c - triangle.a));
                        // Triangle winding is not a collision-side contract for
                        // imported GLBs. At exact contact, separate opposite the
                        // attempted motion so two-sided geometry behaves safely.
                        if (dot(normal, movement) > 0.f)
                            normal = -normal;
                    }

                    deepestPenetration = penetration;
                    deepestNormal = normal;
                }
            }

            if (deepestPenetration <= 0.f)
                break;

            center += deepestNormal * (deepestPenetration + separationSkin);
        }

        return center;
    }

    CameraCollisionWorld::SweepHit CameraCollisionWorld::FindEarliestHit(
        float3 start,
        float3 movement,
        float radius) const
    {
        SweepHit result;
        if (m_Nodes.empty() || lengthSquared(movement) <= 1e-20f)
            return result;

        std::array<uint32_t, 64> stack{};
        size_t stackSize = 0;
        stack[stackSize++] = 0;
        while (stackSize > 0)
        {
            const uint32_t nodeIndex = stack[--stackSize];
            const BvhNode& node = m_Nodes[nodeIndex];
            if (!SegmentIntersectsExpandedBox(
                start, movement, node.bounds, radius, result.time))
            {
                continue;
            }

            if (node.triangleCount == 0)
            {
                stack[stackSize++] = node.leftChild;
                stack[stackSize++] = node.rightChild;
                continue;
            }

            for (uint32_t offset = 0; offset < node.triangleCount; ++offset)
            {
                SweepSphereAgainstTriangle(
                    start,
                    movement,
                    radius,
                    m_Triangles[node.firstTriangle + offset],
                    result.time,
                    result.normal,
                    result.hit);
            }
        }

        return result;
    }

    float3 CameraCollisionWorld::MoveSphere(
        float3 start,
        float3 desiredPosition,
        float radius) const
    {
        if (m_Triangles.empty() || radius <= 0.f ||
            !all(dm::isfinite(start)) || !all(dm::isfinite(desiredPosition)))
        {
            return desiredPosition;
        }

        float3 remainingMovement = desiredPosition - start;
        if (lengthSquared(remainingMovement) <= 1e-12f)
            return start;

        // Repair an authored or mode-copied starting point once movement
        // resumes. A truly stationary camera returns above without touching
        // the BVH, keeping the idle-frame collision cost at zero.
        float3 position = ResolvePenetration(start, remainingMovement, radius);
        const float separationSkin = std::max(radius * 1e-3f, 1e-5f);

        for (uint32_t iteration = 0; iteration < MaxSlideIterations; ++iteration)
        {
            const float movementLength = length(remainingMovement);
            if (movementLength <= 1e-6f)
                break;

            const SweepHit hit = FindEarliestHit(position, remainingMovement, radius);
            if (!hit.hit)
            {
                position += remainingMovement;
                break;
            }

            const float skinFraction = separationSkin / movementLength;
            const float travelFraction = std::max(0.f, hit.time - skinFraction);
            position += remainingMovement * travelFraction;

            remainingMovement *= 1.f - hit.time;
            const float movementIntoSurface = dot(remainingMovement, hit.normal);
            if (movementIntoSurface < 0.f)
                remainingMovement -= hit.normal * movementIntoSurface;
        }

        return ResolvePenetration(position, remainingMovement, radius);
    }
}
