#!/usr/bin/env bash
# Deploy build/hxediter.app to /Applications and force Launch Services to re-scan.
# Without the lsregister kick, Finder's "Open With → hxediter" can take minutes
# (or a logout) to notice a freshly installed app — annoying when iterating.
#
# This is the dev/iteration path; end users still grab the DMG from Releases.
set -euo pipefail

APP="build/hxediter.app"
DEST="/Applications/hxediter.app"
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

if [ ! -d "$APP" ]; then
    echo "$APP not found — run ./release.sh first." >&2
    exit 1
fi

# rm -rf on a running .app succeeds at the filesystem layer (POSIX unlink),
# but the running process keeps executing the old text segment until quit.
# Without this, install + manual re-launch silently leaves the user on the
# old binary, which is the worst kind of "fix didn't work" bug.
osascript -e 'tell application "hxediter" to quit' 2>/dev/null || true

if [ ! -w /Applications ]; then
    echo "Note: /Applications isn't writable by $(id -un); rerun with sudo if the next step fails." >&2
fi

rm -rf "$DEST"
cp -R "$APP" "$DEST"

# -f forces a re-scan of just this bundle, which is what makes Finder's
# Open-With menu pick up the new install immediately.
"$LSREGISTER" -f "$DEST"

echo "Installed $DEST"
echo
echo "First launch will trip Gatekeeper (ad-hoc signed, not notarized):"
echo "  Sequoia or newer: System Settings → Privacy & Security → Open Anyway"
echo "  Ventura / Sonoma: right-click the .app → Open → confirm"
echo
echo "If Finder's Open With menu doesn't show hxediter yet, run: killall Finder"
echo "(Finder caches the menu per-process; lsregister can't invalidate that cache.)"
