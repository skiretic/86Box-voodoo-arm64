/*
 * Minimal Win32 Glide2 alpha blend coverage probe.
 *
 * Builds with i686-w64-mingw32-gcc and runs on Windows 98/K6-class CPUs.
 * It loads glide2x.dll at runtime, configures table-backed source/destination
 * alpha blend factor pairs, and draws enough triangles for the validator mode
 * buckets to see the target alphaMode values.
 */
#include <stdint.h>
#include <windows.h>

typedef int32_t  FxI32;
typedef uint32_t FxU32;
typedef uint16_t FxU16;
typedef uint8_t  FxU8;
typedef int32_t  FxBool;
typedef uint32_t GrColor_t;
typedef uint8_t  GrAlpha_t;

#define FXFALSE 0
#define FXTRUE  1

#define GR_MIPMAPLEVELMASK_EVEN  0x1
#define GR_MIPMAPLEVELMASK_ODD   0x2
#define GR_MIPMAPLEVELMASK_BOTH  (GR_MIPMAPLEVELMASK_EVEN | GR_MIPMAPLEVELMASK_ODD)

#define GR_TMU0 0
#define GR_TMU1 1

#define GR_COMBINE_FUNCTION_LOCAL 1
#define GR_COMBINE_FUNCTION_SCALE_OTHER 3
#define GR_COMBINE_FUNCTION_BLEND 7

#define GR_COMBINE_FACTOR_NONE 0
#define GR_COMBINE_FACTOR_DETAIL_FACTOR 4
#define GR_COMBINE_FACTOR_LOD_FRACTION 5
#define GR_COMBINE_FACTOR_ONE 8

#define GR_COMBINE_LOCAL_ITERATED 0
#define GR_COMBINE_LOCAL_NONE 1
#define GR_COMBINE_OTHER_TEXTURE 1
#define GR_COMBINE_OTHER_NONE 2

#define GR_BLEND_ZERO 0x0
#define GR_BLEND_SRC_ALPHA 0x1
#define GR_BLEND_DST_ALPHA 0x3
#define GR_BLEND_ONE 0x4
#define GR_BLEND_ONE_MINUS_SRC_ALPHA 0x5
#define GR_BLEND_ONE_MINUS_DST_ALPHA 0x7

#define GR_RESOLUTION_640x480 7
#define GR_REFRESH_60Hz 0
#define GR_COLORFORMAT_RGBA 2
#define GR_ORIGIN_UPPER_LEFT 0

#define GR_LOD_64 2
#define GR_ASPECT_1x1 3
#define GR_TEXFMT_RGB_565 0xa
#define GR_MIPMAP_NEAREST 1
#define GR_TEXTUREFILTER_BILINEAR 1
#define GR_BUFFER_BACKBUFFER 1
#define GR_CULL_DISABLE 0

typedef struct {
    float sow;
    float tow;
    float oow;
} GrTmuVertex;

typedef struct {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    GrTmuVertex tmuvtx[3];
} GrVertex;

typedef struct {
    FxI32 smallLod;
    FxI32 largeLod;
    FxI32 aspectRatio;
    FxI32 format;
    void *data;
} GrTexInfo;

typedef void   (WINAPI *PFN_void_void)(void);
typedef void   (WINAPI *PFN_grSstSelect)(int);
typedef FxBool (WINAPI *PFN_grSstWinOpen)(FxU32, FxI32, FxI32, FxI32, FxI32, int, int);
typedef void   (WINAPI *PFN_grBufferClear)(GrColor_t, GrAlpha_t, FxU16);
typedef void   (WINAPI *PFN_grBufferSwap)(int);
typedef void   (WINAPI *PFN_grRenderBuffer)(FxI32);
typedef void   (WINAPI *PFN_grCullMode)(FxI32);
typedef void   (WINAPI *PFN_grColorCombine)(FxI32, FxI32, FxI32, FxI32, FxBool);
typedef void   (WINAPI *PFN_grAlphaCombine)(FxI32, FxI32, FxI32, FxI32, FxBool);
typedef void   (WINAPI *PFN_grAlphaBlendFunction)(FxI32, FxI32, FxI32, FxI32);
typedef FxU32  (WINAPI *PFN_grTexMinAddress)(FxI32);
typedef FxU32  (WINAPI *PFN_grTexTextureMemRequired)(FxU32, GrTexInfo *);
typedef void   (WINAPI *PFN_grTexDownloadMipMap)(FxI32, FxU32, FxU32, GrTexInfo *);
typedef void   (WINAPI *PFN_grTexSource)(FxI32, FxU32, FxU32, GrTexInfo *);
typedef void   (WINAPI *PFN_grTexCombine)(FxI32, FxI32, FxI32, FxI32, FxI32, FxBool, FxBool);
typedef void   (WINAPI *PFN_grTexDetailControl)(FxI32, int, FxU8, float);
typedef void   (WINAPI *PFN_grTexFilterMode)(FxI32, FxI32, FxI32);
typedef void   (WINAPI *PFN_grTexMipMapMode)(FxI32, FxI32, FxBool);
typedef void   (WINAPI *PFN_grDrawTriangle)(const GrVertex *, const GrVertex *, const GrVertex *);

