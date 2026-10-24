"""Convert one supported downloaded OBJ package into a UVSR-ready GLB."""

import argparse
import hashlib
import json
import math
import sys
import time
from pathlib import Path

import bpy
from mathutils import Vector


SCENES = {
    "living_room": {
        "display_name": "Living Room",
        "scale": 1.0,
        "copyright": "Living Room source package; local UVSR conversion",
    },
    "sponza": {
        "display_name": "Sponza",
        "scale": 0.01,
        "copyright": (
            "Sponza by Marko Dabrovic and Frank Meinl/Crytek; "
            "2011 corrections by Morgan McGuire"
        ),
    },
}


def parse_arguments():
    blender_arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--scene", choices=sorted(SCENES), required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    return parser.parse_args(blender_arguments)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def obj_face_report(path):
    face_polygons = 0
    triangulated_faces = 0
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if not line.startswith("f "):
                continue
            face_polygons += 1
            vertex_count = len(line.split()) - 1
            if vertex_count >= 3:
                triangulated_faces += vertex_count - 2
    return {
        "face_polygons": face_polygons,
        "triangulated_faces": triangulated_faces,
    }


def selected_meshes():
    return [item for item in bpy.context.scene.objects if item.type == "MESH"]


def scene_bounds(objects):
    minimum = Vector((math.inf, math.inf, math.inf))
    maximum = Vector((-math.inf, -math.inf, -math.inf))
    for item in objects:
        for corner in item.bound_box:
            world_corner = item.matrix_world @ Vector(corner)
            minimum.x = min(minimum.x, world_corner.x)
            minimum.y = min(minimum.y, world_corner.y)
            minimum.z = min(minimum.z, world_corner.z)
            maximum.x = max(maximum.x, world_corner.x)
            maximum.y = max(maximum.y, world_corner.y)
            maximum.z = max(maximum.z, world_corner.z)
    return {
        "minimum": [round(value, 6) for value in minimum],
        "maximum": [round(value, 6) for value in maximum],
        "dimensions": [round(maximum[index] - minimum[index], 6) for index in range(3)],
    }


def image_report():
    report = []
    missing = []
    for image in sorted(bpy.data.images, key=lambda value: value.name.casefold()):
        if image.type in {"RENDER_RESULT", "COMPOSITING"}:
            continue
        resolved = Path(bpy.path.abspath(image.filepath, library=image.library))
        exists = image.packed_file is not None or resolved.is_file()
        item = {
            "name": image.name,
            "source": image.source,
            "path": str(resolved),
            "exists_or_packed": exists,
        }
        report.append(item)
        if not exists:
            missing.append(item)
    return report, missing


def mesh_report(objects):
    triangles = 0
    vertices = 0
    polygons = 0
    missing_corner_normals = []
    for item in objects:
        mesh = item.data
        mesh.validate(verbose=False, clean_customdata=False)
        mesh.update(calc_edges=True)
        mesh.calc_loop_triangles()
        triangles += len(mesh.loop_triangles)
        vertices += len(mesh.vertices)
        polygons += len(mesh.polygons)
        if not mesh.has_custom_normals and len(mesh.corner_normals) == 0:
            missing_corner_normals.append(item.name)
    return {
        "objects": len(objects),
        "vertices": vertices,
        "polygons": polygons,
        "triangles": triangles,
        "materials": len(bpy.data.materials),
        "missing_corner_normals_after_import": missing_corner_normals,
    }


def normalize_materials(scene):
    changes = {
        "alpha_masks": [],
        "disabled_transmission": [],
        "forced_opaque": [],
        "omitted_height_maps": [],
    }
    for material in sorted(bpy.data.materials, key=lambda value: value.name.casefold()):
        if not material.use_nodes or material.node_tree is None:
            continue

        principled_nodes = [
            node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED"
        ]
        for principled in principled_nodes:
            transmission = principled.inputs.get("Transmission Weight")
            if transmission is not None and transmission.default_value > 0.0:
                if scene == "sponza":
                    transmission.default_value = 0.0
                    changes["disabled_transmission"].append(material.name)

            alpha = principled.inputs.get("Alpha")
            if alpha is not None and alpha.is_linked:
                source_socket = alpha.links[0].from_socket
                material.node_tree.links.remove(alpha.links[0])
                clip = material.node_tree.nodes.new("ShaderNodeMath")
                clip.name = "UVSR Alpha Mask"
                clip.label = "UVSR Alpha Mask"
                clip.operation = "GREATER_THAN"
                clip.inputs[1].default_value = 0.5
                material.node_tree.links.new(source_socket, clip.inputs[0])
                material.node_tree.links.new(clip.outputs[0], alpha)
                changes["alpha_masks"].append(material.name)
            elif alpha is not None and alpha.default_value < 1.0:
                alpha.default_value = 1.0
                changes["forced_opaque"].append(material.name)

            normal = principled.inputs.get("Normal")
            if normal is not None and normal.is_linked:
                source_node = normal.links[0].from_node
                if source_node.type in {"BUMP", "NORMAL_MAP"}:
                    material.node_tree.links.remove(normal.links[0])
                    changes["omitted_height_maps"].append(material.name)

    for values in changes.values():
        values.sort(key=str.casefold)
    return changes


