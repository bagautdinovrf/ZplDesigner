#!/usr/bin/env python3
"""Build a Release x64 Inno Setup installer without bundling the VC++ runtime.

    py -3 tools/build_installer.py
    py -3 tools/build_installer.py --skip-build

The normal path rebuilds against the current sibling QtZpl sources. --skip-build
is for packaging a Release build that has already been built and verified.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
from typing import NamedTuple

import build_helper


PROJECT_ROOT = build_helper.PROJECT_ROOT
Version = tuple[int, int, int, int]
RUNTIME_NAME = re.compile(
    r"(?:(?:msvc[pr]|vcruntime|concrt|vccorlib|vcomp|vcamp|mfc|mfcm|atl)\d.*\.dll"
    r"|ucrtbased?\.dll|api-ms-win-crt-.*\.dll|vc_?redist.*\.exe)", re.IGNORECASE,
)


class FileVersion(NamedTuple):
    version: Version
    debug: bool


def parse_version(value: str, components: int = 4) -> Version:
    if not re.fullmatch(r"(?:0|[1-9]\d*)(?:\.(?:0|[1-9]\d*)){" + str(components - 1) + r"}", value):
        raise ValueError(f"Expected a {components}-component numeric version, got {value!r}")
    parts = tuple(int(part) for part in value.split("."))
    if any(part > 65535 for part in parts):
        raise ValueError(f"Version components must fit in Windows version fields: {value}")
    return (parts + (0,) * (4 - len(parts)))  # type: ignore[return-value]


def version_text(version: Version) -> str:
    return ".".join(str(part) for part in version)


def file_version(path: Path) -> FileVersion:
    """Read numeric VERSIONINFO without launching the executable or using a shell."""
    class FixedFileInfo(ctypes.Structure):
        _fields_ = [(name, wintypes.DWORD) for name in (
            "signature", "structure_version", "file_ms", "file_ls", "product_ms", "product_ls",
            "flags_mask", "flags", "os", "type", "subtype", "date_ms", "date_ls",
        )]

    version_api = ctypes.WinDLL("version", use_last_error=True)
    version_api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version_api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    version_api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    version_api.GetFileVersionInfoW.restype = wintypes.BOOL
    version_api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    version_api.VerQueryValueW.restype = wintypes.BOOL
    size = version_api.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        raise OSError(f"Cannot read VERSIONINFO from {path}: {ctypes.WinError(ctypes.get_last_error())}")
    data = ctypes.create_string_buffer(size)
    if not version_api.GetFileVersionInfoW(str(path), 0, size, data):
        raise ctypes.WinError(ctypes.get_last_error())
    pointer = ctypes.c_void_p()
    length = wintypes.UINT()
    if not version_api.VerQueryValueW(data, "\\", ctypes.byref(pointer), ctypes.byref(length)) or length.value < ctypes.sizeof(FixedFileInfo):
        raise ValueError(f"Invalid VERSIONINFO in {path}")
    info = ctypes.cast(pointer, ctypes.POINTER(FixedFileInfo)).contents
    if info.signature != 0xFEEF04BD:
        raise ValueError(f"Invalid VERSIONINFO signature in {path}")
    version = (info.file_ms >> 16, info.file_ms & 0xFFFF, info.file_ls >> 16, info.file_ls & 0xFFFF)
    return FileVersion(version, bool(info.flags_mask & info.flags & 0x1))


def require_x64(path: Path) -> None:
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b"MZ":
            raise ValueError(f"Not a Windows PE image: {path}")
        offset = struct.unpack_from("<I", header, 0x3C)[0]
        stream.seek(offset)
        pe = stream.read(6)
    if len(pe) != 6 or pe[:4] != b"PE\0\0" or struct.unpack_from("<H", pe, 4)[0] != 0x8664:
        raise ValueError(f"Installer payload must contain x64 PE images: {path}")


def environment_value(environment: dict[str, str], name: str) -> str:
    value = next((value for key, value in environment.items() if key.casefold() == name.casefold()), "")
    if not value:
        raise ValueError(f"The MSVC environment is missing {name}")
    return value


def minimum_runtime(environment: dict[str, str], qt_dir: Path) -> Version:
    toolset = parse_version(environment_value(environment, "VCToolsVersion").strip(), 3)
    redist_dir = Path(environment_value(environment, "VCToolsRedistDir")) / "x64" / "Microsoft.VC143.CRT"
    for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
        if not (redist_dir / name).is_file():
            raise FileNotFoundError(f"Paired MSVC x64 CRT is missing: {redist_dir / name}")
    versions: list[Version] = []
    for dll in sorted(redist_dir.glob("*.dll")):
        require_x64(dll)
        info = file_version(dll)
        if info.debug or info.version[0] != 14 or info.version[:2] < toolset[:2]:
            raise ValueError(f"CRT {dll} does not match the selected Release MSVC toolset")
        versions.append(info.version)
    # Directory names are not reliable CRT versions (e.g. 14.44.35112 can contain
    # 14.44.35211 DLLs). Use actual paired DLLs and the selected toolset instead.
    required = max([toolset, *versions])
    qconfig = (qt_dir / "mkspecs/qconfig.pri").read_text(encoding="utf-8")
    qt_values = dict(re.findall(r"^QT_MSVC_(MAJOR|MINOR)_VERSION\s*=\s*(\d+)\s*$", qconfig, re.MULTILINE))
    if qt_values.get("MAJOR") != "19" or "MINOR" not in qt_values:
        raise ValueError("Cannot determine the Qt kit's compatible MSVC 14.x runtime family")
    qt_toolset = (14, int(qt_values["MINOR"]))
    if min(versions)[:2] < qt_toolset:
        raise ValueError("The Qt kit was built with a newer MSVC toolset; update the selected MSVC tools and CRT")
    # Qt's compiler patch (19.44.35227) is NOT a runtime patch version.
    return required


def validate_build(build_dir: Path, qt_dir: Path, source_dir: Path, environment: dict[str, str], version: Version) -> None:
    cache: dict[str, str] = {}
    for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if not line.startswith(("#", "//")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    if cache.get("CMAKE_BUILD_TYPE") != "Release":
        raise ValueError("Only a Release build can be packaged")
    compiler = shutil.which("cl.exe", path=environment_value(environment, "PATH"))
    if not compiler:
        raise FileNotFoundError("cl.exe was not found in the selected MSVC environment")
    expected_paths = {
        "CMAKE_HOME_DIRECTORY": PROJECT_ROOT,
        "QTZPL_SOURCE_DIR": source_dir,
        "Qt6_DIR": qt_dir / "lib/cmake/Qt6",
        "CMAKE_CXX_COMPILER": Path(compiler),
    }
    for key, expected in expected_paths.items():
        if not cache.get(key) or Path(cache[key]).resolve() != expected.resolve():
            raise ValueError(f"{key} does not match the requested build environment; rebuild before packaging")
    marker = (build_dir / "VERSION").read_text(encoding="utf-8").strip()
    if parse_version(marker, 3) != version:
        raise ValueError("The built VERSION differs from the source VERSION; rebuild before packaging")
    application = build_dir / "QtZplDesigner.exe"
    require_x64(application)
    info = file_version(application)
    if info.debug or info.version != version:
        raise ValueError("The EXE is Debug or its numeric file version differs from VERSION; rebuild before packaging")


def validate_payload(package_dir: Path) -> None:
    for required in ("QtZplDesigner.exe", "Qt6Core.dll", "platforms/qwindows.dll", "VERSION"):
        if not (package_dir / required).is_file():
            raise FileNotFoundError(f"Required installer payload is missing: {required}")
    for path in package_dir.rglob("*"):
        if path.is_file() and RUNTIME_NAME.fullmatch(path.name):
            raise ValueError(f"The installer must not bundle compiler runtime files: {path}")
        if path.is_file() and path.suffix.casefold() in (".exe", ".dll"):
            require_x64(path)
            if path.name.casefold().startswith("qt6") and path.stem.casefold().endswith("d"):
                raise ValueError(f"Debug Qt DLL in Release package: {path}")


def find_iscc(requested: Path | None) -> Path:
    candidates = [requested] if requested else []
    if not requested:
        configured = os.environ.get("ISCC") or shutil.which("ISCC.exe")
        if configured:
            candidates.append(Path(configured))
        for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles"), os.environ.get("LOCALAPPDATA")):
            if base:
                candidates.extend([Path(base) / "Inno Setup 6/ISCC.exe", Path(base) / "Programs/Inno Setup 6/ISCC.exe"])
    for candidate in candidates:
        if candidate and candidate.is_file():
            # ISCC.exe has no useful VERSIONINFO on some Inno releases.
            result = subprocess.run([str(candidate), "/?"], text=True, errors="replace", capture_output=True, check=False)
            match = re.search(r"Inno Setup\s+(\d+)\b", result.stdout + result.stderr)
            if not match or int(match[1]) < 6:
                raise ValueError(f"Inno Setup 6.4 or newer is required: {candidate}")
            # The CLI banner only exposes the major version. installer.iss
            # checks the exact minimum against the preprocessor's compiler Ver.
            return candidate.resolve()
    raise FileNotFoundError("Inno Setup 6.4+ ISCC.exe was not found; install Inno Setup or pass --iscc")


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def payload_manifest(package_dir: Path) -> list[dict[str, str | int]]:
    files = {path.relative_to(package_dir).as_posix(): path for path in package_dir.rglob("*") if path.is_file()}
    # This installed application icon is a direct [Files] entry in installer.iss,
    # not part of the windeployqt staging directory.
    files["setup.ico"] = PROJECT_ROOT / "Installer/assets/setup.ico"
    return [{"file": name, "size": path.stat().st_size, "sha256": sha256(path)} for name, path in sorted(files.items())]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--qt-version", default="6.11.2")
    parser.add_argument("--compiler", default="msvc2022_64")
    parser.add_argument("--build-dir", type=Path, default=PROJECT_ROOT / "build_agent_release")
    parser.add_argument("--qtzpl-source-dir", type=Path, default=PROJECT_ROOT.parent / "qt-zpl")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--iscc", type=Path, help="Path to Inno Setup 6.4+ ISCC.exe")
    parser.add_argument("--skip-build", action="store_true", help="Package an existing verified Release build")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("The installer builder requires Windows and an x64 MSVC Qt kit")
    if args.compiler != "msvc2022_64":
        parser.error("Only the MSVC x64 Qt kit is supported")
    if args.parallel < 1:
        parser.error("--parallel must be positive")
    build_dir = build_helper.checked_build_dir(args.build_dir)
    staging_root = build_helper.checked_build_dir(PROJECT_ROOT / "build_installer")
    if build_dir == staging_root:
        parser.error("--build-dir cannot be build_installer; that directory is reserved for packaging")
    source_dir = args.qtzpl_source_dir.resolve()
    qt_dir = (build_helper.QT_ROOT / args.qt_version / args.compiler).resolve()
    version_string = (PROJECT_ROOT / "VERSION").read_text(encoding="utf-8").strip()
    version = parse_version(version_string, 3)
    iscc = find_iscc(args.iscc)
    environment = build_helper.capture_environment(qt_dir, args.compiler)
    runtime = minimum_runtime(environment, qt_dir)
    print(f"Installer requires Microsoft Visual C++ x64 {version_text(runtime)} or newer.", flush=True)
    if not args.skip_build:
        build_helper.run([
            sys.executable, str(PROJECT_ROOT / "tools/build_helper.py"), "build", "--config", "Release",
            "--target", "QtZplDesigner", "--build-dir", str(build_dir), "--qt-version", args.qt_version,
            "--compiler", args.compiler, "--qtzpl-source-dir", str(source_dir), "--parallel", str(args.parallel),
        ], environment)
    validate_build(build_dir, qt_dir, source_dir, environment, version)
    if staging_root.exists():
        # Validate the resolved absolute target immediately before recursive deletion.
        shutil.rmtree(build_helper.checked_build_dir(staging_root))
    package_dir = staging_root / "package"
    package_dir.mkdir(parents=True)
    application = package_dir / "QtZplDesigner.exe"
    shutil.copy2(build_dir / application.name, application)
    build_helper.run([
        str(qt_dir / "bin/windeployqt.exe"), "--release", "--no-compiler-runtime", "--no-translations", str(application),
    ], environment)
    shutil.copytree(PROJECT_ROOT / "examples", package_dir / "examples")
    for name in ("VERSION", "README.md", "CHANGELOG.md"):
        shutil.copy2(PROJECT_ROOT / name, package_dir / name)
    validate_payload(package_dir)
    output_dir = PROJECT_ROOT / "dist/installer"
    output_dir.mkdir(parents=True, exist_ok=True)
    build_helper.run([
        str(iscc), f"/DPackageDir={package_dir}", f"/DOutputDir={output_dir}",
        f"/DMinimumVCRuntimeVersion={version_text(runtime)}", str(PROJECT_ROOT / "Installer/installer.iss"),
    ], environment)
    setup = output_dir / f"QtZplDesigner-{version_string}-Setup.exe"
    if not setup.is_file():
        raise FileNotFoundError(f"Inno Setup did not produce the expected installer: {setup}")
    manifest = {
        "version": version_string,
        "architecture": "x64",
        "minimum_vc_runtime_version": version_text(runtime),
        "runtime_bundled": False,
        "qt_version": args.qt_version,
        "qtzpl_source_dir": str(source_dir),
        "setup": {"file": setup.name, "sha256": sha256(setup)},
        "payload": payload_manifest(package_dir),
    }
    manifest_path = setup.with_suffix(".manifest.json")
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Installer: {setup}\nManifest: {manifest_path}\nSHA256: {manifest['setup']['sha256']}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
