"""Packaging boundary checks; no compiler, downloads, or installation required."""

from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_installer as installer


def write_pe(path: Path, machine: int = 0x8664) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header = bytearray(64)
    header[:2] = b"MZ"
    struct.pack_into("<I", header, 0x3C, 64)
    path.write_bytes(header + b"PE\0\0" + struct.pack("<H", machine))


class InstallerPackagingTests(unittest.TestCase):
    def test_manifest_includes_the_separately_installed_icon(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            (package / "VERSION").write_text("1.4.1\n", encoding="utf-8")
            icon = root / "Installer/assets/setup.ico"
            icon.parent.mkdir(parents=True)
            icon.write_bytes(b"synthetic-icon")
            with patch.object(installer, "PROJECT_ROOT", root):
                manifest = installer.payload_manifest(package)
            self.assertEqual([entry["file"] for entry in manifest], ["VERSION", "setup.ico"])
            self.assertEqual(manifest[1]["sha256"], installer.sha256(icon))
            self.assertEqual(manifest[1]["size"], len(b"synthetic-icon"))

    def test_numeric_versions_reject_ambiguous_or_unrepresentable_values(self):
        self.assertEqual(installer.parse_version("1.4.1", 3), (1, 4, 1, 0))
        self.assertGreater(installer.parse_version("14.44.35211.0"), installer.parse_version("14.9.9999.0"))
        for value in ("v14.44.35211.0", "14.044.35211.0", "14.44.35211", "14.44.65536.0", "14.44.-1.0"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                installer.parse_version(value)

    def test_non_x64_or_invalid_executables_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "app.exe"
            write_pe(path)
            installer.require_x64(path)
            for machine in (0x14C, 0xAA64):
                write_pe(path, machine)
                with self.assertRaises(ValueError):
                    installer.require_x64(path)
            path.write_bytes(b"invalid")
            with self.assertRaises(ValueError):
                installer.require_x64(path)

    def test_nested_runtime_and_debug_payloads_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary)
            for name in ("QtZplDesigner.exe", "Qt6Core.dll", "platforms/qwindows.dll"):
                write_pe(package / name)
            (package / "VERSION").write_text("1.4.1\n", encoding="utf-8")
            installer.validate_payload(package)
            for name in ("VC_REDIST.X64.EXE", "vcredist_x64.exe", "msvcp140_2.dll", "VCRUNTIME140.dll", "concrt140.dll", "ucrtbase.dll", "api-ms-win-crt-stdio-l1-1-0.dll", "Qt6Cored.dll"):
                with self.subTest(name=name):
                    forbidden = package / "nested" / name
                    write_pe(forbidden)
                    with self.assertRaises(ValueError):
                        installer.validate_payload(package)
                    forbidden.unlink()

    def test_runtime_requirement_uses_dll_versions_not_directory_or_compiler_patch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            qt = root / "qt"
            (qt / "mkspecs").mkdir(parents=True)
            (qt / "mkspecs/qconfig.pri").write_text(
                "QT_MSVC_MAJOR_VERSION = 19\nQT_MSVC_MINOR_VERSION = 44\nQT_MSVC_PATCH_VERSION = 35227\n", encoding="utf-8")
            redist = root / "14.44.35112"
            for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
                write_pe(redist / "x64/Microsoft.VC143.CRT" / name)
            environment = {"VCToolsVersion": "14.44.35207", "VCToolsRedistDir": str(redist)}
            with patch.object(installer, "file_version", return_value=installer.FileVersion((14, 44, 35211, 0), False)):
                self.assertEqual(installer.minimum_runtime(environment, qt), (14, 44, 35211, 0))
            with patch.object(installer, "file_version", return_value=installer.FileVersion((14, 43, 35211, 0), False)):
                with self.assertRaises(ValueError):
                    installer.minimum_runtime(environment, qt)
            (qt / "mkspecs/qconfig.pri").write_text("QT_MSVC_MAJOR_VERSION = 19\nQT_MSVC_MINOR_VERSION = 45\n", encoding="utf-8")
            with patch.object(installer, "file_version", return_value=installer.FileVersion((14, 44, 35211, 0), False)):
                with self.assertRaises(ValueError):
                    installer.minimum_runtime(environment, qt)

    def test_build_rejects_debug_stale_versions_and_a_different_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            qt = root / "qt"
            build = root / "build_release"
            source = root / "qt-zpl"
            compiler = root / "cl.exe"
            write_pe(build / "QtZplDesigner.exe")
            marker = build / "VERSION"
            marker.write_text("1.4.1\n", encoding="utf-8")
            cache_values = {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_HOME_DIRECTORY": str(root),
                "QTZPL_SOURCE_DIR": str(source),
                "Qt6_DIR": str(qt / "lib/cmake/Qt6"),
                "CMAKE_CXX_COMPILER": str(compiler),
            }

            def write_cache():
                (build / "CMakeCache.txt").write_text(
                    "\n".join(f"{key}:STRING={value}" for key, value in cache_values.items()), encoding="utf-8")

            write_cache()
            with patch.object(installer, "PROJECT_ROOT", root), patch.object(installer.shutil, "which", return_value=str(compiler)):
                for file_info in (installer.FileVersion((1, 4, 0, 0), False), installer.FileVersion((1, 4, 1, 0), True)):
                    with patch.object(installer, "file_version", return_value=file_info), self.assertRaises(ValueError):
                        installer.validate_build(build, qt, source, {"PATH": "synthetic"}, (1, 4, 1, 0))
                with patch.object(installer, "file_version", return_value=installer.FileVersion((1, 4, 1, 0), False)):
                    installer.validate_build(build, qt, source, {"PATH": "synthetic"}, (1, 4, 1, 0))
                    marker.write_text("1.4.0\n", encoding="utf-8")
                    with self.assertRaises(ValueError):
                        installer.validate_build(build, qt, source, {"PATH": "synthetic"}, (1, 4, 1, 0))
                    marker.write_text("1.4.1\n", encoding="utf-8")
                    for key, wrong in (("CMAKE_BUILD_TYPE", "Debug"), ("QTZPL_SOURCE_DIR", str(root / "old-snapshot"))):
                        previous = cache_values[key]
                        cache_values[key] = wrong
                        write_cache()
                        with self.assertRaises(ValueError):
                            installer.validate_build(build, qt, source, {"PATH": "synthetic"}, (1, 4, 1, 0))
                        cache_values[key] = previous


if __name__ == "__main__":
    unittest.main()
