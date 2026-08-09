#!/bin/bash
# Auto-format C/C++ files for the project.
# Skips third-party libraries and build artifacts.
# Run from the project root: ./tools/auto_format.sh
# To preview files without formatting: DRY_RUN=1 ./tools/auto_format.sh

set -euo pipefail

# Resolve project root from script location.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Locate clang-format: prefer the one in PATH, fallback to CLion bundled binary.
CF="${CF:-}"
if [[ -z "$CF" ]]; then
    if command -v clang-format >/dev/null 2>&1; then
        CF="clang-format"
    else
        CF="/Users/kai/Applications/CLion.app/Contents/plugins/clion-radler/DotFiles/macos-arm64/clang-format"
    fi
fi

if [[ ! -x "$CF" ]]; then
    echo "Error: clang-format not found or not executable: $CF" >&2
    echo "Install clang-format or set CF=/path/to/clang-format" >&2
    exit 1
fi

echo "==> Using clang-format: $CF"

# Build the find command from project root.
find_args=(
    "$ROOT"
    -type f
    \( -name '*.c' -o -name '*.cpp' -o -name '*.cc' -o -name '*.h' -o -name '*.hpp' \)
)

# Third-party directories that must never be touched.
excludes=(
    '*/rt-thread/*'
    '*/packages/*'
    '*/hpm_sdk_samples/*'
    '*/libraries/*'
    '*/.git/*'
    '*/build*/*'
    '*/cmake-build-*/*'
    '*/__pycache__/*'
    '*/.sconsign.dblite'
    '*/rtconfig.h'
)

for ex in "${excludes[@]}"; do
    find_args+=(! -path "$ex")
done

if [[ "${DRY_RUN:-0}" == "1" ]]; then
    echo "==> Dry-run: listing matching files"
    find "${find_args[@]}" -print
else
    echo "==> Formatting project files"
    find "${find_args[@]}" -exec "$CF" -i {} +
fi

echo "==> Done"
