#include "uvsr_internal.h"

auto UvsrSceneViewer::ToggleFlashlight() -> bool {
        m_ui.FlashlightEnabled = !m_ui.FlashlightEnabled;
        ResetAntiAliasingState();
        uvsr::log::info(
            "Flashlight %s",
            m_ui.FlashlightEnabled ? "on" : "off");
        return true;
    }

auto UvsrSceneViewer::ResetFlashlightMotion() -> void {
        m_FlashlightSwayTime = 0.f;
        m_FlashlightAimDirection = float3(0.f, 0.f, -1.f);
        m_FlashlightResolvedPosition = 0.f;
        m_FlashlightResolvedDirection =
            float3(0.f, 0.f, -1.f);
        m_FlashlightResolvedRight =
            float3(1.f, 0.f, 0.f);
        m_FlashlightDesiredPosition = 0.f;
        m_FlashlightCollisionRadius = 0.f;
        m_FlashlightCollisionInitialized = false;
        m_FlashlightAimInitialized = false;
        m_FlashlightPoseValid = false;
        m_FlashlightMotionSettingsValid = false;
        m_FlashlightCameraPoseValid = false;
        m_FlashlightSubmittedPoseValid = false;
    }

auto UvsrSceneViewer::SameFlashlightVector(
        const float3& left,
        const float3& right) -> bool {
        return left.x == right.x &&
            left.y == right.y &&
            left.z == right.z;
    }

auto UvsrSceneViewer::ApplyFlashlightPresentation() -> void {
        if (!m_Flashlight)
            return;

        const FlashlightSettings settings =
            SanitizeFlashlightSettings(m_ui.Flashlight);
        m_ui.Flashlight = settings;
        const FlashlightLobeSettings lobes =
            ResolveFlashlightLobeSettings(settings);
        const float emissionScale =
            GetFlashlightEmissionScale(m_FlashlightTransition);
        const float3 color(
            settings.colorLinearRed,
            settings.colorLinearGreen,
            settings.colorLinearBlue);
        m_Flashlight->color = color;
        m_Flashlight->intensity =
            settings.peakIntensityCandela * emissionScale;
        m_Flashlight->radius =
            ResolveFlashlightEmitterRadiusMeters(
                settings.angularSizeDegrees);
        m_Flashlight->range = settings.rangeMeters;
        m_Flashlight->innerAngle =
            lobes.spillInnerConeDegrees;
        m_Flashlight->outerAngle =
            lobes.spillOuterConeDegrees;
    }

