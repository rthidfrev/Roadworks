#!/usr/bin/env bash
# Existing worldless tests only. No map, PIE or game session is started.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
mkdir -p "$SATISFACTORY_DEV_ROOT/.local/logs"
test_log="$SATISFACTORY_DEV_ROOT/.local/logs/release-editor-tests.log"
set +e
"$UE_CSS_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" \
    "$SATISFACTORY_STARTER/FactoryGame.uproject" \
    -unattended -RenderOffscreen -nosplash -nosound \
    '-ExecCmds=Automation RunTests ParametricRoads.Editor' \
    '-TestExit=Automation Test Queue Empty' "-abslog=$test_log" \
    > "$SATISFACTORY_DEV_ROOT/.local/logs/release-editor-tests.console.log" 2>&1
test_exit=$?
set -e
# The pinned editor's TestExit returns 1 on the completed-queue path. The
# report, rather than that exit code alone, establishes that every test ran.
python3 "$SATISFACTORY_DEV_ROOT/scripts/verify-release.py" \
    --tests-only --test-log "$test_log" --editor-exit "$test_exit"
