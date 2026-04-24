#include <windows.h>
#include <stdint.h>

/* CRT-free Win98 console tool with deterministic screen markers. */

#define STRESS_ITERS_DEFAULT 200000UL
#define STRESS_ITERS_QUICK    40000UL

typedef union u64v {
    uint64_t q;
    uint32_t u32[2];
    int32_t  s32[2];
    uint16_t u16[4];
    int16_t  s16[4];
    uint8_t  u8[8];
    float    f[2];
} u64v_t;

typedef enum op_kind {
    OPK_FLOAT_BIN,
    OPK_FLOAT_CMP_EQ,
    OPK_FLOAT_CMP_GE,
    OPK_FLOAT_CMP_GT,
    OPK_FLOAT_ACC,
    OPK_FLOAT_NACC,
    OPK_FLOAT_PNACC,
    OPK_FLOAT_MAX,
    OPK_FLOAT_MIN,
    OPK_FLOAT_MUL,
    OPK_FLOAT_SUB,
    OPK_FLOAT_SUBR,
    OPK_FLOAT_SWAPD,
    OPK_CONV_PI2FD,
    OPK_CONV_PF2ID,
    OPK_CONV_PI2FW,
    OPK_CONV_PF2IW,
    OPK_INT_PMULHRW,
    OPK_INT_PAVGUSB,
    OPK_RCP,
    OPK_RSQRT,
    OPK_RCPIT1_COPY,
    OPK_RCPIT2_COPY,
    OPK_RSQIT1_NOP
} op_kind_t;

typedef struct op_case {
    const char *name;
    uint8_t opcode;
    op_kind_t kind;
    int requires_3dnowext;
} op_case_t;

static const op_case_t g_ops[] = {
    {"PI2FD",    0x0d, OPK_CONV_PI2FD,   0},
    {"PF2ID",    0x1d, OPK_CONV_PF2ID,   0},
    {"PI2FW",    0x0c, OPK_CONV_PI2FW,   1},
    {"PF2IW",    0x1c, OPK_CONV_PF2IW,   1},
    {"PFNACC",   0x8a, OPK_FLOAT_NACC,   1},
    {"PFPNACC",  0x8e, OPK_FLOAT_PNACC,  1},
    {"PFCMPGE",  0x90, OPK_FLOAT_CMP_GE, 0},
    {"PFMIN",    0x94, OPK_FLOAT_MIN,    0},
    {"PFRCP",    0x96, OPK_RCP,          0},
    {"PFRSQRT",  0x97, OPK_RSQRT,        0},
    {"PFSUB",    0x9a, OPK_FLOAT_SUB,    0},
    {"PFADD",    0x9e, OPK_FLOAT_BIN,    0},
    {"PFCMPGT",  0xa0, OPK_FLOAT_CMP_GT, 0},
    {"PFMAX",    0xa4, OPK_FLOAT_MAX,    0},
    {"PFRCPIT1", 0xa6, OPK_RCPIT1_COPY,  0},
    {"PFRSQIT1", 0xa7, OPK_RSQIT1_NOP,   0},
    {"PFSUBR",   0xaa, OPK_FLOAT_SUBR,   0},
    {"PFACC",    0xae, OPK_FLOAT_ACC,    0},
    {"PFCMPEQ",  0xb0, OPK_FLOAT_CMP_EQ, 0},
    {"PFMUL",    0xb4, OPK_FLOAT_MUL,    0},
    {"PFRCPIT2", 0xb6, OPK_RCPIT2_COPY,  0},
    {"PMULHRW",  0xb7, OPK_INT_PMULHRW,  0},
    {"PSWAPD",   0xbb, OPK_FLOAT_SWAPD,  1},
    {"PAVGUSB",  0xbf, OPK_INT_PAVGUSB,  0},
};
#define OP_COUNT ((uint32_t)(sizeof(g_ops) / sizeof(g_ops[0])))

static size_t tiny_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static int tiny_contains(const char *s, const char *needle)
{
    size_t i, j, slen = tiny_strlen(s), nlen = tiny_strlen(needle);
    if (!nlen || slen < nlen) return 0;
    for (i = 0; i <= slen - nlen; i++) {
        for (j = 0; j < nlen && s[i + j] == needle[j]; j++) {}
        if (j == nlen) return 1;
    }
    return 0;
}

