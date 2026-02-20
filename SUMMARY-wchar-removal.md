# Executive Summary: Remove `wchar_t` from 86Box String Pipeline

**Branch**: `remove-wchar-title-pipeline`
**Base**: `upstream/master` (`ca61f28f5`)
**Commits**: 11 (9 implementation + 1 plan/changelog + 1 changelog update)
**Source files modified**: 26 (+253/-443 lines, net -190 lines)
**Build status**: Clean (zero errors, zero warnings on macOS ARM64)

---

## 1. Problem Statement

On macOS, BSD's `swprintf` fails when wide strings contain non-ASCII characters (e.g. the ⌘⌫ symbols produced by `QKeySequence::NativeText`) under the C locale. This caused a user-visible bug: the toolbar speed label disappeared whenever the emulator captured the mouse.

A targeted `#ifdef __APPLE__` workaround existed, but the real problem was systemic — `wchar_t` was threaded through the title bar, status bar, message box, INI config, and ISO image pipelines despite all platforms using UTF-8 internally. The wide strings served no purpose on non-Windows platforms and created unnecessary conversion overhead and platform-dependent behavior everywhere.

---

## 2. Solution

Replace `wchar_t` with UTF-8 `char*` across the entire non-Windows-API string pipeline. This was done in 9 incremental commits (A through I), each leaving the tree in a buildable state.

---

## 3. Commits

### Commit A: Core Title Pipeline Conversion (11 files)

The central fix. Converted the entire title bar data path from `wchar_t` to `char*`:

- **`plat.h`** — `plat_get_string()` return type: `wchar_t*` → `const char*`
- **`ui.h`** — `ui_window_title()` parameter/return: `wchar_t*` → `char*`
- **`qt_platform.cpp`** — String storage `QMap<int, std::wstring>` → `QMap<int, std::string>`, all `.toStdWString()` → `.toUtf8().toStdString()`, `plat_pause()` converted to char buffers
- **`qt_progsettings.hpp`** — Static member type updated to match
- **`qt_ui.cpp`** — `QString::fromWCharArray()` → `QString::fromUtf8()`
- **`qt_mainwindow.hpp`** / **`qt_mainwindow.cpp`** — `getTitle(char*)` / `getTitleForNonQtThread(char*)`, uses `.toUtf8()` + `strncpy`
- **`unix_sdl.c`** — `char sdl_win_title[512]`, removed `SDL_iconv_string` wide-to-narrow conversion, direct `SDL_SetWindowTitle`
- **`unix.c`** — All `L"..."` wide literals → narrow `"..."`, `plat_pause()` converted to char buffers
- **`86box.c`** — `char mouse_msg[3][200]`, rewrote `update_mouse_msg()` with `snprintf`, removed `swprintf`/`mbstowcs` and the `#ifdef __APPLE__` workaround that motivated this work. `pc_run()` uses `snprintf`/`strdup`
- **`vnc.c`** — `wcstombs()` → `strncpy()` for VNC window title (bonus fix)

### Commit B: Status Bar Cleanup (4 files)

Removed `ui_sb_set_text_w()` — redundant now that `plat_get_string()` returns narrow strings:

- **`vid_svga.c`** — 3 call sites switched to `ui_sb_set_text()`
- **`qt_ui.cpp`** — Removed function implementation
- **`unix.c`** — Removed no-op stub
- **`ui.h`** — Removed declaration

### Commit C: Message Box Callers (11 files)

Converted all `swprintf` + `plat_get_string()` callers to `snprintf` + `MBX_ANSI`:

- **`86box.c`** — `pc_init_modules()` hardware-not-available messages (8 call sites)
- **`device.c`** — Device not available message
- **`config.c`** — PCap error messages (4 call sites)
- **`network.c`** — Network driver error message
- **`cdrom.c`** — CD-ROM hardware message
- **`vid_ddc.c`** — EDID error (also removed unnecessary `mbstowcs` conversion)
- **`prt_ps.c`** — Ghostscript/GhostPCL errors (2 call sites)
- **`prt_escp.c`** — Dot-matrix font error
- **`hdd.c`** — Invalid config error
- **`qt_platform.cpp`** — Fixed `STRING_EDID_TOO_LARGE` format `%ls` → `%s`
- **`plat.h`** — Updated comment for `STRING_EDID_TOO_LARGE`

### Commit D: Dead Wide String Macros (2 files)

- **`version.h.in`** — Removed `EMU_GIT_HASH_W`, `EMU_SITE_W`, `EMU_ROMS_URL_W`, `EMU_DOCS_URL_W` (zero callers)
- **`86box.c`** — Removed `#include <wchar.h>`, converted pclog banner from `EMU_NAME_W`/`EMU_VERSION_FULL_W` to narrow equivalents

