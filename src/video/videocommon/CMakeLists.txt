#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          VideoCommon -- GPU-accelerated rendering infrastructure.
#          OBJECT library: Vulkan core, GPU thread, SPSC ring,
#          shader loading, graphics pipeline.
#
# Authors: skiretic
#
#          Copyright 2026 skiretic.
#

# -------------------------------------------------------------------------- #
#  SPIR-V shader compilation                                                  #
# -------------------------------------------------------------------------- #

find_program(GLSLC glslc
    HINTS $ENV{VULKAN_SDK}/bin /usr/bin /usr/local/bin
)

if(NOT GLSLC)
    message(FATAL_ERROR "VideoCommon: glslc not found. Install the Vulkan SDK or set VULKAN_SDK.")
endif()

message(STATUS "VideoCommon: using glslc at ${GLSLC}")

# Output directory for generated SPIR-V C headers.
set(VC_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${VC_GENERATED_DIR}")

# compile_shader(INPUT_GLSL OUTPUT_HEADER ARRAY_NAME)
# Compiles a GLSL shader to SPIR-V, then generates a C header with the
# bytecode as a uint32_t array.
function(compile_shader INPUT_GLSL OUTPUT_HEADER ARRAY_NAME)
    # Intermediate SPIR-V binary.
    get_filename_component(BASENAME "${INPUT_GLSL}" NAME_WE)
    get_filename_component(EXT "${INPUT_GLSL}" LAST_EXT)
    string(REPLACE "." "" EXT "${EXT}")
    set(SPV_FILE "${VC_GENERATED_DIR}/${BASENAME}_${EXT}.spv")

    add_custom_command(
        OUTPUT "${SPV_FILE}"
        COMMAND "${GLSLC}" -O --target-env=vulkan1.2 -o "${SPV_FILE}" "${INPUT_GLSL}"
        DEPENDS "${INPUT_GLSL}"
        COMMENT "Compiling SPIR-V: ${INPUT_GLSL} -> ${SPV_FILE}"
        VERBATIM
    )

    # Generate C header from SPIR-V binary via a small CMake script.
    # SpvToHeader.cmake expects: INPUT, OUTPUT, ARRAY.
    add_custom_command(
        OUTPUT "${OUTPUT_HEADER}"
        COMMAND "${CMAKE_COMMAND}"
            -D "INPUT=${SPV_FILE}"
            -D "OUTPUT=${OUTPUT_HEADER}"
            -D "ARRAY=${ARRAY_NAME}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpvToHeader.cmake"
        DEPENDS "${SPV_FILE}"
        COMMENT "Embedding SPIR-V: ${SPV_FILE} -> ${OUTPUT_HEADER}"
        VERBATIM
    )
endfunction()

# Compile vertex and fragment shaders.
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

compile_shader(
    "${SHADER_DIR}/voodoo_uber.vert"
    "${VC_GENERATED_DIR}/voodoo_uber_vert.h"
    "voodoo_uber_vert_spv"
)

compile_shader(
    "${SHADER_DIR}/voodoo_uber.frag"
    "${VC_GENERATED_DIR}/voodoo_uber_frag.h"
    "voodoo_uber_frag_spv"
)

compile_shader(
    "${SHADER_DIR}/postprocess.vert"
    "${VC_GENERATED_DIR}/postprocess_vert.h"
    "postprocess_vert_spv"
)

compile_shader(
    "${SHADER_DIR}/postprocess.frag"
    "${VC_GENERATED_DIR}/postprocess_frag.h"
    "postprocess_frag_spv"
)

# -------------------------------------------------------------------------- #
#  OBJECT library                                                              #
# -------------------------------------------------------------------------- #

set(VIDEOCOMMON_SOURCES
    vc_core.c
    vc_thread.c
    vc_render_pass.c
    vc_batch.c
    vc_shader.c
    vc_pipeline.c
    vc_display.c
    vc_vma_impl.cpp
)

# macOS: Objective-C surface helper for CAMetalLayer extraction.
if(APPLE)
    list(APPEND VIDEOCOMMON_SOURCES vc_macos_surface.m)
endif()

add_library(videocommon OBJECT
    ${VIDEOCOMMON_SOURCES}

    # Generated SPIR-V headers (ensures they are built before compilation).
    "${VC_GENERATED_DIR}/voodoo_uber_vert.h"
    "${VC_GENERATED_DIR}/voodoo_uber_frag.h"
    "${VC_GENERATED_DIR}/postprocess_vert.h"
    "${VC_GENERATED_DIR}/postprocess_frag.h"
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

# Include paths: volk, VMA, project headers, generated SPIR-V headers.
target_include_directories(videocommon PRIVATE
    ${CMAKE_SOURCE_DIR}/extra/volk
    ${CMAKE_SOURCE_DIR}/extra/VMA
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${VC_GENERATED_DIR}
)

# Logging support: enable with -D ENABLE_VIDEOCOMMON_LOG=1
# (off by default; add to cmake invocation for debug builds)
option(ENABLE_VIDEOCOMMON_LOG "Enable VideoCommon debug logging" ON)
if(ENABLE_VIDEOCOMMON_LOG)
    target_compile_definitions(videocommon PRIVATE ENABLE_VIDEOCOMMON_LOG=1)
endif()