static HMODULE glide;
static PFN_void_void grGlideInit;
static PFN_void_void grGlideShutdown;
static PFN_grSstSelect grSstSelect;
static PFN_grSstWinOpen grSstWinOpen;
static PFN_void_void grSstWinClose;
static PFN_grBufferClear grBufferClear;
static PFN_grBufferSwap grBufferSwap;
static PFN_grRenderBuffer grRenderBuffer;
static PFN_grCullMode grCullMode;
static PFN_grColorCombine grColorCombine;
static PFN_grAlphaCombine grAlphaCombine;
static PFN_grAlphaBlendFunction grAlphaBlendFunction;
static PFN_grTexMinAddress grTexMinAddress;
static PFN_grTexTextureMemRequired grTexTextureMemRequired;
static PFN_grTexDownloadMipMap grTexDownloadMipMap;
static PFN_grTexSource grTexSource;
static PFN_grTexCombine grTexCombine;
static PFN_grTexDetailControl grTexDetailControl;
static PFN_grTexFilterMode grTexFilterMode;
static PFN_grTexMipMapMode grTexMipMapMode;
static PFN_grDrawTriangle grDrawTriangle;
static uint16_t tex0[64 * 64];
static uint16_t tex1[64 * 64];
static GrTexInfo info0;
static GrTexInfo info1;
static HANDLE log_file = INVALID_HANDLE_VALUE;
static HWND win;

static void
out_text(const char *s)
{
    DWORD len = 0;
    DWORD wrote;

    while (s[len] != '\0')
        len++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, len, &wrote, NULL);
    if (log_file != INVALID_HANDLE_VALUE)
        WriteFile(log_file, s, len, &wrote, NULL);
}