static void tiny_write(const char *s)
{
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD) tiny_strlen(s), &written, NULL);
}

static void tiny_writeln(const char *s) { tiny_write(s); tiny_write("\r\n"); }

static void hex8(uint32_t v, char out[9])
{
    static const char h[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 8; i++) out[i] = h[(v >> ((7 - i) * 4)) & 0xf];
    out[8] = 0;
}

static void u32dec(uint32_t v, char out[16])
{
    char t[16];
    int i = 0, j = 0;
    if (!v) { out[0] = '0'; out[1] = 0; return; }
    while (v && i < 15) { t[i++] = (char) ('0' + (v % 10U)); v /= 10U; }
    while (i) out[j++] = t[--i];
    out[j] = 0;
}

static uint32_t rotl32(uint32_t x, uint32_t r) { r &= 31U; return (x << r) | (x >> ((32U - r) & 31U)); }
static uint32_t lcg32(uint32_t x) { return x * 1664525U + 1013904223U; }

static int is_float_domain_op(op_kind_t k)
{
    return k == OPK_FLOAT_BIN || k == OPK_FLOAT_CMP_EQ || k == OPK_FLOAT_CMP_GE || k == OPK_FLOAT_CMP_GT ||
           k == OPK_FLOAT_ACC || k == OPK_FLOAT_NACC || k == OPK_FLOAT_PNACC || k == OPK_FLOAT_MAX ||
           k == OPK_FLOAT_MIN || k == OPK_FLOAT_MUL || k == OPK_FLOAT_SUB || k == OPK_FLOAT_SUBR ||
           k == OPK_FLOAT_SWAPD || k == OPK_CONV_PF2ID || k == OPK_CONV_PF2IW || k == OPK_RCP ||
           k == OPK_RSQRT || k == OPK_RCPIT1_COPY || k == OPK_RCPIT2_COPY || k == OPK_RSQIT1_NOP;
}

static void seed_finite_float_inputs(op_kind_t kind, uint32_t s0, uint32_t s1, u64v_t *dst, u64v_t *src)
{
    int32_t d0 = (int32_t) ((s0 & 0x7ffU) - 1024);
    int32_t d1 = (int32_t) (((s0 >> 11) & 0x7ffU) - 1024);
    int32_t sA = (int32_t) ((s1 & 0x7ffU) - 1024);
    int32_t sB = (int32_t) (((s1 >> 11) & 0x7ffU) - 1024);

    dst->f[0] = (float) d0 / 32.0f;
    dst->f[1] = (float) d1 / 32.0f;
    src->f[0] = (float) sA / 32.0f;
    src->f[1] = (float) sB / 32.0f;

    /* Keep reciprocal/sqrt domains legal and stable. */
    if (kind == OPK_RCP || kind == OPK_RSQRT) {
        float x = src->f[0];
        if (x < 0.25f) x = -x + 0.25f;
        src->f[0] = x;
        src->f[1] = x + 0.5f;
    }
}

static int cpuid_supported(void)
{
#if defined(__i386__)
    uint32_t before, after;
    __asm__ volatile(
        "pushfl\n\t"
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x00200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "popfl\n\t"
        : "=&r"(before), "=&r"(after) :: "cc");
    return ((before ^ after) & 0x00200000U) != 0;
#else
    return 0;
#endif
}

static void cpuid_leaf(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
#if defined(__i386__)
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
#else
    *a = *b = *c = *d = 0;
#endif
}

static void do_femms(void) { __asm__ volatile(".byte 0x0f,0x0e" ::: "memory"); }
static void do_prefetch(const void *p) { __asm__ volatile("movl %0, %%eax\n\t.byte 0x0f,0x0d,0x00" :: "r"(p) : "eax", "memory"); }

