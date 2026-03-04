#!/bin/bash
# Build all FRANK OS standalone apps
# Usage: cd apps && ./build_apps.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FOS_DIR="$SCRIPT_DIR/../sdcard/fos"

mkdir -p "$FOS_DIR"

fail=0
count=0

for app_dir in "$SCRIPT_DIR"/source/*/; do
    [ -f "$app_dir/CMakeLists.txt" ] || continue
    app_name="$(basename "$app_dir")"
    count=$((count + 1))

    echo "=== Building $app_name ==="
    build_dir="$app_dir/build"
    rm -rf "$build_dir"
    mkdir "$build_dir"
    (cd "$build_dir" && cmake .. && make -j4) || { echo "*** FAILED: $app_name"; fail=$((fail + 1)); continue; }

    # Copy .ico to sdcard/fos if present in app source
    if [ -f "$app_dir/$app_name.ico" ]; then
        cp "$app_dir/$app_name.ico" "$FOS_DIR/$app_name.ico"
    fi
    echo ""
done

echo "=== $((count - fail))/$count apps built successfully ==="
[ "$fail" -eq 0 ] || exit 1
