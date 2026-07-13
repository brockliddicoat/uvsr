#ifndef UVSR_PBR_HLSLI
#define UVSR_PBR_HLSLI

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
static const uint PbrFeature_Dispersion = 1u << 7;

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
    float distance;
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

float PerceptualRoughnessToAlpha(float perceptualRoughness)
{
    float roughness = saturate(perceptualRoughness);
    return max(roughness * roughness, UVSR_MIN_ALPHA);
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

PbrBsdfEvaluation EvaluateBsdf(
    PbrMaterialParameters material,
    PbrSurfaceInteraction surface,
    float3 lightDirection)
{
    PbrBsdfEvaluation result = (PbrBsdfEvaluation)0;

    float3 geometricNormal = PbrSafeNormalize(surface.geometricNormal, float3(0.0f, 1.0f, 0.0f));
    float3 shadingNormal = PbrSafeNormalize(surface.shadingNormal, geometricNormal);
    float3 viewDirection = PbrSafeNormalize(surface.viewDirection, geometricNormal);
    float3 directionToLight = PbrSafeNormalize(lightDirection, geometricNormal);

    // Geometric-normal checks prevent perturbed normals from emitting energy
    // through the back of a surface. A full shading-normal energy correction
    // is intentionally deferred until transport paths require it.
    float geometricNoV = dot(geometricNormal, viewDirection);
    float geometricNoL = dot(geometricNormal, directionToLight);
    float NoV = dot(shadingNormal, viewDirection);
    float NoL = dot(shadingNormal, directionToLight);
    if (geometricNoV <= UVSR_MIN_COSINE || geometricNoL <= UVSR_MIN_COSINE ||
        NoV <= UVSR_MIN_COSINE || NoL <= UVSR_MIN_COSINE)
        return result;

    float3 halfVector = PbrSafeNormalize(viewDirection + directionToLight, shadingNormal);
    float NoH = saturate(dot(shadingNormal, halfVector));
    float VoH = saturate(dot(viewDirection, halfVector));
    float alpha = PerceptualRoughnessToAlpha(material.perceptualRoughness);
    float3 specularF0 = lerp(material.dielectricF0.xxx, material.baseColor, material.metalness);
    float3 fresnel = FresnelSchlick(VoH, specularF0);

    float distribution = D_GGX(NoH, alpha);
    float visibility = V_SmithGGXCorrelated(NoV, NoL, alpha);
    result.diffuse = EvaluateLambert(material, fresnel);
    result.specular = EvaluateGGX(distribution, visibility, fresnel);
    result.total = result.diffuse + result.specular;
    result.diffusePdf = PdfLambert(NoL);
    result.specularPdf = PdfGGX(NoH, VoH, alpha);
    return result;
}

PbrDirectLighting EvaluateDirectLight(
    PbrMaterialParameters material,
    PbrSurfaceInteraction surface,
    PbrLightSample lightSample)
{
    PbrDirectLighting result = (PbrDirectLighting)0;
    result.incidentRadiance = max(lightSample.incidentRadiance, 0.0f);
    result.visibility = saturate(lightSample.visibility);
    result.samplingPdf = max(lightSample.lightSelectionPdf * lightSample.directionalPdf, UVSR_MIN_PDF);

    PbrBsdfEvaluation bsdf = EvaluateBsdf(material, surface, lightSample.directionToLight);
    float3 shadingNormal = PbrSafeNormalize(surface.shadingNormal, surface.geometricNormal);
    result.cosineTerm = saturate(dot(shadingNormal, lightSample.directionToLight));
    result.sampleWeight = result.cosineTerm * result.visibility / result.samplingPdf;
    result.diffuse = result.incidentRadiance * bsdf.diffuse * result.sampleWeight;
    result.specular = result.incidentRadiance * bsdf.specular * result.sampleWeight;
    result.total = result.diffuse + result.specular;
    return result;
}

#endif // UVSR_PBR_HLSLI
