#include <windows.h>
#include <stdint.h>

#define IMM_STORE_ITERS_DEFAULT 600000UL
#define BRANCH_ITERS_DEFAULT    2200000UL
#define MMX_ITERS_DEFAULT       800000UL
#define SMC_ITERS_DEFAULT       120000UL

typedef uint32_t(__cdecl *smc_fn_t)(void);

static size_t
tiny_strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

static int
tiny_contains(const char *s, const char *needle)
{
    size_t i;
    size_t nlen = tiny_strlen(needle);
    size_t slen = tiny_strlen(s);

    if (nlen == 0 || slen < nlen)
        return 0;

    for (i = 0; i <= (slen - nlen); i++) {
        size_t j;
        int    ok = 1;
        for (j = 0; j < nlen; j++) {
            if (s[i + j] != needle[j]) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return 1;
    }

    return 0;
}

static void
tiny_write(const char *s)
{
    DWORD written = 0;
    HANDLE h      = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(h, s, (DWORD) tiny_strlen(s), &written, NULL);
}

static void
tiny_write_line(const char *s)
{
    tiny_write(s);
    tiny_write("\r\n");
}

static void
hex8(uint32_t v, char out[9])
{
    static const char hex[] = "0123456789abcdef";
    int               i;
    for (i = 0; i < 8; i++) {
        int shift = (7 - i) * 4;
        out[i]    = hex[(v >> shift) & 0xF];
    }
    out[8] = '\0';
}

static void
u32_to_dec(uint32_t v, char out[16])
{
    char tmp[16];
    int  i = 0;
    int  j = 0;
    if (v == 0U) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0U && i < 15) {
        tmp[i++] = (char) ('0' + (v % 10U));
        v /= 10U;
    }
    while (i > 0)
        out[j++] = tmp[--i];
    out[j] = '\0';
}

static uint32_t
rotl32(uint32_t x, uint32_t r)
{
    r &= 31U;
    return (x << r) | (x >> ((32U - r) & 31U));
}

static uint32_t
lcg32(uint32_t x)
{
    return (x * 1664525U) + 1013904223U;
}

