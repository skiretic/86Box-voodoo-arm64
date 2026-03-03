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
 *            Phase 6: + fog, stipple, dither, W-buffer, depth bias/source,
 *                       dual-source blending (ACOLORBEFOREFOG)
 *            Phase 7: + LFB access
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
layout(set = 0, binding = 1) uniform sampler2D tex_tmu1;
layout(set = 0, binding = 2) uniform sampler2D fog_table; /* 64x1 R8G8_UNORM */

/* ---- Inputs from Vertex Shader ---------------------------------------- */
layout(location = 0) noperspective in vec4  vColor;
layout(location = 1) noperspective in vec3  vTexCoord0;
layout(location = 2) noperspective in vec3  vTexCoord1;
layout(location = 3) noperspective in float vDepth;
layout(location = 4) noperspective in float vOOW;       /* 1/W for fog */

/* ---- Outputs ------------------------------------------------------------ */
layout(location = 0, index = 0) out vec4 fragColor;
layout(location = 0, index = 1) out vec4 fragColorPreFog; /* dual-source for ACOLORBEFOREFOG */

/* Allow depth writes from shader (W-buffer, depth bias, depth source). */
layout(depth_any) out float gl_FragDepth;

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

/* ---- Helper: clamp int to [0, 0xFFFF] like CLAMP16 in SW renderer ------- */
int clamp16(int v) {
    return clamp(v, 0, 0xFFFF);
}

/* ---- Helper: fls (find last set) for 16-bit value ----------------------- */
/* Returns the number of leading zeros before the MSB, matching voodoo_fls(). */
int voodoo_fls(uint val16) {
    /* findMSB returns -1 for 0, or bit position [0,15] of highest set bit.
       voodoo_fls counts leading zeros in 16-bit space: 15 - findMSB. */
    int msb = findMSB(val16 & 0xFFFFu);
    return (msb < 0) ? 16 : (15 - msb);
}

/* ---- Helper: compute w_depth from 1/W (matches SW renderer) ------------- */
/* state->w in SW is a 50-bit fixed-point (18.32).  We receive the
   floating-point version (vOOW = startW / 2^32).  The algorithm classifies
   into three ranges based on the upper bits.  We replicate the integer
   logic using 32-bit math only (no double/int64 — GLSL portability). */
int compute_w_depth(float oow_f) {
    /* Overflow: if state->w >= 2^32 (bits 47:32 nonzero), w_depth = 0.
       Since state->w = oow_f * 2^32, overflow means oow_f >= 1.0. */
    if (oow_f >= 1.0 || oow_f < 0.0)
        return 0;

    /* Split the 32-bit value into upper and lower 16-bit halves.
       state->w (lower 32 bits) = oow_f * 2^32.  Since oow_f < 1.0,
       the value fits in uint32.  We split via:
         upper16 = floor(oow_f * 2^16)
         lower16 = fract(oow_f * 2^16) * 2^16                       */
    float scaled = oow_f * 65536.0;
    uint w_upper16 = uint(scaled) & 0xFFFFu;

    /* Underflow: bits 31:16 all zero -> 0xF001. */
    if (w_upper16 == 0u)
        return 0xF001;

    /* Reconstruct lower 32 bits of state->w for mantissa extraction. */
    float frac_part = fract(scaled);
    uint w_lower16 = uint(frac_part * 65536.0) & 0xFFFFu;
    uint w32 = (w_upper16 << 16) | w_lower16;

    int exp = voodoo_fls(w_upper16);
    int mant = int((~w32 >> (19 - exp)) & 0xFFFu);
    int wd = (exp << 12) + mant + 1;
    return min(wd, 0xFFFF);
}

/* ---- Dither: 4x4 Bayer matrix ------------------------------------------ */
/* Standard 4x4 ordered dither matrix matching dither_rb[]/dither_g[] tables
   in vid_voodoo_dither.h.  Values [0..15]. */
const int dither4x4[4][4] = int[4][4](
    int[4]( 0,  8,  2, 10),
    int[4](12,  4, 14,  6),
    int[4]( 3, 11,  1,  9),
    int[4](15,  7, 13,  5)
);

/* 2x2 Bayer matrix (top-left of the 4x4, rescaled). */
const int dither2x2[2][2] = int[2][2](
    int[2]( 0, 8),
    int[2](12, 4)
);

