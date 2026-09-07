#include "renderer_gpu_contract.h"
#include "renderer_pixel_readback_cb.h"
#include "directional_ray_visibility_cb.h"
#include "path_tracing_cb.h"
#include "pbr_deferred_lighting_cb.h"
#include "ray_traced_flashlight_shadows_cb.h"
#include "ray_traced_sky_visibility_cb.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#define GPU_POD(type, bytes) \
    static_assert(sizeof(type) == bytes && alignof(type) == 4 && std::is_trivial_v<type> && \
        std::is_standard_layout_v<type> && std::is_trivially_copyable_v<type>)
GPU_POD(uvsr::gpu_contract::Float2, 8);
GPU_POD(uvsr::gpu_contract::Float3, 12);
GPU_POD(uvsr::gpu_contract::Float4, 16);
GPU_POD(uvsr::gpu_contract::Int2, 8);
GPU_POD(uvsr::gpu_contract::Int4, 16);
GPU_POD(uvsr::gpu_contract::Uint2, 8);
GPU_POD(uvsr::gpu_contract::Uint3, 12);
GPU_POD(uvsr::gpu_contract::Uint4, 16);
GPU_POD(uvsr::gpu_contract::Float3x4, 48);
GPU_POD(uvsr::gpu_contract::Float4x4, 64);
GPU_POD(SceneVertex, 60);
static_assert(offsetof(SceneVertex, pos) == 0 && offsetof(SceneVertex, prevPos) == 12 &&
    offsetof(SceneVertex, texCoord) == 24 && offsetof(SceneVertex, normal) == 32 && offsetof(SceneVertex, tangent) == 44);
#undef GPU_POD

static_assert(MaterialDomain_Opaque == 0);
static_assert(MaterialDomain_AlphaTested == 1);
static_assert(MaterialDomain_AlphaBlended == 2);
static_assert(MaterialDomain_Transmissive == 3);
static_assert(MaterialDomain_TransmissiveAlphaTested == 4);
static_assert(MaterialDomain_TransmissiveAlphaBlended == 5);
static_assert(MaterialFlags_UseSpecularGlossModel == 0x001);
static_assert(MaterialFlags_DoubleSided == 0x002);
static_assert(MaterialFlags_UseMetalRoughOrSpecularTexture == 0x004);
static_assert(MaterialFlags_UseBaseOrDiffuseTexture == 0x008);
static_assert(MaterialFlags_UseEmissiveTexture == 0x010);
static_assert(MaterialFlags_UseNormalTexture == 0x020);
static_assert(MaterialFlags_UseOcclusionTexture == 0x040);
static_assert(MaterialFlags_UseTransmissionTexture == 0x080);
static_assert(MaterialFlags_MetalnessInRedChannel == 0x100);
static_assert(MaterialFlags_UseOpacityTexture == 0x200);
static_assert(MaterialFlags_SubsurfaceScattering == 0x400);
static_assert(MaterialFlags_Hair == 0x800);
static_assert(InstanceFlags_CurveDisjointOrthogonalTriangleStrips == 1u);
static_assert(InstanceFlags_CurveLinearSweptSpheres == 2u);
static_assert(UVSR_LIGHT_TYPE_NONE == 0);
static_assert(UVSR_LIGHT_TYPE_DIRECTIONAL == 1);
static_assert(UVSR_LIGHT_TYPE_SPOT == 2);
static_assert(UVSR_LIGHT_TYPE_POINT == 3);
static_assert(UVSR_DEFERRED_MAX_LIGHTS == 16);
static_assert(UVSR_DEFERRED_MAX_SHADOWS == 16);
static_assert(UVSR_DEFERRED_MAX_LIGHT_PROBES == 16);
static_assert(c_SizeOfTriangleIndices == 12u);
static_assert(c_SizeOfPosition == 12u);
static_assert(c_SizeOfTexcoord == 8u);
static_assert(c_SizeOfNormal == 4u);
static_assert(UVSR_GBUFFER_SPACE_MATERIAL == 0);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_CONSTANTS == 0);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE == 0);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_SPECULAR_TEXTURE == 1);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_NORMAL_TEXTURE == 2);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_EMISSIVE_TEXTURE == 3);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_OCCLUSION_TEXTURE == 4);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_TRANSMISSION_TEXTURE == 5);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE == 6);
static_assert(UVSR_GBUFFER_SPACE_INPUT == 1);
static_assert(UVSR_GBUFFER_BINDING_PUSH_CONSTANTS == 1);
static_assert(UVSR_GBUFFER_BINDING_INSTANCE_BUFFER == 10);
static_assert(UVSR_GBUFFER_BINDING_VERTEX_BUFFER == 11);
static_assert(UVSR_GBUFFER_SPACE_VIEW == 2);
static_assert(UVSR_GBUFFER_BINDING_VIEW_CONSTANTS == 2);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_SAMPLER == 0);
static_assert(UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH == 1u);
static_assert(UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND == 2u);
static_assert(UVSR_SKY_VISIBILITY_APPLY_NEITHER == 0u);
static_assert(UVSR_SKY_VISIBILITY_APPLY_DIFFUSE_IBL == 1u);
static_assert(UVSR_SKY_VISIBILITY_APPLY_SPECULAR_IBL == 2u);
static_assert(UVSR_SKY_VISIBILITY_APPLY_BOTH_IBL == 3u);

