# HxEditer v5

A hex editor for Windows built with Dear ImGui and C++17

## Features

- Fast hex view with inline byte editing and undo
- Drag-and-drop file loading
- Search, goto offset, and keyboard navigation
- Handles files larger than memory (paged view)
- Three color palettes (default, deuteranopia, high-contrast)
- Updater that checks GitHub Releases

## Install

Download the latest `HxEditer-X.Y.Z-win64.exe` from the
[Releases](https://github.com/Romansolja/hxediter/releases) page and run it.
The app will notify you automatically when a new version is out.

Windows only, uses CMake. Build is verified against the MinGW-w64 GCC
toolchain shipped with JetBrains CLion 2026.1 (gcc 13.1.0 at the time
of writing, located at `<CLion install>/bin/mingw/bin/`); newer
versions in the same major series should also work but have not been
exercised. CMake 3.14 or newer required.

## Security

See [SECURITY.md](SECURITY.md) for the threat model, the auto-updater
trust assumptions, and how to report a vulnerability.

## License

MIT. See `LICENSE.txt`.
