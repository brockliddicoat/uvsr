#!/usr/bin/env python3
"""Validate UVSR's legal folder, license text, and documentation index."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCUMENTATION_ROOT = ROOT / "legal" / "documentation"
NON_RECORD_DOCUMENTS = {
    "commercial-licensing.md",
    "contributor-agreement-privacy-notice.md",
    "third-party-notices.md",
}
REQUIRED_NOTICE = "Required Notice: UVSR | https://github.com/brockliddicoat/uvsr"
POLYFORM_BODY_SHA256 = (
    "c0ea4a896d2c8c394b29f9427589996db826cd501c512279ff0ed3ef48fabbe5"
)
MARKDOWN_LINK = re.compile(r"\[[^\]]+\]\(([^)]+)\)")


def fail(message: str) -> None:
    raise RuntimeError(message)


def normalized_bytes(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def check_license() -> None:
    data = normalized_bytes(ROOT / "LICENSE")
    try:
        notice, body = data.split(b"\n\n", 1)
    except ValueError as error:
        raise RuntimeError("LICENSE must contain a notice and license body") from error
    if notice.decode("utf-8") != REQUIRED_NOTICE:
        fail("LICENSE Required Notice differs from the approved attribution")
    digest = hashlib.sha256(body).hexdigest()
    if digest != POLYFORM_BODY_SHA256:
        fail(
            "Polyform Noncommercial 1.0.0 body was modified: "
            f"expected {POLYFORM_BODY_SHA256}, found {digest}"
        )


def tracked_paths() -> set[str]:
    output = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
    )
    return {
        value.decode("utf-8").replace("\\", "/")
        for value in output.split(b"\0")
        if value
    }


def check_layout(paths: set[str]) -> None:
    for path in paths:
        if path.startswith("third_party/") or path.startswith("src/third_party/"):
            fail(f"legacy third_party path remains: {path}")
        if path.startswith("legal/sources/") or path.startswith(
            "legal/code-samples/"
        ):
            fail(f"superseded legal path remains: {path}")
    for required in (
        "legal/README.md",
        "legal/licenses/README.md",
        "legal/samples/README.md",
        "legal/documentation/README.md",
        "legal/documentation/commercial-licensing.md",
        "legal/documentation/contributor-agreement-privacy-notice.md",
        "legal/documentation/third-party-notices.md",
    ):
        if required not in paths:
            fail(f"required legal entry is missing: {required}")


def check_documentation_index() -> None:
    index_path = DOCUMENTATION_ROOT / "README.md"
    index = index_path.read_text(encoding="utf-8")
    records = {
        path.name
        for path in DOCUMENTATION_ROOT.glob("*.md")
        if path.name.casefold() != "readme.md"
        and path.name.casefold() not in NON_RECORD_DOCUMENTS
    }
    linked = set()
    for target in MARKDOWN_LINK.findall(index):
        clean = target.split("#", 1)[0]
        if not clean or "://" in clean or clean.startswith("mailto:"):
            continue
        resolved = (DOCUMENTATION_ROOT / clean).resolve()
        try:
            relative = resolved.relative_to(DOCUMENTATION_ROOT.resolve())
        except ValueError:
            continue
        if len(relative.parts) == 1 and relative.suffix.casefold() == ".md":
            linked.add(relative.name)
            if not resolved.is_file():
                fail(f"documentation index contains a broken link: {clean}")
    missing = sorted(records - linked, key=str.casefold)
    if missing:
        fail("source records missing from documentation index: " + ", ".join(missing))


def check_metadata() -> None:
    path = ROOT / "legal" / "documentation" / "cla-assistant-metadata.json"
    metadata = json.loads(path.read_text(encoding="utf-8"))
    expected = {"name", "email", "ownership", "agreement"}
    if set(metadata) != expected:
        fail("CLA Assistant metadata fields differ from the approved schema")
    if any(not metadata[key].get("required") for key in expected):
        fail("every CLA Assistant metadata field must remain required")


def main() -> int:
    try:
        check_license()
        paths = tracked_paths()
        check_layout(paths)
        check_documentation_index()
        check_metadata()
    except (OSError, RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"Legal inventory check failed: {error}", file=sys.stderr)
        return 1
    print("Legal inventory check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
