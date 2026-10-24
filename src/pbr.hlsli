#ifndef UVSR_PBR_HLSLI
#define UVSR_PBR_HLSLI

#include "lighting_contribution.hlsli"

// UVSR's transport-independent, single-scattering metallic-roughness core.
// Directions point away from the surface. Radiometric inputs and outputs are
// scene-linear HDR values; exposure and display mapping happen later.

static const float UVSR_PI = 3.14159265358979323846;
static const float UVSR_INV_PI = 0.31830988618379067154;
static const float UVSR_MIN_ALPHA = 0.002f;
static const float UVSR_MIN_COSINE = 1e-5f;
static const float UVSR_MIN_PDF = 1e-6f;

static const uint PbrFeature_Coat = 1u << 0;
static const uint PbrFeature_Anisotropy = 1u << 1;
static const uint PbrFeature_Translucency = 1u << 2;
static const uint PbrFeature_Refraction = 1u << 3;
static const uint PbrFeature_Scattering = 1u << 4;
static const uint PbrFeature_ThinFilmIridescence = 1u << 5;
static const uint PbrFeature_Absorption = 1u << 6;
static const uint PbrFeature_DoubleSided = 1u << 7;

// Packed into the FP16 alpha of UVSR's GI source/frontier textures. Integer
// values through seven are exact in half precision, so the metadata survives
// every bounce without another full-resolution G-buffer feature read.
static const uint PbrGiMetadata_OutgoingDoubleSided = 1u << 0;
static const uint PbrGiMetadata_SurfaceDoubleSided = 1u << 1;
static const uint PbrGiMetadata_DiffuseActive = 1u << 2;

struct PbrMaterialParameters
{
    float3 baseColor;
    float metalness;
    float perceptualRoughness;
    float dielectricF0;
    float3 emissive;
    float opacity;
    uint featureMask;
};

struct PbrSurfaceInteraction
{
    float3 position;
    float3 shadingNormal;
    float3 geometricNormal;
    float3 viewDirection;
};

// Values shared by every direct light at one surface are normalized or derived
// once per pixel. Light loops then pay only their per-light direction, half
// vector, Fresnel, distribution, and visibility work.
struct PbrPreparedSurface
{
    float3 shadingNormal;
    float3 geometricNormal;
    float3 viewDirection;
    float shadingNoV;
    float geometricNoV;
};

struct PbrPreparedMaterial
{
    float3 diffuseColor;
    float3 specularF0;
    float alpha;
};

struct PbrBsdfEvaluation
{
    float3 diffuse;
    float3 specular;
    float3 total;
    float diffusePdf;
    float specularPdf;
};

struct PbrLightSample
{
    float3 directionToLight;
    float3 incidentRadiance;
    float visibility;
    float lightSelectionPdf;
    float directionalPdf;
};

struct PbrDirectLighting
{
    float3 diffuse;
    float3 specular;
    float3 total;
    float3 incidentRadiance;
    float cosineTerm;
    float visibility;
    float samplingPdf;
    float sampleWeight;
};

float3 PbrSafeNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12f ? value * rsqrt(lengthSquared) : fallback;
}

PbrPreparedSurface PreparePbrSurface(PbrSurfaceInteraction surface)
{
    PbrPreparedSurface prepared;
    prepared.geometricNormal = PbrSafeNormalize(
        surface.geometricNormal, float3(0.0f, 1.0f, 0.0f));
    prepared.shadingNormal = PbrSafeNormalize(
        surface.shadingNormal, prepared.geometricNormal);
    prepared.viewDirection = PbrSafeNormalize(
        surface.viewDirection, prepared.geometricNormal);
    prepared.shadingNoV = dot(prepared.shadingNormal, prepared.viewDirection);
    prepared.geometricNoV = dot(prepared.geometricNormal, prepared.viewDirection);
    return prepared;
}

uint ClassifyPbrDirectSurfacePrepared(
    PbrPreparedSurface surface,
    float3 directionToLight)
{
    bool backFacing = surface.geometricNoV <= UVSR_MIN_COSINE ||
        dot(surface.geometricNormal, directionToLight) <= UVSR_MIN_COSINE ||
        surface.shadingNoV <= UVSR_MIN_COSINE ||
        dot(surface.shadingNormal, directionToLight) <= UVSR_MIN_COSINE;
    return LightingRejectIf(backFacing, LightingRejection_BackFacing);
}

