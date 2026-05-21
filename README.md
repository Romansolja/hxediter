# HxEditer — macOS port (beta)

C++17 hex editor (Dear ImGui + GLFW + OpenGL), ported to macOS arm64
from the Windows-only [`main-cpp`](https://github.com/Romansolja/hxediter/tree/main-cpp)
branch.

**Status:** v1 beta. Apple Silicon only. Requires macOS 12.0 (Monterey)
or newer. Ad-hoc signed (Developer ID + notarization deferred).

## Install

1. Grab `HxEditer-X.Y.Z-macos-arm64.dmg` from the
   [Releases](https://github.com/Romansolja/hxediter/releases) page.
2. Mount the DMG, drag `hxediter.app` to `/Applications`.
3. **First launch — Gatekeeper warning.** macOS will refuse to open an
   ad-hoc signed app on its first launch (Developer ID + notarization
   is deferred). Path through the warning depends on your macOS version:

   - **macOS 15 Sequoia or newer:** double-clicking shows *"hxediter
     cannot be opened because Apple could not verify…"* with no Open
     button. Apple removed the right-click bypass in Sequoia. Go to
     **System Settings → Privacy & Security**, scroll down, click
     **Open Anyway** next to the hxediter entry, confirm with Touch ID,
     re-launch.
   - **macOS 13 Ventura / 14 Sonoma:** same warning dialog, but you can
     right-click the .app → **Open** → confirm. Faster than the Settings
     dance.

   One-time only. Subsequent launches work normally on any version.

## What's different from the Windows build

Same core hex editor: paged view for large files, inline byte edit,
undo, search, goto, multi-tab, drag-drop, three palettes, HiDPI.

**Chords accept either Cmd or Ctrl** on macOS, so muscle memory from
either platform works:

| Chord | Action |
|---|---|
| `Cmd+Z`         | Undo last byte edit |
| `Cmd+W`         | Close current tab |
| `Cmd+Tab`       | Next tab (`Shift` to reverse; hold to cycle) |
| `Cmd+1` .. `Cmd+9` | Jump to tab N |
| `Cmd+=`         | Zoom in |
| `Cmd+-`         | Zoom out |
| `Cmd+0`         | Reset zoom to 100% |
| `Cmd+scroll`    | Zoom by trackpad / wheel |
| `Cmd+Shift+P`   | Cycle color palette |
| `F1`            | Toggle help overlay |

**Not yet wired on macOS in v1:**
- Auto-updater (the WinHTTP-bound update flow is Windows-only)
- Folder triage workflow (code stays compiled, button hidden)

## Build

Prereqs: Xcode CLT (`xcode-select --install`) and CMake (`brew install
cmake`). Then:

```
./release.sh 5.1.2
```

Drives build → ad-hoc sign → package → SHA256 in one shot. DMG lands
in `build/`. The sign-before-package order is load-bearing — see
comments in `release.sh` if you need to change it.

## Quirks

- **"Open With → hxediter" from Finder doesn't open the file.** GLFW
  doesn't surface the Apple Event for `kAEOpenDocuments`. Workarounds:
  drag-drop onto a running app, use the Open dialog, or pass the path
  as a CLI arg from Terminal.

- **Many UI issues.**

## License

MIT. See `LICENSE.txt`. Security model documented in
[SECURITY.md](SECURITY.md).
