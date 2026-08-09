#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <donut/core/math/math.h>

namespace uvsr
{
    // A compact, read-only collision representation for the loaded scene. UVSR
    // builds it while Donut's imported positions and indices are still resident
    // on the CPU, then Donut is free to release those staging arrays normally.
    // Keeping the collision world independent of renderer resources also makes
    // camera movement deterministic and cheap to unit test.
    class CameraCollisionWorld
    {
    public:
        struct Triangle
        {
            donut::math::float3 a;
            donut::math::float3 b;
            donut::math::float3 c;
        };

        void Clear();
        void Build(std::vector<Triangle> triangles);

        [[nodiscard]] bool Empty() const { return m_Triangles.empty(); }
        [[nodiscard]] size_t GetTriangleCount() const { return m_Triangles.size(); }

        // Matches the clearance intentionally retained by MoveSphere before
        // first contact. Mounted-object recovery uses the same tolerance when
        // deciding whether a validated target can be snapped exactly.
        [[nodiscard]] static float GetSphereSeparationSkin(float radius);

        // Moves a sphere from start to desiredPosition with a continuous sweep.
        // The query cost is bounded by a small number of BVH traversals and is
        // independent of travel distance, which keeps long third-person orbit
        // motions cheap. Tangential motion is preserved at contact so the
        // camera slides along walls instead of stopping completely.
        [[nodiscard]] donut::math::float3 MoveSphere(
            donut::math::float3 start,
            donut::math::float3 desiredPosition,
            float radius) const;

        // Returns the unobstructed fraction of the direct start-to-target
        // sweep, stopping just before first contact without wall sliding.
        // Empty or invalid queries fail open so collision cannot manufacture
        // a shortened mount offset from unusable scene data.
        [[nodiscard]] float GetSphereTravelFraction(
            donut::math::float3 start,
            donut::math::float3 desiredPosition,
            float radius) const;

        // Places a stationary sphere outside authored geometry. This is kept
        // separate from MoveSphere so the camera's zero-cost idle path remains
        // unchanged while mounted objects can repair a newly enlarged hitbox.
        [[nodiscard]] donut::math::float3 ResolveSphere(
            donut::math::float3 center,
            donut::math::float3 movementHint,
            float radius) const;

    private:
        struct BvhNode
        {
            donut::math::box3 bounds = donut::math::box3::empty();
            uint32_t firstTriangle = 0;
            uint32_t triangleCount = 0;
            uint32_t leftChild = 0;
            uint32_t rightChild = 0;
        };

        struct SweepHit
        {
            float time = 1.f;
            donut::math::float3 normal = 0.f;
            bool hit = false;
        };

        uint32_t BuildNode(uint32_t firstTriangle, uint32_t triangleCount);
        [[nodiscard]] SweepHit FindEarliestHit(
            donut::math::float3 start,
            donut::math::float3 movement,
            float radius) const;
        [[nodiscard]] donut::math::float3 ResolvePenetration(
            donut::math::float3 center,
            donut::math::float3 movement,
            float radius) const;

        std::vector<Triangle> m_Triangles;
        std::vector<BvhNode> m_Nodes;
    };
}