uint ClassifyPbrDirectSurface(
    PbrSurfaceInteraction surface,
    float3 lightDirection)
{
    PbrPreparedSurface preparedSurface = PreparePbrSurface(surface);
    float3 directionToLight = PbrSafeNormalize(
        lightDirection, preparedSurface.geometricNormal);
    return ClassifyPbrDirectSurfacePrepared(
        preparedSurface, directionToLight);
}

float PerceptualRoughnessToAlpha(float perceptualRoughness)
{
    float roughness = saturate(perceptualRoughness);
    return max(roughness * roughness, UVSR_MIN_ALPHA);
}

PbrPreparedMaterial PreparePbrMaterial(PbrMaterialParameters material)
{
    PbrPreparedMaterial prepared;
    prepared.diffuseColor = material.baseColor * (1.0f - material.metalness);
    prepared.specularF0 = lerp(
        material.dielectricF0.xxx, material.baseColor, material.metalness);
    prepared.alpha = PerceptualRoughnessToAlpha(material.perceptualRoughness);
    return prepared;
}

float IorToF0(float ior)
{
    float safeIor = max(ior, 1.0f);
    float ratio = (safeIor - 1.0f) / (safeIor + 1.0f);
    return ratio * ratio;
}

float F0ToIor(float f0)
{
    float rootF0 = sqrt(saturate(f0));
    return (1.0f + rootF0) / max(1.0f - rootF0, UVSR_MIN_PDF);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    float oneMinusCosine = 1.0f - saturate(cosTheta);
    float factor = oneMinusCosine * oneMinusCosine;
    factor *= factor * oneMinusCosine;
    return f0 + (1.0f - f0) * factor;
}

float D_GGX(float NoH, float alpha)
{
    float alphaSquared = alpha * alpha;
    float denominator = NoH * NoH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(UVSR_PI * denominator * denominator, UVSR_MIN_PDF);
}

// Height-correlated Smith-GGX visibility, equal to G2 / (4 NoV NoL).
float V_SmithGGXCorrelated(float NoV, float NoL, float alpha)
{
    float alphaSquared = alpha * alpha;
    float lambdaV = NoL * sqrt(max(NoV * NoV * (1.0f - alphaSquared) + alphaSquared, 0.0f));
    float lambdaL = NoV * sqrt(max(NoL * NoL * (1.0f - alphaSquared) + alphaSquared, 0.0f));
    return 0.5f / max(lambdaV + lambdaL, UVSR_MIN_PDF);
}

float3 EvaluateLambert(PbrMaterialParameters material, float3 fresnel)
{
    float3 diffuseColor = material.baseColor * (1.0f - material.metalness);
    return diffuseColor * (1.0f - fresnel) * UVSR_INV_PI;
}

float3 EvaluateGGX(float D, float visibility, float3 fresnel)
{
    return D * visibility * fresnel;
}

float PdfLambert(float NoL)
{
    return saturate(NoL) * UVSR_INV_PI;
}

// PDF for sampling the GGX normal distribution and reflecting the view vector.
float PdfGGX(float NoH, float VoH, float alpha)
{
    return D_GGX(NoH, alpha) * saturate(NoH) / max(4.0f * abs(VoH), UVSR_MIN_PDF);
}

