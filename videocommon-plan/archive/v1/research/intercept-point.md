# Intercept Point: Where to Hook into the Voodoo Pipeline for Vulkan Rendering

## Summary

The Voodoo hardware has **two distinct triangle submission modes**. Both paths
converge at `voodoo_queue_triangle()`, which operates on `voodoo_params_t` --
a struct that contains gradients (start + dVdX + dVdY), NOT per-vertex values.
However, **per-vertex data IS available** in `vert_t` before the setup engine
runs, but ONLY for one of the two modes.

**Recommendation**: Intercept at **two separate points** (one per submission
mode), extract per-vertex data directly from `vert_t` where available, and
reconstruct per-vertex data from gradients where necessary.

---

## The Two Triangle Submission Modes

### Mode 1: Direct Gradient Writes (Voodoo 1 legacy, Glide 2.x)

**Used by**: Glide 2.x, Voodoo 1 games, some V2 games, and direct register
programming.

The CPU (via Glide) pre-computes gradients on the host CPU and writes them
directly to Voodoo registers:

```
CPU writes:
  SST_vertexAx/Ay, SST_vertexBx/By, SST_vertexCx/Cy  (integer 12.4 fixed-point)
  OR: SST_fvertexAx/Ay, ..., SST_fvertexCx/Cy         (IEEE 754 float)
  SST_startR, SST_dRdX, SST_dRdY                       (12.12 fixed-point)
  OR: SST_fstartR, SST_fdRdX, SST_fdRdY                (float -> 12.12)
  ... (same pattern for G, B, A, Z, S, T, W) ...
  SST_triangleCMD  (or SST_ftriangleCMD)               <- fires the triangle
```

**Key**: The CPU has ALREADY done the setup math. `voodoo_params_t` receives
pre-computed gradients directly. There are NO per-vertex color/texcoord values
in `vert_t` for this path. The `vert_t` array (`voodoo->verts[]`) is not
touched at all.

**Register definitions** (from `vid_voodoo_regs.h`):
```c
SST_vertexAx = 0x008,  SST_startR = 0x020,  SST_dRdX = 0x040,  SST_dRdY = 0x060
SST_vertexAy = 0x00c,  SST_startG = 0x024,  SST_dGdX = 0x044,  SST_dGdY = 0x064
SST_vertexBx = 0x010,  SST_startB = 0x028,  SST_dBdX = 0x048,  SST_dBdY = 0x068
SST_vertexBy = 0x014,  SST_startZ = 0x02c,  SST_dZdX = 0x04c,  SST_dZdY = 0x06c
SST_vertexCx = 0x018,  SST_startA = 0x030,  SST_dAdX = 0x050,  SST_dAdY = 0x070
SST_vertexCy = 0x01c,  SST_startS = 0x034,  SST_dSdX = 0x054,  SST_dSdY = 0x074
                        SST_startT = 0x038,  SST_dTdX = 0x058,  SST_dTdY = 0x078
                        SST_startW = 0x03c,  SST_dWdX = 0x05c,  SST_dWdY = 0x07c
SST_triangleCMD = 0x080
```

**Code path** (`vid_voodoo_reg.c` lines 170-337):
```c
case SST_vertexAx:
    voodoo->params.vertexAx = val & 0xffff;          // 12.4 fixed-point directly into params
    break;
case SST_fvertexAx:
    voodoo->fvertexAx.i = val;
    voodoo->params.vertexAx = (int32_t)(int16_t)(int32_t)(voodoo->fvertexAx.f * 16.0f) & 0xffff;
    break;
case SST_startR:
    voodoo->params.startR = val & 0xffffff;           // 12.12 directly into params
    break;
case SST_fstartR:
    tempif.i = val;
    voodoo->params.startR = (int32_t)(tempif.f * 4096.0f);  // float -> 12.12
    break;
case SST_dRdX:
    voodoo->params.dRdX = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);  // sign-extend
    break;
case SST_fdRdX:
    tempif.i = val;
    voodoo->params.dRdX = (int32_t)(tempif.f * 4096.0f);
    break;
// ... same for dRdY, G, B, A, Z, S, T, W ...
case SST_triangleCMD:
    voodoo->params.sign = val & (1 << 31);
    voodoo_queue_triangle(voodoo, &voodoo->params);   // <-- FIRES
    break;
```