static void run_3dnow_rr(uint8_t op, const u64v_t *a, const u64v_t *b, u64v_t *out)
{
    switch (op) {
#define OPCASE(OPV) case OPV: __asm__ volatile( \
        "movq (%1), %%mm0\n\t" \
        "movq (%2), %%mm1\n\t" \
        ".byte 0x0f,0x0f,0xc1," #OPV "\n\t" \
        "movq %%mm0, (%0)\n\t" \
        : : "r"(out), "r"(a), "r"(b) : "memory", "mm0", "mm1"); break
        OPCASE(0x0d); OPCASE(0x1d); OPCASE(0x0c); OPCASE(0x1c);
        OPCASE(0x8a); OPCASE(0x8e); OPCASE(0x90); OPCASE(0x94);
        OPCASE(0x96); OPCASE(0x97); OPCASE(0x9a); OPCASE(0x9e);
        OPCASE(0xa0); OPCASE(0xa4); OPCASE(0xa6); OPCASE(0xa7);
        OPCASE(0xaa); OPCASE(0xae); OPCASE(0xb0); OPCASE(0xb4);
        OPCASE(0xb6); OPCASE(0xb7); OPCASE(0xbb); OPCASE(0xbf);
#undef OPCASE
    default: out->q = 0ULL; break;
    }
}

static void run_3dnow_rm(uint8_t op, const u64v_t *a, const u64v_t *memsrc, u64v_t *out)
{
    switch (op) {
#define OPCASE(OPV) case OPV: __asm__ volatile( \
        "movq (%1), %%mm0\n\t" \
        "movl %2, %%eax\n\t" \
        ".byte 0x0f,0x0f,0x00," #OPV "\n\t" \
        "movq %%mm0, (%0)\n\t" \
        : : "r"(out), "r"(a), "r"(memsrc) : "eax", "memory", "mm0"); break
        OPCASE(0x0d); OPCASE(0x1d); OPCASE(0x0c); OPCASE(0x1c);
        OPCASE(0x8a); OPCASE(0x8e); OPCASE(0x90); OPCASE(0x94);
        OPCASE(0x96); OPCASE(0x97); OPCASE(0x9a); OPCASE(0x9e);
        OPCASE(0xa0); OPCASE(0xa4); OPCASE(0xa6); OPCASE(0xa7);
        OPCASE(0xaa); OPCASE(0xae); OPCASE(0xb0); OPCASE(0xb4);
        OPCASE(0xb6); OPCASE(0xb7); OPCASE(0xbb); OPCASE(0xbf);
#undef OPCASE
    default: out->q = 0ULL; break;
    }
}

