#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene.h"
#include "uvsr_renderer_lighting.h"
#include "uvsr_renderer_frame.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <donut/engine/SceneGraph.h>

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

auto UvsrSceneViewer::ToggleFlashlight() -> bool {
        SetFlashlightEnabled(!m_ui.FlashlightEnabled);
        uvsr::log::info(
            "Flashlight %s",
            m_ui.FlashlightEnabled ? "on" : "off");
        return true;
    }

auto UvsrSceneViewer::SetFlashlightEnabled(
        bool enabled,
        bool invalidateHistory) -> void {
        if (m_ui.FlashlightEnabled == enabled)
            return;

        m_ui.FlashlightEnabled = enabled;
        if (invalidateHistory)
            ResetImageBasedLightingHistory();
    }

auto UvsrSceneViewer::ResetFlashlightMotion() -> void {
        m_lighting->flashlightSwayTime = 0.f;
        // One valid pose covers the camera, collision, and aim caches.
        m_lighting->flashlightResolvedRight = float3(1.f, 0.f, 0.f);
        m_lighting->flashlightPoseValid = false;
        m_lighting->flashlightSubmittedPoseValid = false;
    }

auto UvsrSceneViewer::ApplyFlashlightPresentation() -> void {
        if (!m_lighting->flashlight)
            return;

        const FlashlightSettings settings =
            SanitizeFlashlightSettings(m_ui.Flashlight);
        m_ui.Flashlight = settings;
        const FlashlightLobeSettings lobes =
            ResolveFlashlightLobeSettings(settings);
        const float emissionScale =
            GetFlashlightEmissionScale(m_lighting->flashlightTransition);
        const float3 color(
            settings.colorLinearRed,
            settings.colorLinearGreen,
            settings.colorLinearBlue);
        m_lighting->flashlight->color = color;
        m_lighting->flashlight->intensity =
            settings.peakIntensityCandela * emissionScale;
        m_lighting->flashlight->radius =
            ResolveFlashlightEmitterRadiusMeters(
                ResolveShadowEmitterSize(settings.angularSizeDegrees, m_ui.DirectionalShadows.hardShadows));
        m_lighting->flashlight->range = settings.rangeMeters;
        m_lighting->flashlight->innerAngle =
            lobes.spillInnerConeDegrees;
        m_lighting->flashlight->outerAngle =
            lobes.spillOuterConeDegrees;
    }

auto UvsrSceneViewer::UpdateFlashlightAnimation(float elapsedSeconds) -> void {
        m_lighting->flashlightTransition = AdvanceFlashlightTransition(
            m_lighting->flashlightTransition,
            m_ui.FlashlightEnabled,
            elapsedSeconds);

        ApplyFlashlightPresentation();
    }

auto UvsrSceneViewer::ClampFlashlightAimLag(
        float3 candidate,
        float3 target) -> float3 {
        candidate = normalize(candidate);
        target = normalize(target);
        const float maximumLagRadians =
            radians(FlashlightMaximumAimLagDegrees);
        const float maximumLagCosine =
            std::cos(maximumLagRadians);
        const float alignment = std::clamp(
            dot(candidate, target),
            -1.f,
            1.f);
        if (alignment >= maximumLagCosine)
            return candidate;

        const float3 tangent =
            candidate - target * alignment;
        const float tangentLengthSquared =
            lengthSquared(tangent);
        if (!(tangentLengthSquared > 1e-12f))
            return target;
        return normalize(
            target * maximumLagCosine +
            tangent * (
                std::sin(maximumLagRadians) /
                std::sqrt(tangentLengthSquared)));
    }

auto UvsrSceneViewer::InterpolateFlashlightAim(
        float3 current,
        float3 target,
        float blend) -> float3 {
        current = normalize(current);
        target = normalize(target);
        blend = std::clamp(blend, 0.f, 1.f);
        const float alignment = std::clamp(
            dot(current, target),
            -1.f,
            1.f);
        if (alignment > 0.9995f)
            return normalize(
                current * (1.f - blend) +
                target * blend);
        if (alignment < -0.9995f)
            return target;

        const float angle = std::acos(alignment);
        const float inverseSine = 1.f / std::sin(angle);
        return normalize(
            current *
                (std::sin((1.f - blend) * angle) * inverseSine) +
            target *
                (std::sin(blend * angle) * inverseSine));
    }