static void
out_u32(FxU32 value)
{
    char buf[11];
    int pos = 10;

    buf[pos] = '\0';
    do {
        pos--;
        buf[pos] = (char) ('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    out_text(&buf[pos]);
}

static void
append_text(char *dst, const char *src, int *pos, int max)
{
    while (*src != '\0' && *pos < (max - 1)) {
        dst[*pos] = *src;
        (*pos)++;
        src++;
    }
    dst[*pos] = '\0';
}

static void
append_u32(char *dst, FxU32 value, int *pos, int max)
{
    char tmp[11];
    int tmp_pos = 10;

    tmp[tmp_pos] = '\0';
    do {
        tmp_pos--;
        tmp[tmp_pos] = (char) ('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    append_text(dst, &tmp[tmp_pos], pos, max);
}

static FARPROC
resolve_proc(const char *name, int bytes)
{
    char decorated[128];
    int pos = 0;
    FARPROC proc = GetProcAddress(glide, name);

    if (proc)
        return proc;
    append_text(decorated, "_", &pos, sizeof(decorated));
    append_text(decorated, name, &pos, sizeof(decorated));
    append_text(decorated, "@", &pos, sizeof(decorated));
    append_u32(decorated, (FxU32) bytes, &pos, sizeof(decorated));
    proc = GetProcAddress(glide, decorated);
    if (!proc) {
        out_text("missing ");
        out_text(name);
        out_text(" / ");
        out_text(decorated);
        out_text("\r\n");
    }
    return proc;
}

#define RESOLVE(name, bytes)                                                        \
    do {                                                                            \
        name = (void *) resolve_proc(#name, bytes);                                 \
        if (!name)                                                                  \
            return 2;                                                               \
    } while (0)

static int
load_glide(void)
{
    glide = LoadLibraryA("glide2x.dll");
    if (!glide) {
        out_text("cannot load glide2x.dll\r\n");
        return 1;
    }

    RESOLVE(grGlideInit, 0);
    RESOLVE(grGlideShutdown, 0);
    RESOLVE(grSstSelect, 4);
    RESOLVE(grSstWinOpen, 28);
    RESOLVE(grSstWinClose, 0);
    RESOLVE(grBufferClear, 12);
    RESOLVE(grBufferSwap, 4);
    RESOLVE(grRenderBuffer, 4);
    RESOLVE(grCullMode, 4);
    RESOLVE(grColorCombine, 20);
    RESOLVE(grAlphaCombine, 20);
    RESOLVE(grAlphaBlendFunction, 16);
    RESOLVE(grTexMinAddress, 4);
    RESOLVE(grTexTextureMemRequired, 8);
    RESOLVE(grTexDownloadMipMap, 16);
    RESOLVE(grTexSource, 16);
    RESOLVE(grTexCombine, 28);
    RESOLVE(grTexDetailControl, 16);
    RESOLVE(grTexFilterMode, 12);
    RESOLVE(grTexMipMapMode, 12);
    RESOLVE(grDrawTriangle, 12);
    return 0;
}

static LRESULT CALLBACK
probe_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static int
create_probe_window(void)
{
    WNDCLASSA wc;
    uint8_t *raw = (uint8_t *) &wc;

    for (int i = 0; i < (int) sizeof(wc); i++)
        raw[i] = 0;
    wc.lpfnWndProc = probe_wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "AlphaProbeWindow";
    if (!RegisterClassA(&wc)) {
        out_text("RegisterClassA failed\r\n");
        return 0;
    }

    win = CreateWindowExA(0, "AlphaProbeWindow", "Alpha Probe",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          640, 480, NULL, NULL, wc.hInstance, NULL);
    if (!win) {
        out_text("CreateWindowExA failed\r\n");
        return 0;
    }
    ShowWindow(win, SW_SHOW);
    UpdateWindow(win);
    return 1;
}

static void
fill_texture(uint16_t *tex, uint16_t a, uint16_t b)
{
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++)
            tex[y * 64 + x] = (((x ^ y) & 8) != 0) ? a : b;
    }
}

static void
make_tri(GrVertex v[3], float xoff)
{
    uint8_t *raw = (uint8_t *) v;

    for (int i = 0; i < (int) (sizeof(GrVertex) * 3); i++)
        raw[i] = 0;

    v[0].x = 40.0f + xoff;  v[0].y = 40.0f;
    v[1].x = 600.0f;        v[1].y = 120.0f;
    v[2].x = 240.0f + xoff; v[2].y = 440.0f;

    for (int i = 0; i < 3; i++) {
        v[i].z = 0.5f;
        v[i].r = v[i].g = v[i].b = 255.0f;
        v[i].a = (i == 0) ? 64.0f : ((i == 1) ? 192.0f : 255.0f);
        v[i].ooz = 1.0f;
        v[i].oow = (i == 0) ? 1.0f : ((i == 1) ? 0.35f : 0.08f);
        for (int t = 0; t < 3; t++)
            v[i].tmuvtx[t].oow = v[i].oow;
    }

    for (int t = 0; t < 2; t++) {
        v[0].tmuvtx[t].sow = 0.0f;  v[0].tmuvtx[t].tow = 0.0f;
        v[1].tmuvtx[t].sow = 63.0f * v[1].oow; v[1].tmuvtx[t].tow = 0.0f;
        v[2].tmuvtx[t].sow = 0.0f;  v[2].tmuvtx[t].tow = 63.0f * v[2].oow;
    }
}

static void
draw_case(FxI32 factor, const char *name)
{
    GrVertex v[3];

    out_text("case ");
    out_text(name);
    out_text("\r\n");
    grAlphaBlendFunction(factor, factor, GR_BLEND_ONE, GR_BLEND_ZERO);

    for (int frame = 0; frame < 80; frame++) {
        grBufferClear(0x00202020, 0, 0);
        for (int i = 0; i < 12; i++) {
            make_tri(v, (float) ((i % 4) * 8));
            grDrawTriangle(&v[0], &v[1], &v[2]);
        }
        grBufferSwap(0);
    }
}

static int
app_main(void)
{
    FxU32 addr0;
    FxU32 addr1;
    int rc;

    log_file = CreateFileA("C:\\ALPHAPRB.TXT", GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE)
        log_file = CreateFileA("ALPHAPRB.TXT", GENERIC_WRITE, FILE_SHARE_READ,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    rc = load_glide();
    if (rc) {
        MessageBoxA(NULL, "Alpha probe failed: cannot load/resolve Glide2. See ALPHAPRB.TXT.",
                    "Alpha Probe", MB_OK | MB_ICONERROR);
        return rc;
    }
    if (!create_probe_window()) {
        MessageBoxA(NULL, "Alpha probe failed: cannot create Win98 window. See ALPHAPRB.TXT.",
                    "Alpha Probe", MB_OK | MB_ICONERROR);
        return 4;
    }

    fill_texture(tex0, 0xf800, 0x001f);
    fill_texture(tex1, 0x07e0, 0xffff);
    info0.smallLod = info0.largeLod = GR_LOD_64;
    info0.aspectRatio = GR_ASPECT_1x1;
    info0.format = GR_TEXFMT_RGB_565;
    info0.data = tex0;
    info1 = info0;
    info1.data = tex1;

    out_text("Alpha blend pair probe start\r\n");
    grGlideInit();
    grSstSelect(0);
    if (!grSstWinOpen((FxU32) (uintptr_t) win, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                      GR_COLORFORMAT_RGBA, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        out_text("grSstWinOpen failed\r\n");
        grGlideShutdown();
        MessageBoxA(win, "Alpha probe failed: grSstWinOpen. See C:\\ALPHAPRB.TXT.",
                    "Alpha Probe", MB_OK | MB_ICONERROR);
        return 3;
    }

    grRenderBuffer(GR_BUFFER_BACKBUFFER);
    grCullMode(GR_CULL_DISABLE);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);

    addr0 = grTexMinAddress(GR_TMU0);
    addr1 = grTexMinAddress(GR_TMU1);
    out_text("tmu0 addr=");
    out_u32(addr0);
    out_text(" need=");
    out_u32(grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &info0));
    out_text("\r\n");
    out_text("tmu1 addr=");
    out_u32(addr1);
    out_text(" need=");
    out_u32(grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &info1));
    out_text("\r\n");

    grTexDownloadMipMap(GR_TMU0, addr0, GR_MIPMAPLEVELMASK_BOTH, &info0);
    grTexDownloadMipMap(GR_TMU1, addr1, GR_MIPMAPLEVELMASK_BOTH, &info1);
    grTexSource(GR_TMU0, addr0, GR_MIPMAPLEVELMASK_BOTH, &info0);
    grTexSource(GR_TMU1, addr1, GR_MIPMAPLEVELMASK_BOTH, &info1);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    grTexFilterMode(GR_TMU1, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXTRUE);
    grTexMipMapMode(GR_TMU1, GR_MIPMAP_NEAREST, FXTRUE);
    grTexDetailControl(GR_TMU0, 0, 7, 1.0f);
    grTexDetailControl(GR_TMU1, 0, 7, 1.0f);
    grTexCombine(GR_TMU1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);

    draw_case(GR_BLEND_SRC_ALPHA, "SRC_ALPHA/SRC_ALPHA");
    draw_case(GR_BLEND_DST_ALPHA, "DST_ALPHA/DST_ALPHA");
    draw_case(GR_BLEND_ONE_MINUS_SRC_ALPHA, "ONE_MINUS_SRC_ALPHA/ONE_MINUS_SRC_ALPHA");
    draw_case(GR_BLEND_ONE_MINUS_DST_ALPHA, "ONE_MINUS_DST_ALPHA/ONE_MINUS_DST_ALPHA");

    grSstWinClose();
    grGlideShutdown();
    out_text("Alpha probe done\r\n");
    MessageBoxA(win, "Alpha probe done. Close VM or report done now.", "Alpha Probe", MB_OK);
    if (log_file != INVALID_HANDLE_VALUE)
        CloseHandle(log_file);
    return 0;
}

void
WinMainCRTStartup(void)
{
    ExitProcess((UINT) app_main());
}
