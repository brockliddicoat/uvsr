#!/usr/bin/env python3
"""Generate and validate UVSR's README codebase-size banner."""

from __future__ import annotations

import argparse
import difflib
import os
from dataclasses import dataclass
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
README = ROOT / "README.md"
START_MARKER = "<!-- uvsr-codebase-size:start -->"
END_MARKER = "<!-- uvsr-codebase-size:end -->"
TAGLINE = "**Unified Visibility Stochastic Rendering**"

FIRST_PARTY_SUFFIXES = {
    ".bat",
    ".c",
    ".cfg",
    ".cmake",
    ".cmd",
    ".cpp",
    ".frag",
    ".h",
    ".hlsl",
    ".hlsli",
    ".json",
    ".mjs",
    ".ps1",
    ".py",
    ".rc",
    ".sh",
    ".vert",
}
FIRST_PARTY_BASENAMES = {"CMakeLists.txt", "Makefile"}

# This is the historical UVSR third-party source boundary. It intentionally
# excludes documentation, licenses, assets, IDE metadata, and generated files.
THIRD_PARTY_SUFFIXES = {
    ".bat",
    ".c",
    ".cmake",
    ".cpp",
    ".frag",
    ".h",
    ".hlsl",
    ".hlsli",
    ".m",
    ".mm",
    ".py",
    ".rc",
    ".sh",
    ".vert",
}
THIRD_PARTY_BASENAMES = {"CMakeLists.txt", "Makefile"}


@dataclass(frozen=True)
class OverrideGroup:
    source_root: Path
    patches: tuple[Path, ...]


OVERRIDE_GROUPS = (
    OverrideGroup(
        ROOT / "donut",
        (ROOT / "overrides" / "donut-engine.patch",),
    ),
    OverrideGroup(
        ROOT / "donut",
        (ROOT / "overrides" / "donut-app.patch",),
    ),
    OverrideGroup(
        ROOT / "donut" / "thirdparty" / "imgui",
        (
            ROOT / "overrides" / "imgui-ui.patch",
            ROOT / "overrides" / "imgui-dropdown-roll.patch",
        ),
    ),
)


@dataclass(frozen=True)
class LineCounts:
    first_party: int
    third_party: int

    @property
    def total(self) -> int:
        return self.first_party + self.third_party


def run(command: list[str], cwd: Path = ROOT) -> bytes:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"{' '.join(command)} failed: {detail}")
    return result.stdout


def non_blank_source_lines(path: Path) -> int:
    data = path.read_bytes()
    if b"\0" in data:
        raise RuntimeError(f"binary file entered the source count: {path}")
    return sum(bool(line.strip()) for line in data.splitlines())


def root_owned_files() -> list[Path]:
    output = run(
        [
            "git",
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
            "--",
            "src",
            "tests",
            "tools",
            "CMakeLists.txt",
            "LaunchUVSR.cmd",
        ]
    )
    files: list[Path] = []
    for encoded in output.split(b"\0"):
        if not encoded:
            continue
        relative = encoded.decode("utf-8")
        if relative == "src/third_party" or relative.startswith(
            "src/third_party/"
        ):
            continue
        path = ROOT / relative
        if path.is_file() and (
            path.name in FIRST_PARTY_BASENAMES
            or path.suffix.lower() in FIRST_PARTY_SUFFIXES
        ):
            files.append(path)
    return sorted(set(files))


def is_third_party_source(path: Path) -> bool:
    return (
        path.name in THIRD_PARTY_BASENAMES
        or path.suffix.lower() in THIRD_PARTY_SUFFIXES
    )


def require_pristine_dependencies() -> None:
    status = run(
        [
            "git",
            "status",
            "--porcelain",
            "--untracked-files=all",
            "--ignore-submodules=none",
        ],
        ROOT / "donut",
    ).decode("utf-8", errors="replace")
    if status.strip():
        raise RuntimeError(
            "donut/ or a nested dependency is dirty; UVSR overrides must be "
            "tracked under overrides/ before line counts can be generated"
        )


def pristine_third_party_files() -> list[Path]:
    require_pristine_dependencies()
    output = run(
        ["git", "ls-files", "-z", "--recurse-submodules"],
        ROOT / "donut",
    )
    files: list[Path] = []
    for encoded in output.split(b"\0"):
        if not encoded:
            continue
        path = ROOT / "donut" / encoded.decode("utf-8")
        if is_third_party_source(path):
            if not path.is_file():
                raise RuntimeError(
                    f"missing dependency source {path}; run "
                    "git submodule update --init --recursive"
                )
            files.append(path)

    vendor_root = ROOT / "src" / "third_party"
    if vendor_root.exists():
        output = run(
            [
                "git",
                "ls-files",
                "-z",
                "--cached",
                "--others",
                "--exclude-standard",
                "--",
                "src/third_party",
            ]
        )
        for encoded in output.split(b"\0"):
            if not encoded:
                continue
            path = ROOT / encoded.decode("utf-8")
            if path.is_file() and is_third_party_source(path):
                files.append(path)
    return sorted(set(files))


