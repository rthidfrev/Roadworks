"""Filesystem-only deployment/rollback checks in a temporary fake installation."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SOURCE = Path(__file__).resolve().parents[1] / "scripts/install-main-mod.py"
spec = importlib.util.spec_from_file_location("main_installer", SOURCE)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "steamapps/common/Satisfactory"
        self.mods = self.game / "FactoryGame/Mods"
        self.target = self.mods / "Roadworks"
        (self.mods / "SML").mkdir(parents=True)
        (self.mods / "SML/SML.uplugin").write_text('{"VersionName":"3.12.0"}')
        (self.mods / "OtherMod").mkdir()
        (self.mods / "OtherMod/unrelated.bin").write_bytes(b"preserve other mods")
        (self.game / "FactoryGameSteam.exe").touch()
        (self.game.parents[1] / "appmanifest_526870.acf").write_text('"buildid" "123"')
        self.user = self.root / "user"
        for name in ("Config/Windows/Input.ini", "SaveGames/blueprints/existing.sbp", "SaveGames/existing.sav",
                     "ParametricRoads/Presets/old.json", "Roadworks/Presets/new.json"):
            p = self.user / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"preserve user data")
        (self.root / "environment.lock.json").write_text('{"game":{"steam_build_id":"123"}}')
        (self.root / "dist").mkdir()
        self.archive = self.root / "dist/candidate.zip"
        with zipfile.ZipFile(self.archive, "w") as package:
            package.writestr("Roadworks.uplugin", '{"VersionName":"new"}')
            package.writestr("Binaries/candidate.dll", b"new compiled mod")
        self.manifest = {"artifact": self.archive.name, "version": "new", "commit": "test",
                         "sml_version": "3.12.0", "sha256": hashlib.sha256(self.archive.read_bytes()).hexdigest()}
        (self.root / "dist/manifest.json").write_text(json.dumps(self.manifest))
        for name, value in (("ROOT", self.root), ("GAME", self.game), ("USER_DATA", self.user)):
            mock = patch.object(installer, name, value)
            mock.start()
            self.addCleanup(mock.stop)
        for mock in (patch.object(installer, "no_game_running"),
                     patch("sys.argv", [str(SOURCE)]), patch.object(Path, "home", return_value=self.root)):
            mock.start()
            self.addCleanup(mock.stop)

    def run_install(self):
        with contextlib.redirect_stdout(io.StringIO()):
            installer.main()

    def old_mod(self):
        self.target.mkdir()
        (self.target / "Roadworks.uplugin").write_text('{"VersionName":"old"}')
        (self.target / "old-only.bin").write_bytes(b"old plugin data")

    def test_fresh_install_preserves_everything_else(self):
        before = installer.preserved_files(self.mods)
        self.run_install()
        self.assertEqual(before, installer.preserved_files(self.mods))
        self.assertEqual((self.target / "Binaries/candidate.dll").read_bytes(), b"new compiled mod")
        self.assertTrue(json.loads((self.root / ".local/deploy/latest-receipt.json").read_text())
                        ["unrelated_mods_settings_saves_unchanged"])

    def test_upgrade_retains_backup_and_removes_stale_own_files(self):
        self.old_mod()
        self.run_install()
        receipt = json.loads((self.root / ".local/deploy/latest-receipt.json").read_text())
        self.assertEqual((Path(receipt["backup"]) / "old-only.bin").read_bytes(), b"old plugin data")
        self.assertFalse((self.target / "old-only.bin").exists())
        self.assertFalse(list(self.mods.glob(".Roadworks*")))

    def fail_verification(self):
        before = installer.preserved_files(self.mods)
        with patch.object(installer, "preserved_files", side_effect=[before, {}]):
            with self.assertRaisesRegex(RuntimeError, "outside Roadworks"):
                self.run_install()
        self.assertEqual(before, installer.preserved_files(self.mods))

    def test_failed_upgrade_restores_previous_mod(self):
        self.old_mod()
        self.fail_verification()
        self.assertEqual((self.target / "old-only.bin").read_bytes(), b"old plugin data")
        self.assertEqual(json.loads((self.target / "Roadworks.uplugin").read_text())["VersionName"], "old")

    def test_failed_first_install_restores_absence(self):
        self.fail_verification()
        self.assertFalse(self.target.exists())

    def legacy_mod(self):
        legacy = self.mods / "ParametricRoads"
        legacy.mkdir()
        (legacy / "ParametricRoads.uplugin").write_text('{"VersionName":"legacy"}')
        (legacy / "legacy-only.bin").write_bytes(b"legacy plugin data")
        return legacy

    def test_reference_migration_backs_up_and_removes_old_plugin(self):
        legacy = self.legacy_mod()
        before = installer.preserved_files(self.mods)
        self.run_install()
        self.assertFalse(legacy.exists())
        self.assertTrue((self.target / "Roadworks.uplugin").is_file())
        receipt = json.loads((self.root / ".local/deploy/latest-receipt.json").read_text())
        self.assertEqual((Path(receipt["backups"]["ParametricRoads"]) / "legacy-only.bin").read_bytes(),
                         b"legacy plugin data")
        self.assertEqual(before, installer.preserved_files(self.mods))

    def test_failed_migration_restores_both_references(self):
        self.old_mod()
        legacy = self.legacy_mod()
        self.fail_verification()
        self.assertEqual((legacy / "legacy-only.bin").read_bytes(), b"legacy plugin data")
        self.assertEqual((self.target / "old-only.bin").read_bytes(), b"old plugin data")

    def test_symlinked_legacy_plugin_refused(self):
        (self.mods / "ParametricRoads").symlink_to(self.root / "unrelated-directory")
        with self.assertRaisesRegex(SystemExit, "symlinked"):
            self.run_install()
        self.assertFalse(self.target.exists())

    def test_corrupt_candidate_refused_before_install(self):
        self.archive.write_bytes(b"wrong archive")
        with self.assertRaisesRegex(SystemExit, "does not match"):
            self.run_install()
        self.assertFalse(self.target.exists())

    def test_running_game_refused_before_install(self):
        with patch.object(installer, "no_game_running", side_effect=SystemExit("game running")):
            with self.assertRaisesRegex(SystemExit, "game running"):
                self.run_install()
        self.assertFalse(self.target.exists())


if __name__ == "__main__":
    unittest.main()
