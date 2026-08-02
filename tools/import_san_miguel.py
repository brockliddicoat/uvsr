"""Convert McGuire Archive San Miguel OBJ data to a UVSR-ready glTF.

Run this script through Blender 5.1.2. It preserves the full high-detail
geometry, original PNG base-color textures, authored meter scale, and object
grouping. The resulting geometry buffer is intentionally repacked afterward by
``repack_gltf_buffers.py`` so every tracked file stays below GitHub's limit.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import sys
from array import array
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit

import bpy
from mathutils import Vector


MAX_REPACKABLE_BUFFER_VIEW_BYTES = 90_000_000
_owned_pending_directory: Path | None = None


def parse_arguments() -> argparse.Namespace:
    blender_arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    return parser.parse_args(blender_arguments)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def selected_meshes() -> list[bpy.types.Object]:
    return [item for item in bpy.context.scene.objects if item.type == "MESH"]


def scene_bounds(objects: list[bpy.types.Object]) -> dict:
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
        "minimum": [round(float(value), 6) for value in minimum],
        "maximum": [round(float(value), 6) for value in maximum],
        "dimensions": [
            round(float(maximum[index] - minimum[index]), 6) for index in range(3)
        ],
    }


def gltf_y_up_bounds(blender_bounds: dict) -> dict:
    minimum = blender_bounds["minimum"]
    maximum = blender_bounds["maximum"]
    gltf_minimum = [minimum[0], minimum[2], -maximum[1]]
    gltf_maximum = [maximum[0], maximum[2], -minimum[1]]
    return {
        "minimum": gltf_minimum,
        "maximum": gltf_maximum,
        "dimensions": [
            round(gltf_maximum[index] - gltf_minimum[index], 6)
            for index in range(3)
        ],
    }


def scan_obj_geometry(path: Path) -> dict:
    statistics = {
        "positionStatements": 0,
        "texcoordStatements": 0,
        "normalStatements": 0,
        "faceStatements": 0,
        "trianglesAfterFanTriangulation": 0,
        "facesWithRepeatedPositionIndices": 0,
        "trianglesWithRepeatedPositionIndices": 0,
        "objectStatements": 0,
        "groupStatements": 0,
        "materialAssignments": 0,
    }
    with path.open("rb", buffering=8 * 1024 * 1024) as stream:
        for line in stream:
            if line.startswith(b"v "):
                statistics["positionStatements"] += 1
            elif line.startswith(b"vt "):
                statistics["texcoordStatements"] += 1
            elif line.startswith(b"vn "):
                statistics["normalStatements"] += 1
            elif line.startswith(b"f "):
                corners = line.split()[1:]
                corner_count = len(corners)
                statistics["faceStatements"] += 1
                statistics["trianglesAfterFanTriangulation"] += max(
                    corner_count - 2,
                    0,
                )
                position_indices = [
                    corner.split(b"/", 1)[0] for corner in corners
                ]
                if len(set(position_indices)) != len(position_indices):
                    statistics["facesWithRepeatedPositionIndices"] += 1
                    statistics["trianglesWithRepeatedPositionIndices"] += max(
                        corner_count - 2,
                        0,
                    )
            elif line.startswith(b"o "):
                statistics["objectStatements"] += 1
            elif line.startswith(b"g "):
                statistics["groupStatements"] += 1
            elif line.startswith(b"usemtl "):
                statistics["materialAssignments"] += 1
    return statistics


def display_source_path(path: Path, source_root: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(source_root).as_posix()
    except ValueError:
        return resolved.name


def image_report(source_root: Path) -> tuple[list[dict], list[dict]]:
    report: list[dict] = []
    missing: list[dict] = []
    for image in sorted(bpy.data.images, key=lambda value: value.name.casefold()):
        if image.type in {"RENDER_RESULT", "COMPOSITING"}:
            continue
        resolved = Path(bpy.path.abspath(image.filepath, library=image.library))
        exists = image.packed_file is not None or resolved.is_file()
        item = {
            "name": image.name,
            "source": image.source,
            "path": display_source_path(resolved, source_root),
            "existsOrPacked": exists,
        }
        report.append(item)
        if not exists:
            missing.append(item)
    return report, missing


def mesh_report(objects: list[bpy.types.Object]) -> dict:
    fan_triangles = 0
    tessellated_triangles = 0
    vertices = 0
    polygons = 0
    loops = 0
    largest_objects: list[tuple[int, str]] = []
    for item in objects:
        mesh = item.data
        loop_totals = array("I", [0]) * len(mesh.polygons)
        mesh.polygons.foreach_get("loop_total", loop_totals)
        object_fan_triangles = sum(
            max(int(loop_total) - 2, 0) for loop_total in loop_totals
        )
        mesh.calc_loop_triangles()
        object_triangles = len(mesh.loop_triangles)
        fan_triangles += object_fan_triangles
        tessellated_triangles += object_triangles
        vertices += len(mesh.vertices)
        polygons += len(mesh.polygons)
        loops += len(mesh.loops)
        largest_objects.append((object_triangles, item.name))
    largest_objects.sort(reverse=True)
    return {
        "objects": len(objects),
        "vertices": vertices,
        "polygons": polygons,
        "fanTriangles": fan_triangles,
        "triangles": tessellated_triangles,
        "nonTessellatedDegenerateFanTriangles": (
            fan_triangles - tessellated_triangles
        ),
        "loops": loops,
        "materials": len(bpy.data.materials),
        "largestObjectsByTriangles": [
            {"name": name, "triangles": count}
            for count, name in largest_objects[:20]
        ],
    }


def validate_meshes_for_export(objects: list[bpy.types.Object]) -> dict:
    """Run once the same mesh validation Blender's glTF exporter requires."""

    validated_meshes: set[int] = set()
    changed_meshes = 0
    for item in objects:
        mesh = item.data
        identity = int(mesh.as_pointer())
        if identity in validated_meshes:
            continue
        validated_meshes.add(identity)
        if mesh.validate(verbose=False, clean_customdata=True):
            changed_meshes += 1
    return {
        "uniqueMeshes": len(validated_meshes),
        "changedMeshes": changed_meshes,
    }


