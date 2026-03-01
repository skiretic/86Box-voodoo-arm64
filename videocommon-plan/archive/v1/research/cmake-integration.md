# CMake Integration Plan for VideoCommon

Detailed plan for integrating the VideoCommon Vulkan rendering library into the
86Box CMake build system. Covers the `VIDEOCOMMON` option gate, third-party
dependency management (volk, VMA, glslc), the new `videocommon` static library
target, SPIR-V shader compilation pipeline, platform-specific considerations,
and integration with the existing 86Box executable link.

Research date: 2026-02-26

---

## Table of Contents

1. [VIDEOCOMMON CMake Option](#1-videocommon-cmake-option)
2. [Third-Party Dependencies Integration](#2-third-party-dependencies-integration)
3. [New CMake Target: videocommon](#3-new-cmake-target-videocommon)
4. [Shader Compilation Pipeline](#4-shader-compilation-pipeline)
5. [Platform-Specific Notes](#5-platform-specific-notes)
6. [Integration with Existing Build](#6-integration-with-existing-build)
7. [Conditional Compilation Guards](#7-conditional-compilation-guards)
8. [Directory Layout](#8-directory-layout)
9. [Full Diff Summary](#9-full-diff-summary)

---

## 1. VIDEOCOMMON CMake Option

### Option Definition

Add to `CMakeLists.txt` (top-level) in the options block alongside existing
options like `MOLTENVK`, `VNC`, etc.

```cmake
option(VIDEOCOMMON "GPU-accelerated rendering via Vulkan (VideoCommon)" OFF)
```

**Default: OFF.** VideoCommon is experimental and must not affect existing builds
unless explicitly opted in. The user enables it with:

```bash
cmake -S . -B build -D VIDEOCOMMON=ON ...
```

### Placement in Top-Level CMakeLists.txt

Insert after the `SCREENSHOT_MODE` / `LIBASAN` block (around line 140), before
the `NEW_DYNAREC` block. This groups it with other optional feature flags:

```cmake
# GPU-accelerated rendering
option(VIDEOCOMMON "GPU-accelerated rendering via Vulkan (VideoCommon)" OFF)
```

### Gating Logic

When `VIDEOCOMMON=ON`, the build must:

1. Find a Vulkan implementation (headers + loader or MoltenVK)
2. Build the `videocommon` static library from `src/video/videocommon/`
3. Compile SPIR-V shaders from GLSL sources
4. Define `USE_VIDEOCOMMON` as a compile definition for all consumers
5. Link `videocommon` into the final `86Box` executable

When `VIDEOCOMMON=OFF`, none of these steps occur. No Vulkan dependency is
introduced and no VideoCommon source files are compiled.

### Interaction with MOLTENVK

The existing `MOLTENVK` option (macOS only) controls the Qt Vulkan display
renderer. `VIDEOCOMMON` is independent -- it creates its own Vulkan device for
offscreen rendering and does not use a swapchain. However, on macOS:

- If `VIDEOCOMMON=ON`, the build needs Vulkan headers and a Vulkan loader.
  volk handles this by dynamically loading `libMoltenVK.dylib` or
  `libvulkan.dylib` at runtime.
- `MOLTENVK` and `VIDEOCOMMON` can both be ON or independently toggled.
- The existing `MOLTENVK_INCLUDE` / `MOLTENVK_LIB` paths are used only by the
  Qt `ui` target. VideoCommon uses volk for dynamic loading and ships its own
  Vulkan headers via the vendored volk copy.

---

## 2. Third-Party Dependencies Integration

### 2.1 volk (Vulkan Meta-Loader)

| Property | Value |
|----------|-------|
| Repository | https://github.com/zeux/volk |
| License | MIT |
| Language | C89 |
| Files needed | `volk.h`, `volk.c` |
| Integration | Vendored in `src/video/videocommon/third_party/volk/` |

**Why vendored (not FetchContent):**
- No network required at build time -- critical for CI, air-gapped builds
- volk is stable (2 files, rarely changes) -- no version churn risk
- FetchContent adds configure-time latency and git dependency
- Matches 86Box's existing pattern of including small dependencies directly

**CMake integration:**

```cmake
# In src/video/videocommon/CMakeLists.txt
add_library(volk STATIC
    third_party/volk/volk.c
)
target_include_directories(volk PUBLIC
    third_party/volk
)
# VOLK_STATIC_DEFINES: tells volk we are statically linking, not using
# the shared library. This avoids dllexport/dllimport on Windows.
target_compile_definitions(volk PUBLIC
    VOLK_STATIC_DEFINES
)
# On macOS, tell volk to look for MoltenVK
if(APPLE)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_MACOS_MVK)
endif()
# On Linux/X11 (for future surface creation if needed)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XCB_KHR)
endif()
# On Windows
if(WIN32)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
endif()
```

**What volk replaces:** Instead of `find_package(Vulkan)` and linking against
`Vulkan::Vulkan` (which requires a Vulkan SDK installed), volk dynamically
loads the Vulkan loader at runtime via `dlopen`/`LoadLibrary`. This means:

- No link-time dependency on `vulkan-1.dll` / `libvulkan.so` / `libMoltenVK.dylib`
- Graceful failure if Vulkan is not available (returns `VK_ERROR_INITIALIZATION_FAILED`)
- Function pointers loaded directly from the ICD driver via `volkLoadDevice()`, bypassing the loader trampoline (1-5% less overhead per Vulkan call)

**Note on Vulkan headers:** volk ships `volk.h` which includes the Vulkan
headers. We can either:
- (a) Use the Vulkan headers from the Vulkan SDK (if installed)
- (b) Vendor a copy of `vulkan/vulkan.h` and friends in `third_party/volk/vulkan/`

Approach (b) is recommended for maximum portability. The Vulkan headers are
permissively licensed (Apache 2.0 with patent exception) and change infrequently
for our Vulkan 1.2 target. We pin them to a specific SDK version (e.g., 1.3.283)
in the vendored copy.

### 2.2 VMA (Vulkan Memory Allocator)

| Property | Value |
|----------|-------|
| Repository | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator |
| License | MIT |
| Language | C++ (header-only, compiled in one .cpp TU) |
| Files needed | `vk_mem_alloc.h` |
| Integration | Vendored in `src/video/videocommon/third_party/vma/` |

**Why vendored:** Same reasons as volk -- stability, no network, simplicity.

**CMake integration:**

VMA is header-only but requires `VMA_IMPLEMENTATION` defined in exactly one
translation unit. Since VMA is C++ internally, we compile it in a dedicated
`.cpp` file:

```cmake
# In src/video/videocommon/CMakeLists.txt
add_library(vma STATIC
    third_party/vma/vma_impl.cpp
)
target_include_directories(vma PUBLIC
    third_party/vma
)
# VMA uses volk function pointers, not linked Vulkan functions
target_compile_definitions(vma PUBLIC
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
)
target_link_libraries(vma PUBLIC volk)
```

The implementation file `third_party/vma/vma_impl.cpp`:

```cpp
/*
 * VMA implementation compilation unit.
 * VMA is C++ internally but exposes a C-compatible API.
 * We compile it once here and link statically.
 */
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

/* VMA needs Vulkan function pointers -- we pass them via
 * VmaVulkanFunctions struct at allocator creation time,
 * loaded from volk. */
#include "volk.h"
#include "vk_mem_alloc.h"
```

**Why `VMA_STATIC_VULKAN_FUNCTIONS=0` and `VMA_DYNAMIC_VULKAN_FUNCTIONS=0`:**
We pass volk-loaded function pointers to VMA at creation time via the
`VmaVulkanFunctions` struct. This avoids VMA trying to call `vkGetInstanceProcAddr`
or `vkGetDeviceProcAddr` directly, which would require linking against the
Vulkan loader.

### 2.3 glslc (SPIR-V Compiler)

| Property | Value |
|----------|-------|
| Provider | Google (shaderc), bundled with Vulkan SDK |
| Usage | Build-time only (not linked, not vendored) |
| Purpose | Compiles `.vert`/`.frag` GLSL to `.spv` SPIR-V bytecode |

**Not vendored** -- glslc is a host tool, not a library. It must be found on
the build system:

```cmake
find_program(GLSLC glslc
    HINTS
        $ENV{VULKAN_SDK}/bin
        /usr/bin
        /usr/local/bin
)
if(NOT GLSLC)
    message(FATAL_ERROR
        "glslc not found. Install the Vulkan SDK or shaderc:\n"
        "  macOS:   brew install shaderc\n"
        "  Ubuntu:  apt install glslc\n"
        "  Windows: Install LunarG Vulkan SDK\n"
        "  Or set VULKAN_SDK environment variable."
    )
endif()
message(STATUS "Found glslc: ${GLSLC}")
```

**Alternatives if glslc is unavailable:**
1. `glslangValidator` (Khronos) -- less user-friendly but functionally equivalent
2. Pre-compiled `.spv` files checked into the repository -- eliminates build-time
   dependency but makes shader iteration harder

Recommendation: Require `glslc` at build time. Provide clear error messages
pointing to installation instructions. The Vulkan SDK is a reasonable
prerequisite for anyone building with `VIDEOCOMMON=ON`.

---

## 3. New CMake Target: videocommon

### Static Library Definition

```cmake
# src/video/videocommon/CMakeLists.txt

# --- Third-party dependencies ---

add_library(volk STATIC
    third_party/volk/volk.c
)
target_include_directories(volk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk
)
target_compile_definitions(volk PUBLIC VOLK_STATIC_DEFINES)
if(APPLE)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_MACOS_MVK)
elseif(WIN32)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XCB_KHR)
endif()

add_library(vma STATIC
    third_party/vma/vma_impl.cpp
)
target_include_directories(vma PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma
)
target_compile_definitions(vma PUBLIC
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
)
target_link_libraries(vma PUBLIC volk)

# --- SPIR-V shader compilation (see Section 4) ---

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShader.cmake)

compile_shader(
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/voodoo_uber.vert
    ${CMAKE_CURRENT_BINARY_DIR}/generated/voodoo_uber_vert_spv.h
    voodoo_uber_vert_spv
)
compile_shader(
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/voodoo_uber.frag
    ${CMAKE_CURRENT_BINARY_DIR}/generated/voodoo_uber_frag_spv.h
    voodoo_uber_frag_spv
)

# --- VideoCommon core library ---

add_library(videocommon OBJECT
    vc_core.c
    vc_thread.c
    vc_render_pass.c
    vc_pipeline.c
    vc_shader.c
    vc_texture.c
    vc_batch.c
    vc_readback.c
)

target_include_directories(videocommon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}              # videocommon headers
    ${CMAKE_CURRENT_BINARY_DIR}/generated    # generated SPIR-V headers
)

target_include_directories(videocommon PRIVATE
    ${CMAKE_SOURCE_DIR}/src/include          # 86box headers
)

target_link_libraries(videocommon PUBLIC
    volk
    vma
)

# Ensure SPIR-V headers are generated before videocommon compiles
add_custom_target(videocommon_shaders DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/generated/voodoo_uber_vert_spv.h
    ${CMAKE_CURRENT_BINARY_DIR}/generated/voodoo_uber_frag_spv.h
)
add_dependencies(videocommon videocommon_shaders)

# Platform-specific link dependencies
if(APPLE)
    # dlopen is in libSystem on macOS (no explicit link needed)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(videocommon PRIVATE dl)
elseif(WIN32)
    # LoadLibrary is in kernel32 (linked by default)
endif()

# Threading (for render thread)
find_package(Threads REQUIRED)
target_link_libraries(videocommon PRIVATE Threads::Threads)
```

### Why OBJECT Library (Not STATIC)

The existing 86Box build uses `add_library(... OBJECT ...)` for all subsystem
libraries (see `vid`, `voodoo`, `cpu`, `snd`, etc. in the existing CMake files).
OBJECT libraries are linked by including their object files directly into the
final executable, avoiding the need for separate `.a` / `.lib` static archives.
VideoCommon follows this convention.

The internal helper libraries (`volk`, `vma`) are STATIC because they are
self-contained third-party code that should be compiled independently.

### Source Files

| File | Purpose |
|------|---------|
| `vc_core.c` | VkInstance, VkDevice, VkQueue creation. volk init. Capability detection. |
| `vc_thread.c` | Dedicated render thread. SPSC command ring buffer. Sync primitives. |
| `vc_render_pass.c` | VkRenderPass, VkFramebuffer. Dual front/back framebuffer management. |
| `vc_pipeline.c` | VkPipeline creation, pipeline cache (blend-state keyed), dynamic state. |
| `vc_shader.c` | VkShaderModule creation from embedded SPIR-V. |
| `vc_texture.c` | VkImage management, staging upload, layout transitions, invalidation. |
| `vc_batch.c` | Triangle batching, vertex buffer (stream ring), vkCmdDraw submission. |
| `vc_readback.c` | LFB readback: image-to-staging-buffer copy, fence, map, async staging. |

### Header Files

| File | Location | Purpose |
|------|----------|---------|
| `videocommon.h` | `src/include/86box/videocommon.h` | Master public C11 API header. Included by Voodoo bridge code. |
| `vc_internal.h` | `src/video/videocommon/vc_internal.h` | Internal header for VideoCommon modules. Not public. |
| `vc_types.h` | `src/video/videocommon/vc_types.h` | Type definitions shared across VC modules. |

---

## 4. Shader Compilation Pipeline

### Overview

GLSL source files are compiled to SPIR-V at build time using `glslc`, then
converted to C arrays embedded in header files. This avoids shipping separate
`.spv` files and eliminates runtime file I/O.

```
shaders/voodoo_uber.vert  --[glslc]--> voodoo_uber_vert_spv.spv
                           --[embed]--> generated/voodoo_uber_vert_spv.h

shaders/voodoo_uber.frag  --[glslc]--> voodoo_uber_frag_spv.spv
                           --[embed]--> generated/voodoo_uber_frag_spv.h
```

### CompileShader.cmake Module

Create `src/video/videocommon/cmake/CompileShader.cmake`:

```cmake
# CompileShader.cmake
#
# Provides the compile_shader() function for building GLSL to embedded SPIR-V.
#
# Usage:
#   compile_shader(<input_glsl> <output_header> <array_name>)
#
# This creates two custom commands:
#   1. Compile GLSL -> .spv using glslc
#   2. Convert .spv -> .h (C array) using a CMake script
#

find_program(GLSLC glslc
    HINTS
        $ENV{VULKAN_SDK}/bin
        /usr/bin
        /usr/local/bin
)
if(NOT GLSLC)
    message(FATAL_ERROR
        "glslc not found. Install the Vulkan SDK or shaderc:\n"
        "  macOS:   brew install shaderc\n"
        "  Ubuntu:  apt install glslc\n"
        "  Windows: Install LunarG Vulkan SDK\n"
        "  Or set VULKAN_SDK environment variable."
    )
endif()
message(STATUS "Found glslc: ${GLSLC}")

function(compile_shader SHADER_SOURCE OUTPUT_HEADER ARRAY_NAME)
    # Intermediate SPIR-V file
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SPV_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/${ARRAY_NAME}.spv")

    # Ensure output directory exists
    get_filename_component(OUTPUT_DIR ${OUTPUT_HEADER} DIRECTORY)
    file(MAKE_DIRECTORY ${OUTPUT_DIR})

    # Step 1: Compile GLSL -> SPIR-V
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC}
            --target-env=vulkan1.2
            -O                              # Optimize
            -Werror                         # Treat warnings as errors
            ${SHADER_SOURCE}
            -o ${SPV_FILE}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling SPIR-V: ${SHADER_NAME}"
        VERBATIM
    )

    # Step 2: Convert SPIR-V -> C header
    # Uses a CMake script instead of xxd for cross-platform compatibility
    # (xxd is not available on Windows by default)
    add_custom_command(
        OUTPUT ${OUTPUT_HEADER}
        COMMAND ${CMAKE_COMMAND}
            -D SPV_FILE=${SPV_FILE}
            -D OUTPUT_HEADER=${OUTPUT_HEADER}
            -D ARRAY_NAME=${ARRAY_NAME}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpvToHeader.cmake
        DEPENDS ${SPV_FILE}
        COMMENT "Embedding SPIR-V: ${ARRAY_NAME}"
        VERBATIM
    )
endfunction()
```

### SpvToHeader.cmake Script

Create `src/video/videocommon/cmake/SpvToHeader.cmake`:

```cmake
# SpvToHeader.cmake
#
# Converts a SPIR-V binary file into a C header with a static const array.
# Called as a CMake script (-P) by the compile_shader() function.
#
# Variables (passed via -D):
#   SPV_FILE      - Path to input .spv file
#   OUTPUT_HEADER - Path to output .h file
#   ARRAY_NAME    - C identifier for the array and length variable
#

file(READ ${SPV_FILE} SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" SPV_HEX_LEN)
math(EXPR SPV_BYTE_LEN "${SPV_HEX_LEN} / 2")

# Build the C array contents
set(C_ARRAY "")
set(LINE "")
set(COL 0)
math(EXPR LAST_INDEX "${SPV_HEX_LEN} - 2")
foreach(IDX RANGE 0 ${LAST_INDEX} 2)
    string(SUBSTRING "${SPV_HEX}" ${IDX} 2 BYTE)
    if(COL EQUAL 0)
        string(APPEND LINE "    ")
    endif()
    string(APPEND LINE "0x${BYTE}, ")
    math(EXPR COL "${COL} + 1")
    if(COL EQUAL 12)
        string(APPEND C_ARRAY "${LINE}\n")
        set(LINE "")
        set(COL 0)
    endif()
endforeach()
if(NOT LINE STREQUAL "")
    string(APPEND C_ARRAY "${LINE}\n")
endif()

# Write the header file
file(WRITE ${OUTPUT_HEADER}
"/* Auto-generated SPIR-V bytecode -- do not edit. */
#ifndef ${ARRAY_NAME}_H
#define ${ARRAY_NAME}_H

#include <stddef.h>

static const unsigned char ${ARRAY_NAME}[] = {
${C_ARRAY}};

static const size_t ${ARRAY_NAME}_len = ${SPV_BYTE_LEN};

#endif /* ${ARRAY_NAME}_H */
")
```

### Why CMake Script Instead of xxd

The `xxd` utility (from vim) is not available on all platforms:
- **macOS**: Available via Homebrew or Xcode command line tools, but not guaranteed
- **Windows**: Not available unless MSYS2/Cygwin is installed
- **Linux**: Usually available via `vim-common` but not always installed

A pure CMake script (`SpvToHeader.cmake`) works everywhere CMake runs, with
no additional tool dependencies. The generated output is identical in function
to what `xxd -i` produces.

### Alternative: Python Script

If CMake script performance is a concern for large shaders (it will not be for
our ~2-5 KB shaders), a Python fallback can be added:

```python
#!/usr/bin/env python3
"""Convert SPIR-V binary to C header."""
import sys

def main():
    spv_path, header_path, array_name = sys.argv[1:4]
    with open(spv_path, 'rb') as f:
        data = f.read()
    with open(header_path, 'w') as f:
        f.write(f'/* Auto-generated SPIR-V bytecode -- do not edit. */\n')
        f.write(f'#ifndef {array_name.upper()}_H\n')
        f.write(f'#define {array_name.upper()}_H\n\n')
        f.write(f'#include <stddef.h>\n\n')
        f.write(f'static const unsigned char {array_name}[] = {{\n')
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            f.write('    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',\n')
        f.write(f'}};\n\n')
        f.write(f'static const size_t {array_name}_len = {len(data)};\n\n')
        f.write(f'#endif /* {array_name.upper()}_H */\n')

if __name__ == '__main__':
    main()
```

Recommendation: Use the CMake script. It eliminates the Python dependency and
is fast enough for our tiny shaders.

### Shader Source Location

```
src/video/videocommon/
    shaders/
        voodoo_uber.vert       # Vertex shader (screen-space passthrough + interpolators)
        voodoo_uber.frag       # Fragment shader (Voodoo pixel pipeline uber-shader)
```

### Build Output Location

```
build/src/video/videocommon/
    generated/
        voodoo_uber_vert_spv.spv    # Intermediate SPIR-V (not installed)
        voodoo_uber_vert_spv.h      # C array header (included by vc_shader.c)
        voodoo_uber_frag_spv.spv    # Intermediate SPIR-V
        voodoo_uber_frag_spv.h      # C array header
```

---

## 5. Platform-Specific Notes

### 5.1 macOS (MoltenVK)

**Vulkan availability:**
MoltenVK provides Vulkan 1.2+ on macOS. It translates Vulkan to Metal at
runtime. Available via:
- Vulkan SDK (LunarG) -- installs to `/usr/local/` or `~/VulkanSDK/`
- Homebrew: `brew install molten-vk` (installs to `/opt/homebrew/opt/molten-vk/`)

**volk on macOS:**
volk dynamically loads `libvulkan.1.dylib` (the Vulkan loader from the SDK)
or `libMoltenVK.dylib` directly. On macOS with the Vulkan SDK installed,
`libvulkan.1.dylib` is typically in `/usr/local/lib/` or the SDK path.

If the Vulkan loader is not in the standard library search path, the environment
variable `DYLD_LIBRARY_PATH` or `VK_ICD_FILENAMES` may need to be set:

```bash
export VK_ICD_FILENAMES=/opt/homebrew/opt/molten-vk/share/vulkan/icd.d/MoltenVK_icd.json
```

For the 86Box macOS app bundle, the `libMoltenVK.dylib` should be bundled
alongside the executable. The existing `install_bundle_library` macro in
`src/qt/CMakeLists.txt` handles this pattern (see line 468).

**CMake notes:**
- No `find_package(Vulkan)` needed -- volk bypasses this entirely
- No link against MoltenVK -- volk loads it dynamically
- Vulkan headers come from the vendored volk copy
- The `VK_USE_PLATFORM_MACOS_MVK` define is set on the volk target

### 5.2 Linux

**Vulkan availability:**
Mesa provides Vulkan drivers for most GPUs:
- AMD: `radv` (part of Mesa)
- Intel: `anv` (part of Mesa)
- NVIDIA: proprietary driver
- Pi 5: `v3dv` (part of Mesa)

Install Vulkan packages:
```bash
# Ubuntu/Debian
sudo apt install libvulkan-dev vulkan-tools mesa-vulkan-drivers

# Fedora
sudo dnf install vulkan-loader-devel mesa-vulkan-drivers
```

**volk on Linux:**
volk loads `libvulkan.so.1` via `dlopen()`. The `dl` library must be linked:

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(videocommon PRIVATE dl)
endif()
```

**glslc on Linux:**
```bash
# Ubuntu/Debian
sudo apt install glslc
# OR install the Vulkan SDK
```

### 5.3 Windows

**Vulkan availability:**
The LunarG Vulkan SDK provides the Vulkan loader (`vulkan-1.dll`), validation
layers, and `glslc`. GPU drivers (NVIDIA, AMD, Intel) provide the ICD.

**volk on Windows:**
volk loads `vulkan-1.dll` via `LoadLibraryA()`. No explicit linking needed.
The `VK_USE_PLATFORM_WIN32_KHR` define enables Win32 surface creation.

**glslc on Windows:**
Installed as part of the Vulkan SDK. Found via:
```cmake
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
```

**MSVC notes:**
- volk compiles cleanly with MSVC (C89 compatible)
- VMA compiles as C++ -- ensure the `.cpp` file uses the C++ compiler
- The `videocommon` target sources are `.c` files -- MSVC handles this correctly

### 5.4 Raspberry Pi 5

**Vulkan availability:**
Mesa V3DV driver provides Vulkan 1.3 on Pi 5 (VideoCore VII). Available in
Mesa 24.2+ (shipped with Raspberry Pi OS Bookworm updates).

```bash
sudo apt install mesa-vulkan-drivers vulkan-tools
```

**glslc on Pi 5:**
```bash
sudo apt install glslc
```

**Performance note:**
The Pi 5 uses shared CPU/GPU memory (UMA). VMA handles this transparently by
preferring `HOST_VISIBLE | DEVICE_LOCAL` allocations. No special CMake
configuration needed.

---

## 6. Integration with Existing Build

### 6.1 Changes to `CMakeLists.txt` (Top-Level)

Add the `VIDEOCOMMON` option:

```cmake
# After line ~140 (after LIBASAN option)
option(VIDEOCOMMON "GPU-accelerated rendering via Vulkan (VideoCommon)" OFF)
```

No other changes to the top-level CMakeLists.txt.

### 6.2 Changes to `src/CMakeLists.txt`

Three changes are needed:

**Change 1: Add compile definition when VIDEOCOMMON is ON.**

Insert after the existing `SCREENSHOT_MODE` block (around line 89):

```cmake
if(VIDEOCOMMON)
    add_compile_definitions(USE_VIDEOCOMMON)
endif()
```

This propagates `USE_VIDEOCOMMON` to ALL translation units in the project,
allowing `#ifdef USE_VIDEOCOMMON` guards in Voodoo source files.

**Change 2: Link videocommon into the 86Box executable.**

Modify the `target_link_libraries(86Box ...)` block (line ~120):

```cmake
target_link_libraries(86Box
    cpu
    chipset
    mch
    dev
    mem
    fdd
    game
    cdrom
    rdisk
    mo
    hdd
    net
    print
    scsi
    sio
    snd
    mdsx_dll
    utils
    vid
    voodoo
    plat
    ui
)

# VideoCommon GPU-accelerated rendering (optional)
if(VIDEOCOMMON)
    target_link_libraries(86Box videocommon volk vma)
endif()
```

The `volk` and `vma` targets must be listed explicitly because they are STATIC
libraries (not OBJECT), so their object files are not automatically included
when linking `videocommon` (an OBJECT library).

**Change 3: No `find_package(Vulkan)` needed.**

Because we use volk for dynamic loading, there is no need for CMake's
`find_package(Vulkan)`. This is a deliberate design choice:
- `find_package(Vulkan)` finds the SDK headers and `libvulkan.so` / `vulkan-1.dll`
- We do not link against the Vulkan loader -- volk loads it at runtime
- We vendor the Vulkan headers with volk

This eliminates the Vulkan SDK as a build-time dependency. Only `glslc`
(for shader compilation) requires the SDK or a standalone shaderc install.

### 6.3 Changes to `src/video/CMakeLists.txt`

Add the `videocommon` subdirectory, gated by the option:

```cmake
# After the voodoo library definition (after line ~175)

# VideoCommon GPU-accelerated rendering
if(VIDEOCOMMON)
    add_subdirectory(videocommon)
endif()
```

This is the only change to `src/video/CMakeLists.txt`.

### 6.4 New File: `src/video/videocommon/CMakeLists.txt`

The complete CMakeLists.txt for the videocommon subdirectory. This is a new file
(see Section 3 for the full contents). Summary of what it does:

1. Builds `volk` as a STATIC library from vendored sources
2. Builds `vma` as a STATIC library from vendored sources
3. Finds `glslc` and compiles GLSL shaders to SPIR-V C headers
4. Builds `videocommon` as an OBJECT library
5. Sets up include paths and link dependencies

### 6.5 Link Dependency Graph

```
86Box (executable)
  |
  +-- vid (OBJECT)          -- existing video subsystem
  +-- voodoo (OBJECT)       -- existing Voodoo emulation
  +-- videocommon (OBJECT)   -- NEW: Vulkan rendering core
  |     |
  |     +-- volk (STATIC)   -- NEW: Vulkan dynamic loader
  |     +-- vma (STATIC)    -- NEW: Vulkan memory allocator
  |     +-- Threads::Threads -- pthreads / Win32 threads
  |     +-- dl (Linux only)  -- dynamic loading
  +-- plat (OBJECT)
  +-- ui (OBJECT)
  +-- ... (other subsystems)
```

---

## 7. Conditional Compilation Guards

### 7.1 CMake Compile Definition

When `VIDEOCOMMON=ON`:

```cmake
add_compile_definitions(USE_VIDEOCOMMON)
```

This is set in `src/CMakeLists.txt` with `add_compile_definitions()`, which
applies to all targets in the directory and subdirectories. This matches the
existing pattern used for `USE_GDBSTUB`, `USE_NEW_DYNAREC`, `USE_VNC`, etc.

### 7.2 C/C++ Guards in Source Files

**In Voodoo source files** (existing code, modified):

```c
/* vid_voodoo.c -- device init */
#ifdef USE_VIDEOCOMMON
#    include <86box/videocommon.h>
#endif

static void *
voodoo_init(const device_t *info)
{
    voodoo_t *voodoo = /* ... existing init ... */;

#ifdef USE_VIDEOCOMMON
    if (voodoo->use_gpu_renderer) {
        voodoo->vc_ctx = vc_init(voodoo->h_disp, voodoo->v_disp);
        if (!voodoo->vc_ctx) {
            /* Fall back to software renderer */
            voodoo->use_gpu_renderer = 0;
            pclog("VideoCommon: Vulkan init failed, falling back to SW\n");
        }
    }
#endif

    return voodoo;
}
```

**In VideoCommon source files** (new code):

VideoCommon source files do NOT need `#ifdef USE_VIDEOCOMMON` guards because
they are only compiled when `VIDEOCOMMON=ON` (gated by the CMakeLists.txt
`add_subdirectory(videocommon)` condition). The entire `src/video/videocommon/`
directory is excluded from the build when `VIDEOCOMMON=OFF`.

**In the master public header** (`src/include/86box/videocommon.h`):

```c
#ifndef VIDEOCOMMON_H
#define VIDEOCOMMON_H

#ifdef USE_VIDEOCOMMON

#include <stdint.h>

/* Public API declarations */
typedef struct vc_context vc_context_t;

vc_context_t *vc_init(int width, int height);
void          vc_close(vc_context_t *ctx);
/* ... etc ... */

#else /* !USE_VIDEOCOMMON */

/* Stub out the API when VideoCommon is not compiled */
typedef void vc_context_t;

static inline vc_context_t *vc_init(int width, int height) {
    (void)width; (void)height;
    return NULL;
}
static inline void vc_close(vc_context_t *ctx) { (void)ctx; }

#endif /* USE_VIDEOCOMMON */

#endif /* VIDEOCOMMON_H */
```

The stub approach means Voodoo source files can include `<86box/videocommon.h>`
unconditionally and call `vc_init()` / `vc_close()` -- when VideoCommon is
disabled, the calls are inlined to no-ops. This reduces the number of `#ifdef`
blocks in the Voodoo code.

However, the simpler approach (explicit `#ifdef USE_VIDEOCOMMON` around each
call site) is also acceptable and may be clearer for reviewers. The choice
can be made during implementation.

### 7.3 Voodoo Header Changes

Add fields to `voodoo_t` in `src/include/86box/vid_voodoo_common.h`:

```c
typedef struct voodoo_t {
    /* ... existing fields ... */

#ifdef USE_VIDEOCOMMON
    void *vc_ctx;            /* vc_context_t* -- VideoCommon rendering context */
    int   use_gpu_renderer;  /* 1 = Vulkan, 0 = software fallback */
#endif
} voodoo_t;
```

### 7.4 Device Config Option

Add a user-visible config toggle in the Voodoo device configuration:

```c
/* In vid_voodoo.c device_config_t */
#ifdef USE_VIDEOCOMMON
    {
        .name = "gpu_renderer",
        .description = "GPU-accelerated rendering (Vulkan)",
        .type = CONFIG_BINARY,
        .default_int = 0,   /* Default OFF until feature is stable */
    },
#endif
```

---

## 8. Directory Layout

Complete directory structure for VideoCommon:

```
src/
    include/
        86box/
            videocommon.h               # Master public C11 API header
    video/
        videocommon/
            CMakeLists.txt              # Build definition for videocommon
            cmake/
                CompileShader.cmake     # Shader compilation function
                SpvToHeader.cmake       # SPIR-V to C header conversion script
            vc_core.c                   # Vulkan device/instance init
            vc_core.h                   # Internal header
            vc_thread.c                 # Render thread + SPSC ring
            vc_thread.h
            vc_render_pass.c            # Render pass + framebuffer
            vc_render_pass.h
            vc_pipeline.c               # Pipeline creation + cache
            vc_pipeline.h
            vc_shader.c                 # Shader module management
            vc_shader.h
            vc_texture.c                # Texture image management
            vc_texture.h
            vc_batch.c                  # Triangle batching + draw
            vc_batch.h
            vc_readback.c               # LFB readback
            vc_readback.h
            vc_internal.h               # Shared internal types + macros
            shaders/
                voodoo_uber.vert        # Vertex shader source (GLSL)
                voodoo_uber.frag        # Fragment shader source (GLSL)
            third_party/
                volk/
                    volk.h              # Vendored from github.com/zeux/volk
                    volk.c
                    vulkan/             # Vendored Vulkan headers (from SDK)
                        vulkan.h
                        vulkan_core.h
                        vk_platform.h
                        ... (platform headers)
                vma/
                    vk_mem_alloc.h      # Vendored from GPUOpen
                    vma_impl.cpp        # Implementation TU (defines VMA_IMPLEMENTATION)
```

### What Gets Checked Into Git

- All source files (`.c`, `.h`, `.cpp`, `.cmake`)
- Shader source files (`.vert`, `.frag`)
- Vendored third-party headers and sources (`third_party/`)
- Documentation and plan files (`videocommon-plan/`)

### What Does NOT Get Checked Into Git

- Compiled SPIR-V files (`.spv`) -- generated at build time
- Generated C headers (`*_spv.h`) -- generated at build time
- Build artifacts (`build/`)

---

## 9. Full Diff Summary

### New Files

| File | Purpose |
|------|---------|
| `src/video/videocommon/CMakeLists.txt` | VideoCommon build definition |
| `src/video/videocommon/cmake/CompileShader.cmake` | Shader compilation function |
| `src/video/videocommon/cmake/SpvToHeader.cmake` | SPIR-V to C header script |
| `src/video/videocommon/vc_core.c` | Vulkan init |
| `src/video/videocommon/vc_core.h` | Internal header |
| `src/video/videocommon/vc_thread.c` | Render thread |
| `src/video/videocommon/vc_thread.h` | |
| `src/video/videocommon/vc_render_pass.c` | Render pass |
| `src/video/videocommon/vc_render_pass.h` | |
| `src/video/videocommon/vc_pipeline.c` | Pipeline cache |
| `src/video/videocommon/vc_pipeline.h` | |
| `src/video/videocommon/vc_shader.c` | Shader modules |
| `src/video/videocommon/vc_shader.h` | |
| `src/video/videocommon/vc_texture.c` | Texture management |
| `src/video/videocommon/vc_texture.h` | |
| `src/video/videocommon/vc_batch.c` | Triangle batching |
| `src/video/videocommon/vc_batch.h` | |
| `src/video/videocommon/vc_readback.c` | LFB readback |
| `src/video/videocommon/vc_readback.h` | |
| `src/video/videocommon/vc_internal.h` | Shared internal types |
| `src/video/videocommon/shaders/voodoo_uber.vert` | Vertex shader |
| `src/video/videocommon/shaders/voodoo_uber.frag` | Fragment shader |
| `src/video/videocommon/third_party/volk/volk.h` | Vendored volk |
| `src/video/videocommon/third_party/volk/volk.c` | |
| `src/video/videocommon/third_party/volk/vulkan/` | Vendored Vulkan headers |
| `src/video/videocommon/third_party/vma/vk_mem_alloc.h` | Vendored VMA |
| `src/video/videocommon/third_party/vma/vma_impl.cpp` | VMA implementation TU |
| `src/include/86box/videocommon.h` | Public API header |

### Modified Files

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add `option(VIDEOCOMMON ...)` |
| `src/CMakeLists.txt` | Add `USE_VIDEOCOMMON` compile def, link `videocommon` |
| `src/video/CMakeLists.txt` | Add `add_subdirectory(videocommon)` |
| `src/include/86box/vid_voodoo_common.h` | Add `vc_ctx`, `use_gpu_renderer` fields |
| `src/video/vid_voodoo.c` | Add `vc_init()`/`vc_close()` calls (guarded) |
| `src/video/vid_voodoo_render.c` | Add Vulkan path branch (guarded) |

### Build Commands

```bash
# Standard build WITHOUT VideoCommon (no change from current)
cmake -S . -B build --preset regular
cmake --build build

# Build WITH VideoCommon
cmake -S . -B build --preset regular -D VIDEOCOMMON=ON
cmake --build build

# macOS ARM64 with VideoCommon
cmake -S . -B build --preset regular \
    --toolchain ./cmake/llvm-macos-aarch64.cmake \
    -D NEW_DYNAREC=ON -D QT=ON -D VIDEOCOMMON=ON \
    -D Qt5_ROOT=$(brew --prefix qt@5) \
    -D Qt5LinguistTools_ROOT=$(brew --prefix qt@5) \
    -D OpenAL_ROOT=$(brew --prefix openal-soft) \
    -D LIBSERIALPORT_ROOT=$(brew --prefix libserialport)
cmake --build build
```

### Prerequisites for VIDEOCOMMON=ON

| Platform | Requirement | Install |
|----------|-------------|---------|
| macOS | `glslc`, MoltenVK (runtime) | `brew install shaderc molten-vk` |
| Linux | `glslc`, Mesa Vulkan drivers | `apt install glslc mesa-vulkan-drivers` |
| Windows | Vulkan SDK (includes glslc) | Install LunarG Vulkan SDK |
| Pi 5 | `glslc`, Mesa V3DV | `apt install glslc mesa-vulkan-drivers` |

Note: Vulkan SDK is NOT needed at build time (volk eliminates the link
dependency). Only `glslc` is required for shader compilation. At runtime,
a Vulkan-capable driver must be installed.
