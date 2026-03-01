/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon post-process blit -- vertex stage.
 *
 *          Fullscreen triangle from gl_VertexIndex (no vertex buffer).
 *          Three vertices cover the entire clip space; clipping removes
 *          the overdraw for free.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#version 450

layout(location = 0) out vec2 out_uv;

void main()
{
    /* Generate fullscreen triangle from vertex index.
       Vertex 0: (-1, -1), UV (0, 0)
       Vertex 1: ( 3, -1), UV (2, 0)
       Vertex 2: (-1,  3), UV (0, 2)
       The triangle covers the entire clip space [-1,1] x [-1,1].
       Clipping removes the parts outside the viewport for free.
       UVs >1 are clamped by the sampler (CLAMP_TO_EDGE). */
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(out_uv * 2.0 - 1.0, 0.0, 1.0);
}
