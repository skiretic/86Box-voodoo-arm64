# Public API & Build System Audit

**Date**: 2026-02-28
**Auditor**: vc-debug agent
**Branch**: videocommon-voodoo
**Files audited**:
1. `src/include/86box/videocommon.h` (739 lines)
2. `CMakeLists.txt` (top-level, 237 lines)
3. `src/CMakeLists.txt` (301 lines)
4. `src/video/CMakeLists.txt` (189 lines)
5. `src/video/videocommon/CMakeLists.txt` (113 lines)
6. `src/video/videocommon/cmake/CompileShader.cmake` (68 lines)
7. `src/video/videocommon/cmake/SpvToHeader.cmake` (52 lines)

---

## 1. `src/include/86box/videocommon.h` -- 739 lines

### API Completeness Verification

All 36 public API functions were verified to have:
- An `extern` declaration in the `#ifdef USE_VIDEOCOMMON` block
- A matching `static inline` no-op stub in the `#else` block
- Exactly one implementation in `src/video/videocommon/*.c`

**Functions verified** (36 total):
`vc_init`, `vc_close`, `vc_submit_triangle`, `vc_swap_buffers`, `vc_sync`,
`vc_push_constants`, `vc_readback_pixels`, `vc_readback_color_sync`,
`vc_readback_depth_sync`, `vc_get_fb_dimensions`, `vc_wait_idle`,
`vc_texture_upload_async`, `vc_texture_bind_async`, `vc_set_pipeline_key`,
`vc_set_depth_state`, `vc_fog_table_upload_async`, `vc_readback_is_async`,
`vc_readback_color_async`, `vc_readback_depth_async`, `vc_readback_track_read`,
`vc_readback_mark_dirty`, `vc_readback_mark_all_tiles_dirty`, `vc_clear_buffers`,
`vc_lfb_write_color`, `vc_lfb_write_depth`, `vc_lfb_write_flush`,
`vc_lfb_write_pending`, `vc_get_instance`, `vc_get_device`,
`vc_get_physical_device`, `vc_get_graphics_queue_family`,
`vc_get_graphics_queue`, `vc_get_queue_mutex`, `vc_set_direct_present`,
`vc_get_direct_present`, `vc_get_global_ctx`, `vc_set_global_ctx`

### Issues Found

- **[MODERATE]** Lines 64-65 (`qt_vcrenderer.hpp`): Two functions -- `vc_get_front_color_image()` and
  `vc_get_front_color_image_view()` -- are called from `qt_vcrenderer.cpp` (lines 1162, 1215,
  1267, 1540) but are **not declared in the public header**. They are declared only in:
  - `vc_core.h` (internal, lines 144 and 151) with Vulkan return types (`VkImage`, `VkImageView`)
  - `qt_vcrenderer.hpp` (lines 64-65) via a duplicate `extern "C"` block

  **Analysis**: This is intentional by design. These functions return Vulkan-typed handles
  (`VkImage`, `VkImageView`) that cannot appear in `videocommon.h` because the public header
  must be Vulkan-agnostic for non-VC builds. The Qt renderer has Vulkan headers available
  (via volk) so it re-declares them locally. The `void*`-returning accessors
  (`vc_get_instance`, etc.) in `videocommon.h` serve the same purpose for opaque handles.

  **Recommendation**: This is acceptable as-is but creates a **maintenance risk**: if the
  function signatures change in `vc_core.c`, the duplicate declarations in
  `qt_vcrenderer.hpp` (lines 57-65) must be updated manually. Consider adding a comment
  in both locations cross-referencing the other.

- **[LOW]** Line 68 (`qt_vcrenderer.cpp`): `vc_create_metal_layer()` is declared via a local
  `extern "C"` in `qt_vcrenderer.cpp` and implemented in `qt_vc_metal_layer.mm`. It is not
  in any header. This is a platform-specific helper with exactly one caller, so a local
  declaration is acceptable, but a dedicated `qt_vc_metal_layer.h` header would be cleaner.

- **[LOW]** Lines 50-56: The vertex layout comment says "14 floats per vertex" and
  "56 bytes" -- this is correct (14 * 4 = 56) and matches `vc_vertex_t` in `vc_batch.h`.
  The comment references `vc_batch.h` for the canonical definition, which is good.

