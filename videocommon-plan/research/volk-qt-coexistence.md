# Volk + Qt Vulkan Coexistence Research

**Date**: 2026-03-01
**Status**: Complete
**Relevance**: VideoCommon Phase 2+ (Voodoo GPU rendering)

---

## Executive Summary

**Finding: There is NO conflict between volk and Qt Vulkan in the current 86Box build.**

Volk's global Vulkan function pointer symbols have **hidden visibility** by default
(via `#pragma GCC visibility push(hidden)` in `volk.c`), and Qt's Vulkan renderer
uses `QVulkanFunctions` / `QVulkanDeviceFunctions` (which do their own `dlopen`)
rather than global `vkXxx` symbols. The two mechanisms are completely independent
and do not interfere with each other.

If something IS broken when `VIDEOCOMMON=ON`, the cause is **not** volk symbol
conflicts. Look elsewhere (config array, PCI config, struct layout, cmake link
order, etc.).

---

## 1. How Volk Defines Global Symbols

### Architecture

Volk works by:
1. Auto-defining `VK_NO_PROTOTYPES` before including `vulkan.h` (line 27 of `volk.h`)
2. Declaring `extern PFN_vkXxx vkXxx;` for every Vulkan function in `volk.h`
   (lines 1968-3226)
3. Defining the actual global variables `PFN_vkXxx vkXxx;` (initialized to zero/NULL)
   in `volk.c` (lines 2774-3994)
4. Loading real function pointers via `volkInitialize()` -> `dlopen("libvulkan.dylib")`
   -> `dlsym("vkGetInstanceProcAddr")` -> `volkLoadInstance()` ->
   `vkGetInstanceProcAddr()` for each function

### Source Files

- **Header**: `extra/volk/volk.h` (v344) -- declares `extern` prototypes
- **Implementation**: `extra/volk/volk.c` -- defines global variables
- **Compiled in**: `src/video/videocommon/vc_core.c` (via `#define VOLK_IMPLEMENTATION`)

### Symbol Visibility (Critical)

In `volk.c`, lines 2765-2771:
```c
#ifdef __GNUC__
#ifdef VOLK_DEFAULT_VISIBILITY
#   pragma GCC visibility push(default)
#else
#   pragma GCC visibility push(hidden)
#endif
#endif
```

**By default** (without `VOLK_DEFAULT_VISIBILITY`), all global function pointer
variables are compiled with **hidden visibility**. This means:

- On macOS/Linux (GCC/Clang): symbols are `private external` / hidden
- On Windows (MSVC): no visibility pragma exists, but MSVC doesn't export .exe
  symbols by default anyway -- they're internal

### Empirical Verification (macOS ARM64, current build)

Object file (`vc_core.c.o`):
```
$ nm -m build/src/video/videocommon/CMakeFiles/videocommon.dir/vc_core.c.o | grep vkCreateInstance
0000000000000008 (common) (alignment 2^3) private external _vkCreateInstance
```
`private external` = hidden visibility.

Final binary (`86Box`):
```
$ nm build/src/86Box.app/Contents/MacOS/86Box | grep vkCreateInstance
0000000104e54ac8 s _vkCreateInstance
```
Lowercase `s` = non-external (local) symbol in data section. NOT exported.

Total count:
- 711 volk `vkXxx` symbols present as hidden (`s`) in the final binary
- 0 volk `vkXxx` symbols exported (uppercase `T`, `D`, `B`, or `S`)
- No Vulkan shared library linked: `otool -L 86Box | grep vulkan` returns nothing

---

## 2. How Qt Vulkan Works

### Loading Mechanism

Qt's `QVulkanInstance` performs its own independent Vulkan library loading:

1. `QVulkanInstance::create()` triggers lazy loading
2. Qt's QPA (Qt Platform Abstraction) layer does its own `dlopen("libvulkan.dylib")`
   (or `LoadLibrary("vulkan-1.dll")` on Windows)
3. Qt resolves `vkGetInstanceProcAddr` from its own library handle
4. All Vulkan calls go through `QVulkanFunctions` and `QVulkanDeviceFunctions`
   wrapper classes, NOT global symbols

