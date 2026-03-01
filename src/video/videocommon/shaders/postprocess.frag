/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon post-process blit -- fragment stage.
 *
 *          Samples the offscreen color attachment and outputs to the
 *          swapchain image.  Nearest-neighbor filtering is configured
 *          at sampler creation (VK_FILTER_NEAREST), not in shader.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#version 450

layout(set = 0, binding = 0) uniform sampler2D offscreen_tex;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = texture(offscreen_tex, in_uv);
}
