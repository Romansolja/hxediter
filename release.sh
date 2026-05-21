#!/usr/bin/env bash
# macOS sibling of release.ps1. Drives the whole release in one shot:
#   1. Build hxediter.app
#   2. Ad-hoc sign the loose .app  ← MUST happen before cpack
#   3. Run cpack — copies the now-signed .app into the DMG
#   4. SHA256 the DMG, write SHA256SUMS.txt
#
# Order matters: cpack's install rules snapshot build/hxediter.app into
# the DMG's staging dir. If you sign the loose .app AFTER cpack has
# already run (the v1 ordering), the DMG ships with only the compiler's
# linker-signed stub — macOS 15 Sequoia treats that as broken and refuses
# to launch even with right-click → Open. So the build + sign + package
# sequence is fused here rather than asking the user to remember it.
#
# Phase 6 (Developer ID + notarization) will replace the ad-hoc codesign
# call below with a Developer ID sign + xcrun notarytool submit + stapler
# staple sequence — see plan §7.3. The DMG itself will also need to be
# signed in that flow (ad-hoc DMGs don't need this — the .app is the only
# thing macOS checks).
set -euo pipefail

VERSION="${1:?usage: release.sh VERSION}"
APP="build/hxediter.app"
DMG="build/HxEditer-${VERSION}-macos-arm64.dmg"

# 1. Configure if `build/` hasn't been bootstrapped yet. cmake --build
#    fails on a fresh clone ("could not load cache") because it needs
#    the build tree to exist already, so we run cmake's configure step
#    on demand. Subsequent invocations are no-ops (CMakeCache.txt
#    exists, cmake --build is incremental).
if [ ! -f "build/CMakeCache.txt" ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build
if [ ! -d "$APP" ]; then
    echo "Build failed: $APP missing after cmake --build" >&2
    exit 1
fi

# 2. Ad-hoc sign the loose .app. --deep is fine for ad-hoc; Apple only
#    deprecates --deep when combined with hardened runtime (Phase 6).
codesign --force --deep --sign - "$APP"
codesign --verify --verbose=2 "$APP"

# 3. Package — cpack's install step copies the signed bundle into the
#    DMG. Code signatures live in the Mach-O's LC_CODE_SIGNATURE load
#    command + Contents/_CodeSignature/CodeResources, both of which
#    survive a recursive file copy.
cmake --build build --target package
if [ ! -f "$DMG" ]; then
    echo "Packaging failed: $DMG missing after cmake --build --target package" >&2
    exit 1
fi

# 4. Hash the DMG. Same single-line format as release.ps1 so a unified
#    SHA256SUMS.txt could concatenate both rows and `shasum -c` accepts
#    the result on either OS.
HASH=$(shasum -a 256 "$DMG" | awk '{print tolower($1)}')
printf '%s  HxEditer-%s-macos-arm64.dmg\n' "$HASH" "$VERSION" > SHA256SUMS.txt

# Surface the commit so a stale build/ doesn't quietly ship yesterday's
# binary tagged as today's release.
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
