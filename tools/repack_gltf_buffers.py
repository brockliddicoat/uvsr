#!/usr/bin/env python3
"""Repack glTF buffer views into repository-safe external buffer files.

The output remains ordinary glTF 2.0. Geometry, animation, and embedded-image
buffer views are copied byte-for-byte; only each buffer view's buffer index and
byte offset change. This lets large assets use as many standard external
buffers as necessary without runtime concatenation or asset-quality loss.

Explicitly named blended or transmissive materials can also be flattened to an
opaque PBR approximation for renderers that do not submit those material
domains. Every JSON-only compatibility override is recorded in the report.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import BinaryIO, Iterable
from urllib.parse import unquote


GLB_MAGIC = 0x46546C67
GLB_VERSION = 2
GLB_JSON_CHUNK = 0x4E4F534A
GLB_BINARY_CHUNK = 0x004E4942
DEFAULT_MAX_BUFFER_BYTES = 90_000_000
TRACKED_FILE_LIMIT_BYTES = 100_000_000
COPY_BLOCK_BYTES = 4 * 1024 * 1024

# These extensions can refer to buffer storage outside ordinary bufferViews.
# Repacking them would require extension-specific offset rewrites, so fail
# closed instead of producing a subtly corrupted scene.
UNSUPPORTED_BUFFER_EXTENSIONS = {
    "EXT_meshopt_compression",
    "KHR_draco_mesh_compression",
}
TRANSMISSION_EXTENSION = "KHR_materials_transmission"


@dataclass(frozen=True)
class SourceBuffer:
    path: Path
    offset: int
    length: int


@dataclass(frozen=True)
class SourceContainer:
    document: dict
    buffers: tuple[SourceBuffer, ...]
    source_files: tuple[Path, ...]
    container_bytes: int
    source_bytes: int
    source_sha256: str
    kind: str


@dataclass(frozen=True)
class OutputBuffer:
    relative_path: str
    byte_length: int


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(COPY_BLOCK_BYTES), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def parse_nonnegative_integer(value: object, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{context} must be a nonnegative integer")
    return value


def load_glb(path: Path) -> SourceContainer:
    source_bytes = path.stat().st_size
    with path.open("rb") as stream:
        header = stream.read(12)
        if len(header) != 12:
            raise ValueError("GLB header is truncated")
        magic, version, declared_length = struct.unpack("<III", header)
        if magic != GLB_MAGIC or version != GLB_VERSION:
            raise ValueError("input is not GLB 2.0")
        if declared_length != source_bytes:
            raise ValueError(
                f"GLB length field is {declared_length}, file is {source_bytes} bytes"
            )

        chunks: list[tuple[int, int, int]] = []
        cursor = 12
        while cursor < source_bytes:
            stream.seek(cursor)
            chunk_header = stream.read(8)
            if len(chunk_header) != 8:
                raise ValueError("GLB chunk header is truncated")
            chunk_length, chunk_type = struct.unpack("<II", chunk_header)
            data_offset = cursor + 8
            data_end = data_offset + chunk_length
            if data_end > source_bytes:
                raise ValueError("GLB chunk extends beyond the file")
            chunks.append((chunk_type, data_offset, chunk_length))
            cursor = data_end

        if cursor != source_bytes or not chunks or chunks[0][0] != GLB_JSON_CHUNK:
            raise ValueError("GLB has an invalid chunk sequence")
        binary_chunks = [chunk for chunk in chunks if chunk[0] == GLB_BINARY_CHUNK]
        if len(binary_chunks) != 1:
            raise ValueError("GLB must contain exactly one binary chunk")
        if any(
            chunk_type not in {GLB_JSON_CHUNK, GLB_BINARY_CHUNK}
            for chunk_type, _, _ in chunks
        ):
            raise ValueError("GLB contains an unsupported extra chunk")

        _, json_offset, json_length = chunks[0]
        stream.seek(json_offset)
        json_bytes = stream.read(json_length).rstrip(b" \t\r\n\0")
        try:
            document = json.loads(json_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"GLB JSON is invalid: {error}") from error

    declared_buffers = document.get("buffers")
    if not isinstance(declared_buffers, list) or len(declared_buffers) != 1:
        raise ValueError("GLB JSON must declare exactly one buffer")
    declared_buffer = declared_buffers[0]
    if not isinstance(declared_buffer, dict) or "uri" in declared_buffer:
        raise ValueError("GLB binary buffer declaration is invalid")
    declared_bytes = parse_nonnegative_integer(
        declared_buffer.get("byteLength"), "GLB buffer byteLength"
    )
    _, binary_offset, binary_length = binary_chunks[0]
    if declared_bytes > binary_length or binary_length - declared_bytes > 3:
        raise ValueError("GLB binary chunk length does not match its buffer declaration")

    return SourceContainer(
        document=document,
        buffers=(SourceBuffer(path, binary_offset, declared_bytes),),
        source_files=(path,),
        container_bytes=source_bytes,
        source_bytes=source_bytes,
        source_sha256=sha256_file(path),
        kind="glb",
    )


def safe_external_uri(uri: object, context: str) -> PurePosixPath:
    if not isinstance(uri, str) or not uri or uri.startswith("data:"):
        raise ValueError(f"{context} must use a non-data relative URI")
    decoded = unquote(uri).replace("\\", "/")
    path = PurePosixPath(decoded)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        raise ValueError(f"{context} escapes its glTF directory: {uri}")
    if ":" in path.parts[0]:
        raise ValueError(f"{context} must not use a drive-qualified URI: {uri}")
    return path


def external_image_paths(document: dict, source_directory: Path) -> list[Path]:
    images = document.get("images", [])
    if not isinstance(images, list):
        raise ValueError("glTF images must be an array")
    paths: list[Path] = []
    seen: dict[str, str] = {}
    for index, image in enumerate(images):
        if not isinstance(image, dict):
            raise ValueError(f"glTF image {index} is invalid")
        if "uri" not in image:
            continue
        relative = safe_external_uri(image["uri"], f"glTF image {index}")
        relative_text = relative.as_posix()
        key = relative_text.casefold()
        previous = seen.get(key)
        if previous is not None and previous != relative_text:
            raise ValueError(
                f"glTF image URIs collide case-insensitively: "
                f"{previous} and {relative_text}"
            )
        if previous is not None:
            continue
        seen[key] = relative_text
        path = source_directory.joinpath(*relative.parts)
        if not path.is_file():
            raise FileNotFoundError(path)
        paths.append(path)
    return paths


def load_gltf(path: Path) -> SourceContainer:
    try:
        document = json.loads(path.read_text(encoding="utf-8-sig"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"glTF JSON is invalid: {error}") from error

    declared_buffers = document.get("buffers")
    if not isinstance(declared_buffers, list) or not declared_buffers:
        raise ValueError("glTF must declare at least one buffer")

    source_buffers: list[SourceBuffer] = []
    buffer_uris: dict[str, str] = {}
    for index, declared_buffer in enumerate(declared_buffers):
        if not isinstance(declared_buffer, dict):
            raise ValueError(f"glTF buffer {index} declaration is invalid")
        relative = safe_external_uri(
            declared_buffer.get("uri"), f"glTF buffer {index}"
        )
        relative_text = relative.as_posix()
        key = relative_text.casefold()
        previous = buffer_uris.get(key)
        if previous is not None:
            raise ValueError(
                f"glTF buffer URIs are duplicate or collide case-insensitively: "
                f"{previous} and {relative_text}"
            )
        buffer_uris[key] = relative_text
        buffer_path = path.parent.joinpath(*relative.parts)
        if not buffer_path.is_file():
            raise FileNotFoundError(buffer_path)
        declared_bytes = parse_nonnegative_integer(
            declared_buffer.get("byteLength"),
            f"glTF buffer {index} byteLength",
        )
        actual_bytes = buffer_path.stat().st_size
        if declared_bytes > actual_bytes or actual_bytes - declared_bytes > 3:
            raise ValueError(
                f"glTF buffer {index} length is {actual_bytes}, "
                f"declaration is {declared_bytes}"
            )
        source_buffers.append(SourceBuffer(buffer_path, 0, declared_bytes))

    source_files_by_key: dict[str, Path] = {path.name.casefold(): path}
    for source_file in [
        *(buffer.path for buffer in source_buffers),
        *external_image_paths(document, path.parent),
    ]:
        relative = source_file.relative_to(path.parent).as_posix()
        key = relative.casefold()
        previous = source_files_by_key.get(key)
        if previous is not None and previous != source_file:
            raise ValueError(
                f"glTF source files collide case-insensitively: "
                f"{previous} and {source_file}"
            )
        source_files_by_key[key] = source_file
    source_files = tuple(
        source_files_by_key[key]
        for key in sorted(source_files_by_key)
    )

    return SourceContainer(
        document=document,
        buffers=tuple(source_buffers),
        source_files=source_files,
        container_bytes=path.stat().st_size,
        source_bytes=sum(source_file.stat().st_size for source_file in source_files),
        source_sha256=sha256_file(path),
        kind="gltf",
    )


def load_container(path: Path) -> SourceContainer:
    suffix = path.suffix.casefold()
    if suffix == ".glb":
        return load_glb(path)
    if suffix == ".gltf":
        return load_gltf(path)
    raise ValueError("input must use the .glb or .gltf extension")


def copy_range(
    source: BinaryIO,
    destination: BinaryIO,
    source_offset: int,
    byte_length: int,
) -> None:
    source.seek(source_offset)
    remaining = byte_length
    while remaining:
        block = source.read(min(remaining, COPY_BLOCK_BYTES))
        if not block:
            raise ValueError("source buffer ended while copying a buffer view")
        destination.write(block)
        remaining -= len(block)


def compare_ranges(
    left: BinaryIO,
    left_offset: int,
    right: BinaryIO,
    right_offset: int,
    byte_length: int,
) -> bool:
    left.seek(left_offset)
    right.seek(right_offset)
    remaining = byte_length
    while remaining:
        block_bytes = min(remaining, COPY_BLOCK_BYTES)
        left_block = left.read(block_bytes)
        right_block = right.read(block_bytes)
        if (
            len(left_block) != block_bytes
            or len(right_block) != block_bytes
            or left_block != right_block
        ):
            return False
        remaining -= block_bytes
    return True


def copy_external_images(
    document: dict,
    source_directory: Path,
    output_directory: Path,
    reserved_paths: set[str],
) -> list[str]:
    copied: list[str] = []
    seen: dict[str, str] = {}
    images = document.get("images", [])
    if not isinstance(images, list):
        raise ValueError("glTF images must be an array")
    for index, image in enumerate(images):
        if not isinstance(image, dict):
            raise ValueError(f"glTF image {index} is invalid")
        if "uri" not in image:
            continue
        relative = safe_external_uri(image["uri"], f"glTF image {index}")
        relative_text = relative.as_posix()
        key = relative_text.casefold()
        previous = seen.get(key)
        if previous is not None and previous != relative_text:
            raise ValueError(
                f"glTF image URIs collide case-insensitively: "
                f"{previous} and {relative_text}"
            )
        if previous is not None:
            continue
        if key in reserved_paths:
            raise ValueError(
                f"glTF image URI collides with an output file: {relative_text}"
            )
        seen[key] = relative_text
        source = source_directory.joinpath(*relative.parts)
        destination = output_directory.joinpath(*relative.parts)
        if not source.is_file():
            raise FileNotFoundError(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            raise FileExistsError(destination)
        shutil.copy2(source, destination)
        copied.append(relative_text)
    copied.sort(key=str.casefold)
    return copied


def validate_document(document: dict) -> list[dict]:
    if not isinstance(document, dict):
        raise ValueError("glTF root must be an object")
    asset = document.get("asset")
    if not isinstance(asset, dict) or asset.get("version") != "2.0":
        raise ValueError("input must declare glTF asset version 2.0")
    extensions_used = document.get("extensionsUsed", [])
    if not isinstance(extensions_used, list) or not all(
        isinstance(value, str) for value in extensions_used
    ):
        raise ValueError("extensionsUsed must be an array of strings")
    unsupported = sorted(
        UNSUPPORTED_BUFFER_EXTENSIONS.intersection(extensions_used),
        key=str.casefold,
    )
    if unsupported:
        raise ValueError(
            "buffer repacking does not support: " + ", ".join(unsupported)
        )
    buffers = document.get("buffers")
    if not isinstance(buffers, list) or not buffers:
        raise ValueError("glTF must declare at least one buffer")
    for index, buffer in enumerate(buffers):
        if not isinstance(buffer, dict):
            raise ValueError(f"glTF buffer {index} declaration is invalid")
        unsupported_metadata = sorted(
            set(buffer).difference({"uri", "byteLength"}),
            key=str.casefold,
        )
        if unsupported_metadata:
            raise ValueError(
                f"glTF buffer {index} metadata cannot be preserved while splitting: "
                + ", ".join(unsupported_metadata)
            )
    buffer_views = document.get("bufferViews")
    if not isinstance(buffer_views, list) or not buffer_views:
        raise ValueError("glTF must contain at least one bufferView")
    if not all(isinstance(view, dict) for view in buffer_views):
        raise ValueError("every glTF bufferView must be an object")
    return buffer_views


def force_materials_opaque(
    document: dict,
    requested_names: Iterable[str],
) -> list[dict]:
    """Flatten explicitly named unsupported material domains and audit them."""
    names = list(requested_names)
    if not names:
        return []
    if any(not isinstance(name, str) or not name for name in names):
        raise ValueError("force-opaque material names must be nonempty strings")
    if len(set(names)) != len(names):
        raise ValueError("force-opaque material names must be unique")

    materials = document.get("materials")
    if not isinstance(materials, list):
        raise ValueError("force-opaque overrides require a glTF materials array")
    matches: list[tuple[int, dict]] = []
    for requested_name in names:
        indices = [
            index
            for index, material in enumerate(materials)
            if isinstance(material, dict) and material.get("name") == requested_name
        ]
        if len(indices) != 1:
            raise ValueError(
                f"force-opaque material {requested_name!r} must match exactly once; "
                f"found {len(indices)}"
            )
        index = indices[0]
        matches.append((index, materials[index]))
    matches.sort(key=lambda item: item[0])

    overrides: list[dict] = []
    meshes = document.get("meshes", [])
    if not isinstance(meshes, list):
        raise ValueError("glTF meshes must be an array")
    for material_index, material in matches:
        source_alpha_mode = material.get("alphaMode", "OPAQUE")
        if source_alpha_mode not in {"OPAQUE", "BLEND"}:
            raise ValueError(
                f"force-opaque material {material['name']!r} has unsupported "
                f"source alphaMode {source_alpha_mode!r}"
            )

        extensions = material.get("extensions", {})
        if not isinstance(extensions, dict):
            raise ValueError(
                f"force-opaque material {material['name']!r} has invalid extensions"
            )
        removed_extensions = {}
        if TRANSMISSION_EXTENSION in extensions:
            removed_extensions[TRANSMISSION_EXTENSION] = extensions.pop(
                TRANSMISSION_EXTENSION
            )
        if source_alpha_mode != "BLEND" and not removed_extensions:
            raise ValueError(
                f"force-opaque material {material['name']!r} is already opaque "
                "and has no transmission extension"
            )
        if not extensions:
            material.pop("extensions", None)

        pbr = material.get("pbrMetallicRoughness")
        if pbr is not None and not isinstance(pbr, dict):
            raise ValueError(
                f"force-opaque material {material['name']!r} has invalid PBR data"
            )
        factor = pbr.get("baseColorFactor") if pbr is not None else None
        if factor is not None:
            if (
                not isinstance(factor, list)
                or len(factor) != 4
                or any(
                    isinstance(value, bool) or not isinstance(value, (int, float))
                    for value in factor
                )
            ):
                raise ValueError(
                    f"force-opaque material {material['name']!r} has invalid "
                    "baseColorFactor"
                )
            source_base_color_alpha = factor[3]
        else:
            source_base_color_alpha = 1.0

        material["alphaMode"] = "OPAQUE"
        material.pop("alphaCutoff", None)
        primitive_count = sum(
            1
            for mesh in meshes
            if isinstance(mesh, dict)
            for primitive in mesh.get("primitives", [])
            if isinstance(primitive, dict)
            and primitive.get("material") == material_index
        )
        overrides.append(
            {
                "materialIndex": material_index,
                "materialName": material["name"],
                "primitiveCount": primitive_count,
                "sourceAlphaMode": source_alpha_mode,
                "sourceBaseColorAlpha": source_base_color_alpha,
                "removedExtensions": removed_extensions,
                "outputAlphaMode": "OPAQUE",
                "outputBaseColorAlpha": source_base_color_alpha,
            }
        )

    if not any(
        isinstance(material, dict)
        and isinstance(material.get("extensions"), dict)
        and TRANSMISSION_EXTENSION in material["extensions"]
        for material in materials
    ):
        for declaration in ("extensionsUsed", "extensionsRequired"):
            values = document.get(declaration)
            if isinstance(values, list):
                remaining = [
                    value for value in values if value != TRANSMISSION_EXTENSION
                ]
                if remaining:
                    document[declaration] = remaining
                else:
                    document.pop(declaration, None)

    return overrides


def repack(
    input_path: Path,
    output_directory: Path,
    gltf_name: str,
    buffer_base: str,
    max_buffer_bytes: int,
    source_id: str,
    report_name: str,
    force_opaque_material_names: Iterable[str] = (),
) -> dict:
    input_path = input_path.resolve()
    output_directory = output_directory.resolve()
    if not input_path.is_file():
        raise FileNotFoundError(input_path)
    if output_directory.exists():
        raise FileExistsError(
            f"output directory already exists; choose a fresh path: {output_directory}"
        )
    if max_buffer_bytes <= 0 or max_buffer_bytes >= 100_000_000:
        raise ValueError("max buffer bytes must be in the range 1..99,999,999")
    if Path(gltf_name).name != gltf_name or not gltf_name.casefold().endswith(".gltf"):
        raise ValueError("gltf name must be one .gltf filename")
    if Path(report_name).name != report_name or not report_name.casefold().endswith(".json"):
        raise ValueError("report name must be one .json filename")
    if not buffer_base or any(character not in "abcdefghijklmnopqrstuvwxyz0123456789_-" for character in buffer_base):
        raise ValueError("buffer base must contain lowercase ASCII letters, digits, hyphens, or underscores")

    container = load_container(input_path)
    document = json.loads(json.dumps(container.document))
    buffer_views = validate_document(document)
    material_overrides = force_materials_opaque(
        document,
        force_opaque_material_names,
    )

    pending = output_directory.with_name(
        output_directory.name + f".pending-{os.getpid()}"
    )
    if pending.exists():
        raise FileExistsError(pending)
    pending.mkdir(parents=True)
    buffer_directory = pending / "buffers"
    buffer_directory.mkdir()

    source_handles: dict[Path, BinaryIO] = {}
    output_handles: list[BinaryIO] = []
    output_buffers: list[OutputBuffer] = []
    mappings: list[tuple[int, int, int, int, int]] = []
    current_handle: BinaryIO | None = None
    current_size = 0
    copied_view_bytes = 0
    padding_bytes = 0

    try:
        for view_index, view in enumerate(buffer_views):
            source_buffer_index = parse_nonnegative_integer(
                view.get("buffer", 0), f"bufferView {view_index} buffer"
            )
            if source_buffer_index >= len(container.buffers):
                raise ValueError(f"bufferView {view_index} references a missing buffer")
            source_offset = parse_nonnegative_integer(
                view.get("byteOffset", 0), f"bufferView {view_index} byteOffset"
            )
            byte_length = parse_nonnegative_integer(
                view.get("byteLength"), f"bufferView {view_index} byteLength"
            )
            source_buffer = container.buffers[source_buffer_index]
            if source_offset + byte_length > source_buffer.length:
                raise ValueError(f"bufferView {view_index} exceeds its source buffer")
            if byte_length > max_buffer_bytes:
                raise ValueError(
                    f"bufferView {view_index} is {byte_length} bytes, above the "
                    f"{max_buffer_bytes}-byte output ceiling"
                )

            aligned_offset = (current_size + 3) & ~3
            if current_handle is None or aligned_offset + byte_length > max_buffer_bytes:
                if current_handle is not None:
                    current_handle.close()
                    current_handle = None
                output_index = len(output_buffers)
                relative_path = f"buffers/{buffer_base}-{output_index + 1:02d}.bin"
                output_path = pending.joinpath(*PurePosixPath(relative_path).parts)
                current_handle = output_path.open("wb")
                output_handles.append(current_handle)
                output_buffers.append(OutputBuffer(relative_path, 0))
                current_size = 0
                aligned_offset = 0
            else:
                output_index = len(output_buffers) - 1

            padding = aligned_offset - current_size
            if padding:
                current_handle.write(b"\0" * padding)
                padding_bytes += padding
            source_handle = source_handles.get(source_buffer.path)
            if source_handle is None:
                source_handle = source_buffer.path.open("rb")
                source_handles[source_buffer.path] = source_handle
            absolute_source_offset = source_buffer.offset + source_offset
            copy_range(
                source_handle,
                current_handle,
                absolute_source_offset,
                byte_length,
            )
            current_size = aligned_offset + byte_length
            output_buffers[output_index] = OutputBuffer(
                output_buffers[output_index].relative_path,
                current_size,
            )
            view["buffer"] = output_index
            if aligned_offset:
                view["byteOffset"] = aligned_offset
            else:
                view.pop("byteOffset", None)
            mappings.append(
                (
                    source_buffer_index,
                    source_offset,
                    output_index,
                    aligned_offset,
                    byte_length,
                )
            )
            copied_view_bytes += byte_length

        if current_handle is not None:
            current_handle.close()
            current_handle = None
        for handle in source_handles.values():
            handle.close()
        source_handles.clear()

        document["buffers"] = [
            {"uri": buffer.relative_path, "byteLength": buffer.byte_length}
            for buffer in output_buffers
        ]
        reserved_paths = {
            gltf_name.casefold(),
            report_name.casefold(),
            *(buffer.relative_path.casefold() for buffer in output_buffers),
        }
        copied_images = copy_external_images(
            document,
            input_path.parent,
            pending,
            reserved_paths,
        )

        output_gltf = pending / gltf_name
        output_gltf.write_text(
            json.dumps(document, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
            newline="\n",
        )

        verification_source_handles: dict[Path, BinaryIO] = {}
        verification_output_handles: dict[int, BinaryIO] = {}
        try:
            for mapping_index, (
                source_buffer_index,
                source_offset,
                output_index,
                output_offset,
                byte_length,
            ) in enumerate(mappings):
                source_buffer = container.buffers[source_buffer_index]
                source_handle = verification_source_handles.get(source_buffer.path)
                if source_handle is None:
                    source_handle = source_buffer.path.open("rb")
                    verification_source_handles[source_buffer.path] = source_handle
                output_handle = verification_output_handles.get(output_index)
                if output_handle is None:
                    output_handle = pending.joinpath(
                        *PurePosixPath(output_buffers[output_index].relative_path).parts
                    ).open("rb")
                    verification_output_handles[output_index] = output_handle
                if not compare_ranges(
                    source_handle,
                    source_buffer.offset + source_offset,
                    output_handle,
                    output_offset,
                    byte_length,
                ):
                    raise ValueError(
                        f"repacked bufferView {mapping_index} differs from its source"
                    )
        finally:
            for handle in verification_source_handles.values():
                handle.close()
            for handle in verification_output_handles.values():
                handle.close()

        output_files: list[dict] = []
        output_paths: dict[str, str] = {}
        for path in sorted(
            (item for item in pending.rglob("*") if item.is_file()),
            key=lambda value: value.relative_to(pending).as_posix().casefold(),
        ):
            if path.name == report_name:
                continue
            relative_path = path.relative_to(pending).as_posix()
            key = relative_path.casefold()
            previous = output_paths.get(key)
            if previous is not None:
                raise ValueError(
                    f"output paths collide case-insensitively: "
                    f"{previous} and {relative_path}"
                )
            output_paths[key] = relative_path
            file_bytes = path.stat().st_size
            if file_bytes >= TRACKED_FILE_LIMIT_BYTES:
                raise ValueError(
                    f"output file reaches the strict GitHub ceiling: {path} "
                    f"({file_bytes} bytes)"
                )
            output_files.append(
                {
                    "path": relative_path,
                    "bytes": file_bytes,
                    "sha256": sha256_file(path),
                }
            )

        source_files: list[dict] = []
        for source_file in container.source_files:
            if container.kind == "glb":
                relative_path = source_file.name
            else:
                relative_path = source_file.relative_to(input_path.parent).as_posix()
            source_files.append(
                {
                    "path": relative_path,
                    "bytes": source_file.stat().st_size,
                    "sha256": sha256_file(source_file),
                }
            )
        source_files.sort(key=lambda item: item["path"].casefold())

        report = {
            "schemaVersion": 2,
            "sourceId": source_id,
            "sourceContainer": container.kind,
            "sourceContainerBytes": container.container_bytes,
            "sourceContainerSha256": container.source_sha256,
            "sourceBytes": container.source_bytes,
            "sourceSha256": container.source_sha256,
            "sourceFiles": source_files,
            "sourceBufferCount": len(container.buffers),
            "bufferViewCount": len(buffer_views),
            "copiedBufferViewBytes": copied_view_bytes,
            "alignmentPaddingBytes": padding_bytes,
            "maxBufferBytes": max_buffer_bytes,
            "trackedFileLimitBytes": TRACKED_FILE_LIMIT_BYTES,
            "outputBufferCount": len(output_buffers),
            "externalImagesCopied": copied_images,
            "extensionsUsed": document.get("extensionsUsed", []),
            "materialCompatibilityOverrides": material_overrides,
            "files": output_files,
        }
        (pending / report_name).write_text(
            json.dumps(report, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        os.replace(pending, output_directory)
        return report
    except Exception:
        if current_handle is not None and not current_handle.closed:
            current_handle.close()
        for handle in output_handles:
            if not handle.closed:
                handle.close()
        for handle in source_handles.values():
            if not handle.closed:
                handle.close()
        shutil.rmtree(pending, ignore_errors=True)
        raise


def make_test_glb(path: Path) -> tuple[dict, bytes]:
    binary = b"abcdefgh" + b"IJKLMNOPQRST" + b"uvwxyz"
    document = {
        "asset": {"version": "2.0", "generator": "UVSR self-test"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 8},
            {"buffer": 0, "byteOffset": 8, "byteLength": 12},
            {"buffer": 0, "byteOffset": 20, "byteLength": 6},
        ],
        "extensionsUsed": [TRANSMISSION_EXTENSION],
        "materials": [
            {
                "name": "Synthetic Glass",
                "alphaMode": "BLEND",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.25, 0.5, 0.75, 0.25]
                },
                "extensions": {
                    TRANSMISSION_EXTENSION: {"transmissionFactor": 0.5}
                },
            }
        ],
    }
    json_chunk = json.dumps(document, separators=(",", ":")).encode("utf-8")
    json_chunk += b" " * ((4 - len(json_chunk) % 4) % 4)
    binary_chunk = binary + b"\0" * ((4 - len(binary) % 4) % 4)
    total_length = 12 + 8 + len(json_chunk) + 8 + len(binary_chunk)
    with path.open("wb") as stream:
        stream.write(struct.pack("<III", GLB_MAGIC, GLB_VERSION, total_length))
        stream.write(struct.pack("<II", len(json_chunk), GLB_JSON_CHUNK))
        stream.write(json_chunk)
        stream.write(struct.pack("<II", len(binary_chunk), GLB_BINARY_CHUNK))
        stream.write(binary_chunk)
    return document, binary


def run_self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="uvsr_repack_gltf_") as temporary:
        root = Path(temporary)
        input_path = root / "source.glb"
        _, binary = make_test_glb(input_path)
        output = root / "output"
        report = repack(
            input_path=input_path,
            output_directory=output,
            gltf_name="scene.gltf",
            buffer_base="scene",
            max_buffer_bytes=16,
            source_id="synthetic-self-test",
            report_name="report.json",
            force_opaque_material_names=("Synthetic Glass",),
        )
        if report["outputBufferCount"] != 3:
            raise AssertionError("self-test did not create the expected three buffers")
        output_document = json.loads((output / "scene.gltf").read_text(encoding="utf-8"))
        if report["schemaVersion"] != 2 or len(
            report["materialCompatibilityOverrides"]
        ) != 1:
            raise AssertionError("self-test did not audit its material override")
        output_material = output_document["materials"][0]
        if (
            output_material.get("alphaMode") != "OPAQUE"
            or output_material["pbrMetallicRoughness"]["baseColorFactor"][3]
            != 0.25
            or TRANSMISSION_EXTENSION in output_material.get("extensions", {})
            or TRANSMISSION_EXTENSION in output_document.get("extensionsUsed", [])
        ):
            raise AssertionError("self-test material override is incomplete")
        reconstructed = bytearray(len(binary))
        for source_view, output_view in zip(
            load_glb(input_path).document["bufferViews"],
            output_document["bufferViews"],
        ):
            source_offset = source_view.get("byteOffset", 0)
            length = source_view["byteLength"]
            output_buffer = output_document["buffers"][output_view["buffer"]]
            output_offset = output_view.get("byteOffset", 0)
            with (output / output_buffer["uri"]).open("rb") as stream:
                stream.seek(output_offset)
                reconstructed[source_offset : source_offset + length] = stream.read(length)
        if bytes(reconstructed) != binary:
            raise AssertionError("self-test reconstructed bytes differ")
        print("glTF buffer repack self-test passed")


def parse_arguments(arguments: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output-directory", type=Path)
    parser.add_argument("--gltf-name")
    parser.add_argument("--buffer-base")
    parser.add_argument(
        "--max-buffer-bytes",
        type=int,
        default=DEFAULT_MAX_BUFFER_BYTES,
    )
    parser.add_argument("--source-id")
    parser.add_argument("--report-name", default="repack-report.json")
    parser.add_argument(
        "--force-opaque-material",
        action="append",
        default=[],
        help=(
            "exact material name whose BLEND/transmission domain must be "
            "flattened to opaque PBR; may be repeated"
        ),
    )
    parsed = parser.parse_args(list(arguments))
    if not parsed.self_test:
        missing = [
            name
            for name in (
                "input",
                "output_directory",
                "gltf_name",
                "buffer_base",
                "source_id",
            )
            if getattr(parsed, name) in {None, ""}
        ]
        if missing:
            parser.error("missing required arguments: " + ", ".join(missing))
    return parsed


def main() -> int:
    try:
        arguments = parse_arguments(sys.argv[1:])
        if arguments.self_test:
            run_self_test()
            return 0
        report = repack(
            input_path=arguments.input,
            output_directory=arguments.output_directory,
            gltf_name=arguments.gltf_name,
            buffer_base=arguments.buffer_base,
            max_buffer_bytes=arguments.max_buffer_bytes,
            source_id=arguments.source_id,
            report_name=arguments.report_name,
            force_opaque_material_names=arguments.force_opaque_material,
        )
        print(
            "UVSR_GLTF_REPACK="
            + json.dumps(
                {
                    "outputDirectory": str(arguments.output_directory.resolve()),
                    "sourceContainerSha256": report["sourceContainerSha256"],
                    "bufferViewCount": report["bufferViewCount"],
                    "outputBufferCount": report["outputBufferCount"],
                    "copiedBufferViewBytes": report["copiedBufferViewBytes"],
                    "alignmentPaddingBytes": report["alignmentPaddingBytes"],
                    "materialCompatibilityOverrides": len(
                        report["materialCompatibilityOverrides"]
                    ),
                },
                sort_keys=True,
            )
        )
        return 0
    except Exception as error:
        print(f"glTF buffer repack failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
