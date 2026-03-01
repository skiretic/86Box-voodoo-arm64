/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon Voodoo uber-shader -- fragment stage.
 *
 *          Implements the Voodoo pixel pipeline as a single shader with
 *          push-constant-driven branching.  Pipeline stages are added
 *          incrementally across phases:
 *
 *            Phase 2: iterated color only (flat-shaded triangles)
 *            Phase 4: + texture fetch, texture combine
 *            Phase 5: + color/alpha combine, alpha test, chroma key
 *            Phase 6: + fog, dither, stipple, depth source/bias
 *            Phase 7: + LFB access, ACOLORBEFOREFOG shader blend
 *
 *          The push constant block and descriptor bindings are declared
 *          in full from day one so the pipeline layout never changes.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#version 450

/* ---- Push Constants (64 bytes, full layout -- never changes) ------------ */
layout(push_constant, std430) uniform PushConstants {
    uint  fbzMode;           /* offset  0 */
    uint  fbzColorPath;      /* offset  4 */
    uint  alphaMode;         /* offset  8 */
    uint  fogMode;           /* offset 12 */
    uint  textureMode0;      /* offset 16 */
    uint  textureMode1;      /* offset 20 */
    uint  color0;            /* offset 24 */
    uint  color1;            /* offset 28 */
    uint  chromaKey;         /* offset 32 */
    uint  fogColor;          /* offset 36 */
    uint  zaColor;           /* offset 40 */
    uint  stipple;           /* offset 44 */
    uint  detail0;           /* offset 48 */
    uint  detail1;           /* offset 52 */
    float fb_width;          /* offset 56 */
    float fb_height;         /* offset 60 */
} pc;

/* ---- Inputs from Vertex Shader ---------------------------------------- */
layout(location = 0) noperspective in vec4  vColor;
layout(location = 1)               in vec3  vTexCoord0;
layout(location = 2)               in vec3  vTexCoord1;
layout(location = 3) noperspective in float vDepth;
layout(location = 4) noperspective in float vFog;

/* ---- Output ------------------------------------------------------------- */
layout(location = 0) out vec4 fragColor;

/* ---- Helper: unpack 0xAARRGGBB -> vec4(R,G,B,A) normalized ------------- */
vec4 unpackColor(uint c) {
    return vec4(
        float((c >> 16) & 0xFFu) / 255.0,
        float((c >>  8) & 0xFFu) / 255.0,
        float( c        & 0xFFu) / 255.0,
        float((c >> 24) & 0xFFu) / 255.0
    );
}

/* ---- Helper: unpack 0x00RRGGBB -> vec3(R,G,B) normalized --------------- */
vec3 unpackRGB(uint c) {
    return vec3(
        float((c >> 16) & 0xFFu) / 255.0,
        float((c >>  8) & 0xFFu) / 255.0,
        float( c        & 0xFFu) / 255.0
    );
}

void main() {
    /* ==================================================================
     * Phase 2: Flat-shaded color output only.
     *
     * With default fbzColorPath (all bits zero):
     *   rgb_sel = 0 -> cother = iterated RGB
     *   cc_zero_other = 0 -> src = cother
     *   cc_mselect = 0 (ZERO), cc_reverse_blend = 0 -> factor = 1.0
     *   => src = cother (pass-through identity)
     *
     * Same for alpha combine (cca_* bits 17-25 zero -> pass-through).
     * So Phase 2 just outputs vColor.
     * ================================================================== */

    /* --- STAGE 1: Stipple test (Phase 6) --- */
    /* TODO */

    /* --- STAGE 2-3: Depth (Vulkan fixed-function, Phase 5) --- */

    /* --- STAGE 5: Texture fetch (Phase 4) --- */
    /* TODO: TMU0/TMU1 fetch and combine */

    /* --- STAGE 6-7: Color/alpha select --- */
    vec4 src = vColor;

    /* --- STAGE 8: Chroma key test (Phase 5) --- */
    /* TODO */

    /* --- STAGE 9-11: Alpha select, alpha mask (Phase 5) --- */

    /* --- STAGE 12-13: Color/alpha combine (Phase 5) --- */

    /* --- STAGE 14: Save color-before-fog (Phase 7) --- */

    /* --- STAGE 15: Fog (Phase 6) --- */
    /* TODO */

    /* --- STAGE 16: Alpha test (Phase 5) --- */
    /* TODO */

    /* --- STAGE 17-18: Dither sub + alpha blend (Phase 5/6) --- */

    /* --- STAGE 19: Output dither (Phase 6) --- */
    /* TODO */

    /* --- Output --- */
    fragColor = src;
}