### Mode 2: Setup Engine (Voodoo 2+, Glide 3.x, CMDFIFO)

**Used by**: Glide 3.x, Voodoo 2/Banshee/V3 games via CMDFIFO, and direct
sVertex register writes.

The CPU writes **per-vertex attributes** to a staging vertex (`voodoo->verts[3]`),
then fires `sDrawTriCMD` which calls `voodoo_triangle_setup()` to compute
gradients.

```
CPU writes (per vertex):
  SST_sVx, SST_sVy           (float screen coords)
  SST_sRed, SST_sGreen, ...  (float per-vertex colors)
  SST_sVz, SST_sWb, SST_sW0  (float Z, W)
  SST_sS0, SST_sT0, SST_sS1, SST_sT1  (float texcoords)
  SST_sDrawTriCMD             <- accumulates vertex, fires when 3 vertices ready
```

**Register definitions** (from `vid_voodoo_regs.h`):
```c
SST_sSetupMode   = 0x260,
SST_sVx          = 0x264,
SST_sVy          = 0x268,
SST_sARGB        = 0x26c,    // packed ARGB8888
SST_sRed         = 0x270,    // float
SST_sGreen       = 0x274,
SST_sBlue        = 0x278,
SST_sAlpha       = 0x27c,
SST_sVz          = 0x280,
SST_sWb          = 0x284,
SST_sW0          = 0x288,
SST_sS0          = 0x28c,
SST_sT0          = 0x290,
SST_sW1          = 0x294,
SST_sS1          = 0x298,
SST_sT1          = 0x29c,
SST_sDrawTriCMD  = 0x2a0,
SST_sBeginTriCMD = 0x2a4,
```

**Code path** (`vid_voodoo_reg.c` lines 742-874):
```c
// Each sVertex register write goes to voodoo->verts[3] (staging slot)
case SST_sVx:
    tempif.i = val;
    voodoo->verts[3].sVx = tempif.f;        // float screen X
    break;
case SST_sRed:
    tempif.i = val;
    voodoo->verts[3].sRed = tempif.f;       // float red [0..255]
    break;
// ... etc for all attributes ...

case SST_sDrawTriCMD:
    // Strip/fan vertex management (age-based replacement)
    // When 3 vertices accumulated:
    if (voodoo->num_verticies == 3) {
        voodoo_triangle_setup(voodoo);       // <-- COMPUTES GRADIENTS
        voodoo->cull_pingpong = !voodoo->cull_pingpong;
        voodoo->num_verticies = 2;
    }
    break;
```

**CMDFIFO Type 3 packets** (`vid_voodoo_fifo.c` lines 600-656) follow the
same path -- they write per-vertex data to `voodoo->verts[3]` and then call
`voodoo_cmdfifo_reg_writel(voodoo, SST_sDrawTriCMD, 0)` which triggers
`voodoo_triangle_setup()`.

---

## Key Data Structures

### `vert_t` -- Per-Vertex Data (BEFORE gradient computation)

Defined in `vid_voodoo_common.h` (line 256):
```c
typedef struct vert_t {
    float sVx;      // screen-space X (pixels)
    float sVy;      // screen-space Y (pixels)
    float sRed;     // vertex red   [0..255]
    float sGreen;   // vertex green [0..255]
    float sBlue;    // vertex blue  [0..255]
    float sAlpha;   // vertex alpha [0..255]
    float sVz;      // depth
    float sWb;      // 1/W (base, FBI)
    float sW0;      // 1/W (TMU0)
    float sS0;      // texture S (TMU0)
    float sT0;      // texture T (TMU0)
    float sW1;      // 1/W (TMU1)
    float sS1;      // texture S (TMU1)
    float sT1;      // texture T (TMU1)
} vert_t;
```

