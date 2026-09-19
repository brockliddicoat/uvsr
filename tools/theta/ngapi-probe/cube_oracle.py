"""Independent ray/box oracle for the fixed offscreen cube. No GPU output is a golden."""
from functools import lru_cache
import hashlib
import math
import re
import struct
import zlib

WIDTH = HEIGHT = 128
CLEAR = (17, 34, 51, 255)
TEXTURES = (((255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 255)),
            ((0, 255, 255, 255), (255, 0, 255, 255), (255, 255, 0, 255), (0, 0, 0, 255)))
TRANSFORMS = (
    ((.3125, 0, .1875, 0), (-.125, -.375, .1875, 0), (-.125, .125, .1875, .5)),
    ((.1875, 0, -.3125, .0625), (-.1875, -.375, -.125, 0), (.1875, .125, -.125, .5)),
)
EDGE_EPSILON = 1e-6
TEXEL_EPSILON = 2e-5
DEPTH_TOLERANCE = 3e-6
MAX_MASKED_PIXELS = 256


def inverse3(rows):
    a, b, c = rows[0][:3]
    d, e, f = rows[1][:3]
    g, h, i = rows[2][:3]
    adj = ((e*i-f*h, c*h-b*i, b*f-c*e), (f*g-d*i, a*i-c*g, c*d-a*f), (d*h-e*g, b*g-a*h, a*e-b*d))
    determinant = a*adj[0][0] + b*adj[1][0] + c*adj[2][0]
    if abs(determinant) < 1e-12:
        raise ValueError("singular cube transform")
    return tuple(tuple(value / determinant for value in row) for row in adj)


def trace_pixel(transform, inverse, x, y):
    # A pixel-center ray along increasing Vulkan depth, transformed to the unit
    # box. This uses no triangle list, rasterizer implementation or shader code.
    clip = (2*(x+.5)/WIDTH - 1 - transform[0][3], 2*(y+.5)/HEIGHT - 1 - transform[1][3], -transform[2][3])
    origin = tuple(sum(row[k]*clip[k] for k in range(3)) for row in inverse)
    direction = tuple(row[2] for row in inverse)
    near, far, axis, sign = -math.inf, math.inf, -1, 0
    entries = []
    for k in range(3):
        if abs(direction[k]) < 1e-12:
            if abs(origin[k]) > 1:
                return None, 1.0, None, abs(abs(origin[k])-1) < EDGE_EPSILON
            entries.append(-math.inf)
            continue
        first, last = (-1-origin[k])/direction[k], (1-origin[k])/direction[k]
        entry, leave = min(first, last), max(first, last)
        entries.append(entry)
        if entry > near:
            near, axis, sign = entry, k, -1 if first < last else 1
        far = min(far, leave)
    ambiguous = abs(near-far) < EDGE_EPSILON
    if near > far or near < 0 or near > 1:
        return None, 1.0, None, ambiguous
    ambiguous |= any(k != axis and abs(entry-near) < EDGE_EPSILON for k, entry in enumerate(entries))
    point = tuple(origin[k] + near*direction[k] for k in range(3))
    return point, near, (axis, sign), ambiguous


