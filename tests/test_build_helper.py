"""Build-directory safety and relocation checks without invoking CMake."""

import argparse
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))
import build_helper


class BuildHelperTests(unittest.TestCase):
    def setUp(self):
        temporary_root = PROJECT_ROOT / "build_agent"
        if temporary_root.resolve() != temporary_root:
            raise RuntimeError("Test temporary directory must remain inside the project")
        temporary_root.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="build_helper_tests_", dir=temporary_root)
        self.addCleanup(temporary.cleanup)
        self.temporary = Path(temporary.name).resolve()
        self.project = self.temporary / "project"
        self.build_root = self.project / "build_agent"
        self.build_root.mkdir(parents=True)
        for name, value in (("PROJECT_ROOT", self.project), ("BUILD_ROOT", self.build_root)):
            replacement = patch.object(build_helper, name, value)
            replacement.start()
            self.addCleanup(replacement.stop)

    def create_directory_link(self, link: Path, target: Path):
        try:
            link.symlink_to(target, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"Directory symlinks are unavailable: {error}")

    def test_direct_build_children_are_allowed(self):
        existing = self.build_root / "build_existing"
        existing.mkdir()
        for directory in (existing, self.build_root / "build_new"):
            with self.subTest(directory=directory):
                self.assertEqual(build_helper.checked_build_dir(directory), directory.resolve())

    def test_container_old_root_outside_and_nested_paths_are_rejected(self):
        for directory in (
            self.project,
            self.build_root,
            self.project / "build_agent_debug",
            self.temporary / "build_outside",
            self.build_root / ".." / "build_escape",
            self.build_root / "build_parent" / "build_nested",
            self.build_root / "src",
        ):
            with self.subTest(directory=directory), self.assertRaises(ValueError):
                build_helper.checked_build_dir(directory)

    def test_build_child_link_cannot_escape_container(self):
        outside = self.temporary / "build_outside"
        outside.mkdir()
        link = self.build_root / "build_link"
        self.create_directory_link(link, outside)
        with self.assertRaises(ValueError):
            build_helper.checked_build_dir(link)

    def test_container_link_cannot_redirect_generated_trees(self):
        outside = self.temporary / "outside"
        outside.mkdir()
        self.build_root.rmdir()
        self.create_directory_link(self.build_root, outside)
        with self.assertRaises(ValueError):
            build_helper.checked_build_dir(self.build_root / "build_new")

    def test_configure_refreshes_only_relocated_cache(self):
        build_dir = self.build_root / "build_agent_debug"
        build_dir.mkdir()
        cache = build_dir / "CMakeCache.txt"
        source_dir = self.project.parent / "qt-zpl"
        qt_dir = self.temporary / "qt"
        args = argparse.Namespace(
            build_dir=build_dir, config="Debug", qtzpl_source_dir=source_dir, tests=True,
        )
        for state, previous_dir, expected_fresh in (
            ("absent", None, False),
            ("current", build_dir, False),
            ("relocated", self.project / "build_agent_debug", True),
        ):
            with self.subTest(cache=state):
                if previous_dir is None:
                    cache.unlink(missing_ok=True)
                else:
                    cache.write_text(
                        f"CMAKE_CACHEFILE_DIR:INTERNAL={previous_dir.as_posix()}\n",
                        encoding="utf-8",
                    )
                with patch.object(build_helper, "run") as run:
                    build_helper.configure(args, {}, qt_dir)
                run.assert_called_once()
                command, environment = run.call_args.args
                self.assertEqual("--fresh" in command, expected_fresh)
                self.assertEqual(command[command.index("-B") + 1], str(build_dir))
                self.assertEqual(command[command.index("-S") + 1], str(self.project))
                self.assertIn(f"-DQTZPL_SOURCE_DIR={source_dir}", command)
                self.assertEqual(environment, {})


if __name__ == "__main__":
    unittest.main()