This is exactly what OpenGL needs: per-vertex X, Y, color, depth, texcoord.
All values are float. **This struct only has valid data in the setup engine
path (Mode 2).**

Storage in `voodoo_t`:
```c
vert_t       verts[4];          // [0..2] = triangle verts, [3] = staging slot
unsigned int vertex_ages[3];    // age tracking for strip/fan replacement
unsigned int vertex_next_age;
int          num_verticies;
int          cull_pingpong;
```

### `voodoo_params_t` -- Gradient Data (AFTER setup / direct writes)

Defined in `vid_voodoo_common.h` (line 124):
```c
typedef struct voodoo_params_t {
    int command;

    // Screen-space vertex positions (12.4 fixed-point, i.e. pixel * 16)
    int32_t vertexAx, vertexAy;   // vertex A (topmost after Y-sort)
    int32_t vertexBx, vertexBy;   // vertex B
    int32_t vertexCx, vertexCy;   // vertex C (bottommost after Y-sort)

    // Start values (at vertex A) -- 12.12 fixed-point for colors
    uint32_t startR, startG, startB, startA;
    uint32_t startZ;

    // Screen-space gradients -- 12.12 fixed-point for colors
    int32_t dRdX, dGdX, dBdX, dAdX, dZdX;
    int32_t dRdY, dGdY, dBdY, dAdY, dZdY;

    // W gradients (18.32 fixed-point)
    int64_t startW, dWdX, dWdY;

    // Per-TMU texture coordinate gradients
    struct {
        int64_t startS, startT, startW;    // 18.32 fixed-point
        int64_t dSdX, dTdX, dWdX;
        int64_t dSdY, dTdY, dWdY;
    } tmu[2];

    // Pipeline state (all needed for rendering)
    uint32_t color0, color1;
    uint32_t fbzMode, fbzColorPath, fogMode, alphaMode;
    // ... clip rects, texture state, fog table, etc.

    int sign;  // triangle winding (area sign)
} voodoo_params_t;
```

### What `voodoo_triangle_setup()` Does

Located in `vid_voodoo_setup.c` (line 58-240). This is the gradient computation
function called ONLY in Mode 2. It:

1. Copies `voodoo->verts[0..2]` to local `verts[3]` array
2. Y-sorts vertices (va, vb, vc) so vertexAy <= vertexBy <= vertexCy
3. Computes triangle area: `area = dxAB * dyBC - dxBC * dyAB`
4. Culling check (if enabled)
5. Converts float positions to 12.4 fixed-point in `params.vertexA/B/Cx/y`
6. For each enabled attribute (gated by `sSetupMode` flags):
   - Computes `startValue` = value at vertex A, scaled to fixed-point
   - Computes `dVdX` and `dVdY` using the standard barycentric formula

The gradient computation formula (for color R as example):
```c
// area-normalized edge deltas (precomputed)
dxAB /= area;  dxBC /= area;  dyAB /= area;  dyBC /= area;

// Start value = vertex A value * scale
params.startR = (int32_t)(verts[va].sRed * 4096.0f);

// Gradient in X direction
params.dRdX = (int32_t)(
    ((verts[va].sRed - verts[vb].sRed) * dyBC -
     (verts[vb].sRed - verts[vc].sRed) * dyAB) * 4096.0f);

// Gradient in Y direction
params.dRdY = (int32_t)(
    ((verts[vb].sRed - verts[vc].sRed) * dxAB -
     (verts[va].sRed - verts[vb].sRed) * dxBC) * 4096.0f);
```

