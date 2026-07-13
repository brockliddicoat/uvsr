#!/usr/bin/env python3
"""Generate the lightweight Horizon GI room benchmark as a self-contained GLB."""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/scenes/horizon_gi_benchmark/HorizonGIBenchmark.glb"


def cube_geometry():
    positions = []
    normals = []
    indices = []
    faces = (
        ((1, 0, 0), ((.5, -.5, -.5), (.5, -.5, .5), (.5, .5, .5), (.5, .5, -.5))),
        ((-1, 0, 0), ((-.5, -.5, .5), (-.5, -.5, -.5), (-.5, .5, -.5), (-.5, .5, .5))),
        ((0, 1, 0), ((-.5, .5, -.5), (.5, .5, -.5), (.5, .5, .5), (-.5, .5, .5))),
        ((0, -1, 0), ((-.5, -.5, .5), (.5, -.5, .5), (.5, -.5, -.5), (-.5, -.5, -.5))),
        ((0, 0, 1), ((.5, -.5, .5), (-.5, -.5, .5), (-.5, .5, .5), (.5, .5, .5))),
        ((0, 0, -1), ((-.5, -.5, -.5), (.5, -.5, -.5), (.5, .5, -.5), (-.5, .5, -.5))),
    )
    for normal, corners in faces:
        base = len(positions)
        positions.extend(corners)
        normals.extend((normal,) * 4)
        indices.extend((base, base + 1, base + 2, base, base + 2, base + 3))
    return positions, normals, indices


def cylinder_geometry(segments=12):
    positions = []
    normals = []
    indices = []
    for i in range(segments + 1):
        angle = 2.0 * math.pi * i / segments
        x, z = math.cos(angle) * .5, math.sin(angle) * .5
        positions.extend(((x, -.5, z), (x, .5, z)))
        normals.extend(((math.cos(angle), 0, math.sin(angle)),) * 2)
    for i in range(segments):
        base = i * 2
        indices.extend((base, base + 1, base + 3, base, base + 3, base + 2))
    for y, ny, reverse in ((-.5, -1, True), (.5, 1, False)):
        center = len(positions)
        positions.append((0, y, 0))
        normals.append((0, ny, 0))
        ring = len(positions)
        for i in range(segments):
            angle = 2.0 * math.pi * i / segments
            positions.append((math.cos(angle) * .5, y, math.sin(angle) * .5))
            normals.append((0, ny, 0))
        for i in range(segments):
            a, b = ring + i, ring + (i + 1) % segments
            indices.extend((center, b, a) if reverse else (center, a, b))
    return positions, normals, indices


def uv_sphere_geometry(rings=6, segments=12):
    positions = []
    normals = []
    indices = []
    for r in range(rings + 1):
        v = r / rings
        phi = v * math.pi
        y = math.cos(phi) * .5
        radius = math.sin(phi) * .5
        for s in range(segments + 1):
            theta = s / segments * 2.0 * math.pi
            p = (math.cos(theta) * radius, y, math.sin(theta) * radius)
            positions.append(p)
            normals.append(tuple(component * 2.0 for component in p))
    stride = segments + 1
    for r in range(rings):
        for s in range(segments):
            a = r * stride + s
            b = a + stride
            indices.extend((a, b, a + 1, a + 1, b, b + 1))
    return positions, normals, indices


