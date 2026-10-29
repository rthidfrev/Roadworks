#!/usr/bin/env python3
"""Install only the verified local ParametricRoads candidate in normal Steam.

Does not run the game or alter SML, Mod Manager profiles, settings or saves.
Existing ParametricRoads files are backed up outside Mods before replacement.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import re
from pathlib import Path, PurePosixPath
import shutil
import stat
import uuid
import zipfile

ROOT = Path(__file__).resolve().parent.parent
GAME = Path.home() / ".local/share/Steam/steamapps/common/Satisfactory"
USER_DATA = (Path.home() / ".local/share/Steam/steamapps/compatdata/526870/pfx/drive_c/users"
             / "steamuser/AppData/Local/FactoryGame/Saved")


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def no_game_running():
    for comm in Path("/proc").glob("[0-9]*/comm"):
        try:
            name = comm.read_text().strip().lower()
            # The pinned Win64 game renames its main Proton thread GameThread.
            # Checking only the launcher name misses an already running game.
            if name.startswith("factorygame") or name == "gamethread":
                raise SystemExit("Quit Satisfactory completely before installing.")
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass


def preserved_files(mods):
    files = {}
    for path in mods.rglob("*"):
        if path.is_file() and path.relative_to(mods).parts[0] != "ParametricRoads":
            files[str(path)] = digest(path)
    for folder in (USER_DATA / "Config", USER_DATA / "SaveGames", USER_DATA / "ParametricRoads",
                   GAME / "FactoryGame/Saved/ParametricRoads"):
        if folder.exists():
            for path in folder.rglob("*"):
                if path.is_file():
                    files[str(path)] = digest(path)
    for name in ("profiles.json", "installations.json"):
        path = Path.home() / ".local/share/ficsit" / name
        if path.is_file():
            files[str(path)] = digest(path)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    manifest_path = ROOT / "dist/manifest.json"
    manifest = json.loads(manifest_path.read_text())
    archive = (manifest_path.parent / manifest["artifact"]).resolve()
    if not archive.is_relative_to(manifest_path.parent.resolve()):
        raise SystemExit("Archive is outside the distribution directory.")
    if digest(archive) != manifest["sha256"]:
        raise SystemExit("Archive does not match the verified manifest.")
    game = GAME.resolve()
    mods = game / "FactoryGame/Mods"
    destination = mods / "ParametricRoads"
    if not (game / "FactoryGameSteam.exe").is_file():
        raise SystemExit("Normal Steam installation not found.")
    lock = json.loads((ROOT / "environment.lock.json").read_text())
    steam_manifest = (game.parents[1] / "appmanifest_526870.acf").read_text()
    build = re.search(r'"buildid"\s+"(\d+)"', steam_manifest)
    if not build or build[1] != lock["game"]["steam_build_id"]:
        raise SystemExit("Steam version differs from the pinned SDK.")
    sml = json.loads((mods / "SML/SML.uplugin").read_text())
    if sml["VersionName"] != manifest["sml_version"]:
        raise SystemExit("Installed SML differs from the candidate version.")
    if mods.is_symlink() or destination.is_symlink():
        raise SystemExit("Refusing a symlinked Mods or plugin directory.")
    for descriptor in mods.rglob("ParametricRoads.uplugin"):
        if descriptor != destination / "ParametricRoads.uplugin":
            raise SystemExit(f"Additional ParametricRoads installation: {descriptor}")
    no_game_running()
    with zipfile.ZipFile(archive) as package:
        if package.testzip() is not None:
            raise SystemExit("Corrupt archive.")
        for info in package.infolist():
            path = PurePosixPath(info.filename)
            if path.is_absolute() or ".." in path.parts or "\\" in info.filename:
                raise SystemExit("Unexpected archive path.")
            if stat.S_ISLNK(info.external_attr >> 16):
                raise SystemExit("Unexpected archive symlink.")
        descriptor = json.loads(package.read("ParametricRoads.uplugin"))
        if descriptor["VersionName"] != manifest["version"]:
            raise SystemExit("Descriptor and manifest versions differ.")
        if args.check_only:
            print(f"Ready: {manifest['version']} → {destination}")
            return
        before = preserved_files(mods)
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
        operation = ROOT / ".local/deploy" / stamp
        operation.mkdir(parents=True)
        receipt = {
            "version": manifest["version"], "commit": manifest["commit"],
            "archive": str(archive), "archive_sha256": manifest["sha256"],
            "destination": str(destination), "preserved_sha256": before,
            "state": "prepared", "backup": None,
        }
        receipt_path = operation / "receipt.json"
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
        incoming = mods / (".ParametricRoads-incoming-" + stamp)
        rollback = game / "FactoryGame" / (".ParametricRoads-rollback-" + stamp)
        activated = False
        try:
            incoming.mkdir()
            package.extractall(incoming)
            if destination.exists():
                if not (destination / "ParametricRoads.uplugin").is_file():
                    raise RuntimeError("Existing directory is not identified as ParametricRoads.")
                backup = operation / "ParametricRoads-before"
                shutil.copytree(destination, backup)
                receipt["backup"] = str(backup)
            no_game_running()
            if destination.exists():
                destination.rename(rollback)
            try:
                incoming.rename(destination)
                activated = True
            except BaseException:
                if rollback.exists():
                    rollback.rename(destination)
                raise
            expected = {info.filename: hashlib.sha256(package.read(info)).hexdigest()
                        for info in package.infolist() if not info.is_dir()}
            actual = {str(p.relative_to(destination)): digest(p)
                      for p in destination.rglob("*") if p.is_file()}
            if actual != expected:
                raise RuntimeError("Installed copy differs from the archive.")
            if preserved_files(mods) != before:
                raise RuntimeError("A file outside ParametricRoads changed during installation.")
            if rollback.exists():
                shutil.rmtree(rollback)  # A complete backup is retained in operation/.
            receipt.update(state="installed", installed_sha256=actual,
                           unrelated_mods_settings_saves_unchanged=True)
            print(f"Installed {manifest['version']} in the normal Steam installation.")
            print("Other mods, profiles, settings, blueprints and saves: hashes unchanged.")
        except BaseException as error:
            receipt.update(state="failed", error=str(error))
            if rollback.exists():
                if destination.exists():
                    shutil.move(str(destination), str(operation / "failed-candidate"))
                rollback.rename(destination)
            elif activated and destination.exists():
                shutil.move(str(destination), str(operation / "failed-candidate"))
            raise
        finally:
            if incoming.exists():
                shutil.rmtree(incoming)
            receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
            (ROOT / ".local/deploy/latest-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