### Commit E: INI System — Eliminate wdata Field (3 files, net -153 lines)

The highest-risk change. The INI system (`ini.c`) maintained a redundant `wchar_t wdata[512]` copy of every config value alongside the `char data[512]` field. Every setter synced both via `mbstowcs()`. Every reader parsed wide streams via `fgetws()` then converted back to narrow. This commit eliminates the entire wide path:

- **`ini.c`** — Removed `entry_t.wdata[512]` field, converted `fgetws()` → `fgets()`, `fwprintf()` → `fprintf()`, removed `trim_w()` (48-line wide trim function), removed `ini_fgetws()` (Haiku crash workaround), removed `ini_section_get_wstring()`, `ini_section_set_wstring()`, removed `mbstowcs`/`wcstombs` sync from all 8 setter functions, removed `#include <wchar.h>` and `<wctype.h>`
- **`ini.h`** — Removed `ini_section_get_wstring()`/`ini_section_set_wstring()` declarations and `ini_get_wstring`/`ini_set_wstring` convenience macros
- **`config.h`** — Removed `config_get_wstring`/`config_set_wstring` macros (zero callers — dead code)

Preserved: `ini_detect_bom()`, `ccs=UTF-8` mode on Windows, `ANSI_CFG` compilation guards.

### Commit F: Message Box API — const char*, Remove MBX_ANSI (12 files)

Changed the message box API from type-erased `void*` (disambiguated by `MBX_ANSI` flag) to type-safe `const char*`:

- **`ui.h`** — Removed `#define MBX_ANSI 0x80`, changed `ui_msgbox`/`ui_msgbox_header` signatures from `void*` to `const char*`
- **`qt_ui.cpp`** — Replaced dual-path `MBX_ANSI` ternary logic with direct `QString::fromUtf8()` calls, removed `fromWCharArray` and `static_cast<wchar_t*>`
- **`unix.c`** — Removed entire wide-string conversion branch (`SDL_iconv_string`, `wcslen`, `free`), converted "No ROMs found" to narrow, simplified default header
- **`86box.c`** — Converted App Translocation warning from `L"..."` / `EMU_NAME_W` to narrow strings, stripped `| MBX_ANSI` from 8 calls
- **`device.c`**, **`config.c`**, **`network.c`**, **`cdrom.c`**, **`vid_ddc.c`**, **`prt_ps.c`**, **`prt_escp.c`**, **`hdd.c`** — Stripped `| MBX_ANSI` and `(void *)` casts from all callers

**Bonus bug fix**: `unix.c` had a pre-existing bug where `msgflags` (always 0) was tested instead of `flags` for the message box icon type — the SDL path never displayed the correct icon. Fixed as part of this cleanup.

### Commit G: VISO — UCS-2 Isolation (1 file)

The Virtual ISO module generates ISO9660/Joliet filesystem images on the fly. Joliet requires UCS-2 (big-endian `uint16_t`). The code used `wchar_t` as an intermediate type, which is 4 bytes on macOS/Linux vs 2 bytes on Windows — creating platform-dependent behavior in surrogate pair handling.

- **`cdrom_image_viso.c`** — Replaced all `wchar_t` with `uint16_t` (fixed-width UCS-2):
  - `viso_convert_utf8()` — parameter and locals changed to `uint16_t*`
  - `viso_write_wstring` macro — source type changed, removed unnecessary `c > 0xffff` bounds check (uint16_t cannot exceed 0xFFFF)
  - `viso_fill_fn_joliet()` — `wcsrchr`/`wcslen` replaced with new `ucs2_rchr`/`ucs2_strlen` helpers
  - Volume descriptor section — `EMU_NAME_W`/`EMU_VERSION_W` replaced with runtime `viso_convert_utf8()` calls
  - Added `ucs2_strlen()` and `ucs2_rchr()` static inline helper functions
  - Removed `#include <wchar.h>`

### Commit H: Windows Serial Passthrough Error Messages (1 file)

- **`win_serial_passthrough.c`** — Two error message sites (`open_pseudo_terminal()` and `connect_named_pipe_client()`) converted from `wchar_t` + `swprintf` to `char` + `snprintf`. `FormatMessageW` remains (Win32 API requires `wchar_t`) but its output is immediately converted to narrow via `wcstombs` before being passed to `ui_msgbox`.

### Commit I: Dead Code and Final Cleanup (7 files, +5/-21 lines)

Swept up all remaining dead `wchar_t` infrastructure:

