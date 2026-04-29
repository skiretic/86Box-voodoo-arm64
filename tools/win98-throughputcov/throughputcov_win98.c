#include <windows.h>
#include <stdint.h>

/*
 * Minimal CRT-free Win98 throughput validation harness.
 * Emits deterministic phase markers + per-phase and total hashes.
 */

#define ITERS_SMOKE  80000UL
#define ITERS_FULL  300000UL
#define ITERS_STRESS 900000UL

typedef struct phase_result {
    const char *name;
    uint32_t iters;
    uint32_t hash;
} phase_result_t;

static size_t tiny_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void tiny_write(const char *s)
{
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD) tiny_strlen(s), &written, NULL);
}

static void tiny_writeln(const char *s)
{
    tiny_write(s);
    tiny_write("\r\n");
}

static void u32_hex(uint32_t v, char out[9])
{
    static const char h[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 8; i++) out[i] = h[(v >> ((7 - i) * 4)) & 0xF];
    out[8] = 0;
}

static void u32_dec(uint32_t v, char out[16])
{
    char tmp[16];
    int i = 0;
    int j = 0;
    if (!v) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (v && i < 15) {
        tmp[i++] = (char) ('0' + (v % 10U));
        v /= 10U;
    }
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}

static uint32_t lcg32(uint32_t x)
{
    return x * 1664525U + 1013904223U;
}

static uint32_t rotl32(uint32_t x, uint32_t r)
{
    r &= 31U;
    return (x << r) | (x >> ((32U - r) & 31U));
}

/*
 * Integer-heavy deterministic loop intended to exercise sustained dynarec paths
 * without host/guest dependencies beyond basic 32-bit arithmetic.
 */
static uint32_t run_phase(uint32_t seed, uint32_t iters)
{
    uint32_t a = seed ^ 0x9e3779b9U;
    uint32_t b = seed ^ 0x7f4a7c15U;
    uint32_t c = seed ^ 0x94d049bbU;
    uint32_t i;

    for (i = 0; i < iters; i++) {
        a = lcg32(a + i);
        b ^= rotl32(a, (i & 7U) + 1U);
        c = (c + (a ^ b)) * 2246822519U;
        a ^= rotl32(c, (i & 15U) + 1U);
        b += (a ^ 0x85ebca6bU) + (i * 3U);
        c ^= (b >> ((i & 3U) + 1U));
    }

    return a ^ rotl32(b, 11U) ^ rotl32(c, 19U);
}

static int tiny_eq(const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void emit_phase_line(const phase_result_t *r)
{
    char decbuf[16];
    char hexbuf[9];

    tiny_write("PHASE ");
    tiny_write(r->name);
    tiny_write(" ITERS=");
    u32_dec(r->iters, decbuf);
    tiny_write(decbuf);
    tiny_write(" HASH=");
    u32_hex(r->hash, hexbuf);
    tiny_writeln(hexbuf);
}

static uint32_t expected_for(const char *mode)
{
    /* Locked from first canonical in-guest run on 2026-04-28 ET. */
    if (tiny_eq(mode, "SMOKE")) return 0x747ae302U;
    if (tiny_eq(mode, "FULL")) return 0x9113ac58U;
    return 0x487a9954U; /* STRESS */
}

static uint32_t run_mode(const char *mode)
{
    phase_result_t p1;
    phase_result_t p2;
    phase_result_t p3;
    uint32_t total;
    uint32_t expected;
    char hexbuf[9];

    if (tiny_eq(mode, "SMOKE")) {
        p1.name = "P1"; p1.iters = ITERS_SMOKE;  p1.hash = run_phase(0x1001U, p1.iters);
        p2.name = "P2"; p2.iters = ITERS_SMOKE;  p2.hash = run_phase(0x2002U, p2.iters);
        p3.name = "P3"; p3.iters = ITERS_SMOKE;  p3.hash = run_phase(0x3003U, p3.iters);
    } else if (tiny_eq(mode, "FULL")) {
        p1.name = "P1"; p1.iters = ITERS_FULL;   p1.hash = run_phase(0x1001U, p1.iters);
        p2.name = "P2"; p2.iters = ITERS_FULL;   p2.hash = run_phase(0x2002U, p2.iters);
        p3.name = "P3"; p3.iters = ITERS_FULL;   p3.hash = run_phase(0x3003U, p3.iters);
    } else {
        p1.name = "P1"; p1.iters = ITERS_STRESS; p1.hash = run_phase(0x1001U, p1.iters);
        p2.name = "P2"; p2.iters = ITERS_STRESS; p2.hash = run_phase(0x2002U, p2.iters);
        p3.name = "P3"; p3.iters = ITERS_STRESS; p3.hash = run_phase(0x3003U, p3.iters);
        mode = "STRESS";
    }

    tiny_write("BEGIN MODE=");
    tiny_writeln(mode);

    emit_phase_line(&p1);
    emit_phase_line(&p2);
    emit_phase_line(&p3);

    total = p1.hash ^ rotl32(p2.hash, 7U) ^ rotl32(p3.hash, 13U);

    tiny_write("TOTAL MODE=");
    tiny_write(mode);
    tiny_write(" HASH=");
    u32_hex(total, hexbuf);
    tiny_writeln(hexbuf);

    expected = expected_for(mode);
    if (total == expected) {
        tiny_write("RESULT MODE=");
        tiny_write(mode);
        tiny_writeln(" PASS");
        tiny_writeln("DONE");
        return 0;
    }

    tiny_write("RESULT MODE=");
    tiny_write(mode);
    tiny_writeln(" FAIL");
    tiny_write("EXPECT=");
    u32_hex(expected, hexbuf);
    tiny_writeln(hexbuf);
    tiny_writeln("DONE");
    return 2;
}

static const char *next_arg(const char *cmd)
{
    const char *p = cmd;
    while (*p == ' ' || *p == '\t') p++;

    if (*p == '"') {
        p++;
        while (*p && *p != '"') p++;
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') p++;
    }

    while (*p == ' ' || *p == '\t') p++;
    return p;
}

void _start(void)
{
    const char *cmd = GetCommandLineA();
    const char *arg = next_arg(cmd);
    const char *mode = "FULL";
    int code;

    if (tiny_eq(arg, "SMOKE") || tiny_eq(arg, "FULL") || tiny_eq(arg, "STRESS")) {
        mode = arg;
    }

    tiny_writeln("THROUGHPUTCOV v1");
    code = (int) run_mode(mode);
    ExitProcess((UINT) code);
}