auto UvsrSceneViewer::UpdateFlashlightMotion(float elapsedSeconds) -> void {
        if (!ShouldSubmitFlashlight(m_lighting->flashlightTransition))
        {
            ResetFlashlightMotion();
            return;
        }

        const FlashlightSettings settings =
            SanitizeFlashlightSettings(m_ui.Flashlight);
        const BaseCamera& camera = GetActiveCamera();
        const float3 cameraDirection =
            normalize(camera.GetDir());
        const float3 cameraUp = normalize(camera.GetUp());
        const float3 cameraPosition = camera.GetPosition();
        const bool cameraPoseChanged = !m_lighting->flashlightPoseValid ||
            any(cameraPosition != m_lighting->flashlightCameraPosition) ||
            any(cameraDirection != m_lighting->flashlightCameraDirection) ||
            any(cameraUp != m_lighting->flashlightCameraUp);
        const bool motionSettingsChanged = !m_lighting->flashlightPoseValid ||
            !SameFlashlightMotionSettings(settings, m_lighting->flashlightMotionSettings);
        m_lighting->flashlightCameraPosition = cameraPosition;
        m_lighting->flashlightCameraDirection = cameraDirection;
        m_lighting->flashlightCameraUp = cameraUp;
        m_lighting->flashlightMotionSettings = settings;
        if (!ShouldAdvanceFlashlightMotion(settings, m_lighting->flashlightPoseValid,
                cameraPoseChanged, motionSettingsChanged))
            return;

        float3 cameraRight = cross(cameraDirection, cameraUp);
        if (!(lengthSquared(cameraRight) > 1e-12f))
            cameraRight = float3(1.f, 0.f, 0.f);
        else
            cameraRight = normalize(cameraRight);

        const FlashlightMountPose mount =
            ResolveFlashlightMountPose(
                settings.cameraHorizontalOffsetMeters,
                settings.cameraVerticalOffsetMeters);
        const float3 desiredFlashlightPosition =
            cameraPosition +
            cameraDirection *
                mount.positionForwardMeters +
            cameraRight *
                mount.positionRightMeters +
            cameraUp *
                mount.positionUpMeters;
        const float collisionRadius =
            ResolveFlashlightCollisionRadiusMeters(
                settings.angularSizeDegrees,
                m_scene->cameraCollisionRadius);
        const bool collisionRadiusChanged =
            !m_lighting->flashlightPoseValid ||
            std::abs(collisionRadius - m_lighting->flashlightCollisionRadius) >
                1e-6f;
        const bool desiredPositionChanged =
            !m_lighting->flashlightPoseValid ||
            lengthSquared(
                desiredFlashlightPosition -
                m_lighting->flashlightDesiredPosition) > 1e-12f;

        float3 flashlightPosition = m_lighting->flashlightResolvedPosition;
        if (collisionRadiusChanged || desiredPositionChanged)
        {
            float3 collisionStart = m_lighting->flashlightPoseValid
                ? m_lighting->flashlightResolvedPosition : cameraPosition;
            if (collisionRadiusChanged)
                collisionStart = m_scene->cameraCollisionWorld.ResolveSphere(
                    collisionStart, desiredFlashlightPosition - collisionStart, collisionRadius);
            flashlightPosition = m_scene->cameraCollisionWorld.MoveSphere(
                collisionStart, desiredFlashlightPosition, collisionRadius);
        }

        m_lighting->flashlightDesiredPosition = desiredFlashlightPosition;
        m_lighting->flashlightCollisionRadius = collisionRadius;

        // Collision may displace the emitter, but it never drives the authored
        // camera mount or aim. This keeps wall safety independent from scene
        // depth, surface selection, and the intentional direction-only sway.
        const float3 mountedDirection = normalize(
            cameraDirection * mount.directionForward +
            cameraRight * mount.directionRight +
            cameraUp * mount.directionUp);
        float3 mountedRight =
            cameraRight -
            mountedDirection *
                dot(cameraRight, mountedDirection);
        if (!(lengthSquared(mountedRight) > 1e-12f))
            mountedRight = cross(mountedDirection, cameraUp);
        if (!(lengthSquared(mountedRight) > 1e-12f))
            mountedRight = cameraRight;
        else
            mountedRight = normalize(mountedRight);
        m_lighting->flashlightResolvedPosition = flashlightPosition;

        if (!settings.realisticLens)
        {
            m_lighting->flashlightSwayTime = 0.f;
            m_lighting->flashlightAimDirection = mountedDirection;
            m_lighting->flashlightResolvedDirection = mountedDirection;
            m_lighting->flashlightResolvedRight = mountedRight;
            m_lighting->flashlightPoseValid = true;
            return;
        }

        if (!m_lighting->flashlightPoseValid)
        {
            m_lighting->flashlightAimDirection = mountedDirection;
        }
        else
        {
            const float blend = GetFlashlightAimCorrectionBlend(
                elapsedSeconds,
                settings.aimCorrectionSeconds);
            m_lighting->flashlightAimDirection = InterpolateFlashlightAim(
                m_lighting->flashlightAimDirection,
                mountedDirection,
                blend);
            m_lighting->flashlightAimDirection = ClampFlashlightAimLag(
                m_lighting->flashlightAimDirection,
                mountedDirection);
        }

        m_lighting->flashlightSwayTime = AdvanceFlashlightSwayTime(
            m_lighting->flashlightSwayTime,
            elapsedSeconds);
        const FlashlightSwayOffset sway =
            ResolveFlashlightSwayOffset(
                m_lighting->flashlightSwayTime,
                settings.swayDegrees *
                    GetFlashlightEmissionScale(
                        m_lighting->flashlightTransition));
        float3 beamRight =
            cross(m_lighting->flashlightAimDirection, cameraUp);
        if (!(lengthSquared(beamRight) > 1e-12f))
            beamRight = mountedRight;
        else
            beamRight = normalize(beamRight);
        const float3 beamUp = normalize(
            cross(beamRight, m_lighting->flashlightAimDirection));
        m_lighting->flashlightResolvedDirection = normalize(
            m_lighting->flashlightAimDirection +
            beamRight * std::tan(radians(sway.yawDegrees)) +
            beamUp * std::tan(radians(sway.pitchDegrees)));
        beamRight -=
            m_lighting->flashlightResolvedDirection *
                dot(beamRight, m_lighting->flashlightResolvedDirection);
        if (!(lengthSquared(beamRight) > 1e-12f))
        {
            beamRight =
                mountedRight -
                m_lighting->flashlightResolvedDirection *
                    dot(
                        mountedRight,
                        m_lighting->flashlightResolvedDirection);
        }
        m_lighting->flashlightResolvedRight =
            lengthSquared(beamRight) > 1e-12f
                ? normalize(beamRight)
                : float3(1.f, 0.f, 0.f);
        m_lighting->flashlightPoseValid = true;
    }

