#include "renderer_scene.h"
#include "renderer_scene_draw.h"
#include "renderer_scene_ray.h"
#include "renderer_scene_encoding.h"
#include "renderer_scene_light.h"
#include "camera_collision.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error the portable scene consumer requires exception-disabled compilation
#endif

namespace
{
    using namespace uvsr;

    void Require(bool condition, const char* message)
    {
        if (condition) return;
        fprintf(stderr, "portable scene: %s\n", message);
        exit(1);
    }

    void PrepareScene(RendererScene& scene, uint64_t generation)
    {
        RendererSceneCounts counts;
        counts.nodes = 2;
        counts.instances = counts.meshes = counts.geometries = counts.materials = counts.bufferGroups = 1;
        Require(scene.Prepare(counts).Succeeded(), "scene preparation");
        RendererSceneNode nodes[2];
        nodes[0].firstChildIndex = 1;
        nodes[1].parentIndex = 0;
        nodes[1].leafKind = RendererSceneLeafKind::Instance;
        nodes[1].leafIndex = 0;
        nodes[1].hasLocalTransform = true;
        nodes[1].transform.translation[0] = 2;
        RendererSceneInstance instance;
        instance.nodeIndex = 1;
        instance.meshIndex = 0;
        RendererSceneMesh mesh;
        mesh.bufferGroupIndex = 0;
        mesh.geometries = {0,1};
        mesh.vertexCount = mesh.indexCount = 3;
        RendererSceneGeometry geometry;
        geometry.materialIndex = 0;
        geometry.vertexCount = geometry.indexCount = 3;
        geometry.objectBounds = {{0,-2,-2},{0,2,2},false};
        RendererSceneMaterial material;
        material.selectionId = 45;
        RendererSceneBufferGroup buffers;
        buffers.indexBytes = 12;
        buffers.vertexBytes = 36;
        buffers.attributes[uint32_t(RendererSceneVertexAttribute::Position)] = {0,36};
        Require(scene.Write(0,nodes[0]).Succeeded() && scene.Write(1,nodes[1]).Succeeded() &&
            scene.Write(0,instance).Succeeded() && scene.Write(0,mesh).Succeeded() &&
            scene.Write(0,geometry).Succeeded() && scene.Write(0,material).Succeeded() &&
            scene.Write(0,buffers).Succeeded(), "scene input copies");
        uint8_t workspace[2];
        Require(scene.Seal(0,workspace).Succeeded() && scene.Publish(generation).Succeeded(), "scene publication");
    }
}