- **[LOW]** Line 86 (`vc_readback_pixels`): This is the legacy display-thread readback API.
  It coexists with the newer `vc_readback_color_sync`/`vc_readback_color_async` LFB APIs.
  Both are used: `vc_readback_pixels` from `vid_voodoo_display.c:581` (display/timer thread)
  and the sync/async variants from `vid_voodoo_vk.c` (LFB reads). No actual issue, but the
  relationship between these readback APIs could be documented more clearly.

### Notes

- **Vulkan header isolation**: Correctly achieved. The public header includes only
  `<stdint.h>` and `<stddef.h>`. All Vulkan types are hidden behind `void*` returns.
  The opaque `vc_context_t` typedef is a forward-declared struct in the VC path and
  `typedef void` in the no-op path. This is clean.

- **Thread safety documentation**: Good coverage. `vc_sync()` (line 67-71) has explicit
  thread-safety documentation warning against cross-thread calls. `vc_wait_idle()` (line
  118-125) documents that it is safe from any thread. `vc_readback_mark_dirty()` (line
  266) and `vc_lfb_write_color()` (line 314) document their thread safety. Multiple
  functions note "Safe to call from the FIFO/producer thread."

- **No-op stubs**: All 36 stubs properly suppress unused-parameter warnings via `(void)`.
  Return values are consistent: NULL for pointers, 0 for integers/booleans.

- **C/C++ compatibility**: Correctly wrapped in `extern "C" { ... }` guards (lines 26-28,
  735-737). All types use C11-compatible constructs.

- **`vc_push_constants_t`** is correctly defined locally in `vid_voodoo_vk.c` (not in the
  public header) since the push constant layout is Voodoo-specific. The public API
  accepts `const void* data` and `uint32_t size`, which is correct for a generic API.

---

## 2. `CMakeLists.txt` (top-level) -- 237 lines

### Issues Found

- None. The `VIDEOCOMMON` option is correctly defined.

### Notes

- **Line 141**: `option(VIDEOCOMMON "GPU-accelerated rendering via Vulkan (VideoCommon)" OFF)` --
  Default is OFF, which is correct for an optional feature.
- The option is a simple boolean with no dependencies. There is no `cmake_dependent_option`
  tying it to other features (e.g., Qt), which is correct since VideoCommon can work with
  the SDL UI as well (through software readback to the display system).

---

## 3. `src/CMakeLists.txt` -- 301 lines

### Issues Found

- **[MODERATE]** Line 92: `add_compile_definitions(USE_VIDEOCOMMON)` uses directory-scoped
  `add_compile_definitions()` rather than `target_compile_definitions()`. This means
  **every target** in the `src/` subtree gets `USE_VIDEOCOMMON` defined, even targets
  like `cpu`, `snd`, `net`, etc. that have nothing to do with VideoCommon.

  **Impact**: Not a correctness bug (the define is only checked in `videocommon.h`,
  Voodoo files, and Qt files, all of which need it). However, it is slightly wasteful
  and violates CMake best practices. In practice, the `videocommon.h` no-op stubs ensure
  that even if an unrelated file includes the header, it compiles correctly.

  **Recommendation**: Consider changing to `target_compile_definitions(86Box PRIVATE USE_VIDEOCOMMON)`
  plus `target_compile_definitions(voodoo PRIVATE USE_VIDEOCOMMON)` in `src/video/CMakeLists.txt`.
  However, since the current approach works and matches the pattern used by other options
  (`USE_DYNAREC`, `USE_GDBSTUB`, etc. at lines 68-85), this is not urgent.

- **[LOW]** Line 151: `target_link_libraries(86Box videocommon volk vma)` links volk and vma
  directly to the `86Box` executable. These are already linked transitively via
  `videocommon PUBLIC volk vma` (in `src/video/videocommon/CMakeLists.txt` line 89-92).
  The explicit link is therefore redundant.

  **Analysis**: This is harmless (CMake deduplicates link targets) but clutters the
  build configuration. The explicit link may have been added before the PUBLIC linkage
  was established.

### Notes

- **Line 143-147**: The `voodoo` OBJECT library is always listed in `target_link_libraries`
  (line 144), which is correct -- the voodoo library compiles unconditionally, with
  `vid_voodoo_vk.c` conditionally added via `target_sources(voodoo PRIVATE vid_voodoo_vk.c)`
  in `src/video/CMakeLists.txt:182`.
- **No Vulkan SDK find_package**: Correct. VideoCommon uses volk for dynamic function loading,
  so there is no link-time dependency on the Vulkan loader. This is a deliberate design
  choice for portability (especially MoltenVK on macOS).

