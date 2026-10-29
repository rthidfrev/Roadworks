#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
python3 "$SATISFACTORY_DEV_ROOT/scripts/link-mod.py"
mkdir -p "$SATISFACTORY_DEV_ROOT/.local/logs"
ulimit -n 524288
package_targets=(-target=FactoryGameSteam)
case "${1:-}" in
    "") ;;
    --release) package_targets=(-merge) ;; # Native Alpakit Release: both Windows stores.
    *) echo "Usage: $0 [--release]" >&2; exit 2 ;;
esac
bash "$UE_CSS_ROOT/Engine/Build/BatchFiles/RunUAT.sh" \
    "-ScriptsForProject=$SATISFACTORY_STARTER/FactoryGame.uproject" \
    PackagePlugin "-Project=$SATISFACTORY_STARTER/FactoryGame.uproject" \
    -DLCName=Roadworks -build "${package_targets[@]}" \
    -clientconfig=Shipping -platform=Win64 -nocompileeditor -installed \
    '-ini:Engine:[/Script/Engine.RendererSettings]:r.Shaders.RemoveUnusedInterpolators=0' \
    2>&1 | tee "$SATISFACTORY_DEV_ROOT/.local/logs/package-mod.log"

# The pinned cross-cooker strips velocity PS inputs without matching the VS
# outputs. Disable that optimization only in this cook, never in the game.
# Inspect the resulting bytecode so build success cannot hide this regression.
monitor_cook="$SATISFACTORY_STARTER/Mods/Roadworks/Saved/Cooked/Windows/FactoryGame/Mods/Roadworks"
monitor_shader_dump="$SATISFACTORY_DEV_ROOT/.local/logs/monitor-shader-stable-keys.log"
"$UE_CSS_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" \
    "$SATISFACTORY_STARTER/FactoryGame.uproject" -run=ShaderPipelineCacheTools Dump \
    "$monitor_cook/Metadata/PipelineCaches/ShaderStableInfo-Roadworks-PCD3D_SM6.shk" \
    -unattended -nullrhi -nosplash -nosound "-abslog=$monitor_shader_dump" \
    > "$SATISFACTORY_DEV_ROOT/.local/logs/monitor-shader-stable-keys.console.log" 2>&1
python3 "$SATISFACTORY_DEV_ROOT/scripts/validate-cooked-monitor-shaders.py" \
    --stable-key-dump "$monitor_shader_dump" \
    --archive "$monitor_cook/Content/ShaderArchive-Roadworks-PCD3D_SM6-PCD3D_SM6.ushaderbytecode" \
    --report "$SATISFACTORY_DEV_ROOT/.local/logs/monitor-shader-verification.json"