namespace
{
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Member
    {
        std::string name;
        size_t offset, bytes, elements;
        bool checked = false;
    };
    struct Layout
    {
        size_t bytes;
        std::vector<Member> members;
    };
    using Layouts = std::map<std::string, Layout>;

    template<class T>
    void AddLayout(Layouts& layouts, const char* name, std::initializer_list<Member> members)
    {
        static_assert(alignof(T) == 4 && std::is_trivial_v<T> && std::is_standard_layout_v<T> &&
            std::is_trivially_copyable_v<T>);
        size_t end = 0;
        std::set<std::string> names;
        for (const auto& member : members)
        {
            Require(member.offset == end && member.bytes > 0 && names.insert(member.name).second,
                std::string(name) + ": incomplete or duplicate C++ member list");
            end += member.bytes;
        }
        Require(end == sizeof(T), std::string(name) + ": C++ member list does not cover sizeof");
        Require(layouts.emplace(name, Layout{sizeof(T), members}).second, "duplicate C++ type");
    }

    Layouts HostLayouts()
    {
        Layouts layouts;
#define M(member) Member{#member, offsetof(C, member), sizeof(decltype(C::member)), std::extent_v<decltype(C::member)>}
#define LAYOUT(type, ...) { using C = type; AddLayout<C>(layouts, #type, {__VA_ARGS__}); }
        LAYOUT(PlanarViewConstants,
            M(matWorldToView), M(matViewToClip), M(matWorldToClip), M(matClipToView), M(matViewToWorld),
            M(matClipToWorld), M(matViewToClipNoOffset), M(matWorldToClipNoOffset), M(matClipToViewNoOffset),
            M(matClipToWorldNoOffset), M(viewportOrigin), M(viewportSize), M(viewportSizeInv),
            M(pixelOffset), M(clipToWindowScale), M(clipToWindowBias), M(windowToClipScale),
            M(windowToClipBias), M(cameraDirectionOrPosition));
        LAYOUT(GeometryData,
            M(numIndices), M(numVertices), M(indexBufferIndex), M(indexOffset), M(vertexBufferIndex),
            M(positionOffset), M(prevPositionOffset), M(texCoord1Offset), M(texCoord2Offset),
            M(normalOffset), M(tangentOffset), M(curveRadiusOffset), M(materialIndex), M(pad0), M(pad1),
            M(pad2));
        LAYOUT(InstanceData,
            M(flags), M(firstGeometryInstanceIndex), M(firstGeometryIndex), M(numGeometries), M(transform),
            M(prevTransform));
        LAYOUT(MaterialConstants,
            M(baseOrDiffuseColor), M(flags), M(specularColor), M(materialID), M(emissiveColor), M(domain),
            M(opacity), M(roughness), M(metalness), M(normalTextureScale), M(occlusionStrength),
            M(alphaCutoff), M(transmissionFactor), M(baseOrDiffuseTextureIndex),
            M(metalRoughOrSpecularTextureIndex), M(emissiveTextureIndex), M(normalTextureIndex),
            M(occlusionTextureIndex), M(transmissionTextureIndex), M(opacityTextureIndex),
            M(normalTextureTransformScale), M(padding1), M(sssScale), M(sssTransmissionColor),
            M(sssAnisotropy), M(sssScatteringColor), M(hairMelanin), M(hairBaseColor), M(hairMelaninRedness),
            M(hairLongitudinalRoughness), M(hairAzimuthalRoughness), M(hairIor), M(hairCuticleAngle),
            M(hairDiffuseReflectionTint), M(hairDiffuseReflectionWeight));
        LAYOUT(ShadowConstants,
            M(matWorldToUvzwShadow), M(shadowFadeScale), M(shadowFadeBias), M(shadowMapCenterUV),
            M(shadowFalloffDistance), M(shadowMapArrayIndex), M(shadowMapSizeTexels),
            M(shadowMapSizeTexelsInv));
        LAYOUT(LightConstants,
            M(direction), M(lightType), M(position), M(radius), M(color), M(intensity),
            M(angularSizeOrInvRange), M(innerAngle), M(outerAngle), M(outOfBoundsShadow), M(shadowCascades),
            M(perObjectShadows), M(shadowChannel));
        LAYOUT(LightProbeConstants,
            M(diffuseScale), M(specularScale), M(mipLevels), M(padding1), M(diffuseArrayIndex),
            M(specularArrayIndex), M(padding2), M(frustumPlanes));
        LAYOUT(DeferredLightingConstants,
            M(view), M(shadowMapTextureSize), M(enableAmbientOcclusion), M(padding), M(ambientColorTop),
            M(ambientColorBottom), M(numLights), M(numLightProbes), M(indirectDiffuseScale),
            M(indirectSpecularScale), M(randomOffset), M(padding2), M(noisePattern), M(lights), M(shadows),
            M(lightProbes));
        LAYOUT(GBufferFillConstants,
            M(view), M(viewPrev));
        LAYOUT(GBufferPushConstants,
            M(startInstanceLocation), M(startVertexLocation), M(positionOffset), M(prevPositionOffset),
            M(texCoordOffset), M(normalOffset), M(tangentOffset));
        LAYOUT(FlashlightBeamProfile,
            M(beamRightX), M(beamRightY), M(beamRightZ), M(shapeExponent), M(spillInnerCosine),
            M(spillOuterCosine), M(spillWeight), M(hotspotWeight), M(hotspotInnerCosine),
            M(hotspotOuterCosine), M(emitterRadiusMeters), M(active));
        LAYOUT(FlashlightBeamProfileBinding,
            M(profile), M(lightIndex), M(padding0), M(padding1), M(padding2));
        LAYOUT(DirectionalRayVisibilityConstants,
            M(view), M(directionToLightAndDistance), M(rayBias), M(depthQuantizationStep), M(reverseDepth),
            M(floatDepth), M(angularDiameter), M(sampleCount), M(sampleSequencePhase),
            M(sampleSequenceMode), M(noisePattern), M(padding1), M(padding2), M(padding3));
        LAYOUT(PathTracingConstants,
            M(view), M(flashlight), M(environmentScale), M(rayBias), M(maximumRayDistance),
            M(noisePattern), M(dispatchExtent), M(lightCount), M(flags), M(rayMaterialLimits),
            M(maximumBounces), M(minimumBounces), M(fireflyThreshold), M(fireflyFilter));
        LAYOUT(PbrDeferredLightingConstants,
            M(deferred), M(lightingDebugView), M(skyVisibilityApplication),
            M(directVisibilityLightIndices), M(flashlightLightIndex),
            M(padding), M(flashlightBeamProfile));
        LAYOUT(RayTracedFlashlightShadowConstants,
            M(view), M(lightPositionAndRange), M(lightDirectionAndEmitterRadius), M(beamProfile),
            M(depthQuantizationStep), M(rayBias), M(reverseDepth), M(floatDepth), M(sampleSequencePhase),
            M(sampleCount), M(noisePattern), M(sampleSequenceMode));
        LAYOUT(RayTracedSkyVisibilityConstants,
            M(view), M(sampleSequencePhase), M(sampleCount), M(noisePattern), M(rayDistance),
            M(depthQuantizationStep), M(rayBias), M(reverseDepth), M(floatDepth), M(sampleSequenceMode),
            M(padding0));
        LAYOUT(RendererPixelReadbackConstants,
            M(pixelX), M(pixelY), M(padding0), M(padding1));
#undef LAYOUT
#undef M
        return layouts;
    }

