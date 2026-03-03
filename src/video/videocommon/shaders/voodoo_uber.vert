/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon Voodoo uber-shader -- vertex stage.
 *
 *          Accepts screen-space pixel coordinates and converts them to
 *          Vulkan NDC.  Passes all vertex attributes to the fragment
 *          shader for the Voodoo pixel pipeline.
 *
 *          Vulkan Y-down matches Voodoo Y-down -- no flip needed.
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

/* ---- Vertex Inputs (matches vc_vertex_t, 72 bytes per vertex) ---------- */
layout(location = 0) in vec2  inPosition;    /* screen-space X, Y (pixels)  */
layout(location = 1) in float inDepth;       /* Z depth, normalized [0,1]   */
layout(location = 2) in float inOOW;         /* 1/W for perspective correct */
layout(location = 3) in vec4  inColor;       /* RGBA, normalized [0,1]      */
layout(location = 4) in vec3  inTexCoord0;   /* TMU0: S/W, T/W, 1/W        */
layout(location = 5) in vec3  inTexCoord1;   /* TMU1: S/W, T/W, 1/W        */
layout(location = 6) in float inFog;         /* fog coordinate              */

/* ---- Outputs to Fragment Shader ---------------------------------------- */
layout(location = 0) noperspective out vec4  vColor;     /* iterated RGBA   */
layout(location = 1) noperspective out vec3  vTexCoord0; /* S/W, T/W, 1/W   */
layout(location = 2) noperspective out vec3  vTexCoord1; /* S/W, T/W, 1/W   */
layout(location = 3) noperspective out float vDepth;     /* Voodoo Z depth  */
layout(location = 4) noperspective out float vFog;       /* fog coordinate  */

void main() {
    /*
     * All varyings use noperspective interpolation because Voodoo
     * hardware iterates S/W, T/W, 1/W linearly in screen space and
     * does the perspective divide per-pixel in the texture unit.
     * Using smooth (perspective-correct) interpolation would
     * double-correct these values and cause mosaic/scrambling
     * artifacts.  With no smooth varyings, gl_Position.w = 1.0 and
     * the clip-space position is simply NDC.
     */
    float W = 1.0;

    /*
     * Screen-space to Vulkan NDC.
     *
     * Vulkan clip space: X [-1,+1] left-right, Y [-1,+1] top-bottom,
     *                    Z [0,1] near-far.
     * Voodoo screen:     X,Y in pixels, Y=0 at top.
     *
     * Vulkan Y-down matches Voodoo Y-down -- NO flip.
     */
    float ndc_x = (2.0 * inPosition.x / pc.fb_width)  - 1.0;
    float ndc_y = (2.0 * inPosition.y / pc.fb_height) - 1.0;

    /*
     * Straight NDC -- no perspective encoding needed since all
     * varyings are noperspective.
     */
    gl_Position = vec4(ndc_x, ndc_y, inDepth, W);

    /* Colors: noperspective (Voodoo Gouraud shading is screen-space). */
    vColor = inColor;

    /* Texture coords: noperspective (linear in screen space). */
    vTexCoord0 = inTexCoord0;
    vTexCoord1 = inTexCoord1;

    /* Depth: noperspective (Voodoo Z is linearly interpolated). */
    vDepth = inDepth;

    /* Fog coordinate: noperspective.
     * Pass through inFog directly.  Also consume inOOW so the compiler
     * does not optimise away the vertex attribute at location 2 (which
     * must remain in the pipeline vertex-input state to match
     * vc_vertex_t).  The multiplication by 1.0 is a no-op that keeps
     * inOOW alive without changing the result. */
    vFog = inFog + inOOW * 0.0;
}
