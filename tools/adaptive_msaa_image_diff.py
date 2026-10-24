#!/usr/bin/env python3
"""Validate authenticated adaptive-MSAA captures against Reference BMPs."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import re
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Sequence


MANIFEST_SCHEMA = 2
RECEIPT_SCHEMA = 2
VALIDATION_SUITE_ID = "uvsr-adaptive-msaa-image-diff-v1"
VALIDATION_PHASES = (0.25, 0.5)
VALIDATION_DIMENSIONS = (1920, 1080)
VALIDATION_RECIPE_SHA256 = (
    "0ff1ec5e89f0eaefbb5f2422e2561af316caf6c9d31f53c8020c306fd86e5883"
)
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
DISPLAY_PERCENTILE_99_LIMIT = 0.5
DISPLAY_MAXIMUM_LIMIT = 2.0
SUPPORTED_IMPLEMENTATIONS = ("adaptive-g-buffer", "compact-visibility")
REFERENCE_IMPLEMENTATION = "reference"
RECEIPT_KIND = "uvsr-adaptive-msaa-validation-frame"
SUPPORTED_SAMPLE_COUNTS = (2, 4, 8, 16)
REQUIRED_CASES = (
    "crossing-triangles",
    "shared-material-submeshes",
    "instancing",
    "alpha-cutouts",
    "double-sided",
    "tiny-steep-geometry",
    "sharp-specular",
    "normal-mapped",
    "emissive-detail",
    "sky-edges",
    "motion-taa",
    "moving-shadow-edges",
    "nearby-lights",
    "grazing-geometry",
    "screen-space-visibility",
)
REQUIRED_RUN_COUNT = (
    len(SUPPORTED_IMPLEMENTATIONS)
    * len(SUPPORTED_SAMPLE_COUNTS)
    * len(REQUIRED_CASES)
)
REQUIRED_FRAME_COUNT = 2
GROUP_HISTOGRAM_BIN_COUNT = 17
FALLBACK_REASON_NAMES = (
    "none",
    "invalidSentinel",
    "invalidHandleEncoding",
    "slotOutOfRange",
    "generationMismatch",
    "topologyEpochMismatch",
    "lodRevisionMismatch",
    "deformationRevisionMismatch",
    "materialRevisionMismatch",
    "capacityOverflow",
    "primitiveOverflow",
    "generationExhausted",
    "registryResetRequired",
    "transientMappingCollision",
    "transientMappingFailure",
    "unsupportedDeformation",
    "unsupportedMaterial",
    "unsafeSharing",
    "reconstructionFailure",
    "formatUnsupported",
    "sampleCountUnsupported",
    "workListOverflow",
    "textureContentRevisionMismatch",
    "textureQualificationMismatch",
)
FALLBACK_REASON_COUNT = len(FALLBACK_REASON_NAMES)
FALLBACK_INVALID_KEY_REASON_INDICES = (1, 2, 3, 10)
FALLBACK_STALE_KEY_REASON_INDICES = (4, 5, 6, 7, 8, 11, 12, 22, 23)
FALLBACK_MAPPING_REASON_INDICES = (13, 14)
FALLBACK_RATE_DENOMINATOR_FIELD = "referenceLightingEvaluations"
FALLBACK_RATE_DENOMINATOR_MEANING = (
    "covered samples that the Reference path would shade"
)
ERROR_BUDGET_REASON_NAMES = (
    "none",
    "nonFiniteValue",
    "hdrLimitExceeded",
    "displayLimitExceeded",
)
ERROR_BUDGET_REASON_COUNT = len(ERROR_BUDGET_REASON_NAMES)

DIAGNOSTICS_COUNTER_GROUP_HISTOGRAM_VALID = 1 << 0
DIAGNOSTICS_COUNTER_LIGHTING_VALID = 1 << 1
DIAGNOSTICS_COUNTER_FALLBACK_REASONS_VALID = 1 << 2
DIAGNOSTICS_COUNTER_INVALID_KEYS_VALID = 1 << 3
DIAGNOSTICS_COUNTER_STALE_AND_MAPPING_VALID = 1 << 4
DIAGNOSTICS_COUNTER_WORK_LIST_VALID = 1 << 5
DIAGNOSTICS_COUNTER_ERROR_BUDGET_VALID = 1 << 6
DIAGNOSTICS_COUNTER_ALL_VALIDITY_BITS = (
    DIAGNOSTICS_COUNTER_GROUP_HISTOGRAM_VALID
    | DIAGNOSTICS_COUNTER_LIGHTING_VALID
    | DIAGNOSTICS_COUNTER_FALLBACK_REASONS_VALID
    | DIAGNOSTICS_COUNTER_INVALID_KEYS_VALID
    | DIAGNOSTICS_COUNTER_STALE_AND_MAPPING_VALID
    | DIAGNOSTICS_COUNTER_WORK_LIST_VALID
    | DIAGNOSTICS_COUNTER_ERROR_BUDGET_VALID
)
REQUIRED_DIAGNOSTICS_VALIDITY_BITS = (
    DIAGNOSTICS_COUNTER_GROUP_HISTOGRAM_VALID
    | DIAGNOSTICS_COUNTER_LIGHTING_VALID
    | DIAGNOSTICS_COUNTER_FALLBACK_REASONS_VALID
    | DIAGNOSTICS_COUNTER_INVALID_KEYS_VALID
    | DIAGNOSTICS_COUNTER_STALE_AND_MAPPING_VALID
)

MANIFEST_FIELDS = frozenset(("schema", "suiteId", "phases", "runs"))
RUN_FIELDS = frozenset(
    ("implementation", "sampleCount", "case", "recipeSha256", "roi", "frames")
)
FRAME_FIELDS = frozenset(
    ("phaseIndex", "referenceReceipt", "candidateReceipt")
)
VIEWPORT_FIELDS = frozenset(("minX", "minY", "maxX", "maxY"))
ROI_FIELDS = frozenset(("x", "y", "width", "height"))
RECEIPT_FIELDS = frozenset(
    (
        "schema",
        "kind",
        "captureId",
        "suiteId",
        "case",
        "recipeSha256",
        "phaseIndex",
        "phase",
        "imagePath",
        "imageSha256",
        "sourceCommit",
        "sourceDiffSha256",
        "rendererBuildManifestSha256",
        "rendererExecutableSha256",
        "sceneName",
        "sceneDescriptorSha256",
        "sceneGltfSha256",
        "cameraSha256",
        "settingsSha256",
        "width",
        "height",
        "viewport",
        "frozenSeed",
        "topologyEpoch",
        "topologyRevision",
        "lodRevision",
        "deformationRevision",
        "materialRevision",
        "requestedImplementation",
        "resolvedImplementation",
        "appliedImplementation",
        "requestedSampleCount",
        "resolvedSampleCount",
        "appliedSampleCount",
        "selectedPathSucceeded",
        "submittedMainViewTriangles",
        "diagnosticsProducerProducedThisFrame",
        "diagnosticsReadbackToken",
        "diagnosticsCompletedToken",
        "diagnosticsDispatchComplete",
        "diagnosticsValidityMask",
        "groupHistogram",
        "referenceLightingEvaluations",
        "performedLightingEvaluations",
        "lightingEvaluationsSaved",
        "fallbackReasonCounts",
        "invalidKeyCount",
        "staleKeyCount",
        "mappingFailureCount",
        "errorBudgetRejectionReasonCounts",
        "errorBudgetRejectedGroupCount",
        "errorBudgetRejectedSampleCount",
        "compactShadeSucceeded",
        "compactShadeDispatchedViewCount",
        "compactReconstructDrawCount",
        "compactReconstructTriangleCount",
        "compactFallbackDrawCount",
        "compactFallbackTriangleCount",
        "preparationTopology",
        "workListObservationCount",
        "workListCapacityTotal",
        "workListOccupancyTotal",
        "workListPeakOccupancy",
        "workListOverflowCount",
        "roi",
    )
)
SHA256_FIELDS = (
    "recipeSha256",
    "imageSha256",
    "sourceDiffSha256",
    "rendererBuildManifestSha256",
    "rendererExecutableSha256",
    "sceneDescriptorSha256",
    "sceneGltfSha256",
    "cameraSha256",
    "settingsSha256",
)
GLOBAL_IDENTITY_FIELDS = (
    "suiteId",
    "sourceCommit",
    "sourceDiffSha256",
    "rendererBuildManifestSha256",
    "rendererExecutableSha256",
    "sceneName",
    "sceneDescriptorSha256",
    "sceneGltfSha256",
    "cameraSha256",
    "width",
    "height",
    "viewport",
    "topologyEpoch",
    "topologyRevision",
    "lodRevision",
    "deformationRevision",
    "materialRevision",
    "preparationTopology",
)
MATCHED_PAIR_FIELDS = (
    "suiteId",
    "case",
    "recipeSha256",
    "phaseIndex",
    "phase",
    "sourceCommit",
    "sourceDiffSha256",
    "rendererBuildManifestSha256",
    "rendererExecutableSha256",
    "sceneName",
    "sceneDescriptorSha256",
    "sceneGltfSha256",
    "cameraSha256",
    "settingsSha256",
    "width",
    "height",
    "viewport",
    "frozenSeed",
    "topologyEpoch",
    "topologyRevision",
    "lodRevision",
    "deformationRevision",
    "materialRevision",
    "requestedSampleCount",
    "resolvedSampleCount",
    "appliedSampleCount",
    "referenceLightingEvaluations",
    "preparationTopology",
    "roi",
)


class ValidationError(RuntimeError):
    """Manifest, receipt, work proof, or image input is invalid."""


@dataclass(frozen=True)
class BmpImage:
    width: int
    height: int
    rgb: bytes


@dataclass(frozen=True)
class ValidatedReceipt:
    data: dict[str, object]
    receipt_path: Path
    image_path: Path
    image: BmpImage


def read_bmp(path: Path) -> BmpImage:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValidationError(f"{path}: expected a Windows BMP image")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40 or len(data) < 14 + dib_size:
        raise ValidationError(f"{path}: unsupported BMP DIB header")
    width, signed_height, planes, bits_per_pixel, compression = struct.unpack_from(
        "<iiHHI", data, 18
    )
    if width <= 0 or signed_height == 0 or planes != 1:
        raise ValidationError(f"{path}: invalid BMP dimensions or plane count")
    if bits_per_pixel not in (24, 32) or compression != 0:
        raise ValidationError(
            f"{path}: only uncompressed 24-bit and 32-bit BMP images are supported"
        )
    height = abs(signed_height)
    bytes_per_pixel = bits_per_pixel // 8
    row_stride = ((width * bits_per_pixel + 31) // 32) * 4
    required_size = pixel_offset + row_stride * height
    if required_size > len(data):
        raise ValidationError(f"{path}: truncated BMP pixel payload")

    rgb = bytearray(width * height * 3)
    top_down = signed_height < 0
    for output_y in range(height):
        source_y = output_y if top_down else height - 1 - output_y
        source = pixel_offset + source_y * row_stride
        destination = output_y * width * 3
        for _ in range(width):
            blue = data[source]
            green = data[source + 1]
            red = data[source + 2]
            rgb[destination : destination + 3] = bytes((red, green, blue))
            source += bytes_per_pixel
            destination += 3
    return BmpImage(width, height, bytes(rgb))


def write_bmp(path: Path, image: BmpImage) -> None:
    if len(image.rgb) != image.width * image.height * 3:
        raise ValidationError("BMP writer received a mismatched RGB payload")
    path.parent.mkdir(parents=True, exist_ok=True)
    row_bytes = image.width * 3
    row_stride = (row_bytes + 3) & ~3
    pixel_bytes = row_stride * image.height
    pixel_offset = 14 + 40
    header = struct.pack(
        "<2sIHHI", b"BM", pixel_offset + pixel_bytes, 0, 0, pixel_offset
    )
    dib = struct.pack(
        "<IiiHHIIiiII",
        40,
        image.width,
        image.height,
        1,
        24,
        0,
        pixel_bytes,
        2835,
        2835,
        0,
        0,
    )
    padding = b"\0" * (row_stride - row_bytes)
    output = bytearray(header + dib)
    for y in range(image.height - 1, -1, -1):
        source = y * row_bytes
        row = image.rgb[source : source + row_bytes]
        for x in range(image.width):
            red, green, blue = row[x * 3 : x * 3 + 3]
            output.extend((blue, green, red))
        output.extend(padding)
    path.write_bytes(output)


def empty_histogram() -> list[int]:
    return [0] * 511


def add_histogram(destination: list[int], source: Sequence[int]) -> None:
    for index, count in enumerate(source):
        destination[index] += count


def nearest_rank(histogram: Sequence[int], percentile: float) -> float:
    total = sum(histogram)
    if total == 0:
        raise ValidationError("cannot evaluate an empty error distribution")
    rank = max(1, math.ceil(percentile * total))
    accumulated = 0
    for value, count in enumerate(histogram):
        accumulated += count
        if accumulated >= rank:
            return float(value)
    raise AssertionError("histogram rank exceeded its sample count")


def summarize_histogram(histogram: Sequence[int]) -> dict[str, float | int | bool]:
    total = sum(histogram)
    if total == 0:
        raise ValidationError("cannot summarize an empty error distribution")
    maximum = float(max(index for index, count in enumerate(histogram) if count))
    percentile_99 = nearest_rank(histogram, 0.99)
    return {
        "samples": total,
        "p50": nearest_rank(histogram, 0.50),
        "p90": nearest_rank(histogram, 0.90),
        "p95": nearest_rank(histogram, 0.95),
        "p99": percentile_99,
        "maximum": maximum,
        "aboveP99Limit": sum(histogram[1:]),
        "aboveMaximumLimit": sum(histogram[3:]),
        "accepted": percentile_99 <= DISPLAY_PERCENTILE_99_LIMIT
        and maximum <= DISPLAY_MAXIMUM_LIMIT,
    }


def spatial_error(
    reference: BmpImage, candidate: BmpImage, retain_map: bool
) -> tuple[list[int], bytes | None]:
    if (reference.width, reference.height) != (candidate.width, candidate.height):
        raise ValidationError("reference and candidate dimensions differ")
    histogram = empty_histogram()
    error_map = bytearray(reference.width * reference.height) if retain_map else None
    for pixel in range(reference.width * reference.height):
        offset = pixel * 3
        error = max(
            abs(candidate.rgb[offset + channel] - reference.rgb[offset + channel])
            for channel in range(3)
        )
        histogram[error] += 1
        if error_map is not None:
            error_map[pixel] = error
    return histogram, bytes(error_map) if error_map is not None else None


def temporal_error(
    previous_reference: BmpImage,
    reference: BmpImage,
    previous_candidate: BmpImage,
    candidate: BmpImage,
) -> list[int]:
    dimensions = {
        (image.width, image.height)
        for image in (previous_reference, reference, previous_candidate, candidate)
    }
    if len(dimensions) != 1:
        raise ValidationError("temporal image dimensions differ")
    histogram = empty_histogram()
    for offset in range(0, len(reference.rgb), 3):
        error = max(
            abs(
                (candidate.rgb[offset + channel] - previous_candidate.rgb[offset + channel])
                - (reference.rgb[offset + channel] - previous_reference.rgb[offset + channel])
            )
            for channel in range(3)
        )
        histogram[error] += 1
    return histogram


def crop_image(image: BmpImage, roi: dict[str, int]) -> BmpImage:
    output = bytearray(roi["width"] * roi["height"] * 3)
    destination = 0
    for y in range(roi["y"], roi["y"] + roi["height"]):
        source = (y * image.width + roi["x"]) * 3
        byte_count = roi["width"] * 3
        output[destination : destination + byte_count] = image.rgb[
            source : source + byte_count
        ]
        destination += byte_count
    return BmpImage(roi["width"], roi["height"], bytes(output))


def heatmap_rgb(error_map: bytes) -> bytes:
    output = bytearray(len(error_map) * 3)
    for index, error in enumerate(error_map):
        if error == 0:
            color = (0, 0, 0)
        elif error == 1:
            color = (255, 220, 0)
        elif error == 2:
            color = (255, 96, 0)
        else:
            color = (255, 0, min(255, (error - 3) * 8))
        output[index * 3 : index * 3 + 3] = bytes(color)
    return bytes(output)


def safe_name(text: str) -> str:
    return re.sub(r"[^a-zA-Z0-9_.-]+", "-", text).strip("-") or "capture"


def require_exact_fields(value: object, expected: frozenset[str], context: str) -> dict:
    if not isinstance(value, dict):
        raise ValidationError(f"{context} must be an object")
    actual = frozenset(value)
    if actual != expected:
        missing = sorted(expected - actual)
        unexpected = sorted(actual - expected)
        raise ValidationError(
            f"{context} fields differ; missing={missing}, unexpected={unexpected}"
        )
    return value


def require_string(value: object, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValidationError(f"{context} must be a nonempty string")
    return value


def require_int(
    value: object,
    context: str,
    minimum: int = 0,
    maximum: int | None = None,
) -> int:
    if (
        type(value) is not int
        or value < minimum
        or (maximum is not None and value > maximum)
    ):
        range_text = f"at least {minimum}"
        if maximum is not None:
            range_text = f"from {minimum} through {maximum}"
        raise ValidationError(f"{context} must be an integer {range_text}")
    return value


def require_bool(value: object, context: str) -> bool:
    if type(value) is not bool:
        raise ValidationError(f"{context} must be a JSON boolean")
    return value


def require_number(value: object, context: str) -> float:
    if type(value) not in (int, float) or not math.isfinite(float(value)):
        raise ValidationError(f"{context} must be a finite number")
    return float(value)


def normalize_sha256(value: object, context: str) -> str:
    text = require_string(value, context)
    if not re.fullmatch(r"[0-9a-f]{64}", text) or set(text) == {"0"}:
        raise ValidationError(
            f"{context} must be a nonzero lowercase SHA-256 digest"
        )
    return text


def normalize_source_commit(value: object, context: str) -> str:
    text = require_string(value, context)
    if not re.fullmatch(r"[0-9a-f]{40}", text) or set(text) == {"0"}:
        raise ValidationError(
            f"{context} must be a nonzero lowercase full 40-digit Git commit"
        )
    return text


def require_canonical_token(value: object, context: str) -> str:
    text = require_string(value, context)
    if len(text) > 128 or not re.fullmatch(r"[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?", text):
        raise ValidationError(f"{context} must be a canonical lowercase token")
    return text


def require_canonical_text(value: object, context: str, maximum: int) -> str:
    text = require_string(value, context)
    if (
        len(text) > maximum
        or text[0] == " "
        or text[-1] == " "
        or any(ord(character) < 0x20 or ord(character) == 0x7F for character in text)
    ):
        raise ValidationError(f"{context} is not canonical text")
    return text


def normalize_roi(
    value: object, context: str, width: int | None = None, height: int | None = None
) -> dict[str, int]:
    raw = require_exact_fields(value, ROI_FIELDS, context)
    roi = {
        "x": require_int(raw["x"], f"{context}.x", maximum=UINT32_MAX),
        "y": require_int(raw["y"], f"{context}.y", maximum=UINT32_MAX),
        "width": require_int(
            raw["width"], f"{context}.width", 1, UINT32_MAX
        ),
        "height": require_int(
            raw["height"], f"{context}.height", 1, UINT32_MAX
        ),
    }
    if width is not None and height is not None and (
        roi["x"] + roi["width"] > width or roi["y"] + roi["height"] > height
    ):
        raise ValidationError(f"{context} escapes the {width}x{height} capture")
    return roi


def normalize_viewport(
    value: object, context: str, width: int, height: int
) -> dict[str, int]:
    raw = require_exact_fields(value, VIEWPORT_FIELDS, context)
    viewport = {
        "minX": require_int(
            raw["minX"], f"{context}.minX", maximum=UINT32_MAX
        ),
        "minY": require_int(
            raw["minY"], f"{context}.minY", maximum=UINT32_MAX
        ),
        "maxX": require_int(raw["maxX"], f"{context}.maxX", 1, UINT32_MAX),
        "maxY": require_int(raw["maxY"], f"{context}.maxY", 1, UINT32_MAX),
    }
    if viewport != {"minX": 0, "minY": 0, "maxX": width, "maxY": height}:
        raise ValidationError(
            f"{context} must cover the exact {width}x{height} captured framebuffer"
        )
    return viewport


def resolve_existing_path(base: Path, value: object, suffix: str, context: str) -> Path:
    text = require_string(value, context)
    path = Path(text)
    if not path.is_absolute():
        path = base / path
    path = path.resolve()
    if path.suffix.lower() != suffix:
        raise ValidationError(f"{path}: {context} must use {suffix}")
    if not path.is_file():
        raise ValidationError(f"{path}: {context} does not exist")
    return path


def normalize_canonical_absolute_path(value: object, context: str) -> str:
    text = require_string(value, context)
    path = Path(text)
    if not path.is_absolute():
        raise ValidationError(f"{context} must be an absolute canonical path")
    try:
        canonical = path.resolve().as_posix()
    except OSError as exception:
        raise ValidationError(f"{context} cannot be canonicalized: {exception}") from exception
    if canonical != text:
        raise ValidationError(
            f"{context} must use the canonical generic path {canonical!r}"
        )
    return text


def path_key(path: Path) -> str:
    return os.path.normcase(str(path.resolve()))


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def validate_work_proof(
    receipt: dict[str, object], implementation: str, sample_count: int, kind: str
) -> None:
    context = f"{kind} receipt {receipt.get('captureId', '<unknown>')}"
    expected_implementation = (
        REFERENCE_IMPLEMENTATION if kind == "reference" else implementation
    )
    for field in (
        "requestedImplementation",
        "resolvedImplementation",
        "appliedImplementation",
    ):
        if receipt[field] != expected_implementation:
            raise ValidationError(
                f"{context}: {field} is {receipt[field]!r}, expected "
                f"{expected_implementation!r}"
            )
    for field in (
        "requestedSampleCount",
        "resolvedSampleCount",
        "appliedSampleCount",
    ):
        if receipt[field] != sample_count:
            raise ValidationError(
                f"{context}: {field} is {receipt[field]!r}, expected {sample_count}"
            )

    if not receipt["selectedPathSucceeded"]:
        raise ValidationError(f"{context}: selected render path did not succeed")
    if receipt["submittedMainViewTriangles"] <= 0:
        raise ValidationError(f"{context}: no main-view triangle work was submitted")
    if not receipt["diagnosticsProducerProducedThisFrame"]:
        raise ValidationError(f"{context}: diagnostics producer did not run")
    if receipt["diagnosticsReadbackToken"] <= 0:
        raise ValidationError(f"{context}: diagnostics readback token is invalid")
    if receipt["diagnosticsCompletedToken"] != receipt["diagnosticsReadbackToken"]:
        raise ValidationError(f"{context}: completed diagnostics token does not match")
    if not receipt["diagnosticsDispatchComplete"]:
        raise ValidationError(f"{context}: diagnostics dispatch is incomplete")

    validity_mask = int(receipt["diagnosticsValidityMask"])
    if validity_mask & ~DIAGNOSTICS_COUNTER_ALL_VALIDITY_BITS:
        raise ValidationError(f"{context}: diagnostics validity mask has unknown bits")
    if validity_mask & REQUIRED_DIAGNOSTICS_VALIDITY_BITS != REQUIRED_DIAGNOSTICS_VALIDITY_BITS:
        raise ValidationError(f"{context}: required diagnostics counter families are absent")

    histogram = receipt["groupHistogram"]
    assert isinstance(histogram, list)
    pixel_count = int(receipt["width"]) * int(receipt["height"])
    if sum(histogram) != pixel_count:
        raise ValidationError(f"{context}: group histogram does not cover every pixel")
    if any(histogram[sample_count + 1 :]):
        raise ValidationError(f"{context}: group histogram exceeds the applied sample count")
    if sum(histogram[1 : sample_count + 1]) <= 0:
        raise ValidationError(f"{context}: diagnostics observed no covered shading groups")

    reference_evaluations = int(receipt["referenceLightingEvaluations"])
    performed_evaluations = int(receipt["performedLightingEvaluations"])
    saved_evaluations = int(receipt["lightingEvaluationsSaved"])
    if reference_evaluations <= 0 or reference_evaluations > pixel_count * sample_count:
        raise ValidationError(f"{context}: reference lighting count is invalid")
    if performed_evaluations < 0 or performed_evaluations > reference_evaluations:
        raise ValidationError(f"{context}: performed lighting count is invalid")
    if saved_evaluations != reference_evaluations - performed_evaluations:
        raise ValidationError(f"{context}: lighting evaluation conservation failed")
    if kind == "reference":
        if performed_evaluations != reference_evaluations or saved_evaluations != 0:
            raise ValidationError(f"{context}: Reference must shade every covered sample")
    elif performed_evaluations <= 0:
        raise ValidationError(f"{context}: adaptive path performed no lighting work")

    fallback_reasons_valid = bool(
        validity_mask & DIAGNOSTICS_COUNTER_FALLBACK_REASONS_VALID
    )
    fallback_reasons = receipt["fallbackReasonCounts"]
    assert isinstance(fallback_reasons, list)
    fallback_sample_count = sum(fallback_reasons[1:])
    if not fallback_reasons_valid and any(fallback_reasons):
        raise ValidationError(f"{context}: unavailable fallback counters are nonzero")
    if fallback_reasons_valid and (
        fallback_reasons[0] != 0
        or fallback_sample_count > reference_evaluations
    ):
        raise ValidationError(f"{context}: fallback counters are inconsistent")
    if kind == "reference" and fallback_sample_count != 0:
        raise ValidationError(f"{context}: Reference must not report adaptive fallback")
    for field, indices in (
        ("invalidKeyCount", FALLBACK_INVALID_KEY_REASON_INDICES),
        ("staleKeyCount", FALLBACK_STALE_KEY_REASON_INDICES),
        ("mappingFailureCount", FALLBACK_MAPPING_REASON_INDICES),
    ):
        if int(receipt[field]) != sum(fallback_reasons[index] for index in indices):
            raise ValidationError(
                f"{context}: {field} disagrees with fallback reason bins"
            )

    error_budget_valid = bool(
        validity_mask & DIAGNOSTICS_COUNTER_ERROR_BUDGET_VALID
    )
    error_budget_reasons = receipt["errorBudgetRejectionReasonCounts"]
    assert isinstance(error_budget_reasons, list)
    rejected_groups = int(receipt["errorBudgetRejectedGroupCount"])
    rejected_samples = int(receipt["errorBudgetRejectedSampleCount"])
    rejected_reason_total = sum(error_budget_reasons[1:])
    if not error_budget_valid and (
        any(error_budget_reasons) or rejected_groups != 0 or rejected_samples != 0
    ):
        raise ValidationError(
            f"{context}: unavailable error-budget counters are nonzero"
        )
    if error_budget_valid and (
        error_budget_reasons[0] != 0
        or rejected_reason_total != rejected_groups
        or rejected_samples < rejected_groups
        or rejected_samples > reference_evaluations
    ):
        raise ValidationError(f"{context}: error-budget counters are inconsistent")
    if kind == "reference":
        if error_budget_valid:
            raise ValidationError(
                f"{context}: Reference must not claim adaptive error-budget metadata"
            )
    elif not error_budget_valid:
        raise ValidationError(
            f"{context}: adaptive error-budget counters are not valid"
        )

    for field in ("invalidKeyCount", "staleKeyCount", "mappingFailureCount"):
        if receipt[field] != 0:
            raise ValidationError(f"{context}: {field} must be zero in accepted evidence")

    compact_counts = (
        int(receipt["compactReconstructDrawCount"]),
        int(receipt["compactReconstructTriangleCount"]),
        int(receipt["compactFallbackDrawCount"]),
        int(receipt["compactFallbackTriangleCount"]),
    )
    compact_candidate = kind == "candidate" and implementation == "compact-visibility"
    if compact_candidate:
        reconstruct_draws, reconstruct_triangles, fallback_draws, fallback_triangles = (
            compact_counts
        )
        if not receipt["compactShadeSucceeded"]:
            raise ValidationError(f"{context}: compact shading did not succeed")
        if receipt["compactShadeDispatchedViewCount"] <= 0:
            raise ValidationError(f"{context}: compact shading dispatched no views")
        if reconstruct_draws <= 0 or reconstruct_triangles <= 0:
            raise ValidationError(f"{context}: compact reconstruction submitted no work")
        if (fallback_draws == 0) != (fallback_triangles == 0):
            raise ValidationError(f"{context}: compact fallback draw/triangle proof differs")
        if sample_count == 16 and (fallback_draws <= 0 or fallback_triangles <= 0):
            raise ValidationError(f"{context}: 16x compact evidence is not mixed visibility")
        if reconstruct_triangles + fallback_triangles != receipt["submittedMainViewTriangles"]:
            raise ValidationError(f"{context}: compact submitted triangle split is incomplete")
    else:
        if receipt["compactShadeSucceeded"] or receipt["compactShadeDispatchedViewCount"] != 0:
            raise ValidationError(f"{context}: non-compact path claims compact shading")
        if any(compact_counts):
            raise ValidationError(f"{context}: non-compact path claims compact raster work")

    topology = receipt["preparationTopology"]
    work_values = (
        int(receipt["workListObservationCount"]),
        int(receipt["workListCapacityTotal"]),
        int(receipt["workListOccupancyTotal"]),
        int(receipt["workListPeakOccupancy"]),
        int(receipt["workListOverflowCount"]),
    )
    adaptive_candidate = kind == "candidate"
    if topology == "separated" and adaptive_candidate:
        observations, capacity, occupancy, peak, overflow = work_values
        if validity_mask & DIAGNOSTICS_COUNTER_WORK_LIST_VALID == 0:
            raise ValidationError(f"{context}: separated work-list counters are not valid")
        if observations <= 0 or capacity <= 0:
            raise ValidationError(f"{context}: separated work lists observed no capacity")
        if observations > pixel_count or occupancy > capacity or peak > capacity:
            raise ValidationError(f"{context}: separated work-list counters are inconsistent")
        if overflow != 0:
            raise ValidationError(f"{context}: ordinary validation work list overflowed")
    else:
        if validity_mask & DIAGNOSTICS_COUNTER_WORK_LIST_VALID:
            raise ValidationError(f"{context}: unavailable work-list family is marked valid")
        if any(work_values):
            raise ValidationError(f"{context}: unavailable work-list counters are nonzero")


def normalize_receipt_data(raw: object, receipt_path: Path) -> dict[str, object]:
    context = f"receipt {receipt_path}"
    source = require_exact_fields(raw, RECEIPT_FIELDS, context)
    receipt: dict[str, object] = copy.deepcopy(source)
    receipt["schema"] = require_int(
        receipt["schema"], f"{context}.schema", maximum=UINT32_MAX
    )
    if receipt["schema"] != RECEIPT_SCHEMA:
        raise ValidationError(f"{context} must use schema {RECEIPT_SCHEMA}")
    if receipt["kind"] != RECEIPT_KIND:
        raise ValidationError(
            f"{context}.kind must be the provenance tag {RECEIPT_KIND!r}"
        )
    for field in ("captureId", "suiteId", "case"):
        receipt[field] = require_canonical_token(
            receipt[field], f"{context}.{field}"
        )
    receipt["sceneName"] = require_canonical_text(
        receipt["sceneName"], f"{context}.sceneName", 512
    )
    receipt["imagePath"] = normalize_canonical_absolute_path(
        receipt["imagePath"], f"{context}.imagePath"
    )
    receipt["sourceCommit"] = normalize_source_commit(
        receipt["sourceCommit"], f"{context}.sourceCommit"
    )
    for field in SHA256_FIELDS:
        receipt[field] = normalize_sha256(receipt[field], f"{context}.{field}")
    receipt["phaseIndex"] = require_int(
        receipt["phaseIndex"], f"{context}.phaseIndex", maximum=1
    )
    receipt["phase"] = require_number(receipt["phase"], f"{context}.phase")
    if not 0.0 <= receipt["phase"] <= 1.0:
        raise ValidationError(f"{context}.phase must be from zero through one")
    receipt["width"] = require_int(
        receipt["width"], f"{context}.width", 1, UINT32_MAX
    )
    receipt["height"] = require_int(
        receipt["height"], f"{context}.height", 1, UINT32_MAX
    )
    receipt["viewport"] = normalize_viewport(
        receipt["viewport"], context + ".viewport", receipt["width"], receipt["height"]
    )
    receipt["roi"] = normalize_roi(
        receipt["roi"], context + ".roi", receipt["width"], receipt["height"]
    )
    receipt["frozenSeed"] = require_int(
        receipt["frozenSeed"], f"{context}.frozenSeed", maximum=UINT64_MAX
    )
    receipt["topologyEpoch"] = require_int(
        receipt["topologyEpoch"], f"{context}.topologyEpoch", 1, UINT64_MAX
    )
    for field in (
        "topologyRevision",
        "lodRevision",
        "deformationRevision",
        "materialRevision",
    ):
        receipt[field] = require_int(
            receipt[field], f"{context}.{field}", 1, UINT32_MAX
        )
    for field in (
        "requestedImplementation",
        "resolvedImplementation",
        "appliedImplementation",
    ):
        if receipt[field] not in (REFERENCE_IMPLEMENTATION,) + SUPPORTED_IMPLEMENTATIONS:
            raise ValidationError(f"{context}.{field} has an unknown implementation")
    for field in (
        "requestedSampleCount",
        "resolvedSampleCount",
        "appliedSampleCount",
    ):
        receipt[field] = require_int(
            receipt[field], f"{context}.{field}", 1, UINT32_MAX
        )
    for field in (
        "selectedPathSucceeded",
        "diagnosticsProducerProducedThisFrame",
        "diagnosticsDispatchComplete",
        "compactShadeSucceeded",
    ):
        receipt[field] = require_bool(receipt[field], f"{context}.{field}")
    for field in (
        "diagnosticsValidityMask",
        "compactShadeDispatchedViewCount",
    ):
        receipt[field] = require_int(
            receipt[field], f"{context}.{field}", maximum=UINT32_MAX
        )
    for field in (
        "submittedMainViewTriangles",
        "diagnosticsReadbackToken",
        "diagnosticsCompletedToken",
        "referenceLightingEvaluations",
        "performedLightingEvaluations",
        "lightingEvaluationsSaved",
        "invalidKeyCount",
        "staleKeyCount",
        "mappingFailureCount",
        "errorBudgetRejectedGroupCount",
        "errorBudgetRejectedSampleCount",
        "compactReconstructDrawCount",
        "compactReconstructTriangleCount",
        "compactFallbackDrawCount",
        "compactFallbackTriangleCount",
        "workListObservationCount",
        "workListCapacityTotal",
        "workListOccupancyTotal",
        "workListPeakOccupancy",
        "workListOverflowCount",
    ):
        receipt[field] = require_int(
            receipt[field], f"{context}.{field}", maximum=UINT64_MAX
        )
    histogram = receipt["groupHistogram"]
    if not isinstance(histogram, list) or len(histogram) != GROUP_HISTOGRAM_BIN_COUNT:
        raise ValidationError(
            f"{context}.groupHistogram must contain {GROUP_HISTOGRAM_BIN_COUNT} bins"
        )
    receipt["groupHistogram"] = [
        require_int(
            value,
            f"{context}.groupHistogram[{index}]",
            maximum=UINT64_MAX,
        )
        for index, value in enumerate(histogram)
    ]
    fallback_reasons = receipt["fallbackReasonCounts"]
    if (
        not isinstance(fallback_reasons, list)
        or len(fallback_reasons) != FALLBACK_REASON_COUNT
    ):
        raise ValidationError(
            f"{context}.fallbackReasonCounts must contain "
            f"{FALLBACK_REASON_COUNT} bins"
        )
    receipt["fallbackReasonCounts"] = [
        require_int(
            value,
            f"{context}.fallbackReasonCounts[{index}]",
            maximum=UINT64_MAX,
        )
        for index, value in enumerate(fallback_reasons)
    ]
    error_budget_reasons = receipt["errorBudgetRejectionReasonCounts"]
    if (
        not isinstance(error_budget_reasons, list)
        or len(error_budget_reasons) != ERROR_BUDGET_REASON_COUNT
    ):
        raise ValidationError(
            f"{context}.errorBudgetRejectionReasonCounts must contain "
            f"{ERROR_BUDGET_REASON_COUNT} bins"
        )
    receipt["errorBudgetRejectionReasonCounts"] = [
        require_int(
            value,
            f"{context}.errorBudgetRejectionReasonCounts[{index}]",
            maximum=UINT64_MAX,
        )
        for index, value in enumerate(error_budget_reasons)
    ]
    receipt["preparationTopology"] = require_string(
        receipt["preparationTopology"], f"{context}.preparationTopology"
    )
    if receipt["preparationTopology"] not in ("inline", "separated"):
        raise ValidationError(f"{context}.preparationTopology must be inline or separated")
    return receipt


def load_receipt(
    manifest_directory: Path,
    receipt_value: object,
    receipt_role: str,
    suite_id: str,
    implementation: str,
    sample_count: int,
    case: str,
    phase_index: int,
    expected_phase: float,
    manifest_roi: dict[str, int],
    cache: dict[str, ValidatedReceipt],
) -> ValidatedReceipt:
    receipt_path = resolve_existing_path(
        manifest_directory, receipt_value, ".json", "receipt path"
    )
    key = path_key(receipt_path)
    cached = cache.get(key)
    if cached is None:
        try:
            raw = json.loads(receipt_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exception:
            raise ValidationError(f"could not read receipt {receipt_path}: {exception}") from exception
        receipt = normalize_receipt_data(raw, receipt_path)
        image_path = resolve_existing_path(
            receipt_path.parent, receipt["imagePath"], ".bmp", "receipt imagePath"
        )
        expected_receipt_path = Path(str(image_path) + ".adaptive-msaa.json")
        if path_key(receipt_path) != path_key(expected_receipt_path):
            raise ValidationError(
                f"{receipt_path}: receipt must be the writer-produced BMP sibling "
                f"{expected_receipt_path}"
            )
        observed_digest = file_sha256(image_path)
        if observed_digest != receipt["imageSha256"]:
            raise ValidationError(
                f"{receipt_path}: BMP SHA-256 differs; expected "
                f"{receipt['imageSha256']}, observed {observed_digest}"
            )
        image = read_bmp(image_path)
        if (image.width, image.height) != (receipt["width"], receipt["height"]):
            raise ValidationError(f"{receipt_path}: BMP dimensions differ from receipt")
        cached = ValidatedReceipt(receipt, receipt_path, image_path, image)
        cache[key] = cached

    receipt = cached.data
    if receipt["suiteId"] != suite_id:
        raise ValidationError(f"{receipt_path}: suiteId differs from manifest")
    if receipt["case"] != case:
        raise ValidationError(f"{receipt_path}: case differs from manifest run")
    if receipt["recipeSha256"] is None:
        raise AssertionError("normalized receipt lost recipe SHA-256")
    if receipt["phaseIndex"] != phase_index or receipt["phase"] != expected_phase:
        raise ValidationError(f"{receipt_path}: phase identity differs from manifest")
    if receipt["roi"] != manifest_roi:
        raise ValidationError(f"{receipt_path}: ROI differs from manifest run")
    validate_work_proof(receipt, implementation, sample_count, receipt_role)
    return cached


def validate_matched_pair(reference: ValidatedReceipt, candidate: ValidatedReceipt) -> None:
    for field in MATCHED_PAIR_FIELDS:
        if reference.data[field] != candidate.data[field]:
            raise ValidationError(f"Reference/candidate receipt identity differs at {field}")
    if reference.data["captureId"] == candidate.data["captureId"]:
        raise ValidationError("Reference and candidate capture IDs alias")
    if path_key(reference.receipt_path) == path_key(candidate.receipt_path):
        raise ValidationError("Reference and candidate receipt paths alias")
    if path_key(reference.image_path) == path_key(candidate.image_path):
        raise ValidationError("Reference and candidate image paths alias")


def validate_manifest_structure(manifest: object) -> tuple[str, tuple[float, float], list[dict]]:
    root = require_exact_fields(manifest, MANIFEST_FIELDS, "manifest")
    schema = require_int(root["schema"], "manifest.schema", maximum=UINT32_MAX)
    if schema != MANIFEST_SCHEMA:
        raise ValidationError(f"manifest must use schema {MANIFEST_SCHEMA}")
    suite_id = require_canonical_token(root["suiteId"], "manifest.suiteId")
    if suite_id != VALIDATION_SUITE_ID:
        raise ValidationError(
            f"manifest.suiteId must be the fixed validation suite {VALIDATION_SUITE_ID!r}"
        )
    raw_phases = root["phases"]
    if not isinstance(raw_phases, list) or len(raw_phases) != REQUIRED_FRAME_COUNT:
        raise ValidationError("manifest.phases must contain exactly two phases")
    phases = tuple(
        require_number(value, f"manifest.phases[{index}]")
        for index, value in enumerate(raw_phases)
    )
    if phases != VALIDATION_PHASES:
        raise ValidationError(
            f"manifest phases must exactly equal {list(VALIDATION_PHASES)}"
        )

    runs = root["runs"]
    if not isinstance(runs, list) or len(runs) != REQUIRED_RUN_COUNT:
        raise ValidationError(
            f"manifest must contain exactly {REQUIRED_RUN_COUNT} validation runs"
        )
    required = {
        (implementation, sample_count, case)
        for implementation in SUPPORTED_IMPLEMENTATIONS
        for sample_count in SUPPORTED_SAMPLE_COUNTS
        for case in REQUIRED_CASES
    }
    identities: set[tuple[str, int, str]] = set()
    recipe_sha256s: set[str] = set()
    normalized_runs: list[dict] = []
    for run_index, raw_run in enumerate(runs):
        run = require_exact_fields(raw_run, RUN_FIELDS, f"manifest.runs[{run_index}]")
        implementation = require_string(
            run["implementation"], f"manifest.runs[{run_index}].implementation"
        )
        sample_count = require_int(
            run["sampleCount"],
            f"manifest.runs[{run_index}].sampleCount",
            maximum=UINT32_MAX,
        )
        case = require_canonical_token(
            run["case"], f"manifest.runs[{run_index}].case"
        )
        identity = (implementation, sample_count, case)
        if implementation not in SUPPORTED_IMPLEMENTATIONS:
            raise ValidationError(f"unsupported implementation: {implementation!r}")
        if sample_count not in SUPPORTED_SAMPLE_COUNTS:
            raise ValidationError(f"unsupported sample count: {sample_count!r}")
        if case not in REQUIRED_CASES:
            raise ValidationError(f"unknown validation case: {case!r}")
        if identity in identities:
            raise ValidationError(f"duplicate validation run: {identity}")
        identities.add(identity)
        recipe_sha256 = normalize_sha256(
            run["recipeSha256"], f"manifest.runs[{run_index}].recipeSha256"
        )
        recipe_sha256s.add(recipe_sha256)
        roi = normalize_roi(run["roi"], f"manifest.runs[{run_index}].roi")
        frames = run["frames"]
        if not isinstance(frames, list) or len(frames) != REQUIRED_FRAME_COUNT:
            raise ValidationError(f"{identity}: frames must contain exactly two entries")
        normalized_frames: list[dict] = []
        for frame_index, raw_frame in enumerate(frames):
            frame = require_exact_fields(
                raw_frame,
                FRAME_FIELDS,
                f"manifest.runs[{run_index}].frames[{frame_index}]",
            )
            phase_index = require_int(
                frame["phaseIndex"],
                f"manifest.runs[{run_index}].frames[{frame_index}].phaseIndex",
                maximum=1,
            )
            if phase_index != frame_index:
                raise ValidationError(f"{identity}: phase indices must be ordered zero, one")
            normalized_frames.append(dict(frame))
        normalized_runs.append(
            {
                "implementation": implementation,
                "sampleCount": sample_count,
                "case": case,
                "recipeSha256": recipe_sha256,
                "roi": roi,
                "frames": normalized_frames,
            }
        )
    if identities != required:
        missing = sorted(required - identities)
        unexpected = sorted(identities - required)
        raise ValidationError(
            f"manifest matrix differs; missing={missing[:4]}, unexpected={unexpected[:4]}"
        )
    if len(recipe_sha256s) != 1:
        raise ValidationError(
            "all manifest runs must name the one exact suite recipe SHA-256"
        )
    if next(iter(recipe_sha256s)) != VALIDATION_RECIPE_SHA256:
        raise ValidationError(
            "manifest runs do not name the fixed validation recipe SHA-256"
        )
    return suite_id, (phases[0], phases[1]), normalized_runs


def claim_unique(
    owner_map: dict[str, tuple], value: str, owner: tuple, context: str
) -> None:
    existing = owner_map.get(value)
    if existing is not None and existing != owner:
        raise ValidationError(f"{context} is reused by {existing} and {owner}")
    owner_map[value] = owner


def validate_saved_evaluation_aggregates(
    saved_by_bucket: dict[tuple[str, int], int]
) -> None:
    for implementation in SUPPORTED_IMPLEMENTATIONS:
        for sample_count in SUPPORTED_SAMPLE_COUNTS:
            key = (implementation, sample_count)
            if saved_by_bucket.get(key, 0) <= 0:
                raise ValidationError(
                    f"{implementation} {sample_count}x saved no lighting evaluations"
                )


def report_error_budget_reasons(counts: Sequence[int]) -> list[dict[str, object]]:
    return [
        {"reason": reason, "count": int(counts[index])}
        for index, reason in enumerate(ERROR_BUDGET_REASON_NAMES)
    ]


def report_group_distribution(counts: Sequence[int]) -> dict[str, object]:
    pixel_count = sum(int(value) for value in counts)
    return {
        "meaning": (
            "pixels by final exact-key shading-group count; groupCount 0 is "
            "resolved background or uncovered"
        ),
        "denominator": {
            "field": "sum(groupHistogram)",
            "meaning": "rendered pixels",
            "count": pixel_count,
        },
        "bins": [
            {
                "groupCount": group_count,
                "pixelCount": int(value),
                "fractionOfPixels": (
                    float(value) / float(pixel_count) if pixel_count else 0.0
                ),
            }
            for group_count, value in enumerate(counts)
        ],
    }


def report_fallback_rate(
    counts: Sequence[int], reference_lighting_evaluations: int
) -> dict[str, object]:
    fallback_sample_count = sum(int(value) for value in counts[1:])
    return {
        "numerator": {
            "field": "sum(fallbackReasonCounts[1:])",
            "meaning": "covered samples with one non-None fallback reason",
            "count": fallback_sample_count,
        },
        "denominator": {
            "field": FALLBACK_RATE_DENOMINATOR_FIELD,
            "meaning": FALLBACK_RATE_DENOMINATOR_MEANING,
            "count": reference_lighting_evaluations,
        },
        "rate": (
            float(fallback_sample_count) / float(reference_lighting_evaluations)
            if reference_lighting_evaluations
            else 0.0
        ),
        "reasonCounts": [
            {"reason": reason, "count": int(counts[index])}
            for index, reason in enumerate(FALLBACK_REASON_NAMES)
        ],
    }


def evaluate_manifest(
    manifest_path: Path,
    heatmap_directory: Path | None,
    *,
    expected_dimensions: tuple[int, int] = VALIDATION_DIMENSIONS,
) -> dict[str, object]:
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exception:
        raise ValidationError(f"could not read manifest: {exception}") from exception
    suite_id, phases, runs = validate_manifest_structure(manifest)
    manifest_directory = manifest_path.parent

    receipt_cache: dict[str, ValidatedReceipt] = {}
    reference_by_identity: dict[tuple[int, str, int], tuple[str, str, str]] = {}
    reference_use_count: dict[tuple[int, str, int], int] = {}
    reference_receipt_owners: dict[str, tuple] = {}
    reference_capture_owners: dict[str, tuple] = {}
    reference_image_owners: dict[str, tuple] = {}
    candidate_receipt_owners: dict[str, tuple] = {}
    candidate_capture_owners: dict[str, tuple] = {}
    candidate_image_owners: dict[str, tuple] = {}
    case_contracts: dict[str, tuple[str, str, str]] = {}
    global_identity: dict[str, object] | None = None
    saved_by_bucket: dict[tuple[str, int], int] = {}
    error_budget_by_bucket: dict[tuple[str, int], dict[str, object]] = {}
    group_histograms_by_bucket: dict[tuple[str, int], list[int]] = {}
    fallback_by_bucket: dict[tuple[str, int], dict[str, object]] = {}
    report_runs: list[dict[str, object]] = []
    all_accepted = True

    for run in runs:
        implementation = run["implementation"]
        sample_count = run["sampleCount"]
        case = run["case"]
        recipe_sha256 = run["recipeSha256"]
        roi = run["roi"]
        frames = run["frames"]
        assert isinstance(implementation, str)
        assert isinstance(sample_count, int)
        assert isinstance(case, str)
        assert isinstance(recipe_sha256, str)
        assert isinstance(roi, dict)
        assert isinstance(frames, list)

        full_spatial_total = empty_histogram()
        roi_spatial_total = empty_histogram()
        full_temporal_total = empty_histogram()
        roi_temporal_total = empty_histogram()
        previous_reference: BmpImage | None = None
        previous_candidate: BmpImage | None = None
        previous_reference_roi: BmpImage | None = None
        previous_candidate_roi: BmpImage | None = None
        run_dimensions: tuple[int, int] | None = None
        frame_reports: list[dict[str, object]] = []
        run_group_histogram = [0] * GROUP_HISTOGRAM_BIN_COUNT
        run_fallback_reason_counts = [0] * FALLBACK_REASON_COUNT
        run_fallback_denominator = 0

        for frame_index, frame in enumerate(frames):
            phase = phases[frame_index]
            reference = load_receipt(
                manifest_directory,
                frame["referenceReceipt"],
                "reference",
                suite_id,
                implementation,
                sample_count,
                case,
                frame_index,
                phase,
                roi,
                receipt_cache,
            )
            candidate = load_receipt(
                manifest_directory,
                frame["candidateReceipt"],
                "candidate",
                suite_id,
                implementation,
                sample_count,
                case,
                frame_index,
                phase,
                roi,
                receipt_cache,
            )
            for receipt in (reference, candidate):
                dimensions = (
                    int(receipt.data["width"]),
                    int(receipt.data["height"]),
                )
                if dimensions != expected_dimensions:
                    raise ValidationError(
                        f"{receipt.receipt_path}: capture dimensions {dimensions} "
                        f"differ from the fixed validation dimensions "
                        f"{expected_dimensions}"
                    )
            if reference.data["recipeSha256"] != recipe_sha256 or candidate.data[
                "recipeSha256"
            ] != recipe_sha256:
                raise ValidationError("receipt recipe SHA-256 differs from manifest run")
            validate_work_proof(reference.data, implementation, sample_count, "reference")
            validate_work_proof(candidate.data, implementation, sample_count, "candidate")
            validate_matched_pair(reference, candidate)

            for receipt in (reference, candidate):
                if global_identity is None:
                    global_identity = {
                        field: copy.deepcopy(receipt.data[field])
                        for field in GLOBAL_IDENTITY_FIELDS
                    }
                else:
                    for field in GLOBAL_IDENTITY_FIELDS:
                        if receipt.data[field] != global_identity[field]:
                            raise ValidationError(
                                f"suite-wide receipt identity differs at {field}"
                            )

            case_contract = (
                recipe_sha256,
                json.dumps(roi, sort_keys=True, separators=(",", ":")),
                str(reference.data["settingsSha256"]),
            )
            existing_case_contract = case_contracts.get(case)
            if existing_case_contract is not None and existing_case_contract != case_contract:
                raise ValidationError(f"case recipe/settings identity changed for {case}")
            case_contracts[case] = case_contract

            reference_identity = (sample_count, case, frame_index)
            reference_fingerprint = (
                path_key(reference.receipt_path),
                str(reference.data["captureId"]),
                path_key(reference.image_path),
            )
            existing_reference = reference_by_identity.get(reference_identity)
            if existing_reference is not None and existing_reference != reference_fingerprint:
                raise ValidationError(
                    f"Reference reuse for {reference_identity} changed capture identity"
                )
            reference_by_identity[reference_identity] = reference_fingerprint
            reference_use_count[reference_identity] = (
                reference_use_count.get(reference_identity, 0) + 1
            )
            claim_unique(
                reference_receipt_owners,
                reference_fingerprint[0],
                reference_identity,
                "Reference receipt path",
            )
            claim_unique(
                reference_capture_owners,
                reference_fingerprint[1],
                reference_identity,
                "Reference capture ID",
            )
            claim_unique(
                reference_image_owners,
                reference_fingerprint[2],
                reference_identity,
                "Reference image path",
            )

            candidate_identity = (implementation, sample_count, case, frame_index)
            candidate_fingerprint = (
                path_key(candidate.receipt_path),
                str(candidate.data["captureId"]),
                path_key(candidate.image_path),
            )
            for value, candidate_map, reference_map, label in (
                (
                    candidate_fingerprint[0],
                    candidate_receipt_owners,
                    reference_receipt_owners,
                    "candidate receipt path",
                ),
                (
                    candidate_fingerprint[1],
                    candidate_capture_owners,
                    reference_capture_owners,
                    "candidate capture ID",
                ),
                (
                    candidate_fingerprint[2],
                    candidate_image_owners,
                    reference_image_owners,
                    "candidate image path",
                ),
            ):
                if value in reference_map:
                    raise ValidationError(f"{label} aliases Reference evidence")
                if value in candidate_map:
                    raise ValidationError(f"{label} is reused by two candidate cells")
                candidate_map[value] = candidate_identity
            for value, candidate_map, label in (
                (reference_fingerprint[0], candidate_receipt_owners, "Reference receipt"),
                (reference_fingerprint[1], candidate_capture_owners, "Reference capture ID"),
                (reference_fingerprint[2], candidate_image_owners, "Reference image"),
            ):
                if value in candidate_map:
                    raise ValidationError(f"{label} aliases candidate evidence")

            dimensions = (reference.image.width, reference.image.height)
            if dimensions != (candidate.image.width, candidate.image.height):
                raise ValidationError("reference and candidate dimensions differ")
            if run_dimensions is None:
                run_dimensions = dimensions
            elif dimensions != run_dimensions:
                raise ValidationError("a run changed dimensions between frames")
            normalized_roi = normalize_roi(roi, f"{case}.roi", *dimensions)
            reference_roi = crop_image(reference.image, normalized_roi)
            candidate_roi = crop_image(candidate.image, normalized_roi)

            keep_map = heatmap_directory is not None
            full_spatial_histogram, full_error_map = spatial_error(
                reference.image, candidate.image, keep_map
            )
            roi_spatial_histogram, roi_error_map = spatial_error(
                reference_roi, candidate_roi, keep_map
            )
            add_histogram(full_spatial_total, full_spatial_histogram)
            add_histogram(roi_spatial_total, roi_spatial_histogram)
            full_spatial_summary = summarize_histogram(full_spatial_histogram)
            roi_spatial_summary = summarize_histogram(roi_spatial_histogram)

            full_temporal_summary: dict[str, float | int | bool] | None = None
            roi_temporal_summary: dict[str, float | int | bool] | None = None
            if (
                previous_reference is not None
                and previous_candidate is not None
                and previous_reference_roi is not None
                and previous_candidate_roi is not None
            ):
                full_temporal_histogram = temporal_error(
                    previous_reference,
                    reference.image,
                    previous_candidate,
                    candidate.image,
                )
                roi_temporal_histogram = temporal_error(
                    previous_reference_roi,
                    reference_roi,
                    previous_candidate_roi,
                    candidate_roi,
                )
                add_histogram(full_temporal_total, full_temporal_histogram)
                add_histogram(roi_temporal_total, roi_temporal_histogram)
                full_temporal_summary = summarize_histogram(full_temporal_histogram)
                roi_temporal_summary = summarize_histogram(roi_temporal_histogram)

            if heatmap_directory is not None:
                stem = (
                    f"{safe_name(implementation)}-{sample_count}x-"
                    f"{safe_name(case)}-{frame_index:04d}"
                )
                if full_error_map is not None:
                    write_bmp(
                        heatmap_directory / f"{stem}-full.bmp",
                        BmpImage(
                            reference.image.width,
                            reference.image.height,
                            heatmap_rgb(full_error_map),
                        ),
                    )
                if roi_error_map is not None:
                    write_bmp(
                        heatmap_directory / f"{stem}-roi.bmp",
                        BmpImage(
                            reference_roi.width,
                            reference_roi.height,
                            heatmap_rgb(roi_error_map),
                        ),
                    )

            frame_accepted = (
                bool(full_spatial_summary["accepted"])
                and bool(roi_spatial_summary["accepted"])
                and (
                    full_temporal_summary is None
                    or (
                        bool(full_temporal_summary["accepted"])
                        and roi_temporal_summary is not None
                        and bool(roi_temporal_summary["accepted"])
                    )
                )
            )
            all_accepted = all_accepted and frame_accepted
            candidate_group_histogram = candidate.data["groupHistogram"]
            candidate_fallback_reason_counts = candidate.data[
                "fallbackReasonCounts"
            ]
            assert isinstance(candidate_group_histogram, list)
            assert isinstance(candidate_fallback_reason_counts, list)
            add_histogram(run_group_histogram, candidate_group_histogram)
            add_histogram(
                run_fallback_reason_counts,
                candidate_fallback_reason_counts,
            )
            run_fallback_denominator += int(
                candidate.data[FALLBACK_RATE_DENOMINATOR_FIELD]
            )
            frame_reports.append(
                {
                    "phaseIndex": frame_index,
                    "phase": phase,
                    "referenceReceipt": str(reference.receipt_path),
                    "candidateReceipt": str(candidate.receipt_path),
                    "referenceImage": str(reference.image_path),
                    "candidateImage": str(candidate.image_path),
                    "referenceCaptureId": reference.data["captureId"],
                    "candidateCaptureId": candidate.data["captureId"],
                    "appliedImplementation": candidate.data["appliedImplementation"],
                    "appliedSampleCount": candidate.data["appliedSampleCount"],
                    "submittedMainViewTriangles": candidate.data[
                        "submittedMainViewTriangles"
                    ],
                    "lightingEvaluationsSaved": candidate.data[
                        "lightingEvaluationsSaved"
                    ],
                    "groupDistribution": report_group_distribution(
                        candidate_group_histogram
                    ),
                    "fallbackRate": report_fallback_rate(
                        candidate_fallback_reason_counts,
                        int(candidate.data[FALLBACK_RATE_DENOMINATOR_FIELD]),
                    ),
                    "errorBudgetRejectionReasonCounts":
                        report_error_budget_reasons(
                            candidate.data["errorBudgetRejectionReasonCounts"]
                        ),
                    "errorBudgetRejectedGroupCount": candidate.data[
                        "errorBudgetRejectedGroupCount"
                    ],
                    "errorBudgetRejectedSampleCount": candidate.data[
                        "errorBudgetRejectedSampleCount"
                    ],
                    "compactReconstructDrawCount": candidate.data[
                        "compactReconstructDrawCount"
                    ],
                    "compactFallbackDrawCount": candidate.data[
                        "compactFallbackDrawCount"
                    ],
                    "fullFrameSpatial": full_spatial_summary,
                    "roiSpatial": roi_spatial_summary,
                    "fullFrameTemporal": full_temporal_summary,
                    "roiTemporal": roi_temporal_summary,
                    "accepted": frame_accepted,
                }
            )
            saved_key = (implementation, sample_count)
            saved_by_bucket[saved_key] = saved_by_bucket.get(saved_key, 0) + int(
                candidate.data["lightingEvaluationsSaved"]
            )
            bucket_group_histogram = group_histograms_by_bucket.setdefault(
                saved_key, [0] * GROUP_HISTOGRAM_BIN_COUNT
            )
            add_histogram(bucket_group_histogram, candidate_group_histogram)
            fallback_bucket = fallback_by_bucket.setdefault(
                saved_key,
                {
                    "reasonCounts": [0] * FALLBACK_REASON_COUNT,
                    "referenceLightingEvaluations": 0,
                },
            )
            bucket_fallback_reason_counts = fallback_bucket["reasonCounts"]
            assert isinstance(bucket_fallback_reason_counts, list)
            add_histogram(
                bucket_fallback_reason_counts,
                candidate_fallback_reason_counts,
            )
            fallback_bucket["referenceLightingEvaluations"] = int(
                fallback_bucket["referenceLightingEvaluations"]
            ) + int(candidate.data[FALLBACK_RATE_DENOMINATOR_FIELD])
            error_budget_bucket = error_budget_by_bucket.setdefault(
                saved_key,
                {
                    "rejectedGroupCount": 0,
                    "rejectedSampleCount": 0,
                    "reasonCounts": [0] * ERROR_BUDGET_REASON_COUNT,
                },
            )
            error_budget_bucket["rejectedGroupCount"] = int(
                error_budget_bucket["rejectedGroupCount"]
            ) + int(candidate.data["errorBudgetRejectedGroupCount"])
            error_budget_bucket["rejectedSampleCount"] = int(
                error_budget_bucket["rejectedSampleCount"]
            ) + int(candidate.data["errorBudgetRejectedSampleCount"])
            aggregate_reason_counts = error_budget_bucket["reasonCounts"]
            candidate_reason_counts = candidate.data[
                "errorBudgetRejectionReasonCounts"
            ]
            assert isinstance(aggregate_reason_counts, list)
            assert isinstance(candidate_reason_counts, list)
            for reason_index in range(ERROR_BUDGET_REASON_COUNT):
                aggregate_reason_counts[reason_index] += int(
                    candidate_reason_counts[reason_index]
                )
            previous_reference = reference.image
            previous_candidate = candidate.image
            previous_reference_roi = reference_roi
            previous_candidate_roi = candidate_roi

        full_spatial_summary = summarize_histogram(full_spatial_total)
        roi_spatial_summary = summarize_histogram(roi_spatial_total)
        full_temporal_summary = summarize_histogram(full_temporal_total)
        roi_temporal_summary = summarize_histogram(roi_temporal_total)
        run_accepted = (
            bool(full_spatial_summary["accepted"])
            and bool(roi_spatial_summary["accepted"])
            and bool(full_temporal_summary["accepted"])
            and bool(roi_temporal_summary["accepted"])
            and all(bool(frame["accepted"]) for frame in frame_reports)
        )
        all_accepted = all_accepted and run_accepted
        report_runs.append(
            {
                "implementation": implementation,
                "sampleCount": sample_count,
                "case": case,
                "recipeSha256": recipe_sha256,
                "roi": roi,
                "width": run_dimensions[0] if run_dimensions else 0,
                "height": run_dimensions[1] if run_dimensions else 0,
                "groupDistribution": report_group_distribution(
                    run_group_histogram
                ),
                "fallbackRate": report_fallback_rate(
                    run_fallback_reason_counts,
                    run_fallback_denominator,
                ),
                "fullFrameSpatial": full_spatial_summary,
                "roiSpatial": roi_spatial_summary,
                "fullFrameTemporal": full_temporal_summary,
                "roiTemporal": roi_temporal_summary,
                "frames": frame_reports,
                "accepted": run_accepted,
            }
        )

    expected_reference_count = (
        len(SUPPORTED_SAMPLE_COUNTS) * len(REQUIRED_CASES) * REQUIRED_FRAME_COUNT
    )
    if len(reference_by_identity) != expected_reference_count:
        raise ValidationError("Reference capture identity set is incomplete")
    for identity, count in reference_use_count.items():
        if count != len(SUPPORTED_IMPLEMENTATIONS):
            raise ValidationError(
                f"Reference capture {identity} was not reused exactly across both modes"
            )
    if len(candidate_capture_owners) != REQUIRED_RUN_COUNT * REQUIRED_FRAME_COUNT:
        raise ValidationError("candidate capture identity set is incomplete")
    validate_saved_evaluation_aggregates(saved_by_bucket)

    saved_report = [
        {
            "implementation": implementation,
            "sampleCount": sample_count,
            "lightingEvaluationsSaved": saved_by_bucket[(implementation, sample_count)],
        }
        for implementation in SUPPORTED_IMPLEMENTATIONS
        for sample_count in SUPPORTED_SAMPLE_COUNTS
    ]
    error_budget_report = [
        {
            "implementation": implementation,
            "sampleCount": sample_count,
            "rejectedGroupCount": error_budget_by_bucket[
                (implementation, sample_count)
            ]["rejectedGroupCount"],
            "rejectedSampleCount": error_budget_by_bucket[
                (implementation, sample_count)
            ]["rejectedSampleCount"],
            "reasonCounts": report_error_budget_reasons(
                error_budget_by_bucket[(implementation, sample_count)][
                    "reasonCounts"
                ]
            ),
        }
        for implementation in SUPPORTED_IMPLEMENTATIONS
        for sample_count in SUPPORTED_SAMPLE_COUNTS
    ]
    group_distribution_report = [
        {
            "implementation": implementation,
            "sampleCount": sample_count,
            **report_group_distribution(
                group_histograms_by_bucket[(implementation, sample_count)]
            ),
        }
        for implementation in SUPPORTED_IMPLEMENTATIONS
        for sample_count in SUPPORTED_SAMPLE_COUNTS
    ]
    fallback_rate_report = [
        {
            "implementation": implementation,
            "sampleCount": sample_count,
            **report_fallback_rate(
                fallback_by_bucket[(implementation, sample_count)][
                    "reasonCounts"
                ],
                int(
                    fallback_by_bucket[(implementation, sample_count)][
                        "referenceLightingEvaluations"
                    ]
                ),
            ),
        }
        for implementation in SUPPORTED_IMPLEMENTATIONS
        for sample_count in SUPPORTED_SAMPLE_COUNTS
    ]
    return {
        "schema": MANIFEST_SCHEMA,
        "suiteId": suite_id,
        "recipeSha256": runs[0]["recipeSha256"],
        "metric": "maximum absolute sRGB code-value channel error",
        "temporalMetric": (
            "maximum absolute candidate-minus-Reference frame-delta error"
        ),
        "limits": {
            "p99": DISPLAY_PERCENTILE_99_LIMIT,
            "maximum": DISPLAY_MAXIMUM_LIMIT,
        },
        "matrix": {
            "implementations": list(SUPPORTED_IMPLEMENTATIONS),
            "sampleCounts": list(SUPPORTED_SAMPLE_COUNTS),
            "cases": list(REQUIRED_CASES),
            "runCount": REQUIRED_RUN_COUNT,
            "framesPerRun": REQUIRED_FRAME_COUNT,
            "comparisonFrameCount": REQUIRED_RUN_COUNT * REQUIRED_FRAME_COUNT,
            "uniqueReferenceCaptureCount": expected_reference_count,
            "uniqueCandidateCaptureCount": REQUIRED_RUN_COUNT * REQUIRED_FRAME_COUNT,
        },
        "globalIdentity": global_identity,
        "workProof": {
            "savedEvaluationsByModeAndSampleCount": saved_report,
            "groupDistributionByModeAndSampleCount":
                group_distribution_report,
            "fallbackRateByModeAndSampleCount": fallback_rate_report,
            "errorBudgetRejectionsByModeAndSampleCount": error_budget_report,
        },
        "runs": report_runs,
        "accepted": all_accepted,
    }


def make_test_image(width: int, height: int, values: Iterable[int]) -> BmpImage:
    channels = bytes(values)
    if len(channels) != width * height * 3:
        raise AssertionError("self-test image size mismatch")
    return BmpImage(width, height, channels)


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def test_digest(label: str) -> str:
    return hashlib.sha256(label.encode("utf-8")).hexdigest()


def expect_validation_error(
    label: str, operation: Callable[[], object], expected_fragment: str | None = None
) -> None:
    try:
        operation()
    except ValidationError as exception:
        if expected_fragment is not None and expected_fragment not in str(exception):
            raise AssertionError(
                f"{label}: expected error containing {expected_fragment!r}, got {exception!r}"
            ) from exception
        return
    raise AssertionError(f"{label}: expected ValidationError")


def make_self_test_receipt(
    *,
    role: str,
    capture_id: str,
    image_path: Path,
    suite_id: str,
    case: str,
    recipe_sha256: str,
    settings_sha256: str,
    phase_index: int,
    phase: float,
    implementation: str,
    sample_count: int,
    token: int,
    width: int,
    height: int,
    roi: dict[str, int],
) -> dict[str, object]:
    expected_implementation = (
        REFERENCE_IMPLEMENTATION if role == "reference" else implementation
    )
    pixel_count = width * height
    covered_pixels = pixel_count // 2
    group_histogram = [0] * GROUP_HISTOGRAM_BIN_COUNT
    group_histogram[0] = pixel_count - covered_pixels
    group_histogram[sample_count if role == "reference" else 1] = covered_pixels
    reference_evaluations = covered_pixels * sample_count
    performed_evaluations = (
        reference_evaluations if role == "reference" else covered_pixels
    )
    adaptive_candidate = role == "candidate"
    fallback_reasons = [0] * FALLBACK_REASON_COUNT
    if adaptive_candidate:
        fallback_reasons[17] = 2
    error_budget_reasons = [0, 1, 1, 0] if adaptive_candidate else [0, 0, 0, 0]
    error_budget_rejected_groups = sum(error_budget_reasons[1:])
    error_budget_rejected_samples = (
        error_budget_rejected_groups + 1 if adaptive_candidate else 0
    )
    compact_candidate = role == "candidate" and implementation == "compact-visibility"
    if compact_candidate and sample_count == 16:
        reconstruct_draws, reconstruct_triangles = 1, 6
        fallback_draws, fallback_triangles = 1, 6
    elif compact_candidate:
        reconstruct_draws, reconstruct_triangles = 2, 12
        fallback_draws, fallback_triangles = 0, 0
    else:
        reconstruct_draws = reconstruct_triangles = 0
        fallback_draws = fallback_triangles = 0
    return {
        "schema": RECEIPT_SCHEMA,
        "kind": RECEIPT_KIND,
        "captureId": capture_id,
        "suiteId": suite_id,
        "case": case,
        "recipeSha256": recipe_sha256,
        "phaseIndex": phase_index,
        "phase": phase,
        "imagePath": image_path.resolve().as_posix(),
        "imageSha256": file_sha256(image_path),
        "sourceCommit": "578496047c4753077e36f39a68df8c2c323cee58",
        "sourceDiffSha256": test_digest("source-diff"),
        "rendererBuildManifestSha256": test_digest("build-manifest"),
        "rendererExecutableSha256": test_digest("renderer-executable"),
        "sceneName": "adaptive_msaa_validation.scene.json",
        "sceneDescriptorSha256": test_digest("scene-descriptor"),
        "sceneGltfSha256": test_digest("scene-gltf"),
        "cameraSha256": test_digest("camera"),
        "settingsSha256": settings_sha256,
        "width": width,
        "height": height,
        "viewport": {"minX": 0, "minY": 0, "maxX": width, "maxY": height},
        "frozenSeed": 4096 + phase_index,
        "topologyEpoch": 7,
        "topologyRevision": 11,
        "lodRevision": 13,
        "deformationRevision": 17,
        "materialRevision": 19,
        "requestedImplementation": expected_implementation,
        "resolvedImplementation": expected_implementation,
        "appliedImplementation": expected_implementation,
        "requestedSampleCount": sample_count,
        "resolvedSampleCount": sample_count,
        "appliedSampleCount": sample_count,
        "selectedPathSucceeded": True,
        "submittedMainViewTriangles": 12,
        "diagnosticsProducerProducedThisFrame": True,
        "diagnosticsReadbackToken": token,
        "diagnosticsCompletedToken": token,
        "diagnosticsDispatchComplete": True,
        "diagnosticsValidityMask": REQUIRED_DIAGNOSTICS_VALIDITY_BITS
        | (
            DIAGNOSTICS_COUNTER_ERROR_BUDGET_VALID
            if adaptive_candidate
            else 0
        ),
        "groupHistogram": group_histogram,
        "referenceLightingEvaluations": reference_evaluations,
        "performedLightingEvaluations": performed_evaluations,
        "lightingEvaluationsSaved": reference_evaluations - performed_evaluations,
        "fallbackReasonCounts": fallback_reasons,
        "invalidKeyCount": 0,
        "staleKeyCount": 0,
        "mappingFailureCount": 0,
        "errorBudgetRejectionReasonCounts": error_budget_reasons,
        "errorBudgetRejectedGroupCount": error_budget_rejected_groups,
        "errorBudgetRejectedSampleCount": error_budget_rejected_samples,
        "compactShadeSucceeded": compact_candidate,
        "compactShadeDispatchedViewCount": 1 if compact_candidate else 0,
        "compactReconstructDrawCount": reconstruct_draws,
        "compactReconstructTriangleCount": reconstruct_triangles,
        "compactFallbackDrawCount": fallback_draws,
        "compactFallbackTriangleCount": fallback_triangles,
        "preparationTopology": "inline",
        "workListObservationCount": 0,
        "workListCapacityTotal": 0,
        "workListOccupancyTotal": 0,
        "workListPeakOccupancy": 0,
        "workListOverflowCount": 0,
        "roi": roi,
    }


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="uvsr-adaptive-msaa-diff-") as directory:
        root = Path(directory)
        width = 20
        height = 20
        roi = {"x": 10, "y": 10, "width": 10, "height": 10}
        phases = [0.25, 0.50]
        suite_id = VALIDATION_SUITE_ID
        reference_image = make_test_image(
            width, height, [128] * width * height * 3
        )
        candidate_channels = bytearray(reference_image.rgb)
        candidate_channels[0] = 129
        candidate_image = BmpImage(width, height, bytes(candidate_channels))

        token = 1
        recipe_sha256 = VALIDATION_RECIPE_SHA256
        reference_paths: dict[tuple[int, str, int], Path] = {}
        runs: list[dict[str, object]] = []
        for implementation in SUPPORTED_IMPLEMENTATIONS:
            for sample_count in SUPPORTED_SAMPLE_COUNTS:
                for case in REQUIRED_CASES:
                    settings_sha256 = test_digest("settings-" + case)
                    frames: list[dict[str, object]] = []
                    for phase_index, phase in enumerate(phases):
                        reference_key = (sample_count, case, phase_index)
                        reference_receipt_path = reference_paths.get(reference_key)
                        if reference_receipt_path is None:
                            reference_directory = (
                                root
                                / "captures"
                                / "reference"
                                / f"{sample_count}x"
                                / case
                                / str(phase_index)
                            )
                            reference_bmp_path = reference_directory / "capture.bmp"
                            reference_receipt_path = Path(
                                str(reference_bmp_path) + ".adaptive-msaa.json"
                            )
                            write_bmp(reference_bmp_path, reference_image)
                            reference_receipt = make_self_test_receipt(
                                role="reference",
                                capture_id=(
                                    f"reference-{sample_count}x-{case}-p{phase_index}"
                                ),
                                image_path=reference_bmp_path,
                                suite_id=suite_id,
                                case=case,
                                recipe_sha256=recipe_sha256,
                                settings_sha256=settings_sha256,
                                phase_index=phase_index,
                                phase=phase,
                                implementation=implementation,
                                sample_count=sample_count,
                                token=token,
                                width=width,
                                height=height,
                                roi=roi,
                            )
                            token += 1
                            write_json(reference_receipt_path, reference_receipt)
                            reference_paths[reference_key] = reference_receipt_path

                        candidate_directory = (
                            root
                            / "captures"
                            / "candidate"
                            / implementation
                            / f"{sample_count}x"
                            / case
                            / str(phase_index)
                        )
                        candidate_bmp_path = candidate_directory / "capture.bmp"
                        candidate_receipt_path = Path(
                            str(candidate_bmp_path) + ".adaptive-msaa.json"
                        )
                        write_bmp(candidate_bmp_path, candidate_image)
                        candidate_receipt = make_self_test_receipt(
                            role="candidate",
                            capture_id=(
                                f"candidate-{implementation}-{sample_count}x-"
                                f"{case}-p{phase_index}"
                            ),
                            image_path=candidate_bmp_path,
                            suite_id=suite_id,
                            case=case,
                            recipe_sha256=recipe_sha256,
                            settings_sha256=settings_sha256,
                            phase_index=phase_index,
                            phase=phase,
                            implementation=implementation,
                            sample_count=sample_count,
                            token=token,
                            width=width,
                            height=height,
                            roi=roi,
                        )
                        token += 1
                        write_json(candidate_receipt_path, candidate_receipt)
                        frames.append(
                            {
                                "phaseIndex": phase_index,
                                "referenceReceipt": str(
                                    reference_receipt_path.relative_to(root)
                                ),
                                "candidateReceipt": str(
                                    candidate_receipt_path.relative_to(root)
                                ),
                            }
                        )
                    runs.append(
                        {
                            "implementation": implementation,
                            "sampleCount": sample_count,
                            "case": case,
                            "recipeSha256": recipe_sha256,
                            "roi": roi,
                            "frames": frames,
                        }
                    )

        manifest = {
            "schema": MANIFEST_SCHEMA,
            "suiteId": suite_id,
            "phases": phases,
            "runs": runs,
        }
        manifest_path = root / "manifest.json"
        write_json(manifest_path, manifest)
        heatmap_directory = root / "heatmaps"
        report = evaluate_manifest(
            manifest_path,
            heatmap_directory,
            expected_dimensions=(width, height),
        )
        if not report["accepted"]:
            raise AssertionError("complete authenticated self-test matrix must pass")
        if len(report["runs"]) != REQUIRED_RUN_COUNT:
            raise AssertionError("self-test report lost matrix runs")
        error_budget_report = report["workProof"][
            "errorBudgetRejectionsByModeAndSampleCount"
        ]
        if len(error_budget_report) != (
            len(SUPPORTED_IMPLEMENTATIONS) * len(SUPPORTED_SAMPLE_COUNTS)
        ) or any(
            bucket["rejectedGroupCount"] != len(REQUIRED_CASES) * 2 * 2
            or bucket["rejectedSampleCount"] != len(REQUIRED_CASES) * 2 * 3
            for bucket in error_budget_report
        ):
            raise AssertionError("self-test report lost error-budget aggregates")
        first_run_report = report["runs"][0]
        first_frame_report = first_run_report["frames"][0]
        if (
            first_frame_report["groupDistribution"]["denominator"]["count"]
            != width * height
            or first_run_report["groupDistribution"]["denominator"]["count"]
            != width * height * REQUIRED_FRAME_COUNT
        ):
            raise AssertionError("self-test report lost frame/run group distributions")
        first_frame_fallback = first_frame_report["fallbackRate"]
        if (
            first_frame_fallback["numerator"]["count"] != 2
            or first_frame_fallback["denominator"]["field"]
            != FALLBACK_RATE_DENOMINATOR_FIELD
            or first_frame_fallback["denominator"]["meaning"]
            != FALLBACK_RATE_DENOMINATOR_MEANING
            or first_frame_fallback["denominator"]["count"]
            != (width * height // 2) * SUPPORTED_SAMPLE_COUNTS[0]
        ):
            raise AssertionError("self-test report lost frame fallback-rate proof")
        aggregate_groups = report["workProof"][
            "groupDistributionByModeAndSampleCount"
        ][0]
        aggregate_fallback = report["workProof"][
            "fallbackRateByModeAndSampleCount"
        ][0]
        expected_aggregate_pixels = (
            len(REQUIRED_CASES) * REQUIRED_FRAME_COUNT * width * height
        )
        expected_aggregate_samples = (
            len(REQUIRED_CASES)
            * REQUIRED_FRAME_COUNT
            * (width * height // 2)
            * SUPPORTED_SAMPLE_COUNTS[0]
        )
        if (
            aggregate_groups["denominator"]["count"]
            != expected_aggregate_pixels
            or aggregate_fallback["numerator"]["count"]
            != len(REQUIRED_CASES) * REQUIRED_FRAME_COUNT * 2
            or aggregate_fallback["denominator"]["count"]
            != expected_aggregate_samples
            or aggregate_fallback["reasonCounts"][17]["reason"]
            != "unsafeSharing"
        ):
            raise AssertionError("self-test report lost aggregate fallback/group proof")
        expected_heatmaps = REQUIRED_RUN_COUNT * REQUIRED_FRAME_COUNT * 2
        if len(list(heatmap_directory.glob("*.bmp"))) != expected_heatmaps:
            raise AssertionError("self-test did not emit full-frame and ROI heatmaps")
        if read_bmp(next(root.glob("captures/candidate/**/*.bmp"))) != candidate_image:
            raise AssertionError("BMP round-trip changed image data")
        expect_validation_error(
            "fixed acceptance dimensions",
            lambda: evaluate_manifest(manifest_path, None),
            "fixed validation dimensions",
        )

        def evaluate_variant(label: str, mutate: Callable[[dict], None]) -> None:
            variant = copy.deepcopy(manifest)
            mutate(variant)
            variant_path = root / f"negative-{safe_name(label)}.json"
            write_json(variant_path, variant)
            expect_validation_error(
                label,
                lambda: evaluate_manifest(
                    variant_path,
                    None,
                    expected_dimensions=(width, height),
                ),
            )

        evaluate_variant("schema-1 rejection", lambda value: value.update(schema=1))
        evaluate_variant(
            "suite identity rejection",
            lambda value: value.update(suiteId="different-suite"),
        )
        evaluate_variant(
            "phase recipe rejection",
            lambda value: value.update(phases=[0.0, 1.0]),
        )
        evaluate_variant(
            "suite recipe digest disagreement",
            lambda value: value["runs"][0].update(
                recipeSha256=test_digest("different-suite-recipe")
            ),
        )
        evaluate_variant(
            "wrong suite recipe digest",
            lambda value: [
                run.update(recipeSha256=test_digest("wrong-suite-recipe"))
                for run in value["runs"]
            ],
        )
        evaluate_variant("missing run", lambda value: value["runs"].pop())
        evaluate_variant(
            "duplicate run",
            lambda value: value["runs"].__setitem__(-1, copy.deepcopy(value["runs"][0])),
        )
        evaluate_variant(
            "one frame", lambda value: value["runs"][0]["frames"].pop()
        )
        evaluate_variant(
            "three frames",
            lambda value: value["runs"][0]["frames"].append(
                copy.deepcopy(value["runs"][0]["frames"][1])
            ),
        )
        evaluate_variant(
            "phase index mismatch",
            lambda value: value["runs"][0]["frames"][1].update(phaseIndex=0),
        )
        evaluate_variant(
            "Reference masquerade",
            lambda value: value["runs"][0]["frames"][0].update(
                candidateReceipt=value["runs"][0]["frames"][0]["referenceReceipt"]
            ),
        )
        evaluate_variant(
            "candidate receipt reuse",
            lambda value: value["runs"][1]["frames"][0].update(
                candidateReceipt=value["runs"][0]["frames"][0]["candidateReceipt"]
            ),
        )
        evaluate_variant(
            "Reference reuse outside identity",
            lambda value: value["runs"][1]["frames"][0].update(
                referenceReceipt=value["runs"][0]["frames"][0]["referenceReceipt"]
            ),
        )

        first_run = manifest["runs"][0]
        first_reference_path = root / first_run["frames"][0]["referenceReceipt"]
        first_candidate_path = root / first_run["frames"][0]["candidateReceipt"]
        second_candidate_path = root / first_run["frames"][1]["candidateReceipt"]

        def mutate_receipt_and_expect(
            label: str, receipt_path: Path, mutate: Callable[[dict], None]
        ) -> None:
            original = json.loads(receipt_path.read_text(encoding="utf-8"))
            changed = copy.deepcopy(original)
            mutate(changed)
            write_json(receipt_path, changed)
            try:
                expect_validation_error(
                    label,
                    lambda: evaluate_manifest(
                        manifest_path,
                        None,
                        expected_dimensions=(width, height),
                    ),
                )
            finally:
                write_json(receipt_path, original)

        mutate_receipt_and_expect(
            "receipt image hash mismatch",
            first_candidate_path,
            lambda value: value.update(imageSha256=test_digest("wrong-image")),
        )
        mutate_receipt_and_expect(
            "receipt provenance kind mismatch",
            first_candidate_path,
            lambda value: value.update(kind="candidate"),
        )
        mutate_receipt_and_expect(
            "receipt schema-1 rejection",
            first_candidate_path,
            lambda value: value.update(schema=1),
        )
        mutate_receipt_and_expect(
            "legacy caseId receipt key",
            first_candidate_path,
            lambda value: value.update(caseId=value.pop("case")),
        )
        mutate_receipt_and_expect(
            "missing case receipt key",
            first_candidate_path,
            lambda value: value.pop("case"),
        )
        mutate_receipt_and_expect(
            "legacy savedLightingEvaluations receipt key",
            first_candidate_path,
            lambda value: value.update(
                savedLightingEvaluations=value.pop("lightingEvaluationsSaved")
            ),
        )
        mutate_receipt_and_expect(
            "missing lightingEvaluationsSaved receipt key",
            first_candidate_path,
            lambda value: value.pop("lightingEvaluationsSaved"),
        )
        mutate_receipt_and_expect(
            "uppercase receipt hash",
            first_candidate_path,
            lambda value: value.update(
                imageSha256=str(value["imageSha256"]).upper()
            ),
        )
        mutate_receipt_and_expect(
            "noncanonical capture token",
            first_candidate_path,
            lambda value: value.update(captureId="candidate/invalid"),
        )
        mutate_receipt_and_expect(
            "relative receipt image path",
            first_candidate_path,
            lambda value: value.update(imagePath="capture.bmp"),
        )
        mutate_receipt_and_expect(
            "missing fallback reason counts",
            first_candidate_path,
            lambda value: value.pop("fallbackReasonCounts"),
        )
        mutate_receipt_and_expect(
            "wrong fallback reason array length",
            first_candidate_path,
            lambda value: value.update(fallbackReasonCounts=[0] * 23),
        )
        mutate_receipt_and_expect(
            "wrong error-budget reason array length",
            first_candidate_path,
            lambda value: value.update(errorBudgetRejectionReasonCounts=[0, 1, 1]),
        )
        mutate_receipt_and_expect(
            "missing error-budget rejected sample count",
            first_candidate_path,
            lambda value: value.pop("errorBudgetRejectedSampleCount"),
        )
        mutate_receipt_and_expect(
            "candidate applied Reference",
            first_candidate_path,
            lambda value: value.update(appliedImplementation="reference"),
        )

        original_candidate_receipt = json.loads(
            first_candidate_path.read_text(encoding="utf-8")
        )
        first_candidate_image_path = first_candidate_path.parent / str(
            original_candidate_receipt["imagePath"]
        )
        original_candidate_bytes = first_candidate_image_path.read_bytes()
        changed_bytes = bytearray(original_candidate_bytes)
        changed_bytes[-1] ^= 1
        first_candidate_image_path.write_bytes(changed_bytes)
        try:
            expect_validation_error(
                "BMP bytes changed after receipt",
                lambda: evaluate_manifest(
                    manifest_path,
                    None,
                    expected_dimensions=(width, height),
                ),
            )
        finally:
            first_candidate_image_path.write_bytes(original_candidate_bytes)

        receipt_cache: dict[str, ValidatedReceipt] = {}
        normalized_roi = normalize_roi(roi, "self-test.roi")
        reference_receipt = load_receipt(
            root,
            first_run["frames"][0]["referenceReceipt"],
            "reference",
            suite_id,
            str(first_run["implementation"]),
            int(first_run["sampleCount"]),
            str(first_run["case"]),
            0,
            phases[0],
            normalized_roi,
            receipt_cache,
        )
        candidate_receipt = load_receipt(
            root,
            first_run["frames"][0]["candidateReceipt"],
            "candidate",
            suite_id,
            str(first_run["implementation"]),
            int(first_run["sampleCount"]),
            str(first_run["case"]),
            0,
            phases[0],
            normalized_roi,
            receipt_cache,
        )

        mismatch_mutations: dict[str, Callable[[dict], None]] = {
            "suite": lambda value: value.update(suiteId="different-suite"),
            "recipe": lambda value: value.update(recipeSha256=test_digest("different-recipe")),
            "source commit": lambda value: value.update(sourceCommit="a" * 40),
            "source diff": lambda value: value.update(sourceDiffSha256=test_digest("different-diff")),
            "build": lambda value: value.update(rendererBuildManifestSha256=test_digest("different-build")),
            "executable": lambda value: value.update(rendererExecutableSha256=test_digest("different-exe")),
            "scene name": lambda value: value.update(sceneName="different.scene.json"),
            "scene descriptor": lambda value: value.update(sceneDescriptorSha256=test_digest("different-scene")),
            "scene glTF": lambda value: value.update(sceneGltfSha256=test_digest("different-gltf")),
            "camera": lambda value: value.update(cameraSha256=test_digest("different-camera")),
            "settings": lambda value: value.update(settingsSha256=test_digest("different-settings")),
            "viewport": lambda value: value.update(viewport={"minX": 0, "minY": 0, "maxX": 19, "maxY": 20}),
            "phase": lambda value: value.update(phase=0.375),
            "seed": lambda value: value.update(frozenSeed=4097),
            "topology epoch": lambda value: value.update(topologyEpoch=8),
            "topology revision": lambda value: value.update(topologyRevision=12),
            "LOD revision": lambda value: value.update(lodRevision=14),
            "deformation revision": lambda value: value.update(deformationRevision=18),
            "material revision": lambda value: value.update(materialRevision=20),
            "covered-sample denominator": lambda value: value.update(
                referenceLightingEvaluations=int(
                    value["referenceLightingEvaluations"]
                )
                + 1
            ),
            "ROI": lambda value: value.update(roi={"x": 9, "y": 10, "width": 10, "height": 10}),
        }
        for label, mutate in mismatch_mutations.items():
            changed = copy.deepcopy(candidate_receipt.data)
            mutate(changed)
            expect_validation_error(
                f"matched {label} mismatch",
                lambda changed=changed: validate_matched_pair(
                    reference_receipt,
                    ValidatedReceipt(
                        changed,
                        candidate_receipt.receipt_path,
                        candidate_receipt.image_path,
                        candidate_receipt.image,
                    ),
                ),
            )

        work_mutations: dict[str, Callable[[dict], None]] = {
            "requested implementation": lambda value: value.update(requestedImplementation="reference"),
            "resolved implementation": lambda value: value.update(resolvedImplementation="reference"),
            "applied implementation": lambda value: value.update(appliedImplementation="reference"),
            "requested count": lambda value: value.update(requestedSampleCount=4),
            "resolved count": lambda value: value.update(resolvedSampleCount=4),
            "applied count": lambda value: value.update(appliedSampleCount=4),
            "selected path": lambda value: value.update(selectedPathSucceeded=False),
            "triangle work": lambda value: value.update(submittedMainViewTriangles=0),
            "producer": lambda value: value.update(diagnosticsProducerProducedThisFrame=False),
            "readback token": lambda value: value.update(diagnosticsReadbackToken=0),
            "completed token": lambda value: value.update(diagnosticsCompletedToken=99999),
            "dispatch completion": lambda value: value.update(diagnosticsDispatchComplete=False),
            "validity": lambda value: value.update(
                diagnosticsValidityMask=value["diagnosticsValidityMask"]
                & ~DIAGNOSTICS_COUNTER_LIGHTING_VALID
            ),
            "fallback validity": lambda value: value.update(
                diagnosticsValidityMask=value["diagnosticsValidityMask"]
                & ~DIAGNOSTICS_COUNTER_FALLBACK_REASONS_VALID
            ),
            "covered groups": lambda value: value.update(
                groupHistogram=[width * height] + [0] * (GROUP_HISTOGRAM_BIN_COUNT - 1)
            ),
            "lighting work": lambda value: value.update(
                referenceLightingEvaluations=0,
                performedLightingEvaluations=0,
                lightingEvaluationsSaved=0,
            ),
            "lighting conservation": lambda value: value.update(lightingEvaluationsSaved=1),
            "invalid keys": lambda value: value.update(invalidKeyCount=1),
            "stale keys": lambda value: value.update(staleKeyCount=1),
            "mapping failures": lambda value: value.update(mappingFailureCount=1),
        }
        for label, mutate in work_mutations.items():
            changed = copy.deepcopy(candidate_receipt.data)
            mutate(changed)
            expect_validation_error(
                f"missing or invalid {label}",
                lambda changed=changed: validate_work_proof(
                    changed,
                    str(first_run["implementation"]),
                    int(first_run["sampleCount"]),
                    "candidate",
                ),
            )

        fallback_mutations: dict[str, Callable[[dict], None]] = {
            "reserved fallback reason": lambda value: value.update(
                fallbackReasonCounts=[1] + [0] * (FALLBACK_REASON_COUNT - 1)
            ),
            "fallback count above covered samples": lambda value: value.update(
                fallbackReasonCounts=[0] * 17
                + [int(value["referenceLightingEvaluations"]) + 1]
                + [0] * (FALLBACK_REASON_COUNT - 18)
            ),
            "invalid-key fallback category": lambda value: value.update(
                fallbackReasonCounts=[0, 1]
                + [0] * (FALLBACK_REASON_COUNT - 2)
            ),
            "stale-key fallback category": lambda value: value.update(
                fallbackReasonCounts=[0] * 4
                + [1]
                + [0] * (FALLBACK_REASON_COUNT - 5)
            ),
            "mapping fallback category": lambda value: value.update(
                fallbackReasonCounts=[0] * 13
                + [1]
                + [0] * (FALLBACK_REASON_COUNT - 14)
            ),
        }
        for label, mutate in fallback_mutations.items():
            changed = copy.deepcopy(candidate_receipt.data)
            mutate(changed)
            expect_validation_error(
                f"invalid {label}",
                lambda changed=changed: validate_work_proof(
                    changed,
                    str(first_run["implementation"]),
                    int(first_run["sampleCount"]),
                    "candidate",
                ),
            )

        error_budget_mutations: dict[str, Callable[[dict], None]] = {
            "candidate error-budget validity": lambda value: value.update(
                diagnosticsValidityMask=value["diagnosticsValidityMask"]
                & ~DIAGNOSTICS_COUNTER_ERROR_BUDGET_VALID,
                errorBudgetRejectionReasonCounts=[0, 0, 0, 0],
                errorBudgetRejectedGroupCount=0,
                errorBudgetRejectedSampleCount=0,
            ),
            "reserved error-budget reason": lambda value: value.update(
                errorBudgetRejectionReasonCounts=[1, 1, 1, 0]
            ),
            "error-budget reason sum": lambda value: value.update(
                errorBudgetRejectedGroupCount=3
            ),
            "error-budget rejected samples below groups": lambda value: value.update(
                errorBudgetRejectedSampleCount=1
            ),
            "error-budget rejected samples above Reference": lambda value: value.update(
                errorBudgetRejectedSampleCount=value[
                    "referenceLightingEvaluations"
                ]
                + 1
            ),
        }
        for label, mutate in error_budget_mutations.items():
            changed = copy.deepcopy(candidate_receipt.data)
            mutate(changed)
            expect_validation_error(
                f"invalid {label}",
                lambda changed=changed: validate_work_proof(
                    changed,
                    str(first_run["implementation"]),
                    int(first_run["sampleCount"]),
                    "candidate",
                ),
            )

        zero_rejections = copy.deepcopy(candidate_receipt.data)
        zero_rejections["errorBudgetRejectionReasonCounts"] = [0, 0, 0, 0]
        zero_rejections["errorBudgetRejectedGroupCount"] = 0
        zero_rejections["errorBudgetRejectedSampleCount"] = 0
        validate_work_proof(
            zero_rejections,
            str(first_run["implementation"]),
            int(first_run["sampleCount"]),
            "candidate",
        )

        reference_claims_error_budget = copy.deepcopy(reference_receipt.data)
        reference_claims_error_budget["diagnosticsValidityMask"] |= (
            DIAGNOSTICS_COUNTER_ERROR_BUDGET_VALID
        )
        expect_validation_error(
            "Reference claims adaptive error-budget validity",
            lambda: validate_work_proof(
                reference_claims_error_budget,
                str(first_run["implementation"]),
                int(first_run["sampleCount"]),
                "reference",
            ),
        )
        reference_claims_fallback = copy.deepcopy(reference_receipt.data)
        reference_claims_fallback["fallbackReasonCounts"][17] = 1
        expect_validation_error(
            "Reference claims adaptive fallback",
            lambda: validate_work_proof(
                reference_claims_fallback,
                str(first_run["implementation"]),
                int(first_run["sampleCount"]),
                "reference",
            ),
        )
        reference_unavailable_error_budget = copy.deepcopy(reference_receipt.data)
        reference_unavailable_error_budget[
            "errorBudgetRejectionReasonCounts"
        ] = [0, 1, 0, 0]
        reference_unavailable_error_budget["errorBudgetRejectedGroupCount"] = 1
        reference_unavailable_error_budget["errorBudgetRejectedSampleCount"] = 1
        expect_validation_error(
            "Reference unavailable error-budget counters",
            lambda: validate_work_proof(
                reference_unavailable_error_budget,
                str(first_run["implementation"]),
                int(first_run["sampleCount"]),
                "reference",
            ),
        )

        separated = copy.deepcopy(candidate_receipt.data)
        separated["preparationTopology"] = "separated"
        expect_validation_error(
            "separated work-list proof missing",
            lambda: validate_work_proof(
                separated,
                str(first_run["implementation"]),
                int(first_run["sampleCount"]),
                "candidate",
            ),
        )
        separated["diagnosticsValidityMask"] |= DIAGNOSTICS_COUNTER_WORK_LIST_VALID
        separated["workListObservationCount"] = 4
        separated["workListCapacityTotal"] = 512
        separated["workListOccupancyTotal"] = 16
        separated["workListPeakOccupancy"] = 4
        validate_work_proof(
            separated,
            str(first_run["implementation"]),
            int(first_run["sampleCount"]),
            "candidate",
        )
        separated["workListOverflowCount"] = 1
        expect_validation_error(
            "separated work-list overflow",
            lambda: validate_work_proof(
                separated,
                str(first_run["implementation"]),
                int(first_run["sampleCount"]),
                "candidate",
            ),
        )

        compact_16_run = next(
            run
            for run in manifest["runs"]
            if run["implementation"] == "compact-visibility"
            and run["sampleCount"] == 16
        )
        compact_16_path = root / compact_16_run["frames"][0]["candidateReceipt"]
        compact_16_raw = json.loads(compact_16_path.read_text(encoding="utf-8"))
        compact_16 = normalize_receipt_data(compact_16_raw, compact_16_path)
        for label, mutate in (
            (
                "compact shade",
                lambda value: value.update(compactShadeSucceeded=False),
            ),
            (
                "compact dispatch",
                lambda value: value.update(compactShadeDispatchedViewCount=0),
            ),
            (
                "compact reconstruction",
                lambda value: value.update(
                    compactReconstructDrawCount=0,
                    compactReconstructTriangleCount=0,
                    submittedMainViewTriangles=value["compactFallbackTriangleCount"],
                ),
            ),
            (
                "16x compact local fallback",
                lambda value: value.update(
                    compactFallbackDrawCount=0,
                    compactFallbackTriangleCount=0,
                    submittedMainViewTriangles=value["compactReconstructTriangleCount"],
                ),
            ),
        ):
            changed = copy.deepcopy(compact_16)
            mutate(changed)
            expect_validation_error(
                f"missing {label}",
                lambda changed=changed: validate_work_proof(
                    changed, "compact-visibility", 16, "candidate"
                ),
            )

        saved = {
            (implementation, sample_count): 1
            for implementation in SUPPORTED_IMPLEMENTATIONS
            for sample_count in SUPPORTED_SAMPLE_COUNTS
        }
        saved[("adaptive-g-buffer", 2)] = 0
        expect_validation_error(
            "zero aggregate saved evaluations",
            lambda: validate_saved_evaluation_aggregates(saved),
        )

        def replace_candidate_images_and_expect_rejected(
            label: str, replacements: dict[Path, BmpImage]
        ) -> None:
            originals: dict[Path, tuple[bytes, dict]] = {}
            try:
                for receipt_path, image in replacements.items():
                    original_receipt = json.loads(
                        receipt_path.read_text(encoding="utf-8")
                    )
                    receipt_value = copy.deepcopy(original_receipt)
                    image_path = receipt_path.parent / str(receipt_value["imagePath"])
                    originals[receipt_path] = (
                        image_path.read_bytes(),
                        original_receipt,
                    )
                    write_bmp(image_path, image)
                    receipt_value["imageSha256"] = file_sha256(image_path)
                    write_json(receipt_path, receipt_value)
                rejected = evaluate_manifest(
                    manifest_path,
                    None,
                    expected_dimensions=(width, height),
                )
                if rejected["accepted"]:
                    raise AssertionError(f"{label}: expected image-error rejection")
            finally:
                for receipt_path, (image_bytes, receipt_value) in originals.items():
                    image_path = receipt_path.parent / str(receipt_value["imagePath"])
                    image_path.write_bytes(image_bytes)
                    write_json(receipt_path, receipt_value)

        p99_channels = bytearray(reference_image.rgb)
        for pixel in range(5):
            p99_channels[pixel * 3] = 129
        replace_candidate_images_and_expect_rejected(
            "full-frame p99 limit",
            {first_candidate_path: BmpImage(width, height, bytes(p99_channels))},
        )

        maximum_channels = bytearray(reference_image.rgb)
        maximum_channels[0] = 131
        replace_candidate_images_and_expect_rejected(
            "full-frame maximum limit",
            {first_candidate_path: BmpImage(width, height, bytes(maximum_channels))},
        )

        roi_channels = bytearray(reference_image.rgb)
        for x in (10, 11):
            roi_channels[(10 * width + x) * 3] = 129
        replace_candidate_images_and_expect_rejected(
            "ROI p99 limit",
            {first_candidate_path: BmpImage(width, height, bytes(roi_channels))},
        )

        temporal_a = bytearray(reference_image.rgb)
        temporal_b = bytearray(reference_image.rgb)
        temporal_offset = (10 * width + 10) * 3
        temporal_a[temporal_offset] = 130
        temporal_b[temporal_offset] = 126
        replace_candidate_images_and_expect_rejected(
            "temporal maximum limit",
            {
                first_candidate_path: BmpImage(width, height, bytes(temporal_a)),
                second_candidate_path: BmpImage(width, height, bytes(temporal_b)),
            },
        )

        final_report = evaluate_manifest(
            manifest_path,
            None,
            expected_dimensions=(width, height),
        )
        if not final_report["accepted"]:
            raise AssertionError("self-test did not restore its accepted matrix")


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare schema-2 adaptive-MSAA BMP receipts with authenticated Reference captures."
        )
    )
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--heatmap-directory", type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
    if arguments.self_test:
        self_test()
        print("Adaptive MSAA image-diff self-test passed.")
        return 0
    if arguments.manifest is None or arguments.report is None:
        raise ValidationError("--manifest and --report are required")
    report = evaluate_manifest(
        arguments.manifest.resolve(),
        arguments.heatmap_directory.resolve()
        if arguments.heatmap_directory
        else None,
    )
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        f"Adaptive MSAA image diff: {'accepted' if report['accepted'] else 'rejected'}; "
        f"report={arguments.report}"
    )
    return 0 if report["accepted"] else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValidationError as exception:
        print(f"Adaptive MSAA image diff error: {exception}", file=sys.stderr)
        raise SystemExit(1)
