"""Export Blender Classroom as renderer-compatible glTF PBR.

Run this script through the pinned Blender 5.1.2 executable with the source
``classroom.blend`` already open. The script realizes linked collection
instances, converts evaluated curve and mesh objects to static meshes, replaces
Blender 2.79 Cycles graphs with audited glTF metallic-roughness materials, and
exports the visible frame-1 ``_mainScene``. Source-authored base-color images
are copied byte-for-byte and connected with a white glTF factor so inactive
legacy socket defaults cannot tint them. Legacy bump networks are audited but
intentionally omitted instead of replacing the source art with generated normal
maps. The source UV-test checker papers are deliberately remapped to the existing
blank-paper material, and the spawn-corner trash cluster is omitted. The script
fails when the source inventory, material inventory, geometry baseline, or output
contracts change.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import sys
from collections import Counter
from pathlib import Path
from urllib.parse import unquote

import bmesh
import bpy
from mathutils import Matrix, Vector


SCENE_NAME = "_mainScene"
EXPORT_SCENE_NAME = "UVSR Classroom Export"
EXPECTED_BLENDER_VERSION = (5, 1, 2)
EXPECTED_BLENDER_BUILD_HASH = "ec6e62d40fa9"
EXPECTED_SOURCE_BLEND_SHA256 = (
    "5C526EA3F280566E80253673C9955640527CD0F247EA41B1742620B5BC39F7A4"
)
EXPECTED_LIBRARY_COUNT = 11
EXPECTED_FRAME_ONE_TRIANGLES = 607_484
TRACKED_FILE_LIMIT_BYTES = 100_000_000
DEFAULT_MATERIAL_NAME = "UVSR_Default"
EXPECTED_GLTF_MATERIAL_COUNT = 66
EXPECTED_GLTF_IMAGE_COUNT = 19
EXPECTED_GLTF_TEXTURE_COUNT = 36
EXPECTED_ARCHIVE_IMAGE_COPY_COUNT = 19


EXPECTED_MATERIALS = {
    "base_brass",
    "base_carbon",
    "base_metal",
    "base_PlastiqueNoir",
    "beige_paintedPipe",
    "beigeBook_cover",
    "beigeBook_pages",
    "beigePaint",
    "beigePaintedPlastic",
    "beigePaintedwood",
    "beigeWall",
    "blackBoard",
    "blackBoardLight",
    "blackPaintedWood",
    "boardFrame",
    "ceilingAirVent",
    "ceilingLamp_BlackPlastic",
    "ceilingLamp_metal",
    "ceillingLamp_glass",
    "coloredPlastic",
    "cork",
    "crinkledPaper_paper",
    "dayLight_portal",
    "drawing",
    "drawing.002",
    "drawing.003",
    "drawing.004",
    "drawing.005",
    "dustbin_bottom_metal",
    "dustbin_metal",
    "dustBin_wireframe",
    "frostedGlass",
    "greyDiffusePaint",
    "largeBox_glossyWhitePaper",
    "largeBox_greyPaper",
    "LeatherBook_cover",
    "leatherBook_pages",
    "leatherChair_blackPlastic",
    "leatherChair_leather",
    "leatherChair_metal",
    "leatherCoat",
    "paintedBlind",
    "paintedCeiling",
    "paper",
    "pencil",
    "plaster",
    "plasticGoldPen_greenPlastic",
    "porcelainHandle",
    "radiator_glossyPaint",
    "sc.blade",
    "schoolDesk_blackPlastic",
    "schoolDesk_metal",
    "schoolDesk_plastic",
    "schoolDesk_wood",
    "suitCase_leather",
    "suitCase_metal",
    "suitCoat_darkWood",
    "suitCoat_metal",
    "teacherDesk_blackPlastic",
    "teacherDesk_metal",
    "teacherDesk_plastic",
    "teacherDesk_wood",
    "varnishedWoodDoor",
    "wallClock_blackPlastic",
    "wallClock_darkWood",
    "wallClock_Glass",
    "wallClock_whiteBackground",
    "white_glossPaint",
    "whiteChalk",
    "whitePaintedWindow",
    "woodFloor",
    "woodPlanks",
    "worldMap",
    "yellowLetter",
    "zapBook_color",
}
EXPECTED_UNUSED_MATERIALS = {
    "base_PlastiqueNoir",
    "coloredPlastic",
    "cork",
    "largeBox_greyPaper",
}

# The one local `dustBin` hierarchy owns the photographed wire bin and its 13
# crumpled-paper collection instances. Omission follows owner datablock identity,
# not shared mesh names. The pinned material inventory makes a future second bin
# fail export explicitly instead of disappearing through an over-broad mesh rule.
OMITTED_SOURCE_ROOT_NAME = "dustBin"
EXPECTED_OMITTED_OWNER_NAMES = {
    "dustBin",
    "boulette_papier",
    *(f"boulette_papier.{index:03d}" for index in range(1, 13)),
}
EXPECTED_OMITTED_OWNER_INSTANCE_COUNTS = {
    "dustBin": 3,
    "boulette_papier": 1,
    **{f"boulette_papier.{index:03d}": 1 for index in range(1, 13)},
}
EXPECTED_OMITTED_SOURCE_OBJECT_INSTANCE_COUNTS = {
    "Cylinder": 1,
    "Cylinder.056": 1,
    "Cylinder.057": 1,
    "Paper": 13,
}
EXPECTED_OMITTED_TRIANGLES_BY_SOURCE_OBJECT = {
    "Cylinder": 768,
    "Cylinder.056": 880,
    "Cylinder.057": 3_840,
    "Paper": 52_832,
}
OMITTED_SOURCE_OBJECT_MATERIALS = {
    "crinkledPaper_paper",
    "dustbin_bottom_metal",
    "dustbin_metal",
    "dustBin_wireframe",
}

# `drawing` uses Blender's generated UV-test checker. Keep its material identity
# for auditability, but suppress the diagnostic image and match the existing
# blank-paper appearance exactly.
DIAGNOSTIC_UV_TEST_IMAGE_BY_MATERIAL = {
    "drawing": "checker",
}
BLANK_APPEARANCE_REFERENCE_BY_MATERIAL = {
    "drawing": "drawing.004",
}

DROP_MATERIALS = {"dayLight_portal", "wallClock_Glass"}
MASK_MATERIALS = {"crinkledPaper_paper", "dustBin_wireframe"}
GLASS_MATERIALS = {"ceillingLamp_glass", "frostedGlass"}
EMISSIVE_MATERIALS = {"blackBoardLight"}
METAL_MATERIALS = {
    "base_brass",
    "base_metal",
    "ceilingLamp_metal",
    "dustbin_bottom_metal",
    "dustbin_metal",
    "leatherChair_metal",
    "sc.blade",
    "schoolDesk_metal",
    "suitCase_metal",
    "suitCoat_metal",
    "teacherDesk_metal",
}
OPAQUE_MATERIALS = EXPECTED_MATERIALS.difference(
    DROP_MATERIALS | MASK_MATERIALS | GLASS_MATERIALS
)


# The source image chosen for each retained legacy color network. Every
# multi-image graph is resolved explicitly instead of depending on node
# traversal order.
BASE_IMAGE_BY_MATERIAL: dict[str, str] = {
    "beige_paintedPipe": "base_paintedPlasterWall.jpg",
    "beigeBook_pages": "zapBook.jpg",
    "beigePaint": "base_paintedPlasterWall.jpg",
    "beigePaintedPlastic": "base_paintedPlasterWall.jpg",
    "beigePaintedwood": "base_paintedPlasterWall.jpg",
    "beigeWall": "base_wallPaint.jpg",
    "blackBoard": "blackBoard.png",
    "boardFrame": "base_paintedPlasterWall.jpg",
    "ceilingAirVent": "ceilingAirVent_AO.png",
    "ceilingLamp_metal": "base_bluredMetal.png",
    "cork": "cork.jpg",
    "crinkledPaper_paper": "crinkledPaper.png",
    "drawing.002": "childDrawing_03.jpg",
    "drawing.003": "childDrawing_07.png",
    "drawing.005": "childDrawing_05.jpg",
    "dustbin_bottom_metal": "base_bluredMetal.png",
    "dustbin_metal": "base_bareMetal.png",
    "greyDiffusePaint": "base_paintedPlasterWall.jpg",
    "largeBox_greyPaper": "base_paper_01.png",
    "LeatherBook_cover": "base_leather.jpg",
    "leatherBook_pages": "zapBook.jpg",
    "leatherChair_leather": "base_leather.jpg",
    "leatherChair_metal": "base_bluredMetal.png",
    "leatherCoat": "base_leather.jpg",
    "paintedCeiling": "base_wallPaint.jpg",
    "pencil": "pencil_color.png",
    "radiator_glossyPaint": "radiator_AO.png",
    "schoolDesk_metal": "base_bluredMetal.png",
    "schoolDesk_wood": "base_brightWood.png",
    "suitCase_leather": "base_leather.jpg",
    "suitCase_metal": "base_bluredMetal.png",
    "suitCoat_darkWood": "base_darkWood.png",
    "suitCoat_metal": "base_bluredMetal.png",
    "teacherDesk_metal": "base_bluredMetal.png",
    "teacherDesk_wood": "base_brownWood.jpg",
    "varnishedWoodDoor": "base_brownWood.jpg",
    "wallClock_whiteBackground": "wallClock.png",
    "woodFloor": "base_woodFloor.jpg",
    "woodPlanks": "woodPlanks.jpg",
    "worldMap": "europeMap.png",
    "zapBook_color": "zapBook.jpg",
}

ALPHA_MASK_IMAGE_BY_MATERIAL: dict[str, str] = {
    "dustBin_wireframe": "dustbin_wireframe.png",
}

BASE_COLOR_FACTOR_OVERRIDES: dict[str, tuple[float, float, float, float]] = {
    "base_carbon": (0.14731897, 0.14731897, 0.14731897, 1.0),
    "beigeBook_cover": (0.22367015, 0.18186766, 0.10361500, 1.0),
    "blackPaintedWood": (0.03630647, 0.02979981, 0.02515635, 1.0),
    "coloredPlastic": (0.55201149, 0.00699541, 0.0, 1.0),
    "schoolDesk_plastic": (0.48266399, 0.29951400, 0.16555700, 1.0),
    "teacherDesk_plastic": (0.48266399, 0.29951400, 0.16555700, 1.0),
    "wallClock_blackPlastic": (0.07, 0.07, 0.07, 1.0),
    "wallClock_darkWood": (0.02633226, 0.02485024, 0.02396628, 1.0),
    "white_glossPaint": (0.8, 0.8, 0.8, 1.0),
    "whitePaintedWindow": (0.8, 0.8, 0.8, 1.0),
}


def parse_arguments() -> argparse.Namespace:
    arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    return parser.parse_args(arguments)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def safe_output_directory(path: Path) -> Path:
    resolved = path.resolve()
    require(resolved.name not in {"", ".", ".."}, "output directory is too broad")
    require(resolved.parent != resolved, "output directory cannot be a filesystem root")
    require(len(resolved.parts) >= 4, "output directory is too broad")
    return resolved


def relative_source_path(path: Path, source_root: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(source_root).as_posix()
    except ValueError:
        return resolved.name


def active_surface_nodes(material: bpy.types.Material) -> list[bpy.types.Node]:
    if material.node_tree is None:
        return []
    outputs = [
        node
        for node in material.node_tree.nodes
        if node.type == "OUTPUT_MATERIAL" and node.is_active_output
    ]
    if not outputs:
        outputs = [
            node for node in material.node_tree.nodes if node.type == "OUTPUT_MATERIAL"
        ]
    if not outputs:
        return []
    surface = outputs[0].inputs.get("Surface")
    pending = [link.from_node for link in surface.links] if surface else []
    result: list[bpy.types.Node] = []
    seen: set[int] = set()
    while pending:
        node = pending.pop(0)
        identity = int(node.as_pointer())
        if identity in seen:
            continue
        seen.add(identity)
        result.append(node)
        for socket in node.inputs:
            if socket.is_linked:
                pending.extend(link.from_node for link in socket.links)
    return result


def choose_shader_node(
    source: bpy.types.Material,
    category: str,
) -> bpy.types.Node | None:
    nodes = active_surface_nodes(source)
    if category == "emissive":
        order = ("EMISSION", "BSDF_GLASS", "BSDF_DIFFUSE", "GROUP", "BSDF_GLOSSY")
    elif category == "glass":
        order = ("BSDF_GLASS", "BSDF_DIFFUSE", "GROUP", "BSDF_GLOSSY")
    elif category == "metal":
        order = ("BSDF_GLOSSY", "GROUP", "BSDF_DIFFUSE")
    else:
        order = ("BSDF_DIFFUSE", "GROUP", "BSDF_PRINCIPLED", "BSDF_GLOSSY")
    for node_type in order:
        match = next((node for node in nodes if node.type == node_type), None)
        if match is not None:
            return match
    return None


def socket_color(socket: bpy.types.NodeSocket | None) -> tuple[float, float, float, float]:
    if socket is None:
        return (0.8, 0.8, 0.8, 1.0)
    value = socket.default_value
    try:
        values = [float(component) for component in value]
    except TypeError:
        return (0.8, 0.8, 0.8, 1.0)
    if len(values) < 3:
        return (0.8, 0.8, 0.8, 1.0)
    alpha = values[3] if len(values) >= 4 else 1.0
    return tuple(max(0.0, min(1.0, item)) for item in (*values[:3], alpha))


def source_color_socket(
    source: bpy.types.Material,
    category: str,
) -> bpy.types.NodeSocket | None:
    node = choose_shader_node(source, category)
    if node is None:
        return None
    socket = node.inputs.get("Base Color")
    if socket is None:
        socket = node.inputs.get("Color")
    return socket


def source_image_identifiers(image: bpy.types.Image) -> set[str]:
    result = {image.name}
    if image.filepath:
        result.add(Path(bpy.path.abspath(image.filepath)).name)
    return result


def upstream_color_image_names(socket: bpy.types.NodeSocket | None) -> set[str]:
    pending = [link.from_node for link in socket.links] if socket is not None else []
    seen: set[int] = set()
    result: set[str] = set()
    while pending:
        node = pending.pop(0)
        identity = int(node.as_pointer())
        if identity in seen:
            continue
        seen.add(identity)
        if node.type == "TEX_IMAGE" and node.image is not None:
            result.update(source_image_identifiers(node.image))
        for node_input in node.inputs:
            if node_input.is_linked:
                pending.extend(link.from_node for link in node_input.links)
    return result


def active_surface_image_names(source: bpy.types.Material) -> set[str]:
    result: set[str] = set()
    for node in active_surface_nodes(source):
        if node.type == "TEX_IMAGE" and node.image is not None:
            result.update(source_image_identifiers(node.image))
    return result


def source_base_color(
    source: bpy.types.Material,
    category: str,
) -> tuple[float, float, float, float]:
    override = BASE_COLOR_FACTOR_OVERRIDES.get(source.name)
    if override is not None:
        return override
    socket = source_color_socket(source, category)
    if socket is None:
        return tuple(float(value) for value in source.diffuse_color)
    return socket_color(socket)


def socket_scalar(socket: bpy.types.NodeSocket | None, fallback: float) -> float:
    if socket is None:
        return fallback
    try:
        value = float(socket.default_value)
    except (TypeError, ValueError):
        return fallback
    return value if math.isfinite(value) else fallback


def source_roughness(source: bpy.types.Material, category: str) -> float:
    nodes = active_surface_nodes(source)
    glossy = next((node for node in nodes if node.type == "BSDF_GLOSSY"), None)
    principled = next((node for node in nodes if node.type == "BSDF_PRINCIPLED"), None)
    diffuse = next((node for node in nodes if node.type == "BSDF_DIFFUSE"), None)
    translucent = next((node for node in nodes if node.type == "BSDF_TRANSLUCENT"), None)

    if category == "glass":
        # UVSR cannot draw transmission. A broad white dielectric preserves the
        # source's neutral glass intent without becoming a solid mirror.
        return 0.9 if source.name == "frostedGlass" else 0.75
    if glossy is not None:
        roughness = socket_scalar(glossy.inputs.get("Roughness"), 0.22)
        return max(0.02, min(1.0, roughness))
    if principled is not None:
        roughness = socket_scalar(principled.inputs.get("Roughness"), 0.6)
        return max(0.02, min(1.0, roughness))
    if diffuse is not None or translucent is not None:
        # Cycles Diffuse BSDF roughness controls Oren-Nayar diffusion, not the
        # microfacet roughness used by glTF. With no glossy branch, 1.0 is the
        # faithful non-specular approximation.
        return 1.0
    return 0.22 if category == "metal" else 0.6


def bump_settings(source: bpy.types.Material) -> dict | None:
    bumps = [node for node in active_surface_nodes(source) if node.type == "BUMP"]
    if not bumps:
        return None
    node = bumps[0]
    strength = socket_scalar(node.inputs.get("Strength"), 1.0)
    distance = socket_scalar(node.inputs.get("Distance"), 0.1)
    invert = bool(getattr(node, "invert", False))
    return {
        "node": node.name,
        "strength": strength,
        "distance": distance,
        "invert": invert,
        "heightLinked": bool(node.inputs.get("Height") and node.inputs["Height"].is_linked),
    }


def material_category(name: str) -> str:
    if name in DROP_MATERIALS:
        return "drop"
    if name in EMISSIVE_MATERIALS:
        return "emissive"
    if name in GLASS_MATERIALS:
        return "glass"
    if name in METAL_MATERIALS:
        return "metal"
    if name in MASK_MATERIALS:
        return "mask"
    if name in OPAQUE_MATERIALS:
        return "opaque"
    raise RuntimeError(f"unclassified Classroom material: {name}")


def index_source_images(source_root: Path) -> tuple[dict[str, Path], list[dict]]:
    indexed: dict[str, Path] = {}
    report: list[dict] = []
    for path in sorted(source_root.rglob("*"), key=lambda value: value.as_posix().casefold()):
        if not path.is_file() or path.suffix.casefold() not in {".png", ".jpg", ".jpeg"}:
            continue
        key = path.name.casefold()
        previous = indexed.get(key)
        require(previous is None, f"source image basename collision: {previous} and {path}")
        indexed[key] = path.resolve()
        report.append(
            {
                "path": relative_source_path(path, source_root),
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    return indexed, report


def load_image(
    path: Path,
    image_cache: dict[Path, bpy.types.Image],
    *,
    non_color: bool,
) -> bpy.types.Image:
    resolved = path.resolve()
    image = image_cache.get(resolved)
    if image is None:
        image = bpy.data.images.load(str(resolved), check_existing=True)
        image.name = resolved.name
        image.filepath = str(resolved)
        image_cache[resolved] = image
    if non_color:
        image.colorspace_settings.name = "Non-Color"
    return image


def build_materials(
    source_materials: dict[str, bpy.types.Material],
    source_images: dict[str, Path],
) -> tuple[dict[str, bpy.types.Material], list[dict]]:
    local_materials: dict[str, bpy.types.Material] = {}
    material_report: list[dict] = []
    image_cache: dict[Path, bpy.types.Image] = {}

    for name in sorted(EXPECTED_MATERIALS, key=str.casefold):
        source = source_materials[name]
        if name in OMITTED_SOURCE_OBJECT_MATERIALS:
            material_report.append(
                {
                    "sourceMaterial": name,
                    "outputMaterial": None,
                    "category": "omittedWithSourceObject",
                    "reason": "used only by the omitted spawn-corner trash cluster",
                }
            )
            continue
        category = material_category(name)
        if category == "drop":
            material_report.append(
                {
                    "sourceMaterial": name,
                    "category": category,
                    "reason": (
                        "Cycles-only daylight portal helper"
                        if name == "dayLight_portal"
                        else "thin glass cover would obscure the opaque clock face"
                    ),
                }
            )
            continue

        source_color = source_color_socket(source, category)
        source_color_images = upstream_color_image_names(source_color)
        source_color_image_keys = {value.casefold() for value in source_color_images}
        source_base_color_factor = source_base_color(source, category)
        color_image_name = BASE_IMAGE_BY_MATERIAL.get(name)
        alpha_mask_image_name = ALPHA_MASK_IMAGE_BY_MATERIAL.get(name)
        diagnostic_image_name = DIAGNOSTIC_UV_TEST_IMAGE_BY_MATERIAL.get(name)
        appearance_reference_name = BLANK_APPEARANCE_REFERENCE_BY_MATERIAL.get(name)
        require(
            sum(
                value is not None
                for value in (
                    color_image_name,
                    alpha_mask_image_name,
                    diagnostic_image_name,
                )
            )
            <= 1,
            f"material has conflicting base image policies: {name}",
        )
        if color_image_name is not None:
            require(
                color_image_name.casefold() in source_color_image_keys,
                f"base image is not reachable from the active source color socket: {name}",
            )
        elif alpha_mask_image_name is not None:
            active_image_keys = {
                value.casefold() for value in active_surface_image_names(source)
            }
            require(
                category == "mask"
                and alpha_mask_image_name.casefold() in active_image_keys,
                f"alpha mask is not reachable from the active source surface: {name}",
            )
        elif diagnostic_image_name is not None:
            require(
                diagnostic_image_name.casefold() in source_color_image_keys,
                f"diagnostic UV-test image is not reachable from the active source color socket: {name}",
            )
            diagnostic_image = bpy.data.images.get(diagnostic_image_name)
            require(
                diagnostic_image is not None
                and diagnostic_image.source == "GENERATED"
                and diagnostic_image.type == "UV_TEST"
                and diagnostic_image.generated_type == "UV_GRID"
                and tuple(diagnostic_image.size) == (2048, 2048),
                f"diagnostic UV-test image contract differs: {diagnostic_image_name}",
            )
            require(
                appearance_reference_name is not None
                and appearance_reference_name in source_materials,
                f"blank appearance reference is missing: {name}",
            )
            reference = source_materials[appearance_reference_name]
            reference_category = material_category(appearance_reference_name)
            reference_base_color_factor = source_base_color(
                reference,
                reference_category,
            )
            require(
                all(
                    abs(left - right) <= 1e-6
                    for left, right in zip(
                        source_base_color_factor,
                        reference_base_color_factor,
                    )
                ),
                f"diagnostic paper base color differs from {appearance_reference_name}",
            )
        else:
            require(
                not source_color_images,
                f"active source color images have no explicit export policy: {name}",
            )
        if source_color is not None and source_color.is_linked and not source_color_images:
            require(
                name in BASE_COLOR_FACTOR_OVERRIDES,
                f"linked non-image color network has no explicit approximation: {name}",
            )
        base_image_path: Path | None = None
        base_image: bpy.types.Image | None = None
        base_image_label: str | None = None
        base_image_source: str | None = None
        base_image_name = color_image_name or alpha_mask_image_name
        if base_image_name is not None:
            base_image_path = source_images.get(base_image_name.casefold())
            require(base_image_path is not None, f"base image is missing: {base_image_name}")
            base_image = load_image(base_image_path, image_cache, non_color=False)
            base_image_label = base_image_path.name
            base_image_source = (
                "archiveAlphaMask"
                if alpha_mask_image_name is not None
                else "archiveFile"
            )
        base_color_factor = (
            (1.0, 1.0, 1.0, 1.0)
            if base_image is not None and alpha_mask_image_name is None
            else source_base_color_factor
        )
        base_color_factor_source = (
            "whiteForSourceImage"
            if base_image is not None and alpha_mask_image_name is None
            else (
                "sourceFactorForAlphaMask"
                if alpha_mask_image_name is not None
                else (
                    "explicitLinkedNetworkApproximation"
                    if name in BASE_COLOR_FACTOR_OVERRIDES
                    else (
                        "blankAppearanceReference"
                        if appearance_reference_name is not None
                        else "sourceSocketDefault"
                    )
                )
            )
        )

        local = bpy.data.materials.new(name=f"{name}__UVSR_PBR")
        local.use_nodes = True
        local.surface_render_method = "DITHERED"
        nodes = local.node_tree.nodes
        nodes.clear()
        output = nodes.new("ShaderNodeOutputMaterial")
        principled = nodes.new("ShaderNodeBsdfPrincipled")
        local.node_tree.links.new(principled.outputs["BSDF"], output.inputs["Surface"])

        metallic = 1.0 if category == "metal" or name == "dustBin_wireframe" else 0.0
        roughness = source_roughness(source, category)
        if appearance_reference_name is not None:
            reference = source_materials[appearance_reference_name]
            reference_roughness = source_roughness(
                reference,
                material_category(appearance_reference_name),
            )
            require(
                abs(roughness - reference_roughness) <= 1e-6,
                f"diagnostic paper roughness differs from {appearance_reference_name}",
            )
        principled.inputs["Base Color"].default_value = base_color_factor
        principled.inputs["Metallic"].default_value = metallic
        principled.inputs["Roughness"].default_value = roughness
        principled.inputs["IOR"].default_value = 1.45
        principled.inputs["Alpha"].default_value = 1.0
        local.diffuse_color = base_color_factor

        if base_image is not None:
            texture = nodes.new("ShaderNodeTexImage")
            texture.name = "UVSR Base Color"
            texture.label = base_image_label
            texture.image = base_image
            texture.interpolation = "Linear"
            texture.extension = "REPEAT"
            local.node_tree.links.new(texture.outputs["Color"], principled.inputs["Base Color"])
            if category == "mask":
                local.node_tree.links.new(texture.outputs["Alpha"], principled.inputs["Alpha"])

        if category == "emissive":
            emission_color = base_color_factor
            emission_strength = 1.0
            principled.inputs["Emission Color"].default_value = emission_color
            principled.inputs["Emission Strength"].default_value = emission_strength
            if base_image is not None:
                base_texture = nodes.get("UVSR Base Color")
                local.node_tree.links.new(
                    base_texture.outputs["Color"],
                    principled.inputs["Emission Color"],
                )

        local_materials[name] = local
        source_bump = bump_settings(source)
        material_report.append(
            {
                "sourceMaterial": name,
                "outputMaterial": local.name,
                "category": category,
                "alphaMode": "MASK" if category == "mask" else "OPAQUE",
                "sourceBaseColorFactor": [
                    round(value, 8) for value in source_base_color_factor
                ],
                "baseColorFactor": [round(value, 8) for value in base_color_factor],
                "baseColorFactorSource": base_color_factor_source,
                "baseColorImage": base_image_label,
                "baseColorImageSource": base_image_source,
                "diagnosticSourceImage": diagnostic_image_name,
                "appearanceReferenceMaterial": appearance_reference_name,
                "sourceColorImages": sorted(source_color_images, key=str.casefold),
                "metallicFactor": metallic,
                "roughnessFactor": round(roughness, 8),
                "sourceBump": source_bump,
                "normalTexture": None,
            }
        )

    fallback = bpy.data.materials.new(name=DEFAULT_MATERIAL_NAME)
    fallback.use_nodes = True
    fallback.surface_render_method = "DITHERED"
    fallback_principled = fallback.node_tree.nodes.get("Principled BSDF")
    require(fallback_principled is not None, "default Principled material is missing")
    fallback_principled.inputs["Base Color"].default_value = (0.8, 0.8, 0.8, 1.0)
    fallback_principled.inputs["Metallic"].default_value = 0.0
    fallback_principled.inputs["Roughness"].default_value = 0.8
    local_materials[DEFAULT_MATERIAL_NAME] = fallback
    material_report.append(
        {
            "sourceMaterial": None,
            "outputMaterial": fallback.name,
            "category": "opaque",
            "alphaMode": "OPAQUE",
            "sourceBaseColorFactor": [0.8, 0.8, 0.8, 1.0],
            "baseColorFactor": [0.8, 0.8, 0.8, 1.0],
            "baseColorFactorSource": "fallback",
            "baseColorImage": None,
            "baseColorImageSource": None,
            "sourceColorImages": [],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.8,
            "sourceBump": None,
            "normalTexture": None,
            "reason": "Blender default material for source geometry with no material slots",
        }
    )

    return local_materials, material_report


def mesh_triangle_count(mesh: bpy.types.Mesh) -> int:
    mesh.calc_loop_triangles()
    return len(mesh.loop_triangles)


def remove_dropped_faces_and_replace_materials(
    mesh: bpy.types.Mesh,
    local_materials: dict[str, bpy.types.Material],
) -> tuple[Counter, int]:
    if len(mesh.materials) == 0:
        mesh.materials.append(local_materials[DEFAULT_MATERIAL_NAME])
    source_slots = list(mesh.materials)
    dropped_faces: Counter = Counter()
    dropped_indices = {
        index
        for index, material in enumerate(source_slots)
        if material is not None and material.name in DROP_MATERIALS
    }
    if dropped_indices:
        editable = bmesh.new()
        editable.from_mesh(mesh)
        faces = [face for face in editable.faces if face.material_index in dropped_indices]
        for face in faces:
            material = source_slots[face.material_index]
            dropped_faces[material.name] += max(len(face.verts) - 2, 0)
        bmesh.ops.delete(editable, geom=faces, context="FACES")
        editable.to_mesh(mesh)
        editable.free()

    used_old_indices = sorted({polygon.material_index for polygon in mesh.polygons})
    remap: dict[int, int] = {}
    replacement_slots: list[bpy.types.Material] = []
    for old_index in used_old_indices:
        require(
            old_index < len(source_slots),
            f"mesh {mesh.name} polygon references material slot {old_index}, "
            f"but only {len(source_slots)} slots exist",
        )
        source = source_slots[old_index]
        if source is None:
            source = local_materials[DEFAULT_MATERIAL_NAME]
        require(source.name not in DROP_MATERIALS, "dropped material face survived deletion")
        replacement = local_materials.get(source.name)
        require(replacement is not None, f"no PBR replacement for material {source.name}")
        remap[old_index] = len(replacement_slots)
        replacement_slots.append(replacement)

    for polygon in mesh.polygons:
        polygon.material_index = remap[polygon.material_index]
    mesh.materials.clear()
    for material in replacement_slots:
        mesh.materials.append(material)
    mesh.update()
    return dropped_faces, mesh_triangle_count(mesh)


def spawn_corner_omitted_owner_names_by_pointer(
    scene: bpy.types.Scene,
) -> dict[int, str]:
    root = scene.objects.get(OMITTED_SOURCE_ROOT_NAME)
    require(
        root is not None
        and root.type == "EMPTY"
        and root.instance_type == "COLLECTION"
        and root.instance_collection is not None
        and root.instance_collection.name == "dustBin",
        "spawn-corner dustBin root contract differs",
    )
    descendants = list(root.children_recursive)
    hierarchy = [root, *descendants]
    actual_names = {obj.name for obj in hierarchy}
    require(
        actual_names == EXPECTED_OMITTED_OWNER_NAMES
        and len(hierarchy) == len(EXPECTED_OMITTED_OWNER_NAMES)
        and len(root.children) == 13
        and len(descendants) == 13,
        "spawn-corner dustBin hierarchy differs: " + repr(sorted(actual_names)),
    )
    for child in descendants:
        require(
            child.parent == root
            and child.type == "EMPTY"
            and child.instance_type == "COLLECTION"
            and child.instance_collection is not None
            and child.instance_collection.name == "crinkledPaper",
            f"spawn-corner paper owner contract differs: {child.name}",
        )
    return {int(obj.as_pointer()): obj.name for obj in hierarchy}


def collect_source_instances(
    scene: bpy.types.Scene,
) -> tuple[list[dict], object, dict[str, bpy.types.Material]]:
    bpy.context.window.scene = scene
    scene.frame_set(1)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    instances: list[dict] = []
    converted_meshes: dict[tuple[int, str], bpy.types.Mesh | None] = {}
    source_materials: dict[str, bpy.types.Material] = {}
    for instance_index, instance in enumerate(depsgraph.object_instances):
        evaluated = instance.object
        if evaluated.type not in {"MESH", "CURVE"} or evaluated.hide_render:
            continue
        original = evaluated.original
        key = (int(original.as_pointer()), evaluated.type)
        if key not in converted_meshes:
            try:
                converted_meshes[key] = bpy.data.meshes.new_from_object(
                    evaluated,
                    preserve_all_data_layers=True,
                    depsgraph=depsgraph,
                )
            except RuntimeError as error:
                require(
                    "does not have geometry data" in str(error),
                    f"could not evaluate {evaluated.name}: {error}",
                )
                converted_meshes[key] = None
        for material in evaluated.data.materials:
            if material is None:
                continue
            previous = source_materials.get(material.name)
            require(
                previous is None or previous.as_pointer() == material.as_pointer(),
                f"material name resolves to different datablocks: {material.name}",
            )
            source_materials[material.name] = material
        instance_parent = instance.parent
        instance_owner = (
            instance_parent.original
            if instance_parent is not None
            else None
        )
        instances.append(
            {
                "index": instance_index,
                "evaluated": evaluated,
                "original": original,
                "key": key,
                "mesh": converted_meshes[key],
                "matrix": Matrix(instance.matrix_world),
                "sourceType": evaluated.type,
                "instanceOwnerPointer": (
                    int(instance_owner.as_pointer())
                    if instance_owner is not None
                    else None
                ),
                "instanceOwnerName": (
                    instance_owner.name
                    if instance_owner is not None
                    else None
                ),
            }
        )
    require(instances, "Classroom scene has no evaluated renderable objects")
    actual_materials = set(source_materials)
    require(
        actual_materials == EXPECTED_MATERIALS,
        "Classroom material inventory changed: missing="
        + repr(sorted(EXPECTED_MATERIALS - actual_materials))
        + ", unexpected="
        + repr(sorted(actual_materials - EXPECTED_MATERIALS)),
    )
    return instances, depsgraph, source_materials


def build_export_scene(
    source_scene: bpy.types.Scene,
    instances: list[dict],
    depsgraph: object,
    local_materials: dict[str, bpy.types.Material],
) -> tuple[bpy.types.Scene, dict]:
    export_scene = bpy.data.scenes.new(EXPORT_SCENE_NAME)
    export_scene.render.engine = "BLENDER_EEVEE"
    export_scene.unit_settings.system = "METRIC"
    export_scene.unit_settings.scale_length = 1.0
    export_collection = bpy.data.collections.new("Classroom")
    export_scene.collection.children.link(export_collection)

    mesh_cache: dict[tuple[int, str], bpy.types.Mesh | None] = {}
    mesh_statistics: dict[tuple[int, str], dict] = {}
    dropped_triangles: Counter = Counter()
    source_instance_triangles = 0
    output_instance_triangles = 0
    curve_instance_count = 0
    mesh_instance_count = 0
    omitted_instance_counts: Counter = Counter()
    omitted_triangles: Counter = Counter()
    omitted_owner_instance_counts: Counter = Counter()
    omitted_instance_records: list[dict] = []
    omitted_owner_names_by_pointer = spawn_corner_omitted_owner_names_by_pointer(
        source_scene
    )

    for item in instances:
        if item["sourceType"] == "CURVE":
            curve_instance_count += 1
        else:
            mesh_instance_count += 1

        source_object_name = item["original"].name
        owner_name = omitted_owner_names_by_pointer.get(item["instanceOwnerPointer"])
        if owner_name is not None:
            mesh = item["mesh"]
            triangles = (
                mesh_triangle_count(mesh)
                if mesh is not None and len(mesh.polygons) > 0
                else 0
            )
            source_instance_triangles += triangles
            omitted_instance_counts[source_object_name] += 1
            omitted_triangles[source_object_name] += triangles
            omitted_owner_instance_counts[owner_name] += 1
            omitted_instance_records.append(
                {
                    "sourceInstanceIndex": item["index"],
                    "owner": owner_name,
                    "sourceObject": source_object_name,
                    "triangles": triangles,
                }
            )
            continue

        key = item["key"]
        if key not in mesh_cache:
            mesh = item["mesh"]
            if mesh is None or len(mesh.polygons) == 0:
                mesh_cache[key] = None
                mesh_statistics[key] = {
                    "sourceTriangles": 0,
                    "outputTriangles": 0,
                    "droppedTriangles": {},
                }
            else:
                mesh.name = f"{item['original'].name}__UVSR"
                source_triangles = mesh_triangle_count(mesh)
                dropped, output_triangles = remove_dropped_faces_and_replace_materials(
                    mesh,
                    local_materials,
                )
                mesh_cache[key] = mesh if output_triangles > 0 else None
                mesh_statistics[key] = {
                    "sourceTriangles": source_triangles,
                    "outputTriangles": output_triangles,
                    "droppedTriangles": dict(dropped),
                }

        statistics = mesh_statistics[key]
        source_instance_triangles += statistics["sourceTriangles"]
        output_instance_triangles += statistics["outputTriangles"]
        for material, triangles in statistics["droppedTriangles"].items():
            dropped_triangles[material] += triangles
        mesh = mesh_cache[key]
        if mesh is None:
            continue
        obj = bpy.data.objects.new(
            f"{item['original'].name}__{item['index']:04d}",
            mesh,
        )
        obj.matrix_world = item["matrix"]
        export_collection.objects.link(obj)

    require(
        source_instance_triangles == EXPECTED_FRAME_ONE_TRIANGLES,
        f"evaluated frame-1 triangle baseline changed: {source_instance_triangles}",
    )
    require(
        dict(omitted_owner_instance_counts)
        == EXPECTED_OMITTED_OWNER_INSTANCE_COUNTS,
        "spawn-corner omitted owner inventory changed: "
        + repr(dict(omitted_owner_instance_counts)),
    )
    require(
        dict(omitted_instance_counts)
        == EXPECTED_OMITTED_SOURCE_OBJECT_INSTANCE_COUNTS,
        "spawn-corner omitted instance inventory changed: "
        + repr(dict(omitted_instance_counts)),
    )
    require(
        dict(omitted_triangles) == EXPECTED_OMITTED_TRIANGLES_BY_SOURCE_OBJECT,
        "spawn-corner omitted triangle inventory changed: "
        + repr(dict(omitted_triangles)),
    )
    require(
        output_instance_triangles
        + sum(dropped_triangles.values())
        + sum(omitted_triangles.values())
        == source_instance_triangles,
        "deliberate object and material drops do not reconcile with source triangles",
    )

    source_camera = source_scene.camera or bpy.data.objects.get("renderCam")
    require(source_camera is not None and source_camera.type == "CAMERA", "renderCam is missing")
    camera_data = source_camera.data.copy()
    camera_data.name = "renderCam"
    camera = bpy.data.objects.new("renderCam", camera_data)
    camera.matrix_world = Matrix(source_camera.matrix_world)
    export_collection.objects.link(camera)
    export_scene.camera = camera

    return export_scene, {
        "evaluatedInstances": len(instances),
        "meshInstances": mesh_instance_count,
        "curveInstancesConverted": curve_instance_count,
        "uniqueEvaluatedMeshes": len(mesh_cache),
        "sourceFrameOneTriangles": source_instance_triangles,
        "omittedSourceHierarchy": {
            "root": OMITTED_SOURCE_ROOT_NAME,
            "owners": sorted(EXPECTED_OMITTED_OWNER_NAMES, key=str.casefold),
            "instancesByOwner": dict(sorted(omitted_owner_instance_counts.items())),
            "instances": omitted_instance_records,
        },
        "omittedInstancesBySourceObject": dict(sorted(omitted_instance_counts.items())),
        "omittedTrianglesBySourceObject": dict(sorted(omitted_triangles.items())),
        "omittedTrianglesTotal": sum(omitted_triangles.values()),
        "droppedTrianglesByMaterial": dict(sorted(dropped_triangles.items())),
        "exportedTrianglesExpected": output_instance_triangles,
    }


def count_gltf_triangles(document: dict) -> int:
    accessors = document.get("accessors", [])
    meshes = document.get("meshes", [])
    nodes = document.get("nodes", [])
    scenes = document.get("scenes", [])
    scene_index = document.get("scene", 0)
    require(0 <= scene_index < len(scenes), "glTF default scene is missing")

    mesh_triangles: list[int] = []
    for mesh in meshes:
        total = 0
        for primitive in mesh.get("primitives", []):
            require(primitive.get("mode", 4) == 4, "glTF primitive is not triangles")
            accessor_index = primitive.get("indices")
            if accessor_index is None:
                position_index = primitive.get("attributes", {}).get("POSITION")
                require(position_index is not None, "glTF primitive has no POSITION")
                count = accessors[position_index]["count"]
            else:
                count = accessors[accessor_index]["count"]
            require(count % 3 == 0, "glTF primitive index count is not divisible by three")
            total += count // 3
        mesh_triangles.append(total)

    total = 0
    pending = list(scenes[scene_index].get("nodes", []))
    visited: set[int] = set()
    while pending:
        node_index = pending.pop()
        require(0 <= node_index < len(nodes), "glTF scene references a missing node")
        require(node_index not in visited, "glTF scene graph repeats or cycles a node")
        visited.add(node_index)
        node = nodes[node_index]
        pending.extend(node.get("children", []))
        mesh_index = node.get("mesh")
        if mesh_index is not None:
            require(0 <= mesh_index < len(mesh_triangles), "glTF node references a missing mesh")
            total += mesh_triangles[mesh_index]
    return total


def postprocess_and_audit_gltf(
    gltf_path: Path,
    material_report: list[dict],
    expected_triangles: int,
) -> dict:
    document = json.loads(gltf_path.read_text(encoding="utf-8"))
    policies = {
        item["outputMaterial"]: item
        for item in material_report
        if item.get("outputMaterial") is not None
    }
    output_material_names: set[str] = set()
    for material in document.get("materials", []):
        name = material.get("name")
        require(name in policies, f"glTF contains an unclassified material: {name}")
        require(name not in output_material_names, f"glTF material name is duplicated: {name}")
        output_material_names.add(name)
        policy = policies[name]
        pbr = material.setdefault("pbrMetallicRoughness", {})
        pbr["metallicFactor"] = policy["metallicFactor"]
        pbr["roughnessFactor"] = policy["roughnessFactor"]
        pbr["baseColorFactor"] = policy["baseColorFactor"]
        material["alphaMode"] = policy["alphaMode"]
        if policy["alphaMode"] == "MASK":
            material["alphaCutoff"] = 0.5
        else:
            material.pop("alphaCutoff", None)
        extensions = material.get("extensions", {})
        extensions.pop("KHR_materials_transmission", None)
        if not extensions:
            material.pop("extensions", None)
        if policy["baseColorImage"] is not None:
            require("baseColorTexture" in pbr, f"glTF lost base color texture for {name}")
            if policy["baseColorImageSource"] != "archiveAlphaMask":
                require(
                    policy["baseColorFactor"] == [1.0, 1.0, 1.0, 1.0],
                    f"glTF texture factor is not white for {name}",
                )
        require(
            "normalTexture" not in material,
            f"glTF unexpectedly contains a generated normal texture for {name}",
        )

    required_materials = {
        item["outputMaterial"]
        for item in material_report
        if item.get("outputMaterial") is not None
    }
    missing_materials = required_materials - output_material_names
    expected_missing_materials = {
        f"{name}__UVSR_PBR" for name in EXPECTED_UNUSED_MATERIALS
    }
    require(
        missing_materials == expected_missing_materials
        and not (output_material_names - required_materials),
        "glTF material inventory differs: missing="
        + repr(sorted(missing_materials))
        + ", unexpected="
        + repr(sorted(output_material_names - required_materials)),
    )

    extensions_used = document.get("extensionsUsed", [])
    if "KHR_materials_transmission" in extensions_used:
        extensions_used.remove("KHR_materials_transmission")
    if extensions_used:
        document["extensionsUsed"] = extensions_used
    else:
        document.pop("extensionsUsed", None)

    require(document.get("images"), "glTF export contains no images")
    require(document.get("textures"), "glTF export contains no textures")
    require(
        len(document["materials"]) == EXPECTED_GLTF_MATERIAL_COUNT,
        "glTF material count differs",
    )
    require(
        len(document["images"]) == EXPECTED_GLTF_IMAGE_COUNT,
        "glTF source-image count differs",
    )
    require(
        len(document["textures"]) == EXPECTED_GLTF_TEXTURE_COUNT,
        "glTF texture binding count differs",
    )
    require(document.get("cameras") and len(document["cameras"]) == 1, "glTF camera count differs")
    exported_triangles = count_gltf_triangles(document)
    require(
        exported_triangles == expected_triangles,
        f"glTF exported {exported_triangles} triangles, expected {expected_triangles}",
    )

    gltf_path.write_text(
        json.dumps(document, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return {
        "nodes": len(document.get("nodes", [])),
        "meshes": len(document.get("meshes", [])),
        "materials": len(document.get("materials", [])),
        "images": len(document.get("images", [])),
        "textures": len(document.get("textures", [])),
        "cameras": len(document.get("cameras", [])),
        "buffers": len(document.get("buffers", [])),
        "bufferViews": len(document.get("bufferViews", [])),
        "accessors": len(document.get("accessors", [])),
        "triangles": exported_triangles,
        "alphaModes": dict(
            Counter(material.get("alphaMode", "OPAQUE") for material in document["materials"])
        ),
        "extensionsUsed": document.get("extensionsUsed", []),
        "unusedSourceMaterials": sorted(EXPECTED_UNUSED_MATERIALS, key=str.casefold),
    }


def audit_source_image_exports(
    gltf_path: Path,
    source_root: Path,
    source_images: dict[str, Path],
) -> list[dict]:
    document = json.loads(gltf_path.read_text(encoding="utf-8"))
    output_root = gltf_path.parent.parent.resolve()
    result: list[dict] = []
    seen_output_paths: set[str] = set()
    archive_copy_count = 0
    for image_item in document.get("images", []):
        uri = image_item.get("uri")
        require(isinstance(uri, str) and uri, "glTF image has no external URI")
        relative = Path(unquote(uri))
        require(
            not relative.is_absolute() and ".." not in relative.parts,
            f"glTF image URI is unsafe: {uri}",
        )
        output_path = (gltf_path.parent / relative).resolve()
        try:
            output_relative = output_path.relative_to(output_root).as_posix()
        except ValueError as exception:
            raise RuntimeError(f"glTF image escapes the export root: {uri}") from exception
        require(output_path.is_file(), f"glTF image is missing: {output_path}")
        key = output_relative.casefold()
        require(key not in seen_output_paths, f"glTF image URI is duplicated: {uri}")
        seen_output_paths.add(key)

        item = {
            "outputPath": output_relative,
            "outputBytes": output_path.stat().st_size,
            "outputSha256": sha256(output_path),
        }
        require(
            output_path.name.casefold() != "checker.png",
            "source UV-test checker must not be packaged",
        )
        source_path = source_images.get(output_path.name.casefold())
        require(source_path is not None, f"exported image has no archive source: {uri}")
        source_hash = sha256(source_path)
        require(
            output_path.stat().st_size == source_path.stat().st_size
            and item["outputSha256"] == source_hash,
            f"exported image changed source bytes: {output_path.name}",
        )
        item.update(
            {
                "sourceType": "archiveFile",
                "sourcePath": relative_source_path(source_path, source_root),
                "sourceBytes": source_path.stat().st_size,
                "sourceSha256": source_hash,
            }
        )
        archive_copy_count += 1
        result.append(item)

    require(
        archive_copy_count == EXPECTED_ARCHIVE_IMAGE_COPY_COUNT,
        f"export copied {archive_copy_count} archive images, expected {EXPECTED_ARCHIVE_IMAGE_COPY_COUNT}",
    )
    return sorted(result, key=lambda item: item["outputPath"].casefold())


def file_inventory(root: Path) -> list[dict]:
    result: list[dict] = []
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix().casefold()):
        if not path.is_file():
            continue
        if path.name == "blender-export-report.json":
            continue
        size = path.stat().st_size
        require(size < TRACKED_FILE_LIMIT_BYTES, f"output reaches GitHub limit: {path}")
        result.append(
            {
                "path": path.relative_to(root).as_posix(),
                "bytes": size,
                "sha256": sha256(path),
            }
        )
    return result


def main() -> None:
    arguments = parse_arguments()
    source_root = arguments.source_root.resolve()
    output_directory = safe_output_directory(arguments.output_directory)
    source_blend = (source_root / "classroom.blend").resolve()
    require(source_root.is_dir(), f"source root is missing: {source_root}")
    require(source_blend.is_file(), f"source blend is missing: {source_blend}")
    require(sha256(source_blend) == EXPECTED_SOURCE_BLEND_SHA256, "source blend SHA-256 differs")
    require(tuple(bpy.app.version) == EXPECTED_BLENDER_VERSION, "Blender version differs")
    build_hash = bpy.app.build_hash
    if isinstance(build_hash, bytes):
        build_hash = build_hash.decode("ascii")
    require(build_hash == EXPECTED_BLENDER_BUILD_HASH, "Blender build hash differs")
    require(Path(bpy.data.filepath).resolve() == source_blend, "opened blend is not the requested source")

    libraries = sorted(bpy.data.libraries, key=lambda item: item.filepath.casefold())
    require(len(libraries) == EXPECTED_LIBRARY_COUNT, f"linked library count differs: {len(libraries)}")
    library_report: list[dict] = []
    for library in libraries:
        path = Path(bpy.path.abspath(library.filepath)).resolve()
        require(path.is_file(), f"linked library is missing: {path}")
        library_report.append(
            {
                "path": relative_source_path(path, source_root),
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
        )

    source_scene = bpy.data.scenes.get(SCENE_NAME)
    require(source_scene is not None, f"source scene is missing: {SCENE_NAME}")
    instances, depsgraph, source_materials = collect_source_instances(source_scene)
    source_images, source_image_report = index_source_images(source_root)
    local_materials, material_report = build_materials(
        source_materials,
        source_images,
    )
    export_scene, geometry_report = build_export_scene(
        source_scene,
        instances,
        depsgraph,
        local_materials,
    )

    if output_directory.exists():
        shutil.rmtree(output_directory)
    output_directory.mkdir(parents=True)
    components = output_directory / "components"
    components.mkdir()
    gltf_path = components / "blender_classroom.gltf"
    bpy.context.window.scene = export_scene
    export_result = bpy.ops.export_scene.gltf(
        filepath=str(gltf_path),
        check_existing=False,
        export_format="GLTF_SEPARATE",
        export_texture_dir="textures",
        export_image_format="AUTO",
        export_keep_originals=False,
        export_materials="EXPORT",
        export_cameras=True,
        export_lights=False,
        export_animations=False,
        export_current_frame=True,
        export_apply=True,
        export_skins=False,
        export_morph=False,
        export_tangents=True,
        export_yup=True,
        use_active_scene=True,
        use_renderable=True,
        use_visible=False,
    )
    require(export_result == {"FINISHED"}, f"glTF export failed: {export_result}")

    gltf_report = postprocess_and_audit_gltf(
        gltf_path,
        material_report,
        geometry_report["exportedTrianglesExpected"],
    )
    source_image_exports = audit_source_image_exports(
        gltf_path,
        source_root,
        source_images,
    )
    report = {
        "schemaVersion": 1,
        "scene": "blender_classroom",
        "displayName": "Blender Classroom",
        "blenderVersion": bpy.app.version_string,
        "blenderBuildHash": EXPECTED_BLENDER_BUILD_HASH,
        "source": {
            "blend": "classroom.blend",
            "bytes": source_blend.stat().st_size,
            "sha256": sha256(source_blend),
            "activeScene": SCENE_NAME,
            "frame": 1,
            "libraries": library_report,
            "images": source_image_report,
        },
        "conversion": {
            "evaluatedStaticFrame": True,
            "linkedCollectionInstancesRealized": True,
            "curvesConvertedToMeshes": True,
            "skinsExported": False,
            "animationsExported": False,
            "lightsExported": False,
            "cameraExported": True,
            "coordinateMapping": "Blender Z-up (x,y,z) to glTF Y-up (x,z,-y)",
            "materialPolicy": {
                "opaqueOrMaskOnly": True,
                "transmissionPreserved": False,
                "sourceBaseColorImagesCopiedByteForByte": True,
                "diagnosticUvTestImagePolicy": {
                    name: {
                        "sourceImage": DIAGNOSTIC_UV_TEST_IMAGE_BY_MATERIAL[name],
                        "appearanceReferenceMaterial": reference,
                        "packaged": False,
                    }
                    for name, reference in sorted(
                        BLANK_APPEARANCE_REFERENCE_BY_MATERIAL.items()
                    )
                },
                "sourceGeneratedUvTestImagesPackaged": False,
                "texturedBaseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "generatedNormalTextures": False,
                "sourceBumpPolicy": (
                    "audited but omitted to preserve the source-authored image appearance"
                ),
                "sourceBumpMaterials": sorted(
                    (
                        item["sourceMaterial"]
                        for item in material_report
                        if item.get("sourceBump") is not None
                    ),
                    key=str.casefold,
                ),
                "materials": material_report,
            },
            "sourceImageExports": source_image_exports,
        },
        "geometry": geometry_report,
        "gltf": gltf_report,
        "exportResult": sorted(export_result),
    }
    report_path = output_directory / "blender-export-report.json"
    report_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    report["files"] = file_inventory(output_directory)
    report_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        "UVSR_CLASSROOM_EXPORT="
        + json.dumps(
            {
                "outputDirectory": str(output_directory),
                "sourceTriangles": geometry_report["sourceFrameOneTriangles"],
                "outputTriangles": gltf_report["triangles"],
                "materials": gltf_report["materials"],
                "images": gltf_report["images"],
                "buffers": gltf_report["buffers"],
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
