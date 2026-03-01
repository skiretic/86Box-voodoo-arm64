# Perspective-Correct Texturing with Screen-Space Vertices

## Research Date: 2026-02-26

## Problem Statement

The Voodoo hardware submits **screen-space (pre-transformed) vertices** to its rasterizer.
The pixel pipeline then interpolates S/W, T/W, and 1/W linearly across the triangle and
performs a per-pixel division (`S = (S/W) / (1/W)`) to recover perspective-correct texture
coordinates.

If we naively submit these screen-space vertices to OpenGL with `gl_Position.w = 1.0`,
OpenGL's perspective-correct varying interpolation degenerates to **linear (affine)
interpolation** -- because when all W values are equal, the perspective correction formula
has no effect. This produces incorrect "warped" textures typical of PS1-era affine texturing.

We need a technique to get OpenGL to reproduce the Voodoo's perspective-correct S/T
interpolation while still using pre-transformed (screen-space) vertex positions.

---

## 1. OpenGL Perspective Interpolation Mechanics

### 1.1 The GL Specification Formula

**Reference**: OpenGL 4.6 Core Profile Specification, Section 14.6.1 "Interpolation"
(page 427); equivalent in OpenGL 4.1 Core Profile, Section 13.6.1.

OpenGL defines perspective-correct interpolation of a `smooth` varying `f` as:

```
            a * (f_a / w_a) + b * (f_b / w_b) + c * (f_c / w_c)
f_interp = -------------------------------------------------------
              a * (1 / w_a)  + b * (1 / w_b)  + c * (1 / w_c)
```

Where:
- `a`, `b`, `c` are the **barycentric coordinates** (computed in 2D screen/window space)
- `f_a`, `f_b`, `f_c` are the varying values at each vertex
- `w_a`, `w_b`, `w_c` are the **clip-space W** values at each vertex (i.e., `gl_Position.w`)

This formula is called "hyperbolic interpolation" -- it divides each attribute by W before
linear interpolation, then divides the result by the similarly-interpolated 1/W, effectively
performing perspective correction.

