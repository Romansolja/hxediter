#!/usr/bin/env bash
# Build → sign → package → hash, in that exact order:
#   1. cmake build
#   2. Ad-hoc codesign the loose .app  ← MUST come before cpack
#   3. cpack copies the now-signed .app into the DMG
#   4. shasum the DMG, write SHA256SUMS.txt
#
# Signing AFTER cpack ships a DMG containing only the linker-signed stub — macOS 15 Sequoia
# refuses to launch that even with right-click → Open.
set -euo pipefail

# Single source of truth: project(... VERSION X.Y.Z ...) in CMakeLists.txt.
# cpack reads the same value into CPACK_PACKAGE_FILE_NAME, so the DMG name and
# this script can never drift. Accepts an optional positional argument as a
# sanity check — useful for catching "I forgot to bump CMakeLists" before
# signing / packaging eats minutes of build time.
CMAKE_VERSION=$(sed -n -E 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' \
                CMakeLists.txt | head -n1)
if [ -z "$CMAKE_VERSION" ]; then
    echo "Failed to parse VERSION from CMakeLists.txt project()." >&2
    exit 1
fi
if [ "${1-}" != "" ] && [ "$1" != "$CMAKE_VERSION" ]; then
    echo "Version mismatch: argument '$1' != CMakeLists.txt '$CMAKE_VERSION'." >&2
    echo "Bump project(... VERSION ...) in CMakeLists.txt, then re-run." >&2
    exit 1
fi
VERSION="$CMAKE_VERSION"
APP="build/hxediter.app"
DMG="build/HxEditer-${VERSION}-macos-arm64.dmg"

# 1. Configure on demand — cmake --build fails on a fresh clone without a populated build/.
if [ ! -f "build/CMakeCache.txt" ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build
if [ ! -d "$APP" ]; then
    echo "Build failed: $APP missing after cmake --build" >&2
    exit 1
fi

# 2. Ad-hoc sign — --deep is fine here; only deprecated with hardened runtime.
codesign --force --deep --sign - "$APP"
codesign --verify --verbose=2 "$APP"

# 3. Package — cpack copies the signed bundle into the DMG.
cmake --build build --target package
if [ ! -f "$DMG" ]; then
    echo "Packaging failed: $DMG missing after cmake --build --target package" >&2
    exit 1
fi

# 4. Hash the DMG. Format matches release.ps1 — rows are concatenable for `shasum -c`.
HASH=$(shasum -a 256 "$DMG" | awk '{print tolower($1)}')
printf '%s  HxEditer-%s-macos-arm64.dmg\n' "$HASH" "$VERSION" > SHA256SUMS.txt

# Surface the commit — a stale build/ could otherwise ship yesterday's binary.
HEAD=$(git rev-parse HEAD 2>/dev/null || echo "")
DIRTY=""
if [ -n "$HEAD" ] && [ -n "$(git status --porcelain 2>/dev/null)" ]; then
    DIRTY=" (dirty)"
fi

echo "Upload to a new v${VERSION} GitHub release:"
echo "  $DMG"
echo "  SHA256SUMS.txt  =  $HASH"
if [ -n "$HEAD" ]; then
    echo "  built from      =  ${HEAD}${DIRTY}"
else
    echo "  built from      =  (git rev-parse HEAD failed)"
fi
