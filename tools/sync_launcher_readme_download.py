#!/usr/bin/env python3
"""Validate or update UVSR's marked launcher download block."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_README = ROOT / "README.md"
DEFAULT_CANONICAL_FEED = ROOT / "launcher" / "launcher-feed-v1.json"
DEFAULT_LEGACY_FEED = ROOT / "installer" / "launcher-feed-v1.json"
DEFAULT_RENDERER_CONTRACT = ROOT / "cmake" / "uvsr-launcher-build-contract-v1.json"
START_MARKER = "<!-- uvsr-launcher-download:start -->"
END_MARKER = "<!-- uvsr-launcher-download:end -->"
UNAVAILABLE_MESSAGE = "> UVSR Launcher download is temporarily unavailable."
ARTIFACT_NAME = "UVSR-Launcher-Windows-11-x64.exe"
RELEASE_PREFIX = (
    "https://github.com/brockliddicoat/uvsr/releases/download/uvsr-launcher-v"
)
SIGNED_LINK_LABEL_PREFIX = "Download UVSR Launcher v"
UNSIGNED_LINK_LABEL_PREFIX = "Download Unsigned UVSR Launcher v"
UNSIGNED_LINK_LABEL_SUFFIX = " (unsigned manual bootstrap)"
UNSIGNED_CHECKSUM_LABEL = "SHA-256 checksum"
UNSIGNED_DISCLOSURE = (
    "> Not Authenticode-signed. Windows may warn. Launcher self-updates are disabled."
)
STATE_UNAVAILABLE = "unavailable"
STATE_UNSIGNED = "unsigned"
STATE_SIGNED = "signed"
STATE_KINDS = {STATE_UNAVAILABLE, STATE_UNSIGNED, STATE_SIGNED}
LATEST_ROUTE = f"/releases/latest/download/{ARTIFACT_NAME}".encode("ascii")
LATEST_ALIAS_ROUTE = (
    f"/releases/download/uvsr-launcher-latest/{ARTIFACT_NAME}"
).encode("ascii")
LAUNCHER_ARTIFACT_PATTERN = re.compile(
    re.escape(ARTIFACT_NAME.encode("ascii")), re.IGNORECASE
)
VERSION_PATTERN = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
PRODUCT_ID = "0c47a7a8-1ec4-4ffd-b6c4-2f7614181223"
MAXIMUM_RELEASE_SEQUENCE = 9_007_199_254_740_991
MAXIMUM_LAUNCHER_BYTES = 256 * 1024 * 1024
MAXIMUM_LAUNCHER_FEED_BYTES = 16 * 1024
MAXIMUM_RENDERER_CONTRACT_BYTES = 16 * 1024
MAXIMUM_VERSION_COMPONENT = 2_147_483_647
CONTRACT_ID_PATTERN = re.compile(r"[A-Za-z0-9-]{1,96}")
DXC_DATE_PATTERN = re.compile(r"[0-9]{4}_[0-9]{2}_[0-9]{2}")


class SyncError(RuntimeError):
    """Raised when the launcher download block is unsafe or noncanonical."""


class DownloadState:
    def __init__(self, kind: str, version: str | None) -> None:
        if kind not in STATE_KINDS:
            raise SyncError(f"invalid launcher download state {kind!r}")
        if (kind == STATE_UNAVAILABLE) != (version is None):
            raise SyncError("launcher download state and version are inconsistent")
        if version is not None:
            validate_version(version)
        self.kind = kind
        self.version = version

    @property
    def is_available(self) -> bool:
        return self.kind != STATE_UNAVAILABLE


class ParsedReadme:
    def __init__(
        self,
        data: bytes,
        block_start: int,
        block_end: int,
        newline: bytes,
        state: DownloadState,
    ) -> None:
        self.data = data
        self.block_start = block_start
        self.block_end = block_end
        self.newline = newline
        self.state = state


def validate_version(version: str) -> str:
    if VERSION_PATTERN.fullmatch(version) is None or any(
        int(component) > MAXIMUM_VERSION_COMPONENT for component in version.split(".")
    ):
        raise SyncError(
            f"invalid launcher version {version!r}; expected X.Y.Z without leading zeros"
        )
    return version


def release_url(version: str) -> str:
    validate_version(version)
    return f"{RELEASE_PREFIX}{version}/{ARTIFACT_NAME}"


def published_link(version: str) -> str:
    return f"> [{SIGNED_LINK_LABEL_PREFIX}{version}]({release_url(version)})"


def unsigned_link(version: str) -> str:
    return (
        f"> [{UNSIGNED_LINK_LABEL_PREFIX}{version}{UNSIGNED_LINK_LABEL_SUFFIX}]"
        f"({release_url(version)})"
    )


def unsigned_checksum_link(version: str) -> str:
    return f"> [{UNSIGNED_CHECKSUM_LABEL}]({release_url(version)}.sha256)"


def render_block(kind: str, version: str | None, newline: bytes) -> bytes:
    if newline not in (b"\n", b"\r\n"):
        raise SyncError("README uses an unsupported launcher block newline style")
    state = DownloadState(kind, version)
    if state.kind == STATE_UNAVAILABLE:
        lines = (START_MARKER, UNAVAILABLE_MESSAGE, END_MARKER)
    elif state.kind == STATE_SIGNED:
        lines = (START_MARKER, published_link(state.version), END_MARKER)
    else:
        lines = (
            START_MARKER,
            unsigned_link(state.version),
            ">",
            unsigned_checksum_link(state.version),
            ">",
            UNSIGNED_DISCLOSURE,
            END_MARKER,
        )
    return newline.join(part.encode("ascii") for part in lines)


def marker_line_bounds(data: bytes, marker: bytes, position: int) -> tuple[int, int]:
    line_start = data.rfind(b"\n", 0, position) + 1
    newline_position = data.find(b"\n", position + len(marker))
    line_end = len(data) if newline_position < 0 else newline_position
    content_end = line_end - 1 if data[line_end - 1 : line_end] == b"\r" else line_end
    if data[line_start:content_end] != marker:
        raise SyncError(f"README marker must occupy its exact line: {marker.decode('ascii')}")
    return line_start, content_end


def block_newline(data: bytes, start_content_end: int) -> bytes:
    if data[start_content_end : start_content_end + 2] == b"\r\n":
        return b"\r\n"
    if data[start_content_end : start_content_end + 1] == b"\n":
        return b"\n"
    raise SyncError("README start marker must be followed by a newline")


def validate_launcher_artifact_ownership(
    data: bytes, block_start: int, block_end: int, expected_count: int
) -> None:
    matches = list(LAUNCHER_ARTIFACT_PATTERN.finditer(data))
    if len(matches) != expected_count:
        raise SyncError(
            "README launcher executable references must be owned only by the "
            "generated download block"
        )
    for match in matches:
        if match.start() < block_start or match.end() > block_end:
            raise SyncError(
                "README contains a launcher executable reference outside its generated block"
            )


def reject_duplicate_json_properties(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for name, value in pairs:
        if name in result:
            raise SyncError(f"launcher feed contains duplicate property {name!r}")
        result[name] = value
    return result


def require_exact_keys(
    value: object, expected: set[str], description: str
) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != expected:
        raise SyncError(f"launcher feed {description} properties are not canonical")
    return value


def parse_feed(data: bytes) -> dict[str, object]:
    if not 1 <= len(data) <= MAXIMUM_LAUNCHER_FEED_BYTES:
        raise SyncError("launcher feed exceeds its strict size limit")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=reject_duplicate_json_properties)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SyncError(f"launcher feed is not strict UTF-8 JSON: {error}") from error

    feed = require_exact_keys(
        value,
        {
            "schemaVersion",
            "productId",
            "channel",
            "releaseSequence",
            "version",
            "artifact",
        },
        "root",
    )
    artifact = require_exact_keys(
        feed["artifact"], {"name", "size", "sha256"}, "artifact"
    )
    sequence = feed["releaseSequence"]
    size = artifact["size"]
    version = feed["version"]
    if not isinstance(version, str):
        raise SyncError("launcher feed version is not a string")
    validate_version(version)
    if (
        type(feed["schemaVersion"]) is not int
        or feed["schemaVersion"] != 1
        or feed["productId"] != PRODUCT_ID
        or feed["channel"] != "stable"
        or type(sequence) is not int
        or not 1 <= sequence <= MAXIMUM_RELEASE_SEQUENCE
        or artifact["name"] != ARTIFACT_NAME
        or type(size) is not int
        or not 1 <= size <= MAXIMUM_LAUNCHER_BYTES
        or not isinstance(artifact["sha256"], str)
        or SHA256_PATTERN.fullmatch(artifact["sha256"]) is None
    ):
        raise SyncError("launcher feed release identity is not canonical")
    return feed


def read_feed(canonical_path: Path, legacy_path: Path) -> dict[str, object]:
    for path in (canonical_path, legacy_path):
        if path.is_symlink():
            raise SyncError(f"refusing to use a symlink as a launcher feed: {path}")
    try:
        canonical = canonical_path.read_bytes()
        legacy = legacy_path.read_bytes()
    except OSError as error:
        raise SyncError(f"could not read launcher feed: {error}") from error
    if canonical != legacy:
        raise SyncError("canonical and legacy launcher feeds are not byte-identical")
    return parse_feed(canonical)


def parse_renderer_contract(data: bytes) -> dict[str, object]:
    if not 1 <= len(data) <= MAXIMUM_RENDERER_CONTRACT_BYTES:
        raise SyncError("renderer build contract exceeds its strict size limit")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=reject_duplicate_json_properties)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SyncError(
            f"renderer build contract is not strict UTF-8 JSON: {error}"
        ) from error

    contract = require_exact_keys(
        value,
        {
            "schemaVersion",
            "productId",
            "contractId",
            "minimumLauncherReleaseSequence",
            "d3d12AgilitySdkVersion",
            "directXHeadersVersion",
            "dxcVersion",
            "dxcDate",
        },
        "renderer contract",
    )
    minimum_sequence = contract["minimumLauncherReleaseSequence"]
    version_fields = (
        contract["d3d12AgilitySdkVersion"],
        contract["directXHeadersVersion"],
        contract["dxcVersion"],
    )
    if not all(isinstance(version, str) for version in version_fields):
        raise SyncError("renderer build contract dependency version is not a string")
    for version in version_fields:
        validate_version(version)
    if (
        type(contract["schemaVersion"]) is not int
        or contract["schemaVersion"] != 1
        or contract["productId"] != PRODUCT_ID
        or not isinstance(contract["contractId"], str)
        or CONTRACT_ID_PATTERN.fullmatch(contract["contractId"]) is None
        or type(minimum_sequence) is not int
        or not 1 <= minimum_sequence <= MAXIMUM_RELEASE_SEQUENCE
        or not isinstance(contract["dxcDate"], str)
        or DXC_DATE_PATTERN.fullmatch(contract["dxcDate"]) is None
    ):
        raise SyncError("renderer build contract identity is not canonical")
    return contract


def read_renderer_contract(path: Path) -> dict[str, object]:
    if path.is_symlink():
        raise SyncError(f"refusing to use a symlink as a renderer build contract: {path}")
    try:
        data = path.read_bytes()
    except OSError as error:
        raise SyncError(f"could not read renderer build contract: {error}") from error
    return parse_renderer_contract(data)


def parse_readme(data: bytes) -> ParsedReadme:
    if LATEST_ROUTE in data or LATEST_ALIAS_ROUTE in data:
        raise SyncError(
            "README contains a mutable latest launcher download route"
        )

    start_marker = START_MARKER.encode("ascii")
    end_marker = END_MARKER.encode("ascii")
    start_count = data.count(start_marker)
    end_count = data.count(end_marker)
    if start_count != 1 or end_count != 1:
        raise SyncError(
            "README must contain exactly one launcher download start marker and one end marker"
        )

    start_position = data.find(start_marker)
    end_position = data.find(end_marker)
    if end_position < start_position:
        raise SyncError("README launcher download markers are reversed")

    block_start, start_content_end = marker_line_bounds(
        data, start_marker, start_position
    )
    _, block_end = marker_line_bounds(data, end_marker, end_position)
    newline = block_newline(data, start_content_end)
    block = data[block_start:block_end]

    unavailable = render_block(STATE_UNAVAILABLE, None, newline)
    if block == unavailable:
        validate_launcher_artifact_ownership(data, block_start, block_end, 0)
        return ParsedReadme(
            data,
            block_start,
            block_end,
            newline,
            DownloadState(STATE_UNAVAILABLE, None),
        )

    try:
        block_text = block.decode("ascii")
        normalized = block_text.replace("\r\n", "\n")
    except UnicodeDecodeError as error:
        raise SyncError("README launcher download block must contain only ASCII") from error

    signed_pattern = re.compile(
        re.escape(START_MARKER)
        + r"\n> \["
        + re.escape(SIGNED_LINK_LABEL_PREFIX)
        + r"(?P<label_version>[^\]\r\n]+)"
        + r"\]\("
        + re.escape(RELEASE_PREFIX)
        + r"(?P<version>[^/\r\n]+)/"
        + re.escape(ARTIFACT_NAME)
        + r"\)\n"
        + re.escape(END_MARKER)
    )
    unsigned_pattern = re.compile(
        re.escape(START_MARKER)
        + r"\n> \["
        + re.escape(UNSIGNED_LINK_LABEL_PREFIX)
        + r"(?P<label_version>[^\]\r\n]+)"
        + re.escape(UNSIGNED_LINK_LABEL_SUFFIX)
        + r"\]\("
        + re.escape(RELEASE_PREFIX)
        + r"(?P<version>[^/\r\n]+)/"
        + re.escape(ARTIFACT_NAME)
        + r"\)\n>\n> \["
        + re.escape(UNSIGNED_CHECKSUM_LABEL)
        + r"\]\("
        + re.escape(RELEASE_PREFIX)
        + r"(?P<checksum_version>[^/\r\n]+)/"
        + re.escape(ARTIFACT_NAME)
        + r"\.sha256\)\n>\n"
        + re.escape(UNSIGNED_DISCLOSURE)
        + r"\n"
        + re.escape(END_MARKER)
    )
    kind = STATE_SIGNED
    match = signed_pattern.fullmatch(normalized)
    if match is None:
        kind = STATE_UNSIGNED
        match = unsigned_pattern.fullmatch(normalized)
    if match is None:
        raise SyncError("README launcher download block is noncanonical")
    version = validate_version(match.group("version"))
    label_version = validate_version(match.group("label_version"))
    if label_version != version:
        raise SyncError("README launcher download label and URL versions differ")
    if kind == STATE_UNSIGNED:
        checksum_version = validate_version(match.group("checksum_version"))
        if checksum_version != version:
            raise SyncError(
                "README launcher download and checksum URL versions differ"
            )
    if block != render_block(kind, version, newline):
        raise SyncError("README launcher download block is noncanonical")
    expected_references = 2 if kind == STATE_UNSIGNED else 1
    validate_launcher_artifact_ownership(
        data, block_start, block_end, expected_references
    )
    return ParsedReadme(
        data, block_start, block_end, newline, DownloadState(kind, version)
    )


def read_readme(path: Path) -> ParsedReadme:
    if path.is_symlink():
        raise SyncError(f"refusing to use a symlink as the README: {path}")
    try:
        data = path.read_bytes()
    except OSError as error:
        raise SyncError(f"could not read README {path}: {error}") from error
    return parse_readme(data)


def atomic_replace(path: Path, data: bytes) -> None:
    try:
        original_mode = stat.S_IMODE(path.stat().st_mode)
    except OSError as error:
        raise SyncError(f"could not inspect README {path}: {error}") from error

    file_descriptor = -1
    temporary_name: str | None = None
    try:
        file_descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
        )
        with os.fdopen(file_descriptor, "wb") as stream:
            file_descriptor = -1
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary_name, original_mode)
        os.replace(temporary_name, path)
        temporary_name = None
    except OSError as error:
        raise SyncError(f"could not atomically update README {path}: {error}") from error
    finally:
        if file_descriptor >= 0:
            os.close(file_descriptor)
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass


def set_state(path: Path, kind: str, version: str | None) -> bool:
    target = DownloadState(kind, version)
    parsed = read_readme(path)
    replacement = render_block(target.kind, target.version, parsed.newline)
    updated = (
        parsed.data[: parsed.block_start]
        + replacement
        + parsed.data[parsed.block_end :]
    )
    if updated == parsed.data:
        return False

    reparsed = parse_readme(updated)
    if (
        reparsed.state.kind != target.kind
        or reparsed.state.version != target.version
    ):
        raise SyncError("generated README launcher download state did not verify")
    if parsed.data[: parsed.block_start] != updated[: parsed.block_start]:
        raise SyncError("generated README changed bytes before the launcher block")
    suffix = parsed.data[parsed.block_end :]
    if not updated.endswith(suffix):
        raise SyncError("generated README changed bytes after the launcher block")
    atomic_replace(path, updated)
    return True


def expect_sync_error(action, expected: str) -> None:
    try:
        action()
    except SyncError as error:
        if expected.casefold() not in str(error).casefold():
            raise RuntimeError(
                f"expected error containing {expected!r}, received {str(error)!r}"
            ) from error
        return
    raise RuntimeError(f"expected sync error containing {expected!r}")


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="uvsr-launcher-readme-sync-") as temp:
        root = Path(temp)
        readme = root / "README.md"
        prefix = b"# UVSR\r\n\r\nBefore \xe2\x98\x83.\r\n\r\n"
        suffix = b"\r\n\r\nAfter.\r\n"
        readme.write_bytes(
            prefix
            + render_block(STATE_UNAVAILABLE, None, b"\r\n")
            + suffix
        )

        initial = read_readme(readme)
        if (
            initial.state.kind != STATE_UNAVAILABLE
            or initial.state.version is not None
            or initial.state.is_available
        ):
            raise RuntimeError("unavailable launcher fixture parsed as published")
        if readme.read_bytes() != initial.data:
            raise RuntimeError("launcher check changed the README")

        for kind, expected_block in (
            (
                STATE_SIGNED,
                render_block(STATE_SIGNED, "1.2.3", b"\r\n"),
            ),
            (
                STATE_UNSIGNED,
                render_block(STATE_UNSIGNED, "1.2.3", b"\r\n"),
            ),
        ):
            if not set_state(readme, kind, "1.2.3"):
                raise RuntimeError(f"{kind} launcher update reported no change")
            published = readme.read_bytes()
            if not published.startswith(prefix) or not published.endswith(suffix):
                raise RuntimeError(
                    f"{kind} launcher update changed bytes outside its block"
                )
            if expected_block not in published:
                raise RuntimeError(
                    f"{kind} launcher update lost CRLF block formatting"
                )
            state = read_readme(readme).state
            if (
                state.kind != kind
                or state.version != "1.2.3"
                or not state.is_available
            ):
                raise RuntimeError(f"{kind} launcher block did not pass exact check")
            if set_state(readme, kind, "1.2.3"):
                raise RuntimeError(f"identical {kind} update rewrote the README")

        unsigned = readme.read_bytes()
        for required_text in (
            b"(unsigned manual bootstrap)",
            b".exe.sha256)",
            b"Not Authenticode-signed.",
            b"Windows may warn.",
            b"Launcher self-updates are disabled.",
        ):
            if required_text not in unsigned:
                raise RuntimeError(
                    f"unsigned launcher disclosure lost {required_text!r}"
                )

        if not set_state(readme, STATE_UNAVAILABLE, None):
            raise RuntimeError("launcher unavailable update reported no change")
        if readme.read_bytes() != (
            prefix
            + render_block(STATE_UNAVAILABLE, None, b"\r\n")
            + suffix
        ):
            raise RuntimeError("launcher unavailable update was not canonical")

        expect_sync_error(lambda: DownloadState("unknown", None), "invalid")
        expect_sync_error(
            lambda: DownloadState(STATE_UNAVAILABLE, "1.2.3"), "inconsistent"
        )
        expect_sync_error(
            lambda: DownloadState(STATE_SIGNED, None), "inconsistent"
        )

        missing = b"# UVSR\n"
        expect_sync_error(lambda: parse_readme(missing), "exactly one")
        duplicate = (
            render_block(STATE_UNAVAILABLE, None, b"\n")
            + b"\n"
            + START_MARKER.encode("ascii")
            + b"\n"
        )
        expect_sync_error(lambda: parse_readme(duplicate), "exactly one")
        reversed_markers = (
            END_MARKER.encode("ascii")
            + b"\n"
            + START_MARKER.encode("ascii")
        )
        expect_sync_error(lambda: parse_readme(reversed_markers), "reversed")
        nested_markers = (
            START_MARKER.encode("ascii")
            + b"\n"
            + START_MARKER.encode("ascii")
            + b"\n"
            + END_MARKER.encode("ascii")
        )
        expect_sync_error(lambda: parse_readme(nested_markers), "exactly one")

        for malformed_version in (
            "1.2",
            "v1.2.3",
            "01.2.3",
            "1.02.3",
            "1.2.03",
            "2147483648.2.3",
        ):
            expect_sync_error(
                lambda value=malformed_version: validate_version(value), "invalid"
            )

        latest = b"\n".join(
            (
                START_MARKER.encode("ascii"),
                (
                    f"> [{SIGNED_LINK_LABEL_PREFIX}1.2.3](https://github.com/brockliddicoat/uvsr"
                    f"{LATEST_ROUTE.decode('ascii')})"
                ).encode("ascii"),
                END_MARKER.encode("ascii"),
            )
        )
        expect_sync_error(lambda: parse_readme(latest), "mutable latest")

        outside_versioned = (
            render_block(STATE_UNAVAILABLE, None, b"\n")
            + b"\n"
            + published_link("1.2.3").encode("ascii")
        )
        expect_sync_error(
            lambda: parse_readme(outside_versioned), "owned only"
        )

        outside_alias = (
            render_block(STATE_UNAVAILABLE, None, b"\n")
            + b"\nhttps://github.com/brockliddicoat/uvsr"
            + LATEST_ALIAS_ROUTE
        )
        expect_sync_error(lambda: parse_readme(outside_alias), "mutable latest")

        duplicate_versioned = (
            render_block(STATE_SIGNED, "1.2.3", b"\n")
            + b"\n"
            + published_link("1.2.3").encode("ascii")
        )
        expect_sync_error(
            lambda: parse_readme(duplicate_versioned), "owned only"
        )
        duplicate_unsigned = (
            render_block(STATE_UNSIGNED, "1.2.3", b"\n")
            + b"\n"
            + unsigned_checksum_link("1.2.3").encode("ascii")
        )
        expect_sync_error(
            lambda: parse_readme(duplicate_unsigned), "owned only"
        )

        for unsafe_destination in (
            "https://github.com/brockliddicoat/uvsr/releases/download/other-tag/"
            + ARTIFACT_NAME,
            RELEASE_PREFIX.replace("brockliddicoat", "BrockLiddicoat")
            + "1.2.3/"
            + ARTIFACT_NAME,
            "https://example.invalid/downloads/" + ARTIFACT_NAME,
        ):
            unsafe_outside = (
                render_block(STATE_UNAVAILABLE, None, b"\n")
                + f"\n> [Unsafe launcher]({unsafe_destination})".encode("ascii")
            )
            expect_sync_error(
                lambda value=unsafe_outside: parse_readme(value), "owned only"
            )

        noncanonical = render_block(STATE_UNAVAILABLE, None, b"\n").replace(
            UNAVAILABLE_MESSAGE.encode("ascii"),
            UNAVAILABLE_MESSAGE.encode("ascii") + b" ",
        )
        expect_sync_error(lambda: parse_readme(noncanonical), "noncanonical")

        mismatched_label = render_block(STATE_SIGNED, "1.2.3", b"\n").replace(
            b"Download UVSR Launcher v1.2.3",
            b"Download UVSR Launcher v1.2.4",
        )
        expect_sync_error(lambda: parse_readme(mismatched_label), "versions differ")

        mismatched_unsigned_label = render_block(
            STATE_UNSIGNED, "1.2.3", b"\n"
        ).replace(
            b"Download Unsigned UVSR Launcher v1.2.3",
            b"Download Unsigned UVSR Launcher v1.2.4",
        )
        expect_sync_error(
            lambda: parse_readme(mismatched_unsigned_label), "versions differ"
        )
        mismatched_checksum = render_block(
            STATE_UNSIGNED, "1.2.3", b"\n"
        ).replace(
            b"uvsr-launcher-v1.2.3/UVSR-Launcher-Windows-11-x64.exe.sha256",
            b"uvsr-launcher-v1.2.4/UVSR-Launcher-Windows-11-x64.exe.sha256",
        )
        expect_sync_error(
            lambda: parse_readme(mismatched_checksum), "checksum URL versions differ"
        )
        missing_disclosure = render_block(
            STATE_UNSIGNED, "1.2.3", b"\n"
        ).replace(UNSIGNED_DISCLOSURE.encode("ascii"), b"> Unsigned.")
        expect_sync_error(
            lambda: parse_readme(missing_disclosure), "noncanonical"
        )

        valid_feed = {
            "schemaVersion": 1,
            "productId": PRODUCT_ID,
            "channel": "stable",
            "releaseSequence": 7,
            "version": "1.2.3",
            "artifact": {
                "name": ARTIFACT_NAME,
                "size": 1234,
                "sha256": "a" * 64,
            },
        }
        feed_bytes = json.dumps(valid_feed, separators=(",", ":")).encode("utf-8")
        if parse_feed(feed_bytes)["version"] != "1.2.3":
            raise RuntimeError("valid launcher feed did not preserve its version")
        expect_sync_error(
            lambda: parse_feed(feed_bytes.replace(b'"channel"', b'"Channel"')),
            "properties",
        )
        duplicate_feed = feed_bytes.replace(
            b'"schemaVersion":1,', b'"schemaVersion":1,"schemaVersion":1,'
        )
        expect_sync_error(lambda: parse_feed(duplicate_feed), "duplicate")
        expect_sync_error(
            lambda: parse_feed(feed_bytes + b" " * MAXIMUM_LAUNCHER_FEED_BYTES),
            "size limit",
        )
        canonical_feed = root / "launcher-feed-v1.json"
        legacy_feed = root / "legacy-launcher-feed-v1.json"
        canonical_feed.write_bytes(feed_bytes)
        legacy_feed.write_bytes(feed_bytes)
        if read_feed(canonical_feed, legacy_feed)["releaseSequence"] != 7:
            raise RuntimeError("launcher feed mirror validation lost its sequence")
        legacy_feed.write_bytes(feed_bytes + b"\n")
        expect_sync_error(
            lambda: read_feed(canonical_feed, legacy_feed), "byte-identical"
        )

        valid_contract = {
            "schemaVersion": 1,
            "productId": PRODUCT_ID,
            "contractId": "uvsr-windows-dx12-stable-619-v1",
            "minimumLauncherReleaseSequence": 4,
            "d3d12AgilitySdkVersion": "1.619.5",
            "directXHeadersVersion": "1.619.5",
            "dxcVersion": "1.9.2602",
            "dxcDate": "2026_02_20",
        }
        contract_bytes = json.dumps(
            valid_contract, separators=(",", ":")
        ).encode("utf-8")
        parsed_contract = parse_renderer_contract(contract_bytes)
        if parsed_contract["minimumLauncherReleaseSequence"] != 4:
            raise RuntimeError("valid renderer contract lost its release sequence")
        expect_sync_error(
            lambda: parse_renderer_contract(
                contract_bytes.replace(b'"contractId"', b'"ContractId"')
            ),
            "properties",
        )
        duplicate_contract = contract_bytes.replace(
            b'"schemaVersion":1,', b'"schemaVersion":1,"schemaVersion":1,'
        )
        expect_sync_error(
            lambda: parse_renderer_contract(duplicate_contract), "duplicate"
        )
        expect_sync_error(
            lambda: parse_renderer_contract(
                contract_bytes.replace(
                    b'"minimumLauncherReleaseSequence":4',
                    b'"minimumLauncherReleaseSequence":0',
                )
            ),
            "identity",
        )
        expect_sync_error(
            lambda: parse_renderer_contract(
                contract_bytes.replace(b'"dxcDate":"2026_02_20"', b'"dxcDate":"latest"')
            ),
            "identity",
        )
        contract_path = root / "uvsr-launcher-build-contract-v1.json"
        contract_path.write_bytes(contract_bytes)
        if read_renderer_contract(contract_path)["dxcVersion"] != "1.9.2602":
            raise RuntimeError("renderer contract file validation lost its DXC version")

    print("Launcher README download synchronization self-test passed.")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate or update the marked UVSR Launcher README download block."
    )
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true")
    action.add_argument("--set-version", metavar="X.Y.Z")
    action.add_argument("--set-unsigned-version", metavar="X.Y.Z")
    action.add_argument("--set-from-feed", action="store_true")
    action.add_argument("--set-unavailable", action="store_true")
    action.add_argument("--self-test", action="store_true")
    action.add_argument("--print-feed-json", action="store_true")
    action.add_argument("--print-renderer-contract-json", action="store_true")
    action.add_argument("--print-state-json", action="store_true")
    parser.add_argument("--readme", type=Path, default=DEFAULT_README)
    parser.add_argument(
        "--canonical-feed", type=Path, default=DEFAULT_CANONICAL_FEED
    )
    parser.add_argument("--legacy-feed", type=Path, default=DEFAULT_LEGACY_FEED)
    parser.add_argument(
        "--renderer-contract", type=Path, default=DEFAULT_RENDERER_CONTRACT
    )
    arguments = parser.parse_args()

    try:
        if arguments.self_test:
            self_test()
            return 0

        if arguments.print_feed_json:
            feed = read_feed(arguments.canonical_feed, arguments.legacy_feed)
            print(json.dumps(feed, separators=(",", ":"), sort_keys=True))
            return 0

        if arguments.print_renderer_contract_json:
            contract = read_renderer_contract(arguments.renderer_contract)
            print(json.dumps(contract, separators=(",", ":"), sort_keys=True))
            return 0

        if arguments.print_state_json:
            state = read_readme(arguments.readme).state
            print(
                json.dumps(
                    {
                        "available": state.is_available,
                        "kind": state.kind,
                        "version": state.version,
                    },
                    separators=(",", ":"),
                    sort_keys=True,
                )
            )
            return 0

        readme = arguments.readme
        if arguments.check:
            state = read_readme(readme).state
            if state.is_available:
                print(
                    "README launcher download block is canonical for "
                    f"{state.kind} version {state.version}."
                )
            else:
                print("README launcher download block is canonically unavailable.")
            return 0

        if arguments.set_from_feed:
            feed = read_feed(arguments.canonical_feed, arguments.legacy_feed)
            version = str(feed["version"])
            kind = STATE_SIGNED
        elif arguments.set_unsigned_version is not None:
            version = arguments.set_unsigned_version
            kind = STATE_UNSIGNED
        elif arguments.set_version is not None:
            version = arguments.set_version
            kind = STATE_SIGNED
        else:
            version = None
            kind = STATE_UNAVAILABLE
        changed = set_state(readme, kind, version)
        state_text = kind if version is None else f"{kind} version {version}"
        verb = "Updated" if changed else "Verified"
        print(f"{verb} README launcher download block as {state_text}.")
        return 0
    except SyncError as error:
        print(f"Launcher README synchronization failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