**Source**: [OpenGL 4.6 Specification PDF (LSU mirror)](https://www.ece.lsu.edu/koppel/gp/refs/glspec46.compatibility.pdf),
[GLSL Rasterization Wikibook](https://en.wikibooks.org/wiki/GLSL_Programming/Rasterization),
[Perspective-Correct Interpolation Technical Report (NUS)](https://www.comp.nus.edu.sg/~lowkl/publications/lowk_persp_interp_techrep.pdf)

### 1.2 What Happens When W = 1.0 for All Vertices

When `w_a = w_b = w_c = 1.0`, the formula simplifies to:

```
f_interp = (a * f_a + b * f_b + c * f_c) / (a + b + c)
         = a * f_a + b * f_b + c * f_c
```

This is simple **linear (affine) interpolation**. The perspective correction disappears
entirely. Textures will appear warped/skewed -- the classic "PS1 wobble" artifact.

This is the core problem: if we set `gl_Position = vec4(ndc_x, ndc_y, z, 1.0)`, we get
affine interpolation of all varyings.

### 1.3 The `noperspective` Qualifier

GLSL provides the `noperspective` interpolation qualifier:

```glsl
noperspective out vec2 v_texcoord;
```

This forces **linear screen-space interpolation** regardless of the W values, bypassing the
perspective formula entirely. This is the **opposite** of what we want -- it guarantees
affine interpolation. We do NOT want `noperspective`.

The default `smooth` qualifier (or no qualifier) gives perspective-correct interpolation,
which is what we need -- but it only works if the W values are meaningful.

**Source**: [OpenGL Interpolation Qualifiers (Geeks3D)](https://www.geeks3d.com/20130514/opengl-interpolation-qualifiers-glsl-tutorial/),
[GLSL Type Qualifiers (Khronos Wiki)](https://wikis.khronos.org/opengl/Type_Qualifier_(GLSL))

---

## 2. The Standard Technique: Encoding W into gl_Position

### 2.1 The Core Insight

The key insight is that OpenGL's perspective interpolation formula uses `gl_Position.w` as
the denominator. If we have a per-vertex W value (from the Voodoo's 1/W parameter), we can
**encode it into gl_Position.w** to make OpenGL's hardware interpolator reproduce the exact
same perspective correction the Voodoo does in software.

The technique:

```
gl_Position = vec4(screen_x * w, screen_y * w, z * w, w)
```

After OpenGL's perspective divide (`gl_Position.xyz / gl_Position.w`), this produces:
- NDC x = `screen_x * w / w = screen_x`  (correct screen position)
- NDC y = `screen_y * w / w = screen_y`  (correct screen position)
- NDC z = `z * w / w = z`                 (correct depth)

But the **W value is preserved** in the rasterizer for varying interpolation. OpenGL will
then automatically apply the perspective formula using our W values.

### 2.2 The Mathematics

Given three Voodoo vertices with positions `(x_i, y_i, z_i)` and W values `w_i`:

**Vertex shader output**:
```
gl_Position = vec4(x_ndc * w, y_ndc * w, z_ndc * w, w)
```

**After perspective divide** (done automatically by GL):
```
gl_Position_ndc = (x_ndc, y_ndc, z_ndc)  -- correct screen position
```

**During rasterization**, for any varying `v` with per-vertex values `v_0, v_1, v_2`:
```
            a*(v_0/w_0) + b*(v_1/w_1) + c*(v_2/w_2)
v_interp = -------------------------------------------
              a*(1/w_0)  + b*(1/w_1)  + c*(1/w_2)
```

This is **exactly** the Voodoo's perspective-correct interpolation! The hardware does the
S/W, T/W linear interpolation and 1/W division for us.

### 2.3 What We Pass as Varyings

For the Voodoo's S/W and T/W texture coordinates, we have two options:

**Option A: Pass raw S/W and T/W, divide in fragment shader**
```glsl
// Vertex shader
out vec3 v_stw0;  // S/W, T/W, W (for TMU 0)
v_stw0 = vec3(sS0, sT0, sW0);

// Fragment shader
vec2 texcoord = v_stw0.xy / v_stw0.z;  // Manual perspective divide
```

**Option B: Pass raw S and T, let GL interpolate them with perspective correction**

Since GL already applies perspective correction using `gl_Position.w`, if we pass the
**raw** (non-pre-divided) S and T values, GL's interpolation formula gives us:

```
S_interp = (a*(S_a/w_a) + b*(S_b/w_b) + c*(S_c/w_c)) / (a/w_a + b/w_b + c/w_c)
```

But this is NOT what the Voodoo does. The Voodoo interpolates S/W linearly and divides by
the linearly-interpolated 1/W. These are the same operation (perspective-correct interpolation
of S given W), so the result is equivalent.

**However**, there is a critical subtlety: the Voodoo's S and T values (`sS0`, `sT0`) are
**already pre-multiplied by W** (they are S/W, not S). They are what Glide writes as S/W
into the vertex registers. The Voodoo hardware iterates S/W linearly, not S. So:

- `sS0` = S/W  (texture S pre-divided by W)
- `sT0` = T/W  (texture T pre-divided by W)
- `sW0` = 1/W  (inverse of W for perspective correction)

Given this, the correct approach is **Option A**: pass S/W and T/W as varyings (they will be
linearly interpolated by GL due to the perspective correction canceling out -- see below), and
divide by the interpolated W in the fragment shader.

**Wait -- this needs more careful analysis.** See Section 3.

---

## 3. Detailed Analysis for the Voodoo Case

### 3.1 What the Voodoo Registers Contain

From the 86Box source code (`src/include/86box/vid_voodoo_common.h`):

```c
typedef struct vert_t {
    float sVx;     // Screen X position
    float sVy;     // Screen Y position
    float sRed;    // Vertex color R
    float sGreen;  // Vertex color G
    float sBlue;   // Vertex color B
    float sAlpha;  // Vertex color A
    float sVz;     // Depth (Z buffer value)
    float sWb;     // Base 1/W (for depth/fog)
    float sW0;     // TMU0 1/W
    float sS0;     // TMU0 S/W
    float sT0;     // TMU0 T/W
    float sW1;     // TMU1 1/W
    float sS1;     // TMU1 S/W
    float sT1;     // TMU1 T/W
} vert_t;
```

**Source**: 3dfx SST-1 Programmer's Guide
([PDF](http://www.o3one.org/hwdocs/video/voodoo_graphics.pdf)):

> "The S and T coordinates used by SST-1 for rendering must be divided by W prior to being
> sent to SST-1 (i.e., SST-1 iterates S/W and T/W prior to perspective correction)."

> "During each iteration of span/trapezoid walking, a division is performed by 1/W to
> correct for perspective distortion."

So:
- **`sS0` and `sT0` are S/W and T/W** (pre-divided by W)
- **`sW0` is 1/W** (the reciprocal, used for perspective division)
- **`sWb` is the base 1/W** (used as a fallback when per-TMU W is not specified)

### 3.2 What the Software Rasterizer Does

From `src/video/vid_voodoo_render.c`, lines 386-413:

```c
// Perspective-correct texture fetch (textureMode bit 0 = perspective enable)
if (params->textureMode[tmu] & 1) {
    int64_t _w = 0;
    if (state->tmu0_w)
        _w = (int64_t) ((1ULL << 48) / state->tmu0_w);
    // _w is now 1/(1/W) = W (with fixed-point scaling)
    state->tex_s = (((state->tmu0_s >> 14) * _w) + ...) >> 30;
    state->tex_t = (((state->tmu0_t >> 14) * _w) + ...) >> 30;
    // Final tex_s = (S/W) * W = S
    // Final tex_t = (T/W) * W = T
} else {
    // No perspective correction: use S/W and T/W directly as coordinates
    state->tex_s = state->tmu0_s >> 28;
    state->tex_t = state->tmu0_t >> 28;
}
```

The rasterizer:
1. Linearly interpolates `S/W`, `T/W`, and `1/W` across the triangle (using per-pixel
   `dSdX`, `dTdX`, `dWdX` gradients)
2. When `textureMode & 1` (perspective enable): computes `W = 1 / (1/W)` and multiplies
   to get `S = (S/W) * W`, `T = (T/W) * W`
3. When perspective is disabled: uses `S/W` and `T/W` directly as texture coordinates

### 3.3 Mapping to OpenGL

The Voodoo iterates three quantities linearly in screen space:
- `S/W` (with gradients `dSdX`, `dSdY`)
- `T/W` (with gradients `dTdX`, `dTdY`)
- `1/W` (with gradients `dWdX`, `dWdY`)

Then per-pixel: `S = (S/W) / (1/W)`, `T = (T/W) / (1/W)`

OpenGL's perspective formula for a varying `v` is:
```
v_interp = SUM(barycentric_i * v_i / w_i) / SUM(barycentric_i / w_i)
```

If we set `gl_Position.w = W_vertex` (where `W_vertex` is the per-vertex `1 / (1/W)` = `W`),
and pass raw `S` as a varying (where `S = sS0 / sW0 = (S/W) / (1/W)`), then GL will compute:

```
S_interp = SUM(b_i * S_i / W_i) / SUM(b_i / W_i)
         = SUM(b_i * (S/W)_i) / SUM(b_i * (1/W)_i)
```

This is the perspective-correct interpolation of S, which is exactly what the Voodoo computes!

**But there's a problem**: we don't have the raw S values directly. We have `S/W` values. To
get `S`, we would need to divide: `S = sS0 / sW0`. This introduces a per-vertex division.

**Alternative approach**: Pass `S/W` as the varying but use `noperspective` interpolation,
then divide by interpolated `1/W` in the fragment shader.

Let's analyze both approaches:

### Approach A: Reconstruct S, use perspective interpolation via gl_Position.w

```glsl
// Vertex shader
uniform vec2 u_viewport_size;

// Per-vertex input
layout(location = 0) in vec2 a_position;    // sVx, sVy (screen space)
layout(location = 1) in float a_depth;      // sVz
layout(location = 2) in vec4 a_color;       // RGBA
layout(location = 3) in float a_w;          // sW0 (= 1/W)
layout(location = 4) in vec2 a_st;          // sS0, sT0 (= S/W, T/W)

out vec2 v_texcoord;   // smooth (perspective-correct)
out vec4 v_color;      // smooth (perspective-correct)

void main() {
    // Compute actual W from 1/W
    float W = 1.0 / a_w;  // WARNING: division by zero if a_w == 0

    // Recover true S, T from S/W, T/W
    float S = a_st.x / a_w;  // = (S/W) / (1/W) = S * W
    float T = a_st.y / a_w;  // = (T/W) / (1/W) = T * W
    // WAIT: a_st.x = S/W, and a_w = 1/W, so:
    // S = (S/W) * (1 / (1/W)) = (S/W) * W
    // But a_st.x / a_w = (S/W) / (1/W) = (S/W) * W = S .... yes, same thing.

    v_texcoord = vec2(S, T);
    v_color = a_color;

    // Convert screen coords to NDC
    vec2 ndc = (a_position / u_viewport_size) * 2.0 - 1.0;
    float z_ndc = a_depth;  // map to [-1, 1] as needed

    // Encode W into gl_Position for perspective-correct interpolation
    gl_Position = vec4(ndc * W, z_ndc * W, W);
}
```

```glsl
// Fragment shader
in vec2 v_texcoord;  // Perspective-correct S, T
uniform sampler2D u_texture;

void main() {
    vec2 tc = v_texcoord;  // Already perspective-correct!
    // ... texture lookup, combine with color, etc.
}
```

**Pros**: Fragment shader is simple -- just use `v_texcoord` directly.
**Cons**: Requires per-vertex division `1.0 / a_w` in vertex shader (cheap -- only 3 vertices
per triangle). Risk of division by zero if `sW0 == 0`.

### Approach B: Pass S/W and T/W with noperspective, divide by 1/W in fragment shader

```glsl
// Vertex shader
noperspective out vec2 v_st_over_w;  // S/W, T/W (linearly interpolated)
noperspective out float v_one_over_w; // 1/W (linearly interpolated)

void main() {
    v_st_over_w = a_st;     // Pass S/W, T/W directly
    v_one_over_w = a_w;     // Pass 1/W directly

    vec2 ndc = (a_position / u_viewport_size) * 2.0 - 1.0;
    gl_Position = vec4(ndc, a_depth, 1.0);  // W = 1.0 (no perspective)
}
```

```glsl
// Fragment shader
noperspective in vec2 v_st_over_w;
noperspective in float v_one_over_w;

void main() {
    // Perspective divide in fragment shader
    vec2 tc = v_st_over_w / v_one_over_w;  // Per-pixel division
    // ... texture lookup
}
```

**Pros**: No per-vertex math. Exactly mirrors Voodoo's interpolation model. Trivially handles
`textureMode & 1 == 0` (no perspective) by skipping the division.
**Cons**: Per-pixel division in fragment shader (slightly more expensive). All varyings must
use `noperspective`. Colors are also affected -- Voodoo linearly interpolates colors.

### Approach C (Recommended): Hybrid -- use gl_Position.w for texture, noperspective for color

The Voodoo **linearly** interpolates vertex colors (R, G, B, A) in screen space.
But it **perspective-corrects** texture coordinates (when textureMode bit 0 is set).

These are different interpolation modes! OpenGL applies a single interpolation formula per
varying based on the qualifier, so we need:

- `smooth` (perspective-correct) for texture coordinates, driven by `gl_Position.w`
- `noperspective` (linear) for vertex colors

But there's a conflict: if `gl_Position.w != 1`, then `smooth` varyings get perspective
correction and `noperspective` varyings stay linear. This is exactly what we want!

```glsl
// Vertex shader
layout(location = 0) in vec2 a_position;   // Screen X, Y
layout(location = 1) in float a_depth;     // Z buffer value
layout(location = 2) in vec4 a_color;      // RGBA (0-255 range)
layout(location = 3) in float a_wb;        // sWb (base 1/W)
layout(location = 4) in float a_w0;        // sW0 (TMU0 1/W)
layout(location = 5) in vec2 a_st0;        // sS0, sT0 (TMU0 S/W, T/W)
layout(location = 6) in float a_w1;        // sW1 (TMU1 1/W)
layout(location = 7) in vec2 a_st1;        // sS1, sT1 (TMU1 S/W, T/W)

uniform vec2 u_viewport_size;

smooth out vec2 v_texcoord0;        // Perspective-correct S, T for TMU0
smooth out vec2 v_texcoord1;        // Perspective-correct S, T for TMU1
noperspective out vec4 v_color;     // Linearly-interpolated vertex color
noperspective out float v_fog_w;    // 1/W for fog computation (linear)
flat out float v_depth_raw;         // For depth buffer writes (if needed)

void main() {
    // Compute W from 1/W (for TMU0 -- used as the perspective reference)
    float W = (a_w0 != 0.0) ? (1.0 / a_w0) : 1.0;

    // Recover true S, T from S/W, T/W
    vec2 st0 = a_st0 * W;  // (S/W * W, T/W * W) = (S, T)
    vec2 st1 = a_st1 * W;  // Same for TMU1

    v_texcoord0 = st0;
    v_texcoord1 = st1;
    v_color = a_color / 255.0;
    v_fog_w = a_wb;

    // Convert screen coords to NDC [-1, 1]
    vec2 ndc = (a_position / u_viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y: Voodoo Y=0 at top, GL Y=0 at bottom

    // Depth: Voodoo Z is [0, 0xFFFF] or [0, 0xFFFFF] depending on depth bits
    // Map to NDC [-1, 1] for GL
    float z_ndc = a_depth * 2.0 - 1.0;  // Assumes a_depth is normalized [0, 1]

    // CRITICAL: multiply all components by W for perspective-correct interpolation
    gl_Position = vec4(ndc * W, z_ndc * W, W);
}
```

```glsl
// Fragment shader
smooth in vec2 v_texcoord0;         // Already perspective-correct!
smooth in vec2 v_texcoord1;
noperspective in vec4 v_color;      // Linearly interpolated (correct for Voodoo)
noperspective in float v_fog_w;

uniform sampler2D u_tex0;
uniform sampler2D u_tex1;

void main() {
    // v_texcoord0 is already the true (S, T) -- GL did the perspective correction
    vec4 texel0 = texture(u_tex0, v_texcoord0);
    vec4 texel1 = texture(u_tex1, v_texcoord1);

    // ... combine texel with vertex color per fbzColorPath ...
}
```

---

## 4. How Other Emulators Handle This

### 4.1 DOSBox / DOSBox-X (Voodoo OpenGL)

**Source**: [VOGONS Forums: 3dfx voodoo chip emulation](http://www.vogons.org/viewtopic.php?t=25606&start=240),
[VOGONS: Enable OpenGL for Fast 3DFX Voodoo Emulation](https://www.vogons.org/viewtopic.php?t=39231)

DOSBox's Voodoo OpenGL path uses the **legacy fixed-function pipeline** (not shaders) with
`glOrtho` for the projection matrix (since vertices are already screen-space). For perspective-
correct texturing, it uses:

```c
// Legacy OpenGL approach (DOSBox voodoo_vogl.cpp)
glTexCoord4f(S_over_W * Z, T_over_W * Z, 0.0f, 1.0f / Z);
// or equivalently:
glMultiTexCoord4fARB(GL_TEXTURE0, s_w * z, t_w * z, 0.0, oow);
```

Combined with `texture2DProj` in the fragment stage (or GL's built-in projective texture
lookup), this achieves perspective-correct texturing by passing the 4th texture coordinate (Q)
as the divisor.

**Key insight**: DOSBox uses `glOrtho` (not `glFrustum`) because the vertices are already in
screen space. The perspective correction is done entirely through the texture Q coordinate.

**Limitation**: This approach is limited to the legacy pipeline. In core profile GL 4.1, we
don't have `glTexCoord4f` or `texture2DProj` (well, `textureProj` exists but requires a
`sampler2D` with vec3/vec4 coords). Our approach through `gl_Position.w` is the modern
equivalent.

### 4.2 PCSX2 (PlayStation 2)

**Source**: [GSdx Shader Discussion (#788)](https://github.com/pcsx2/pcsx2/issues/788),
[Maister's PS2 GS Emulation Blog](https://themaister.net/blog/2024/07/03/playstation-2-gs-emulation-the-final-frontier-of-vulkan-compute-emulation/)

The PS2's GS (Graphics Synthesizer) uses STQ coordinates:
- `Q = 1/W`
- `S = s * Q` (= S/W)
- `T = t * Q` (= T/W)

This is the **exact same** convention as the Voodoo. PCSX2's hardware renderer uses:

```glsl
// PCSX2 vertex shader approach (from issue #788 discussion):
float w = 1.0 / q;          // Recover W from Q (= 1/W)
out.texcoord = st * w;       // Recover S, T from S/W, T/W
gl_Position *= w;            // Encode W into clip position
```

This is **identical** to our Approach C. PCSX2 multiplies `gl_Position` by W (= 1/Q) and
passes the recovered S, T values as `smooth` varyings.

For flat/2D primitives (sprites), PCSX2 checks if all Q values are identical and, if so,
flattens the W to avoid unnecessary perspective correction overhead.

### 4.3 Dolphin (GameCube/Wii)

**Source**: [Dolphin VertexShaderGen.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexShaderGen.cpp),
[Dolphin VertexShaderManager.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexShaderManager.cpp)

Dolphin's situation is different from the Voodoo. The GameCube/Wii GPU (Flipper/Hollywood)
has a full transform & lighting pipeline, so vertices are NOT pre-transformed. Dolphin's
vertex shader performs the full model-view-projection transform:

```glsl
o.pos = rawpos * position_matrix;
```

The output `gl_Position` naturally has a meaningful W component from the projection matrix.
Dolphin does NOT need the `gl_Position *= w` trick because it has a real projection.

**However**, Dolphin does deal with screen-space pixel center offsets (the console GPU uses
pixel centers at 7/12 = 0.58333 instead of 0.5) which is relevant for our subpixel precision
concerns.

### 4.4 MAME (Voodoo Software Renderer)

**Source**: [MAME voodoo.cpp](https://github.com/mamedev/mame/blob/master/src/devices/video/voodoo.cpp)

MAME's Voodoo emulation is **entirely software-based**. It performs the same linear
interpolation of S/W, T/W, 1/W and per-pixel division as 86Box's software rasterizer.
MAME does not have a GPU-accelerated Voodoo renderer to reference.

---

## 5. Important Edge Cases

### 5.1 When sW0 = 0 (No Perspective)

If `sW0 == 0.0`, then `W = 1/0 = infinity`. The software rasterizer handles this:
```c
int64_t _w = 0;
if (state->tmu0_w)  // Only divide if W != 0
    _w = (int64_t) ((1ULL << 48) / state->tmu0_w);
```
When `1/W == 0`, `_w` stays 0, making `tex_s = 0` and `tex_t = 0`. In our GL path, we should
guard against this:
```glsl
float W = (a_w0 != 0.0) ? (1.0 / a_w0) : 1.0;
```
Setting `W = 1.0` when `1/W == 0` means affine interpolation, which is reasonable.

### 5.2 textureMode Bit 0: Perspective Enable/Disable

The Voodoo's `textureMode` register bit 0 controls whether perspective correction is applied:
- **Bit 0 = 1**: Perspective-correct (divide S/W by 1/W)
- **Bit 0 = 0**: Affine (use S/W directly as texture coordinate)

From `vid_voodoo_render.c` line 388:
```c
if (params->textureMode[tmu] & 1) {
    // perspective-correct: tex_s = (S/W) / (1/W) = S
} else {
    // affine: tex_s = S/W (used directly)
}
```

When perspective is disabled, we should set `gl_Position.w = 1.0` (or use `noperspective`
for texcoords too) and pass S/W, T/W directly as texture coordinates.

In our uber-shader approach, this means the vertex extraction code needs to know the
`textureMode` state to decide whether to encode W or not. Or we can use a simpler approach:
always encode W, and in the fragment shader, choose whether to use the perspective-corrected
or linearly-interpolated coordinates based on a uniform flag.

**Recommended**: Always encode W into `gl_Position` (Approach C). For the affine case, pass
the S/W and T/W as **additional** `noperspective` varyings and select between them in the
fragment shader based on the `textureMode` uniform.

### 5.3 Separate W per TMU

The Voodoo supports separate W values for TMU0 and TMU1 (`sW0` vs `sW1`). Since
`gl_Position.w` is a single value, we can only use one W for perspective correction.

The solution: Use the **base W** (`sWb`) or TMU0 W (`sW0`) for `gl_Position.w`, and for
TMU1 with a different W, either:

1. Pass TMU1 S/T/W as `noperspective` varyings and divide in the fragment shader, OR
2. Pre-correct TMU1 texcoords in the vertex shader using `sW1 / sW0` ratio

In practice, most games either:
- Use `sWb` as the base W (which gets copied to both TMUs via `SETUPMODE_Wb`)
- Or set `sW0 == sW1` (both TMUs share the same perspective)

From the setup code (`vid_voodoo_setup.c` lines 187-201):
```c
if (voodoo->sSetupMode & SETUPMODE_Wb) {
    // sWb sets W for both TMUs
    voodoo->params.tmu[0].startW = voodoo->params.tmu[1].startW = voodoo->params.startW;
}
if (voodoo->sSetupMode & SETUPMODE_W0) {
    // sW0 overrides TMU0 W (and TMU1 inherits TMU0's W)
    voodoo->params.tmu[1].startW = voodoo->params.tmu[0].startW;
}
if (voodoo->sSetupMode & SETUPMODE_W1) {
    // sW1 overrides TMU1 W independently
    // (TMU1 can have different W than TMU0)
}
```

**Recommended strategy**:
- Use `sW0` (TMU0 W) for `gl_Position.w`
- If `sW1 != sW0`, pass TMU1 S/T as `noperspective` and divide by interpolated `sW1` in
  the fragment shader
- In practice, this rarely happens (most games use shared W)

### 5.4 Color Interpolation

Voodoo interpolates colors (R, G, B, A) **linearly** in screen space -- there is no
perspective correction for colors. This is correct behavior: Gouraud shading on the Voodoo
is always affine.

In our shader, colors MUST use the `noperspective` qualifier:
```glsl
noperspective out vec4 v_color;
```

If we accidentally use `smooth` (the default) for colors, they will be perspective-corrected,
which is incorrect and will produce subtle color banding/shifting artifacts on 3D geometry.

### 5.5 Depth (Z) Interpolation

The Voodoo interpolates Z linearly in screen space (not perspective-corrected). Z is used for
depth buffer comparison. Since `gl_Position.z` (after perspective divide) is used for
`gl_FragDepth` by default, and we've encoded W into `gl_Position`, the automatic depth
interpolation will be perspective-correct (which is wrong for Voodoo).

**Solution**: Either:
1. Write `gl_FragDepth` manually from a `noperspective` varying, OR
2. Use the GL depth buffer only for coarse Z rejection and handle precise Voodoo Z in the
   fragment shader via a `noperspective float v_depth` varying.

Option 2 is simpler and matches the Voodoo's behavior. Use:
```glsl
// Vertex shader
noperspective out float v_depth;
v_depth = a_depth;  // Raw Voodoo Z, linearly interpolated

// Fragment shader
noperspective in float v_depth;
// Convert v_depth to depth buffer format and compare
gl_FragDepth = v_depth;  // Write corrected depth
```

---

## 6. Recommended Solution for VideoCommon

### 6.1 Vertex Layout

Per vertex, we submit:
```
struct voodoo_gl_vertex {
    float x, y;       // Screen position (sVx, sVy)
    float z;           // Depth (sVz, normalized)
    float w;           // 1/W (sW0 or sWb, used for perspective)
    float r, g, b, a;  // Vertex color (sRed, sGreen, sBlue, sAlpha)
    float s0, t0;      // TMU0 S/W, T/W (sS0, sT0)
    float w0;          // TMU0 1/W (sW0) -- for fragment shader divide
    float s1, t1;      // TMU1 S/W, T/W (sS1, sT1)
    float w1;          // TMU1 1/W (sW1)
};
```

### 6.2 Vertex Shader

```glsl
#version 410 core

// Per-vertex attributes
layout(location = 0) in vec2 a_position;    // Screen X, Y
layout(location = 1) in float a_depth;      // Normalized Z [0, 1]
layout(location = 2) in float a_oow;        // 1/W for gl_Position
layout(location = 3) in vec4 a_color;       // RGBA [0, 255]
layout(location = 4) in vec2 a_st0;         // TMU0 S/W, T/W
layout(location = 5) in float a_w0;         // TMU0 1/W
layout(location = 6) in vec2 a_st1;         // TMU1 S/W, T/W
layout(location = 7) in float a_w1;         // TMU1 1/W

// Uniforms
uniform vec2 u_viewport_size;               // Framebuffer dimensions
uniform bool u_persp_tmu0;                  // textureMode[0] & 1
uniform bool u_persp_tmu1;                  // textureMode[1] & 1

// Outputs to fragment shader
smooth out vec2 v_texcoord0;                // Perspective-corrected S, T for TMU0
smooth out vec2 v_texcoord1;                // Perspective-corrected S, T for TMU1
noperspective out vec2 v_st0_affine;        // S/W, T/W for TMU0 (affine fallback)
noperspective out vec2 v_st1_affine;        // S/W, T/W for TMU1 (affine fallback)
noperspective out vec4 v_color;             // Gouraud color (always linear)
noperspective out float v_depth;            // Voodoo Z (linear interpolation)
noperspective out float v_fog_oow;          // 1/W for fog computation

void main() {
    // Recover W from 1/W
    float W = (a_oow != 0.0) ? (1.0 / a_oow) : 1.0;

    // Perspective-corrected texture coords (recover true S, T)
    v_texcoord0 = a_st0 * W;   // (S/W) * W = S
    v_texcoord1 = a_st1 * W;   // (T/W) * W = T

    // Affine texture coords (pass S/W, T/W directly)
    v_st0_affine = a_st0;
    v_st1_affine = a_st1;

    // Vertex color (normalize from [0, 255] to [0, 1])
    v_color = a_color / 255.0;

    // Depth and fog
    v_depth = a_depth;
    v_fog_oow = a_oow;

    // Convert screen coords to NDC [-1, 1]
    vec2 ndc;
    ndc.x = (a_position.x / u_viewport_size.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (a_position.y / u_viewport_size.y) * 2.0;  // Y flip

    float z_ndc = a_depth * 2.0 - 1.0;

    // Encode W into gl_Position for perspective-correct varying interpolation
    gl_Position = vec4(ndc * W, z_ndc * W, W);
}
```

### 6.3 Fragment Shader (Texture Coordinate Selection)

```glsl
#version 410 core

smooth in vec2 v_texcoord0;            // Perspective-correct
smooth in vec2 v_texcoord1;
noperspective in vec2 v_st0_affine;    // Affine
noperspective in vec2 v_st1_affine;
noperspective in vec4 v_color;
noperspective in float v_depth;
noperspective in float v_fog_oow;

uniform bool u_persp_tmu0;
uniform bool u_persp_tmu1;
uniform sampler2D u_tex0;
uniform sampler2D u_tex1;

out vec4 fragColor;

void main() {
    // Select perspective-correct or affine texture coords
    vec2 tc0 = u_persp_tmu0 ? v_texcoord0 : v_st0_affine;
    vec2 tc1 = u_persp_tmu1 ? v_texcoord1 : v_st1_affine;

    vec4 texel0 = texture(u_tex0, tc0);
    // ... rest of uber-shader pipeline ...

    gl_FragDepth = v_depth;  // Write linearly-interpolated depth
}
```

### 6.4 Optimization: Avoiding Dual Varying Sets

Passing both `smooth` and `noperspective` versions of texture coordinates costs extra
varyings. Since the vast majority of Voodoo games use perspective-correct texturing
(bit 0 = 1), we can optimize:

**Option**: Always use perspective-correct (`smooth`) texture coords. When perspective is
disabled (`textureMode & 1 == 0`), set `gl_Position.w = 1.0` for all vertices in the batch,
which makes `smooth` degenerate to linear. Since we batch triangles with the same state, this
is naturally handled.

This eliminates the need for the `noperspective` affine varyings and the uniform boolean
branch in the fragment shader. The vertex extraction code simply checks the textureMode
and either encodes W or sets W = 1.0:

```c
// In vid_voodoo_gl.c vertex extraction:
if (params->textureMode[0] & 1) {
    // Perspective mode: encode W
    vtx.w = verts[i].sW0;  // or sWb
} else {
    // Affine mode: W = 1.0 (makes smooth = linear)
    vtx.w = 0.0;  // 1/W = 0 => W = infinity... no.
    // Better: set a sentinel that the vertex shader maps to W=1.0
    // Or: just always pass sW0 and let the shader handle it.
}
```

Actually, a cleaner solution: the vertex shader always computes `W = 1.0 / oow`, and the
C code passes `oow = 0.0` when perspective is disabled. The vertex shader then:
```glsl
float W = (a_oow > 0.0) ? (1.0 / a_oow) : 1.0;
```
When `oow = 0`, `W = 1.0`, and `smooth` varyings become linear. The texture coordinates
would then be `a_st0 * 1.0 = a_st0 = S/W`, which is what the affine path wants.

But wait -- when `W = 1`, `gl_Position.w = 1`, so `smooth` varyings are linearly
interpolated, and `v_texcoord0 = a_st0 * 1 = S/W` which is used directly. This is correct
for the affine case!

**THIS IS THE SIMPLEST AND CLEANEST SOLUTION.**

---

## 7. Final Recommended Approach

### Summary

1. **Per-vertex, pass `1/W` (from `sW0` or `sWb`) as an attribute**
2. **In the vertex shader**:
   - Compute `W = 1.0 / (1/W)` (guarding against zero)
   - Multiply `gl_Position = vec4(ndc * W, z_ndc * W, W)`
   - Compute texture coords as `a_st * W` (recovers true S, T from S/W, T/W)
   - Pass colors as `noperspective` (Voodoo colors are always linearly interpolated)
   - Pass depth as `noperspective` (Voodoo Z is linearly interpolated)
3. **When perspective is disabled** (`textureMode & 1 == 0`):
   - Pass `1/W = 0` (or a very small value), causing `W = 1.0` in the shader
   - This makes `smooth` varyings degenerate to linear, which is affine texturing
   - Texture coords become `S/W * 1 = S/W`, used directly (correct for affine)
4. **Fragment shader**: Use `v_texcoord0` directly -- it is already correct
5. **For separate TMU1 W** (rare): Pass TMU1 S/T/W as additional `noperspective` varyings
   and divide in the fragment shader

### Why This Works

The mathematics:
- OpenGL's perspective formula: `f_interp = SUM(b_i * f_i / w_i) / SUM(b_i / w_i)`
- With `f_i = S_i = (S/W)_i * W_i` and `w_i = W_i`:
  - Numerator: `SUM(b_i * (S/W)_i * W_i / W_i) = SUM(b_i * (S/W)_i)`
  - Denominator: `SUM(b_i / W_i) = SUM(b_i * (1/W)_i)`
  - Result: `SUM(b_i * (S/W)_i) / SUM(b_i * (1/W)_i)`
- This is the Voodoo's formula: linearly interpolate S/W, divide by linearly interpolated 1/W

This technique is well-established and used by multiple emulators (PCSX2, DOSBox).

---

## Sources

### OpenGL Specification
- [OpenGL 4.6 Specification (Khronos/LSU)](https://www.ece.lsu.edu/koppel/gp/refs/glspec46.compatibility.pdf) -- Section 14.6.1, page 427
- [GLSL Type Qualifiers (Khronos Wiki)](https://wikis.khronos.org/opengl/Type_Qualifier_(GLSL))
- [OpenGL Interpolation Qualifiers (Geeks3D)](https://www.geeks3d.com/20130514/opengl-interpolation-qualifiers-glsl-tutorial/)
- [GLSL Rasterization and Interpolation (Wikibooks)](https://en.wikibooks.org/wiki/GLSL_Programming/Rasterization)
- [Perspective-Correct Interpolation (NUS Technical Report)](https://www.comp.nus.edu.sg/~lowkl/publications/lowk_persp_interp_techrep.pdf)
- [Perspective-Correct Texturing (WebGL Fundamentals)](https://webglfundamentals.org/webgl/lessons/webgl-3d-perspective-correct-texturemapping.html)

### Voodoo Hardware
- [3dfx SST-1 (Voodoo Graphics) Programmer's Guide](http://www.o3one.org/hwdocs/video/voodoo_graphics.pdf)
- [Glide 3.0 Programming Guide (Bitsavers)](https://www.bitsavers.org/components/3dfx/Glide_Programming_Guide_3.0_199806.pdf)
- 86Box source: `src/include/86box/vid_voodoo_common.h` (vert_t struct)
- 86Box source: `src/video/vid_voodoo_setup.c` (gradient setup)
- 86Box source: `src/video/vid_voodoo_render.c` (software rasterizer W division)
- 86Box source: `src/include/86box/vid_voodoo_regs.h` (textureMode flags)

### Other Emulators
- [PCSX2 GSdx Shader Discussion (#788)](https://github.com/pcsx2/pcsx2/issues/788)
- [PS2 GS Emulation -- Maister's Graphics Adventures](https://themaister.net/blog/2024/07/03/playstation-2-gs-emulation-the-final-frontier-of-vulkan-compute-emulation/)
- [Dolphin VertexShaderGen.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexShaderGen.cpp)
- [MAME voodoo.cpp](https://github.com/mamedev/mame/blob/master/src/devices/video/voodoo.cpp)
- [DOSBox-X Voodoo source](https://github.com/joncampbell123/dosbox-x/blob/master/src/hardware/voodoo.cpp)
- [VOGONS: 3dfx voodoo chip emulation (perspective discussion)](http://www.vogons.org/viewtopic.php?t=25606&start=240)
- [VOGONS: Enable OpenGL for Fast 3DFX Voodoo Emulation](https://www.vogons.org/viewtopic.php?t=39231)

### Techniques
- [Hacks of Life: Perspective-Correct Texturing, Q Coordinates, and GLSL](http://hacksoflife.blogspot.com/2009/11/perspective-correct-texturing-q.html)
- [Hacks of Life: Perspective Correct Texturing in OpenGL](http://hacksoflife.blogspot.com/2008/08/perspective-correct-texturing-in-opengl.html)
- [Khronos Forums: Perspective Correct Texturing with Ortho Projection](https://community.khronos.org/t/perspective-correct-texturing-with-an-ortho-projection/46652)
- [Interpolation Redux (OpenGL Tutorial)](https://paroj.github.io/gltut/Texturing/Tut14%20Interpolation%20Redux.html)