    struct Frame
    {
        std::string type;
        std::vector<Member> members;
    };

    std::set<std::string> VerifyDump(const std::string& text, const Layouts& layouts)
    {
        const size_t begin = text.find("; Buffer Definitions:");
        const size_t end = text.find("; Resource Bindings:", begin);
        Require(begin != std::string::npos && end != std::string::npos && end > begin, "missing DXIL layout boundaries");
        std::istringstream lines(text.substr(begin + 21, end - begin - 21));
        const std::regex typePattern(R"(^struct ([A-Za-z_][A-Za-z0-9_.:]*)$)");
        const std::regex scalarPattern(R"(^(?:row_major )?(?:float|int|uint)([1-4])?(?:x([1-4]))?$)");
        const std::regex memberPattern(
            R"(^.*?([A-Za-z_$][A-Za-z0-9_$]*)(?:\[([0-9]+)\])?;;?[ ]*; Offset:[ ]*([0-9]+)(?: Size:[ ]*([0-9]+))?[ ]*$)");
        std::vector<Frame> stack;
        std::set<std::string> seen;
        std::string pending, line;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            Require(line.front() == ';', "malformed DXIL layout line");
            line.erase(0, 1);
            const size_t first = line.find_first_not_of(' ');
            if (first == std::string::npos)
                continue;
            line = line.substr(first);
            std::smatch match;
            if (std::regex_match(line, match, typePattern))
            {
                Require(pending.empty(), "DXIL type omitted its opening brace");
                pending = match[1].str();
                pending = pending.substr(pending.find_last_of('.') + 1);
                continue;
            }
            if (line == "{")
            {
                Require(!pending.empty() || stack.empty(), "unexpected DXIL opening brace");
                stack.push_back({pending, {}});
                pending.clear();
                continue;
            }
            if (stack.empty())
            {
                Require(pending.empty() && (line.rfind("cbuffer ", 0) == 0 ||
                    line.rfind("Resource bind info for ", 0) == 0), "unexpected DXIL layout content");
                continue;
            }
            if (line == "}" && stack.back().type.empty())
            {
                stack.pop_back();
                continue;
            }
            Require(pending.empty() && std::regex_match(line, match, memberPattern), "malformed DXIL member or closing brace: " + line);
            Member member{match[1].str(), std::stoull(match[3].str()), 0,
                match[2].matched ? std::stoull(match[2].str()) : 0};
            const size_t bytes = match[4].matched ? std::stoull(match[4].str()) : 0;
            if (line.front() == '}')
            {
                Require(!stack.back().type.empty(), "untyped DXIL closing member");
                Frame frame = std::move(stack.back());
                stack.pop_back();
                const auto expected = layouts.find(frame.type);
                if (expected != layouts.end())
                {
                    const auto& host = expected->second;
                    Require(frame.members.size() == host.members.size(), frame.type + ": missing or extra DXIL members");
                    for (size_t index = 0; index < host.members.size(); ++index)
                    {
                        const auto& observed = frame.members[index];
                        const auto& wanted = host.members[index];
                        Require(observed.name == wanted.name && observed.offset >= member.offset &&
                            observed.offset - member.offset == wanted.offset && observed.elements == wanted.elements &&
                            observed.bytes * (observed.elements ? observed.elements : 1) == wanted.bytes,
                            frame.type + "." + wanted.name + ": DXIL member layout differs from C++");
                    }
                    member.bytes = host.bytes;
                    member.checked = true;
                    Require(!bytes || bytes == host.bytes * (member.elements ? member.elements : 1),
                        frame.type + ": DXIL size differs from sizeof");
                    seen.insert(frame.type);
                }
                else if (bytes && !frame.members.empty() && std::all_of(frame.members.begin(), frame.members.end(),
                    [](const auto& child) { return child.checked; }))
                {
                    const auto& last = frame.members.back();
                    Require(last.offset >= member.offset && bytes == last.offset - member.offset +
                        last.bytes * (last.elements ? last.elements : 1), frame.type + ": cbuffer size differs from C++");
                    if (frame.members.size() == 1)
                        Require(frame.members[0].offset == 0, frame.type + ": cbuffer root is not at zero");
                }
            }
            else
            {
                const size_t name = static_cast<size_t>(match.position(1));
                const std::string declaration = line.substr(0, name ? name - 1 : 0);
                std::smatch scalar;
                if (std::regex_match(declaration, scalar, scalarPattern))
                    member.bytes = 4 * (scalar[1].matched ? std::stoull(scalar[1].str()) : 1) *
                        (scalar[2].matched ? std::stoull(scalar[2].str()) : 1);
            }
            Require(!stack.empty(), "DXIL member has no containing block");
            auto& siblings = stack.back().members;
            Require(std::none_of(siblings.begin(), siblings.end(),
                [&](const auto& sibling) { return sibling.name == member.name; }), "duplicate DXIL member");
            siblings.push_back(std::move(member));
        }
        Require(stack.empty() && pending.empty(), "unterminated DXIL structure");
        return seen;
    }

    void ByteGoldens()
    {
        GeometryData geometry{};
        geometry.indexBufferIndex = -3;
        geometry.materialIndex = 0x01020304;
        std::array<uint32_t, 16> words{};
        static_assert(sizeof(geometry) == sizeof(words));
        std::memcpy(words.data(), &geometry, sizeof(geometry));
        Require(words[2] == 0xfffffffdu && words[12] == 0x01020304u, "geometry buffer byte positions changed");
        const uvsr::gpu_contract::Float4 floating{1.f, -2.f, 3.5f, -4.25f};
        uvsr::gpu_contract::Int4 integer{{1, -2, 3, -4}};
        integer[2] = 5;
        std::array<uint32_t, 4> floats{}, integers{};
        std::memcpy(floats.data(), &floating, sizeof(floating));
        std::memcpy(integers.data(), &integer, sizeof(integer));
        Require(floats == std::array<uint32_t, 4>{0x3f800000u, 0xc0000000u, 0x40600000u, 0xc0880000u} &&
            integers == std::array<uint32_t, 4>{1u, 0xfffffffeu, 5u, 0xfffffffcu},
            "scalar bit patterns, signed integer storage or Int4 mutation changed");
        DeferredLightingConstants deferred{};
        const auto* base = reinterpret_cast<const std::byte*>(&deferred);
        Require(reinterpret_cast<const std::byte*>(&deferred.lights[15]) - base == 2544 &&
            reinterpret_cast<const std::byte*>(&deferred.shadows[15]) - base == 4336 &&
            reinterpret_cast<const std::byte*>(&deferred.lightProbes[15]) - base == 6368,
            "deferred array boundary bytes changed");
    }

    void RejectCorruption(const std::string& original, const Layouts& layouts)
    {
        const size_t token = original.find("matWorldToView;");
        Require(token != std::string::npos, "GPU probe omitted its real matrix member");
        const size_t begin = original.rfind('\n', token) + 1;
        const size_t end = original.find('\n', token);
        Require(end != std::string::npos, "GPU probe matrix line is incomplete");
        const size_t close = original.find("} g_View;");
        const size_t size = original.find("Size:");
        const size_t array = original.find("frustumPlanes[6]");
        Require(close != std::string::npos && size != std::string::npos && array != std::string::npos,
            "GPU probe omitted its structure, size or array evidence");
        for (unsigned corruption = 0; corruption < 8; ++corruption)
        {
            std::string changed = original;
            if (corruption == 0)
                changed.erase(begin, end + 1 - begin);
            else if (corruption == 1)
                changed.insert(begin, original.substr(begin, end + 1 - begin));
            else if (corruption == 2)
                changed.insert(end, "bad-offset");
            else if (corruption == 3)
                changed.erase(close, 1);
            else if (corruption == 4 || corruption == 5)
            {
                const size_t value = changed.find_first_of("0123456789",
                    corruption == 4 ? changed.find("Offset:", token) : size);
                const size_t after = changed.find_first_not_of("0123456789", value);
                Require(value != std::string::npos && after != std::string::npos, "probe number is incomplete");
                changed.replace(value, after - value, "999999");
            }
            else if (corruption == 6)
                changed.replace(array, 16, "frustumPlanes[5]");
            else
                changed.replace(changed.rfind("float4x4", token), 8, "float3x4");
            bool rejected = false;
            try { (void)VerifyDump(changed, layouts); }
            catch (const std::exception&) { rejected = true; }
            Require(rejected, "DXIL parser accepted incomplete, duplicated or changed ABI evidence");
        }
    }
}

int main(int argc, char** argv)
{
    const char* dumpPath = "host";
    try
    {
        Require(argc > 1, "expected actual compiled DXIL dump paths");
        ByteGoldens();
        const auto layouts = HostLayouts();
        std::set<std::string> seen;
        for (int argument = 1; argument < argc; ++argument)
        {
            dumpPath = argv[argument];
            std::ifstream input(dumpPath, std::ios::binary);
            Require(bool(input), "cannot open DXIL dump");
            const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            const auto observed = VerifyDump(text, layouts);
            seen.insert(observed.begin(), observed.end());
            if (argument == 1)
                RejectCorruption(text, layouts);
        }
        dumpPath = "compiled type inventory";
        Require(seen.size() == layouts.size(), "compiled DXIL omitted an expected C++ contract type");
        for (const auto& [name, layout] : layouts)
        {
            std::cout << name << " sizeof=" << layout.bytes << '\n';
            for (const auto& member : layout.members)
                std::cout << "  " << member.name << " offsetof=" << member.offset <<
                    " sizeof=" << member.bytes << " elements=" << member.elements << '\n';
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "GPU ABI " << dumpPath << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