static u64v_t ref_eval(op_kind_t k, u64v_t dst, u64v_t src)
{
    u64v_t r = dst;
    switch (k) {
    case OPK_FLOAT_BIN:   r.f[0] = dst.f[0] + src.f[0]; r.f[1] = dst.f[1] + src.f[1]; break;
    case OPK_FLOAT_CMP_EQ:r.u32[0] = (dst.f[0] == src.f[0]) ? 0xffffffffU : 0U; r.u32[1] = (dst.f[1] == src.f[1]) ? 0xffffffffU : 0U; break;
    case OPK_FLOAT_CMP_GE:r.u32[0] = (dst.f[0] >= src.f[0]) ? 0xffffffffU : 0U; r.u32[1] = (dst.f[1] >= src.f[1]) ? 0xffffffffU : 0U; break;
    case OPK_FLOAT_CMP_GT:r.u32[0] = (dst.f[0] >  src.f[0]) ? 0xffffffffU : 0U; r.u32[1] = (dst.f[1] >  src.f[1]) ? 0xffffffffU : 0U; break;
    case OPK_FLOAT_ACC:   r.f[0] = dst.f[0] + dst.f[1]; r.f[1] = src.f[0] + src.f[1]; break;
    case OPK_FLOAT_NACC:  r.f[0] = dst.f[0] - dst.f[1]; r.f[1] = src.f[0] - src.f[1]; break;
    case OPK_FLOAT_PNACC: r.f[0] = dst.f[0] - dst.f[1]; r.f[1] = src.f[0] + src.f[1]; break;
    case OPK_FLOAT_MAX:   if (src.f[0] > r.f[0]) r.f[0] = src.f[0]; if (src.f[1] > r.f[1]) r.f[1] = src.f[1]; break;
    case OPK_FLOAT_MIN:   if (src.f[0] < r.f[0]) r.f[0] = src.f[0]; if (src.f[1] < r.f[1]) r.f[1] = src.f[1]; break;
    case OPK_FLOAT_MUL:   r.f[0] = dst.f[0] * src.f[0]; r.f[1] = dst.f[1] * src.f[1]; break;
    case OPK_FLOAT_SUB:   r.f[0] = dst.f[0] - src.f[0]; r.f[1] = dst.f[1] - src.f[1]; break;
    case OPK_FLOAT_SUBR:  r.f[0] = src.f[0] - dst.f[0]; r.f[1] = src.f[1] - dst.f[1]; break;
    case OPK_FLOAT_SWAPD: r.f[0] = src.f[1]; r.f[1] = src.f[0]; break;
    case OPK_CONV_PI2FD:  r.f[0] = (float) src.s32[0]; r.f[1] = (float) src.s32[1]; break;
    case OPK_CONV_PF2ID:  r.s32[0] = (int32_t) src.f[0]; r.s32[1] = (int32_t) src.f[1]; break;
    case OPK_CONV_PI2FW:  r.f[0] = (float) src.s16[0]; r.f[1] = (float) src.s16[1]; break;
    case OPK_CONV_PF2IW:
        r.s16[0] = (int16_t) ((int32_t) src.f[0]);
        r.s16[1] = (int16_t) ((int32_t) src.f[1]);
        break;
    case OPK_INT_PMULHRW:
        r.s16[0] = (int16_t) ((((int32_t) dst.s16[0] * (int32_t) src.s16[0]) + 0x8000) >> 16);
        r.s16[1] = (int16_t) ((((int32_t) dst.s16[1] * (int32_t) src.s16[1]) + 0x8000) >> 16);
        r.s16[2] = (int16_t) ((((int32_t) dst.s16[2] * (int32_t) src.s16[2]) + 0x8000) >> 16);
        r.s16[3] = (int16_t) ((((int32_t) dst.s16[3] * (int32_t) src.s16[3]) + 0x8000) >> 16);
        break;
    case OPK_INT_PAVGUSB:
        r.u8[0] = (uint8_t) ((dst.u8[0] + src.u8[0] + 1U) >> 1); r.u8[1] = (uint8_t) ((dst.u8[1] + src.u8[1] + 1U) >> 1);
        r.u8[2] = (uint8_t) ((dst.u8[2] + src.u8[2] + 1U) >> 1); r.u8[3] = (uint8_t) ((dst.u8[3] + src.u8[3] + 1U) >> 1);
        r.u8[4] = (uint8_t) ((dst.u8[4] + src.u8[4] + 1U) >> 1); r.u8[5] = (uint8_t) ((dst.u8[5] + src.u8[5] + 1U) >> 1);
        r.u8[6] = (uint8_t) ((dst.u8[6] + src.u8[6] + 1U) >> 1); r.u8[7] = (uint8_t) ((dst.u8[7] + src.u8[7] + 1U) >> 1);
        break;
    case OPK_RCP:
        r.f[0] = 1.0f / src.f[0];
        r.f[1] = r.f[0];
        break;
    case OPK_RSQRT: {
        double x = (double) src.f[0];
        double y = 1.0;
        int i;
        for (i = 0; i < 10; i++) y = 0.5 * (y + (1.0 / (x * y)));
        r.f[0] = (float) y;
        r.f[1] = r.f[0];
        break;
    }
    case OPK_RCPIT1_COPY: r = src; break;
    case OPK_RCPIT2_COPY: r = src; break;
    case OPK_RSQIT1_NOP: break;
    }
    return r;
}

static int close_enough(float a, float b)
{
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= 0.0005f;
}

