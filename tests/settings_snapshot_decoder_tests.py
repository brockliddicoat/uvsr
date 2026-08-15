#!/usr/bin/env python3

from pathlib import Path
from contextlib import redirect_stdout
import io
import os
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

import decode_settings_snapshot as decoder  # noqa: E402

REGISTERED_VERSION = sorted(decoder.load_registered_versions())[0]


class SettingsSnapshotDecoderTests(unittest.TestCase):
    def test_known_vectors_match_cpp_contract(self) -> None:
        self.assertEqual(
            decoder.build_snapshot_code("", REGISTERED_VERSION),
            REGISTERED_VERSION + "cbf29ce4842223256c62272e07bb",
        )
        self.assertEqual(
            decoder.build_snapshot_code("a=b\n", REGISTERED_VERSION),
            REGISTERED_VERSION + "ec8b8c82c37596fba90fe6756c5c",
        )

    def test_catalog_round_trip_and_escaping(self) -> None:
        canonical = "scene.current=Sample\\nScene\nui.skin=amp\n"
        code = decoder.build_snapshot_code(canonical, REGISTERED_VERSION)
        with tempfile.TemporaryDirectory() as directory:
            catalog = Path(directory) / "catalog.txt"
            catalog.write_text(
                f"# UVSR Settings Snapshot Catalog v{REGISTERED_VERSION}\n"
                f"[{code}]\n{canonical}[/{code}]\n",
                encoding="utf-8",
            )
            snapshots = decoder.read_matching_snapshots(catalog, code)
        self.assertEqual(snapshots, [canonical])
        self.assertEqual(
            decoder.parse_settings(snapshots[0]),
            {"scene.current": "Sample\nScene", "ui.skin": "amp"},
        )

    def test_distinct_duplicate_blocks_are_a_detectable_collision(self) -> None:
        first = "a=b\n"
        code = decoder.build_snapshot_code(first, REGISTERED_VERSION)
        second = "a=c\n"
        with tempfile.TemporaryDirectory() as directory:
            catalog = Path(directory) / "catalog.txt"
            catalog.write_text(
                f"[{code}]\n{first}[/{code}]\n"
                f"[{code}]\n{second}[/{code}]\n",
                encoding="utf-8",
            )
            snapshots = decoder.read_matching_snapshots(catalog, code)
        self.assertEqual(len(list(dict.fromkeys(snapshots))), 2)

    def test_default_catalogs_include_writable_package_local_store(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            local_root = Path(directory)
            ordinary = (
                local_root
                / "UVSR"
                / f"settings-snapshots-v{REGISTERED_VERSION}.txt"
            )
            packaged = (
                local_root
                / "Packages"
                / "Example.Package"
                / "LocalCache"
                / "Local"
                / "UVSR"
                / f"settings-snapshots-v{REGISTERED_VERSION}.txt"
            )
            packaged.parent.mkdir(parents=True)
            packaged.write_text("# package-local catalog\n", encoding="utf-8")
            with mock.patch.dict(
                os.environ, {"LOCALAPPDATA": str(local_root)}
            ):
                catalogs = decoder.default_catalog_paths(REGISTERED_VERSION)
        self.assertEqual(catalogs, [ordinary, packaged])

    def test_registry_accepts_multiple_versions_and_rejects_reserved_codes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            registry = Path(directory) / "versions.def"
            registry.write_text(
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x0002u, 0x0000000000000001ull, "
                "0x0000000000000002ull)\n"
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x00abu, 0x0000000000000003ull, "
                "0x0000000000000004ull)\n",
                encoding="utf-8",
            )
            versions = decoder.load_registered_versions(registry)
        self.assertEqual(versions, {"0002", "00ab"})
        with self.assertRaises(ValueError):
            decoder.build_snapshot_code("a=b\n", "0000")
        with self.assertRaises(ValueError):
            decoder.build_snapshot_code("a=b\n", "0001")

    def test_registry_rejects_reserved_or_colliding_rows(self) -> None:
        invalid_registries = (
            (
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x0002u, 0x0000000000000001ull, "
                "0x0000000000000002ull)\n"
                "<<<<<<< unseen-branch\n"
            ),
            (
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x0002u, 0x0000000000000000ull, "
                "0x0000000000000000ull)\n"
            ),
            (
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x0001u, 0x0000000000000001ull, "
                "0x0000000000000002ull)\n"
            ),
            (
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x0002u, 0x0000000000000001ull, "
                "0x0000000000000002ull)\n"
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x0002u, 0x0000000000000003ull, "
                "0x0000000000000004ull)\n"
            ),
            (
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x0002u, 0x0000000000000001ull, "
                "0x0000000000000002ull)\n"
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x0003u, 0x0000000000000001ull, "
                "0x0000000000000002ull)\n"
            ),
        )
        with tempfile.TemporaryDirectory() as directory:
            registry = Path(directory) / "versions.def"
            for source in invalid_registries:
                registry.write_text(source, encoding="utf-8")
                with self.assertRaises(ValueError):
                    decoder.load_registered_versions(registry)

    def test_catalog_name_is_selected_from_the_code_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.dict(
                os.environ, {"LOCALAPPDATA": str(Path(directory))}
            ):
                catalogs = decoder.default_catalog_paths("00ab")
        self.assertEqual(
            catalogs[0].name,
            "settings-snapshots-v00ab.txt",
        )

    def test_cli_round_trip_uses_a_nondefault_registered_version(self) -> None:
        canonical = "ui.skin=amp\n"
        code = decoder.build_snapshot_code(canonical, "00ab")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            registry = root / "versions.def"
            registry.write_text(
                "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION("
                "0x00abu, 0x0000000000000001ull, "
                "0x0000000000000002ull)\n",
                encoding="utf-8",
            )
            catalog = root / "catalog.txt"
            catalog.write_text(
                f"[{code}]\n{canonical}[/{code}]\n",
                encoding="utf-8",
            )
            output = io.StringIO()
            with mock.patch.object(
                sys,
                "argv",
                [
                    "decode_settings_snapshot.py",
                    code,
                    "--registry",
                    str(registry),
                    "--catalog",
                    str(catalog),
                ],
            ), redirect_stdout(output):
                result = decoder.main()
        self.assertEqual(result, 0)
        self.assertEqual(output.getvalue(), "ui.skin=amp\n")


if __name__ == "__main__":
    unittest.main()