auto UvsrSceneViewer::UpdateFlashlightAnimation(float elapsedSeconds) -> void {
        m_FlashlightTransition = AdvanceFlashlightTransition(
            m_FlashlightTransition,
            m_ui.FlashlightEnabled,
            elapsedSeconds);

        ApplyFlashlightPresentation();
        if (!ShouldSubmitFlashlight(m_FlashlightTransition))
            ResetFlashlightMotion();
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
        if (!ShouldSubmitFlashlight(m_FlashlightTransition))
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
        const FlashlightMotionSettings motionSettings =
            ResolveFlashlightMotionSettings(settings);
        const bool cameraPoseChanged =
            !m_FlashlightCameraPoseValid ||
            !SameFlashlightVector(
                cameraPosition,
                m_FlashlightCameraPosition) ||
            !SameFlashlightVector(
                cameraDirection,
                m_FlashlightCameraDirection) ||
            !SameFlashlightVector(cameraUp, m_FlashlightCameraUp);
        const bool motionSettingsChanged =
            !m_FlashlightMotionSettingsValid ||
            motionSettings != m_FlashlightMotionSettings;
        m_FlashlightCameraPosition = cameraPosition;
        m_FlashlightCameraDirection = cameraDirection;
        m_FlashlightCameraUp = cameraUp;
        m_FlashlightCameraPoseValid = true;
        m_FlashlightMotionSettings = motionSettings;
        m_FlashlightMotionSettingsValid = true;
        if (!ShouldAdvanceFlashlightMotion(
                motionSettings,
                m_FlashlightPoseValid,
                cameraPoseChanged,
                motionSettingsChanged))
        {
            return;
        }

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
                m_CameraCollisionRadius);
        const bool collisionRadiusChanged =
            !m_FlashlightCollisionInitialized ||
            std::abs(collisionRadius - m_FlashlightCollisionRadius) >
                1e-6f;
        const bool desiredPositionChanged =
            !m_FlashlightCollisionInitialized ||
            lengthSquared(
                desiredFlashlightPosition -
                m_FlashlightDesiredPosition) > 1e-12f;

        float3 flashlightPosition = m_FlashlightResolvedPosition;
        if (!m_FlashlightCollisionInitialized)
        {
            const float3 collisionStart =
                m_CameraCollisionWorld.ResolveSphere(
                cameraPosition,
                desiredFlashlightPosition - cameraPosition,
                collisionRadius);
            flashlightPosition = m_CameraCollisionWorld.MoveSphere(
                collisionStart,
                desiredFlashlightPosition,
                collisionRadius);
        }
        else if (collisionRadiusChanged || desiredPositionChanged)
        {
            float3 collisionStart = m_FlashlightResolvedPosition;
            if (collisionRadiusChanged)
            {
                collisionStart = m_CameraCollisionWorld.ResolveSphere(
                    collisionStart,
                    desiredFlashlightPosition - collisionStart,
                    collisionRadius);
            }
            flashlightPosition = m_CameraCollisionWorld.MoveSphere(
                collisionStart,
                desiredFlashlightPosition,
                collisionRadius);
        }

        m_FlashlightDesiredPosition = desiredFlashlightPosition;
        m_FlashlightCollisionRadius = collisionRadius;
        m_FlashlightCollisionInitialized = true;

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
        m_FlashlightResolvedPosition = flashlightPosition;

        if (!settings.realisticLens)
        {
            m_FlashlightSwayTime = 0.f;
            m_FlashlightAimDirection = mountedDirection;
            m_FlashlightAimInitialized = true;
            m_FlashlightResolvedDirection = mountedDirection;
            m_FlashlightResolvedRight = mountedRight;
            m_FlashlightPoseValid = true;
            return;
        }

        if (!m_FlashlightAimInitialized)
        {
            m_FlashlightAimDirection = mountedDirection;
            m_FlashlightAimInitialized = true;
        }
        else
        {
            const float blend = GetFlashlightAimCorrectionBlend(
                elapsedSeconds,
                settings.aimCorrectionSeconds);
            m_FlashlightAimDirection = InterpolateFlashlightAim(
                m_FlashlightAimDirection,
                mountedDirection,
                blend);
            m_FlashlightAimDirection = ClampFlashlightAimLag(
                m_FlashlightAimDirection,
                mountedDirection);
        }

        m_FlashlightSwayTime = AdvanceFlashlightSwayTime(
            m_FlashlightSwayTime,
            elapsedSeconds);
        const FlashlightSwayOffset sway =
            ResolveFlashlightSwayOffset(
                m_FlashlightSwayTime,
                settings.swayDegrees *
                    GetFlashlightEmissionScale(
                        m_FlashlightTransition));
        float3 beamRight =
            cross(m_FlashlightAimDirection, cameraUp);
        if (!(lengthSquared(beamRight) > 1e-12f))
            beamRight = mountedRight;
        else
            beamRight = normalize(beamRight);
        const float3 beamUp = normalize(
            cross(beamRight, m_FlashlightAimDirection));
        m_FlashlightResolvedDirection = normalize(
            m_FlashlightAimDirection +
            beamRight * std::tan(radians(sway.yawDegrees)) +
            beamUp * std::tan(radians(sway.pitchDegrees)));
        beamRight -=
            m_FlashlightResolvedDirection *
                dot(beamRight, m_FlashlightResolvedDirection);
        if (!(lengthSquared(beamRight) > 1e-12f))
        {
            beamRight =
                mountedRight -
                m_FlashlightResolvedDirection *
                    dot(
                        mountedRight,
                        m_FlashlightResolvedDirection);
        }
        m_FlashlightResolvedRight =
            lengthSquared(beamRight) > 1e-12f
                ? normalize(beamRight)
                : float3(1.f, 0.f, 0.f);
        m_FlashlightPoseValid = true;
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
        if (!m_FlashlightPoseValid ||
            !ShouldSubmitFlashlight(m_FlashlightTransition) ||
            !m_Flashlight ||
            !m_FlashlightNode)
            return;

        const bool positionChanged =
            !m_FlashlightSubmittedPoseValid ||
            !SameFlashlightVector(
                m_FlashlightResolvedPosition,
                m_FlashlightSubmittedPosition);
        const bool orientationChanged =
            !m_FlashlightSubmittedPoseValid ||
            !SameFlashlightVector(
                m_FlashlightResolvedDirection,
                m_FlashlightSubmittedDirection) ||
            !SameFlashlightVector(
                m_FlashlightResolvedRight,
                m_FlashlightSubmittedRight);
        if (positionChanged)
            m_Flashlight->SetPosition(
                double3(m_FlashlightResolvedPosition));

        if (orientationChanged)
        {
            SetFlashlightDirectionAndRoll(
                m_Flashlight,
                m_FlashlightResolvedDirection,
                m_FlashlightResolvedRight);
        }
        m_FlashlightSubmittedPosition = m_FlashlightResolvedPosition;
        m_FlashlightSubmittedDirection = m_FlashlightResolvedDirection;
        m_FlashlightSubmittedRight = m_FlashlightResolvedRight;
        m_FlashlightSubmittedPoseValid = true;
    }

auto UvsrSceneViewer::AttachFlashlightToScene() -> void {
        if (!m_Scene ||
            !m_Scene->GetSceneGraph() ||
            !m_Scene->GetSceneGraph()->GetRootNode())
        {
            return;
        }

        m_Flashlight = std::make_shared<SpotLight>();
        m_Flashlight->SetName(FlashlightPublicName);

        m_FlashlightNode = std::make_shared<SceneGraphNode>();
        m_FlashlightNode->SetName(FlashlightPublicName);
        m_FlashlightNode->SetLeaf(m_Flashlight);
        m_Scene->GetSceneGraph()->Attach(
            m_Scene->GetSceneGraph()->GetRootNode(),
            m_FlashlightNode);

        m_FlashlightSubmittedPoseValid = false;
        ApplyFlashlightPresentation();
        UpdateFlashlightTransform();
    }

auto UvsrSceneViewer::IsFlashlight(const std::shared_ptr<Light>& light) const -> bool {
        return light && light == m_Flashlight;
    }

