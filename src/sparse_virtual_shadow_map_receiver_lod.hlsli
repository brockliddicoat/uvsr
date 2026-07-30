#ifndef UVSR_SPARSE_VIRTUAL_SHADOW_MAP_RECEIVER_LOD_HLSLI
#define UVSR_SPARSE_VIRTUAL_SHADOW_MAP_RECEIVER_LOD_HLSLI

// Recover only camera-view Z from reverse-Z depth. This is intentionally a
// four-coefficient projective evaluation rather than a second full matrix
// multiply. Using axial distance delays coarsening at the frustum edges and is
// therefore conservative relative to Euclidean camera distance.
float GetSvsmReceiverViewDistance(float cameraDepth)
{
    float viewZ =
        cameraDepth * g_Svsm.cameraView.matClipToView[2][2] +
        g_Svsm.cameraView.matClipToView[3][2];
    float viewW =
        cameraDepth * g_Svsm.cameraView.matClipToView[2][3] +
        g_Svsm.cameraView.matClipToView[3][3];
    if (!isfinite(viewZ) ||
        !isfinite(viewW) ||
        abs(viewW) <= 1e-20f)
    {
        return 0.0f;
    }

    float distance = abs(viewZ / viewW);
    return isfinite(distance) ? distance : 0.0f;
}

uint GetSvsmReceiverFirstClipmapLevel(float receiverViewDistance)
{
    uint configuredLevel = min(
        g_Svsm.resolutionBias,
        uint(SVSM_SPARSE_CLIPMAP_COUNT - 1));
    float startDistance =
        g_Svsm.receiverDistanceMipClampStart;
    uint maximumLevel = min(
        g_Svsm.receiverDistanceMipClampMaximumLevel,
        uint(SVSM_SPARSE_CLIPMAP_COUNT - 2));
    if (!(receiverViewDistance >= 0.0f) ||
        !isfinite(receiverViewDistance) ||
        !(startDistance > 0.0f) ||
        !isfinite(startDistance) ||
        maximumLevel == 0u)
    {
        return configuredLevel;
    }

    float distanceRatio =
        receiverViewDistance / startDistance;
    if (!(distanceRatio >= 1.0f) ||
        !isfinite(distanceRatio))
    {
        return configuredLevel;
    }

    float logarithmicLevel =
        floor(log2(distanceRatio)) + 1.0f;
    if (!(logarithmicLevel >= 1.0f) ||
        !isfinite(logarithmicLevel))
    {
        return configuredLevel;
    }

    uint distanceLevel = uint(min(
        logarithmicLevel,
        float(maximumLevel)));
    return max(configuredLevel, distanceLevel);
}

uint GetSvsmReceiverFirstClipmapLevelFromDepth(float cameraDepth)
{
    return GetSvsmReceiverFirstClipmapLevel(
        GetSvsmReceiverViewDistance(cameraDepth));
}

#endif
