# Plan: Remove Remaining wchar_t from 86Box

**Branch**: `remove-wchar-title-pipeline` (continuing existing work)

**Goal**: Eliminate all `wchar_t` from the codebase except where Win32 APIs mandate it.

---

## Current State After Phases A–I

Phases A–I converted the title/status bar pipeline, INI system, message box API, VISO subsystem, and cleaned up dead macros. What remains falls into 3 categories:

1. **~440 dead `#include <wchar.h>` lines** — files that include the header but use nothing from it
2. **Dead wchar_t code** — unused macros and `#if 0` blocks
3. **Live Win32-only wchar_t** — thin wrappers around Win32 W-suffix APIs that immediately convert to narrow

---

## Phase J: Remove Dead `#include <wchar.h>` (~440 files)

The vast majority of files include `<wchar.h>` but don't use `wchar_t`, `swprintf`, `mbstowcs`, `wcstombs`, `WCHAR`, or any other wide-string function or type. This include was likely cargo-culted through the codebase from a template.

**Approach**: Bulk remove `#include <wchar.h>` from every `.c` and `.cpp` file that does NOT contain any of: `wchar_t`, `WCHAR`, `wcscmp`, `wcslen`, `wcsrchr`, `wcstombs`, `mbstowcs`, `swprintf`, `fwprintf`, `fgetws`, `wcsncpy`, `wcscpy`, `wcsdup`, `wcscat`, `sf_wchar_open`, `L"`, `%ls`, `%S`.

**Files to KEEP `#include <wchar.h>`** (confirmed usage):
- `src/cdrom/cdrom_image.c` — `sf_wchar_open`, `mbstowcs` (Win32 path)
- `src/qt/qt_platform.cpp` — `wchar_t` for GetModuleFileNameW, GetSystemWindowsDirectoryW, SetThreadDescription
- `src/qt/win_serial_passthrough.c` — `wchar_t errorMsg_w` for FormatMessageW
- `src/qt/win_cdrom_ioctl.c` — `WCHAR path[256]` for CreateFileW
- `src/qt/win_opendir.c` — may need it for plat_dir.h (verify after Phase K)
- `src/qt/qt_mediamenu.cpp` — verify (may be dead after earlier phases)

**Estimated**: ~435 files modified, 1 line removed per file. Net -435 lines.

**Risk**: LOW — removing an unused include cannot change behavior. Build will catch any file that actually needed it.

**Commit strategy**: Single commit. Message: "Remove dead #include <wchar.h> from ~435 files"

---

## Phase K: Remove Dead wchar_t Code

### K1: Remove `#if 0` block in config.h

`src/include/86box/config.h` lines 29–137 contain `storage_cfg_t` and `config_t` structs inside `#if 0`. This is dead code — no `.c` or `.cpp` file references `config_t` or `storage_cfg_t`. The `wchar_t path[1024]` field in `storage_cfg_t` is the last `wchar_t` in any header.

**Action**: Delete the entire `#if 0` ... `#endif` block (lines 29–138).

### K2: Remove unused VC() macro in qt_main.cpp

`src/qt/qt_main.cpp:89` defines `#define VC(x) const_cast<wchar_t *>(x)` but it is never used anywhere in the file or codebase.

**Action**: Delete line 89.

### K3: Remove dead UNICODE branches in plat_dir.h

`src/include/86box/plat_dir.h` has `#ifdef UNICODE` branches that define `wchar_t d_name[]` and `wchar_t dir[]`. Only 3 files include this header:
- `cdrom_image_viso.c` — does NOT define UNICODE
- `config.c` — does NOT define UNICODE
- `win_opendir.c` — does NOT define UNICODE

The UNICODE branches are dead code. The non-UNICODE `char` branches are what's actually compiled.

**Action**: Remove the `#ifdef UNICODE` / `#else` / `#endif` conditionals, keeping only the `char` versions:
```c
    char d_name[MAXNAMLEN + 1];
    ...
    char dir[MAXDIRLEN + 1];
```

### K4: Remove `#include <wchar.h>` from win_opendir.c

After K3, `plat_dir.h` no longer references `wchar_t`, and `win_opendir.c` uses only narrow string functions (`strcpy`, `strncpy`, `strcat`). The `#include <wchar.h>` becomes dead.

**Action**: Remove `#include <wchar.h>` from `win_opendir.c`.