7. Calls `voodoo_queue_triangle(voodoo, &voodoo->params)` at line 239

---

## The Data Flow Diagram

```
MODE 1 (Direct gradients -- Voodoo 1 / Glide 2.x):
  CPU (Glide) computes gradients
       |
       v
  Register writes: SST_vertexAx, SST_startR, SST_dRdX, SST_dRdY, ...
       |
       v
  voodoo->params (voodoo_params_t) -- gradients + positions
       |
  SST_triangleCMD fires
       |
       v
  voodoo_queue_triangle(&voodoo->params)
       |
       v
  params_buffer[] ring -> render threads -> voodoo_triangle()

  ** NO vert_t data exists. Per-vertex values were never stored. **


MODE 2 (Setup engine -- Voodoo 2+ / Glide 3.x / CMDFIFO):
  CPU (Glide/CMDFIFO) sends per-vertex data
       |
       v
  Register writes: SST_sVx, SST_sRed, ... -> voodoo->verts[3]
       |
  SST_sDrawTriCMD fires (when 3 verts accumulated)
       |
       v
  voodoo->verts[0..2] (vert_t) -- per-vertex float data
       |
  voodoo_triangle_setup()   <-- GRADIENT COMPUTATION HAPPENS HERE
       |                         reads from verts[0..2]
       v                         writes to voodoo->params
  voodoo->params (voodoo_params_t) -- gradients + positions
       |
  voodoo_queue_triangle(&voodoo->params)
       |
       v
  params_buffer[] ring -> render threads -> voodoo_triangle()

  ** vert_t data exists in voodoo->verts[0..2] at setup time. **
```

---

## Reconstruction Analysis

### Can we reconstruct per-vertex values from gradients?

**Mathematically: YES**, with caveats.

Given:
- Three vertex positions: (xA, yA), (xB, yB), (xC, yC) -- available in params
- Start value at vertex A: `startV`
- Gradients: `dVdX`, `dVdY`

The value at any point (x, y) is:
```
V(x, y) = startV + dVdX * (x - xA) + dVdY * (y - yA)
```

So the values at vertices B and C are:
```
V_B = startV + dVdX * (xB - xA) + dVdY * (yB - yA)
V_A = startV   (trivially)
V_C = startV + dVdX * (xC - xA) + dVdY * (yC - yA)
```

**However, there are precision issues:**

1. **Vertex positions are 12.4 fixed-point** (pixels * 16, stored as int16_t).
   The reconstruction requires converting back: `x_float = vertexAx / 16.0f`.

2. **Color/Z start/gradients are 12.12 fixed-point**. Values must be divided
   by 4096.0f. The int32_t cast during setup loses precision.

3. **Texture/W start/gradients are 18.32 fixed-point**. Values must be divided
   by 4294967296.0f (2^32). The int64_t cast loses precision.

4. **The rounding from float->int32_t during setup is lossy**. The setup engine
   does `(int32_t)(value * 4096.0f)` which truncates toward zero. The original
   float per-vertex value cannot be exactly recovered -- there is up to 1 LSB
   of error in the 12.12 representation, which propagates to the reconstruction.

5. **The gradient computation itself loses precision**. The area division and
   cross-product differences amplify floating-point error. For thin triangles
   (near-degenerate), the gradients can be wildly inaccurate, but this is also
   true of the original hardware.

### Precision of reconstruction per attribute

| Attribute | Fixed-point format | Scale | Reconstruction error |
|-----------|-------------------|-------|---------------------|
| Position X/Y | 12.4 (s15.4 in int16) | *16 | Exact (integer) |
| R, G, B, A | 12.12 (in int32) | *4096 | Up to ~0.00024 (1/4096) per vertex |
| Z | 20.12 (in int32) | *4096 | Up to ~0.00024 per vertex |
| W | 18.32 (in int64) | *2^32 | Up to ~2.3e-10 per vertex |
| S, T | 18.32 (in int64) | *2^32 | Up to ~2.3e-10 per vertex |

