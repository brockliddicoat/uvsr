#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene_nvrhi.h"
#include "uvsr_renderer_lighting_nvrhi.h"
#include "uvsr_renderer_frame_nvrhi.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <utility>

using namespace donut;
using namespace donut::app;
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
        m_lighting->flashlightResolvedRight = gpu_contract::Float3{1.f, 0.f, 0.f};
        m_lighting->flashlightPoseValid = false;
        m_lighting->flashlightSubmittedPoseValid = false;
    }

auto UvsrSceneViewer::ApplyFlashlightPresentation() -> bool {
        if (!m_lighting->flashlight)
            return true;

        const FlashlightSettings settings =
            SanitizeFlashlightSettings(m_ui.Flashlight);
        const FlashlightLobeSettings lobes =
            ResolveFlashlightLobeSettings(settings);
        const float emissionScale =
            GetFlashlightEmissionScale(m_lighting->flashlightTransition);
        RendererSceneLightValues candidate;
        if (!ReadSceneLightValues(m_lighting->flashlight, candidate))
            return false;
        candidate.color = {settings.colorLinearRed, settings.colorLinearGreen, settings.colorLinearBlue};
        candidate.intensity = settings.peakIntensityCandela * emissionScale;
        candidate.radius = ResolveFlashlightEmitterRadiusMeters(
            ResolveShadowEmitterSize(settings.angularSizeDegrees, m_ui.DirectionalShadows.hardShadows));
        candidate.range = settings.rangeMeters;
        candidate.innerAngle = lobes.spillInnerConeDegrees;
        candidate.outerAngle = lobes.spillOuterConeDegrees;
        if (!SetSceneLightValues(m_lighting->flashlight, candidate)) return false;
        m_ui.Flashlight = settings;
        return true;
    }

auto UvsrSceneViewer::UpdateFlashlightAnimation(float elapsedSeconds) -> void {
        m_lighting->flashlightTransition = AdvanceFlashlightTransition(
            m_lighting->flashlightTransition,
            m_ui.FlashlightEnabled,
            elapsedSeconds);

        if (!ApplyFlashlightPresentation())
        {
            uvsr::log::error("Flashlight presentation transaction failed");
            GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
        }
    }

auto UvsrSceneViewer::ClampFlashlightAimLag(
        gpu_contract::Float3 candidate,
        gpu_contract::Float3 target) -> gpu_contract::Float3 {
        candidate = Normalize(candidate);
        target = Normalize(target);
        const float maximumLagRadians =
            Radians(FlashlightMaximumAimLagDegrees);
        const float maximumLagCosine =
            std::cos(maximumLagRadians);
        const float alignment = std::clamp(
            Dot(candidate, target),
            -1.f,
            1.f);
        if (alignment >= maximumLagCosine)
            return candidate;

        const gpu_contract::Float3 tangent =
            candidate - target * alignment;
        const float tangentLengthSquared =
            LengthSquared(tangent);
        if (!(tangentLengthSquared > 1e-12f))
            return target;
        return Normalize(
            target * maximumLagCosine +
            tangent * (
                std::sin(maximumLagRadians) /
                std::sqrt(tangentLengthSquared)));
    }

