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
 *            Phase 4: + texture fetch, basic texture combine
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

/* ---- Descriptor Set 0: Texture Samplers -------------------------------- */
layout(set = 0, binding = 0) uniform sampler2D tex_tmu0;

/* ---- Inputs from Vertex Shader ---------------------------------------- */
layout(location = 0) noperspective in vec4  vColor;
layout(location = 1) noperspective in vec3  vTexCoord0;
layout(location = 2) noperspective in vec3  vTexCoord1;
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

/* ---- Helper: float [0,1] -> 8-bit integer ------------------------------- */
uint to8(float v) {
    return uint(clamp(v, 0.0, 1.0) * 255.0 + 0.5);
}

void main() {
    /* --- STAGE 1: Stipple test (Phase 6) --- */
    /* TODO */

    /* --- STAGE 2-3: Depth (Vulkan fixed-function via EDS1, Phase 5) --- */

    /* ==================================================================
     * STAGE 5: Texture fetch (Phase 4)
     *
     * When FBZCP_TEXTURE_ENABLED (bit 27 of fbzColorPath) is set,
     * sample TMU0 using perspective-corrected texture coordinates.
     * vTexCoord0 = (S/W, T/W, 1/W) from vertex shader (smooth interp).
     * Perspective divide: U = (S/W) / (1/W), V = (T/W) / (1/W).
     * ================================================================== */
    vec4 texel = vec4(1.0);
    bool textured = (pc.fbzColorPath & (1u << 27)) != 0u;

    if (textured) {
        float oow = vTexCoord0.z;
        if (oow > 0.0) {
            vec2 uv = vTexCoord0.xy / oow;

            /* Voodoo S/T are in texel coordinates (0..width, 0..height).
               Normalize to [0,1] using the actual texture dimensions. */
            vec2 tex_size = vec2(textureSize(tex_tmu0, 0));
            uv /= tex_size;

            texel = texture(tex_tmu0, uv);
        } else {
            texel = texture(tex_tmu0, vec2(0.0));
        }
    }

    /* ==================================================================
     * STAGE 5b: Texture combine (Phase 5.11)
     *
     * textureMode0 bits 12-29 control a configurable blend formula
     * that processes the raw texel before color combine consumes it.
     * For single-TMU, c_other = 0 (no upstream TMU), c_local = texel.
     *
     * Passthrough shortcut: if all combine bits are zero, skip entirely.
     * ================================================================== */
    if (textured && (pc.textureMode0 & 0x3FFFF000u) != 0u) {
        vec3 tc_other = vec3(0.0);  /* no upstream TMU yet */
        float tca_other = 0.0;
        vec3 tc_local = texel.rgb;
        float tca_local = texel.a;

        /* ---- Color combine ---- */

        /* Step 1: zero_other */
        bool tc_zero_other = (pc.textureMode0 & (1u << 12)) != 0u;
        vec3 tc_src = tc_zero_other ? vec3(0.0) : tc_other;

        /* Step 2: sub_clocal */
        if ((pc.textureMode0 & (1u << 13)) != 0u)
            tc_src -= tc_local;

        /* Step 3: mselect factor */
        uint tc_ms = (pc.textureMode0 >> 14) & 7u;
        vec3 tc_factor;
        switch (tc_ms) {
            case 0u:  tc_factor = vec3(0.0);               break; /* ZERO */
            case 1u:  tc_factor = tc_local;                 break; /* CLOCAL */
            case 2u:  tc_factor = vec3(tca_other);          break; /* AOTHER */
            case 3u:  tc_factor = vec3(tca_local);          break; /* ALOCAL */
            case 4u:  tc_factor = vec3(0.0);                break; /* DETAIL (TODO) */
            case 5u:  tc_factor = vec3(0.0);                break; /* LOD_FRAC (TODO) */
            default:  tc_factor = vec3(0.0);                break;
        }

        /* Reverse blend: if bit NOT set, invert factor */
        if ((pc.textureMode0 & (1u << 17)) == 0u)
            tc_factor = vec3(1.0) - tc_factor;

        tc_src *= tc_factor;

        /* Step 4: add */
        if ((pc.textureMode0 & (1u << 18)) != 0u)
            tc_src += tc_local;
        else if ((pc.textureMode0 & (1u << 19)) != 0u)
            tc_src += vec3(tca_local);

        /* Step 5: clamp */
        tc_src = clamp(tc_src, 0.0, 1.0);

        /* Step 6: invert output */
        if ((pc.textureMode0 & (1u << 20)) != 0u)
            tc_src = vec3(1.0) - tc_src;

        /* ---- Alpha combine ---- */

        /* Step 1: tca_zero_other */
        bool tca_z_other = (pc.textureMode0 & (1u << 21)) != 0u;
        float tca_src = tca_z_other ? 0.0 : tca_other;

        /* Step 2: tca_sub_clocal */
        if ((pc.textureMode0 & (1u << 22)) != 0u)
            tca_src -= tca_local;

        /* Step 3: tca_mselect factor */
        uint tca_ms = (pc.textureMode0 >> 23) & 7u;
        float tca_factor;
        switch (tca_ms) {
            case 0u:  tca_factor = 0.0;               break; /* ZERO */
            case 1u:  tca_factor = tca_local;          break; /* CLOCAL */
            case 2u:  tca_factor = tca_other;          break; /* AOTHER */
            case 3u:  tca_factor = tca_local;          break; /* ALOCAL */
            case 4u:  tca_factor = 0.0;                break; /* DETAIL (TODO) */
            case 5u:  tca_factor = 0.0;                break; /* LOD_FRAC (TODO) */
            default:  tca_factor = 0.0;                break;
        }

        /* Reverse blend: if bit NOT set, invert factor */
        if ((pc.textureMode0 & (1u << 26)) == 0u)
            tca_factor = 1.0 - tca_factor;

        tca_src *= tca_factor;

        /* Step 4: add (both clocal and alocal add a_local for alpha) */
        if ((pc.textureMode0 & (1u << 27)) != 0u ||
            (pc.textureMode0 & (1u << 28)) != 0u)
            tca_src += tca_local;

        /* Step 5: clamp */
        tca_src = clamp(tca_src, 0.0, 1.0);

        /* Step 6: invert output */
        if ((pc.textureMode0 & (1u << 29)) != 0u)
            tca_src = 1.0 - tca_src;

        /* Replace texel with combined result */
        texel = vec4(tc_src, tca_src);
    }

    /* ==================================================================
     * STAGE 6-7: Color/alpha combine (Phase 5 -- full pipeline)
     *
     * fbzColorPath bit layout for color combine:
     *   [1:0]   cc_rgbselect            -- other color source
     *   [3:2]   a_sel                   -- other alpha source
     *   [4]     cc_localselect          -- local color source
     *   [6:5]   cca_localselect         -- local alpha source
     *   [7]     cc_localselect_override -- use texel alpha bit 7
     *   [8]     cc_zero_other
     *   [9]     cc_sub_clocal
     *   [12:10] cc_mselect
     *   [13]    cc_reverse_blend
     *   [15:14] cc_add
     *   [16]    cc_invert_output
     *   [17]    cca_zero_other
     *   [18]    cca_sub_clocal
     *   [21:19] cca_mselect
     *   [22]    cca_reverse_blend
     *   [24:23] cca_add
     *   [25]    cca_invert_output
     * ================================================================== */

    vec4 c0 = unpackColor(pc.color0);
    vec4 c1 = unpackColor(pc.color1);

    /* ---- Color "other" source ---- */
    uint cc_rgbsel = pc.fbzColorPath & 3u;
    vec3 c_other;
    switch (cc_rgbsel) {
        case 0u:  c_other = vColor.rgb;   break; /* iterated */
        case 1u:  c_other = texel.rgb;    break; /* texture */
        case 2u:  c_other = c1.rgb;       break; /* color1 */
        default:  c_other = vec3(0.0);    break; /* LFB placeholder */
    }

    /* ---- Alpha "other" source ---- */
    uint a_sel = (pc.fbzColorPath >> 2) & 3u;
    float a_other;
    switch (a_sel) {
        case 0u:  a_other = vColor.a;   break; /* iterated alpha */
        case 1u:  a_other = texel.a;    break; /* texture alpha */
        case 2u:  a_other = c1.a;       break; /* color1 alpha */
        default:  a_other = 0.0;        break; /* LFB placeholder */
    }

    /* ---- Chroma key test ----
     * fbzMode bit 1: enable chroma key.
     * Compare "other" RGB against chromaKey as 8-bit integers. */
    if ((pc.fbzMode & (1u << 1)) != 0u) {
        uint r8 = to8(c_other.r);
        uint g8 = to8(c_other.g);
        uint b8 = to8(c_other.b);
        uint ck_r = (pc.chromaKey >> 16) & 0xFFu;
        uint ck_g = (pc.chromaKey >>  8) & 0xFFu;
        uint ck_b =  pc.chromaKey        & 0xFFu;
        if (r8 == ck_r && g8 == ck_g && b8 == ck_b)
            discard;
    }

    /* ---- Alpha mask test ----
     * fbzMode bit 13: discard if low bit of aother (8-bit) is 0. */
    if ((pc.fbzMode & (1u << 13)) != 0u) {
        uint a8 = to8(a_other);
        if ((a8 & 1u) == 0u)
            discard;
    }

    /* ---- Color local source ---- */
    uint cc_localselect = (pc.fbzColorPath >> 4) & 1u;
    bool cc_localselect_override = (pc.fbzColorPath & (1u << 7)) != 0u;

    vec3 c_local;
    if (cc_localselect_override) {
        /* Use texel alpha bit 7 to choose: 1 = color0, 0 = iterated. */
        uint tex_a8 = to8(texel.a);
        c_local = ((tex_a8 & 0x80u) != 0u) ? c0.rgb : vColor.rgb;
    } else {
        c_local = (cc_localselect != 0u) ? c0.rgb : vColor.rgb;
    }

    /* ---- Alpha local source ---- */
    uint cca_localselect = (pc.fbzColorPath >> 5) & 3u;
    float a_local;
    switch (cca_localselect) {
        case 0u:  a_local = vColor.a;   break; /* iterated alpha */
        case 1u:  a_local = c0.a;       break; /* color0 alpha */
        case 2u:  a_local = vDepth;     break; /* iterated Z (as alpha) */
        default:  a_local = 0.0;        break;
    }

    /* ==================================================================
     * Color combine pipeline:
     *   src = zero_other ? 0 : other
     *   if (sub_clocal) src -= local
     *   src *= mselect_factor (with reverse blend)
     *   src += add
     *   clamp [0,1]
     *   if (invert) src = 1 - src
     *
     * SW renderer reverse blend: factor ^= 0xFF; factor++;
     * In float: if (!reverse) factor = 1.0 - factor.
     * ================================================================== */

    /* Step 1: zero_other */
    bool cc_zero_other = (pc.fbzColorPath & (1u << 8)) != 0u;
    vec3 c_src = cc_zero_other ? vec3(0.0) : c_other;

    /* Step 2: sub_clocal */
    bool cc_sub_clocal = (pc.fbzColorPath & (1u << 9)) != 0u;
    if (cc_sub_clocal)
        c_src -= c_local;

    /* Step 3: mselect factor */
    uint cc_mselect = (pc.fbzColorPath >> 10) & 7u;
    vec3 c_factor;
    switch (cc_mselect) {
        case 0u:  c_factor = vec3(0.0);               break; /* zero */
        case 1u:  c_factor = c_local;                  break; /* clocal */
        case 2u:  c_factor = vec3(a_other);            break; /* aother */
        case 3u:  c_factor = vec3(a_local);            break; /* alocal */
        case 4u:  c_factor = vec3(texel.a);            break; /* texture alpha */
        case 5u:  c_factor = texel.rgb;                break; /* texture RGB */
        default:  c_factor = vec3(0.0);                break;
    }

    /* Reverse blend */
    bool cc_reverse = (pc.fbzColorPath & (1u << 13)) != 0u;
    if (!cc_reverse)
        c_factor = vec3(1.0) - c_factor;

    c_src *= c_factor;

    /* Step 4: add */
    uint cc_add = (pc.fbzColorPath >> 14) & 3u;
    switch (cc_add) {
        case 0u:  /* add zero -- nothing */                break;
        case 1u:  c_src += c_local;                        break;
        case 2u:  c_src += vec3(a_local);                  break;
        default:                                           break;
    }

    /* Step 5: clamp */
    c_src = clamp(c_src, 0.0, 1.0);

    /* Step 6: invert output */
    bool cc_invert = (pc.fbzColorPath & (1u << 16)) != 0u;
    if (cc_invert)
        c_src = vec3(1.0) - c_src;

    /* ==================================================================
     * Alpha combine pipeline (same structure, scalar):
     *   src = zero_other ? 0 : a_other
     *   if (sub_clocal) src -= a_local
     *   src *= factor (with reverse blend)
     *   src += add
     *   clamp [0,1]
     *   if (invert) src = 1 - src
     * ================================================================== */

    /* Step 1: cca_zero_other */
    bool cca_zero_other = (pc.fbzColorPath & (1u << 17)) != 0u;
    float a_src = cca_zero_other ? 0.0 : a_other;

    /* Step 2: cca_sub_clocal */
    bool cca_sub_clocal = (pc.fbzColorPath & (1u << 18)) != 0u;
    if (cca_sub_clocal)
        a_src -= a_local;

    /* Step 3: cca_mselect factor */
    uint cca_mselect = (pc.fbzColorPath >> 19) & 7u;
    float a_factor;
    switch (cca_mselect) {
        case 0u:  a_factor = 0.0;            break; /* zero */
        case 1u:  a_factor = a_local;        break; /* alocal */
        case 2u:  a_factor = a_other;        break; /* aother */
        case 3u:  a_factor = a_local;        break; /* alocal (duplicate) */
        case 4u:  a_factor = texel.a;        break; /* texture alpha */
        default:  a_factor = 0.0;            break;
    }

    /* Reverse blend */
    bool cca_reverse = (pc.fbzColorPath & (1u << 22)) != 0u;
    if (!cca_reverse)
        a_factor = 1.0 - a_factor;

    a_src *= a_factor;

    /* Step 4: cca_add -- any nonzero value adds a_local (SW: if (cca_add) a += a_local) */
    uint cca_add = (pc.fbzColorPath >> 23) & 3u;
    if (cca_add != 0u)
        a_src += a_local;

    /* Step 5: clamp */
    a_src = clamp(a_src, 0.0, 1.0);

    /* Step 6: cca_invert output */
    bool cca_invert = (pc.fbzColorPath & (1u << 25)) != 0u;
    if (cca_invert)
        a_src = 1.0 - a_src;

    /* Assemble final combined color. */
    vec4 combined = vec4(c_src, a_src);

    /* --- STAGE 14: Save color-before-fog (Phase 7) --- */
    /* TODO */

    /* --- STAGE 15: Fog (Phase 6) --- */
    /* Consume vFog so the compiler keeps the vertex-to-fragment interface
     * intact and Vulkan validation does not warn about an unconsumed
     * output at location 4.  The multiply-by-zero is a no-op. */
    combined.rgb += vFog * 0.0;

    /* ==================================================================
     * STAGE 16: Alpha test
     *
     * alphaMode bits:
     *   [0]     enable
     *   [3:1]   function (0=NEVER..7=ALWAYS)
     *   [31:24] reference value (8-bit)
     *
     * Compare combined alpha (as 8-bit integer) against reference.
     * ================================================================== */
    if ((pc.alphaMode & 1u) != 0u) {
        uint afunc = (pc.alphaMode >> 1) & 7u;
        uint aref  = (pc.alphaMode >> 24) & 0xFFu;
        uint aval  = to8(combined.a);

        bool pass = false;
        switch (afunc) {
            case 0u:  pass = false;            break; /* NEVER */
            case 1u:  pass = (aval < aref);    break; /* LESS */
            case 2u:  pass = (aval == aref);   break; /* EQUAL */
            case 3u:  pass = (aval <= aref);   break; /* LEQUAL */
            case 4u:  pass = (aval > aref);    break; /* GREATER */
            case 5u:  pass = (aval != aref);   break; /* NOTEQUAL */
            case 6u:  pass = (aval >= aref);   break; /* GEQUAL */
            case 7u:  pass = true;             break; /* ALWAYS */
        }
        if (!pass)
            discard;
    }

    /* --- STAGE 17-18: Dither sub + alpha blend (Phase 6) --- */
    /* TODO */

    /* --- STAGE 19: Output dither (Phase 6) --- */
    /* TODO */

    /* --- Output --- */
    fragColor = combined;
}