---

## 4. `src/video/CMakeLists.txt` -- 189 lines

### Issues Found

- None.

### Notes

- **Lines 163-175**: The `voodoo` OBJECT library correctly lists all Voodoo source files.
  `vid_voodoo_vk.c` is conditionally added (line 182), not statically listed.
- **Line 183**: `target_link_libraries(voodoo PUBLIC videocommon)` -- the PUBLIC linkage
  ensures that videocommon's include directories and compile definitions propagate
  to anything that links voodoo. This is correct because `voodoo` is an OBJECT library
  and `86Box` links it.
- **Lines 187-189**: `add_subdirectory(videocommon)` is correctly guarded by `if(VIDEOCOMMON)`.
  If VIDEOCOMMON is OFF, the subdirectory is never processed, and `vid_voodoo_vk.c` is
  never compiled.
- **Line 177-179**: The SSE2 flag for voodoo on x86 is correctly limited to non-MSVC
  compilers (MSVC enables SSE2 by default).

---

## 5. `src/video/videocommon/CMakeLists.txt` -- 113 lines

### Issues Found

- **[MODERATE]** Lines 15-27 (WSI platform definitions): Missing **Wayland** WSI define
  (`VK_USE_PLATFORM_WAYLAND_KHR`). Modern Linux desktops increasingly use Wayland
  natively. Without this define, volk will not load Wayland surface creation functions,
  meaning `VCRenderer` cannot create a `VkSurfaceKHR` on a Wayland-only compositor.

  **Current state**: Lines 22-26 define `VK_USE_PLATFORM_XCB_KHR` and
  `VK_USE_PLATFORM_XLIB_KHR` for Linux. Qt5/Qt6 can use XWayland as a fallback, but
  native Wayland requires `VK_USE_PLATFORM_WAYLAND_KHR`.

  **Recommendation**: Add `VK_USE_PLATFORM_WAYLAND_KHR` to the Linux block:
  ```cmake
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_compile_definitions(volk PUBLIC
          VK_USE_PLATFORM_XCB_KHR
          VK_USE_PLATFORM_XLIB_KHR
          VK_USE_PLATFORM_WAYLAND_KHR
      )
  endif()
  ```
  Note: This is safe even on systems without Wayland headers because volk only declares
  function pointer types -- the actual Wayland surface creation would be attempted only
  if the VCRenderer detects a Wayland display. However, volk.h does include `<wayland-client.h>`
  when `VK_USE_PLATFORM_WAYLAND_KHR` is defined, so a build dependency on
  `libwayland-dev` would be required. This should be a separate CMake option
  (e.g., `cmake_dependent_option(WAYLAND_WSI ...)`).

  **Severity reduced to MODERATE**: Since `qt_vcrenderer.cpp` currently only implements
  Xlib surface creation for Linux (line 75: `#include <X11/Xlib.h>`), Wayland WSI is
  not yet implemented in the renderer code. The missing define matches the current code.
  This becomes HIGH when Wayland surface creation is added.

- **[LOW]** Lines 15-27: Missing **FreeBSD** handling. The `elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")`
  block only matches Linux. FreeBSD with X11 would need `VK_USE_PLATFORM_XCB_KHR` and/or
  `VK_USE_PLATFORM_XLIB_KHR` as well. However, FreeBSD is not a primary target for 86Box,
  and the main CMake build already has minimal FreeBSD-specific handling.

- **[LOW]** Lines 103-108: The `dl` library is linked only on Linux. This is correct because:
  - macOS: `dlopen` is in libSystem (no explicit link needed)
  - FreeBSD: `dlopen` is in libc (no explicit link needed)
  - Windows: Uses `LoadLibraryA` (no `dl` needed)
  However, if the Linux guard changes to include other Unixes, this would need updating.

- **[LOW]** Line 14: `target_compile_definitions(volk PUBLIC VOLK_STATIC_DEFINES)` --
  `VOLK_STATIC_DEFINES` is a volk-specific define that makes volk declare Vulkan function
  pointers as extern (for multi-TU use). This is correct for the static library approach.

### Notes

- **Source file completeness**: All 8 `.c` files in `src/video/videocommon/` are listed:
  `vc_core.c`, `vc_render_pass.c`, `vc_shader.c`, `vc_pipeline.c`, `vc_texture.c`,
  `vc_batch.c`, `vc_readback.c`, `vc_thread.c`. Verified against filesystem.

