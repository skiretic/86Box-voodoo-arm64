/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon shader module -- SPIR-V loading and
 *          VkShaderModule creation for the Voodoo uber-shader.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_SHADER_H
#define VIDEOCOMMON_SHADER_H

#include "vc_internal.h"

/* Holds the loaded SPIR-V shader modules. */
typedef struct vc_shaders_t {
    VkShaderModule vert;
    VkShaderModule frag;
} vc_shaders_t;

/* Create shader modules from embedded SPIR-V bytecode.
   Returns 0 on success, -1 on failure. */
int  vc_shaders_create(vc_ctx_t *ctx, vc_shaders_t *shaders);

/* Destroy shader modules. */
void vc_shaders_destroy(vc_ctx_t *ctx, vc_shaders_t *shaders);

#endif /* VIDEOCOMMON_SHADER_H */
