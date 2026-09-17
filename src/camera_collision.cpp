#include "camera_collision.h"
#include "checked_worklist.h"
#include "renderer_scene.h"
#include <float.h>

#include <algorithm>
#include <cmath>
#include <new>
#include <type_traits>
#include <string.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error camera collision requires exception-disabled compilation
#endif

namespace uvsr
{
    namespace
    {
        using Point = CameraCollisionWorld::Point;
        static_assert(sizeof(Point) == 12 && std::is_trivially_copyable_v<Point>);
        static_assert(sizeof(CameraCollisionWorld::Triangle) == 36);
        // component division and second-argument min/max ties preserve the
        // retained contact math and fixed-toolset BVH bytes.
        Point operator+(Point a, Point b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
        Point operator-(Point a, Point b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
        Point operator-(Point a) noexcept { return {-a.x, -a.y, -a.z}; }
        Point operator*(Point a, float b) noexcept { return {a.x * b, a.y * b, a.z * b}; }
        Point operator*(float a, Point b) noexcept { return {a * b.x, a * b.y, a * b.z}; }
        Point operator/(Point a, float b) noexcept { return {a.x / b, a.y / b, a.z / b}; }
        Point& operator+=(Point& a, Point b) noexcept { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
        Point& operator-=(Point& a, Point b) noexcept { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
        Point& operator*=(Point& a, float b) noexcept { a.x *= b; a.y *= b; a.z *= b; return a; }
        Point& operator/=(Point& a, float b) noexcept { a.x /= b; a.y /= b; a.z /= b; return a; }
        float Coordinate(Point value, uint32_t axis) noexcept { return axis == 0 ? value.x : axis == 1 ? value.y : value.z; }
        float Dot(Point a, Point b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
        float LengthSquared(Point value) noexcept { return Dot(value, value); }
        float Length(Point value) noexcept { return std::sqrt(LengthSquared(value)); }
        Point Normalize(Point value) noexcept { return value / Length(value); }
        Point Cross(Point a, Point b) noexcept
        { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
        bool IsFinite(Point value) noexcept
        { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
        Point Minimum(Point a, Point b) noexcept
        { return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z}; }
        Point Maximum(Point a, Point b) noexcept
        { return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z}; }

        struct Bounds
        {
            Point minimum{FLT_MAX, FLT_MAX, FLT_MAX};
            Point maximum{-FLT_MAX, -FLT_MAX, -FLT_MAX};

            Bounds() noexcept = default;
            Bounds(Point low, Point high) noexcept : minimum(low), maximum(high) {}
            void Include(Point point) noexcept
            { minimum = Minimum(minimum, point); maximum = Maximum(maximum, point); }
            void Include(const Bounds& bounds) noexcept
            { minimum = Minimum(minimum, bounds.minimum); maximum = Maximum(maximum, bounds.maximum); }
            Point Extent() const noexcept { return maximum - minimum; }
            Point Clamp(Point point) const noexcept { return Minimum(Maximum(point, minimum), maximum); }
            bool Intersects(const Bounds& other) const noexcept
            {
                return other.minimum.x <= maximum.x && other.minimum.y <= maximum.y && other.minimum.z <= maximum.z &&
                    minimum.x <= other.maximum.x && minimum.y <= other.maximum.y && minimum.z <= other.maximum.z;
            }
        };

#if defined(UVSR_BUILD_TESTING)
        thread_local uint32_t allocationFailure = 0;
        thread_local uint32_t allocationOrdinal = 0;
#endif
        bool CanAllocate() noexcept
        {
#if defined(UVSR_BUILD_TESTING)
            return allocationFailure == 0 || ++allocationOrdinal != allocationFailure;
#else
            return true;
#endif
        }

        template<class T> bool ValidInput(ArrayView<const T> input) noexcept
        {
            return input.IsValid() && input.count <= size_t(PTRDIFF_MAX) / sizeof(T);
        }

        // the pinned MSVC nth_element is iterative and in-place. plain triangles
        // and a nonthrowing comparator keep that path free of allocation/throws.
        // re-audit its partition and insertion sort when changing the toolset.
        static_assert(std::is_trivially_copyable_v<CameraCollisionWorld::Triangle> &&
            std::is_nothrow_move_constructible_v<CameraCollisionWorld::Triangle> &&
            std::is_nothrow_move_assignable_v<CameraCollisionWorld::Triangle> &&
            std::is_nothrow_swappable_v<CameraCollisionWorld::Triangle>);

        constexpr uint32_t MaxTrianglesPerLeaf = 8;
        constexpr uint32_t MaxPenetrationIterations = 8;
        constexpr uint32_t MaxSlideIterations = 4;

        constexpr size_t MaxBuildTasks()
        {
            size_t pending = 1;
            for (uint32_t count = UINT32_MAX; count > MaxTrianglesPerLeaf; count /= 2)
                ++pending;
            return pending;
        }
        static_assert(MaxBuildTasks() == 30);

        Bounds GetTriangleBounds(const CameraCollisionWorld::Triangle& triangle)
        {
            Bounds bounds = Bounds{};
            bounds.Include(triangle.a);
            bounds.Include(triangle.b);
            bounds.Include(triangle.c);
            return bounds;
        }

        Point GetTriangleCentroid(const CameraCollisionWorld::Triangle& triangle) noexcept
        {
            return (triangle.a + triangle.b + triangle.c) / 3.f;
        }

        bool IsUsableTriangle(const CameraCollisionWorld::Triangle& triangle)
        {
            if (!IsFinite(triangle.a) ||
                !IsFinite(triangle.b) ||
                !IsFinite(triangle.c))
            {
                return false;
            }

            // Degenerate importer output cannot provide a stable separation
            // direction and should not become an invisible collision spike.
            return LengthSquared(Cross(triangle.b - triangle.a, triangle.c - triangle.a)) > 1e-20f;
        }

        Point ClosestPointOnTriangle(
            Point point,
            const CameraCollisionWorld::Triangle& triangle)
        {
            // Voronoi-region test from Real-Time Collision Detection. Unlike a
            // plane-only test, this handles triangle edges and vertices, which
            // is what gives the camera sphere a continuous rounded hitbox at
            // mesh seams and corners.
            const Point ab = triangle.b - triangle.a;
            const Point ac = triangle.c - triangle.a;
            const Point ap = point - triangle.a;
            const float d1 = Dot(ab, ap);
            const float d2 = Dot(ac, ap);
            if (d1 <= 0.f && d2 <= 0.f)
                return triangle.a;

            const Point bp = point - triangle.b;
            const float d3 = Dot(ab, bp);
            const float d4 = Dot(ac, bp);
            if (d3 >= 0.f && d4 <= d3)
                return triangle.b;

            const float vc = d1 * d4 - d3 * d2;
            if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
            {
                const float v = d1 / (d1 - d3);
                return triangle.a + v * ab;
            }

            const Point cp = point - triangle.c;
            const float d5 = Dot(ab, cp);
            const float d6 = Dot(ac, cp);
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
                const Point bc = triangle.c - triangle.b;
                const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                return triangle.b + w * bc;
            }

            const float denominator = 1.f / (va + vb + vc);
            const float v = vb * denominator;
            const float w = vc * denominator;
            return triangle.a + ab * v + ac * w;
        }

        bool PointInTriangle(
            Point point,
            const CameraCollisionWorld::Triangle& triangle,
            Point normal)
        {
            constexpr float EdgeTolerance = -1e-5f;
            return Dot(Cross(triangle.b - triangle.a, point - triangle.a), normal) >= EdgeTolerance &&
                Dot(Cross(triangle.c - triangle.b, point - triangle.b), normal) >= EdgeTolerance &&
                Dot(Cross(triangle.a - triangle.c, point - triangle.c), normal) >= EdgeTolerance;
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
            Point start,
            Point movement,
            const Bounds& bounds,
            float radius,
            float maximumTime)
        {
            float entryTime = 0.f;
            float exitTime = maximumTime;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const float minimum = Coordinate(bounds.minimum, axis) - radius;
                const float maximum = Coordinate(bounds.maximum, axis) + radius;
                if (std::abs(Coordinate(movement, axis)) <= 1e-12f)
                {
                    if (Coordinate(start, axis) < minimum || Coordinate(start, axis) > maximum)
                        return false;
                    continue;
                }

                const float inverseDirection = 1.f / Coordinate(movement, axis);
                float first = (minimum - Coordinate(start, axis)) * inverseDirection;
                float second = (maximum - Coordinate(start, axis)) * inverseDirection;
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
            Point normal,
            Point movement,
            float& bestTime,
            Point& bestNormal,
            bool& foundHit)
        {
            const float normalLengthSquared = LengthSquared(normal);
            if (time < 0.f || time > bestTime || normalLengthSquared <= 1e-20f)
                return;

            normal /= std::sqrt(normalLengthSquared);
            if (Dot(movement, normal) >= -1e-7f)
                return;

            bestTime = time;
            bestNormal = normal;
            foundHit = true;
        }

        void SweepSphereAgainstTriangle(
            Point start,
            Point movement,
            float radius,
            const CameraCollisionWorld::Triangle& triangle,
            float& bestTime,
            Point& bestNormal,
            bool& foundHit)
        {
            const Point ab = triangle.b - triangle.a;
            const Point ac = triangle.c - triangle.a;
            const Point unnormalizedNormal = Cross(ab, ac);
            const float normalLengthSquared = LengthSquared(unnormalizedNormal);
            if (normalLengthSquared <= 1e-20f)
                return;

            const Point triangleNormal = unnormalizedNormal / std::sqrt(normalLengthSquared);
            const float signedDistance = Dot(start - triangle.a, triangleNormal);
            const float normalVelocity = Dot(movement, triangleNormal);
            if (std::abs(normalVelocity) > 1e-12f)
            {
                const float contactDistance = normalVelocity < 0.f ? radius : -radius;
                const float faceTime = (contactDistance - signedDistance) / normalVelocity;
                if (faceTime >= 0.f && faceTime <= bestTime)
                {
                    const Point faceNormal = normalVelocity < 0.f
                        ? triangleNormal
                        : -triangleNormal;
                    const Point sphereCenter = start + movement * faceTime;
                    const Point contactPoint = sphereCenter - faceNormal * radius;
                    if (PointInTriangle(contactPoint, triangle, triangleNormal))
                    {
                        ConsiderSweepNormal(
                            faceTime, faceNormal, movement,
                            bestTime, bestNormal, foundHit);
                    }
                }
            }

            const float movementLengthSquared = LengthSquared(movement);
            const float radiusSquared = radius * radius;
            const Point vertices[3] = {
                triangle.a, triangle.b, triangle.c
            };
            for (const Point& vertex : vertices)
            {
                const Point relativeStart = start - vertex;
                float vertexTime = 0.f;
                if (FindSmallestQuadraticRoot(
                    movementLengthSquared,
                    2.f * Dot(relativeStart, movement),
                    LengthSquared(relativeStart) - radiusSquared,
                    bestTime,
                    vertexTime))
                {
                    const Point centerAtHit = start + movement * vertexTime;
                    ConsiderSweepNormal(
                        vertexTime, centerAtHit - vertex, movement,
                        bestTime, bestNormal, foundHit);
                }
            }

            struct Edge { Point start; Point end; };
            const Edge edges[3] = {
                { triangle.a, triangle.b },
                { triangle.b, triangle.c },
                { triangle.c, triangle.a }
            };
            for (const auto& [edgeStart, edgeEnd] : edges)
            {
                const Point edge = edgeEnd - edgeStart;
                const float edgeLengthSquared = LengthSquared(edge);
                if (edgeLengthSquared <= 1e-20f)
                    continue;

                const Point relativeStart = start - edgeStart;
                const Point perpendicularStart = relativeStart -
                    edge * (Dot(relativeStart, edge) / edgeLengthSquared);
                const Point perpendicularMovement = movement -
                    edge * (Dot(movement, edge) / edgeLengthSquared);

                float edgeTime = 0.f;
                if (!FindSmallestQuadraticRoot(
                    LengthSquared(perpendicularMovement),
                    2.f * Dot(perpendicularStart, perpendicularMovement),
                    LengthSquared(perpendicularStart) - radiusSquared,
                    bestTime,
                    edgeTime))
                {
                    continue;
                }

                const Point centerAtHit = start + movement * edgeTime;
                const float edgeParameter = Dot(centerAtHit - edgeStart, edge) /
                    edgeLengthSquared;
                if (edgeParameter <= 0.f || edgeParameter >= 1.f)
                    continue;

                const Point closestOnEdge = edgeStart + edge * edgeParameter;
                ConsiderSweepNormal(
                    edgeTime, centerAtHit - closestOnEdge, movement,
                    bestTime, bestNormal, foundHit);
            }
        }
    }

#if defined(UVSR_BUILD_TESTING)
    void SetCameraCollisionAllocationFailure(uint32_t ordinal) noexcept
    {
        allocationFailure = ordinal;
        allocationOrdinal = 0;
    }
#endif

    CameraCollisionWorld::~CameraCollisionWorld()
    {
        delete[] m_Triangles;
        delete[] m_Nodes;
    }

    CameraCollisionWorld::CameraCollisionWorld(CameraCollisionWorld&& other) noexcept
    {
        *this = static_cast<CameraCollisionWorld&&>(other);
    }

    CameraCollisionWorld& CameraCollisionWorld::operator=(CameraCollisionWorld&& other) noexcept
    {
        if (this == &other) return *this;
        delete[] m_Triangles;
        delete[] m_Nodes;
        m_Triangles = other.m_Triangles;
        m_Nodes = other.m_Nodes;
        m_TriangleCount = other.m_TriangleCount;
        m_TriangleCapacity = other.m_TriangleCapacity;
        m_NodeCount = other.m_NodeCount;
        m_NodeCapacity = other.m_NodeCapacity;
        other.m_Triangles = nullptr;
        other.m_Nodes = nullptr;
        other.m_TriangleCapacity = other.m_NodeCapacity = 0;
        other.Clear();
        return *this;
    }

    void CameraCollisionWorld::Clear() noexcept
    {
        m_TriangleCount = m_NodeCount = 0;
    }

    CameraCollisionBuildError CameraCollisionWorld::AllocateTriangles(size_t count) noexcept
    {
        if (count > UINT32_MAX || count > size_t(PTRDIFF_MAX) / sizeof(Triangle))
            return CameraCollisionBuildError::CapacityExceeded;
        if (count)
        {
            if (!CanAllocate() || !(m_Triangles = new (std::nothrow) Triangle[count]))
                return CameraCollisionBuildError::AllocationFailed;
        }
        m_TriangleCapacity = count;
        return CameraCollisionBuildError::None;
    }

    CameraCollisionBuildError CameraCollisionWorld::FinishBuild() noexcept
    {
        static_assert(sizeof(BvhNode) == 40 && std::is_trivially_copyable_v<BvhNode>);
        if (!m_TriangleCount) return CameraCollisionBuildError::None;
        // median leaves have at least four triangles: 2L-1 fits this bound.
        m_NodeCapacity = m_TriangleCount / 2 + 1;
        if (m_NodeCapacity > size_t(PTRDIFF_MAX) / sizeof(BvhNode))
            return CameraCollisionBuildError::CapacityExceeded;
        if (!CanAllocate() || !(m_Nodes = new (std::nothrow) BvhNode[m_NodeCapacity]))
            return CameraCollisionBuildError::AllocationFailed;
        return BuildNodes(m_NodeCapacity);
    }

    CameraCollisionBuildError CameraCollisionWorld::Build(ArrayView<const Triangle> triangles) noexcept
    {
        if (!ValidInput(triangles)) return CameraCollisionBuildError::InvalidSource;
        CameraCollisionWorld candidate;
        auto result = candidate.AllocateTriangles(triangles.count);
        if (result != CameraCollisionBuildError::None) return result;
        for (size_t index = 0; index < triangles.count; ++index)
            if (IsUsableTriangle(triangles.data[index]))
                candidate.m_Triangles[candidate.m_TriangleCount++] = triangles.data[index];
        result = candidate.FinishBuild();
        if (result == CameraCollisionBuildError::None)
            *this = static_cast<CameraCollisionWorld&&>(candidate);
        return result;
    }

    CameraCollisionBuildError CameraCollisionWorld::BuildFromScene(const RendererSceneView& scene,
        ArrayView<const CameraCollisionSourceBuffers> buffers,
        ArrayView<const uint32_t> instanceOrder) noexcept
    {
        if (!scene.generation || !ValidInput(scene.nodes) || !ValidInput(scene.meshes) ||
            !ValidInput(scene.instances) || !ValidInput(scene.geometries) || !ValidInput(scene.bufferGroups) ||
            !ValidInput(buffers) || buffers.count != scene.bufferGroups.count ||
            !ValidInput(instanceOrder) || (instanceOrder.count && instanceOrder.count != scene.instances.count))
            return CameraCollisionBuildError::InvalidSource;
        if (scene.instances.count > UINT32_MAX || scene.meshes.count > UINT32_MAX ||
            scene.nodes.count > UINT32_MAX || scene.geometries.count > UINT32_MAX)
            return CameraCollisionBuildError::CapacityExceeded;
        for (size_t index = 0; index < buffers.count; ++index)
        {
            const auto& source = buffers.data[index];
            if (!ValidInput(source.indices) || !ValidInput(source.positions) ||
                source.indices.count % sizeof(uint32_t))
                return CameraCollisionBuildError::InvalidSource;
        }
        if (instanceOrder.count)
        {
            struct Seen { unsigned char* data = nullptr; ~Seen() { delete[] data; } } seen;
            if (!CanAllocate() || !(seen.data = new (std::nothrow) unsigned char[instanceOrder.count]{}))
                return CameraCollisionBuildError::AllocationFailed;
            for (size_t index = 0; index < instanceOrder.count; ++index)
            {
                const uint32_t value = instanceOrder.data[index];
                if (value >= instanceOrder.count || seen.data[value])
                    return CameraCollisionBuildError::InvalidSource;
                seen.data[value] = 1;
            }
        }

        CameraCollisionWorld candidate;
        size_t triangleCapacity = 0;
        // first validate and size, then fill one candidate allocation. no importer
        // graph or second triangle array participates in the collision owner.
        for (unsigned pass = 0; pass < 2; ++pass)
        {
            for (size_t order = 0; order < scene.instances.count; ++order)
            {
                const uint32_t instanceIndex = instanceOrder.count ? instanceOrder.data[order] : uint32_t(order);
                const auto& instance = scene.instances.data[instanceIndex];
                if (instance.meshIndex >= scene.meshes.count || instance.nodeIndex >= scene.nodes.count)
                    return CameraCollisionBuildError::InvalidSource;
                const auto& posedMesh = scene.meshes.data[instance.meshIndex];
                const uint32_t meshIndex = posedMesh.skinPrototypeIndex == InvalidSceneIndex
                    ? instance.meshIndex : posedMesh.skinPrototypeIndex;
                if (meshIndex >= scene.meshes.count) return CameraCollisionBuildError::InvalidSource;
                const auto& mesh = scene.meshes.data[meshIndex];
                if (mesh.bufferGroupIndex >= buffers.count || mesh.geometries.first > scene.geometries.count ||
                    mesh.geometries.count > scene.geometries.count - mesh.geometries.first)
                    return CameraCollisionBuildError::InvalidSource;
                const auto& source = buffers.data[mesh.bufferGroupIndex];
                if (!source.indices.count || !source.positions.count) continue;
                const size_t indexCount = source.indices.count / sizeof(uint32_t);
                const size_t positionCount = source.positions.count / sizeof(Point);
                const auto& world = scene.nodes.data[instance.nodeIndex].world;
                float linear[9];
                float translation[3];
                for (unsigned row = 0; row < 3; ++row)
                    for (unsigned column = 0; column < 3; ++column)
                    {
                        const double value = world.linear[row * 3 + column];
                        if (!std::isfinite(value) || value > FLT_MAX || value < -FLT_MAX)
                            return CameraCollisionBuildError::InvalidSource;
                        linear[row * 3 + column] = float(value);
                    }
                for (unsigned lane = 0; lane < 3; ++lane)
                {
                    const double value = world.translation[lane];
                    if (!std::isfinite(value) || value > FLT_MAX || value < -FLT_MAX)
                        return CameraCollisionBuildError::InvalidSource;
                    translation[lane] = float(value);
                }
                for (uint32_t geometryIndex = 0; geometryIndex < mesh.geometries.count; ++geometryIndex)
                {
                    const auto& geometry = scene.geometries.data[mesh.geometries.first + geometryIndex];
                    if (geometry.primitive != RendererScenePrimitive::Triangles) continue;
                    // sealed geometry and retained source extents must agree.
                    // reject a broken range transaction; individual bad index
                    // values below retain the previous omission policy.
                    if (geometry.vertexOffsetInMesh > mesh.vertexCount ||
                        geometry.vertexCount > mesh.vertexCount - geometry.vertexOffsetInMesh ||
                        mesh.indexOffset > indexCount ||
                        geometry.indexOffsetInMesh > indexCount - mesh.indexOffset ||
                        mesh.vertexOffset > positionCount || geometry.vertexOffsetInMesh > positionCount - mesh.vertexOffset)
                        return CameraCollisionBuildError::InvalidSource;
                    const size_t firstIndex = size_t(mesh.indexOffset) + geometry.indexOffsetInMesh;
                    const size_t firstVertex = size_t(mesh.vertexOffset) + geometry.vertexOffsetInMesh;
                    if (geometry.indexCount > indexCount - firstIndex ||
                        geometry.vertexCount > positionCount - firstVertex)
                        return CameraCollisionBuildError::InvalidSource;
                    if (pass == 0)
                    {
                        const size_t count = geometry.indexCount / 3;
                        if (count > UINT32_MAX - triangleCapacity)
                            return CameraCollisionBuildError::CapacityExceeded;
                        triangleCapacity += count;
                        continue;
                    }
                    for (size_t offset = 0; geometry.indexCount - offset >= 3; offset += 3)
                    {
                        Point points[3];
                        bool valid = true;
                        for (unsigned corner = 0; corner < 3; ++corner)
                        {
                            uint32_t vertex;
                            memcpy(&vertex, source.indices.data + (firstIndex + offset + corner) * sizeof(vertex), sizeof(vertex));
                            // GPU-aligned position spans can include a whole
                            // phantom point. indices belong to this geometry.
                            if (vertex >= geometry.vertexCount) { valid = false; break; }
                            Point point;
                            memcpy(&point, source.positions.data + (firstVertex + vertex) * sizeof(Point), sizeof(Point));
                            points[corner] = {
                                point.x * linear[0] + point.y * linear[3] + point.z * linear[6] + translation[0],
                                point.x * linear[1] + point.y * linear[4] + point.z * linear[7] + translation[1],
                                point.x * linear[2] + point.y * linear[5] + point.z * linear[8] + translation[2]};
                        }
                        if (!valid) continue; // preserve omission of malformed individual triangles.
                        const Triangle triangle{points[0], points[1], points[2]};
                        if (!IsUsableTriangle(triangle)) continue;
                        if (candidate.m_TriangleCount >= candidate.m_TriangleCapacity)
                            return CameraCollisionBuildError::CapacityExceeded;
                        candidate.m_Triangles[candidate.m_TriangleCount++] = triangle;
                    }
                }
            }
            if (pass == 0)
            {
                const auto result = candidate.AllocateTriangles(triangleCapacity);
                if (result != CameraCollisionBuildError::None) return result;
            }
        }
        const auto result = candidate.FinishBuild();
        if (result == CameraCollisionBuildError::None)
            *this = static_cast<CameraCollisionWorld&&>(candidate);
        return result;
    }

    CameraCollisionBuildError CameraCollisionWorld::BuildNodes(size_t nodeCapacity) noexcept
    {
        struct Task
        {
            uint32_t firstTriangle;
            uint32_t triangleCount;
            uint32_t parent;
            bool rightChild;
        };
        // local preparation scratch. at UINT32_MAX triangles the longest left
        // descent expands 29 nodes, leaving at most 30 pending tasks.
        Task storage[MaxBuildTasks()];
        CheckedWorklist<Task> pending{ ArrayView<Task>(storage) };
        if (!pending.TryPush({ 0, uint32_t(m_TriangleCount), UINT32_MAX, false }))
            return CameraCollisionBuildError::CapacityExceeded;
        Task task;
        while (pending.TryPop(task))
        {
            const uint32_t firstTriangle = task.firstTriangle;
            const uint32_t triangleCount = task.triangleCount;
            if (triangleCount == 0 || firstTriangle > m_TriangleCount ||
                triangleCount > m_TriangleCount - firstTriangle ||
                (task.parent != UINT32_MAX && task.parent >= m_NodeCount))
                return CameraCollisionBuildError::InvalidTree;
            if (m_NodeCount >= nodeCapacity || m_NodeCount >= m_NodeCapacity)
                return CameraCollisionBuildError::CapacityExceeded;

            const uint32_t nodeIndex = uint32_t(m_NodeCount);
            m_Nodes[m_NodeCount++] = {};
            if (task.parent != UINT32_MAX)
            {
                if (task.rightChild)
                    m_Nodes[task.parent].rightChild = nodeIndex;
                else
                    m_Nodes[task.parent].leftChild = nodeIndex;
            }
            Bounds bounds = Bounds{};
            Bounds centroidBounds = Bounds{};
            for (uint32_t index = firstTriangle; index < firstTriangle + triangleCount; ++index)
            {
                bounds.Include(GetTriangleBounds(m_Triangles[index]));
                centroidBounds.Include(GetTriangleCentroid(m_Triangles[index]));
            }
            m_Nodes[nodeIndex].minimum = bounds.minimum;
            m_Nodes[nodeIndex].maximum = bounds.maximum;
            m_Nodes[nodeIndex].firstTriangle = firstTriangle;
            m_Nodes[nodeIndex].triangleCount = triangleCount;
            if (triangleCount <= MaxTrianglesPerLeaf)
                continue;

            const Point centroidExtent = centroidBounds.Extent();
            uint32_t splitAxis = 0;
            if (centroidExtent.y > centroidExtent.x)
                splitAxis = 1;
            if (centroidExtent.z > Coordinate(centroidExtent, splitAxis))
                splitAxis = 2;
            if (Coordinate(centroidExtent, splitAxis) <= 1e-6f)
                continue;

            const uint32_t leftCount = triangleCount / 2;
            const uint32_t middle = firstTriangle + leftCount;
            std::nth_element(
                m_Triangles + firstTriangle,
                m_Triangles + middle,
                m_Triangles + firstTriangle + triangleCount,
                [splitAxis](const Triangle& left, const Triangle& right) noexcept
                {
                    return Coordinate(GetTriangleCentroid(left), splitAxis) < Coordinate(GetTriangleCentroid(right), splitAxis);
                });
            m_Nodes[nodeIndex].triangleCount = 0;
            // right first on the stack preserves left-first preorder node IDs,
            // partition call order and the prior tie behavior in the fixed STL.
            if (pending.Remaining() < 2 ||
                !pending.TryPush({ middle, triangleCount - leftCount, nodeIndex, true }) ||
                !pending.TryPush({ firstTriangle, leftCount, nodeIndex, false }))
                return CameraCollisionBuildError::CapacityExceeded;
        }
        return CameraCollisionBuildError::None;
    }

    CameraCollisionWorld::Point CameraCollisionWorld::ResolvePenetration(
        Point sourceCenter, Point sourceMovement, float radius) const
    {
        Point center = sourceCenter;
        const Point movement = sourceMovement;
        const float radiusSquared = radius * radius;
        const float separationSkin = std::max(radius * 1e-4f, 1e-5f);

        // Resolving only the deepest overlap per iteration is independent of
        // mesh traversal order and converges reliably at wall/floor corners.
        for (uint32_t iteration = 0; iteration < MaxPenetrationIterations; ++iteration)
        {
            float deepestPenetration = 0.f;
            Point deepestNormal{};

            // Median subdivision keeps the tree shallow (about 20 levels for
            // the largest bundled GLB), so a fixed traversal stack avoids two
            // small heap allocations on every stationary camera frame.
            uint32_t stack[64]{};
            size_t stackSize = 0;
            stack[stackSize++] = 0;

            const Bounds sphereBounds(center - Point{radius, radius, radius}, center + Point{radius, radius, radius});
            while (stackSize > 0)
            {
                const uint32_t nodeIndex = stack[--stackSize];

                const BvhNode& node = m_Nodes[nodeIndex];
                if (!Bounds(node.minimum, node.maximum).Intersects(sphereBounds))
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
                    const Bounds triangleBounds = GetTriangleBounds(triangle);
                    if (LengthSquared(center - triangleBounds.Clamp(center)) >= radiusSquared)
                        continue;

                    const Point closest = ClosestPointOnTriangle(center, triangle);
                    const Point separation = center - closest;
                    const float distanceSquaredToTriangle = LengthSquared(separation);
                    if (distanceSquaredToTriangle >= radiusSquared)
                        continue;

                    const float distanceToTriangle = std::sqrt(std::max(distanceSquaredToTriangle, 0.f));
                    const float penetration = radius - distanceToTriangle;
                    if (penetration <= deepestPenetration)
                        continue;

                    Point normal;
                    if (distanceToTriangle > 1e-6f)
                    {
                        normal = separation / distanceToTriangle;
                    }
                    else
                    {
                        normal = Normalize(Cross(triangle.b - triangle.a, triangle.c - triangle.a));
                        // Triangle winding is not a collision-side contract for
                        // imported GLBs. At exact contact, separate opposite the
                        // attempted motion so two-sided geometry behaves safely.
                        if (Dot(normal, movement) > 0.f)
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
        Point sourceStart, Point sourceMovement, float radius) const
    {
        const Point start = sourceStart;
        const Point movement = sourceMovement;
        Point normal{};
        SweepHit result;
        if ((m_NodeCount == 0) || LengthSquared(movement) <= 1e-20f)
            return result;

        uint32_t stack[64]{};
        size_t stackSize = 0;
        stack[stackSize++] = 0;
        while (stackSize > 0)
        {
            const uint32_t nodeIndex = stack[--stackSize];
            const BvhNode& node = m_Nodes[nodeIndex];
            if (!SegmentIntersectsExpandedBox(
                start, movement, Bounds(node.minimum, node.maximum), radius, result.time))
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
                    normal,
                    result.hit);
            }
        }

        result.normal = normal;
        return result;
    }

    CameraCollisionWorld::Point CameraCollisionWorld::MoveSphere(
        Point sourceStart, Point sourceDesiredPosition, float radius) const
    {
        const Point start = sourceStart;
        const Point desiredPosition = sourceDesiredPosition;
        if ((m_TriangleCount == 0) || radius <= 0.f ||
            !IsFinite(start) || !IsFinite(desiredPosition))
        {
            return desiredPosition;
        }

        Point remainingMovement = desiredPosition - start;
        if (LengthSquared(remainingMovement) <= 1e-12f)
            return start;

        // Repair an authored or mode-copied starting point once movement
        // resumes. A truly stationary camera returns above without touching
        // the BVH, keeping the idle-frame collision cost at zero.
        Point position = ResolvePenetration(start, remainingMovement, radius);
        const float separationSkin = std::max(radius * 1e-3f, 1e-5f);

        for (uint32_t iteration = 0; iteration < MaxSlideIterations; ++iteration)
        {
            const float movementLength = Length(remainingMovement);
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
            const float movementIntoSurface = Dot(remainingMovement, hit.normal);
            if (movementIntoSurface < 0.f)
                remainingMovement -= hit.normal * movementIntoSurface;
        }

        return ResolvePenetration(position, remainingMovement, radius);
    }

    CameraCollisionWorld::Point CameraCollisionWorld::ResolveSphere(
        Point sourceCenter, Point sourceMovementHint, float radius) const
    {
        const Point center = sourceCenter;
        const Point movementHint = sourceMovementHint;
        if ((m_TriangleCount == 0) || radius <= 0.f ||
            !IsFinite(center) ||
            !IsFinite(movementHint))
        {
            return center;
        }

        return ResolvePenetration(center, movementHint, radius);
    }
}