**Verdict**: Reconstruction from gradients introduces per-vertex error
proportional to the fixed-point quantization. For colors (12.12), this is
about 1/4096 of a color unit (where 255 = full white). This is well within
the "acceptable 1 LSB divergence" the design doc already accepts.

### Reconstruction formula (concrete code)

```c
// Given voodoo_params_t *p, reconstruct per-vertex colors for GL:
float xA = (float)p->vertexAx / 16.0f;
float yA = (float)p->vertexAy / 16.0f;
float xB = (float)p->vertexBx / 16.0f;
float yB = (float)p->vertexBy / 16.0f;
float xC = (float)p->vertexCx / 16.0f;
float yC = (float)p->vertexCy / 16.0f;

// Colors (12.12 fixed-point -> float, range [0..255])
float rA = (float)(int32_t)p->startR / 4096.0f;
float rB = rA + ((float)p->dRdX * (xB - xA) + (float)p->dRdY * (yB - yA)) / 4096.0f;
float rC = rA + ((float)p->dRdX * (xC - xA) + (float)p->dRdY * (yC - yA)) / 4096.0f;

// Texture coords (18.32 fixed-point -> float)
float sA = (double)p->tmu[0].startS / 4294967296.0;
float dSdX = (double)p->tmu[0].dSdX / 4294967296.0;
float dSdY = (double)p->tmu[0].dSdY / 4294967296.0;
float sB = sA + dSdX * (xB - xA) + dSdY * (yB - yA);
float sC = sA + dSdX * (xC - xA) + dSdY * (yC - yA);
```

**Important**: The gradient values in `voodoo_params_t` are ALREADY scaled to
fixed-point. The `dRdX` is in 12.12 units -- it represents "change in
(color * 4096) per pixel." So the position deltas must be in pixel units (not
12.4 units), and the division by 4096 happens once at the end.

Actually, wait -- let me re-examine. The positions in `voodoo_params_t` are
in 12.4 fixed-point (pixels * 16). But the gradients are "per pixel" not
"per 12.4 unit." Looking at how the software renderer uses them:

```c
// vid_voodoo_render.c line 1648
dx = 8 - (params->vertexAx & 0xf);   // sub-pixel adjustment in 12.4 units
// ...
state.base_r += (dx * params->dRdX + dy * params->dRdY) >> 4;
```

The `>> 4` at the end confirms: positions are in 12.4 (sub-pixel units * 16),
and the gradients are per-pixel, so multiplying a 12.4 position delta by a
per-pixel gradient gives a result that's 16x too large, hence the `>> 4` shift.

**Corrected reconstruction formula**:
```c
// Positions: 12.4 fixed-point -> pixel float
float xA = (float)p->vertexAx / 16.0f;
float yA = (float)p->vertexAy / 16.0f;
float xB = (float)p->vertexBx / 16.0f;
float yB = (float)p->vertexBy / 16.0f;
float xC = (float)p->vertexCx / 16.0f;
float yC = (float)p->vertexCy / 16.0f;

// Color at A (12.12 -> float [0..255])
float rA = (float)(int32_t)p->startR / 4096.0f;
// Gradients are per-pixel (12.12), positions are now in pixels, so:
float dRdX_f = (float)p->dRdX / 4096.0f;   // per-pixel change in color
float dRdY_f = (float)p->dRdY / 4096.0f;

float rB = rA + dRdX_f * (xB - xA) + dRdY_f * (yB - yA);
float rC = rA + dRdX_f * (xC - xA) + dRdY_f * (yC - yA);

// For GL: normalize to [0..1]
rA /= 255.0f;  rB /= 255.0f;  rC /= 255.0f;
```

---

## Approach Comparison

### Approach A: Intercept at `voodoo_triangle_setup()` BEFORE gradient computation