class GlbBuilder:
    def __init__(self):
        self.binary = bytearray()
        self.buffer_views = []
        self.accessors = []
        self.materials = []
        self.meshes = []
        self.nodes = []
        self.mesh_cache = {}
        self.geometries = {
            "cube": cube_geometry(),
            "cylinder": cylinder_geometry(),
            "sphere": uv_sphere_geometry(),
        }

    def align(self, alignment=4):
        while len(self.binary) % alignment:
            self.binary.append(0)

    def add_accessor(self, values, component_type, accessor_type, target):
        self.align()
        offset = len(self.binary)
        if component_type == 5126:
            flat = [component for value in values for component in value]
            self.binary.extend(struct.pack(f"<{len(flat)}f", *flat))
            minimum = [min(v[i] for v in values) for i in range(len(values[0]))]
            maximum = [max(v[i] for v in values) for i in range(len(values[0]))]
        else:
            self.binary.extend(struct.pack(f"<{len(values)}H", *values))
            minimum, maximum = [min(values)], [max(values)]
        view = len(self.buffer_views)
        self.buffer_views.append({"buffer": 0, "byteOffset": offset,
                                  "byteLength": len(self.binary) - offset, "target": target})
        accessor = len(self.accessors)
        self.accessors.append({"bufferView": view, "componentType": component_type,
                               "count": len(values), "type": accessor_type,
                               "min": minimum, "max": maximum})
        return accessor

    def material(self, name, color, roughness=.75, metallic=0.0, emissive=None, strength=1.0):
        result = {
            "name": name,
            "pbrMetallicRoughness": {
                "baseColorFactor": [*color, 1.0],
                "metallicFactor": metallic,
                "roughnessFactor": roughness,
            },
        }
        if emissive is not None:
            result["emissiveFactor"] = list(emissive)
            result["extensions"] = {"KHR_materials_emissive_strength": {"emissiveStrength": strength}}
        self.materials.append(result)
        return len(self.materials) - 1

    def mesh(self, shape, material):
        key = (shape, material)
        if key in self.mesh_cache:
            return self.mesh_cache[key]
        positions, normals, indices = self.geometries[shape]
        position_accessor = self.add_accessor(positions, 5126, "VEC3", 34962)
        normal_accessor = self.add_accessor(normals, 5126, "VEC3", 34962)
        index_accessor = self.add_accessor(indices, 5123, "SCALAR", 34963)
        index = len(self.meshes)
        self.meshes.append({
            "name": f"{shape}_{material}",
            "primitives": [{
                "attributes": {"POSITION": position_accessor, "NORMAL": normal_accessor},
                "indices": index_accessor,
                "material": material,
            }],
        })
        self.mesh_cache[key] = index
        return index

    def node(self, name, shape, material, translation, scale, rotation=None):
        node = {"name": name, "mesh": self.mesh(shape, material),
                "translation": list(translation), "scale": list(scale)}
        if rotation is not None:
            node["rotation"] = list(rotation)
        self.nodes.append(node)
        return len(self.nodes) - 1

    def write(self, path):
        document = {
            "asset": {"version": "2.0", "generator": "UVSR Horizon GI benchmark generator"},
            "extensionsUsed": ["KHR_materials_emissive_strength", "KHR_lights_punctual"],
            "extensions": {"KHR_lights_punctual": {"lights": [{
                "name": "Neutral benchmark sun", "type": "directional",
                "color": [1, 1, 1], "intensity": 1.0,
            }]}},
            "buffers": [{"byteLength": len(self.binary)}],
            "bufferViews": self.buffer_views,
            "accessors": self.accessors,
            "materials": self.materials,
            "meshes": self.meshes,
            "nodes": self.nodes + [{
                "name": "Neutral benchmark sun",
                "rotation": [-0.2706, 0.6533, 0.2706, 0.6533],
                "extensions": {"KHR_lights_punctual": {"light": 0}},
            }],
            "scenes": [{"name": "Horizon GI Benchmark", "nodes": list(range(len(self.nodes) + 1))}],
            "scene": 0,
        }
        json_bytes = json.dumps(document, separators=(",", ":")).encode("utf-8")
        json_bytes += b" " * ((4 - len(json_bytes) % 4) % 4)
        self.align()
        bin_bytes = bytes(self.binary)
        total = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
        payload = bytearray(struct.pack("<III", 0x46546C67, 2, total))
        payload.extend(struct.pack("<II", len(json_bytes), 0x4E4F534A))
        payload.extend(json_bytes)
        payload.extend(struct.pack("<II", len(bin_bytes), 0x004E4942))
        payload.extend(bin_bytes)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)