def validate_override_inventory() -> None:
    listed = [patch.resolve() for group in OVERRIDE_GROUPS for patch in group.patches]
    if len(listed) != len(set(listed)):
        raise RuntimeError("a dependency override patch is counted more than once")

    discovered = {
        patch.resolve() for patch in (ROOT / "overrides").glob("*.patch")
    }
    if set(listed) != discovered:
        missing = sorted(discovered - set(listed))
        stale = sorted(set(listed) - discovered)
        detail: list[str] = []
        if missing:
            detail.append(
                "not counted: " + ", ".join(path.name for path in missing)
            )
        if stale:
            detail.append(
                "missing: " + ", ".join(path.name for path in stale)
            )
        raise RuntimeError("dependency override inventory mismatch (" + "; ".join(detail) + ")")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    for patch in listed:
        build_reference = f"overrides/{patch.name}"
        if cmake.count(build_reference) != 1:
            raise RuntimeError(
                f"{patch.name} must have exactly one active CMake reference"
            )


def patch_paths(patch: Path) -> list[Path]:
    paths: list[Path] = []
    pattern = re.compile(r"^diff --git a/(.+) b/(.+)$")
    for line in patch.read_text(encoding="utf-8-sig").splitlines():
        match = pattern.match(line)
        if match:
            if match.group(1) != match.group(2):
                raise RuntimeError(f"renamed override paths are unsupported: {line}")
            paths.append(Path(match.group(2)))
    if not paths:
        raise RuntimeError(f"override contains no file diffs: {patch}")
    return paths


def diff_ownership(before: list[bytes], after: list[bytes]) -> tuple[int, int]:
    additions = 0
    deletions = 0
    matcher = difflib.SequenceMatcher(None, before, after, autojunk=False)
    for operation, before_start, before_end, after_start, after_end in (
        matcher.get_opcodes()
    ):
        if operation == "equal":
            continue
        deletions += sum(
            bool(line.strip()) for line in before[before_start:before_end]
        )
        additions += sum(
            bool(line.strip()) for line in after[after_start:after_end]
        )
    return additions, deletions


def write_normalized_patch(source: Path, destination: Path) -> None:
    text = source.read_bytes().decode("utf-8-sig")
    destination.write_bytes(
        text.replace("\r\n", "\n").replace("\r", "\n").encode("utf-8")
    )


