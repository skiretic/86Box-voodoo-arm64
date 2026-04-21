#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMM_STORE_ITERS_DEFAULT 600000UL
#define BRANCH_ITERS_DEFAULT    2200000UL
#define MMX_ITERS_DEFAULT       800000UL
#define SMC_ITERS_DEFAULT       120000UL

typedef uint32_t (__cdecl *smc_fn_t)(void);

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
phase_imm_store(unsigned long iters)
{
    unsigned long i;
    uint32_t seed = 0x1234abcdU;
    uint32_t chk  = 0x9e3779b9U;
    enum {
        ARR_WORDS = 32768,
        ARR_MASK  = ARR_WORDS - 1
    };
    uint32_t *arr = (uint32_t *) calloc(ARR_WORDS, sizeof(uint32_t));

    if (!arr)
        return 0U;

    for (i = 0; i < iters; i++) {
        uint32_t idx = (seed >> 7) & ARR_MASK;
        uint32_t val = seed ^ 0xa5a5a5a5U ^ ((uint32_t) i * 2654435761U);

        arr[idx] = val;
        chk ^= rotl32(arr[(idx + 37U) & ARR_MASK] + val + (uint32_t) i, (uint32_t) (i & 31U));
        seed = lcg32(seed);
    }

    for (i = 0; i < ARR_WORDS; i++)
        chk ^= rotl32(arr[i] + (uint32_t) i, (uint32_t) (i & 31U));

    free(arr);
    return chk;
}

static uint32_t
phase_branch_helper(unsigned long iters)
{
    unsigned long i;
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
                    chk += rotl32(t, (uint32_t) (i & 31U));
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

        chk ^= rotl32(x + y + t + state, (uint32_t) ((i ^ state) & 31U));
    }

    return chk ^ x ^ y ^ state;
}

static uint32_t
phase_mmx_touch(unsigned long iters)
{
#if defined(__i386__)
    unsigned long i;
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

        chk ^= rotl32(out + (uint32_t) i, (uint32_t) (i & 31U));
        a = lcg32(a ^ out);
        b = lcg32(b + 0x9e3779b9U);
    }

    __asm__ volatile("emms");
    return chk;
#else
    unsigned long i;
    uint32_t x   = 0x55aa55aaU;
    uint32_t chk = 0x0badc0deU;

    for (i = 0; i < iters; i++) {
        x = lcg32(x);
        chk ^= rotl32(x, (uint32_t) (i & 31U));
    }

    return chk ^ 0x4d4d5821U;
#endif
}

static uint32_t
phase_smc_touch(unsigned long iters)
{
    unsigned long i;
    uint32_t chk = 0x11223344U;
    uint8_t *buf = (uint8_t *) VirtualAlloc(NULL, 16U, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!buf)
        return 0U;

    buf[0] = 0xB8; /* mov eax, imm32 */
    *(uint32_t *) &buf[1] = 0U;
    buf[5] = 0xC3; /* ret */

    for (i = 0; i < iters; i++) {
        uint32_t imm  = 0x600d0000U ^ ((uint32_t) i * 1103515245U);
        uint32_t rval;

        *(uint32_t *) &buf[1] = imm;
        FlushInstructionCache(GetCurrentProcess(), buf, 6U);

        rval = ((smc_fn_t) buf)();
        chk ^= rotl32(rval + (uint32_t) i, (uint32_t) (i & 31U));
    }

    VirtualFree(buf, 0U, MEM_RELEASE);
    return chk;
}

static int
run_phase(const char *name, uint32_t *out_checksum, uint32_t (*fn)(unsigned long), unsigned long iters)
{
    uint32_t checksum;

    printf("PHASE_START %s\n", name);
    fflush(stdout);

    checksum = fn(iters);
    if (checksum == 0U) {
        printf("MICROSTRESS_ERROR phase=%s checksum=00000000\n", name);
        fflush(stdout);
        return 1;
    }

    *out_checksum = checksum;
    printf("PHASE_END %s checksum=%08lx iters=%lu\n", name, (unsigned long) checksum, iters);
    fflush(stdout);
    return 0;
}

int
main(int argc, char **argv)
{
    int enable_smc = 0;
    int quick_mode = 0;
    int i;
    uint32_t total = 0x13572468U;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--smc"))
            enable_smc = 1;
        else if (!strcmp(argv[i], "--quick"))
            quick_mode = 1;
        else if (!strcmp(argv[i], "--help")) {
            printf("Usage: MICROSTR.EXE [--quick] [--smc]\n");
            printf("  --quick  Reduce iteration counts for a fast sanity run.\n");
            printf("  --smc    Enable self-modifying code phase.\n");
            return 0;
        } else {
            printf("MICROSTRESS_ERROR unknown_arg=%s\n", argv[i]);
            return 2;
        }
    }

    printf("MICROSTRESS_START\n");
    fflush(stdout);

    {
        unsigned long div = quick_mode ? 8UL : 1UL;
        uint32_t chk = 0U;

        if (run_phase("imm_store", &chk, phase_imm_store, IMM_STORE_ITERS_DEFAULT / div))
            return 3;
        total ^= rotl32(chk, 1U);

        if (run_phase("branch_helper", &chk, phase_branch_helper, BRANCH_ITERS_DEFAULT / div))
            return 4;
        total ^= rotl32(chk, 7U);

        if (run_phase("mmx_touch", &chk, phase_mmx_touch, MMX_ITERS_DEFAULT / div))
            return 5;
        total ^= rotl32(chk, 13U);

        if (enable_smc) {
            if (run_phase("smc_touch", &chk, phase_smc_touch, SMC_ITERS_DEFAULT / div))
                return 6;
            total ^= rotl32(chk, 19U);
        } else {
            printf("PHASE_SKIP smc_touch reason=disabled\n");
            fflush(stdout);
        }
    }

    printf("MICROSTRESS_DONE total=%08lx\n", (unsigned long) total);
    fflush(stdout);
    return 0;
}