Source: [Qt 5.15 QVulkanInstance docs](https://doc.qt.io/qt-5/qvulkaninstance.html):
> "Neither the QtGui library nor the platform plugins link directly to libvulkan
> or similar, and the same applies to Qt applications by default."

### How 86Box Qt Vulkan Renderers Call Vulkan

Both `qt_vulkanrenderer.cpp` (VulkanRenderer2) and `qt_vulkanwindowrenderer.cpp`
(VulkanRendererEmu) use Qt's wrapper:

```cpp
m_devFuncs = m_window->vulkanInstance()->deviceFunctions(dev);
// ...
m_devFuncs->vkCmdDraw(cb, 4, 1, 0, 0);          // via QVulkanDeviceFunctions
m_devFuncs->vkCreateGraphicsPipelines(...);       // via QVulkanDeviceFunctions
instance.functions()->vkEnumeratePhysicalDevices(...); // via QVulkanFunctions
```

**No Qt Vulkan file in `src/qt/` ever includes `volk.h`** (verified by search).
**No Qt Vulkan file calls global `vkXxx()` directly** (all go through `m_devFuncs->`).

### Compilation Flags

- `videocommon` target: `VK_NO_PROTOTYPES` is set (CMake)
- `voodoo` target: `VK_NO_PROTOTYPES` is set (CMake)
- `ui` target: `VK_NO_PROTOTYPES` is **NOT** set

This is correct and harmless:
- `videocommon`/`voodoo` include `volk.h` which re-declares all functions
- `ui` (Qt) includes `QVulkanWindow` which includes `vulkan.h` -- but since
  Qt never calls global Vulkan symbols directly (it uses QVulkanFunctions),
  the prototypes are just type declarations, never linked against

### Linking

- `videocommon` links NO Vulkan library (volk does dlopen)
- `ui` links NO Vulkan library (Qt does dlopen)
- `voodoo` links NO Vulkan library
- Final `86Box` binary has NO Vulkan library in `otool -L` output

---

## 3. Why There Is No Conflict

The two Vulkan loading mechanisms are completely independent:

| Aspect | Volk (VideoCommon) | Qt Vulkan (UI) |
|--------|-------------------|----------------|
| Library loading | `dlopen("libvulkan.dylib")` in `volkInitialize()` | `dlopen("libvulkan.dylib")` in `QVulkanInstance::create()` |
| Function resolution | Global `PFN_vkXxx` variables filled by `volkLoadInstance/Device()` | Internal QVulkanFunctions pointers filled by Qt |
| Symbol visibility | Hidden (`#pragma GCC visibility push(hidden)`) | N/A (Qt uses dlsym, not linker symbols) |
| Calling convention | `vkCmdDraw(...)` (resolves to global variable) | `m_devFuncs->vkCmdDraw(...)` (method call) |
| Compilation | `VK_NO_PROTOTYPES` defined | Standard vulkan.h prototypes (unused) |

Key points:
1. Volk's global symbols are **hidden** -- they cannot shadow or be shadowed by
   any other library's exports
2. Qt never calls global `vkXxx()` -- it uses its own wrapper classes
3. Neither component links `libvulkan` -- both use dlopen independently
4. The two `dlopen` calls to the same library are fine -- the dynamic loader
   returns the same handle (reference counted)

---

## 4. Volk Configuration Macros Reference

For completeness, here are all relevant volk configuration macros:

### `VK_NO_PROTOTYPES`
- **Purpose**: Prevents `vulkan.h` from declaring Vulkan functions as extern
  (which would conflict with volk's own declarations)
- **Set by**: `volk.h` auto-defines this (line 27) before including vulkan.h
- **Must be set**: On any translation unit that includes `volk.h`
- **Status**: Already correctly set via CMake for `videocommon` and `voodoo` targets

### `VOLK_IMPLEMENTATION`
- **Purpose**: When defined before including `volk.h`, it also includes `volk.c`
  (header-only mode). Alternatively, compile `volk.c` directly.
- **Must appear**: In exactly ONE translation unit
- **Status**: Defined in `vc_core.c` line 23

### `VOLK_DEFAULT_VISIBILITY`
- **Purpose**: If defined, changes symbol visibility from hidden to default
- **Default**: NOT defined (symbols are hidden)
- **Recommendation**: Do NOT define this. Hidden visibility is what we want.

### `VOLK_NO_DEVICE_PROTOTYPES`
- **Purpose**: Hides device-level extern declarations in `volk.h`
- **Use with**: `volkLoadInstanceOnly()` + `VolkDeviceTable` per-device pattern
- **Status**: Not needed for 86Box (single VkDevice, global loading is fine)

### `VOLK_NAMESPACE` (C++ only)
- **Purpose**: Wraps all volk symbols in `volk::` namespace
- **Limitation**: Only works in C++ (`#error` if used in C)
- **Status**: Not usable for VideoCommon (C11 codebase)

### `VOLK_VULKAN_H_PATH`
- **Purpose**: Redirect which vulkan.h header volk includes
- **Status**: Not needed (default vulkan.h path is correct)

### `VOLK_STATIC_DEFINES`
- **Purpose**: CMake variable to pass platform defines when using volk as a
  static library via `find_package(volk)`
- **Status**: Not applicable (we vendor volk as source)

---

## 5. VolkDeviceTable (Local Mode) -- Not Needed

Volk supports a "local mode" via `VolkDeviceTable`:

```c
struct VolkDeviceTable table;
volkLoadDeviceTable(&table, device);
// Then use: table.vkCmdDraw(...) instead of global vkCmdDraw(...)
```

This avoids global symbol pollution entirely but requires changing all call sites
to use `table.vkXxx(...)` instead of `vkXxx(...)`.

**Recommendation**: Not needed for 86Box because:
1. Hidden visibility already prevents symbol conflicts
2. We have a single VkDevice
3. The global calling convention (`vkCmdDraw(...)`) is simpler and matches all
   Vulkan documentation/tutorials

---

## 6. Potential Issues on Other Platforms

### Windows (MSVC)
- MSVC doesn't have `#pragma GCC visibility` -- but .exe files don't export
  symbols by default (only DLLs do with `__declspec(dllexport)`)
- volk's global variables are internal to the .exe
- Qt loads Vulkan via `LoadLibrary("vulkan-1.dll")` independently
- **No conflict possible**

### Linux (GCC)
- GCC respects `#pragma GCC visibility push(hidden)` just like Clang
- Symbols will be hidden in the final binary
- **No conflict possible** (verified by the pragma in volk.c)

### Pi 5 (Mesa V3DV)
- Same as Linux -- GCC visibility pragma works
- **No conflict possible**

### Static linking edge case
- If someone linked `libvulkan.so`/`libvulkan.dylib` as a static library AND
  volk was compiled without hidden visibility, there would be duplicate symbol
  errors at link time
- This does NOT apply to 86Box (no static Vulkan linking, hidden visibility)

---

## 7. If the Glide Detection Bug Is NOT a Symbol Conflict, What Could It Be?

Since volk/Qt symbol conflicts are definitively ruled out, the Glide detection
bug (`gpu_renderer=1` breaks Voodoo detection even with `vc_voodoo_init()`
commented out) must have a different cause. Possible angles:

1. **Config array side effect**: The `gpu_renderer` config entry in the Voodoo
   device_config_t array might affect how 86Box reads PCI config or MMIO
2. **CMake link order**: The `videocommon` OBJECT library being linked might
   change initialization order
3. **Compile definition leakage**: `USE_VIDEOCOMMON` is set project-wide when
   `VIDEOCOMMON=ON` -- check if any Voodoo code path checks this and changes
   behavior
4. **VMA C++ runtime**: `vc_vma_impl.cpp` pulls in C++ runtime -- could affect
   global constructor ordering

---

## 8. Known Solutions from Other Projects

### ImGui + Volk (GitHub issue #4854)
- ImGui had the same problem: its Vulkan backend defines global `vkXxx` symbols
  when `VK_NO_PROTOTYPES` is set
- Solution: `ImGui_ImplVulkan_LoadFunctions()` callback to delegate loading
- Not applicable to 86Box (Qt doesn't use global symbols)

### ANGLE + Volk
- Google's ANGLE project vendors volk similarly
- Uses it as a static library with hidden visibility (default)
- No special conflict avoidance needed because ANGLE is the only Vulkan user
  in its process

---

## References

- [volk source (vendored)](../../extra/volk/volk.h) -- v344
- [volk GitHub README](https://github.com/zeux/volk/blob/master/README.md)
- [Qt 5.15 QVulkanInstance](https://doc.qt.io/qt-5/qvulkaninstance.html)
- [ImGui volk conflict issue](https://github.com/ocornut/imgui/issues/4854)
- [ANGLE volk integration](https://chromium.googlesource.com/angle/angle/+/refs/heads/main/src/third_party/volk/)
- [Vulkan-Hpp volk conflict issue](https://github.com/KhronosGroup/Vulkan-Hpp/issues/378)