@lru_cache(maxsize=4)
def reference(case):
    if case not in range(4):
        raise ValueError("invalid cube case")
    transform = TRANSFORMS[case//2]
    inverse = inverse3(transform)
    colors, depths, mask, foreground = bytearray(), [], [], []
    faces = set()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            point, depth, face, ambiguous = trace_pixel(transform, inverse, x, y)
            color = CLEAR
            if point is not None:
                px, py, pz = point
                axis, sign = face
                # Source cube UV convention expressed by face coordinates,
                # independently of its indexed vertex representation.
                if axis == 0:
                    uv = ((pz+1)/2 if sign < 0 else (1-pz)/2, (1-py)/2)
                elif axis == 1:
                    uv = ((1-pz)/2, (px+1)/2)
                else:
                    uv = ((1-px)/2 if sign < 0 else (px+1)/2, (1-py)/2)
                texel = []
                for value in uv:
                    value = value*1.5 + .125
                    if case % 2:
                        value = min(max(value, 0.0), 1.0)
                        ambiguous |= 0 < value < 1 and abs(2*value-round(2*value)) < TEXEL_EPSILON
                    else:
                        value %= 1.0
                        ambiguous |= abs(2*value-round(2*value)) < TEXEL_EPSILON
                    texel.append(min(int(value*2), 1))
                color = TEXTURES[case//2][texel[1]*2+texel[0]]
                faces.add(face)
            colors.extend(color)
            depths.append(depth)
            mask.append(ambiguous)
            foreground.append(point is not None)
    if sum(mask) > MAX_MASKED_PIXELS or len(faces) != 3 or not 2000 < sum(foreground) < 8000:
        raise ValueError("reference has an invalid mask, face count or coverage")
    return bytes(colors), tuple(depths), tuple(mask), tuple(foreground)


def compare_images(case, color, depth_bytes):
    if len(color) != WIDTH*HEIGHT*4 or len(depth_bytes) != WIDTH*HEIGHT*4:
        raise ValueError("missing or truncated cube image")
    expected, depths, mask, foreground = reference(case)
    actual_depth = struct.unpack(f"<{WIDTH*HEIGHT}f", depth_bytes)
    first = None
    max_color, max_depth = 0, 0.0
    for index, (actual_z, expected_z, masked) in enumerate(zip(actual_depth, depths, mask)):
        pixel = tuple(color[index*4:index*4+4])
        expected_pixel = tuple(expected[index*4:index*4+4])
        valid = math.isfinite(actual_z) and 0 <= actual_z <= 1 and pixel in (CLEAR, *TEXTURES[case//2])
        color_error = max(abs(a-b) for a, b in zip(pixel, expected_pixel))
        depth_error = abs(actual_z-expected_z)
        if not masked:
            max_color = max(max_color, color_error)
            max_depth = max(max_depth, depth_error)
            valid &= color_error == 0 and depth_error <= DEPTH_TOLERANCE
        if not valid and first is None:
            first = {"x": index % WIDTH, "y": index // WIDTH, "masked": masked,
                     "expected_rgba": expected_pixel, "actual_rgba": pixel,
                     "expected_depth": expected_z, "actual_depth": actual_z if math.isfinite(actual_z) else str(actual_z)}
    return {"status": "pass" if first is None else "fail", "first_failure": first,
            "checked_pixels": len(mask)-sum(mask), "masked_pixels": sum(mask), "max_masked_pixels": MAX_MASKED_PIXELS,
            "foreground_pixels": sum(foreground), "background_pixels": len(mask)-sum(foreground),
            "max_color_error": max_color, "max_depth_error": max_depth,
            "color_tolerance": 0, "depth_tolerance": DEPTH_TOLERANCE}


def write_png(path, rgba):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind+data))
    scanlines = b"".join(b"\0" + rgba[y*WIDTH*4:(y+1)*WIDTH*4] for y in range(HEIGHT))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


def check_cube_records(records, metadata, output):
    prefix = "theta.m2.ngapi.native_heap_cube."
    ids = {prefix + "controls"} | {f"{prefix}opt{opt}.case{case}" for opt in (0, 3) for case in range(4)}
    if len(records) != 9 or {row["case_id"] for row in records} != ids:
        raise ValueError("missing, duplicate or unexpected cube case IDs")
    strides = set()
    images = []
    for row in records:
        if row["status"] != "pass":
            raise ValueError("a required native cube case did not pass")
        if row["case_id"] == prefix + "controls":
            if row["checks"] != 10 or row["shader_cases_executed"] != 0:
                raise ValueError("cube CPU controls have an incorrect denominator")
            continue
        opt, case = map(int, re.search(r"\.opt(0|3)\.case([0-3])$", row["case_id"]).groups())
        address = int(row["vertex_address"], 16)
        expected = metadata[opt]
        fields = {"view": case//2, "resource_index": 1 if case < 2 else 3, "sampler_index": 2+case%2,
                  "vertex_count": 24, "vertex_stride": 24, "index_count": 36, "root_bytes": 80,
                  "width": WIDTH, "height": HEIGHT, "heap_slots": 4, "first_input_mismatch_byte": -1,
                  "guard_intact": True, "shader_cases_executed": 1,
                  "color_file": f"cube.opt{opt}.case{case}.rgba", "depth_file": f"cube.opt{opt}.case{case}.depth"}
        if (any(row[key] != value for key, value in fields.items())
                or row["source_sha256"] != expected["identity"]["source_sha256"]
                or row["payload_sha256"] != expected["payload_sha256"]
                or not 0 < address <= (1 << 64)-1-696 or address % 4
                or row["nonzero_high_address_bits"] != bool(address >> 32)
                or row["image_descriptor_bytes"] <= 0 or row["sampler_descriptor_bytes"] <= 0):
            raise ValueError(f"cube record mismatch: {row['case_id']}")
        strides.add((row["image_descriptor_bytes"], row["sampler_descriptor_bytes"]))
        color = (output / fields["color_file"]).read_bytes()
        depth = (output / fields["depth_file"]).read_bytes()
        result = compare_images(case, color, depth)
        actual_png = output / f"cube.opt{opt}.case{case}.actual.png"
        expected_png = output / f"cube.case{case}.expected.png"
        write_png(actual_png, color)
        write_png(expected_png, reference(case)[0])
        row["image_oracle"] = dict(result, color_sha256=hashlib.sha256(color).hexdigest(), depth_sha256=hashlib.sha256(depth).hexdigest(),
                                   actual_png=str(actual_png), expected_png=str(expected_png))
        images.append(result["status"])
    if len(strides) != 1 or images != ["pass"]*8:
        raise ValueError("cube image oracle or consistent descriptor stride check failed")
