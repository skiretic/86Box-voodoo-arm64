# Remove wchar_t from Title/Status Bar Pipeline

**Branch**: `remove-wchar-title-pipeline` (4 commits, 21 files, net -33 lines)

**Problem**: On macOS, BSD's `swprintf` fails when wide strings contain non-ASCII characters (e.g. ⌘⌫ from `QKeySequence::NativeText`) under the C locale, causing the toolbar speed label to disappear on mouse capture.

**Solution**: Replace `wchar_t` with UTF-8 `char*` throughout the title/status bar pipeline, eliminating the class of bugs entirely.

---

## Commit A: Core Title Pipeline Conversion (11 files)

**API changes:**
- `plat_get_string()` returns `const char*` instead of `wchar_t*` (`plat.h`)
- `ui_window_title()` takes/returns `char*` instead of `wchar_t*` (`ui.h`)

**Qt bridge:**
- `qt_platform.cpp` — String storage `QMap<int, std::wstring>` → `QMap<int, std::string>`, all `.toStdWString()` → `.toUtf8().toStdString()`, `plat_pause()` uses char buffers
- `qt_progsettings.hpp` — Static member type updated
- `qt_ui.cpp` — `QString::fromWCharArray()` → `QString::fromUtf8()`
- `qt_mainwindow.hpp` — `getTitle(char*)` / `getTitleForNonQtThread(char*)`
- `qt_mainwindow.cpp` — `windowTitle().toUtf8()` + `strncpy`

**SDL bridge:**
- `unix_sdl.c` — `char sdl_win_title[512]`, removed `SDL_iconv_string` conversion, direct `SDL_SetWindowTitle`
- `unix.c` — All `L"..."` wide literals → narrow `"..."`, `plat_pause()` uses char buffers

**Core:**
- `86box.c` — `char mouse_msg[3][200]`, rewrote `update_mouse_msg()` with `snprintf` (removed `swprintf`/`mbstowcs`/`#ifdef __APPLE__` workaround), `pc_run()` uses `snprintf`/`strdup`

**Bonus fix (not in original plan):**
- `vnc.c` — `wcstombs()` → `strncpy()` for VNC window title

---

## Commit B: Status Bar Cleanup (4 files)

Removed `ui_sb_set_text_w()` entirely — redundant now that `plat_get_string()` returns narrow strings:
- `vid_svga.c` — 3 call sites → `ui_sb_set_text()`
- `qt_ui.cpp` — Removed function implementation
- `unix.c` — Removed no-op stub
- `ui.h` — Removed declaration

---

## Commit C: Message Box Callers (11 files)

Converted all `swprintf` + `plat_get_string()` callers to `snprintf` + `MBX_ANSI`:
- `86box.c` — `pc_init_modules()` hardware-not-available messages
- `device.c` — Device not available message
- `config.c` — PCap error messages (4 call sites)
- `network.c` — Network driver error message
- `cdrom.c` — CD-ROM hardware message
- `vid_ddc.c` — EDID error (also removed unnecessary `mbstowcs` conversion)
- `prt_ps.c` — Ghostscript/GhostPCL errors
- `prt_escp.c` — Dot-matrix font error
- `hdd.c` — Invalid config error
- `qt_platform.cpp` — Fixed `STRING_EDID_TOO_LARGE` format `%ls` → `%s`
- `plat.h` — Updated comment for `STRING_EDID_TOO_LARGE`

---

## Commit D: Dead Code Cleanup (2 files)

- `version.h.in` — Removed `EMU_GIT_HASH_W`, `EMU_SITE_W`, `EMU_ROMS_URL_W`, `EMU_DOCS_URL_W` (zero callers)
- `86box.c` — Removed `#include <wchar.h>`, converted pclog banner from `EMU_NAME_W`/`EMU_VERSION_FULL_W` to narrow equivalents

**Kept** (still has active callers): `LSTR`, `EMU_NAME_W`, `EMU_VERSION_W`, `EMU_BUILD_W`, `EMU_VERSION_FULL_W` (used by `cdrom_image_viso.c`, `unix.c`, macOS App Translocation), `sizeof_w()` (used by `utils/ini.c`)
