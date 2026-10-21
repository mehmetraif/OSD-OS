#!/usr/bin/env bash
# The app's --core-info, as the Arcade module runs a copy of the app to read
# the FinalBurn Neo core's version (ArcadeBackend::readOwnVersions()): the
# test core's name and version on its output, told before Qt opens anything
# (env -i: no display, no platform plugin to find), and a file that isn't a
# core refused, saying why.
#   app_core_info_test.sh <osdos> <test core>
# Skipped (77) without the app, which is built apart from the tests (build/).
set -u

APP="$1" CORE="$2"
[ -x "$APP" ] || { echo "SKIP: no app at $APP (build it first, in build/)"; exit 77; }

out=$(env -i PATH="$PATH" "$APP" --core-info "$CORE" 2>&1)
status=$?
[ "$status" -eq 0 ] || { echo "FAIL: --core-info $CORE ended with $status: $out"; exit 1; }
expected=$'name=OSD/OS Test Core\nversion=v1.0.0.03 6e7c2a1'
[ "$out" = "$expected" ] || { echo "FAIL: --core-info printed: $out"; exit 1; }

if out=$(env -i PATH="$PATH" "$APP" --core-info "$0" 2>&1); then
    echo "FAIL: --core-info took a file that isn't a core: $out"
    exit 1
fi
[ -n "$out" ] || { echo "FAIL: --core-info refused a file that isn't a core without saying why"; exit 1; }
echo "PASS"
