#!/usr/bin/env python3
"""Link the tracked plugin into the downloaded starter without copying sources."""
from pathlib import Path
import os

root = Path(__file__).resolve().parent.parent
source = root / "Mods/Roadworks"
starter = Path(os.environ.get("SATISFACTORY_STARTER", root / ".local/SatisfactoryModLoader")).resolve()
link = starter / "Mods/Roadworks"
legacy = starter / "Mods/ParametricRoads"
if legacy.is_symlink() and legacy.resolve() == root / "Mods/ParametricRoads":
    legacy.unlink()
elif legacy.exists() or legacy.is_symlink():
    raise SystemExit(f"Remove or migrate the previous SDK plugin manually: {legacy}")
if not (source / "Roadworks.uplugin").is_file():
    raise SystemExit(f"Tracked plugin not found: {source}")
if link.is_symlink():
    if link.resolve() != source:
        raise SystemExit(f"Existing link points elsewhere: {link}")
elif link.exists():
    raise SystemExit(f"Refusing to replace an existing plugin directory: {link}")
else:
    link.symlink_to(source, target_is_directory=True)
print(f"SDK plugin: {link} -> {source}")