**Where**: Inside `voodoo_triangle_setup()` (vid_voodoo_setup.c), after
the Y-sort and culling check but BEFORE gradient computation. Read per-vertex
data directly from the `verts[va/vb/vc]` array.

**Pros**:
- Per-vertex data is available in pristine float form (no fixed-point loss)
- Perfect mapping to GL vertex attributes (position, color, texcoord, depth)
- No reconstruction math needed
- Numerically exact

**Cons**:
- Only works for Mode 2 (setup engine path)
- Mode 1 (direct gradient writes) NEVER passes through this function -- the
  CPU writes gradients directly to `voodoo_params_t` and fires `triangleCMD`
- Would need a SEPARATE intercept for Mode 1
- **Cannot be the sole intercept point**

### Approach B: Intercept at `voodoo_queue_triangle()` AFTER gradient computation

**Where**: Inside `voodoo_queue_triangle()` (vid_voodoo_render.c line 1850),
which is the common convergence point for both modes.

**Pros**:
- Single intercept point handles BOTH submission modes
- All pipeline state (fbzMode, alphaMode, etc.) is fully populated
- Texture references already resolved (`voodoo_use_texture()` called)
- Clean architectural boundary
- Simpler to implement and maintain

**Cons**:
- Per-vertex data must be reconstructed from gradients (math above)
- Reconstruction introduces up to 1/4096 error per color channel per vertex
- More complex vertex extraction code
- The vertex positions in params are Y-sorted (A is topmost), which changes
  the original vertex order -- this affects winding but OpenGL handles this
  via `params.sign`

### Approach C: Dual Intercept (RECOMMENDED)

**Where**: Two intercept points:
1. **Mode 2**: In `voodoo_triangle_setup()`, capture per-vertex data from
   `verts[va/vb/vc]` BEFORE gradient computation, then let setup proceed
   normally (for SW fallback compatibility).
2. **Mode 1**: In `voodoo_queue_triangle()`, reconstruct per-vertex data
   from gradients.

Both paths feed into the same VideoCommon submission function
(`vc_voodoo_submit_triangle()`).

**Pros**:
- Best precision for Mode 2 (no reconstruction needed)
- Still handles Mode 1 correctly (reconstruction)
- Clean separation of concerns
- Both paths produce the same output format (per-vertex data for GL)

**Cons**:
- Two intercept points = more integration code in existing Voodoo files
- Must ensure pipeline state is equally populated at both points
- Slightly more complex to maintain

---

## Recommended Architecture

### Option C with a twist: Reconstruct at `voodoo_queue_triangle()` for both

After further analysis, **Approach B is actually the best choice**, with
Approach C as a possible future optimization. Here is why:

1. **The precision loss from reconstruction is negligible.** The Voodoo hardware
   itself operates in fixed-point. Our reconstruction recovers values to within
   1 LSB of the fixed-point representation. The GPU will then interpolate these
   values with higher precision than the Voodoo hardware ever had. The design
   doc already accepts "<=1 LSB divergence."

2. **A single intercept point is dramatically simpler.** One code path, one
   place to branch GL vs SW, one place to copy pipeline state.

3. **Mode 2 per-vertex data has a problem anyway.** The `verts[]` array in
   `voodoo_t` is reused for strip/fan processing. By the time
   `voodoo_triangle_setup()` runs, the three active vertices are in `verts[0..2]`
   but with age-based replacement. After setup returns and before
   `voodoo_queue_triangle()` returns, the next `sDrawTriCMD` could overwrite
   one of the vertices. We would need to copy the per-vertex data at setup
   time, which adds complexity.

4. **The reconstruction math is straightforward.** It is 3 multiply-adds per
   attribute per vertex (B and C; A is just startValue). For a triangle with
   ~10 attributes, that's about 60 FP operations -- trivial compared to the
   cost of a GL draw call.

### Final Recommendation