def bake_import_transforms(objects):
    bpy.ops.object.select_all(action="DESELECT")
    for item in objects:
        item.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    bpy.context.view_layer.update()


def main():
    arguments = parse_arguments()
    configuration = SCENES[arguments.scene]
    source_directory = arguments.source_root.resolve() / arguments.scene
    source = source_directory / f"{arguments.scene}.obj"
    output_directory = arguments.output_root.resolve() / arguments.scene
    output_directory.mkdir(parents=True, exist_ok=True)
    output = output_directory / f"{arguments.scene}.glb"
    pending_output = output.with_name(f"{output.stem}.pending{output.suffix}")
    report_path = output_directory / "conversion-report.json"

    if not source.is_file():
        raise FileNotFoundError(source)

    source_geometry = obj_face_report(source)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    started = time.time()
    import_result = bpy.ops.wm.obj_import(
        filepath=str(source),
        global_scale=configuration["scale"],
        clamp_size=0.0,
        forward_axis="NEGATIVE_Z",
        up_axis="Y",
        use_split_objects=False,
        use_split_groups=False,
        import_vertex_groups=False,
        validate_meshes=True,
        close_spline_loops=True,
        collection_separator="",
        mtl_name_collision_mode="MAKE_UNIQUE",
    )

    objects = selected_meshes()
    if not objects:
        raise RuntimeError(f"{arguments.scene} imported without mesh objects")

    bake_import_transforms(objects)
    converted_bounds = scene_bounds(objects)
    details = mesh_report(objects)
    images, missing_images = image_report()
    if missing_images:
        raise RuntimeError(f"{arguments.scene} has unresolved imported images: {missing_images}")
    material_changes = normalize_materials(arguments.scene)
    source_geometry["discarded_degenerate_triangles"] = (
        source_geometry["triangulated_faces"] - details["triangles"]
    )

    bpy.ops.object.select_all(action="DESELECT")
    for item in objects:
        item.hide_viewport = False
        item.hide_render = False
        item.hide_set(False)
        item.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]

    # Keep the last known-good output intact until Blender confirms a complete
    # replacement. Reusing the final path directly would let a cancelled export
    # accidentally pass the existence check by finding an older GLB.
    pending_output.unlink(missing_ok=True)
    export_result = bpy.ops.export_scene.gltf(
        filepath=str(pending_output),
        check_existing=False,
        export_format="GLB",
        export_copyright=configuration["copyright"],
        export_image_format="AUTO",
        export_keep_originals=False,
        export_texcoords=True,
        export_normals=True,
        export_gn_mesh=False,
        export_tangents=False,
        export_materials="EXPORT",
        export_unused_images=False,
        export_unused_textures=False,
        export_vertex_color="NONE",
        export_all_vertex_colors=False,
        export_attributes=False,
        use_mesh_edges=False,
        use_mesh_vertices=False,
        use_selection=True,
        use_visible=False,
        use_renderable=False,
        export_cameras=False,
        export_lights=False,
        export_yup=True,
        export_apply=False,
        export_extras=False,
        export_animations=False,
        export_gpu_instances=False,
        export_draco_mesh_compression_enable=False,
        export_shared_accessors=False,
        export_original_specular=False,
        will_save_settings=False,
    )
    elapsed = time.time() - started
    if set(export_result) != {"FINISHED"} or not pending_output.is_file():
        pending_output.unlink(missing_ok=True)
        raise RuntimeError(
            f"Blender did not finish exporting {arguments.scene}: "
            f"{sorted(export_result)}"
        )
    pending_output.replace(output)

    report = {
        "scene": arguments.scene,
        "display_name": configuration["display_name"],
        "blender_version": bpy.app.version_string,
        "source": str(source),
        "source_bytes": source.stat().st_size,
        "source_sha256": sha256(source),
        "source_geometry": source_geometry,
        "scale": configuration["scale"],
        "import_result": sorted(import_result),
        "export_result": sorted(export_result),
        "converted_bounds_blender_z_up": converted_bounds,
        "geometry": details,
        "material_changes": material_changes,
        "images": images,
        "output": str(output),
        "output_bytes": output.stat().st_size,
        "output_sha256": sha256(output),
        "elapsed_seconds": round(elapsed, 3),
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("UVSR_SCENE_CONVERSION=" + json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