void main() {
    /* ==================================================================
     * STAGE 1: Stipple test
     *
     * fbzMode bit  2: stipple enable
     * fbzMode bit 12: 0 = rotating, 1 = pattern mode
     * ================================================================== */
    if ((pc.fbzMode & (1u << 2)) != 0u) {
        int x = int(gl_FragCoord.x);
        int y = int(gl_FragCoord.y);
        if ((pc.fbzMode & (1u << 12)) != 0u) {
            /* Pattern mode: index = (y&3)<<3 | (~x & 7) */
            int idx = ((y & 3) << 3) | (~x & 7);
            if ((pc.stipple & (1u << idx)) == 0u)
                discard;
        } else {
            /* Rotating mode: approximate with x mod 32 */
            int idx = x & 31;
            if ((pc.stipple & (1u << idx)) == 0u)
                discard;
        }
    }

    /* ==================================================================
     * STAGE 2-3: Depth / W-buffer / depth bias / depth source
     *
     * fbzMode bit  3: W-buffer (use w_depth instead of Z)
     * fbzMode bit 16: depth bias (add zaColor lower 16 as int16)
     * fbzMode bit 20: depth source (replace with zaColor & 0xFFFF)
     * ================================================================== */
    {
        bool need_depth_write = false;
        float final_depth = vDepth;

        if ((pc.fbzMode & (1u << 3)) != 0u) {
            /* W-buffer mode. */
            int wd = compute_w_depth(vOOW);
            final_depth = float(wd) / 65535.0;
            need_depth_write = true;
        }

        if ((pc.fbzMode & (1u << 16)) != 0u) {
            /* Depth bias: lower 16 bits of zaColor as signed int16. */
            int bias = int(pc.zaColor & 0xFFFFu);
            if (bias >= 32768) bias -= 65536; /* sign-extend */
            int d16 = int(final_depth * 65535.0);
            d16 = clamp16(d16 + bias);
            final_depth = float(d16) / 65535.0;
            need_depth_write = true;
        }

        if ((pc.fbzMode & (1u << 20)) != 0u) {
            /* Depth source: replace entirely with zaColor lower 16 bits. */
            final_depth = float(pc.zaColor & 0xFFFFu) / 65535.0;
            need_depth_write = true;
        }

        if (need_depth_write)
            gl_FragDepth = final_depth;
        else
            gl_FragDepth = vDepth;
    }

    /* ==================================================================
     * STAGE 5: Texture fetch (Phase 4 + Phase 6.1 dual-TMU)
     *
     * When FBZCP_TEXTURE_ENABLED (bit 27 of fbzColorPath) is set,
     * sample TMU(s) using perspective-corrected texture coordinates.
     * vTexCoord0/1 = (S/W, T/W, 1/W) from vertex shader (noperspective).
     * Perspective divide: U = (S/W) / (1/W), V = (T/W) / (1/W).
     *
     * Voodoo 2 TMU pipeline: TMU1 (upstream) -> TMU0 (downstream).
     * TMU1's output becomes TMU0's c_other input.
     * ================================================================== */
    vec4 texel = vec4(1.0);
    bool textured = (pc.fbzColorPath & (1u << 27)) != 0u;
    bool tmu0_enabled = (pc.textureMode0 & 1u) != 0u;
    bool tmu1_enabled = (pc.textureMode1 & 1u) != 0u;

    /* TMU1 fetch (upstream, or single-TMU-via-TMU1). */
    vec4 texel1 = vec4(0.0);
    if (textured && tmu1_enabled) {
        /* TMU1 texture is at binding 1, coords in vTexCoord1. */
        float oow1 = vTexCoord1.z;
        if (oow1 > 0.0) {
            vec2 uv1 = vTexCoord1.xy / oow1;
            vec2 tex_size1 = vec2(textureSize(tex_tmu1, 0));
            uv1 /= tex_size1;
            texel1 = texture(tex_tmu1, uv1);
        } else {
            texel1 = texture(tex_tmu1, vec2(0.0));
        }
    }

    /* TMU0 fetch (downstream). */
    if (textured && tmu0_enabled) {
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
    } else if (textured && tmu1_enabled) {
        /* Single-TMU via TMU1: use TMU1's texel as the primary texel.
           This texel will go through TMU0 combine (or color combine directly). */
        texel = texel1;
    }

    /* ==================================================================
     * STAGE 5a: TMU1 texture combine (Phase 6.1 -- dual-TMU)
     *
     * TMU1 is the upstream texture unit.  Its c_other is always 0
     * (no further upstream TMU).  Its c_local is its own texel (texel1).
     * The SW renderer only processes TMU1 combine when tc_sub_clocal_1
     * or tca_sub_clocal_1 is set; otherwise TMU1 outputs raw texel.
     *
     * textureMode1 bits use the same layout as textureMode0:
     *   [13]    tc_sub_clocal
     *   [14:16] tc_mselect
     *   [17]    tc_reverse_blend
     *   [18]    tc_add_clocal
     *   [19]    tc_add_alocal
     *   [22]    tca_sub_clocal
     *   [23:25] tca_mselect
     *   [26]    tca_reverse_blend
     *   [27]    tca_add_clocal
     *   [28]    tca_add_alocal
     * ================================================================== */
    if (textured && tmu1_enabled && tmu0_enabled) {
        vec3 tc1_local = texel1.rgb;
        float tca1_local = texel1.a;

        /* ---- TMU1 Color combine ---- */
        /* TMU1 c_other = 0 always (no upstream). */
        if ((pc.textureMode1 & (1u << 13)) != 0u) {
            /* tc_sub_clocal_1: src = 0 - c_local = -c_local */
            vec3 tc1_src = -tc1_local;

            /* mselect factor */
            uint tc1_ms = (pc.textureMode1 >> 14) & 7u;
            vec3 tc1_factor;
            switch (tc1_ms) {
                case 0u:  tc1_factor = vec3(0.0);               break; /* ZERO */
                case 1u:  tc1_factor = tc1_local;               break; /* CLOCAL */
                case 2u:  tc1_factor = vec3(0.0);               break; /* AOTHER (0, no upstream) */
                case 3u:  tc1_factor = vec3(tca1_local);        break; /* ALOCAL */
                case 4u:  tc1_factor = vec3(0.0);               break; /* DETAIL (TODO) */
                case 5u:  tc1_factor = vec3(0.0);               break; /* LOD_FRAC (TODO) */
                default:  tc1_factor = vec3(0.0);               break;
            }

            /* Reverse blend */
            bool tc1_reverse = (pc.textureMode1 & (1u << 17)) != 0u;

            /* Trilinear: flip reverse on odd LOD levels. */
            if ((pc.textureMode1 & (1u << 30)) != 0u) {
                /* Approximate: just use the trilinear flip. */
                /* TODO: proper LOD computation for TMU1. */
            }

            if (!tc1_reverse)
                tc1_factor = vec3(1.0) - tc1_factor;

            tc1_src *= tc1_factor;

            /* Add */
            if ((pc.textureMode1 & (1u << 18)) != 0u)
                tc1_src += tc1_local;
            else if ((pc.textureMode1 & (1u << 19)) != 0u)
                tc1_src += vec3(tca1_local);

            texel1.rgb = clamp(tc1_src, 0.0, 1.0);
        }

        /* ---- TMU1 Alpha combine ---- */
        if ((pc.textureMode1 & (1u << 22)) != 0u) {
            /* tca_sub_clocal_1: src = 0 - a_local = -a_local */
            float tca1_src = -tca1_local;

            /* mselect factor */
            uint tca1_ms = (pc.textureMode1 >> 23) & 7u;
            float tca1_factor;
            switch (tca1_ms) {
                case 0u:  tca1_factor = 0.0;               break; /* ZERO */
                case 1u:  tca1_factor = tca1_local;         break; /* CLOCAL */
                case 2u:  tca1_factor = 0.0;                break; /* AOTHER (0) */
                case 3u:  tca1_factor = tca1_local;         break; /* ALOCAL */
                case 4u:  tca1_factor = 0.0;                break; /* DETAIL (TODO) */
                case 5u:  tca1_factor = 0.0;                break; /* LOD_FRAC (TODO) */
                default:  tca1_factor = 0.0;                break;
            }

            /* Reverse blend */
            bool tca1_reverse = (pc.textureMode1 & (1u << 26)) != 0u;
            if (!tca1_reverse)
                tca1_factor = 1.0 - tca1_factor;

            tca1_src *= tca1_factor;

            /* Add */
            if ((pc.textureMode1 & (1u << 27)) != 0u ||
                (pc.textureMode1 & (1u << 28)) != 0u)
                tca1_src += tca1_local;

            texel1.a = clamp(tca1_src, 0.0, 1.0);
        }
        /* Note: TMU1 has no invert_output bits in the SW renderer macros. */
    }

    /* ==================================================================
     * STAGE 5b: TMU0 texture combine (Phase 5.11 + Phase 6.1)
     *
     * textureMode0 bits 12-29 control a configurable blend formula
     * that processes the raw texel before color combine consumes it.
     *
     * c_other = TMU1 output (for dual-TMU) or 0 (for single-TMU).
     * c_local = TMU0 texel.
     *
     * Passthrough shortcut: for single-TMU, if all combine bits are zero
     * the texel passes through unchanged.  For dual-TMU, we must always
     * run the combine because the default path (all zeros, reverse=0)
     * produces c_other * 1.0 = TMU1 output, replacing the TMU0 texel.
     * ================================================================== */
    bool dual_tmu = tmu0_enabled && tmu1_enabled;
    if (textured && (dual_tmu || (pc.textureMode0 & 0x3FFFF000u) != 0u)) {
        /* c_other = TMU1 output when both TMUs active, else 0. */
        vec3 tc_other = dual_tmu ? texel1.rgb : vec3(0.0);
        float tca_other = dual_tmu ? texel1.a : 0.0;
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

    /* ==================================================================
     * STAGE 14: Save color-before-fog for ACOLORBEFOREFOG dual-source blend
     * ================================================================== */
    vec4 preFogColor = combined;

    /* ==================================================================
     * STAGE 15: Fog
     *
     * fogMode register bits:
     *   bit 0: FOG_ENABLE
     *   bit 1: FOG_ADD (additive fog -- fog_r/g/b start at 0 not fogColor)
     *   bit 2: FOG_MULT (multiplicative fog -- src = fog instead of src += fog)
     *   bit 3: FOG_ALPHA (use iterated alpha as fog factor)
     *   bit 4: FOG_Z (use Z as fog factor)
     *   bit 3+4: FOG_W (use W as fog factor -- both bits set)
     *   bit 5: FOG_CONSTANT (just add fogColor, skip table/factor)
     *
     * Fog algorithm (from SW APPLY_FOG macro):
     *   if (FOG_CONSTANT):
     *     src += fogColor
     *   else:
     *     fog_rgb = FOG_ADD ? 0 : fogColor
     *     if (!FOG_MULT) fog_rgb -= src
     *     fog_a = lookup factor from table/Z/alpha/W
     *     fog_a++ (add 1 to factor, making range [1..256])
     *     fog_rgb = (fog_rgb * fog_a) >> 8
     *     if (FOG_MULT) src = fog_rgb else src += fog_rgb
     *   clamp
     * ================================================================== */
    if ((pc.fogMode & 1u) != 0u) {
        vec3 fog_color = unpackRGB(pc.fogColor);

        if ((pc.fogMode & 0x20u) != 0u) {
            /* FOG_CONSTANT: simply add fogColor to combined color. */
            combined.rgb += fog_color;
        } else {
            /* Start with fog color or zero depending on FOG_ADD. */
            vec3 fog_rgb;
            if ((pc.fogMode & 0x02u) != 0u)
                fog_rgb = vec3(0.0); /* FOG_ADD */
            else
                fog_rgb = fog_color;

            /* If NOT FOG_MULT, subtract source color. */
            if ((pc.fogMode & 0x04u) == 0u)
                fog_rgb -= combined.rgb;

            /* Compute fog factor (fog_a) based on mode. */
            float fog_a = 0.0;
            uint fog_sel = pc.fogMode & 0x18u; /* bits 3-4 */

            if (fog_sel == 0x00u) {
                /* Table fog: lookup from fog table using w_depth. */
                int wd = compute_w_depth(vOOW);
                int fog_idx = (wd >> 10) & 0x3F;
                /* texelFetch the fog table (64x1 R8G8_UNORM).
                   .r = fog, .g = dfog. Both [0,1] representing [0,255]. */
                vec4 ft = texelFetch(fog_table, ivec2(fog_idx, 0), 0);
                float fog_base = ft.r * 255.0;
                float dfog = ft.g * 255.0;
                float frac = float((wd >> 2) & 0xFF);
                fog_a = fog_base + (dfog * frac) / 1024.0;
            } else if (fog_sel == 0x10u) {
                /* FOG_Z: use Z value bits [27:20] -> 8-bit fog factor. */
                /* vDepth is [0,1], Z register is 20.12 -> depth16 = z >> 12.
                   But fog uses (z >> 20) & 0xFF which is upper 8 bits of
                   the raw 32-bit Z.  Z = vDepth * 65535 * 4096 (20.12).
                   (z >> 20) = vDepth * 65535 * 4096 / (1<<20) = vDepth * 255.99 */
                fog_a = vDepth * 255.0;
            } else if (fog_sel == 0x08u) {
                /* FOG_ALPHA: use iterated alpha as fog factor. */
                fog_a = clamp(vColor.a, 0.0, 1.0) * 255.0;
            } else {
                /* FOG_W (0x18): bits 39:32 of W -> 8-bit.
                   W is state->w (18.32 fixed), bits 39:32 = (w >> 32) & 0xFF.
                   In float: oow * 2^32 -> bits 39:32 = floor(oow * 256) & 0xFF */
                float w_upper = abs(vOOW) * 4294967296.0; /* 2^32 */
                fog_a = clamp(floor(w_upper / 16777216.0), 0.0, 255.0); /* >> 24 nah, >> 32 then & 0xFF */
                /* Actually (w >> 32) & 0xFF = floor(w_fixed / 2^32) & 0xFF
                   w_fixed = oow * 2^32, so (w >> 32) = floor(oow) & 0xFF */
                fog_a = clamp(mod(floor(abs(vOOW)), 256.0), 0.0, 255.0);
            }

            /* fog_a++  (SW renderer adds 1 to make range [1..256]) */
            fog_a += 1.0;

            /* Apply: fog_rgb = (fog_rgb * fog_a) / 256 */
            fog_rgb *= fog_a / 256.0;

            /* Combine: if FOG_MULT, replace; else add. */
            if ((pc.fogMode & 0x04u) != 0u)
                combined.rgb = fog_rgb;
            else
                combined.rgb += fog_rgb;
        }

        /* Clamp to [0,1]. */
        combined.rgb = clamp(combined.rgb, 0.0, 1.0);
    }

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

    /* ==================================================================
     * STAGE 17: Dither (fbzMode bit 8)
     *
     * Voodoo dithers combined color to RGB565 precision using a 4x4 or
     * 2x2 Bayer ordered dither matrix.  This runs BEFORE alpha blend
     * (the "dithersub" path) or AFTER alpha blend (the "dither" path).
     * For now we implement the post-blend dither only, which quantizes
     * the output to 565 precision for correctness.
     *
     * fbzMode bit  8: dither enable
     * fbzMode bit 11: 2x2 mode (when set alongside bit 8)
     * ================================================================== */
    if ((pc.fbzMode & (1u << 8)) != 0u) {
        int x = int(gl_FragCoord.x);
        int y = int(gl_FragCoord.y);

        /* Convert to 8-bit [0..255] for dither. */
        int sr = int(combined.r * 255.0 + 0.5);
        int sg = int(combined.g * 255.0 + 0.5);
        int sb = int(combined.b * 255.0 + 0.5);

        if ((pc.fbzMode & (1u << 11)) != 0u) {
            /* 2x2 dither. */
            int d = dither2x2[y & 1][x & 1];
            /* Dither RB: 5-bit quantization with 4x4 pattern scaled.
               dither_rb[c][y][x] = (c + (d * 2)) >> 3 for 5 bits.
               dither_g[c][y][x]  = (c + d) >> 2 for 6 bits.
               2x2 variant uses same math with the 2x2 matrix. */
            sr = (sr + (d * 2)) >> 3;
            sg = (sg + d) >> 2;
            sb = (sb + (d * 2)) >> 3;
        } else {
            /* 4x4 dither. */
            int d = dither4x4[y & 3][x & 3];
            sr = (sr + (d * 2)) >> 3;
            sg = (sg + d) >> 2;
            sb = (sb + (d * 2)) >> 3;
        }

        /* Clamp to 5/6/5 bit range. */
        sr = clamp(sr, 0, 31);
        sg = clamp(sg, 0, 63);
        sb = clamp(sb, 0, 31);

        /* Convert back to [0,1] float. */
        combined.r = float(sr) / 31.0;
        combined.g = float(sg) / 63.0;
        combined.b = float(sb) / 31.0;
    } else {
        /* No dither: still quantize to 565 (truncation). */
        combined.r = float(int(combined.r * 255.0 + 0.5) >> 3) / 31.0;
        combined.g = float(int(combined.g * 255.0 + 0.5) >> 2) / 63.0;
        combined.b = float(int(combined.b * 255.0 + 0.5) >> 3) / 31.0;
    }

    /* --- Output --- */
    fragColor = combined;
    fragColorPreFog = preFogColor;
}
