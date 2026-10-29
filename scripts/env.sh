#!/usr/bin/env bash
# Source this file; keep all downloaded SDKs and Wine state inside this project.
SATISFACTORY_DEV_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export SATISFACTORY_DEV_ROOT
# Resolve shared SDK roots: Alpakit/MSBuild project references are relative to
# their real checkout and break when reached through a worktree cache symlink.
if [[ -f "$SATISFACTORY_DEV_ROOT/.local/environment.sh" ]]; then
    source "$SATISFACTORY_DEV_ROOT/.local/environment.sh"
fi
export SATISFACTORY_STARTER="$(realpath -m -- "${SATISFACTORY_STARTER:-$SATISFACTORY_DEV_ROOT/.local/SatisfactoryModLoader}")"
export UE_CSS_ROOT="$(realpath -m -- "${UE_CSS_ROOT:-$SATISFACTORY_DEV_ROOT/.local/UnrealEngineCSS}")"
export UE_WINE_MSVC="$SATISFACTORY_DEV_ROOT/.local/msvc"
export WINEPREFIX="$SATISFACTORY_DEV_ROOT/.local/wineprefix"
export WWISE_CACHE="$SATISFACTORY_DEV_ROOT/.local/wwise-cache"
