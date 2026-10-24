/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "array_view.h"
#include "renderer_gpu_scalar.h"
#include "renderer_material_contract.h"

namespace uvsr
{
    constexpr uint32_t InvalidSceneIndex = UINT32_MAX;

    struct RendererSceneRange
    {
        uint32_t first = 0;
        uint32_t count = 0;
    };

    struct RendererSceneString
    {
        // UTF-8 byte span. consumers use length, not an assumed trailing zero.
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    struct RendererSceneByteRange
    {
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    struct RendererSceneBounds
    {
        gpu_contract::Float3 minimum{};
        gpu_contract::Float3 maximum{};
        bool empty = true;
    };

    // row-major linear lanes for row vectors. local * parent produces world.
    struct RendererSceneAffine
    {
        double linear[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
        double translation[3]{};
    };

    struct RendererSceneTransform
    {
        double translation[3]{};
        // raw static quaternion XYZW, matching imported affine semantics.
        // animation normalizes evaluated rotations before submitting a transform.
        double rotation[4]{0, 0, 0, 1};
        double scaling[3]{1, 1, 1};
    };

    enum class RendererSceneLeafKind : uint8_t
    {
        None,
        Instance,
        Light,
        Camera,
        Animation,
        Count
    };

    enum RendererSceneContent : uint32_t
    {
        SceneContentNone = 0,
        SceneContentOpaque = 1u << 0,
        SceneContentAlphaTested = 1u << 1,
        SceneContentBlended = 1u << 2,
        SceneContentLight = 1u << 3,
        SceneContentCamera = 1u << 4,
        SceneContentAnimation = 1u << 5
    };

    struct RendererSceneNode
    {
        RendererSceneString name;
        uint32_t parentIndex = InvalidSceneIndex;
        uint32_t firstChildIndex = InvalidSceneIndex;
        uint32_t nextSiblingIndex = InvalidSceneIndex;
        RendererSceneLeafKind leafKind = RendererSceneLeafKind::None;
        uint32_t leafIndex = InvalidSceneIndex;
        // current TRS is authoritative. previous affines are temporal snapshots,
        // not recomposed through a possibly changed hierarchy.
        RendererSceneTransform transform;
        bool hasLocalTransform = false;
        RendererSceneAffine local;
        RendererSceneAffine world;
        RendererSceneAffine previousLocal;
        RendererSceneAffine previousWorld;
        // current local/world, bounds and traversal are derived.
        RendererSceneBounds worldBounds;
        uint32_t preorderIndex = InvalidSceneIndex;
        uint32_t subtreeEnd = 0;
        uint32_t leafContent = SceneContentNone;
        uint32_t subtreeContent = SceneContentNone;
    };

    enum class RendererScenePrimitive : uint8_t
    {
        Triangles,
        Lines,
        LineStrip,
        Count
    };

    enum class RendererSceneMeshType : uint8_t
    {
        Triangles,
        CurvePolytubes,
        CurveDisjointOrthogonalTriangleStrips,
        CurveLinearSweptSpheres,
        Count
    };

    enum class RendererSceneVertexAttribute : uint8_t
    {
        Position,
        PreviousPosition,
        TexCoord0,
        TexCoord1,
        Normal,
        Tangent,
        JointIndices,
        JointWeights,
        CurveRadius,
        Count
    };

    struct RendererSceneBufferGroup
    {
        uint64_t indexBytes = 0;
        uint64_t vertexBytes = 0;
        uint64_t morphBytes = 0;
        // position/previous: float3. UV: float2. normal/tangent: packed signed
        // normalized bytes. joints: four uint16 indices and float4 weights.
        // curve radius: float. transforms belong to the instance stream.
        RendererSceneByteRange attributes[uint32_t(RendererSceneVertexAttribute::Count)]{};
        RendererSceneRange morphRanges;
    };

    struct RendererSceneGeometry
    {
        uint32_t materialIndex = InvalidSceneIndex;
        RendererSceneBounds objectBounds;
        uint32_t indexOffsetInMesh = 0;
        uint32_t vertexOffsetInMesh = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        RendererScenePrimitive primitive = RendererScenePrimitive::Triangles;
    };

    struct RendererSceneMesh
    {
        RendererSceneString name;
        RendererSceneMeshType type = RendererSceneMeshType::Triangles;
        uint32_t bufferGroupIndex = InvalidSceneIndex;
        uint32_t skinPrototypeIndex = InvalidSceneIndex;
        RendererSceneRange geometries;
        // a declared import envelope must contain all geometry bounds. otherwise
        // Seal derives the union, including on retry after a later check fails.
        RendererSceneBounds objectBounds;
        bool hasDeclaredBounds = false;
        // mesh and geometry offsets count elements, unlike attribute byte ranges.
        uint32_t indexOffset = 0;
        uint32_t vertexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        bool morphTargetAnimation = false;
        bool isSkinPrototype = false;
    };

    struct RendererSceneInstance
    {
        uint32_t nodeIndex = InvalidSceneIndex;
        uint32_t meshIndex = InvalidSceneIndex;
        RendererSceneRange joints;
    };

    struct RendererSceneJoint
    {
        uint32_t nodeIndex = InvalidSceneIndex;
        gpu_contract::Float4x4 inverseBind{};
    };

    enum class RendererSceneMaterialTextureSlot : uint8_t
    {
        BaseOrDiffuse,
        MetalRoughOrSpecular,
        Normal,
        Emissive,
        Occlusion,
        Transmission,
        Opacity,
        Count
    };

    struct RendererSceneMaterialValues
    {
        RendererMaterialDomain domain = RendererMaterialDomain::Opaque;
        uint32_t textures[uint32_t(RendererSceneMaterialTextureSlot::Count)]{
            InvalidSceneIndex, InvalidSceneIndex, InvalidSceneIndex,
            InvalidSceneIndex, InvalidSceneIndex, InvalidSceneIndex, InvalidSceneIndex};
        gpu_contract::Float3 baseOrDiffuseColor{1, 1, 1};
        gpu_contract::Float3 specularColor{};
        gpu_contract::Float3 emissiveColor{};
        float emissiveIntensity = 1;
        float metalness = 0;
        float roughness = 0;
        float opacity = 1;
        float alphaCutoff = 0.5f;
        float transmissionFactor = 0;
        float normalTextureScale = 1;
        float occlusionStrength = 1;
        gpu_contract::Float2 normalTextureTransformScale{1, 1};
        bool useSpecularGlossModel = false;
        bool enableSubsurfaceScattering = false;
        struct Subsurface
        {
            gpu_contract::Float3 transmissionColor{0.5f, 0.5f, 0.5f};
            gpu_contract::Float3 scatteringColor{0.5f, 0.5f, 0.5f};
            float scale = 1;
            float anisotropy = 0;
        } subsurface;
        bool enableHair = false;
        struct Hair
        {
            gpu_contract::Float3 baseColor{1, 1, 1};
            float melanin = 0.5f;
            float melaninRedness = 0.5f;
            float longitudinalRoughness = 0.25f;
            float azimuthalRoughness = 0.6f;
            float diffuseReflectionWeight = 0;
            gpu_contract::Float3 diffuseReflectionTint{};
            float ior = 1.55f;
            float cuticleAngle = 3;
        } hair;
        bool enableBaseOrDiffuseTexture = true;
        bool enableMetalRoughOrSpecularTexture = true;
        bool enableNormalTexture = true;
        bool enableEmissiveTexture = true;
        bool enableOcclusionTexture = true;
        bool enableTransmissionTexture = true;
        bool enableOpacityTexture = true;
        bool doubleSided = false;
        bool metalnessInRedChannel = false;
    };

    struct RendererSceneMaterial
    {
        // existing numeric UI/snapshot identity. it is independent of the table
        // index and stays unchanged by edits or authored-value reset.
        uint32_t selectionId = InvalidSceneIndex;
        RendererSceneString name;
        RendererSceneString modelFileName;
        int32_t materialIndexInModel = -1;
        RendererSceneMaterialValues values;
        RendererSceneMaterialValues originalValues;
    };

    enum class RendererSceneTextureAlpha : uint8_t
    {
        Unknown,
        Straight,
        Premultiplied,
        Opaque,
        Custom,
        Count
    };

    struct RendererSceneTexture
    {
        RendererSceneString path;
        RendererSceneString mimeType;
        RendererSceneTextureAlpha alpha = RendererSceneTextureAlpha::Unknown;
        uint32_t originalBitsPerPixel = 0;
    };

    enum class RendererSceneLightKind : uint8_t
    {
        Directional,
        Point,
        Spot,
        Count
    };

    struct RendererSceneLightValues
    {
        gpu_contract::Float3 color{1, 1, 1};
        float irradiance = 1;
        float angularSize = 0; // degrees, as are the spot cone angles.
        float intensity = 1;
        float radius = 0;
        float range = 0;
        float innerAngle = 180;
        float outerAngle = 180;
    };

    struct RendererSceneLight
    {
        uint32_t nodeIndex = InvalidSceneIndex;
        RendererSceneLightKind kind = RendererSceneLightKind::Directional;
        RendererSceneLightValues values;
    };

    enum class RendererSceneCameraKind : uint8_t
    {
        Perspective,
        Orthographic,
        Count
    };

    struct RendererSceneCamera
    {
        uint32_t nodeIndex = InvalidSceneIndex;
        RendererSceneCameraKind kind = RendererSceneCameraKind::Perspective;
        float nearPlane = 0;
        float farPlane = 1;
        float verticalFov = 1; // radians.
        float aspectRatio = 1;
        float xMagnitude = 1;
        float yMagnitude = 1;
        bool hasFarPlane = false;
        bool hasAspectRatio = false;
    };

    enum class RendererSceneAnimationAttribute : uint8_t
    {
        Undefined,
        Scaling,
        Rotation,
        Translation,
        LeafProperty,
        Count
    };

    enum class RendererSceneInterpolation : uint8_t
    {
        Step,
        Linear,
        Slerp,
        CatmullRomSpline,
        HermiteSpline,
        Count
    };

    struct RendererSceneAnimation
    {
        uint32_t nodeIndex = InvalidSceneIndex;
        RendererSceneRange channels;
        float duration = 0; // derived maximum sampler endpoint, with zero as its lower bound.
    };

    struct RendererSceneAnimationChannel
    {
        // exactly one target. only LeafProperty may target a material.
        uint32_t nodeIndex = InvalidSceneIndex;
        uint32_t materialIndex = InvalidSceneIndex;
        uint32_t samplerIndex = InvalidSceneIndex;
        RendererSceneAnimationAttribute attribute = RendererSceneAnimationAttribute::Undefined;
        RendererSceneString property;
    };

    struct RendererSceneAnimationSampler
    {
        RendererSceneRange keyframes;
        RendererSceneInterpolation interpolation = RendererSceneInterpolation::Step;
    };

    struct RendererSceneKeyframe
    {
        float time = 0;
        gpu_contract::Float4 value{};
        gpu_contract::Float4 inTangent{};
        gpu_contract::Float4 outTangent{};
    };
}