- **Shader completeness**: All 4 shaders are compiled:
  - `voodoo_uber.vert` -> `voodoo_uber_vert_spv.h`
  - `voodoo_uber.frag` -> `voodoo_uber_frag_spv.h`
  - `postprocess.vert` -> `postprocess_vert_spv.h`
  - `postprocess.frag` -> `postprocess_frag_spv.h`
  Verified against filesystem listing in `shaders/`.

- **Shader dependency tracking**: Line 95-101 creates a `videocommon_shaders` custom target
  that depends on all 4 generated headers. `videocommon` depends on this target (line 101).
  Additionally, `qt_vcrenderer.cpp` depends on the postprocess shader headers, and the
  Qt CMakeLists.txt correctly adds `add_dependencies(ui videocommon_shaders)` (line 281
  of `src/qt/CMakeLists.txt`).

- **VMA configuration**: `VMA_STATIC_VULKAN_FUNCTIONS=0` and
  `VMA_DYNAMIC_VULKAN_FUNCTIONS=0` (lines 37-38) are correct because VMA receives
  function pointers manually via `VmaVulkanFunctions` at allocator creation time,
  loaded from volk. Verified in `vma_impl.cpp`.

- **Include directories**: `videocommon` has PUBLIC includes for its own source dir and
  the generated SPIR-V header dir, plus PRIVATE includes for `86box/` public headers.
  The PRIVATE include prevents other targets from accidentally getting 86Box headers
  through the videocommon dependency chain.

---

## 6. `src/video/videocommon/cmake/CompileShader.cmake` -- 68 lines

### Issues Found

- **[LOW]** Line 13-18: The `find_program` for `glslc` searches a fixed set of HINTS paths.
  On Windows, the Vulkan SDK typically installs to `C:\VulkanSDK\<version>\Bin\glslc.exe`.
  The `$ENV{VULKAN_SDK}/bin` hint covers this, but only if the environment variable is set.
  The `glslc` binary from the LunarG SDK is often also on PATH, so `find_program` will
  find it without hints in typical setups.

  **Recommendation**: Consider adding `$ENV{VULKAN_SDK}/Bin` (capital B) as a hint for
  Windows compatibility, since some SDK versions use uppercase `Bin`.

### Notes

- **SPIR-V target**: `--target-env=vulkan1.2` (line 44) correctly targets the Vulkan 1.2
  SPIR-V environment, matching the Vulkan 1.2 baseline in the design doc.
- **Optimization**: `-O` flag (line 45) enables basic SPIR-V optimization. This is
  appropriate for production builds.
- **Werror**: `-Werror` (line 46) treats GLSL warnings as errors, which is good for
  catching shader issues at build time.
- **Cross-platform header generation**: Uses a CMake script (`SpvToHeader.cmake`) instead
  of `xxd`, which is not available on Windows. Good design choice.

---

## 7. `src/video/videocommon/cmake/SpvToHeader.cmake` -- 52 lines

### Issues Found

- **[LOW]** Lines 12-14: The SPIR-V binary is read as hex using `file(READ ... HEX)` and
  then each byte is emitted individually as `0xNN`. This produces `unsigned char` arrays.
  SPIR-V modules are specified as `uint32_t` arrays in the Vulkan spec, but
  `vkCreateShaderModule` accepts `const uint32_t* pCode` and `size_t codeSize` (in bytes).
  Using `unsigned char` requires a cast to `uint32_t*` at the call site.

  **Analysis**: This is a common pattern and works correctly as long as the SPIR-V binary
  has correct alignment (which it does, coming from glslc) and the cast is performed.
  Verified that `vc_shader.c` handles this correctly.

### Notes

- **Include guard**: Uses `#ifndef ${ARRAY_NAME}_H` pattern (line 41), preventing
  double-inclusion issues.
- **Static arrays**: `static const unsigned char` (line 46) ensures the array is not
  exported and does not conflict across translation units.
- **Length variable**: `static const size_t ${ARRAY_NAME}_len` (line 49) provides the
  byte count, which maps directly to `vkCreateShaderModule`'s `codeSize` parameter.

---

## Cross-File Consistency Analysis

### `USE_VIDEOCOMMON` define propagation

The `USE_VIDEOCOMMON` define flows correctly through the build:

