# Third-party notices

HxEditer is distributed under the MIT license (see `LICENSE.txt`). It
bundles or links the following third-party works, each governed by its
own license. Their authors retain copyright; the terms below apply to
each bundled work, not to HxEditer as a whole.

---

## Fonts (shipped in `assets/fonts/`, copied into `hxediter.app/Contents/Resources/assets/fonts/` at build time)

### Roboto-Regular.ttf
- **Project:** Roboto
- **Authors:** Christian Robertson / Google LLC
- **License:** Apache License, Version 2.0
- **Source:** https://fonts.google.com/specimen/Roboto
- **License text:** https://www.apache.org/licenses/LICENSE-2.0

### JetBrainsMono-Regular.ttf
- **Project:** JetBrains Mono
- **Authors:** JetBrains s.r.o. and the JetBrains Mono project contributors
- **License:** SIL Open Font License, Version 1.1
- **Source:** https://www.jetbrains.com/lp/mono/
- **License text:** https://openfontlicense.org

### fa-solid-900.ttf
- **Project:** Font Awesome 6 Free (Solid)
- **Authors:** Fonticons, Inc.
- **License:** SIL Open Font License, Version 1.1 (font files)
- **Source:** https://fontawesome.com
- **License text:** https://openfontlicense.org
- **Note:** Only the free-tier Solid font file is bundled. Font Awesome
  icons in the CSS / Pro tiers are not redistributed here.

`include/IconsFontAwesome6.h` (the codepoint constants header) is part
of the `IconFontCppHeaders` project by Juliette Foucaut and Doug Binks
(https://github.com/juliettef/IconFontCppHeaders), distributed under
the Zlib license. The header itself is unmodified.

---

## Build-time dependencies (fetched, not bundled in the release DMG)

Each is pulled by `FetchContent` at configure time and linked statically.
Their source isn't redistributed in the binary release; the headers /
.cpp files are compiled directly into `hxediter`.

### GLFW
- **License:** Zlib
- **Source:** https://github.com/glfw/glfw
- **Pinned commit:** see `CMakeLists.txt`

### Dear ImGui
- **License:** MIT
- **Source:** https://github.com/ocornut/imgui
- **Pinned commit:** see `CMakeLists.txt`

---

When adding or replacing a bundled asset, update this file in the same
commit. License compliance is per-redistribution: anything shipped
inside `hxediter.app` needs its license preserved alongside.
