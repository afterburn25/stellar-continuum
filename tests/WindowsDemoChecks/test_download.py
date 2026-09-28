"""Verify transfer integrity and runner restrictions without launching a game."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import package_windows_demo as PACKAGE
import smoke_windows_demo as SMOKE
from test_package import fixture


class WindowsDemoDownloadChecks(unittest.TestCase):
    def test_startup_uses_the_application_shutdown_path(self):
        self.assertEqual(["--", "--stellar-startup-smoke"], SMOKE.STARTUP_ARGUMENTS[-2:])
        self.assertNotIn("--quit-after", SMOKE.STARTUP_ARGUMENTS)

    def make_archive(self, directory):
        export = Path(directory) / "windows"
        fixture(export)
        return PACKAGE.package(export, Path(directory) / "dist", "a" * 40,
                               "https://github.com/afterburn25/stellar-continuum/actions/runs/1")

    def test_verified_download_has_complete_checksums(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = self.make_archive(temp)
            _, manifest = SMOKE.verify_archive(archive)
            self.assertEqual(PACKAGE.EXE, manifest["entry_point"])
            self.assertEqual("a" * 40, manifest["git_commit"])

    def test_changed_archive_fails_its_sidecar(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = self.make_archive(temp)
            with archive.open("ab") as stream:
                stream.write(b"unexpected change")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                SMOKE.verify_archive(archive)

    def test_unmanifested_file_is_rejected_even_with_matching_zip_hash(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = self.make_archive(temp)
            with zipfile.ZipFile(archive, "a") as contents:
                contents.writestr("unexpected.txt", "unexpected")
            archive.with_suffix(".zip.sha256").write_text(PACKAGE.sha256(archive) + "  " + archive.name)
            with self.assertRaisesRegex(ValueError, "manifest"):
                SMOKE.verify_archive(archive)

    def test_desktop_execution_is_refused_without_starting_process(self):
        with patch.dict(SMOKE.os.environ, {"GITHUB_ACTIONS": "false"}), \
             patch.object(SMOKE.subprocess, "Popen") as process, \
             patch.object(sys, "argv", ["smoke", "candidate", "--output", "dist", "--log", "run.log"]), \
             contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(1, SMOKE.main())
            process.assert_not_called()


if __name__ == "__main__":
    unittest.main()