auto UvsrSceneViewer::InterpolateFlashlightAim(
        gpu_contract::Float3 current,
        gpu_contract::Float3 target,
        float blend) -> gpu_contract::Float3 {
        current = Normalize(current);
        target = Normalize(target);
        blend = std::clamp(blend, 0.f, 1.f);
        const float alignment = std::clamp(
            Dot(current, target),
            -1.f,
            1.f);
        if (alignment > 0.9995f)
            return Normalize(
                current * (1.f - blend) +
                target * blend);
        if (alignment < -0.9995f)
            return target;

        const float angle = std::acos(alignment);
        const float inverseSine = 1.f / std::sin(angle);
        return Normalize(
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
        const CameraController& camera = GetActiveCamera();
        const gpu_contract::Float3 cameraDirection =
            Normalize(camera.GetDir());
        const gpu_contract::Float3 cameraUp = Normalize(camera.GetUp());
        const gpu_contract::Float3 cameraPosition = camera.GetPosition();
        const bool cameraPoseChanged = !m_lighting->flashlightPoseValid ||
            cameraPosition != m_lighting->flashlightCameraPosition ||
            cameraDirection != m_lighting->flashlightCameraDirection ||
            cameraUp != m_lighting->flashlightCameraUp;
        const bool motionSettingsChanged = !m_lighting->flashlightPoseValid ||
            !SameFlashlightMotionSettings(settings, m_lighting->flashlightMotionSettings);
        m_lighting->flashlightCameraPosition = cameraPosition;
        m_lighting->flashlightCameraDirection = cameraDirection;
        m_lighting->flashlightCameraUp = cameraUp;
        m_lighting->flashlightMotionSettings = settings;
        if (!ShouldAdvanceFlashlightMotion(settings, m_lighting->flashlightPoseValid,
                cameraPoseChanged, motionSettingsChanged))
            return;

        gpu_contract::Float3 cameraRight = Cross(cameraDirection, cameraUp);
        if (!(LengthSquared(cameraRight) > 1e-12f))
            cameraRight = gpu_contract::Float3{1.f, 0.f, 0.f};
        else
            cameraRight = Normalize(cameraRight);

        const FlashlightMountPose mount =
            ResolveFlashlightMountPose(
                settings.cameraHorizontalOffsetMeters,
                settings.cameraVerticalOffsetMeters);
        const gpu_contract::Float3 desiredFlashlightPosition =
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
            LengthSquared(
                desiredFlashlightPosition -
                m_lighting->flashlightDesiredPosition) > 1e-12f;

        gpu_contract::Float3 flashlightPosition = m_lighting->flashlightResolvedPosition;
        if (collisionRadiusChanged || desiredPositionChanged)
        {
            gpu_contract::Float3 collisionStart = m_lighting->flashlightPoseValid
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
        const gpu_contract::Float3 mountedDirection = Normalize(
            cameraDirection * mount.directionForward +
            cameraRight * mount.directionRight +
            cameraUp * mount.directionUp);
        gpu_contract::Float3 mountedRight =
            cameraRight -
            mountedDirection *
                Dot(cameraRight, mountedDirection);
        if (!(LengthSquared(mountedRight) > 1e-12f))
            mountedRight = Cross(mountedDirection, cameraUp);
        if (!(LengthSquared(mountedRight) > 1e-12f))
            mountedRight = cameraRight;
        else
            mountedRight = Normalize(mountedRight);
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
        gpu_contract::Float3 beamRight =
            Cross(m_lighting->flashlightAimDirection, cameraUp);
        if (!(LengthSquared(beamRight) > 1e-12f))
            beamRight = mountedRight;
        else
            beamRight = Normalize(beamRight);
        const gpu_contract::Float3 beamUp = Normalize(
            Cross(beamRight, m_lighting->flashlightAimDirection));
        m_lighting->flashlightResolvedDirection = Normalize(
            m_lighting->flashlightAimDirection +
            beamRight * std::tan(Radians(sway.yawDegrees)) +
            beamUp * std::tan(Radians(sway.pitchDegrees)));
        beamRight -=
            m_lighting->flashlightResolvedDirection *
                Dot(beamRight, m_lighting->flashlightResolvedDirection);
        if (!(LengthSquared(beamRight) > 1e-12f))
        {
            beamRight =
                mountedRight -
                m_lighting->flashlightResolvedDirection *
                    Dot(
                        mountedRight,
                        m_lighting->flashlightResolvedDirection);
        }
        m_lighting->flashlightResolvedRight =
            LengthSquared(beamRight) > 1e-12f
                ? Normalize(beamRight)
                : gpu_contract::Float3{1.f, 0.f, 0.f};
        m_lighting->flashlightPoseValid = true;
    }

auto UvsrSceneViewer::ReadSceneLightValues(
        RendererSceneHandle light, RendererSceneLightValues& output) const -> bool {
        const auto* record = FindRendererSceneLight(m_scene->canonical.View(), light);
        if (!record) return false;
        output = record->values;
        return true;
    }

auto UvsrSceneViewer::ReadSceneLightDirection(
        RendererSceneHandle light, RendererSceneLightDirection& output) const -> bool {
        RendererSceneLightFrame frame;
        if (!GetRendererSceneLightFrame(m_scene->canonical.View(), light, frame).Succeeded()) return false;
        output = {frame.direction[0], frame.direction[1], frame.direction[2]};
        return true;
    }

auto UvsrSceneViewer::SetSceneLightValues(
        RendererSceneHandle light, const RendererSceneLightValues& candidate) -> bool {
        return m_scene->canonical.SetLight(light, candidate).Succeeded();
    }

auto UvsrSceneViewer::SetSceneLightPose(RendererSceneHandle light,
        const gpu_contract::Float3* position, const RendererSceneLightDirection* direction, const gpu_contract::Float3* right) -> bool {
        RendererSceneLightPose pose;
        pose.setPosition = position != nullptr;
        pose.setDirection = direction != nullptr;
        pose.setRight = right != nullptr;
        if (position) { pose.position[0] = position->x; pose.position[1] = position->y; pose.position[2] = position->z; }
        if (direction) { pose.direction[0] = direction->x; pose.direction[1] = direction->y; pose.direction[2] = direction->z; }
        if (right) { pose.right[0] = right->x; pose.right[1] = right->y; pose.right[2] = right->z; }
        return SetRendererSceneLightPose(m_scene->canonical, light, pose).Succeeded();
    }

auto UvsrSceneViewer::UpdateFlashlightTransform() -> bool {
        if (!m_lighting->flashlightPoseValid ||
            !ShouldSubmitFlashlight(m_lighting->flashlightTransition) ||
            !m_lighting->flashlight)
            return true;

        const bool positionChanged = !m_lighting->flashlightSubmittedPoseValid ||
            m_lighting->flashlightResolvedPosition != m_lighting->flashlightSubmittedPosition;
        const bool orientationChanged = !m_lighting->flashlightSubmittedPoseValid ||
            m_lighting->flashlightResolvedDirection != m_lighting->flashlightSubmittedDirection ||
            m_lighting->flashlightResolvedRight != m_lighting->flashlightSubmittedRight;
        const auto& resolvedDirection = m_lighting->flashlightResolvedDirection;
        const RendererSceneLightDirection direction{resolvedDirection.x, resolvedDirection.y, resolvedDirection.z};
        if (!SetSceneLightPose(m_lighting->flashlight,
                positionChanged ? &m_lighting->flashlightResolvedPosition : nullptr,
                orientationChanged ? &direction : nullptr,
                orientationChanged ? &m_lighting->flashlightResolvedRight : nullptr))
            return false;
        m_lighting->flashlightSubmittedPosition = m_lighting->flashlightResolvedPosition;
        m_lighting->flashlightSubmittedDirection = m_lighting->flashlightResolvedDirection;
        m_lighting->flashlightSubmittedRight = m_lighting->flashlightResolvedRight;
        m_lighting->flashlightSubmittedPoseValid = true;
        return true;
    }

auto UvsrSceneViewer::IsFlashlight(RendererSceneHandle light) const -> bool {
        return light && light == m_lighting->flashlight;
    }