1. `CMakeLists.txt` line 141: `option(VIDEOCOMMON ...)` -- user-facing option
2. `src/CMakeLists.txt` line 92: `add_compile_definitions(USE_VIDEOCOMMON)` -- global define
3. All source files that check `#ifdef USE_VIDEOCOMMON` receive the define
4. `videocommon.h` uses `#ifdef USE_VIDEOCOMMON` to switch between real API and no-op stubs

**Verified callers**:
- `vid_voodoo.c`: Uses `#ifdef USE_VIDEOCOMMON` at 5 locations
- `vid_voodoo_display.c`: Uses `#ifdef USE_VIDEOCOMMON` at 4 locations
- `vid_voodoo_fb.c`: Calls bridge functions (guarded in the common header)
- `qt_vcrenderer.cpp`: Entire file wrapped in `#ifdef USE_VIDEOCOMMON`
- `qt_vcrenderer.hpp`: Entire file wrapped in `#ifdef USE_VIDEOCOMMON`

### Link dependency chain

```
86Box
  |-- voodoo (OBJECT)
  |     |-- [if VIDEOCOMMON] vid_voodoo_vk.c
  |     |-- [if VIDEOCOMMON] PUBLIC link: videocommon
  |
  |-- [if VIDEOCOMMON] videocommon (OBJECT)
  |     |-- PUBLIC link: volk (STATIC)
  |     |-- PUBLIC link: vma (STATIC)
  |     |-- PRIVATE link: dl (Linux only)
  |     |-- PRIVATE link: Threads::Threads
  |
  |-- [if VIDEOCOMMON] explicit link: videocommon volk vma (redundant, see issue above)
  |
  |-- ui (OBJECT, Qt)
        |-- [if VIDEOCOMMON] qt_vcrenderer.cpp
        |-- [if VIDEOCOMMON] PRIVATE link: volk (for VK types + WSI defines)
        |-- [if VIDEOCOMMON] PRIVATE includes: videocommon internal headers
        |-- [if VIDEOCOMMON, APPLE] qt_vc_metal_layer.mm
        |-- [if VIDEOCOMMON, APPLE] -framework QuartzCore
```

The link chain is correct. The `PUBLIC` linkage from `voodoo -> videocommon -> volk/vma`
ensures that include directories and compile definitions propagate correctly to the
`86Box` executable.

### Duplicate declarations in `qt_vcrenderer.hpp`

`qt_vcrenderer.hpp` lines 52-74 re-declares 11 VC functions in an `extern "C"` block:
- 6 are also in `videocommon.h` (`vc_get_instance`, `vc_get_device`, etc.)
- 2 are NOT in `videocommon.h` (`vc_get_front_color_image`, `vc_get_front_color_image_view`)
- 3 more from `videocommon.h` (`vc_set_direct_present`, `vc_get_direct_present`,
  `vc_get_global_ctx`, `vc_set_global_ctx`, `vc_get_fb_dimensions`)

This duplication is intentional (the Qt file needs Vulkan-typed returns that the public
header cannot provide), but the 6 functions that ARE in `videocommon.h` could be obtained
by simply including `videocommon.h` instead of re-declaring them. The Qt renderer
currently does NOT include `videocommon.h` at all.

---

## Summary

| Severity | Count | Description |
|----------|-------|-------------|
| CRITICAL | 0 | -- |
| HIGH     | 0 | -- |
| MODERATE | 2 | Global `USE_VIDEOCOMMON` define scope; missing Wayland WSI define |
| LOW      | 6 | Redundant link targets; missing FreeBSD WSI; glslc Windows hint; `unsigned char` SPIR-V arrays; `vc_create_metal_layer` has no header; `vc_readback_pixels` vs newer API documentation |

**Overall assessment**: The public API header and build system are well-structured and
correct. No critical or high-severity issues. The two moderate issues are:
1. `USE_VIDEOCOMMON` define scope is broader than necessary (matches existing patterns)
2. Wayland WSI is not yet supported (matches current renderer implementation)

The `videocommon.h` header achieves its design goals effectively:
- Complete Vulkan header isolation from non-VC builds
- All 36 functions have matching no-op stubs
- Thread safety is documented for key functions
- C/C++ compatible via `extern "C"` guards

The CMake build system correctly handles:
- Conditional compilation via `VIDEOCOMMON` option
- SPIR-V shader compilation with cross-platform header generation
- Dynamic Vulkan loading via volk (no link-time Vulkan dependency)
- Platform-specific WSI defines for macOS (Metal), Windows (Win32), Linux (X11/XCB)
- Dependency propagation through OBJECT library PUBLIC linkage
