#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          VideoCommon -- GPU-accelerated rendering infrastructure.
#          OBJECT library: Vulkan core, GPU thread, SPSC ring.
#
# Authors: Anthony Campbell
#
#          Copyright 2026 Anthony Campbell.
#

add_library(videocommon OBJECT
    vc_core.c
    vc_thread.c
    vc_vma_impl.cpp
)

# VK_NO_PROTOTYPES: volk loads all Vulkan functions dynamically.
target_compile_definitions(videocommon PRIVATE
    VK_NO_PROTOTYPES
)

# VOLK_IMPLEMENTATION is defined in vc_core.c via #define, not here,
# because it must appear in exactly one translation unit.

# Platform-specific defines.
if(APPLE)
    target_compile_definitions(videocommon PRIVATE
        VK_USE_PLATFORM_METAL_EXT
    )
elseif(WIN32)
    target_compile_definitions(videocommon PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
    )
elseif(UNIX)
    target_compile_definitions(videocommon PRIVATE
        VK_USE_PLATFORM_XCB_KHR
    )
endif()

# USE_VIDEOCOMMON so the public header exposes the real API.
target_compile_definitions(videocommon PRIVATE USE_VIDEOCOMMON)

# Include paths: volk, VMA, project headers.
target_include_directories(videocommon PRIVATE
    ${CMAKE_SOURCE_DIR}/extra/volk
    ${CMAKE_SOURCE_DIR}/extra/VMA
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Logging support: enable with -D ENABLE_VIDEOCOMMON_LOG=1
# (off by default; add to cmake invocation for debug builds)
option(ENABLE_VIDEOCOMMON_LOG "Enable VideoCommon debug logging" ON)
if(ENABLE_VIDEOCOMMON_LOG)
    target_compile_definitions(videocommon PRIVATE ENABLE_VIDEOCOMMON_LOG=1)
endif()