- **`86box.h`** — Removed `#if 0` block containing dead `pc_reload(wchar_t *fn)` declaration
- **`vnc.h`** + **`vnc.c`** — Changed `vnc_take_screenshot` signature to `const char *fn`, removed `#include <wchar.h>`
- **`plat.h`** — Removed `sizeof_w()` macro (zero callers remaining after Phase E removed its last user in `ini.c`)
- **`unix_osd.c`** — Fixed stale extern from `wchar_t sdl_win_title[512]` to `char sdl_win_title[512]` (the actual variable was converted in Commit A but this extern was missed), eliminated the `sdl_win_title_mb` intermediate buffer and `wcstombs` call
- **`fdd_86f.c`** — Fixed `L"wb"` → `"wb"` (wide string literal passed to `plat_fopen()` which takes `const char*` — a latent bug inside `#ifdef D86F_COMPRESS`)
- **`version.h.in`** — Removed `_LSTR`, `LSTR`, `EMU_NAME_W`, `EMU_VERSION_W`, `EMU_BUILD_W`, `EMU_VERSION_FULL_W` (all confirmed zero callers via full codebase search)

---

## 4. Files Modified

| File | Commits | Description |
|------|---------|-------------|
| `src/86box.c` | A, C, D, F | Mouse messages, error boxes, App Translocation, pclog banner |
| `src/cdrom/cdrom.c` | C, F | CD-ROM error message |
| `src/cdrom/cdrom_image_viso.c` | G | VISO wchar_t → uint16_t |
| `src/config.c` | C, F | PCap error messages |
| `src/device.c` | C, F | Device error message |
| `src/disk/hdd.c` | C, F | HDD config error |
| `src/floppy/fdd_86f.c` | I | L"wb" bug fix |
| `src/include/86box/86box.h` | I | Dead pc_reload declaration |
| `src/include/86box/config.h` | E | Dead wstring macros |
| `src/include/86box/ini.h` | E | Removed wstring API |
| `src/include/86box/plat.h` | C, I | STRING_EDID comment, sizeof_w removal |
| `src/include/86box/ui.h` | B, F | Removed ui_sb_set_text_w, MBX_ANSI, void* signatures |
| `src/include/86box/version.h.in` | D, I | Removed all _W macros and LSTR |
| `src/include/86box/vnc.h` | I | vnc_take_screenshot signature |
| `src/network/network.c` | C, F | Network error message |
| `src/printer/prt_escp.c` | C, F | Dot-matrix font error |
| `src/printer/prt_ps.c` | C, F | Ghostscript errors |
| `src/qt/qt_mainwindow.cpp` | A | getTitle → char* |
| `src/qt/qt_mainwindow.hpp` | A | getTitle → char* |
| `src/qt/qt_platform.cpp` | A, C | String storage, plat_pause, EDID format |
| `src/qt/qt_progsettings.hpp` | A | Static member type |
| `src/qt/qt_ui.cpp` | A, B, F | fromWCharArray removal, ui_sb_set_text_w removal, MBX_ANSI removal |
| `src/qt/win_serial_passthrough.c` | H | Error messages to narrow |
| `src/unix/unix.c` | A, B, F | Wide literals, plat_pause, msgbox wide path, No ROMs |
| `src/unix/unix_osd.c` | I | Stale wchar_t extern |
| `src/unix/unix_sdl.c` | A | sdl_win_title to char |
| `src/utils/ini.c` | E | wdata elimination (-153 lines) |
| `src/video/vid_ddc.c` | C, F | EDID error message |
| `src/video/vid_svga.c` | B | ui_sb_set_text_w → ui_sb_set_text |
| `src/vnc.c` | A, I | VNC title, screenshot signature, wchar.h removal |

---

## 5. Bugs Fixed (Pre-existing)

Two bugs were discovered and fixed incidentally during this work:

1. **SDL message box icon never displayed correctly** (`unix.c`): The function tested `msgflags` (a local variable initialized to 0 and never written) instead of `flags` (the parameter) when choosing the SDL message box icon type. Every SDL message box displayed as `SDL_MESSAGEBOX_INFORMATION` regardless of whether it was an error, warning, or fatal. Fixed in Commit F.

2. **Wide string literal passed to narrow function** (`fdd_86f.c`): `plat_fopen(dev->original_file_name, L"wb")` passed a `wchar_t*` literal to a function expecting `const char*`. This was inside `#ifdef D86F_COMPRESS` (possibly never compiled), but was a type error regardless. Fixed in Commit I.

---

## 6. API Changes