def png_uses_alpha(path: Path) -> bool:
    """Return whether a PNG declares alpha or a transparency chunk."""

    with path.open("rb") as stream:
        if stream.read(8) != b"\x89PNG\r\n\x1a\n":
            return False
        while True:
            length_bytes = stream.read(4)
            chunk_type = stream.read(4)
            if len(length_bytes) != 4 or len(chunk_type) != 4:
                return False
            length = int.from_bytes(length_bytes, "big")
            chunk_data = stream.read(length)
            if len(chunk_data) != length or len(stream.read(4)) != 4:
                return False
            if chunk_type == b"IHDR":
                if len(chunk_data) != 13:
                    return False
                if chunk_data[9] in {4, 6}:
                    return True
            elif chunk_type == b"tRNS":
                return True
            elif chunk_type in {b"IDAT", b"IEND"}:
                return False


def linked_image_node(socket: bpy.types.NodeSocket) -> bpy.types.Node | None:
    if socket is None or not socket.is_linked:
        return None
    source_node = socket.links[0].from_node
    if source_node.type != "TEX_IMAGE":
        return None
    return source_node


def source_base_color_bindings(material_library: Path) -> dict[str, str]:
    """Read the exact material-to-map_Kd contract from the source MTL."""

    bindings: dict[str, str] = {}
    current_material: str | None = None
    for line_number, raw_line in enumerate(
        material_library.read_text(encoding="utf-8").splitlines(),
        start=1,
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        command, separator, value = line.partition(" ")
        value = value.strip()
        if command == "newmtl":
            if not separator or not value:
                raise RuntimeError(
                    f"invalid newmtl at {material_library.name}:{line_number}"
                )
            current_material = value
        elif command == "map_Kd":
            if current_material is None or not separator or not value:
                raise RuntimeError(
                    f"invalid map_Kd at {material_library.name}:{line_number}"
                )
            if value.startswith("-"):
                raise RuntimeError(
                    "San Miguel map_Kd options need an explicit parser: "
                    f"{material_library.name}:{line_number}"
                )
            normalized_path = value.replace("\\", "/")
            if current_material in bindings:
                raise RuntimeError(
                    f"duplicate map_Kd for San Miguel material {current_material}"
                )
            bindings[current_material] = normalized_path
    return dict(sorted(bindings.items(), key=lambda item: item[0].casefold()))


def normalize_materials(source_root: Path) -> dict:
    """Translate McGuire's legacy MTL alpha and normal-map intent to glTF."""

    changes = {
        "alphaMasks": set(),
        "forcedOpaque": set(),
        "transmissionFlattened": {},
        "normalMapsPreserved": set(),
        "omittedHeightOrAmbiguousNormalMaps": set(),
        "baseColorTextures": {},
    }
    for material in sorted(bpy.data.materials, key=lambda value: value.name.casefold()):
        if not material.use_nodes or material.node_tree is None:
            continue
        principled_nodes = [
            node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED"
        ]
        for principled in principled_nodes:
            alpha = principled.inputs.get("Alpha")
            base_color = principled.inputs.get("Base Color")
            base_color_node = linked_image_node(base_color)
            base_color_image = (
                base_color_node.image if base_color_node is not None else None
            )
            base_color_path = (
                Path(bpy.path.abspath(
                    base_color_image.filepath,
                    library=base_color_image.library,
                ))
                if base_color_image is not None
                else None
            )
            if base_color_path is not None:
                source_path = display_source_path(base_color_path, source_root)
                previous_path = changes["baseColorTextures"].get(material.name)
                if previous_path is not None and previous_path != source_path:
                    raise RuntimeError(
                        f"material {material.name} has conflicting base-color images: "
                        f"{previous_path} and {source_path}"
                    )
                changes["baseColorTextures"][material.name] = source_path
            alpha_source = None
            if alpha is not None and alpha.is_linked:
                alpha_source = alpha.links[0].from_socket
            elif (
                alpha is not None
                and base_color_image is not None
                and base_color_path is not None
                and base_color_path.is_file()
                and png_uses_alpha(base_color_path)
            ):
                alpha_source = base_color_node.outputs.get("Alpha")

            if alpha is not None and alpha_source is not None:
                if alpha.is_linked:
                    material.node_tree.links.remove(alpha.links[0])
                clip = material.node_tree.nodes.new("ShaderNodeMath")
                clip.name = "UVSR Alpha Mask"
                clip.label = "UVSR Alpha Mask"
                clip.operation = "GREATER_THAN"
                clip.inputs[1].default_value = 0.5
                material.node_tree.links.new(alpha_source, clip.inputs[0])
                material.node_tree.links.new(clip.outputs[0], alpha)
                changes["alphaMasks"].add(material.name)
            elif alpha is not None and alpha.default_value < 1.0:
                # UVSR's primary deferred path does not instantiate blended
                # geometry. Keep glass and legacy dissolve materials visible as
                # opaque surfaces instead of silently dropping them.
                alpha.default_value = 1.0
                changes["forcedOpaque"].add(material.name)

            transmission = principled.inputs.get("Transmission Weight")
            if transmission is not None:
                if transmission.is_linked:
                    raise RuntimeError(
                        f"material {material.name} has linked transmission that "
                        "cannot be flattened without baking"
                    )
                if transmission.default_value > 0.0:
                    previous_value = changes["transmissionFlattened"].get(
                        material.name
                    )
                    source_value = float(transmission.default_value)
                    if previous_value is not None and previous_value != source_value:
                        raise RuntimeError(
                            f"material {material.name} has conflicting transmission "
                            "weights"
                        )
                    # Donut classifies a material as transmissive whenever the
                    # glTF extension is present, while UVSR currently submits only
                    # opaque and alpha-tested domains. Remove transmission at the
                    # authoring node so the exporter omits that extension and the
                    # complete mesh remains visible as an opaque PBR fallback.
                    changes["transmissionFlattened"][material.name] = source_value
                    transmission.default_value = 0.0

            normal = principled.inputs.get("Normal")
            if normal is not None and normal.is_linked:
                source_node = normal.links[0].from_node
                color = source_node.inputs.get("Color")
                normal_image_node = linked_image_node(color)
                normal_image = (
                    normal_image_node.image
                    if normal_image_node is not None
                    else None
                )
                normal_path = (
                    Path(bpy.path.abspath(
                        normal_image.filepath,
                        library=normal_image.library,
                    ))
                    if normal_image is not None
                    else None
                )
                normal_name = normal_path.name if normal_path is not None else ""
                if source_node.type == "NORMAL_MAP" and normal_name.startswith("N_"):
                    strength = source_node.inputs.get("Strength")
                    if strength is not None:
                        strength.default_value = 1.0
                    changes["normalMapsPreserved"].add(material.name)
                elif source_node.type in {"BUMP", "NORMAL_MAP"}:
                    # The source notice identifies incorrect bump/normal MTL
                    # mappings. Preserve the explicitly named N_* tangent maps,
                    # but do not mislabel the one remaining height/ambiguous map
                    # as a tangent-space glTF normal texture.
                    material.node_tree.links.remove(normal.links[0])
                    changes["omittedHeightOrAmbiguousNormalMaps"].add(material.name)

    result = {}
    for key, values in changes.items():
        if isinstance(values, set):
            result[key] = sorted(values, key=str.casefold)
        else:
            result[key] = dict(
                sorted(values.items(), key=lambda item: item[0].casefold())
            )
    return result


def bake_import_transforms(objects: list[bpy.types.Object]) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    for item in objects:
        item.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    bpy.context.view_layer.update()


def maximum_buffer_view_bytes(gltf_path: Path) -> int:
    document = json.loads(gltf_path.read_text(encoding="utf-8"))
    return max(
        (int(view.get("byteLength", 0)) for view in document.get("bufferViews", [])),
        default=0,
    )


def localize_external_images(
    gltf_path: Path,
    source_root: Path,
    output_root: Path,
) -> list[str]:
    """Copy exporter-referenced source PNGs beside glTF without re-encoding."""

    document = json.loads(gltf_path.read_text(encoding="utf-8"))
    localized: list[str] = []
    destinations: dict[str, Path] = {}
    for index, image in enumerate(document.get("images", [])):
        uri = image.get("uri")
        if not isinstance(uri, str) or not uri:
            raise RuntimeError(f"glTF image {index} has no external URI")
        parsed = urlsplit(uri)
        if parsed.scheme or parsed.netloc or parsed.query or parsed.fragment:
            raise RuntimeError(f"glTF image {index} has a non-file URI: {uri}")
        source = (gltf_path.parent / unquote(parsed.path).replace("/", os.sep)).resolve()
        try:
            relative = source.relative_to(source_root)
        except ValueError as error:
            raise RuntimeError(
                f"glTF image {index} escaped the San Miguel source root: {uri}"
            ) from error
        if not source.is_file():
            raise FileNotFoundError(source)
        destination = output_root / relative
        comparison_key = relative.as_posix().casefold()
        previous_source = destinations.get(comparison_key)
        if previous_source is not None and previous_source != source:
            raise RuntimeError(
                f"glTF images collide at {relative.as_posix()}: "
                f"{previous_source} and {source}"
            )
        destinations[comparison_key] = source
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists():
            shutil.copy2(source, destination)
        image["uri"] = quote(relative.as_posix(), safe="/-._~")
        localized.append(relative.as_posix())

    gltf_path.write_text(
        json.dumps(document, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return sorted(set(localized), key=str.casefold)


def exported_material_report(gltf_path: Path) -> dict:
    document = json.loads(gltf_path.read_text(encoding="utf-8"))
    images = document.get("images", [])
    textures = document.get("textures", [])
    alpha_masks: list[str] = []
    alpha_blends: list[str] = []
    transmission_materials: list[str] = []
    normal_maps: list[str] = []
    base_color_textures: dict[str, str] = {}
    for index, material in enumerate(document.get("materials", [])):
        name = material.get("name", f"material-{index}")
        if material.get("alphaMode") == "MASK":
            alpha_masks.append(name)
        elif material.get("alphaMode") == "BLEND":
            alpha_blends.append(name)
        if "KHR_materials_transmission" in material.get("extensions", {}):
            transmission_materials.append(name)
        if "normalTexture" in material:
            normal_maps.append(name)
        base_color = material.get("pbrMetallicRoughness", {}).get(
            "baseColorTexture"
        )
        if base_color is not None:
            texture_index = base_color.get("index")
            if not isinstance(texture_index, int) or not (
                0 <= texture_index < len(textures)
            ):
                raise RuntimeError(
                    f"glTF material {name} has invalid base-color texture"
                )
            image_index = textures[texture_index].get("source")
            if not isinstance(image_index, int) or not (0 <= image_index < len(images)):
                raise RuntimeError(
                    f"glTF material {name} has invalid base-color image"
                )
            uri = images[image_index].get("uri")
            if not isinstance(uri, str) or not uri:
                raise RuntimeError(
                    f"glTF material {name} has no external base-color image URI"
                )
            if name in base_color_textures:
                raise RuntimeError(f"duplicate glTF material name: {name}")
            base_color_textures[name] = unquote(uri).replace("\\", "/")
    return {
        "materialCount": len(document.get("materials", [])),
        "alphaMaskMaterials": sorted(alpha_masks, key=str.casefold),
        "alphaBlendMaterials": sorted(alpha_blends, key=str.casefold),
        "transmissionMaterials": sorted(
            transmission_materials,
            key=str.casefold,
        ),
        "normalTextureMaterials": sorted(normal_maps, key=str.casefold),
        "baseColorTextures": dict(
            sorted(
                base_color_textures.items(),
                key=lambda item: item[0].casefold(),
            )
        ),
    }


def exported_geometry_report(gltf_path: Path) -> dict:
    document = json.loads(gltf_path.read_text(encoding="utf-8"))
    accessors = document.get("accessors", [])
    mesh_triangles: list[int] = []
    primitives = 0
    for mesh_index, mesh in enumerate(document.get("meshes", [])):
        triangles = 0
        for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
            mode = int(primitive.get("mode", 4))
            if mode != 4:
                raise RuntimeError(
                    f"glTF mesh {mesh_index} primitive {primitive_index} "
                    f"uses unsupported mode {mode}"
                )
            accessor_index = primitive.get("indices")
            if accessor_index is None:
                accessor_index = primitive.get("attributes", {}).get("POSITION")
            if not isinstance(accessor_index, int) or not (
                0 <= accessor_index < len(accessors)
            ):
                raise RuntimeError(
                    f"glTF mesh {mesh_index} primitive {primitive_index} "
                    "has no valid triangle-count accessor"
                )
            count = int(accessors[accessor_index].get("count", -1))
            if count <= 0 or count % 3 != 0:
                raise RuntimeError(
                    f"glTF mesh {mesh_index} primitive {primitive_index} "
                    f"has invalid triangle element count {count}"
                )
            triangles += count // 3
            primitives += 1
        mesh_triangles.append(triangles)

    nodes = document.get("nodes", [])
    scenes = document.get("scenes", [])
    scene_index = int(document.get("scene", 0))
    if not (0 <= scene_index < len(scenes)):
        raise RuntimeError(f"glTF has invalid default scene {scene_index}")
    visiting: set[int] = set()
    visited: set[int] = set()
    instanced_triangles = 0
    mesh_instances = 0

    def visit_node(node_index: int) -> None:
        nonlocal instanced_triangles, mesh_instances
        if node_index in visited:
            return
        if node_index in visiting or not (0 <= node_index < len(nodes)):
            raise RuntimeError(f"glTF scene graph has invalid node {node_index}")
        visiting.add(node_index)
        node = nodes[node_index]
        mesh_index = node.get("mesh")
        if mesh_index is not None:
            if not isinstance(mesh_index, int) or not (
                0 <= mesh_index < len(mesh_triangles)
            ):
                raise RuntimeError(
                    f"glTF node {node_index} has invalid mesh {mesh_index}"
                )
            instanced_triangles += mesh_triangles[mesh_index]
            mesh_instances += 1
        for child in node.get("children", []):
            if not isinstance(child, int):
                raise RuntimeError(
                    f"glTF node {node_index} has a nonnumeric child"
                )
            visit_node(child)
        visiting.remove(node_index)
        visited.add(node_index)

    for root in scenes[scene_index].get("nodes", []):
        if not isinstance(root, int):
            raise RuntimeError("glTF default scene has a nonnumeric root")
        visit_node(root)

    return {
        "meshes": len(mesh_triangles),
        "meshInstances": mesh_instances,
        "primitives": primitives,
        "uniqueMeshTriangles": sum(mesh_triangles),
        "instancedTriangles": instanced_triangles,
    }


def validate_expected_material_audit(
    geometry: dict,
    material_changes: dict,
    exported_materials: dict,
    localized_images: list[str],
    source_base_colors: dict[str, str],
) -> None:
    expected_counts = {
        "alphaMasks": 95,
        "normalMapsPreserved": 56,
    }
    for key, expected in expected_counts.items():
        actual = len(material_changes[key])
        if actual != expected:
            raise RuntimeError(
                f"San Miguel material audit expected {expected} {key}, found {actual}"
            )
    if material_changes["forcedOpaque"] != ["material_041"]:
        raise RuntimeError(
            "San Miguel material audit expected only material_041 to be forced opaque"
        )
    expected_transmission = {
        "material_79": 0.10000000149011612,
        "materialn": 0.30000001192092896,
        "materialo": 0.36666667461395264,
    }
    if material_changes["transmissionFlattened"] != expected_transmission:
        raise RuntimeError(
            "San Miguel material audit transmission fallback differs: "
            f"{material_changes['transmissionFlattened']}"
        )
    if material_changes["omittedHeightOrAmbiguousNormalMaps"] != ["material_44"]:
        raise RuntimeError(
            "San Miguel material audit expected only material_44's ambiguous bump map to be omitted"
        )
    if geometry["materials"] != 287:
        raise RuntimeError(
            f"San Miguel material audit expected 287 materials, found {geometry['materials']}"
        )
    if exported_materials["materialCount"] != 287:
        raise RuntimeError(
            "San Miguel glTF must export exactly 287 materials, found "
            f"{exported_materials['materialCount']}"
        )
    if len(source_base_colors) != 264:
        raise RuntimeError(
            "San Miguel MTL must define exactly 264 map_Kd bindings, found "
            f"{len(source_base_colors)}"
        )
    if material_changes["baseColorTextures"] != source_base_colors:
        raise RuntimeError(
            "Blender import changed San Miguel's material-to-map_Kd bindings"
        )
    if exported_materials["baseColorTextures"] != source_base_colors:
        raise RuntimeError(
            "glTF export changed San Miguel's material-to-map_Kd bindings"
        )
    if len(exported_materials["alphaMaskMaterials"]) != 95:
        raise RuntimeError("San Miguel glTF must export exactly 95 alpha-mask materials")
    if exported_materials["alphaBlendMaterials"]:
        raise RuntimeError("San Miguel glTF must not export blended materials")
    if exported_materials["transmissionMaterials"]:
        raise RuntimeError("San Miguel glTF must not export transmission materials")
    if len(exported_materials["normalTextureMaterials"]) != 56:
        raise RuntimeError("San Miguel glTF must export exactly 56 normal textures")
    if len(localized_images) != 269:
        raise RuntimeError(
            f"San Miguel glTF must localize 269 used source images, found {len(localized_images)}"
        )


def main() -> None:
    global _owned_pending_directory
    arguments = parse_arguments()
    if bpy.app.version_string != "5.1.2":
        raise RuntimeError(
            "San Miguel conversion is pinned to Blender 5.1.2, found "
            f"{bpy.app.version_string}"
        )
    source_root = arguments.source_root.resolve()
    source = source_root / "san-miguel.obj"
    material_library = source_root / "san-miguel.mtl"
    license_path = source_root / "license.txt"
    output_directory = arguments.output_directory.resolve()
    pending = output_directory.with_name(
        output_directory.name + f".pending-{os.getpid()}"
    )
    if output_directory.exists():
        raise FileExistsError(output_directory)
    if pending.exists():
        raise FileExistsError(pending)
    for required in (source, material_library, license_path):
        if not required.is_file():
            raise FileNotFoundError(required)
    pending.mkdir(parents=True)
    _owned_pending_directory = pending
    output = pending / "san_miguel.gltf"

    bpy.ops.wm.read_factory_settings(use_empty=True)
    source_geometry = scan_obj_geometry(source)
    source_base_colors = source_base_color_bindings(material_library)
    import_result = bpy.ops.wm.obj_import(
        filepath=str(source),
        global_scale=1.0,
        clamp_size=0.0,
        forward_axis="NEGATIVE_Z",
        up_axis="Y",
        use_split_objects=True,
        use_split_groups=True,
        import_vertex_groups=False,
        validate_meshes=False,
        close_spline_loops=True,
        collection_separator="",
        mtl_name_collision_mode="MAKE_UNIQUE",
    )
    objects = selected_meshes()
    if not objects:
        raise RuntimeError("San Miguel imported without mesh objects")

    bake_import_transforms(objects)
    blender_bounds = scene_bounds(objects)
    imported_geometry = mesh_report(objects)
    expected_imported_polygons = (
        source_geometry["faceStatements"]
        - source_geometry["facesWithRepeatedPositionIndices"]
    )
    expected_imported_triangles = (
        source_geometry["trianglesAfterFanTriangulation"]
        - source_geometry["trianglesWithRepeatedPositionIndices"]
    )
    if (
        imported_geometry["polygons"] != expected_imported_polygons
        or imported_geometry["fanTriangles"] != expected_imported_triangles
    ):
        raise RuntimeError(
            "Blender import changed valid source face topology: "
            f"source={source_geometry['faceStatements']} polygons/"
            f"{source_geometry['trianglesAfterFanTriangulation']} triangles, "
            f"expected after repeated-index rejection="
            f"{expected_imported_polygons} polygons/"
            f"{expected_imported_triangles} triangles, "
            f"imported={imported_geometry['polygons']} polygons/"
            f"{imported_geometry['fanTriangles']} fan triangles"
        )
    mesh_validation = validate_meshes_for_export(objects)
    geometry = mesh_report(objects)
    topology_audit = {
        "sourceFaceStatements": source_geometry["faceStatements"],
        "sourceTrianglesAfterFanTriangulation": source_geometry[
            "trianglesAfterFanTriangulation"
        ],
        "discardedRepeatedPositionIndexFaces": source_geometry[
            "facesWithRepeatedPositionIndices"
        ],
        "discardedRepeatedPositionIndexTriangles": source_geometry[
            "trianglesWithRepeatedPositionIndices"
        ],
        "importedPolygons": imported_geometry["polygons"],
        "importedFanTriangles": imported_geometry["fanTriangles"],
        "importedTessellatedTriangles": imported_geometry["triangles"],
        "validationRemovedPolygons": (
            imported_geometry["polygons"] - geometry["polygons"]
        ),
        "validationRemovedFanTriangles": (
            imported_geometry["fanTriangles"] - geometry["fanTriangles"]
        ),
        "validatedPolygons": geometry["polygons"],
        "validatedTessellatedTriangles": geometry["triangles"],
        "validatedNonTessellatedDegenerateFanTriangles": geometry[
            "nonTessellatedDegenerateFanTriangles"
        ],
        "validSourceTopologyImportedBeforeRequiredValidation": True,
    }
    images, missing_images = image_report(source_root)
    if missing_images:
        raise RuntimeError(
            f"San Miguel has unresolved imported images: {missing_images[:20]}"
        )
    material_changes = normalize_materials(source_root)

    bpy.ops.object.select_all(action="DESELECT")
    for item in objects:
        item.hide_viewport = False
        item.hide_render = False
        item.hide_set(False)
        item.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]

    export_result = bpy.ops.export_scene.gltf(
        filepath=str(output),
        check_existing=False,
        export_format="GLTF_SEPARATE",
        export_texture_dir="textures",
        export_copyright=(
            "San Miguel by Guillermo M. Leal Llaguno; 2017 improvements by "
            "Morgan McGuire, Guedis Cardenas, Michael Mara, and Nicholas Hull"
        ),
        export_image_format="AUTO",
        export_keep_originals=True,
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
    if set(export_result) != {"FINISHED"} or not output.is_file():
        raise RuntimeError(f"Blender did not finish exporting: {sorted(export_result)}")

    localized_images = localize_external_images(output, source_root, pending)
    exported_geometry = exported_geometry_report(output)
    expected_exported_triangles = geometry["triangles"]
    if exported_geometry["instancedTriangles"] != expected_exported_triangles:
        raise RuntimeError(
            "glTF export changed nondegenerate topology: "
            f"expected {expected_exported_triangles} tessellated triangles, "
            f"exported {exported_geometry['instancedTriangles']} instances"
        )
    topology_audit.update(
        {
            "exportedInstancedTriangles": exported_geometry[
                "instancedTriangles"
            ],
            "renderableTopologyPreserved": True,
        }
    )
    exported_materials = exported_material_report(output)
    missing_alpha_masks = sorted(
        set(material_changes["alphaMasks"])
        - set(exported_materials["alphaMaskMaterials"]),
        key=str.casefold,
    )
    missing_normal_maps = sorted(
        set(material_changes["normalMapsPreserved"])
        - set(exported_materials["normalTextureMaterials"]),
        key=str.casefold,
    )
    if missing_alpha_masks or missing_normal_maps:
        raise RuntimeError(
            "glTF material export lost normalized source intent: "
            f"alpha masks={missing_alpha_masks[:20]}, "
            f"normal maps={missing_normal_maps[:20]}"
        )
    validate_expected_material_audit(
        geometry,
        material_changes,
        exported_materials,
        localized_images,
        source_base_colors,
    )

    binary_files = sorted(pending.glob("*.bin"), key=lambda path: path.name.casefold())
    if len(binary_files) != 1:
        raise RuntimeError(
            f"expected one pre-repack geometry buffer, found {len(binary_files)}"
        )
    exported_textures = sorted(
        (path for path in (pending / "textures").rglob("*") if path.is_file()),
        key=lambda path: path.relative_to(pending).as_posix().casefold(),
    )
    maximum_buffer_view = maximum_buffer_view_bytes(output)
    if maximum_buffer_view > MAX_REPACKABLE_BUFFER_VIEW_BYTES:
        raise RuntimeError(
            f"San Miguel has a {maximum_buffer_view}-byte buffer view, above the "
            f"{MAX_REPACKABLE_BUFFER_VIEW_BYTES}-byte lossless repack ceiling"
        )
    report = {
        "schemaVersion": 1,
        "scene": "san_miguel_retextured",
        "displayName": "Retextured San Miguel",
        "blenderVersion": bpy.app.version_string,
        "blenderBuildHash": bpy.app.build_hash.decode("ascii"),
        "source": source.name,
        "sourceBytes": source.stat().st_size,
        "sourceSha256": sha256(source),
        "materialLibrary": material_library.name,
        "materialLibraryBytes": material_library.stat().st_size,
        "materialLibrarySha256": sha256(material_library),
        "licenseSha256": sha256(license_path),
        "scale": 1.0,
        "objForwardAxis": "-Z",
        "objUpAxis": "Y",
        "gltfUpAxis": "Y",
        "importResult": sorted(import_result),
        "exportResult": sorted(export_result),
        "sourceGeometry": source_geometry,
        "topologyAudit": topology_audit,
        "blenderWorldBounds": blender_bounds,
        "gltfWorldBounds": gltf_y_up_bounds(blender_bounds),
        "geometry": geometry,
        "preValidationGeometry": imported_geometry,
        "meshValidation": mesh_validation,
        "exportedGeometry": exported_geometry,
        "materialChanges": material_changes,
        "exportedMaterials": exported_materials,
        "localizedImages": localized_images,
        "importedImages": images,
        "exportedTextureCount": len(exported_textures),
        "exportedTextureBytes": sum(path.stat().st_size for path in exported_textures),
        "outputGltfBytes": output.stat().st_size,
        "outputGltfSha256": sha256(output),
        "outputBuffer": binary_files[0].name,
        "outputBufferBytes": binary_files[0].stat().st_size,
        "outputBufferSha256": sha256(binary_files[0]),
        "maximumBufferViewBytes": maximum_buffer_view,
    }
    (pending / "blender-import-report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    os.replace(pending, output_directory)
    _owned_pending_directory = None
    print(
        "UVSR_SAN_MIGUEL_IMPORT="
        + json.dumps(
            {
                "outputDirectory": str(output_directory),
                "outputGltfSha256": report["outputGltfSha256"],
                "outputBufferBytes": report["outputBufferBytes"],
                "exportedInstancedTriangles": report["exportedGeometry"][
                    "instancedTriangles"
                ],
                "alphaMaskMaterials": len(
                    report["exportedMaterials"]["alphaMaskMaterials"]
                ),
                "normalTextureMaterials": len(
                    report["exportedMaterials"]["normalTextureMaterials"]
                ),
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    try:
        main()
    except Exception:
        # Remove only a pending directory that this process created. A
        # pre-existing same-PID path is never claimed or deleted.
        if _owned_pending_directory is not None:
            shutil.rmtree(_owned_pending_directory, ignore_errors=True)
            _owned_pending_directory = None
        raise
