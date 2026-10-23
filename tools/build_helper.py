#!/usr/bin/env python3
"""Build, test, run, and deploy QtZpl Designer on Windows.

    py -3 tools/build_helper.py all --clean --config Debug
    py -3 tools/build_helper.py run
    py -3 tools/build_helper.py deploy --config Release
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from uuid import uuid4


PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_ROOT = PROJECT_ROOT / "build_agent"
QT_ROOT = Path(os.environ.get("QT_ROOT", "C:/Qt"))
QT_TOOLS = QT_ROOT / "Tools"


def find_vcvars64() -> Path:
    for edition in ("Community", "Professional", "Enterprise", "BuildTools"):
        candidate = Path(
            f"C:/Program Files/Microsoft Visual Studio/2022/{edition}"
            "/VC/Auxiliary/Build/vcvars64.bat"
        )
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("Visual Studio 2022 vcvars64.bat was not found")


def capture_environment(qt_dir: Path, compiler: str) -> dict[str, str]:
    qtenv = qt_dir / "bin/qtenv2.bat"
    if not qtenv.is_file():
        raise FileNotFoundError(f"Qt environment script was not found: {qtenv}")
    marker = f"__DESIGNER_ENV_{uuid4().hex}__"
    commands = [f'call "{qtenv}"']
    if compiler.casefold().startswith("msvc"):
        commands.append(f'call "{find_vcvars64()}"')
    commands.extend(["echo(", f"echo {marker}", "set"])
    result = subprocess.run(
        " && ".join(commands), shell=True, text=True, encoding="cp866",
        errors="replace", capture_output=True, check=False,
    )
    lines = result.stdout.splitlines()
    marker_index = next((i for i, line in enumerate(lines) if line.strip() == marker), None)
    if result.returncode or marker_index is None:
        # Do not print the captured environment: it can contain credentials.
        setup_output = "\n".join(lines if marker_index is None else lines[:marker_index])
        raise RuntimeError(f"Qt/MSVC initialization failed:\n{setup_output}\n{result.stderr}")
    environment = os.environ.copy()
    for line in lines[marker_index + 1:]:
        name, separator, value = line.partition("=")
        if name and separator:
            environment[name] = value
    path_keys = [name for name in environment if name.casefold() == "path"]
    inherited_path = environment[path_keys[-1]] if path_keys else ""
    for name in path_keys:
        del environment[name]
    environment["PATH"] = os.pathsep.join([
        str(qt_dir / "bin"),
        str(QT_TOOLS / "CMake_64/bin"),
        str(QT_TOOLS / "Ninja"),
        inherited_path,
    ])
    environment["QTDIR"] = str(qt_dir)
    return environment


def checked_build_dir(path: Path) -> Path:
    """Limit generated trees and deletion to build_* children of build_agent."""
    resolved = path.resolve()
    if BUILD_ROOT.resolve() != BUILD_ROOT or resolved.parent != BUILD_ROOT or not resolved.name.startswith("build_"):
        raise ValueError(f"Build directory must be a build_* child of {BUILD_ROOT}: {resolved}")
    return resolved


def cached_compiler(build_dir: Path) -> Path | None:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_CXX_COMPILER:FILEPATH="):
            return Path(line.split("=", 1)[1])
    return None


def run(command: list[str], environment: dict[str, str]) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(command, cwd=BUILD_ROOT, env=environment, check=False)
    if result.returncode:
        raise SystemExit(result.returncode)


def configure(args: argparse.Namespace, environment: dict[str, str], qt_dir: Path) -> None:
    fresh = []
    cache = args.build_dir / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_CACHEFILE_DIR:INTERNAL="):
                if Path(line.split("=", 1)[1]).resolve() != args.build_dir.resolve():
                    # CMake caches contain absolute paths. Regenerate after a move,
                    # preserving logs, screenshots, and other build artifacts.
                    fresh = ["--fresh"]
                break
    run([
        str(QT_TOOLS / "CMake_64/bin/cmake.exe"),
        *fresh,
        "-S", str(PROJECT_ROOT), "-B", str(args.build_dir), "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={args.config}",
        f"-DCMAKE_PREFIX_PATH={qt_dir}",
        f"-DQTZPL_SOURCE_DIR={args.qtzpl_source_dir}",
        f"-DDESIGNER_BUILD_TESTS={'ON' if args.tests else 'OFF'}",
    ], environment)


def deploy(args: argparse.Namespace, environment: dict[str, str], qt_dir: Path) -> None:
    # CMake writes this marker beside the EXE only after a successful link.
    # Never label an older executable with a newly edited source VERSION.
    version_file = args.build_dir / "VERSION"
    source_version = (PROJECT_ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not version_file.is_file():
        raise RuntimeError("The built VERSION marker is missing; rebuild QtZplDesigner before deployment")
    built_version = version_file.read_text(encoding="utf-8").strip()
    if built_version != source_version:
        raise RuntimeError(
            f"Built version {built_version!r} differs from source version {source_version!r}; "
            "rebuild QtZplDesigner before deployment"
        )
    destination = PROJECT_ROOT / "dist" / args.config
    destination.mkdir(parents=True, exist_ok=True)
    application = destination / "QtZplDesigner.exe"
    shutil.copy2(args.build_dir / "QtZplDesigner.exe", application)
    run([
        str(qt_dir / "bin/windeployqt.exe"),
        "--debug" if args.config == "Debug" else "--release",
        "--compiler-runtime", "--no-translations", str(application),
    ], environment)
    shutil.copytree(PROJECT_ROOT / "examples", destination / "examples", dirs_exist_ok=True)
    for name in ("README.md", "CHANGELOG.md"):
        shutil.copy2(PROJECT_ROOT / name, destination / name)
    shutil.copy2(version_file, destination / "VERSION")
    print(f"Runnable application: {application} (version {built_version})", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", nargs="?", default="all", choices=("configure", "build", "test", "all", "run", "deploy"))
    parser.add_argument("--config", choices=("Debug", "Release", "RelWithDebInfo"), default="Debug")
    parser.add_argument("--qt-version", default="6.11.2")
    parser.add_argument("--compiler", default="msvc2022_64")
    parser.add_argument("--qtzpl-source-dir", type=Path, default=PROJECT_ROOT.parent / "qt-zpl")
    parser.add_argument("--build-dir", type=Path, help="build_* directory directly inside the project's build_agent")
    parser.add_argument("--target", default="all")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--test-jobs", type=int, default=os.cpu_count() or 1,
                        help="Test executables ctest runs at once (default: all logical CPUs)")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--no-tests", dest="tests", action="store_false")
    parser.add_argument("--smoke-test", action="store_true", help="Pass --smoke-test to run")
    parser.add_argument("--screenshot", type=Path, help="Pass screenshot destination to run")
    parser.set_defaults(tests=True)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This helper initializes a Windows Qt/MSVC kit; use CMake directly on other platforms")
    if args.parallel < 1:
        parser.error("--parallel must be positive")
    if args.test_jobs < 1:
        parser.error("--test-jobs must be positive")
    args.build_dir = checked_build_dir(args.build_dir or BUILD_ROOT / f"build_agent_{args.config.casefold()}")
    args.qtzpl_source_dir = args.qtzpl_source_dir.resolve()
    qt_dir = (QT_ROOT / args.qt_version / args.compiler).resolve()
    if not qt_dir.is_dir():
        parser.error(f"Qt kit does not exist: {qt_dir}")
    environment = capture_environment(qt_dir, args.compiler)
    compiler = cached_compiler(args.build_dir)
    if args.build_dir.exists() and (args.clean or (compiler and not compiler.exists())):
        # Revalidate the fully resolved absolute path immediately before deletion.
        safe_directory = checked_build_dir(args.build_dir)
        print(f"Removing generated build directory: {safe_directory}", flush=True)
        shutil.rmtree(safe_directory)
    # Apply the current source path on every invocation: an existing CMake cache
    # may still point to an old checkout or snapshot, including during deploy.
    configure(args, environment, qt_dir)
    if args.action == "configure":
        return 0
    target = "QtZplDesigner" if args.action in ("run", "deploy") else args.target
    run([
        str(QT_TOOLS / "CMake_64/bin/cmake.exe"),
        "--build", str(args.build_dir), "--target", target, "--parallel", str(args.parallel),
    ], environment)
    if args.action in ("test", "all") and args.tests:
        test_environment = environment.copy()
        test_environment["QT_QPA_PLATFORM"] = "offscreen"
        run([
            str(QT_TOOLS / "CMake_64/bin/ctest.exe"), "--test-dir", str(args.build_dir),
            "--output-on-failure", "-C", args.config, "--parallel", str(args.test_jobs),
        ], test_environment)
    if args.action == "deploy":
        deploy(args, environment, qt_dir)
    if args.action == "run":
        command = [str(args.build_dir / "QtZplDesigner.exe")]
        if args.smoke_test:
            command.append("--smoke-test")
            environment["QT_QPA_PLATFORM"] = "offscreen"
        if args.screenshot:
            command.extend(["--screenshot", str(args.screenshot.resolve())])
        run(command, environment)
    print(f"QtZpl Designer {args.action} ({args.config}) succeeded.", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
