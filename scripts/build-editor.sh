#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
python3 "$SATISFACTORY_DEV_ROOT/scripts/link-mod.py"
builder="$UE_CSS_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
if [[ ! -f "$builder" ]]; then
    echo "CSS engine not found; see docs/DEVELOPMENT.md." >&2
    exit 1
fi
mkdir -p "$SATISFACTORY_DEV_ROOT/.local/logs"
bash "$builder" FactoryEditor Linux Development \
    "-project=$SATISFACTORY_STARTER/FactoryGame.uproject" -WaitMutex \
    2>&1 | tee "$SATISFACTORY_DEV_ROOT/.local/logs/build-editor.log"
