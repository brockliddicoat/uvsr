#!/usr/bin/env python3
"""Generate UVSR's deterministic, first-party precomputed R8 noise assets."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


ALGORITHM = "uvsr-spectral-stbn-v1"
RESOLUTIONS = (64, 128, 256, 512)
TEMPORAL_LAYERS = 64
BASE_SEED = 0x55565352


def _rank_encode(values: np.ndarray) -> np.ndarray:
    flat = values.reshape(-1)
    order = np.argsort(flat, kind="stable")
    ranks = np.empty(order.size, dtype=np.uint64)
    ranks[order] = np.arange(order.size, dtype=np.uint64)
    encoded = ((ranks * 256 + order.size // 2) // order.size).clip(0, 255)
    return encoded.astype(np.uint8).reshape(values.shape)


def _spectral_blue_2d(size: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    values = _rank_encode(rng.standard_normal((size, size))).astype(np.float64)
    frequency = np.fft.fftfreq(size)
    radius = np.hypot(frequency[:, None], frequency[None, :])
    target = np.power(radius, 1.35)
    target[0, 0] = 0.0
    for _ in range(10):
        spectrum = np.fft.fft2(values - values.mean())
        phase = spectrum / np.maximum(np.abs(spectrum), 1e-20)
        values = np.fft.ifft2(phase * target).real
        values = _rank_encode(values).astype(np.float64)
    return values.astype(np.uint8)


def _spectral_blue_1d(length: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    values = _rank_encode(rng.standard_normal(length)).astype(np.float64)
    target = np.power(np.abs(np.fft.fftfreq(length)), 1.35)
    target[0] = 0.0
    for _ in range(16):
        spectrum = np.fft.fft(values - values.mean())
        phase = spectrum / np.maximum(np.abs(spectrum), 1e-20)
        values = np.fft.ifft(phase * target).real
        values = _rank_encode(values).astype(np.float64)
    return values.astype(np.uint8)


def _spatial_white(size: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    ranks = rng.permutation(size * size).astype(np.uint64)
    encoded = ((ranks * 256 + (size * size) // 2) // (size * size)).clip(0, 255)
    return encoded.astype(np.uint8).reshape((size, size))


def _spatiotemporal_blue(size: int, seed: int) -> np.ndarray:
    # The published STBN objective asks for spatially blue XY slices and
    # temporally blue Z columns while leaving unrelated XY/Z pairs uncoupled.
    # This clean-room construction combines independent toroidal spectral-blue
    # rank fields with circularly shifted temporal rank sequences. Each fixed
    # pixel therefore sees the same temporal-blue sequence up to rotation,
    # while every slice retains high-frequency spatial structure.
    spatial = _spectral_blue_2d(size, seed ^ 0x212CA684)
    phase = _spectral_blue_2d(size, seed ^ 0xAA3D4B62) >> 2
    temporal = _spectral_blue_1d(
        TEMPORAL_LAYERS,
        seed ^ 0x785AD35E) >> 2
    layers = np.empty((TEMPORAL_LAYERS, size, size), dtype=np.uint8)
    for layer in range(TEMPORAL_LAYERS):
        temporal_rank = temporal[(layer + phase) & (TEMPORAL_LAYERS - 1)]
        layers[layer] = (
            spatial.astype(np.uint16) +
            temporal_rank.astype(np.uint16) * 4
        ).astype(np.uint8)
    return layers


def _write_asset(
    output_directory: Path,
    name: str,
    values: np.ndarray,
    pattern: str,
    resolution: int,
    layers: int,
) -> dict[str, object]:
    path = output_directory / name
    payload = np.ascontiguousarray(values, dtype=np.uint8).tobytes(order="C")
    path.write_bytes(payload)
    return {
        "file": name,
        "pattern": pattern,
        "width": resolution,
        "height": resolution,
        "layers": layers,
        "format": "R8_UNORM",
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def generate(output_directory: Path) -> None:
    output_directory.mkdir(parents=True, exist_ok=True)
    assets: list[dict[str, object]] = []
    for resolution in RESOLUTIONS:
        seed = BASE_SEED ^ resolution
        assets.append(_write_asset(
            output_directory,
            f"spatial-white-{resolution}x{resolution}x1-r8.bin",
            _spatial_white(resolution, seed ^ 0x4909EE03),
            "Spatial White",
            resolution,
            1,
        ))
        assets.append(_write_asset(
            output_directory,
            f"spatial-blue-{resolution}x{resolution}x1-r8.bin",
            _spectral_blue_2d(resolution, seed ^ 0xB1DA2D4E),
            "Spatial Blue",
            resolution,
            1,
        ))
        assets.append(_write_asset(
            output_directory,
            f"spatiotemporal-blue-{resolution}x{resolution}x64-r8.bin",
            _spatiotemporal_blue(resolution, seed),
            "Spatiotemporal Blue",
            resolution,
            TEMPORAL_LAYERS,
        ))

    manifest = {
        "algorithm": ALGORITHM,
        "seed": BASE_SEED,
        "source": "First-party clean-room spectral construction based on the published STBN spatial/temporal objective.",
        "assets": assets,
    }
    (output_directory / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "assets" / "noise",
    )
    args = parser.parse_args()
    generate(args.output.resolve())


if __name__ == "__main__":
    main()