static uint32_t
phase_imm_store(uint32_t iters)
{
    uint32_t i;
    uint32_t seed = 0x1234abcdU;
    uint32_t chk  = 0x9e3779b9U;
    enum {
        ARR_WORDS = 32768,
        ARR_MASK  = ARR_WORDS - 1
    };
    uint32_t *arr = (uint32_t *) VirtualAlloc(NULL, ARR_WORDS * sizeof(uint32_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!arr)
        return 0U;

    for (i = 0; i < ARR_WORDS; i++)
        arr[i] = 0U;

    for (i = 0; i < iters; i++) {
        uint32_t idx = (seed >> 7) & ARR_MASK;
        uint32_t val = seed ^ 0xa5a5a5a5U ^ (i * 2654435761U);
        arr[idx] = val;
        chk ^= rotl32(arr[(idx + 37U) & ARR_MASK] + val + i, (i & 31U));
        seed = lcg32(seed);
    }

    for (i = 0; i < ARR_WORDS; i++)
        chk ^= rotl32(arr[i] + i, (i & 31U));

    VirtualFree(arr, 0U, MEM_RELEASE);
    return chk;
}

static uint32_t
phase_branch_helper(uint32_t iters)
{
    uint32_t i;
    uint32_t x     = 0x31415926U;
    uint32_t y     = 0x27182818U;
    uint32_t state = 1U;
    uint32_t chk   = 0xfeedfaceU;

    for (i = 0; i < iters; i++) {
        uint32_t t = x ^ rotl32(y, state);
        switch (state & 7U) {
            case 0:
                if (t & 1U)
                    x += 0x9e3779b9U;
                else
                    y ^= 0x7f4a7c15U;
                state = (state + 3U) ^ (t & 15U);
                break;
            case 1:
                if ((int32_t) t < 0)
                    x = rotl32(x ^ y, 5U);
                else
                    y = rotl32(y + x, 11U);
                state = (state * 5U) + 1U;
                break;
            case 2:
                if ((t & 255U) == 0x5aU)
                    chk ^= 0x13579bdfU;
                else
                    chk += rotl32(t, (i & 31U));
                state ^= t;
                break;
            case 3:
                x = (x + y) ^ (t >> 3);
                y = (y - x) ^ (t << 1);
                state += 7U;
                break;
            case 4:
                x ^= rotl32(t, 9U);
                if (x & 0x10000U)
                    y += 0x10204081U;
                else
                    y -= 0x01122334U;
                state ^= 0x55aa55aaU;
                break;
            case 5:
                y = (y ^ x) + 0x6c8e9cf5U;
                state = rotl32(state + t, 3U);
                break;
            case 6:
                x = x + rotl32(y, 13U);
                y = y ^ rotl32(x, 7U);
                state += (t & 3U) + 1U;
                break;
            default:
                x ^= 0xdeadbeefU;
                y ^= 0x8badf00dU;
                state = state ^ 0x01010101U;
                break;
        }
        chk ^= rotl32(x + y + t + state, ((i ^ state) & 31U));
    }

    return chk ^ x ^ y ^ state;
}

static uint32_t
phase_mmx_touch(uint32_t iters)
{
#if defined(__i386__)
    uint32_t i;
    uint32_t a   = 0x89abcdefU;
    uint32_t b   = 0x10203040U;
    uint32_t out = 0U;
    uint32_t chk = 0x2468ace0U;

    for (i = 0; i < iters; i++) {
        __asm__ volatile(
            "movd %1, %%mm0\n\t"
            "movd %2, %%mm1\n\t"
            "paddd %%mm1, %%mm0\n\t"
            "movd %%mm0, %0\n\t"
            : "=r"(out)
            : "r"(a), "r"(b)
            : "mm0", "mm1");
        chk ^= rotl32(out + i, (i & 31U));
        a = lcg32(a ^ out);
        b = lcg32(b + 0x9e3779b9U);
    }
    __asm__ volatile("emms");
    return chk;
#else
    uint32_t i;
    uint32_t x   = 0x55aa55aaU;
    uint32_t chk = 0x0badc0deU;
    for (i = 0; i < iters; i++) {
        x = lcg32(x);
        chk ^= rotl32(x, (i & 31U));
    }
    return chk ^ 0x4d4d5821U;
#endif
}

static uint32_t
phase_smc_touch(uint32_t iters)
{
    uint32_t i;
    uint32_t chk = 0x11223344U;
    uint8_t *buf = (uint8_t *) VirtualAlloc(NULL, 16U, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!buf)
        return 0U;

    buf[0] = 0xB8;
    *(uint32_t *) &buf[1] = 0U;
    buf[5] = 0xC3;

    for (i = 0; i < iters; i++) {
        uint32_t imm  = 0x600d0000U ^ (i * 1103515245U);
        uint32_t rval;
        *(uint32_t *) &buf[1] = imm;
        FlushInstructionCache(GetCurrentProcess(), buf, 6U);
        rval = ((smc_fn_t) buf)();
        chk ^= rotl32(rval + i, (i & 31U));
    }

    VirtualFree(buf, 0U, MEM_RELEASE);
    return chk;
}

static void
emit_phase_end(const char *name, uint32_t checksum, uint32_t iters)
{
    char hx[9];
    char dec[16];
    tiny_write("PHASE_END ");
    tiny_write(name);
    tiny_write(" checksum=");
    hex8(checksum, hx);
    tiny_write(hx);
    tiny_write(" iters=");
    u32_to_dec(iters, dec);
    tiny_write(dec);
    tiny_write("\r\n");
}

static uint32_t
run_phase(const char *name, uint32_t iters, uint32_t (*fn)(uint32_t), uint32_t *err)
{
    uint32_t chk;
    tiny_write("PHASE_START ");
    tiny_write_line(name);
    chk = fn(iters);
    if (chk == 0U) {
        tiny_write("MICROSTRESS_ERROR phase=");
        tiny_write(name);
        tiny_write_line(" checksum=00000000");
        *err = 1U;
        return 0U;
    }
    emit_phase_end(name, chk, iters);
    *err = 0U;
    return chk;
}

__attribute__((noreturn)) void
_start(void)
{
    const char *cmd = GetCommandLineA();
    int         quick_mode = tiny_contains(cmd, "--quick");
    int         enable_smc = tiny_contains(cmd, "--smc");
    uint32_t    div = quick_mode ? 8U : 1U;
    uint32_t    total = 0x13572468U;
    uint32_t    chk;
    uint32_t    err;
    char        hx[9];

    tiny_write_line("MICROSTRESS_START");

    chk = run_phase("imm_store", IMM_STORE_ITERS_DEFAULT / div, phase_imm_store, &err);
    if (err)
        ExitProcess(3U);
    total ^= rotl32(chk, 1U);

    chk = run_phase("branch_helper", BRANCH_ITERS_DEFAULT / div, phase_branch_helper, &err);
    if (err)
        ExitProcess(4U);
    total ^= rotl32(chk, 7U);

    chk = run_phase("mmx_touch", MMX_ITERS_DEFAULT / div, phase_mmx_touch, &err);
    if (err)
        ExitProcess(5U);
    total ^= rotl32(chk, 13U);

    if (enable_smc) {
        chk = run_phase("smc_touch", SMC_ITERS_DEFAULT / div, phase_smc_touch, &err);
        if (err)
            ExitProcess(6U);
        total ^= rotl32(chk, 19U);
    } else {
        tiny_write_line("PHASE_SKIP smc_touch reason=disabled");
    }

    tiny_write("MICROSTRESS_DONE total=");
    hex8(total, hx);
    tiny_write(hx);
    tiny_write("\r\n");
    ExitProcess(0U);
}
