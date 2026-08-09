#include "camera_collision.h"
#include "flashlight.h"

#include <algorithm>
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

    std::vector<CameraCollisionWorld::Triangle> MakeMountChordBlocker()
    {
        return {
            {
                float3(-1.2f, 0.f, -1.f),
                float3(-0.8f, 0.f, -1.f),
                float3(-0.8f, 0.f, 1.f)
            },
            {
                float3(-1.2f, 0.f, -1.f),
                float3(-0.8f, 0.f, 1.f),
                float3(-1.2f, 0.f, 1.f)
            }
        };
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
    passed &= Check(emptyWorld.GetSphereTravelFraction(
        float3(-1.f, 2.f, 3.f), float3(4.f, 5.f, 6.f), 0.25f) == 1.f,
        "an empty collision world leaves direct sphere travel unrestricted");

    CameraCollisionWorld wallWorld;
    wallWorld.Build(MakeWall());
    passed &= Check(wallWorld.GetTriangleCount() == 128,
        "the wall collision world retains every valid triangle");

    const float headOnTravelFraction =
        wallWorld.GetSphereTravelFraction(
            float3(-1.f, 0.f, 0.f),
            float3(1.f, 0.f, 0.f),
            0.25f);
    passed &= Check(
        NearlyEqual(headOnTravelFraction, 0.374875f, 2e-5f),
        "direct sphere travel stops just before its first head-on contact");

    const float tangentTravelFraction =
        wallWorld.GetSphereTravelFraction(
            float3(-0.25f, -9.7f, -8.7f),
            float3(-0.25f, -9.3f, -8.7f),
            0.25f);
    passed &= Check(tangentTravelFraction == 1.f,
        "direct sphere travel preserves motion tangent to a wall");

    const float movingAwayTravelFraction =
        wallWorld.GetSphereTravelFraction(
            float3(-1.f, 0.f, 0.f),
            float3(-2.f, 0.5f, 0.f),
            0.25f);
    passed &= Check(movingAwayTravelFraction == 1.f,
        "direct sphere travel remains unrestricted while moving away");

    const float3 cameraAnchor(-1.f, 0.f, 0.f);
    const float3 fullMountTarget(0.4f, 0.3f, -0.2f);
    const float3 resolvedMountAnchor = wallWorld.ResolveSphere(
        cameraAnchor,
        fullMountTarget - cameraAnchor,
        0.25f);
    const float mountExtension = wallWorld.GetSphereTravelFraction(
        resolvedMountAnchor,
        fullMountTarget,
        0.25f);
    const float3 constrainedMountTarget = resolvedMountAnchor +
        (fullMountTarget - resolvedMountAnchor) * mountExtension;
    const float3 constrainedMountOffset =
        constrainedMountTarget - resolvedMountAnchor;
    const float3 fullMountOffset = fullMountTarget - resolvedMountAnchor;
    passed &= Check(
        mountExtension > 0.f && mountExtension < 1.f &&
            constrainedMountTarget.x <= -0.249f &&
            constrainedMountTarget.x >= -0.252f,
        "a wall retracts the mounted sphere to direct first contact");
    passed &= Check(
        NearlyEqual(
            constrainedMountOffset.x / fullMountOffset.x,
            mountExtension) &&
            NearlyEqual(
                constrainedMountOffset.y / fullMountOffset.y,
                mountExtension) &&
            NearlyEqual(
                constrainedMountOffset.z / fullMountOffset.z,
                mountExtension),
        "mount collision scales forward and lateral offsets uniformly");

    CameraCollisionWorld mountChordWorld;
    mountChordWorld.Build(MakeMountChordBlocker());
    const float3 mountRecoveryAnchor(-2.f, 0.f, 0.f);
    const float3 oldSafeMount(-1.f, 1.f, 0.f);
    const float3 newSafeMount(-1.f, -1.f, 0.f);
    constexpr float MountRecoveryRadius = 0.1f;
    passed &= Check(
        mountChordWorld.GetSphereTravelFraction(
            mountRecoveryAnchor,
            oldSafeMount,
            MountRecoveryRadius) == 1.f &&
        mountChordWorld.GetSphereTravelFraction(
            mountRecoveryAnchor,
            newSafeMount,
            MountRecoveryRadius) == 1.f,
        "old and new mount rays remain independently clear");
    const float3 blockedMountChord = mountChordWorld.MoveSphere(
        oldSafeMount,
        newSafeMount,
        MountRecoveryRadius);
    passed &= Check(
        lengthSquared(blockedMountChord - newSafeMount) > 1e-4f,
        "an intervening protrusion blocks the direct mount chord");
    const float3 retractedMount = mountChordWorld.MoveSphere(
        oldSafeMount,
        mountRecoveryAnchor,
        MountRecoveryRadius);
    const float3 reroutedMount = mountChordWorld.MoveSphere(
        retractedMount,
        newSafeMount,
        MountRecoveryRadius);
    passed &= Check(
        lengthSquared(retractedMount - mountRecoveryAnchor) <= 1e-10f &&
            lengthSquared(reroutedMount - newSafeMount) <= 1e-10f,
        "routing through the safe camera anchor clears a blocked mount chord");

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

    const float3 repairedOverlap = wallWorld.ResolveSphere(
        float3(-0.1f, 0.f, 0.f),
        float3(1.f, 0.f, 0.f),
        0.25f);
    passed &= Check(
        repairedOverlap.x <= -0.249f && repairedOverlap.x >= -0.252f,
        "a stationary mounted sphere can repair an enlarged overlapping hitbox");

    const float3 firstWallPressure = wallWorld.MoveSphere(
        float3(-1.f, 0.f, 0.f), float3(0.2f, 0.f, 0.f), 0.25f);
    const float3 secondWallPressure = wallWorld.MoveSphere(
        firstWallPressure, float3(1.f, 0.f, 0.f), 0.25f);
    passed &= Check(
        NearlyEqual(firstWallPressure.x, secondWallPressure.x, 2e-3f),
        "continued wall pressure preserves the mounted sphere standoff");

    const float3 largerEmitter = wallWorld.MoveSphere(
        float3(-1.f, 0.f, 0.f), float3(1.f, 0.f, 0.f), 0.5f);
    passed &= Check(
        largerEmitter.x <= -0.499f && largerEmitter.x >= -0.502f,
        "a larger analytical emitter produces a larger wall standoff");

    constexpr float MaximumFlashlightEmitterRadius = 0.176327f;
    const float maximumFlashlightSeparationSkin =
        CameraCollisionWorld::GetSphereSeparationSkin(
            MaximumFlashlightEmitterRadius);
    passed &= Check(
        maximumFlashlightSeparationSkin > 1e-5f &&
            NearlyEqual(
                maximumFlashlightSeparationSkin,
                MaximumFlashlightEmitterRadius * 1e-3f,
                1e-7f),
        "the largest flashlight hitbox uses its radius-scaled arrival tolerance");

    constexpr float PredictiveMountLength = 0.2f;
    constexpr float PredictiveRadius = 0.1f;
    const uvsr::FlashlightMountRetractionRange predictiveRange =
        uvsr::ResolveFlashlightMountRetractionRange(
            PredictiveRadius,
            PredictiveMountLength);
    float previousPredictiveExtension = 1.f;
    for (float anchorX : { -1.05f, -0.85f, -0.60f, -0.35f })
    {
        const float3 anchor(anchorX, 0.f, 0.f);
        const float probeDistance =
            PredictiveMountLength + predictiveRange.farDistanceMeters;
        const float3 probeEnd(
            anchorX + probeDistance,
            0.f,
            0.f);
        const float safeDistance = wallWorld.GetSphereTravelFraction(
            anchor,
            probeEnd,
            PredictiveRadius) * probeDistance;
        const float hardLimit = std::clamp(
            safeDistance / PredictiveMountLength,
            0.f,
            1.f);
        const float mountClearance = std::clamp(
            safeDistance - PredictiveMountLength,
            0.f,
            predictiveRange.farDistanceMeters);
        const float predictiveExtension =
            uvsr::ResolveFlashlightMountRetractionExtension(
                mountClearance,
                predictiveRange);
        passed &= Check(
            hardLimit == 1.f &&
                predictiveExtension <= previousPredictiveExtension,
            "the flashlight retracts monotonically before its hard mount ray is blocked");
        previousPredictiveExtension = predictiveExtension;
    }
    passed &= Check(
        previousPredictiveExtension == 0.f,
        "the predictive envelope centers the flashlight before physical contact");

    CameraCollisionWorld degenerateWorld;
    degenerateWorld.Build({
        { float3(0.f), float3(0.f), float3(0.f) }
    });
    passed &= Check(degenerateWorld.Empty(),
        "degenerate triangles are excluded from collision");

    return passed ? 0 : 1;
}