def override_ownership() -> tuple[int, int]:
    validate_override_inventory()
    additions = 0
    deletions = 0
    for group in OVERRIDE_GROUPS:
        relative_paths: set[Path] = set()
        for patch in group.patches:
            relative_paths.update(patch_paths(patch))
        unsupported = [
            path for path in relative_paths if not is_third_party_source(path)
        ]
        if unsupported:
            raise RuntimeError(
                "override source boundary is undefined for: "
                + ", ".join(str(path) for path in sorted(unsupported))
            )

        with tempfile.TemporaryDirectory(prefix="uvsr-line-count-") as temp:
            temp_root = Path(temp)
            original_root = temp_root / "original"
            patched_root = temp_root / "patched"
            for relative in sorted(relative_paths):
                source = group.source_root / relative
                if not source.is_file():
                    raise RuntimeError(f"missing pristine override source: {source}")
                for destination_root in (original_root, patched_root):
                    destination = destination_root / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source, destination)

            environment = os.environ.copy()
            environment["GIT_CEILING_DIRECTORIES"] = str(temp_root)
            for patch_index, patch in enumerate(group.patches):
                normalized_patch = temp_root / f"{patch_index}-{patch.name}"
                write_normalized_patch(patch, normalized_patch)
                result = subprocess.run(
                    [
                        "git",
                        "apply",
                        "--no-index",
                        "--whitespace=nowarn",
                        str(normalized_patch),
                    ],
                    cwd=patched_root,
                    env=environment,
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                if result.returncode != 0:
                    detail = result.stderr.decode(
                        "utf-8", errors="replace"
                    ).strip()
                    raise RuntimeError(f"cannot apply {patch.name}: {detail}")

            for relative in sorted(relative_paths):
                before = (original_root / relative).read_bytes().splitlines()
                after = (patched_root / relative).read_bytes().splitlines()
                file_additions, file_deletions = diff_ownership(before, after)
                additions += file_additions
                deletions += file_deletions
    return additions, deletions


def compute_counts() -> LineCounts:
    ordinary_first_party = sum(
        non_blank_source_lines(path) for path in root_owned_files()
    )
    pristine_third_party = sum(
        non_blank_source_lines(path) for path in pristine_third_party_files()
    )
    override_additions, override_deletions = override_ownership()
    if override_deletions > pristine_third_party:
        raise RuntimeError("override deletions exceed pristine dependency source")
    return LineCounts(
        first_party=ordinary_first_party + override_additions,
        third_party=pristine_third_party - override_deletions,
    )


def render_block(counts: LineCounts) -> str:
    return "\n".join(
        (
            START_MARKER,
            f"**First-Party Lines of Code:** {counts.first_party:,} non-blank source lines.",
            "",
            f"**Third-Party Lines of Code:** {counts.third_party:,} non-blank source lines.",
            "",
            f"**Total Lines of Code:** {counts.total:,} non-blank source lines.",
            "",
            "Counts cover UVSR source, tests, tools, build scripts, retained pinned",
            "dependency source, and final first-party dependency overrides. Documentation,",
            "assets, licenses, binaries, and generated build output are excluded. Regenerate",
            "with `tools/update_readme_line_counts.cmd --write`.",
            END_MARKER,
        )
    )


def normalized_readme() -> str:
    return README.read_bytes().decode("utf-8-sig").replace("\r\n", "\n")


def replace_block(text: str, block: str) -> str:
    if START_MARKER in text or END_MARKER in text:
        if text.count(START_MARKER) != 1 or text.count(END_MARKER) != 1:
            raise RuntimeError("README has an incomplete or duplicate line-count block")
        start = text.index(START_MARKER)
        end = text.index(END_MARKER, start) + len(END_MARKER)
        text = text[:start].rstrip("\n") + "\n\n" + text[end:].lstrip("\n")

    anchor = TAGLINE + "\n"
    if text.count(anchor) != 1:
        raise RuntimeError("README tagline is missing or ambiguous")
    return text.replace(anchor, anchor + "\n" + block + "\n", 1)


def check_readme(counts: LineCounts) -> bool:
    actual = normalized_readme()
    expected_block = render_block(counts)
    expected = replace_block(actual, expected_block)
    if actual != expected:
        print(
            "README codebase-size counts are missing or stale; run "
            "tools/update_readme_line_counts.cmd --write.",
            file=sys.stderr,
        )
        return False
    return True


def write_readme(counts: LineCounts) -> None:
    original = README.read_bytes()
    newline = "\r\n" if b"\r\n" in original else "\n"
    updated = replace_block(normalized_readme(), render_block(counts))
    README.write_bytes(updated.replace("\n", newline).encode("utf-8"))


def self_test() -> None:
    additions, deletions = diff_ownership(
        [b"retained", b"", b"vendor"],
        [b"retained", b"", b"first", b"party", b"  "],
    )
    if (additions, deletions) != (2, 1):
        raise RuntimeError("override ownership fixture failed")
    fixture = f"# UVSR\n\n{TAGLINE}\n\nBody\n"
    inserted = replace_block(fixture, render_block(LineCounts(10, 20)))
    if inserted.count(START_MARKER) != 1 or "30 non-blank" not in inserted:
        raise RuntimeError("README block fixture failed")
    with tempfile.TemporaryDirectory(prefix="uvsr-line-count-self-test-") as temp:
        temp_root = Path(temp)
        fixture = temp_root / "fixture.txt"
        fixture.write_text("before\n", encoding="utf-8", newline="\n")
        crlf_patch = temp_root / "fixture-crlf.patch"
        crlf_patch.write_bytes(
            b"diff --git a/fixture.txt b/fixture.txt\r\n"
            b"--- a/fixture.txt\r\n"
            b"+++ b/fixture.txt\r\n"
            b"@@ -1 +1 @@\r\n"
            b"-before\r\n"
            b"+after\r\n"
        )
        normalized_patch = temp_root / "fixture.patch"
        write_normalized_patch(crlf_patch, normalized_patch)
        run(
            [
                "git",
                "apply",
                "--no-index",
                "--whitespace=nowarn",
                str(normalized_patch),
            ],
            temp_root,
        )
        if fixture.read_text(encoding="utf-8") != "after\n":
            raise RuntimeError("normalized patch application fixture failed")
    print("README line-count self-test passed.")


def main() -> int:
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--check", action="store_true")
    action.add_argument("--write", action="store_true")
    action.add_argument("--print", dest="print_only", action="store_true")
    action.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    if arguments.self_test:
        self_test()
        return 0

    counts = compute_counts()
    if arguments.write:
        write_readme(counts)
        print(f"Updated README: {counts.first_party:,} first-party, "
              f"{counts.third_party:,} third-party, {counts.total:,} total.")
        return 0
    if arguments.print_only:
        print(render_block(counts))
        return 0
    if check_readme(counts):
        print(f"README line counts are current: {counts.first_party:,} first-party, "
              f"{counts.third_party:,} third-party, {counts.total:,} total.")
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