auto UvsrSceneViewer::SetFlashlightDirectionAndRoll(
        const std::shared_ptr<SpotLight>& light,
        const float3& direction,
        const float3& right) -> void {
        if (!light || !light->GetNode())
            return;

        const double3 directionD =
            normalize(double3(direction));
        double3 rightD = double3(right);
        rightD -= directionD * dot(rightD, directionD);
        if (!(lengthSquared(rightD) > 1e-20))
            rightD = normalize(orthogonal(directionD));
        else
            rightD = normalize(rightD);
        const double3 upD =
            normalize(cross(rightD, directionD));

        SceneGraphNode* node = light->GetNode();
        SceneGraphNode* parent = node->GetParent();
        daffine3 parentToWorld = daffine3::identity();
        if (parent)
            parentToWorld =
                daffine3(parent->GetLocalToWorldTransform());

        const daffine3 worldToLocal =
            lookatZ(directionD, upD);
        const daffine3 localToParent =
            inverse(worldToLocal * parentToWorld);
        dquat rotation;
        double3 scaling;
        decomposeAffine<double>(
            localToParent,
            nullptr,
            &rotation,
            &scaling);
        node->SetTransform(nullptr, &rotation, &scaling);
    }

auto UvsrSceneViewer::UpdateFlashlightTransform() -> void {
        if (!m_lighting->flashlightPoseValid ||
            !ShouldSubmitFlashlight(m_lighting->flashlightTransition) ||
            !m_lighting->flashlight ||
            !m_lighting->flashlightNode)
            return;

        const bool positionChanged = !m_lighting->flashlightSubmittedPoseValid ||
            any(m_lighting->flashlightResolvedPosition != m_lighting->flashlightSubmittedPosition);
        const bool orientationChanged = !m_lighting->flashlightSubmittedPoseValid ||
            any(m_lighting->flashlightResolvedDirection != m_lighting->flashlightSubmittedDirection) ||
            any(m_lighting->flashlightResolvedRight != m_lighting->flashlightSubmittedRight);
        if (positionChanged)
            m_lighting->flashlight->SetPosition(
                double3(m_lighting->flashlightResolvedPosition));

        if (orientationChanged)
        {
            SetFlashlightDirectionAndRoll(
                m_lighting->flashlight,
                m_lighting->flashlightResolvedDirection,
                m_lighting->flashlightResolvedRight);
        }
        m_lighting->flashlightSubmittedPosition = m_lighting->flashlightResolvedPosition;
        m_lighting->flashlightSubmittedDirection = m_lighting->flashlightResolvedDirection;
        m_lighting->flashlightSubmittedRight = m_lighting->flashlightResolvedRight;
        m_lighting->flashlightSubmittedPoseValid = true;
    }

auto UvsrSceneViewer::AttachFlashlightToScene() -> void {
        if (!m_scene->world ||
            !m_scene->world->GetSceneGraph() ||
            !m_scene->world->GetSceneGraph()->GetRootNode())
        {
            return;
        }

        m_lighting->flashlight = std::make_shared<SpotLight>();
        m_lighting->flashlight->SetName(FlashlightPublicName);

        m_lighting->flashlightNode = std::make_shared<SceneGraphNode>();
        m_lighting->flashlightNode->SetName(FlashlightPublicName);
        m_lighting->flashlightNode->SetLeaf(m_lighting->flashlight);
        m_scene->world->GetSceneGraph()->Attach(
            m_scene->world->GetSceneGraph()->GetRootNode(),
            m_lighting->flashlightNode);

        m_lighting->flashlightSubmittedPoseValid = false;
        ApplyFlashlightPresentation();
        UpdateFlashlightTransform();
    }

auto UvsrSceneViewer::IsFlashlight(const std::shared_ptr<Light>& light) const -> bool {
        return light && light == m_lighting->flashlight;
    }
