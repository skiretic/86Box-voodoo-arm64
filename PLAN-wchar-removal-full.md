# Complete wchar_t Removal from 86Box Codebase

**Branch**: `remove-wchar-full` (off `remove-wchar-title-pipeline`)
**Predecessor**: Commits A-D on `remove-wchar-title-pipeline` (title/status bar pipeline — already done)

## Table of Contents

1. [Scope and Inventory](#1-scope-and-inventory)
2. [Phase E: INI System — Eliminate wdata Field](#2-phase-e-ini-system)
3. [Phase F: Message Box Pipeline — Make MBX_ANSI the Default](#3-phase-f-message-box-pipeline)
4. [Phase G: CD-ROM VISO — UCS-2 Isolation](#4-phase-g-cd-rom-viso)
5. [Phase H: Platform/OS Interop — Windows-Only Wrappers](#5-phase-h-platform-os-interop)
6. [Phase I: Dead Code and Final Cleanup](#6-phase-i-dead-code-cleanup)
7. [Files Affected Summary](#7-files-affected-summary)
8. [Risk Assessment](#8-risk-assessment)
9. [Testing Checklist](#9-testing-checklist)

---

## 1. Scope and Inventory

### What was already removed (Commits A-D)

The title/status bar pipeline is fully converted:
- `plat_get_string()` → returns `const char*`
- `ui_window_title()` → takes/returns `char*`
- `update_mouse_msg()` → uses `snprintf`
- `plat_pause()` → uses `char` buffers
- `ui_sb_set_text_w()` → removed entirely
- All `pc_init_modules()` / device error message boxes → `snprintf` + `MBX_ANSI`
- VNC title → `strncpy` instead of `wcstombs`
- Dead `_W` macros removed from `version.h.in` (kept `EMU_NAME_W`, `EMU_VERSION_W`, `EMU_BUILD_W`, `EMU_VERSION_FULL_W`, `LSTR` — still have callers)

### What still uses wchar_t (19 sites across 15 files)

#### Category A: INI System (1 file, ~30 sites)
- `src/utils/ini.c` — `entry_t.wdata[512]` field, `fgetws()`/`fwprintf()` for INI read/write, `wcslen`/`wcsncpy`/`wcstombs`/`mbstowcs` everywhere, `trim_w()`, `ini_strip_quotes()` operates on both `data` and `wdata`, `ini_section_get_wstring()`, `ini_section_set_wstring()`, every `ini_section_set_*()` calls `mbstowcs()` to sync `wdata`
- `src/include/86box/ini.h` — `ini_section_get_wstring()`, `ini_section_set_wstring()` declarations + convenience macros
- `src/include/86box/config.h` — `config_get_wstring()`, `config_set_wstring()` macros (**zero callers** — dead code)

#### Category B: Message Box Wide Path (3 files, 4 sites)
- `src/unix/unix.c:688` — `ui_msgbox_header()` default header uses `EMU_NAME_W`
- `src/unix/unix.c:711-714` — Non-`MBX_ANSI` path uses `SDL_iconv_string` to convert `wchar_t*` → UTF-8
- `src/unix/unix.c:1344` — `L"No ROMs found."` + `EMU_NAME_W` wide literal in SDL main
- `src/qt/qt_ui.cpp:156-157` — `ui_msgbox_header()` has dual path: `MBX_ANSI` → `QString(char*)` vs default → `QString::fromWCharArray(wchar_t*)`
- `src/include/86box/ui.h:41-42` — `ui_msgbox()`/`ui_msgbox_header()` take `void*` (type-erased, disambiguated by `MBX_ANSI` flag)

#### Category C: CD-ROM / ISO9660 (2 files, ~10 sites)
- `src/cdrom/cdrom_image_viso.c` — `viso_convert_utf8()` decodes UTF-8 → `wchar_t` for UCS-2 output, `viso_write_wstring()` writes `wchar_t` → `uint16_t` (Joliet/UCS-2), file extension checks via `wcsrchr`/`wcslen`, uses `EMU_NAME_W`/`EMU_VERSION_W` for ISO volume descriptor
- `src/cdrom/cdrom_image.c:343-354` — Windows-only `wchar_t filename_w[4096]` + `mbstowcs` + `sf_wchar_open()` for libsndfile

#### Category D: Platform / Windows Interop (5 files, ~8 sites)
- `src/qt/qt_platform.cpp:188-196` — `plat_get_exe_name()` Windows path uses `GetModuleFileNameW` + `wchar_t`
- `src/qt/qt_platform.cpp:759-762` — `plat_get_system_directory()` uses `GetSystemWindowsDirectoryW` + `wchar_t`
- `src/qt/qt_platform.cpp:957-959` — `plat_thread_set_name()` uses `mbstowcs` + `SetThreadDescription` (Windows)
- `src/qt/win_serial_passthrough.c:220-256` — `FormatMessageW` + `swprintf` + `wchar_t` error messages (Windows)
- `src/qt/win_cdrom_ioctl.c:47,835` — `ioctl_t.path` is `WCHAR[256]`, `wsprintf` + `CreateFileW` (Windows)
- `src/qt/qt_main.cpp:89` — `VC()` macro (`const_cast<wchar_t*>`) (Windows)
- `src/qt/qt_main.cpp:626,705-711` — `SetCurrentProcessExplicitAppUserModelID(L"...")`, `FindWindowW`/`FindWindowExW` (Windows)
- `src/qt/qt_util.cpp:79-80` — `RegGetValueW` with `L"..."` registry paths (Windows)
- `src/qt/qt_winrawinputfilter.cpp:378` — `wcscmp(L"ImmersiveColorSet", ...)` (Windows)
- `src/qt/qt_vmmanager_windarkmodefilter.cpp:94` — Same `wcscmp` pattern (Windows)

#### Category E: Structs with wchar_t Fields (2 files)
- `src/include/86box/config.h:42` — `storage_cfg_t.path` is `wchar_t[1024]` (but callers use `char`-style operations — may already be functionally `char` despite the type)
- `src/include/86box/plat_dir.h:32,46` — `dirent.d_name` and `DIR_t.dir` are `wchar_t` when `UNICODE` is defined (Windows/Termux only)

#### Category F: Dead Code / Declarations
- `src/include/86box/86box.h:292` — `pc_reload(wchar_t *fn)` inside `#if 0`
- `src/include/86box/vnc.h:29` — `vnc_take_screenshot(wchar_t *fn)` declaration
- `src/vnc.c:307` — `vnc_take_screenshot(UNUSED(wchar_t *fn))` stub implementation
- `src/include/86box/plat.h:77-78` — `sizeof_w()` macro
- `src/qt/qt_harddiskdialog.cpp:541-553` — `_wfopen` + wide literals inside `#if 0` (dead code)
- `src/floppy/fdd_86f.c:3199` — `plat_fopen(dev->original_file_name, L"wb")` inside `#ifdef D86F_COMPRESS` (may or may not be compiled)

#### Category G: OSD Module
- `src/unix/unix_osd.c:47` — `extern wchar_t sdl_win_title[512]` (should be `char` now after Commit A)
- `src/unix/unix_osd.c:181` — `wcstombs(sdl_win_title_mb, sdl_win_title, 256)` conversion

---

## 2. Phase E: INI System — Eliminate wdata Field

**Risk: HIGH** — The INI system is the config file backbone. Every setting in 86Box flows through it. A bug here corrupts VM configs.

### Background

The `entry_t` struct currently has TWO copies of every config value:
```c
typedef struct entry_t {
    list_t  list;
    char    name[128];
    char    data[512];     // narrow (UTF-8 on non-Windows, ANSI/UTF-8 on Windows)
    wchar_t wdata[512];    // wide copy, always kept in sync
} entry_t;
```

Every setter function (e.g., `ini_section_set_int`) writes to `data` then calls `mbstowcs()` to sync `wdata`. Every getter has both a `_string` (returns `data`) and `_wstring` (returns `wdata`) variant. The INI **read** path uses `fgetws()` (wide stream read), parses into `wdata`, then `wcstombs()` to sync `data`. The INI **write** path converts `name` and `wdata` back to wide with `mbstowcs()`/`fwprintf()`.

**Key insight**: `config_get_wstring` / `config_set_wstring` have **zero callers** anywhere in the codebase. The `ini_section_get_wstring` / `ini_section_set_wstring` functions also have zero external callers (only defined in `ini.c` and declared in `ini.h`). The entire `wdata` field and the wide read/write path exist solely to support INI files that might have been written in a wide encoding — but on non-Windows this is always UTF-8 anyway, and on Windows the `ccs=UTF-8` mode is used when `ANSI_CFG` is not defined.

### Strategy

Convert the INI system to be purely `char`/UTF-8 internally. The wide read/write path is only needed for **backward compatibility with existing config files** that were written in wide format on Windows — but since the `ccs=UTF-8` flag already handles this on Windows, and non-Windows always uses UTF-8, the wide path is redundant.

### Changes

#### File: `src/utils/ini.c`

**Struct change:**
```c
typedef struct entry_t {
    list_t list;
    char   name[128];
    char   data[512];
    // wdata REMOVED
} entry_t;
```

**Read path (`ini_read_ex`):**
- Replace `wchar_t buff[1024]` with `char buff[1024]`
- Replace `fgetws()` with `fgets()`
- Replace all `wcslen(buff)` with `strlen(buff)`
- Replace `L'\n'`, `L'\r'`, `L'\0'`, `L' '`, `L'\t'`, `L'#'`, `L';'`, `L'['`, `L']'`, `L'='` with narrow equivalents
- Replace `wctomb(&(sname[d++]), buff[c++])` with `sname[d++] = buff[c++]`
- Replace `wcsncpy(ne->wdata, ...)` + `wcstombs(ne->data, ne->wdata, ...)` with `strncpy(ne->data, &buff[d], sizeof(ne->data) - 1)`
- Remove `#ifdef __HAIKU__` / `ini_fgetws()` entirely (Haiku fgetws crash workaround becomes moot)
- Remove `#ifdef _WIN32` / `c16stombs` (no more wide data to convert)

**Write path (`ini_write_ex`):**
- Remove `wchar_t wtemp[512]`
- Replace `mbstowcs(wtemp, sec->name, ...)` + `fwprintf(fp, L"...", wtemp)` with `fprintf(fp, "...", sec->name)`
- Replace `fwprintf(fp, L"%ls = %ls\n", wtemp, ent->wdata)` with `fprintf(fp, "%s = %s\n", ent->name, ent->data)`
- On Windows non-ANSI path, keep `ccs=UTF-8` mode flag (handles BOM and encoding automatically)

**Strip quotes (`ini_strip_quotes`):**
- Remove all `ent->wdata[...]` operations — only operate on `ent->data`
- Remove `trim_w(ent->wdata)` — only call `trim(ent->data)`

**Getter/setter functions:**
- Remove `ini_section_get_wstring()` entirely
- Remove `ini_section_set_wstring()` entirely
- In all `ini_section_set_*()` functions: remove the `mbstowcs(ent->wdata, ent->data, ...)` line
- In `ini_section_set_string()`: remove the `mbstowcs` / `mbstoc16s` sync

**Helper functions:**
- Remove `trim_w()` entirely (only operates on `wchar_t*`, no callers after `ini_strip_quotes` conversion)
- Remove `ini_fgetws()` (Haiku workaround, no longer needed)

#### File: `src/include/86box/ini.h`

- Remove `ini_section_get_wstring()` declaration
- Remove `ini_section_set_wstring()` declaration
- Remove `ini_get_wstring` convenience macro
- Remove `ini_set_wstring` convenience macro

#### File: `src/include/86box/config.h`

- Remove `config_get_wstring` macro (zero callers)
- Remove `config_set_wstring` macro (zero callers)

### Windows Compatibility Notes

On Windows, the `ccs=UTF-8` mode in `fopen` makes the C runtime handle BOM detection and encoding conversion automatically. When we switch to `fgets()`/`fprintf()`, the `ccs=UTF-8` mode will still work — it will read/write UTF-8 bytes through the narrow stream functions. The `#if defined(ANSI_CFG) || !defined(_WIN32)` guards should be kept: the ANSI path uses plain `"rt"`/`"wt"`, and the Windows-Unicode path uses `"rt, ccs=UTF-8"`/`"wt, ccs=UTF-8"`.

**CRITICAL**: The Windows `ccs=UTF-8` mode with `fgets()` should read UTF-8 just fine. But we must verify that existing config files written by the old `fwprintf()` path are still readable. Since `fwprintf()` with `ccs=UTF-8` writes UTF-8, and `fgets()` with `ccs=UTF-8` reads UTF-8, this should be transparent. Existing non-BOM files (written in ANSI mode) will also be fine since `fgets` reads raw bytes.

### BOM Handling

The current code has `ini_detect_bom()` which checks for UTF-8 BOM (`EF BB BF`) and skips 3 bytes. This should be preserved. On Windows with `ccs=UTF-8`, the BOM may be handled by the runtime already, but the explicit skip is harmless.

---

## 3. Phase F: Message Box Pipeline — Make MBX_ANSI the Default

**Risk: MEDIUM** — Changes the fundamental message box interface. All callers must be consistent.

### Background

Currently `ui_msgbox()` and `ui_msgbox_header()` accept `void*` parameters. The `MBX_ANSI` flag (0x80) disambiguates:
- Without `MBX_ANSI`: parameters are `wchar_t*`, converted via `QString::fromWCharArray()` (Qt) or `SDL_iconv_string()` (SDL)
- With `MBX_ANSI`: parameters are `char*`, used directly as UTF-8

After Commits A-D, almost all callers already pass `MBX_ANSI`. The remaining non-ANSI callers are:
1. `src/unix/unix.c:688` — default header `EMU_NAME_W` when `!MBX_ANSI && !header`
2. `src/unix/unix.c:711-714` — the `wchar_t*` conversion path in `ui_msgbox_header_common()`
3. `src/unix/unix.c:1344` — `L"No ROMs found."` + `EMU_NAME_W` (SDL UI main)
4. `src/86box.c:1249` — `L"App Translocation"` + `EMU_NAME_W` (macOS only)

### Strategy

1. Convert the remaining 2 callers to narrow strings + `MBX_ANSI`
2. Change `ui_msgbox()` / `ui_msgbox_header()` signatures from `void*` to `const char*`
3. Remove the `MBX_ANSI` flag entirely — ALL callers are now ANSI
4. Remove the wide conversion paths from both Qt and SDL implementations

### Changes

#### File: `src/86box.c`

**Line 1249** — App Translocation warning:
```c
// Before:
ui_msgbox_header(MBX_FATAL, L"App Translocation", EMU_NAME_W L" cannot determine...");
// After:
ui_msgbox_header(MBX_FATAL, "App Translocation", EMU_NAME " cannot determine the emulated machine's location due to a macOS security feature. Please move the " EMU_NAME " app to another folder (not /Applications), or make a copy of it and open that copy instead.");
```

#### File: `src/unix/unix.c`

**Line 688** — Default header:
```c
// Before:
header = (void *) EMU_NAME_W;
// After:
header = (void *) EMU_NAME;
```

**Line 1344** — No ROMs found (SDL UI main):
```c
// Before:
ui_msgbox_header(MBX_FATAL, L"No ROMs found.", EMU_NAME_W L" could not find any usable ROM images.\n\nPlease download a ROM set and extract it into the \"roms\" directory.");
// After:
ui_msgbox_header(MBX_FATAL, "No ROMs found.", EMU_NAME " could not find any usable ROM images.\n\nPlease download a ROM set and extract it into the \"roms\" directory.");
```

**Lines 705-721** — Remove the wide conversion branch entirely. After this, all callers are ANSI, so only the narrow path remains:
```c
// The entire else branch (SDL_iconv_string wchar_t conversion) is removed
// Only the direct UTF-8 path stays
```

#### File: `src/include/86box/ui.h`

Change signatures:
```c
// Before:
extern int ui_msgbox(int flags, void *message);
extern int ui_msgbox_header(int flags, void *header, void *message);
// After:
extern int ui_msgbox(int flags, const char *message);
extern int ui_msgbox_header(int flags, const char *header, const char *message);
```

Remove `MBX_ANSI` define (0x80).

#### File: `src/qt/qt_ui.cpp`

Lines 156-157 — Remove the dual-path logic:
```cpp
// Before:
const auto hdr = (flags & MBX_ANSI) ? QString(static_cast<char*>(header)) : QString::fromWCharArray(static_cast<const wchar_t*>(header));
const auto msg = (flags & MBX_ANSI) ? QString(static_cast<char*>(message)) : QString::fromWCharArray(static_cast<const wchar_t*>(message));
// After:
const auto hdr = QString::fromUtf8(header);
const auto msg = QString::fromUtf8(message);
```

Also update the `flags` masking — currently `int type = flags & 0x1f;` — the `& 0x1f` mask strips `MBX_ANSI`. After removing `MBX_ANSI`, adjust the mask if needed (check all flag values to ensure no collision).

#### All Previous MBX_ANSI Callers

After removing the flag, strip `MBX_ANSI` from all callers converted in Commit C:
- `src/86box.c` — `pc_init_modules()` calls: remove `| MBX_ANSI`
- `src/device.c` — remove `| MBX_ANSI`
- `src/config.c` — remove `| MBX_ANSI`, remove `(void *)` casts
- `src/network/network.c` — remove `| MBX_ANSI`
- `src/cdrom/cdrom.c` — remove `| MBX_ANSI`
- `src/video/vid_ddc.c` — remove `| MBX_ANSI`
- `src/printer/prt_ps.c` — remove `| MBX_ANSI`, remove `(void *)` casts
- `src/printer/prt_escp.c` — remove `| MBX_ANSI`, remove `(void *)` casts
- `src/disk/hdd.c` — remove `| MBX_ANSI`, remove `(void *)` casts
- `src/qt/win_serial_passthrough.c` — needs conversion too (see Phase H)

**Total files modified in this phase: ~14**

### Flag Collision Check

Current flag values in `ui.h`:
- `MBX_INFO` = 0x00
- `MBX_ERROR` = 0x01
- `MBX_QUESTION` = 0x02
- `MBX_WARNING` = 0x03 (estimated)
- `MBX_FATAL` = 0x10 (estimated)
- `MBX_QUESTION_YN` etc.
- `MBX_ANSI` = 0x80

Need to verify exact values. The `0x1f` mask in Qt `showMessage` strips bit 7 (MBX_ANSI). After removing MBX_ANSI, the mask can stay as-is or be simplified.

---

## 4. Phase G: CD-ROM VISO — UCS-2 Isolation

**Risk: MEDIUM** — ISO9660/Joliet requires UCS-2 (big-endian uint16_t). This is a **legitimate** use of wide characters that cannot be fully eliminated — but it can be isolated from `wchar_t`.

### Background

The VISO (Virtual ISO) module creates ISO9660 filesystem images on the fly. It has two output formats:
1. **ISO9660 primary volume descriptor** — uses plain ASCII/narrow `char` via `viso_write_string()`
2. **Joliet supplementary volume descriptor** — uses UCS-2 (big-endian `uint16_t`) via `viso_write_wstring()`

The current code uses `wchar_t` as an intermediate type between UTF-8 filenames and UCS-2 output. The problem: `wchar_t` is 4 bytes on macOS/Linux but 2 bytes on Windows, so the code already has special handling for surrogate pairs (converting UTF-32 codepoints >= U+10000 to UTF-16 surrogates).

### Strategy

Replace `wchar_t` with `uint16_t` (fixed-width UCS-2) as the intermediate type. This:
- Eliminates platform-dependent `wchar_t` size issues
- Makes the UCS-2 intent explicit
- Removes dependency on `<wchar.h>` functions (`wcslen`, `wcsrchr`)

### Changes

#### File: `src/cdrom/cdrom_image_viso.c`

**`viso_convert_utf8()`:**
```c
// Before:
static size_t viso_convert_utf8(wchar_t *dest, const char *src, ssize_t buf_size)
// After:
static size_t viso_convert_utf8(uint16_t *dest, const char *src, ssize_t buf_size)
```
- Change `wchar_t *p = dest` → `uint16_t *p = dest`
- The surrogate pair logic already exists and is correct for `uint16_t`
- On macOS/Linux where `wchar_t` is 4 bytes, this CHANGES behavior — codepoints >= U+10000 now correctly produce surrogate pairs in the output buffer (previously they'd store full codepoints in 32-bit wchar_t slots, which `viso_write_wstring` would then have to handle). With `uint16_t`, the surrogate pair logic in `viso_convert_utf8` is the canonical location.

**`VISO_WRITE_STR_FUNC` macro for `viso_write_wstring`:**
```c
// Before:
VISO_WRITE_STR_FUNC(viso_write_wstring, uint16_t, wchar_t, cpu_to_be16, c > 0xffff)
// After:
VISO_WRITE_STR_FUNC(viso_write_wstring, uint16_t, uint16_t, cpu_to_be16, 0)
```
- The source type changes from `wchar_t` to `uint16_t`
- The bounds check `c > 0xffff` is no longer needed since `uint16_t` can't exceed 0xFFFF
- Change to `0` (always false) for the bounds check

**`viso_fill_fn_joliet()`:**
```c
// Before:
wchar_t utf8dec[len + 1];
// After:
uint16_t utf8dec[len + 1];
```
- Replace `wcsrchr(utf8dec, L'.')` with a simple loop: `uint16_t *ext = NULL; for (size_t i = len; i > 0; i--) { if (utf8dec[i-1] == '.') { ext = &utf8dec[i-1]; break; } }`
- Replace `wcslen(ext)` with a simple loop or inline calculation from `ext` pointer offset

**Volume descriptor section (line ~1080-1135):**
```c
// Before:
viso_write_wstring((uint16_t *) p, EMU_NAME_W, 16, VISO_CHARSET_A);
...
wchar_t wtemp[16];
viso_convert_utf8(wtemp, basename, 16);
viso_write_wstring((uint16_t *) p, wtemp, 16, VISO_CHARSET_D);
...
viso_write_wstring((uint16_t *) p, EMU_NAME_W L" " EMU_VERSION_W L" VIRTUAL ISO", 64, VISO_CHARSET_A);
viso_write_wstring((uint16_t *) p, L"", 64, VISO_CHARSET_D);
```

After conversion:
- `EMU_NAME_W` → Need a UCS-2 conversion. Options:
  - **Option A (simple)**: Create a local `uint16_t` buffer and convert at runtime: `uint16_t emu_name_w[32]; viso_convert_utf8(emu_name_w, EMU_NAME, 32);`
  - **Option B (static)**: Pre-build static UCS-2 arrays (ugly, fragile)
  - **Recommended**: Option A. The overhead is negligible (happens once per VISO creation).
- `L""` (empty wide string) → `(uint16_t[]){0}` or a `uint16_t` zero buffer
- `wchar_t wtemp[16]` → `uint16_t wtemp[16]`

**Helper: `ucs2_strlen()`**
Add a small inline helper:
```c
static inline size_t
ucs2_strlen(const uint16_t *s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}
```

**Helper: `ucs2_rchr()`**
```c
static inline const uint16_t *
ucs2_rchr(const uint16_t *s, uint16_t c)
{
    const uint16_t *last = NULL;
    while (*s) {
        if (*s == c)
            last = s;
        s++;
    }
    return last;
}
```

#### File: `src/cdrom/cdrom_image.c`

**Lines 343-354** — Windows-only `sf_wchar_open`:
```c
#ifdef _WIN32
    wchar_t filename_w[4096];
    mbstowcs(filename_w, filename, 4096);
    audio->file = sf_wchar_open(filename_w, SFM_READ, &audio->info);
#else
    audio->file = sf_open(filename, SFM_READ, &audio->info);
#endif
```
This is a Windows API interop issue — `sf_wchar_open` is libsndfile's wide-char variant for Windows. **Keep as-is** — this is Windows API surface, not internal `wchar_t` usage. Alternatively, could investigate if libsndfile supports UTF-8 paths on Windows (likely not in older versions). **Mark as "won't fix" for now.**

---

## 5. Phase H: Platform/OS Interop — Windows-Only Wrappers

**Risk: LOW** — These are all inside `#ifdef _WIN32` / `#ifdef Q_OS_WINDOWS` blocks. They're Windows API surface — `wchar_t` is the native Windows string type and these calls must use it.

### Strategy

These **cannot and should not** be fully converted to narrow strings because the Windows API (Win32) natively uses `WCHAR`/`wchar_t`. The goal is to:
1. **Isolate** the wide string usage to the thinnest possible wrapper around the Win32 call
2. **Convert** error message output from wide to narrow+`MBX_ANSI` (or narrow-only after Phase F)
3. Leave Win32 API calls (`CreateFileW`, `FindWindowW`, `GetModuleFileNameW`, `SetCurrentProcessExplicitAppUserModelID`, `RegGetValueW`, `SetThreadDescription`, `FormatMessageW`) as-is — they require `wchar_t`

### Changes

#### File: `src/qt/win_serial_passthrough.c` (2 sites)

**Lines 220-225 and 253-257** — Named pipe error messages:
```c
// Before:
wchar_t errorMsg[1024] = { 0 };
wchar_t finalMsg[1024] = { 0 };
FormatMessageW(..., errorMsg, ...);
swprintf(finalMsg, 1024, L"Named Pipe (...): %ls\n", ..., errorMsg);
ui_msgbox(MBX_ERROR | MBX_FATAL, finalMsg);

// After:
wchar_t errorMsg_w[1024] = { 0 };
char    finalMsg[2048] = { 0 };
char    errorMsg[1024] = { 0 };
FormatMessageW(..., errorMsg_w, ...);
wcstombs(errorMsg, errorMsg_w, sizeof(errorMsg));
snprintf(finalMsg, sizeof(finalMsg), "Named Pipe (...): %s\n", ..., errorMsg);
ui_msgbox(MBX_ERROR | MBX_FATAL, finalMsg);  // after Phase F, no MBX_ANSI needed
```

Note: `FormatMessageW` must still use `wchar_t` (Win32 API). We just convert to narrow immediately after.

#### File: `src/qt/win_cdrom_ioctl.c`

**`ioctl_t.path`** is `WCHAR[256]` — must stay wide because `CreateFileW` requires it (line 89). **Leave as-is.** This is a Windows kernel interface.

**Line 835** — `wsprintf(ioctl->path, L"%S", &(drv[8]))` — **Leave as-is.** This converts a narrow drive path to wide for `CreateFileW`.

#### File: `src/qt/qt_platform.cpp`

**Lines 188-196** — `plat_get_exe_name()` Windows path:
```c
wchar_t *temp;
temp = (wchar_t *) calloc(size, sizeof(wchar_t));
GetModuleFileNameW(NULL, temp, size);
c16stombs(s, (uint16_t *) temp, size);
free(temp);
```
**Leave as-is.** This is a thin wrapper around Win32 API — converts wide to narrow immediately.

**Lines 759-762** — `plat_get_system_directory()`:
```c
wchar_t wc[512];
GetSystemWindowsDirectoryW(wc, 512);
QString str = QString::fromWCharArray(wc);
strcpy(outbuf, str.toUtf8());
```
**Leave as-is.** Same pattern — thin Win32 wrapper.

**Lines 957-959** — `plat_thread_set_name()`:
```c
wchar_t wname[2048];
mbstowcs(wname, name, (len >= 1024) ? 1024 : len);
pSetThreadDescription(thread ? (HANDLE) thread : GetCurrentThread(), wname);
```
**Leave as-is.** Win32 API requires wide string.

#### File: `src/qt/qt_main.cpp`

**Line 89** — `#define VC(x) const_cast<wchar_t *>(x)` — Check if this macro is actually used anywhere. If not, remove it. If yes, it's likely Windows-only.

**Line 626** — `SetCurrentProcessExplicitAppUserModelID(L"86Box.86Box")` — **Leave as-is.** Win32 API.

**Lines 705-711** — `FindWindowW` / `FindWindowExW` calls — **Leave as-is.** Win32 API.

#### File: `src/qt/qt_util.cpp`

**Lines 79-80** — `RegGetValueW` with `L"..."` strings — **Leave as-is.** Win32 API.

#### File: `src/qt/qt_winrawinputfilter.cpp`

**Line 378** — `wcscmp(L"ImmersiveColorSet", ...)` — **Leave as-is.** Win32 message processing.

#### File: `src/qt/qt_vmmanager_windarkmodefilter.cpp`

**Line 94** — Same `wcscmp` pattern — **Leave as-is.**

### Summary

Only `win_serial_passthrough.c` has actionable changes (convert error messages to narrow). Everything else is legitimate Win32 API interop that must remain wide.

---

## 6. Phase I: Dead Code and Final Cleanup

**Risk: LOW** — Removing unused declarations and dead code.

### Changes

#### File: `src/include/86box/86box.h`

**Line 292** — `extern void pc_reload(wchar_t *fn);` inside `#if 0`:
- Remove the entire `#if 0` block (it's dead code)

#### File: `src/include/86box/vnc.h`

**Line 29** — `extern void vnc_take_screenshot(wchar_t *fn);`:
- Change to `extern void vnc_take_screenshot(const char *fn);`
- (Or just `char *fn` if the project doesn't use `const` here)

#### File: `src/vnc.c`

**Line 307** — `vnc_take_screenshot(UNUSED(wchar_t *fn))`:
- Change to `vnc_take_screenshot(UNUSED(const char *fn))`

#### File: `src/include/86box/plat.h`

**Lines 77-78** — `sizeof_w()` macro:
- After Phase E removes all callers in `ini.c`, check for any remaining callers
- If zero callers remain, remove the macro entirely

#### File: `src/include/86box/plat_dir.h`

**Lines 32, 46** — `wchar_t` fields under `#ifdef UNICODE`:
- **Leave as-is.** This is Windows/Termux POSIX emulation layer — `UNICODE` is a Windows SDK define that controls whether Win32 API functions use wide chars. This struct mirrors the Win32 `dirent` layout.

#### File: `src/include/86box/config.h`

**Line 42** — `storage_cfg_t.path` is `wchar_t[1024]`:
- Investigation shows that callers on non-Windows platforms use `sprintf(ioctl->path, "%s", drv)` (narrow), while Windows uses `wsprintf(ioctl->path, L"%S", ...)` (wide). This field is dual-use depending on platform.
- **Leave as-is for now.** Converting this would require auditing every storage device path handler. This can be a follow-up task.

#### File: `src/floppy/fdd_86f.c`

**Line 3199** — `plat_fopen(dev->original_file_name, L"wb")` inside `#ifdef D86F_COMPRESS`:
- Change `L"wb"` to `"wb"`. `plat_fopen()` takes `const char*` — the wide literal is a bug (compiles only because of implicit conversion or because D86F_COMPRESS is never defined).

#### File: `src/qt/qt_harddiskdialog.cpp`

**Lines 541-553** — `_wfopen` + wide literals inside `#if 0`:
- **Leave as-is.** Dead code. Could optionally be removed entirely.

#### File: `src/unix/unix_osd.c`

**Line 47** — `extern wchar_t sdl_win_title[512]`:
- Change to `extern char sdl_win_title[512]` (this should have been done in Commit A but was missed)

**Line 181** — `wcstombs(sdl_win_title_mb, sdl_win_title, 256)`:
- Change to `strncpy(sdl_win_title_mb, sdl_win_title, sizeof(sdl_win_title_mb) - 1)`
- Or even better, just use `sdl_win_title` directly since it's now a `char` array

#### File: `src/include/86box/version.h.in`

After all phases complete, check if `EMU_NAME_W`, `EMU_VERSION_W`, `EMU_BUILD_W`, `EMU_VERSION_FULL_W`, and `LSTR` still have callers:
- If VISO is converted to use runtime conversion (Phase G), `EMU_NAME_W` and `EMU_VERSION_W` in `cdrom_image_viso.c` go away
- `unix.c:688` and `unix.c:1344` go away in Phase F
- `86box.c:1249` goes away in Phase F
- `qt_main.cpp:89` (`VC` macro) may or may not still reference wide strings
- Check for any remaining callers. If zero, remove all `_W` macros and `LSTR` from `version.h.in`

#### File: `src/utils/ini.c` (ancillary)

Remove `#include <wchar.h>` if no longer needed after Phase E.

#### File: `src/vnc.c`

Remove `#include <wchar.h>` (line 21) — no longer needed after screenshot signature change.

---

## 7. Files Affected Summary

### By Phase

| Phase | Files Modified | Risk | Description |
|-------|---------------|------|-------------|
| E | 3 | HIGH | INI system: `ini.c`, `ini.h`, `config.h` |
| F | ~14 | MEDIUM | Message box: `ui.h`, `qt_ui.cpp`, `unix.c`, `86box.c`, + ~10 callers |
| G | 1-2 | MEDIUM | VISO: `cdrom_image_viso.c`, (keep `cdrom_image.c`) |
| H | 1 | LOW | Windows: `win_serial_passthrough.c` |
| I | ~8 | LOW | Cleanup: `86box.h`, `vnc.h`, `vnc.c`, `plat.h`, `version.h.in`, `unix_osd.c`, `fdd_86f.c`, `ini.c` |

### Files NOT touched (legitimate wchar_t usage that stays)

These use `wchar_t` because the **Windows API requires it**:
- `src/qt/qt_platform.cpp` — `GetModuleFileNameW`, `GetSystemWindowsDirectoryW`, `SetThreadDescription` (3 sites, all `#ifdef Q_OS_WINDOWS`)
- `src/qt/win_cdrom_ioctl.c` — `ioctl_t.path` (WCHAR), `CreateFileW` (Windows CD-ROM IOCTL)
- `src/qt/qt_main.cpp` — `SetCurrentProcessExplicitAppUserModelID`, `FindWindowW` (Windows shell)
- `src/qt/qt_util.cpp` — `RegGetValueW` (Windows registry)
- `src/qt/qt_winrawinputfilter.cpp` — `wcscmp` on Windows message parameters
- `src/qt/qt_vmmanager_windarkmodefilter.cpp` — Same pattern
- `src/cdrom/cdrom_image.c` — `sf_wchar_open` (libsndfile Windows API)
- `src/include/86box/plat_dir.h` — POSIX dirent emulation under `#ifdef UNICODE`
- `src/include/86box/config.h` — `storage_cfg_t.path` (dual-use, complex to change)

---

## 8. Risk Assessment

### Phase E (INI — HIGH risk)

**What could go wrong:**
1. **Existing config files break** — If a user's `86box.cfg` was written with `fwprintf` + BOM, switching to `fgets` might misread it. Mitigation: Keep the BOM detection and `ccs=UTF-8` mode on Windows.
2. **Non-ASCII config values** — ROM paths, machine names with accented characters. These flow through `ini_section_get_string()` → `data` field, which is already UTF-8 on non-Windows. On Windows, the `ccs=UTF-8` mode ensures UTF-8 read/write.
3. **Double-encoding** — If `data` and `wdata` were ever out of sync, removing `wdata` could expose bugs that were hidden. Unlikely since every setter syncs both.
4. **Haiku compatibility** — Removing `ini_fgetws()` relies on `fgets()` working correctly on Haiku. Standard C library function, should be fine.

**Mitigation:** Test with a config file containing non-ASCII paths (e.g., ROM path with accented characters, machine name with unicode).

### Phase F (Message Box — MEDIUM risk)

**What could go wrong:**
1. **Missed callers** — If any caller still passes `wchar_t*` without `MBX_ANSI`, it will crash or display garbage. Mitigation: grep thoroughly, change signature from `void*` to `const char*` so the compiler catches type mismatches.
2. **Qt string encoding** — Switching from `fromWCharArray` to `fromUtf8` assumes all inputs are valid UTF-8. They should be since `plat_get_string()` and `snprintf` produce UTF-8.

### Phase G (VISO — MEDIUM risk)

**What could go wrong:**
1. **Surrogate pair handling** — Changing from `wchar_t` (4 bytes on macOS) to `uint16_t` changes how codepoints >= U+10000 are stored. The surrogate pair logic in `viso_convert_utf8()` already handles this case, but needs careful testing with filenames containing emoji or rare CJK characters.
2. **UCS-2 output correctness** — Joliet filenames must be valid UCS-2 big-endian. Changing the intermediate type shouldn't affect the final `cpu_to_be16` conversion.

### Phase H (Windows — LOW risk)
Minimal changes. Only `win_serial_passthrough.c` error messages.

### Phase I (Cleanup — LOW risk)
Dead code removal and type fixes. Straightforward.

---

## 9. Testing Checklist

### Phase E (INI System)
- [ ] Create a new VM — verify `86box.cfg` is written correctly
- [ ] Open existing VM — verify all settings load (CPU, video, sound, drives)
- [ ] Change a setting (e.g., video card) — verify it persists after restart
- [ ] VM with non-ASCII ROM path — verify it loads correctly
- [ ] VM with floppy/CD-ROM image path containing spaces or special chars

### Phase F (Message Box)
- [ ] Trigger "hardware not available" error (misconfigure a device) — verify message displays
- [ ] Trigger network/PCap error — verify message displays
- [ ] macOS App Translocation warning (if testable) — verify message
- [ ] SDL UI: "No ROMs found" error (rename roms dir temporarily)
- [ ] Pause overlay shows correctly

### Phase G (VISO)
- [ ] Mount a directory as virtual CD-ROM — verify files are visible in guest OS
- [ ] Files with non-ASCII names (accented, CJK, emoji) — verify Joliet names work
- [ ] Volume descriptor metadata shows correct application name

### Phase H (Windows Interop)
- [ ] Windows only: Named pipe serial port error — verify error message displays
- [ ] (All other Windows changes are untouched)

### Phase I (Cleanup)
- [ ] Build succeeds on macOS ARM64
- [ ] Build succeeds on Linux x86-64 (if CI available)
- [ ] `vnc_take_screenshot` compiles with new signature
- [ ] OSD menu displays title correctly (SDL UI)

### Cross-cutting
- [ ] No compiler warnings related to `wchar_t` conversions
- [ ] `grep -r 'wchar_t' src/` shows only legitimate Win32 API and plat_dir.h usage
- [ ] No `#include <wchar.h>` in files that don't need it

---

## 10. Recommended Commit Order

1. **Commit E**: INI system — eliminate `wdata` (highest impact, do first so issues surface early)
2. **Commit F**: Message box — make `const char*` the only path (depends on E being stable)
3. **Commit G**: VISO — UCS-2 isolation (independent, can be reordered)
4. **Commit H**: Windows serial passthrough error messages (small, independent)
5. **Commit I**: Dead code + final cleanup (always last — sweeps up everything)

Phases G and H are independent of each other and of E/F, so they could be done in parallel. E must come before F (some callers reference INI strings in error messages). I must be last.

---

## 11. What Stays (Permanent wchar_t)

After all phases, `wchar_t` will remain ONLY in:

1. **Windows API interop** (8 files) — `GetModuleFileNameW`, `CreateFileW`, `FindWindowW`, `SetCurrentProcessExplicitAppUserModelID`, `RegGetValueW`, `SetThreadDescription`, `FormatMessageW`, `wcscmp` on Windows messages. These are unavoidable — Windows API is inherently wide-string.

2. **`storage_cfg_t.path`** (`config.h`) — Dual-use field, complex to change, requires auditing all storage device code. Deferred to a future PR.

3. **`plat_dir.h`** — Windows/Termux POSIX dirent emulation under `#ifdef UNICODE`. Part of OS compatibility layer.

4. **`cdrom_image.c`** — `sf_wchar_open()` on Windows (libsndfile API).

5. **`c16stombs` / `mbstoc16s`** (`plat.h`, `qt_platform.cpp`) — UTF-16 ↔ UTF-8 conversion helpers used by the Windows API wrappers above. Kept as utility functions.

Everything else will be pure UTF-8 `char*`.
