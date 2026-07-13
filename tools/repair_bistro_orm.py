"""Repair UVSR's converted Bistro GLBs without rewriting their binary payloads.

The converter placed Bistro's packed occlusion/roughness/metalness texture in
KHR_materials_specular.specularTexture. Donut correctly ignores that texture
for the metallic-roughness workflow, leaving glTF's default metalness of one.
This tool moves the texture reference to metallicRoughnessTexture. The
converted files contain zero in the red channel, so it must not be bound as
authored material occlusion; UVSR's screen-space visibility remains separate.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


GLB_MAGIC = b"glTF"
JSON_CHUNK_TYPE = 0x4E4F534A
REPAIR_VERSION = 4

EXPECTED_ASSETS = {
    "BistroExterior.glb": {
        "material_count": 132,
        "dielectric_materials": {"Paris_LiquorBottle_01_Labels"},
        # The Blender export marks every material BLEND. Only these base-color
        # images contain a nontrivial alpha channel (both 0 and 255); treating
        # the other 120 materials as alpha-tested creates patterned holes.
        "alpha_tested_materials": {
            "Foliage_Bux_Hedges46.DoubleSided",
            "Foliage_Flowers.DoubleSided",
            "Foliage_Ivy_leaf_a.DoubleSided",
            "Foliage_Leaves.DoubleSided",
            "Foliage_Linde_Tree_Large_Green_Leaves.DoubleSided",
            "Foliage_Linde_Tree_Large_Orange_Leaves.DoubleSided",
            "LanternEmissive",
            "MASTER_Curtains",
            "MASTER_Forge_Metal",
            "MASTER_Side_Letters",
        },
        "blended_materials": set(),
    },
    "BistroInterior_Wine.glb": {
        "material_count": 74,
        "dielectric_materials": {
            "Water",
            "Ice",
            "Paris_Table_cloth_01",
            "Beer",
            "Lightbulb",
            "Red_Wine",
            "White_Wine",
        },
        "alpha_tested_materials": {
            "MASTER_Interior_01_Paris_Lantern1",
            "Plants_plants.DoubleSided",
            "Plates_Details",
        },
        "blended_materials": {
            "Water",
            "Ice",
            "Beer",
            "Red_Wine",
            "White_Wine",
        },
    },
}


def validate_bistro_document(path: Path, document: dict) -> dict:
    expected = EXPECTED_ASSETS.get(path.name)
    if expected is None:
        raise RuntimeError(f"{path}: not a recognized UVSR Bistro asset")

    generator = document.get("asset", {}).get("generator", "")
    materials = document.get("materials", [])
    material_names = {material.get("name") for material in materials}
    if (
        not generator.startswith("Khronos glTF Blender I/O")
        or len(materials) != expected["material_count"]
        or not expected["dielectric_materials"].issubset(material_names)
    ):
        raise RuntimeError(f"{path}: Bistro conversion signature does not match")
    return expected


def repair_glb(path: Path) -> int:
    with path.open("r+b") as glb:
        header = glb.read(20)
        if len(header) != 20:
            raise RuntimeError(f"{path}: truncated GLB header")

        magic, version, total_length, json_length, json_type = struct.unpack(
            "<4sIIII", header
        )
        if magic != GLB_MAGIC or version != 2 or json_type != JSON_CHUNK_TYPE:
            raise RuntimeError(f"{path}: unsupported GLB container")
        if path.stat().st_size != total_length:
            raise RuntimeError(f"{path}: GLB length field does not match file size")

        json_bytes = glb.read(json_length)
        document = json.loads(json_bytes.decode("utf-8").rstrip(" \x00"))
        expected = validate_bistro_document(path, document)
        previous_repair = document.get("asset", {}).get("extras", {}).get(
            "uvsrBistroOrmRepaired", 0
        )

        repaired_materials: set[int] = set()
        for material_index, material in enumerate(document.get("materials", [])):
            material_name = material.get("name")
            if material_name in expected["alpha_tested_materials"]:
                desired_alpha_mode = "MASK"
            elif material_name in expected["blended_materials"]:
                desired_alpha_mode = "BLEND"
            else:
                desired_alpha_mode = "OPAQUE"
            if desired_alpha_mode == "MASK":
                if material.get("alphaMode") != "MASK":
                    material["alphaMode"] = "MASK"
                    repaired_materials.add(material_index)
                if material.get("alphaCutoff", 0.5) != 0.5:
                    material["alphaCutoff"] = 0.5
                    repaired_materials.add(material_index)
            elif desired_alpha_mode == "BLEND":
                if material.get("alphaMode") != "BLEND":
                    material["alphaMode"] = "BLEND"
                    repaired_materials.add(material_index)
                if material.pop("alphaCutoff", None) is not None:
                    repaired_materials.add(material_index)
            else:
                if material.pop("alphaMode", None) is not None:
                    repaired_materials.add(material_index)
                if material.pop("alphaCutoff", None) is not None:
                    repaired_materials.add(material_index)

            extensions = material.get("extensions", {})
            specular = extensions.get("KHR_materials_specular", {})
            texture_info = specular.get("specularTexture")
            if texture_info is not None:
                metallic_roughness = material.setdefault("pbrMetallicRoughness", {})
                existing_texture = metallic_roughness.get("metallicRoughnessTexture")
                if existing_texture is not None and existing_texture != texture_info:
                    raise RuntimeError(
                        f"{path}: material {material.get('name', '<unnamed>')} has "
                        "conflicting packed material textures"
                    )
                metallic_roughness["metallicRoughnessTexture"] = texture_info
                specular.pop("specularTexture")
                repaired_materials.add(material_index)

            # The remaining textureless conversion artifacts are transparent,
            # cloth, labels, or emissive glass: all dielectric, never metal.
            if material.get("name") in expected["dielectric_materials"]:
                metallic_roughness = material.setdefault("pbrMetallicRoughness", {})
                if metallic_roughness.get("metallicFactor", 1.0) != 0.0:
                    metallic_roughness["metallicFactor"] = 0.0
                    repaired_materials.add(material_index)
                extensions.pop("KHR_materials_specular", None)
            elif not specular:
                extensions.pop("KHR_materials_specular", None)

            if not extensions:
                material.pop("extensions", None)

        # Version 1 of this repair also bound the texture's zero-filled red
        # channel as visibility. Remove only those tool-authored bindings.
        if previous_repair is True or previous_repair == 1:
            for material_index, material in enumerate(document.get("materials", [])):
                metallic_roughness_texture = material.get(
                    "pbrMetallicRoughness", {}
                ).get("metallicRoughnessTexture")
                if material.get("occlusionTexture") == metallic_roughness_texture:
                    material.pop("occlusionTexture", None)
                    repaired_materials.add(material_index)

        if not repaired_materials and previous_repair == REPAIR_VERSION:
            return 0
        if not repaired_materials:
            raise RuntimeError(f"{path}: no mislabeled Bistro ORM textures found")

        if not any(
            "KHR_materials_specular" in material.get("extensions", {})
            for material in document.get("materials", [])
        ):
            for extension_list in ("extensionsUsed", "extensionsRequired"):
                if extension_list in document:
                    remaining_extensions = [
                        extension
                        for extension in document[extension_list]
                        if extension != "KHR_materials_specular"
                    ]
                    if remaining_extensions:
                        document[extension_list] = remaining_extensions
                    else:
                        document.pop(extension_list)

        document.setdefault("asset", {}).setdefault("extras", {})[
            "uvsrBistroOrmRepaired"
        ] = REPAIR_VERSION
        repaired_json = json.dumps(document, separators=(",", ":")).encode("utf-8")
        if len(repaired_json) > json_length:
            raise RuntimeError(
                f"{path}: repaired JSON exceeds fixed chunk by "
                f"{len(repaired_json) - json_length} bytes"
            )

        backup = path.with_suffix(path.suffix + ".pre-uvsr-json")
        if backup.exists():
            backup_json = backup.read_bytes()
            if len(backup_json) != json_length:
                raise RuntimeError(f"{path}: existing JSON chunk backup has the wrong size")
            backup_document = json.loads(backup_json.decode("utf-8").rstrip(" \x00"))
            validate_bistro_document(path, backup_document)
        else:
            backup.write_bytes(json_bytes)

        glb.seek(20)
        glb.write(repaired_json)
        glb.write(b" " * (json_length - len(repaired_json)))
        return len(repaired_materials)


def restore_glb(path: Path) -> None:
    backup = path.with_suffix(path.suffix + ".pre-uvsr-json")
    if not backup.exists():
        raise RuntimeError(f"{path}: original JSON chunk backup is missing")

    with path.open("r+b") as glb:
        header = glb.read(20)
        if len(header) != 20:
            raise RuntimeError(f"{path}: truncated GLB header")
        magic, version, total_length, json_length, json_type = struct.unpack(
            "<4sIIII", header
        )
        if magic != GLB_MAGIC or version != 2 or json_type != JSON_CHUNK_TYPE:
            raise RuntimeError(f"{path}: unsupported GLB container")
        if path.stat().st_size != total_length:
            raise RuntimeError(f"{path}: GLB length field does not match file size")

        original_json = backup.read_bytes()
        if len(original_json) != json_length:
            raise RuntimeError(f"{path}: JSON chunk backup has the wrong size")
        document = json.loads(original_json.decode("utf-8").rstrip(" \x00"))
        validate_bistro_document(path, document)

        glb.seek(20)
        glb.write(original_json)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--restore",
        action="store_true",
        help="restore the original JSON chunks saved by the first repair",
    )
    parser.add_argument("glb", nargs="+", type=Path)
    args = parser.parse_args()

    for path in args.glb:
        if args.restore:
            restore_glb(path)
            state = "restored original JSON chunk"
        else:
            repaired = repair_glb(path)
            state = (
                f"repaired {repaired} materials" if repaired else "already repaired"
            )
        print(f"{path}: {state}")


if __name__ == "__main__":
    main()
