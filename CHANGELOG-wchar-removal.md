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

---

## Commit E: INI System — Eliminate wdata Field (3 files, net -153 lines)

Converted the INI system to be purely `char`/UTF-8 internally:
- `ini.c` — Removed `entry_t.wdata[512]` field, converted `fgetws()`→`fgets()`, `fwprintf()`→`fprintf()`, removed `trim_w()`, `ini_fgetws()` (Haiku workaround), `ini_section_get_wstring()`, `ini_section_set_wstring()`, all `mbstowcs`/`wcstombs` sync in setters, `#include <wchar.h>` and `<wctype.h>`
- `ini.h` — Removed `ini_section_get_wstring()`/`ini_section_set_wstring()` declarations and `ini_get_wstring`/`ini_set_wstring` convenience macros
- `config.h` — Removed `config_get_wstring`/`config_set_wstring` macros (zero callers)

**Preserved**: `ini_detect_bom()`, `ccs=UTF-8` mode on Windows, `ANSI_CFG` guards

---

## Commit F: Message Box API — const char*, Remove MBX_ANSI (12 files)

Changed `ui_msgbox`/`ui_msgbox_header` signatures from `void*` to `const char*` and removed the `MBX_ANSI` flag entirely:
- `ui.h` — Removed `#define MBX_ANSI 0x80`, changed signatures to `const char*`
- `qt_ui.cpp` — Replaced dual-path `MBX_ANSI` ternary with direct `QString::fromUtf8()`
- `unix.c` — Removed wide conversion branch (`SDL_iconv_string`), converted "No ROMs found" to narrow, fixed `msgflags` vs `flags` bug in icon selection
- `86box.c` — Converted App Translocation warning to narrow strings, stripped `| MBX_ANSI` from 8 calls
- `device.c`, `config.c`, `network.c`, `cdrom.c`, `vid_ddc.c`, `prt_ps.c`, `prt_escp.c`, `hdd.c` — Stripped `| MBX_ANSI` and `(void *)` casts

**Bonus fix**: `unix.c` had a pre-existing bug where `msgflags` (always 0) was tested instead of `flags` for message box icon type

---

## Commit G: VISO — UCS-2 Isolation (1 file)

Replaced `wchar_t` with `uint16_t` in `cdrom_image_viso.c` for platform-independent UCS-2:
- `viso_convert_utf8()` — `wchar_t*` → `uint16_t*` parameter and locals
- `viso_write_wstring` macro — source type `wchar_t` → `uint16_t`, removed unnecessary `c > 0xffff` check
- `viso_fill_fn_joliet()` — `wchar_t` → `uint16_t` array, `wcsrchr`/`wcslen` → `ucs2_rchr`/`ucs2_strlen` helpers
- Volume descriptor — `EMU_NAME_W`/`EMU_VERSION_W` → runtime `viso_convert_utf8()` calls, `L""` → `static const uint16_t empty_ucs2[]`
- Added `ucs2_strlen()` and `ucs2_rchr()` helper functions
- Removed `#include <wchar.h>`

---

## Commit H: Windows Serial Passthrough Error Messages (1 file)

Converted `win_serial_passthrough.c` error messages from wide to narrow:
- `open_pseudo_terminal()` — `wchar_t errorMsg` → `wchar_t errorMsg_w` (for `FormatMessageW`) + `char errorMsg` (narrow via `wcstombs`), `swprintf` → `snprintf`
- `connect_named_pipe_client()` — same pattern
- `FormatMessageW` stays (Win32 API requires `wchar_t`), conversion happens immediately after

---

## Commit I: Dead Code and Final Cleanup (7 files, +5/-21 lines)

Removed all remaining dead `wchar_t` infrastructure:
- `86box.h` — Removed `#if 0` block with `pc_reload(wchar_t *fn)` dead code
- `vnc.h` + `vnc.c` — Changed `vnc_take_screenshot` signature to `const char *fn`, removed `#include <wchar.h>`
- `plat.h` — Removed `sizeof_w()` macro (zero callers after Phase E)
- `unix_osd.c` — Fixed extern to `char sdl_win_title[512]`, eliminated `sdl_win_title_mb` intermediate buffer
- `fdd_86f.c` — Fixed `L"wb"` → `"wb"` bug in `#ifdef D86F_COMPRESS` block
- `version.h.in` — Removed `LSTR`, `_LSTR`, `EMU_NAME_W`, `EMU_VERSION_W`, `EMU_BUILD_W`, `EMU_VERSION_FULL_W` (all zero callers)

---

## Summary

**10 commits, ~30 files modified** across the full pipeline:

| Commit | Scope | Risk | Net Lines |
|--------|-------|------|-----------|
| A | Core title pipeline | HIGH | ~-33 |
| B | Status bar cleanup | LOW | small |
| C | Message box callers | MEDIUM | small |
| D | Dead `_W` macros | LOW | small |
| E | INI system wdata | HIGH | -153 |
| F | Message box API const char* | MEDIUM | ~12 files |
| G | VISO UCS-2 isolation | MEDIUM | 1 file |
| H | Windows serial error msgs | LOW | 1 file |
| I | Dead code + final cleanup | LOW | -21 |

**Remaining `wchar_t`** (intentional — Windows API surface only):
- `qt_platform.cpp` — `GetModuleFileNameW`, `GetSystemWindowsDirectoryW`, `SetThreadDescription`
- `win_cdrom_ioctl.c` — `CreateFileW`, `ioctl_t.path` (WCHAR)
- `win_serial_passthrough.c` — `FormatMessageW` (output immediately converted to narrow)
- `qt_main.cpp` — `SetCurrentProcessExplicitAppUserModelID`, `FindWindowW`
- `qt_util.cpp` — `RegGetValueW`
- `qt_winrawinputfilter.cpp`, `qt_vmmanager_windarkmodefilter.cpp` — `wcscmp` on Windows messages
- `cdrom_image.c` — `sf_wchar_open` (libsndfile Windows API)
- `plat_dir.h` — POSIX dirent under `#ifdef UNICODE`
- `config.h` — `storage_cfg_t.path` (dual-use, deferred)