**Intercept at `voodoo_queue_triangle()`** (single point). The branch should
look like:

```c
void
voodoo_queue_triangle(voodoo_t *voodoo, voodoo_params_t *params)
{
    if (voodoo->use_gl_renderer) {
        vc_voodoo_submit_triangle(voodoo->vc_ctx, params);
        return;  // skip SW render path entirely
    }

    // ... existing SW path (params_buffer, render threads, etc.) ...
}
```

Inside `vc_voodoo_submit_triangle()`, reconstruct per-vertex values:

```c
void
vc_voodoo_submit_triangle(vc_context_t *ctx, const voodoo_params_t *p)
{
    float verts[3][2];   // screen XY
    float colors[3][4];  // RGBA [0..1]
    float depth[3];      // Z [0..1]
    float tc0[3][3];     // S, T, W for TMU0
    float tc1[3][3];     // S, T, W for TMU1
    float w[3];          // 1/W (base)

    // Extract positions (12.4 -> float pixels)
    verts[0][0] = (float)p->vertexAx / 16.0f;
    verts[0][1] = (float)p->vertexAy / 16.0f;
    verts[1][0] = (float)p->vertexBx / 16.0f;
    verts[1][1] = (float)p->vertexBy / 16.0f;
    verts[2][0] = (float)p->vertexCx / 16.0f;
    verts[2][1] = (float)p->vertexCy / 16.0f;

    // Reconstruct per-vertex colors from gradients
    float dx_ba = verts[1][0] - verts[0][0];
    float dy_ba = verts[1][1] - verts[0][1];
    float dx_ca = verts[2][0] - verts[0][0];
    float dy_ca = verts[2][1] - verts[0][1];

    // Color A = start value
    colors[0][0] = (float)(int32_t)p->startR / (4096.0f * 255.0f);
    colors[0][1] = (float)(int32_t)p->startG / (4096.0f * 255.0f);
    colors[0][2] = (float)(int32_t)p->startB / (4096.0f * 255.0f);
    colors[0][3] = (float)(int32_t)p->startA / (4096.0f * 255.0f);

    // Color B = A + gradient * delta_position
    colors[1][0] = colors[0][0] + ((float)p->dRdX * dx_ba + (float)p->dRdY * dy_ba) / (4096.0f * 255.0f);
    colors[1][1] = colors[0][1] + ((float)p->dGdX * dx_ba + (float)p->dGdY * dy_ba) / (4096.0f * 255.0f);
    colors[1][2] = colors[0][2] + ((float)p->dBdX * dx_ba + (float)p->dBdY * dy_ba) / (4096.0f * 255.0f);
    colors[1][3] = colors[0][3] + ((float)p->dAdX * dx_ba + (float)p->dAdY * dy_ba) / (4096.0f * 255.0f);

    // Color C = A + gradient * delta_position
    colors[2][0] = colors[0][0] + ((float)p->dRdX * dx_ca + (float)p->dRdY * dy_ca) / (4096.0f * 255.0f);
    // ... same pattern for G, B, A ...

    // Reconstruct per-vertex depth
    depth[0] = (float)(int32_t)p->startZ / (4096.0f * 65535.0f);
    depth[1] = depth[0] + ((float)p->dZdX * dx_ba + (float)p->dZdY * dy_ba) / (4096.0f * 65535.0f);
    depth[2] = depth[0] + ((float)p->dZdX * dx_ca + (float)p->dZdY * dy_ca) / (4096.0f * 65535.0f);

    // Reconstruct per-vertex texture coords (18.32 format)
    // ... similar pattern using tmu[0].startS, tmu[0].dSdX, tmu[0].dSdY ...

    // Submit to VideoCommon ring buffer
    vc_ring_submit_triangle(ctx, verts, colors, depth, tc0, tc1, w, p);
}
```

### Why GPU Interpolation Will Match