**Files**: 3 (`config.h`, `qt_main.cpp`, `plat_dir.h`) + 1 (`win_opendir.c`)
**Risk**: LOW
**Commit**: Single commit. "Remove dead wchar_t code: #if 0 block, VC() macro, UNICODE branches"

---

## Phase L: Tighten cdrom_image.c (Optional / Investigate)

`src/cdrom/cdrom_image.c:337–357` uses `sf_wchar_open()` on Windows because libsndfile historically needed `wchar_t*` for non-ASCII paths on Windows. Modern libsndfile (≥1.0.29) supports UTF-8 filenames via `sf_open()` on all platforms.

**Option A (safe)**: Leave as-is. The `#ifdef _WIN32` block with `mbstowcs` + `sf_wchar_open` works and only compiles on Windows. Risk of breaking Windows builds if libsndfile version is older.

**Option B (clean)**: Replace with `sf_open(filename, ...)` on all platforms. Requires confirming that 86Box's minimum libsndfile version supports UTF-8 on Windows.

**Recommendation**: Option A (leave as-is). This is Windows-only code behind `#ifdef _WIN32`. It's 4 lines of platform-specific glue. Not worth the risk.

---

## What MUST Stay (Win32 API surface)

These files contain `wchar_t` / `WCHAR` that cannot be removed because Win32 APIs require wide strings:

| File | Win32 API | Pattern |
|------|-----------|---------|
| `qt_platform.cpp` | `GetModuleFileNameW`, `GetSystemWindowsDirectoryW`, `SetThreadDescription` | Narrow → wide → Win32 → narrow (or vice versa). Thin wrapper, immediately converts. |
| `win_serial_passthrough.c` | `FormatMessageW` | Wide temp buffer, immediately `wcstombs` to narrow. |
| `win_cdrom_ioctl.c` | `CreateFileW`, `wsprintf` | `WCHAR path[256]` in struct, used for device path. Windows CD-ROM IOCTL requires UNICODE. |
| `win_joystick_rawinput.c` | `GetRawInputDeviceInfoW`, `CreateFileW`, `HidD_GetProductString` | WCHAR device names, immediately `WideCharToMultiByte` to narrow `joy->name`. |
| `qt_vmmanager_windarkmodefilter.cpp` | Windows `WM_SETTINGCHANGE` message | `wcscmp(L"ImmersiveColorSet", (wchar_t*)msg->lParam)` — message payload is wchar_t by OS definition. |
| `qt_winrawinputfilter.cpp` | Same as above | Same pattern. |
| `cdrom_image.c` | `sf_wchar_open` (libsndfile Win32) | `#ifdef _WIN32` only. 4 lines. |
| `qt_main.cpp` | `SetCurrentProcessExplicitAppUserModelID`, `FindWindowW` | Qt/Windows interop in `main()`. VC() macro removed but underlying calls stay. |

**Total**: 8 files with legitimate Win32 wchar_t. All are `#ifdef _WIN32` / `#ifdef Q_OS_WINDOWS` guarded — they don't compile on macOS/Linux at all.

---

## Execution Plan

| Phase | Files | Risk | Dependency |
|-------|-------|------|------------|
| J (dead includes) | ~435 | LOW | None |
| K (dead code) | 4 | LOW | None |
| L (cdrom_image) | 1 | MEDIUM | None (optional) |

**J and K can run in parallel** — no file overlap.

Phase L is optional. Recommend skipping unless you want to chase the last 4 lines of platform-specific glue.

---

## After Completion

**wchar_t / WCHAR count**:
- Before Phases A–I: ~30+ files with wchar_t usage, ~440 files with dead includes
- After Phases A–I: ~12 files with wchar_t usage, ~440 files with dead includes
- After Phases J+K: **8 files** with wchar_t, **all Windows-only, all behind #ifdef**
- Zero wchar_t on macOS/Linux codepath

**`#include <wchar.h>` count**:
- Before: ~445
- After: ~6 (only in files that actually use wide functions on Windows)

---

## Verification

- Build on macOS ARM64 (primary — no wchar_t should remain in compiled code)
- Cross-check: `grep -r 'wchar_t\|WCHAR\|wchar\.h' src/ | grep -v '#ifdef _WIN32' | grep -v '#ifdef Q_OS_WIN' | grep -v 'win_'` should return only the 2 dark mode filter files and cdrom_image.c (all Windows-guarded)
