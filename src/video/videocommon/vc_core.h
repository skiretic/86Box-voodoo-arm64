/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon core -- internal declarations for Vulkan
 *          instance/device creation, VMA init, and capability detection.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEOCOMMON_CORE_H
#define VIDEOCOMMON_CORE_H

#include "vc_internal.h"

/* Create and destroy the VideoCommon context (Vulkan instance, device, VMA). */
vc_ctx_t *vc_init(void);
void      vc_destroy(vc_ctx_t *ctx);

#endif /* VIDEOCOMMON_CORE_H */