### Removed APIs
| API | Location | Replacement |
|-----|----------|-------------|
| `ui_sb_set_text_w(wchar_t*)` | `ui.h` | `ui_sb_set_text(char*)` (already existed) |
| `ini_section_get_wstring()` | `ini.h` | `ini_section_get_string()` (already existed) |
| `ini_section_set_wstring()` | `ini.h` | `ini_section_set_string()` (already existed) |
| `config_get_wstring` macro | `config.h` | `config_get_string` (already existed) |
| `config_set_wstring` macro | `config.h` | `config_set_string` (already existed) |
| `MBX_ANSI` flag (0x80) | `ui.h` | No longer needed — all callers pass `char*` |
| `sizeof_w()` macro | `plat.h` | Standard `sizeof()` or direct calculation |
| `LSTR` / `_LSTR` macros | `version.h.in` | Not needed — no `_W` macros remain |
| `EMU_NAME_W` | `version.h.in` | `EMU_NAME` (narrow, already existed) |
| `EMU_VERSION_W` | `version.h.in` | `EMU_VERSION` (narrow, already existed) |
| `EMU_BUILD_W` | `version.h.in` | `EMU_BUILD` (narrow, already existed) |
| `EMU_VERSION_FULL_W` | `version.h.in` | `EMU_VERSION_FULL` (narrow, already existed) |
| `EMU_GIT_HASH_W` | `version.h.in` | `EMU_GIT_HASH` (narrow, already existed) |
| `EMU_SITE_W` | `version.h.in` | `EMU_SITE` (narrow, already existed) |
| `EMU_ROMS_URL_W` | `version.h.in` | `EMU_ROMS_URL` (narrow, already existed) |
| `EMU_DOCS_URL_W` | `version.h.in` | `EMU_DOCS_URL` (narrow, already existed) |

### Changed Signatures
| Function | Before | After |
|----------|--------|-------|
| `plat_get_string(int)` | Returns `wchar_t*` | Returns `const char*` |
| `ui_window_title(...)` | Takes/returns `wchar_t*` | Takes/returns `char*` |
| `ui_msgbox(int, ...)` | Takes `void *message` | Takes `const char *message` |
| `ui_msgbox_header(int, ...)` | Takes `void *header, void *message` | Takes `const char *header, const char *message` |
| `vnc_take_screenshot(...)` | Takes `wchar_t *fn` | Takes `const char *fn` |

---

## 7. What Remains (Intentional)

After all commits, `wchar_t` remains **only** where the Windows API requires it. These are all inside `#ifdef _WIN32` / `#ifdef Q_OS_WINDOWS` guards and cannot be converted because the Win32 API is natively wide-string:

| File | Usage | Win32 API |
|------|-------|-----------|
| `qt_platform.cpp` | 3 sites | `GetModuleFileNameW`, `GetSystemWindowsDirectoryW`, `SetThreadDescription` |
| `win_cdrom_ioctl.c` | `ioctl_t.path` (WCHAR[256]) | `CreateFileW` |
| `win_serial_passthrough.c` | `errorMsg_w` (temp buffer) | `FormatMessageW` (output immediately converted to narrow) |
| `qt_main.cpp` | Wide string literals | `SetCurrentProcessExplicitAppUserModelID`, `FindWindowW`, `FindWindowExW` |
| `qt_util.cpp` | Wide registry paths | `RegGetValueW` |
| `qt_winrawinputfilter.cpp` | `wcscmp` | Windows color change message comparison |
| `qt_vmmanager_windarkmodefilter.cpp` | `wcscmp` | Same pattern |
| `cdrom_image.c` | `wchar_t filename_w[4096]` | `sf_wchar_open` (libsndfile Windows API) |
| `plat_dir.h` | `dirent.d_name`, `DIR_t.dir` | POSIX dirent emulation under `#ifdef UNICODE` |
| `config.h` | `storage_cfg_t.path` (wchar_t[1024]) | Dual-use field (complex to change, deferred) |

---

## 8. Risk Assessment and Testing Notes

### High Risk (Commits A, E)
- **Title pipeline** (A): Affects what the user sees in the window title, pause overlay, and mouse capture label on every platform
- **INI system** (E): Every VM configuration setting flows through `ini.c` — a bug here corrupts configs silently

### Medium Risk (Commits C, F, G)
- **Message box callers** (C, F): Error messages for hardware-not-found, network, CD-ROM, printer, display. Incorrect conversion could show garbage or crash
- **VISO** (G): ISO9660 Joliet filenames — incorrect UCS-2 encoding would make virtual CD-ROM contents unreadable in guest OS

### Low Risk (Commits B, D, H, I)
- Dead code removal, type fixes, and format string corrections

### Recommended Manual Testing
- [ ] Launch emulator → verify window title displays correctly
- [ ] Capture mouse → verify speed label shows (the original bug)
- [ ] Pause → verify " - PAUSED" suffix appears in title
- [ ] Open existing VM → verify all settings load from `86box.cfg`
- [ ] Change a setting → verify it persists after restart
- [ ] Trigger hardware-not-available error → verify message box displays
- [ ] Mount directory as virtual CD-ROM → verify files visible in guest OS (Joliet names)
- [ ] SDL UI: rename roms directory → verify "No ROMs found" error displays
- [ ] Status bar: verify monitor sleep/wake messages (vid_svga.c)
- [ ] VNC: verify window title displays correctly