PbrBsdfEvaluation EvaluateBsdfPrepared(
    PbrPreparedMaterial material,
    PbrPreparedSurface surface,
    float3 directionToLight)
{
    PbrBsdfEvaluation result = (PbrBsdfEvaluation)0;

    // Geometric-normal checks prevent perturbed normals from emitting energy
    // through the back of a surface. A full shading-normal energy correction
    // is intentionally deferred until transport paths require it.
    float geometricNoV = surface.geometricNoV;
    float geometricNoL = dot(surface.geometricNormal, directionToLight);
    float NoV = surface.shadingNoV;
    float NoL = dot(surface.shadingNormal, directionToLight);
    if (geometricNoV <= UVSR_MIN_COSINE || geometricNoL <= UVSR_MIN_COSINE ||
        NoV <= UVSR_MIN_COSINE || NoL <= UVSR_MIN_COSINE)
        return result;

    float3 halfVector = PbrSafeNormalize(
        surface.viewDirection + directionToLight, surface.shadingNormal);
    float NoH = saturate(dot(surface.shadingNormal, halfVector));
    float VoH = saturate(dot(surface.viewDirection, halfVector));
    float3 fresnel = FresnelSchlick(VoH, material.specularF0);

    float distribution = D_GGX(NoH, material.alpha);
    float visibility = V_SmithGGXCorrelated(NoV, NoL, material.alpha);
    result.diffuse = material.diffuseColor * (1.0f - fresnel) * UVSR_INV_PI;
    result.specular = EvaluateGGX(distribution, visibility, fresnel);
    result.total = result.diffuse + result.specular;
    result.diffusePdf = PdfLambert(NoL);
    result.specularPdf = PdfGGX(NoH, VoH, material.alpha);
    return result;
}

PbrBsdfEvaluation EvaluateBsdf(
    PbrMaterialParameters material,
    PbrSurfaceInteraction surface,
    float3 lightDirection)
{
    PbrPreparedSurface preparedSurface = PreparePbrSurface(surface);
    float3 directionToLight = PbrSafeNormalize(
        lightDirection, preparedSurface.geometricNormal);
    return EvaluateBsdfPrepared(
        PreparePbrMaterial(material), preparedSurface, directionToLight);
}

PbrDirectLighting EvaluateDirectLightPrevalidated(
    PbrPreparedMaterial material,
    PbrPreparedSurface surface,
    PbrLightSample lightSample)
{
    PbrDirectLighting result = (PbrDirectLighting)0;
    result.incidentRadiance = max(lightSample.incidentRadiance, 0.0f);
    result.visibility = saturate(lightSample.visibility);
    result.samplingPdf = max(lightSample.lightSelectionPdf * lightSample.directionalPdf, UVSR_MIN_PDF);

    PbrBsdfEvaluation bsdf = EvaluateBsdfPrepared(
        material, surface, lightSample.directionToLight);
    result.cosineTerm = saturate(dot(
        surface.shadingNormal, lightSample.directionToLight));
    result.sampleWeight = result.cosineTerm * result.visibility / result.samplingPdf;
    result.diffuse = result.incidentRadiance * bsdf.diffuse * result.sampleWeight;
    result.specular = result.incidentRadiance * bsdf.specular * result.sampleWeight;
    result.total = result.diffuse + result.specular;
    return result;
}

PbrDirectLighting EvaluateDirectLight(
    PbrMaterialParameters material,
    PbrSurfaceInteraction surface,
    PbrLightSample lightSample)
{
    LightingContributionGate exactGate = MakeLightingContributionGate(
        0u, 0u, 0.0f, 1.0f);
    uint rejection = LightingClassifyContribution(
        exactGate,
        LightingSource_Direct,
        lightSample.incidentRadiance,
        lightSample.visibility);
    if (!LightingShouldEvaluate(rejection))
    {
        PbrDirectLighting result = (PbrDirectLighting)0;
        result.incidentRadiance = max(lightSample.incidentRadiance, 0.0f);
        result.visibility = saturate(lightSample.visibility);
        result.samplingPdf = max(
            lightSample.lightSelectionPdf * lightSample.directionalPdf,
            UVSR_MIN_PDF);
        return result;
    }
    PbrPreparedSurface preparedSurface = PreparePbrSurface(surface);
    lightSample.directionToLight = PbrSafeNormalize(
        lightSample.directionToLight, preparedSurface.geometricNormal);
    rejection = ClassifyPbrDirectSurfacePrepared(
        preparedSurface, lightSample.directionToLight);
    if (!LightingShouldEvaluate(rejection))
    {
        PbrDirectLighting result = (PbrDirectLighting)0;
        result.incidentRadiance = max(lightSample.incidentRadiance, 0.0f);
        result.visibility = saturate(lightSample.visibility);
        result.samplingPdf = max(
            lightSample.lightSelectionPdf * lightSample.directionalPdf,
            UVSR_MIN_PDF);
        return result;
    }
    return EvaluateDirectLightPrevalidated(
        PreparePbrMaterial(material), preparedSurface, lightSample);
}

#endif // UVSR_PBR_HLSLI