static int validate_one(const op_case_t *op, const u64v_t *a, const u64v_t *b, uint32_t iters, uint32_t *hash_io)
{
    uint32_t i, bad = 0;
    u64v_t rr, rm, exp_rr, exp_rm;
    uint32_t hash = *hash_io;

    for (i = 0; i < iters; i++) {
        u64v_t in0 = *a, in1 = *b;
        in0.u32[0] ^= lcg32(hash + i);
        in0.u32[1] ^= rotl32(hash + 0x9e3779b9U + i, (i + 3U) & 31U);
        in1.u32[0] ^= lcg32(hash ^ (i * 3U + 1U));
        in1.u32[1] ^= rotl32(hash ^ i, i & 31U);

        /* Use finite numeric vectors for float-domain ops to avoid NaN/Inf edge
           behavior differences from drowning signal in coverage validation. */
        if (is_float_domain_op(op->kind))
            seed_finite_float_inputs(op->kind, in0.u32[0] ^ in0.u32[1], in1.u32[0] ^ in1.u32[1], &in0, &in1);

        run_3dnow_rr(op->opcode, &in0, &in1, &rr);
        run_3dnow_rm(op->opcode, &in0, &in1, &rm);
        exp_rr = ref_eval(op->kind, in0, in1);
        exp_rm = ref_eval(op->kind, in0, in1);

        if (op->kind == OPK_RCP || op->kind == OPK_RSQRT) {
            if (!close_enough(rr.f[0], exp_rr.f[0]) || !close_enough(rm.f[0], exp_rm.f[0])) bad++;
            if (!close_enough(rr.f[1], rr.f[0]) || !close_enough(rm.f[1], rm.f[0])) bad++;
        } else {
            if (rr.q != exp_rr.q || rm.q != exp_rm.q) bad++;
        }
        hash ^= rr.u32[0] + rotl32(rr.u32[1], (i & 31U));
        hash ^= rm.u32[0] + rotl32(rm.u32[1], ((i + 7U) & 31U));
        hash = lcg32(hash ^ (uint32_t) op->opcode);
    }
    *hash_io = hash;
    return bad ? 0 : 1;
}

static void print_result(const char *name, const char *res, uint32_t h)
{
    char hx[9];
    hex8(h, hx);
    tiny_write("3DNOW_OP ");
    tiny_write(name);
    tiny_write(" ");
    tiny_write(res);
    tiny_write(" hash=");
    tiny_write(hx);
    tiny_write("\r\n");
}