def build_scene():
    b = GlbBuilder()
    plaster = b.material("Warm-neutral plaster", (.58, .56, .53))
    terracotta = b.material("Terracotta peg wall", (.55, .19, .10))
    green = b.material("Green floor panel", (.12, .42, .18))
    dark = b.material("Dark metal", (.018, .022, .030), roughness=.42, metallic=.72)
    pale = b.material("Pale furniture", (.68, .65, .61), roughness=.68)
    orange = b.material("Orange emitter", (.72, .32, .08), emissive=(1.0, .32, .04), strength=18.0)
    white = b.material("White emitter", (.82, .82, .82), emissive=(1.0, 1.0, 1.0), strength=22.0)
    blue = b.material("Blue emitter", (.35, .55, .78), emissive=(.12, .55, 1.0), strength=20.0)

    b.node("Floor", "cube", plaster, (0, -.08, 0), (6.4, .16, 6.4))
    # UVSR's scene-fit camera starts on +Z, so leave that side open and place
    # the feature wall at -Z. The first frame is immediately benchmark-ready.
    b.node("Back wall", "cube", terracotta, (0, 2.0, -3.05), (6.4, 4.15, .18))
    b.node("Left wall", "cube", plaster, (-3.1, 2.0, 0), (.18, 4.15, 6.2))
    b.node("Ceiling lip", "cube", plaster, (0, 4.05, -1.1), (6.4, .14, 3.9))

    for row in range(7):
        for column in range(11):
            x = -2.75 + column * .31
            y = 1.55 + row * .35
            b.node(f"Wall peg {row:02d}-{column:02d}", "sphere", dark,
                   (x, y, -2.91), (.13, .13, .10))

    b.node("Perforated floor slab", "cube", green, (.35, .055, .25), (1.9, .11, 2.65))
    for row in range(8):
        for column in range(6):
            x = -.39 + column * .30
            z = -.82 + row * .30
            b.node(f"Floor inset {row:02d}-{column:02d}", "cylinder", dark,
                   (x, .13, z), (.13, .035, .13))

    b.node("Orange emissive panel", "cube", orange, (.78, 2.15, -2.87), (.56, 2.35, .08))
    b.node("White emissive panel", "cube", white, (1.58, 1.64, -2.86), (.52, 1.55, .08))
    b.node("Blue emissive panel", "cube", blue, (2.42, 2.02, -2.86), (.62, 2.38, .08))

    # Bench, shelves, ladder and a thin-legged table deliberately exercise the
    # finite-thickness and silhouette behavior of the visibility bitmask.
    b.node("Bench top", "cube", pale, (-1.05, .95, -1.55), (2.25, .18, .66))
    for x in (-1.95, -.15):
        b.node(f"Bench leg {x}", "cube", pale, (x, .46, -1.55), (.15, .9, .52))
    for y in (1.32, 1.62, 1.92):
        b.node(f"Shelf {y}", "cube", pale, (-.9, y, -2.55), (1.85, .09, .38))
    for x in (-1.78, -.05):
        b.node(f"Shelf upright {x}", "cube", dark, (x, 1.63, -2.55), (.06, .95, .06))
    for x in (-2.55, -2.05):
        b.node(f"Ladder rail {x}", "cube", dark, (x, 1.05, -1.55), (.08, 2.05, .08))
    for y in (.25, .60, .95, 1.30, 1.65):
        b.node(f"Ladder rung {y}", "cube", dark, (-2.30, y, -1.55), (.58, .06, .07))

    b.node("Table top", "cube", pale, (1.85, .72, .70), (1.48, .10, .82))
    for x in (1.22, 2.48):
        for z in (1.02, .38):
            b.node(f"Table leg {x}-{z}", "cube", dark, (x, .34, z), (.055, .72, .055))
    b.node("Right pedestal", "cube", pale, (2.37, .46, -.45), (.55, .92, .55))
    b.node("Right small cube", "cube", pale, (2.48, 1.02, -.42), (.43, .43, .43))

    b.write(OUTPUT)
    print(f"Wrote {OUTPUT} ({OUTPUT.stat().st_size:,} bytes)")


if __name__ == "__main__":
    build_scene()
