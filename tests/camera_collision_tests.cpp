#include "camera_collision.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace donut::math;
using uvsr::CameraCollisionWorld;

namespace
{
    bool NearlyEqual(float actual, float expected, float tolerance = 1e-4f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool Check(bool condition, const char* description)
    {
        if (!condition)
            std::fprintf(stderr, "FAILED: %s\n", description);
        return condition;
    }

    std::vector<CameraCollisionWorld::Triangle> MakeWall()
    {
        std::vector<CameraCollisionWorld::Triangle> triangles;
        constexpr int Subdivisions = 8;
        constexpr float Minimum = -10.f;
        constexpr float CellSize = 20.f / float(Subdivisions);
        for (int y = 0; y < Subdivisions; ++y)
        {
            for (int z = 0; z < Subdivisions; ++z)
            {
                const float y0 = Minimum + float(y) * CellSize;
                const float y1 = y0 + CellSize;
                const float z0 = Minimum + float(z) * CellSize;
                const float z1 = z0 + CellSize;
                triangles.push_back({
                    float3(0.f, y0, z0), float3(0.f, y1, z0), float3(0.f, y1, z1) });
                triangles.push_back({
                    float3(0.f, y0, z0), float3(0.f, y1, z1), float3(0.f, y0, z1) });
            }
        }
        return triangles;
    }
}

int main()
{
    bool passed = true;

    CameraCollisionWorld emptyWorld;
    const float3 unobstructed = emptyWorld.MoveSphere(
        float3(-1.f, 2.f, 3.f), float3(4.f, 5.f, 6.f), 0.25f);
    passed &= Check(all(unobstructed == float3(4.f, 5.f, 6.f)),
        "an empty collision world preserves desired movement");

    CameraCollisionWorld wallWorld;
    wallWorld.Build(MakeWall());
    passed &= Check(wallWorld.GetTriangleCount() == 128,
        "the wall collision world retains every valid triangle");

    const float3 blocked = wallWorld.MoveSphere(
        float3(-1.f, 0.f, 0.f), float3(1.f, 0.f, 0.f), 0.25f);
    passed &= Check(blocked.x <= -0.249f && blocked.x >= -0.252f,
        "a fast sphere cannot tunnel through a thin wall");
    passed &= Check(NearlyEqual(blocked.y, 0.f) && NearlyEqual(blocked.z, 0.f),
        "a head-on wall collision does not add tangential drift");

    const float3 sliding = wallWorld.MoveSphere(
        float3(-1.f, -1.f, 0.f), float3(1.f, 1.f, 0.f), 0.25f);
    passed &= Check(sliding.x <= -0.249f && sliding.x >= -0.252f,
        "wall sliding maintains the camera hitbox radius");
    passed &= Check(sliding.y > 0.9f,
        "wall sliding preserves tangential movement");

    const float3 movingAway = wallWorld.MoveSphere(
        float3(-1.f, 0.f, 0.f), float3(-2.f, 0.5f, 0.f), 0.25f);
    passed &= Check(NearlyEqual(movingAway.x, -2.f) && NearlyEqual(movingAway.y, 0.5f),
        "movement away from geometry remains unchanged");

    const float3 stationaryEye(-1.f, 0.f, 0.f);
    const float3 stationaryWithWallBetweenPivot = wallWorld.MoveSphere(
        stationaryEye, stationaryEye, 0.25f);
    passed &= Check(all(stationaryWithWallBetweenPivot == stationaryEye),
        "a wall between a hypothetical pivot and a stationary eye has no effect");

    CameraCollisionWorld degenerateWorld;
    degenerateWorld.Build({
        { float3(0.f), float3(0.f), float3(0.f) }
    });
    passed &= Check(degenerateWorld.Empty(),
        "degenerate triangles are excluded from collision");

    return passed ? 0 : 1;
}