int main()
{
    RendererScene scene;
    PrepareScene(scene,41);
    auto view = scene.View();
    RendererSceneDrawList draws;
    RendererSceneRaySelection rays;
    const RendererSceneFrustum frustum{{
        {{1,0,0},10},{{-1,0,0},10},{{0,1,0},10},{{0,-1,0},10},{{0,0,1},10},{{0,0,-1},10}}};
    Require(draws.Prepare(view).Succeeded() && draws.Build(view,frustum).Succeeded() && draws.View().count == 1 &&
        draws.View().data[0].instance == 0, "canonical raster selection");
    Require(rays.Prepare(view).Succeeded() && rays.View().instances.count == 1 && rays.View().geometries.data[0].opaque,
        "canonical ray selection");
    InstanceData encodedInstance;
    GeometryData encodedGeometry;
    MaterialConstants encodedMaterial;
    Require(EncodeRendererSceneInstance(view,0,0,encodedInstance).Succeeded() &&
        encodedInstance.transform.values[3] == 2 && encodedInstance.prevTransform.values[3] == 0,
        "independent current and supplied previous snapshots");
    Require(EncodeRendererSceneGeometry(view,0,0,{5,6},16,encodedGeometry).Succeeded() &&
        encodedGeometry.materialIndex == 0 && encodedGeometry.indexBufferIndex == 5,
        "plain backend-resolved descriptor indices");
    Require(EncodeRendererSceneMaterial(view.materials.data[0].values,45,{},0,encodedMaterial).Succeeded() &&
        encodedMaterial.materialID == 45, "shared material encoding");
    const uint32_t indices[]{0,1,2};
    const CameraCollisionWorld::Point vertices[]{{0,-2,-2},{0,2,-2},{0,0,2}};
    const CameraCollisionSourceBuffers buffers{{reinterpret_cast<const unsigned char*>(indices),sizeof(indices)},
        {reinterpret_cast<const unsigned char*>(vertices),sizeof(vertices)}};
    CameraCollisionWorld collision;
    Require(collision.BuildFromScene(view,{&buffers,1}) == CameraCollisionBuildError::None, "canonical collision preparation");
    const auto stopped = collision.MoveSphere({0,0,0},{4,0,0},0.25f);
    Require(stopped.x > 1.74f && stopped.x < 1.751f, "collision and encoded instance share the translated wall");

    auto values = view.materials.data[0].values;
    values.roughness = 0.25f;
    Require(scene.SetMaterial({41,0},values).changed && rays.Matches(scene.View()), "roughness retains ray classification");
    values.domain = RendererMaterialDomain::AlphaTested;
    Require(scene.SetMaterial({41,0},values).changed && !rays.Matches(scene.View()) &&
        rays.Prepare(scene.View()).Succeeded() && !rays.View().geometries.data[0].opaque,
        "alpha changes invalidate and rebuild ray classification");
    Require(ClassifyPathTracingSceneDomain(scene.View()) == PathTracingSceneDomainStatus::Supported,
        "canonical alpha-tested path domain");
    for (uint32_t mode = 0; mode < 4; ++mode)
    {
        auto changed = values;
        if (mode == 0) changed.transmissionFactor = .5f;
        if (mode == 1) changed.enableSubsurfaceScattering = true;
        if (mode == 2) changed.enableHair = true;
        if (mode == 3) changed.domain = RendererMaterialDomain::AlphaBlended;
        Require(scene.SetMaterial({41,0}, changed).changed &&
            ClassifyPathTracingSceneDomain(scene.View()) == (mode == 3 ?
                PathTracingSceneDomainStatus::BlendedGeometryOmitted : PathTracingSceneDomainStatus::Unsupported),
            "path eligibility consumes current canonical material commands immediately");
        Require(!scene.SetMaterial({40,0}, values).Succeeded() &&
            ClassifyPathTracingSceneDomain(scene.View()) != PathTracingSceneDomainStatus::Supported,
            "rejected material command leaves domain unchanged");
        Require(scene.SetMaterial({41,0}, values).changed &&
            ClassifyPathTracingSceneDomain(scene.View()) == PathTracingSceneDomainStatus::Supported,
            "restored material immediately restores path eligibility");
    }
    Require(scene.AdvancePreviousTransforms().changed, "explicit submitted-snapshot advance");
    auto transform = view.nodes.data[1].transform;
    transform.translation[0] = 5;
    Require(scene.SetTransform({41,1},transform).changed && rays.Matches(scene.View()), "motion retains ray membership");
    view = scene.View();
    Require(draws.Build(view,frustum).Succeeded() && EncodeRendererSceneInstance(view,0,0,encodedInstance).Succeeded() &&
        encodedInstance.transform.values[3] == 5 && encodedInstance.prevTransform.values[3] == 2,
        "derived draw and temporal instance state after a transform transaction");
    // collision is a prepared geometry snapshot. a consumer adding mesh motion
    // must rebuild it before using the moved geometry for camera queries.
    Require(collision.BuildFromScene(view,{&buffers,1}) == CameraCollisionBuildError::None, "collision replacement after geometry motion");
    const auto movedWall = collision.MoveSphere({0,0,0},{7,0,0},0.25f);
    Require(movedWall.x > 4.74f && movedWall.x < 4.751f, "reprepared collision uses the new instance world");

    draws.Reset();
    rays.Reset();
    collision.Clear();
    view = {};
    scene.Reset();
    PrepareScene(scene,42);
    Require(!FindRendererSceneNode(scene.View(),{41,1}) &&
        scene.SetMaterial({41,0},values).error == RendererSceneError::Generation,
        "replacement rejects the retired scene identity");
    puts("portable scene: canonical selection, encoding, collision and invalidation passed without graphics or parser dependencies");
    return 0;
}
