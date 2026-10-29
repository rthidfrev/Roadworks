# Development

## Setup

Use the CSS Unreal engine and SML versions in `environment.lock.json`. Follow the
[official setup guide](https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/index.html)
for the private SDK, toolchain and Wwise integration. Dependencies are installed
separately and must not be redistributed with this repository.

On the supported Linux/Wine setup, configure and source the paths:

```bash
export SATISFACTORY_STARTER=/path/to/SatisfactoryModLoader
export UE_CSS_ROOT=/path/to/UnrealEngineCSS
source scripts/env.sh
```

Defaults are `.local/SatisfactoryModLoader` and `.local/UnrealEngineCSS`.
Optional machine overrides go in ignored `.local/environment.sh`. MSVC/Wine,
Wwise and soundbanks must already be prepared. `link-mod.py` links the authored
plugin into the starter and refuses to replace an unrelated directory.

## Build, test and package

From the repository root, after reviewing the change:

```bash
source scripts/env.sh
python3 scripts/verify-release.py --freeze
bash scripts/build-editor.sh > .local/logs/release-editor-build.console.log 2>&1
bash scripts/run-editor-tests.sh
bash scripts/package-mod.sh --release > .local/logs/release-package.console.log 2>&1
"$UE_CSS_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" \
  "$SATISFACTORY_STARTER/FactoryGame.uproject" -run=pythonscript \
  "-script=$SATISFACTORY_DEV_ROOT/scripts/audit-release-assets.py" \
  -unattended -nullrhi -nosplash -nosound
python3 -m unittest discover -s tests
# Commit the reviewed source, then:
python3 scripts/verify-release.py
```

Do not run simultaneous UBT builds. The editor suite is worldless; it never starts
a game or PIE. The pinned editor may return 1 on completed-queue exit; the runner
checks all expected test names and results. Gameplay tests belong to the maintainer.

`package-mod.sh` alone builds a Steam development ZIP. `--release` uses native
Alpakit `-merge` for both Windows stores. Only Windows client targets are enabled.
The cook-only interpolator override and shader verifier protect the two current
monitor materials' SM6 velocity signatures; no renderer INI is installed.

The final verifier requires unchanged frozen plugin inputs, a clean commit,
completed tests, cooked assets, current shader/asset evidence and matching native
ZIPs with notices. It writes SMR, Windows and source ZIPs plus SHA-256 manifests
to ignored `dist/`. It does not upload. Human save/removal and multiplayer checks
in the [player guide](USER_GUIDE.md) are required before advertising compatibility.

## Local installation

With the game closed, `python3 scripts/install-main-mod.py --check-only` checks the
normal Steam installation. Without that option it updates Roadworks, backs up
and removes the pre-release `ParametricRoads` plugin if present, and verifies
unrelated data. It never launches the game.
Use host-visible process checks, not an isolated `/proc` view. The latest backup
and receipt are recorded in `.local/deploy/latest-receipt.json`.

The plugin, module and content mount are `Roadworks`. Legacy input/volume IDs
and the preset storage folder are retained for existing preferences and files.
Native redirects resolve references from pre-release packages.

## Assets

The [V2 handoff](../art/road-monitor/v2/README.md) describes the Blender source,
baked maps and FBXs. Do not change pivots or coordinate conversion casually.
The committed Unreal assets can be compiled/cooked without regenerating art.
Inspect SDK declarations, not generated FactoryGame C++ bodies, when changing
native integration. Keep source changes compatible with `REQUIREMENTS.md`.
