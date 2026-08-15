#!/usr/bin/env python3
"""Decode a UVSR settings snapshot code through its local snapshot catalog."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import sys


CODE_PATTERN = re.compile(r"[0-9a-f]{32}")
REGISTRY_ENTRY_PATTERN = re.compile(
    r"UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION\("
    r"0x([0-9a-fA-F]{4})u\s*,\s*"
    r"0x([0-9a-fA-F]{16})ull\s*,\s*"
    r"0x([0-9a-fA-F]{16})ull\s*\)"
)
MASK_64 = (1 << 64) - 1
REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_REGISTRY_PATH = (
    REPOSITORY_ROOT / "src" / "settings_snapshot_schema_versions.def"
)


def load_registered_versions(path: Path = DEFAULT_REGISTRY_PATH) -> set[str]:
    try:
        registry = path.read_text(encoding="utf-8")
    except FileNotFoundError as error:
        raise ValueError(f"Settings schema registry not found: {path}.") from error
    entries: list[tuple[str, str]] = []
    for line_number, line in enumerate(registry.splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith("//"):
            continue
        match = REGISTRY_ENTRY_PATTERN.fullmatch(stripped)
        if match is None:
            raise ValueError(
                "Settings schema registry contains a malformed row at "
                f"{path}:{line_number}."
            )
        entries.append(
            (
                match.group(1).lower(),
                (match.group(2) + match.group(3)).lower(),
            )
        )
    if not entries:
        raise ValueError(f"Settings schema registry has no entries: {path}.")
    versions = [entry[0] for entry in entries]
    fingerprints = [entry[1] for entry in entries]
    if any(int(version, 16) < 2 for version in versions):
        raise ValueError(
            f"Settings schema registry contains a reserved version: {path}."
        )
    if any(int(fingerprint, 16) == 0 for fingerprint in fingerprints):
        raise ValueError(
            f"Settings schema registry contains an empty fingerprint: {path}."
        )
    if len(set(versions)) != len(versions):
        raise ValueError(
            f"Settings schema registry contains a duplicate version: {path}."
        )
    if len(set(fingerprints)) != len(fingerprints):
        raise ValueError(
            f"Settings schema registry contains a duplicate fingerprint: {path}."
        )
    return set(versions)


def build_snapshot_code(canonical: str, version: str) -> str:
    if re.fullmatch(r"[0-9a-f]{4}", version) is None or int(version, 16) < 2:
        raise ValueError(
            "Snapshot version must be four lowercase hex digits at or above 0002."
        )
    primary = 14695981039346656037
    secondary = 7809847782465536322
    for byte in canonical.encode("utf-8"):
        primary ^= byte
        primary = (primary * 1099511628211) & MASK_64
        secondary ^= byte ^ 0xA5
        secondary = (secondary * 14029467366897019727) & MASK_64
    payload = primary.to_bytes(8, "big") + secondary.to_bytes(8, "big")[:6]
    return version + payload.hex()


def default_catalog_paths(version: str) -> list[Path]:
    local_app_data = os.environ.get("LOCALAPPDATA")
    if not local_app_data:
        raise ValueError(
            "LOCALAPPDATA is unavailable; pass --catalog with the catalog path."
        )
    local_root = Path(local_app_data)
    catalog_name = f"settings-snapshots-v{version}.txt"
    catalogs = [local_root / "UVSR" / catalog_name]
    packages_root = local_root / "Packages"
    try:
        package_directories = sorted(packages_root.iterdir())
    except FileNotFoundError:
        return catalogs
    for package_directory in package_directories:
        candidate = (
            package_directory
            / "LocalCache"
            / "Local"
            / "UVSR"
            / catalog_name
        )
        if candidate.is_file():
            catalogs.append(candidate)
    return catalogs


def read_matching_snapshots(path: Path, code: str) -> list[str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError as error:
        raise ValueError(
            f"Snapshot catalog not found: {path}. Copy the code in UVSR first."
        ) from error

    opening = f"[{code}]"
    closing = f"[/{code}]"
    snapshots: list[str] = []
    index = 0
    while index < len(lines):
        if lines[index] != opening:
            index += 1
            continue
        index += 1
        block: list[str] = []
        while index < len(lines) and lines[index] != closing:
            block.append(lines[index])
            index += 1
        if index >= len(lines):
            raise ValueError(f"Snapshot catalog has an unterminated {code} entry.")
        snapshots.append("\n".join(block) + "\n")
        index += 1
    return snapshots


def unescape_value(value: str) -> str:
    output: list[str] = []
    index = 0
    while index < len(value):
        character = value[index]
        if character != "\\":
            output.append(character)
            index += 1
            continue
        index += 1
        if index >= len(value):
            raise ValueError("Snapshot contains a trailing escape character.")
        escaped = value[index]
        replacements = {"\\": "\\", "n": "\n", "r": "\r", "t": "\t"}
        if escaped not in replacements:
            raise ValueError(f"Snapshot contains an unknown \\{escaped} escape.")
        output.append(replacements[escaped])
        index += 1
    return "".join(output)


def parse_settings(canonical: str) -> dict[str, str]:
    settings: dict[str, str] = {}
    for line in canonical.splitlines():
        name, separator, escaped_value = line.partition("=")
        if not separator or not name or name in settings:
            raise ValueError(f"Snapshot contains an invalid setting line: {line!r}")
        settings[name] = unescape_value(escaped_value)
    return settings


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Resolve a 32-character UVSR settings code from the snapshot "
            "catalog written when the code was copied."
        )
    )
    parser.add_argument("code", help="32-character lowercase UVSR settings code")
    parser.add_argument(
        "--catalog",
        type=Path,
        help=(
            "catalog path; by default searches %%LOCALAPPDATA%%/UVSR and "
            "package-local UVSR catalogs"
        ),
    )
    parser.add_argument(
        "--registry",
        type=Path,
        default=DEFAULT_REGISTRY_PATH,
        help=(
            "schema registry path; defaults to the registry beside this "
            "repository tool"
        ),
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="write a JSON object instead of name=value lines",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if CODE_PATTERN.fullmatch(arguments.code) is None:
        print(
            "error: expected 32 lowercase hexadecimal characters",
            file=sys.stderr,
        )
        return 2

    try:
        version = arguments.code[:4]
        registered_versions = load_registered_versions(arguments.registry)
        if version not in registered_versions:
            raise ValueError(
                f"Snapshot schema version {version} is not registered in "
                f"{arguments.registry}."
            )
        if arguments.catalog:
            catalogs = [arguments.catalog]
            snapshots = read_matching_snapshots(
                arguments.catalog, arguments.code
            )
            existing_catalogs = catalogs
        else:
            catalogs = default_catalog_paths(version)
            existing_catalogs = [path for path in catalogs if path.is_file()]
            if not existing_catalogs:
                raise ValueError(
                    "Snapshot catalog not found in any default UVSR location. "
                    "Copy the code in UVSR first."
                )
            snapshots = []
            for catalog in existing_catalogs:
                snapshots.extend(
                    read_matching_snapshots(catalog, arguments.code)
                )
        if not snapshots:
            raise ValueError(
                f"Unknown settings snapshot {arguments.code} in the searched "
                f"catalogs: {', '.join(map(str, existing_catalogs))}."
            )
        unique_snapshots = list(dict.fromkeys(snapshots))
        if len(unique_snapshots) != 1:
            raise ValueError(
                f"Fingerprint collision for {arguments.code}; the catalog "
                "contains multiple distinct snapshots."
            )
        canonical = unique_snapshots[0]
        if build_snapshot_code(canonical, version) != arguments.code:
            raise ValueError(
                f"Snapshot {arguments.code} failed its fingerprint check."
            )
        settings = parse_settings(canonical)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if arguments.json:
        print(json.dumps(settings, indent=2, sort_keys=True))
    else:
        for name, value in settings.items():
            print(f"{name}={value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
