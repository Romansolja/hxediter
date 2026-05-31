# HxEditer — macOS (beta)

C++17 hex editor (Dear ImGui + GLFW + OpenGL) for macOS arm64. Earlier
Windows builds live on the [`main-cpp`](https://github.com/Romansolja/hxediter/tree/main-cpp)
branch.

**Status:** v1 beta. Apple Silicon only. Requires macOS 12.0 (Monterey)
or newer. Ad-hoc signed (Developer ID + notarization deferred).

## Install

1. Grab `HxEditer-X.Y.Z-macos-arm64.dmg` and `SHA256SUMS.txt` from the
   [Releases](https://github.com/Romansolja/hxediter/releases) page.
2. (Optional but recommended — see [SECURITY.md](SECURITY.md).) Verify
   the DMG matches the published checksum:
   ```
   shasum -a 256 -c SHA256SUMS.txt
   ```
3. Mount the DMG, drag `hxediter.app` to `/Applications`.
4. **First launch — Gatekeeper warning.** macOS will refuse to open an
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
either platform works. The one exception is `Cmd+Tab` — macOS reserves
it for the system app switcher, so the keystroke never reaches the app.
Use `Ctrl+Tab` instead.

| Chord | Action |
|---|---|
| `Cmd+Z`         | Undo last byte edit |
| `Cmd+W`         | Close current tab |
| `Ctrl+Tab`      | Next tab (`Shift` to reverse; hold to cycle) |
| `Cmd+1` .. `Cmd+9` | Jump to tab N |
| `Cmd+=`         | Zoom in |
| `Cmd+-`         | Zoom out |
| `Cmd+0`         | Reset zoom to 100% |
| `Cmd+scroll`    | Zoom by trackpad / wheel |
| `Cmd+Shift+P`   | Cycle color palette |
| `F1`            | Toggle help overlay |

**Not yet wired on macOS in v1:**
- Auto-updater — re-download the DMG to update.

## Inspecting a raw device

hxediter can open a block or character device **read-only** to inspect
its raw bytes — handy for peeking at a disk or partition. Drag-drop and
"Open With" only accept ordinary files, so pass the device node on the
command line (reading a disk needs root):

```
sudo /Applications/hxediter.app/Contents/MacOS/hxediter /dev/rdisk0     # whole disk
sudo /Applications/hxediter.app/Contents/MacOS/hxediter /dev/rdisk0s1   # one partition
```

Prefer the raw `rdiskN` node (unbuffered, faster for large reads). The
view is read-only — editing a mounted disk out from under the OS is not
something this tool will do — and reads are block-aligned to satisfy the
raw device. A node with no fixed size (`/dev/null`, `/dev/random`) is
rejected; pipes and sockets are refused outright.

## Build

Prereqs: Xcode CLT (`xcode-select --install`) and CMake (`brew install
cmake`). Then:

```
./release.sh 5.1.2
```

Drives build → ad-hoc sign → package → SHA256 in one shot. DMG lands
in `build/`. The sign-before-package order is load-bearing — see
comments in `release.sh` if you need to change it.

### Tests

Pure-helper and `HexEditorCore` integration tests live under `tests/`
and are wired into CTest. After a configure step:

```
ctest --test-dir build --output-on-failure
```

The same suite runs on every push via [`.github/workflows/build.yml`](.github/workflows/build.yml).

### Install locally from source

```
./install.sh
```

Copies `build/hxediter.app` to `/Applications` and kicks Launch Services
so Finder's "Open With" menu picks it up immediately. End users should
still install from the DMG on the Releases page — this is the dev path.

## License & third-party notices

HxEditer itself is MIT — see [LICENSE.txt](LICENSE.txt). Bundled fonts
and statically linked dependencies (GLFW, Dear ImGui, Font Awesome,
Roboto, JetBrains Mono) keep their own licenses; full attribution lives
in [NOTICES.md](NOTICES.md). Threat model and update policy are in
[SECURITY.md](SECURITY.md).