OpenGL's hardware interpolation is **linear in screen space** for `flat`/`smooth`
qualifiers. The Voodoo hardware also does **linear screen-space interpolation**
(it walks edges and spans with `start + dX * step`). So when we reconstruct
per-vertex values and let GL interpolate, the per-pixel values should match the
Voodoo's own rasterizer within floating-point rounding differences.

For perspective-correct texture coordinates (S/T divided by W), the Voodoo does
this division per-pixel in the TMU. We need to pass S, T, and W as separate
vertex attributes and do the perspective divide in the fragment shader, NOT rely
on GL's built-in perspective correction (which uses the vertex shader's
`gl_Position.w`).

---

## Edge Cases and Concerns

### 1. Y-Sorted Vertices
The setup engine Y-sorts vertices so A is topmost. Direct gradient writes may
or may not be Y-sorted (depends on Glide). The `params.sign` flag indicates
winding. For GL, we can use `glFrontFace()` per-batch or emit vertices in the
correct order using `sign`.

### 2. Vertex Position Precision
Positions are 12.4 fixed-point (4 bits sub-pixel = 1/16 pixel precision). This
matches typical software rasterizers. OpenGL rasterization has at least 4 bits
sub-pixel precision per spec (GL 4.1 spec section 3.6.1), so this is fine.

### 3. `startR` as uint32_t but Represents Signed Value
The `startR` field is `uint32_t` in the struct but holds a value that should be
interpreted as signed for reconstruction (colors can be clamped, but intermediate
values during interpolation can be negative on Voodoo hardware). The cast to
`(int32_t)` in the reconstruction is correct.

### 4. The `FBZ_PARAM_ADJUST` Flag
In `voodoo_triangle()` (render code), there is a parameter adjustment step:
```c
if (params->fbzColorPath & FBZ_PARAM_ADJUST) {
    state.base_r += (dx * params->dRdX + dy * params->dRdY) >> 4;
    // ...
}
```
This shifts the start point by a sub-pixel offset to snap to pixel centers.
OpenGL's rasterizer handles sub-pixel positioning natively, so we do NOT need
to replicate this adjustment -- it is an artifact of the software edge-walker.

### 5. Strip/Fan Vertex Reuse
In Mode 2, the setup engine reuses vertices across strip/fan primitives. Our
intercept at `voodoo_queue_triangle()` is downstream of this logic, so each
call represents an independent triangle with all data in `params`. No concern.

### 6. Degenerate Triangles
If `area == 0`, the setup engine returns early and never calls
`voodoo_queue_triangle()`. So our intercept will never see degenerate triangles.
For Mode 1, the Glide library typically filters these, but a degenerate triangle
would have `dVdX = dVdY = 0` (or undefined), and OpenGL would render zero pixels
anyway.

---

## References

- **86Box source**: `src/include/86box/vid_voodoo_common.h` (struct definitions)
- **86Box source**: `src/include/86box/vid_voodoo_regs.h` (register addresses)
- **86Box source**: `src/video/vid_voodoo_reg.c` (register write handlers)
- **86Box source**: `src/video/vid_voodoo_setup.c` (`voodoo_triangle_setup()`)
- **86Box source**: `src/video/vid_voodoo_render.c` (`voodoo_queue_triangle()`,
  `voodoo_triangle()`)
- **86Box source**: `src/video/vid_voodoo_fifo.c` (CMDFIFO Type 3 packet handling)
- **3dfx SST-1 Specification**: http://www.o3one.org/hwdocs/video/voodoo_graphics.pdf
- **Fabien Sanglard's Voodoo retrospective**: https://fabiensanglard.net/3dfx_sst1/
- **OpenGL 4.1 Spec, Section 3.6.1**: Rasterization sub-pixel precision requirements
- **MAME voodoo.cpp**: https://github.com/mamedev/mame/blob/master/src/devices/video/voodoo.cpp
  (reference Voodoo emulation, software-only)