__attribute__((noreturn)) void _start(void)
{
    uint32_t eax, ebx, ecx, edx;
    uint32_t maxext = 0, pass = 0, fail = 0, skip = 0, total_hash = 0x3d90c0deU;
    uint32_t misc_hash;
    uint32_t i, iters;
    int has_cpuid, has_3dnow = 0, has_3dnowext = 0;
    const char *cmd = GetCommandLineA();
    char nbuf[16], hbuf[9];
    u64v_t a = {0}, b = {0};
    uint8_t prefetch_buf[1024];
    char prefetch_status = 'F', femms_status = 'F';
    int verbose = tiny_contains(cmd, "--verbose");
    uint32_t op_hashes[OP_COUNT];
    uint8_t op_status[OP_COUNT];

    tiny_writeln("3DNOWCOV_START");
    tiny_writeln("3DNOWCOV_PREP_START");
    has_cpuid = cpuid_supported();
    if (has_cpuid) {
        cpuid_leaf(0x80000000U, &maxext, &ebx, &ecx, &edx);
        if (maxext >= 0x80000001U) {
            cpuid_leaf(0x80000001U, &eax, &ebx, &ecx, &edx);
            has_3dnow = (edx & (1U << 31)) ? 1 : 0;
            has_3dnowext = (edx & (1U << 30)) ? 1 : 0;
        }
    }
    iters = tiny_contains(cmd, "--quick") ? STRESS_ITERS_QUICK : STRESS_ITERS_DEFAULT;
    tiny_writeln("3DNOWCOV_PREP_END");

    if (!has_cpuid || !has_3dnow) {
        tiny_writeln("3DNOWCOV_ERROR NO_3DNOW");
        ExitProcess(2);
    }

    a.u32[0] = 0x3f800000U; a.u32[1] = 0x40000000U;
    b.u32[0] = 0x40400000U; b.u32[1] = 0xbf800000U;

    tiny_writeln("3DNOWCOV_LOOP_START 1");
    for (i = 0; i < OP_COUNT; i++) {
        uint32_t op_hash = total_hash ^ (uint32_t) g_ops[i].opcode;
        op_hashes[i] = op_hash;
        op_status[i] = (uint8_t) '?';
        if (g_ops[i].requires_3dnowext && !has_3dnowext) {
            op_status[i] = (uint8_t) 'S';
            if (verbose)
                print_result(g_ops[i].name, "SKIP_NO_3DNOWEXT", op_hash);
            skip++;
            total_hash = lcg32(total_hash ^ op_hash);
            continue;
        }
        if (validate_one(&g_ops[i], &a, &b, iters, &op_hash)) {
            op_status[i] = (uint8_t) 'P';
            if (verbose)
                print_result(g_ops[i].name, "PASS", op_hash);
            pass++;
        } else {
            op_status[i] = (uint8_t) 'F';
            print_result(g_ops[i].name, "FAIL", op_hash);
            fail++;
        }
        op_hashes[i] = op_hash;
        total_hash ^= rotl32(op_hash, i & 31U);
        total_hash = lcg32(total_hash);
    }
    tiny_writeln("3DNOWCOV_LOOP_END 1");

    tiny_writeln("3DNOWCOV_LOOP_START 2");
    for (i = 0; i < iters; i++) {
        uint32_t off = (i * 64U) & 1023U;
        uint8_t *base = prefetch_buf;
        do_prefetch(base + off);
        do_prefetch(base + ((off + 32U) & 1023U));
        total_hash ^= rotl32((uint32_t) off + i, i & 31U);
    }
    prefetch_status = 'P';
    do_femms();
    femms_status = 'P';
    tiny_writeln("3DNOWCOV_LOOP_END 2");

    misc_hash = lcg32(total_hash ^ 0x6d697363U ^ ((uint32_t) prefetch_status << 8) ^ (uint32_t) femms_status);
    tiny_write("3DNOWCOV_MISC prefetch=");
    tiny_write((prefetch_status == 'P') ? "P" : "F");
    tiny_write(" femms=");
    tiny_write((femms_status == 'P') ? "P" : "F");
    tiny_write(" hash=");
    hex8(misc_hash, hbuf); tiny_write(hbuf);
    tiny_write("\r\n");

    tiny_write("3DNOWCOV_COUNTS pass=");
    u32dec(pass, nbuf); tiny_write(nbuf);
    tiny_write(" fail=");
    u32dec(fail, nbuf); tiny_write(nbuf);
    tiny_write(" skip=");
    u32dec(skip, nbuf); tiny_write(nbuf);
    tiny_write(" iters=");
    u32dec(iters, nbuf); tiny_write(nbuf);
    tiny_write("\r\n");

    tiny_write("3DNOWCOV_TOTAL hash=");
    hex8(total_hash, hbuf); tiny_write(hbuf);
    tiny_write("\r\n");

    if (!verbose) {
        uint32_t base = 0;
        uint32_t row = 1;
        while (base < OP_COUNT) {
            uint32_t k;
            tiny_write("3DNOWCOV_STAT_ROW");
            u32dec(row, nbuf);
            tiny_write(nbuf);
            tiny_write(" ");
            for (k = 0; k < 5 && (base + k) < OP_COUNT; k++) {
                char code[3];
                static const char hx[] = "0123456789abcdef";
                uint8_t opc = g_ops[base + k].opcode;
                code[0] = hx[(opc >> 4) & 0xf];
                code[1] = hx[opc & 0xf];
                code[2] = 0;
                tiny_write(code);
                tiny_write((op_status[base + k] == 'P') ? "P " :
                           (op_status[base + k] == 'F') ? "F " : "S ");
            }
            tiny_write("\r\n");
            tiny_write("3DNOWCOV_HASH_ROW");
            tiny_write(nbuf);
            tiny_write(" ");
            for (k = 0; k < 5 && (base + k) < OP_COUNT; k++) {
                char hh[9];
                char code[3];
                static const char hx[] = "0123456789abcdef";
                uint8_t opc = g_ops[base + k].opcode;
                code[0] = hx[(opc >> 4) & 0xf];
                code[1] = hx[opc & 0xf];
                code[2] = 0;
                hex8(op_hashes[base + k], hh);
                tiny_write(code);
                tiny_write("=");
                tiny_write(hh);
                tiny_write(" ");
            }
            tiny_write("\r\n");
            base += 5;
            row++;
        }
    }

    if (fail) {
        tiny_writeln("3DNOWCOV_ERROR VALIDATION_FAIL");
        ExitProcess(1);
    }

    tiny_writeln("3DNOWCOV_DONE");
    ExitProcess(0);
}
