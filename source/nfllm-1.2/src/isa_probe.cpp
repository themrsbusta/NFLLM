/* Run only in the named QEMU proxy labs; CPUID is executed unconditionally. */
#include <stdio.h>
#include <string.h>

typedef unsigned long u32;
typedef float f32;

struct cpuid_regs {
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
};

static void cpuid_leaf(u32 leaf, struct cpuid_regs *out);
#pragma aux cpuid_leaf = \
    "push ebx" \
    "db 0fh, 0a2h" \
    "mov [edi], eax" \
    "mov [edi+4], ebx" \
    "mov [edi+8], ecx" \
    "mov [edi+12], edx" \
    "pop ebx" \
    __parm [__eax] [__edi] \
    __modify [__eax __ebx __ecx __edx]

/* This isolated probe runs only in protected-mode QEMU labs, never on Am486. */
static u32 read_cr4(void);
#pragma aux read_cr4 = \
    "mov eax, cr4" \
    __value [__eax] \
    __modify [__eax]

static u32 mmx_paddd(u32 left, u32 right);
#pragma aux mmx_paddd = \
    "movd mm0, eax" \
    "movd mm1, edx" \
    "paddd mm0, mm1" \
    "movd eax, mm0" \
    "emms" \
    __parm [__eax] [__edx] \
    __value [__eax] \
    __modify [__edx]

static u32 mmx_pavgusb(u32 left, u32 right);
#pragma aux mmx_pavgusb = \
    "movd mm0, eax" \
    "movd mm1, edx" \
    "pavgusb mm0, mm1" \
    "movd eax, mm0" \
    "emms" \
    __parm [__eax] [__edx] \
    __value [__eax] \
    __modify [__edx]

static u32 mmx_pavgb(u32 left, u32 right);
#pragma aux mmx_pavgb = \
    "movd mm0, eax" \
    "movd mm1, edx" \
    "pavgb mm0, mm1" \
    "movd eax, mm0" \
    "emms" \
    __parm [__eax] [__edx] \
    __value [__eax] \
    __modify [__edx]

static void dnow_pfadd(const f32 *left, const f32 *right, f32 *output);
#pragma aux dnow_pfadd = \
    "movq mm0, [eax]" \
    "movq mm1, [edx]" \
    "pfadd mm0, mm1" \
    "movq [ecx], mm0" \
    "emms" \
    __parm [__eax] [__edx] [__ecx] \
    __modify [__eax __edx __ecx]

static void dnow_pswapd(const u32 *input, u32 *output);
#pragma aux dnow_pswapd = \
    "movq mm0, [eax]" \
    "pswapd mm0, mm0" \
    "movq [edx], mm0" \
    "emms" \
    __parm [__eax] [__edx] \
    __modify [__eax __edx]

static void sse_addps(const f32 *left, const f32 *right, f32 *output);
#pragma aux sse_addps = \
    "movups xmm0, [eax]" \
    "addps xmm0, [edx]" \
    "movups [ecx], xmm0" \
    __parm [__eax] [__edx] [__ecx] \
    __modify [__eax __edx __ecx]

static void sse2_paddd(const u32 *left, const u32 *right, u32 *output);
#pragma aux sse2_paddd = \
    "movdqu xmm0, [eax]" \
    "paddd xmm0, [edx]" \
    "movdqu [ecx], xmm0" \
    __parm [__eax] [__edx] [__ecx] \
    __modify [__eax __edx __ecx]

static void sse3_haddps(const f32 *input, f32 *output);
#pragma aux sse3_haddps = \
    "movups xmm0, [eax]" \
    "haddps xmm0, xmm0" \
    "movups [edx], xmm0" \
    __parm [__eax] [__edx] \
    __modify [__eax __edx]

static void ssse3_pshufb(const unsigned char *input,
                         const unsigned char *mask,
                         unsigned char *output);
#pragma aux ssse3_pshufb = \
    "movdqu xmm0, [eax]" \
    "movdqu xmm1, [edx]" \
    "db 66h, 0fh, 38h, 00h, 0c1h" \
    "movdqu [ecx], xmm0" \
    __parm [__eax] [__edx] [__ecx] \
    __modify [__eax __edx __ecx]

static int fail(const char *name)
{
    printf("%s: FAIL\n", name);
    fflush(stdout);
    return 1;
}

static int pass(const char *name)
{
    printf("%s: PASS\n", name);
    fflush(stdout);
    return 0;
}

static void trying(const char *name)
{
    printf("TRY: %s\n", name);
    fflush(stdout);
}

static int run_mmx(void)
{
    trying("MMX paddd");
    u32 result = mmx_paddd(0x00020001UL, 0x00040003UL);
    return result == 0x00060004UL ? pass("MMX paddd") : fail("MMX paddd");
}

static int run_ammx(void)
{
    trying("AMMX pavgb");
    u32 result = mmx_pavgb(0x04030201UL, 0x08070605UL);
    return result == 0x06050403UL ? pass("AMMX pavgb") :
                                   fail("AMMX pavgb");
}

static int run_3dnow(void)
{
    const f32 left[2] = { 1.25f, -2.5f };
    const f32 right[2] = { 0.5f, 3.25f };
    f32 output[2] = { 0.0f, 0.0f };
    const u32 words[2] = { 0x11223344UL, 0xaabbccddUL };
    u32 swapped[2] = { 0U, 0U };
    u32 average;
    int failures = 0;

    trying("3DNow! pavgusb");
    average = mmx_pavgusb(0x04030201UL, 0x08070605UL);
    if (average != 0x06050403UL)
        failures += fail("3DNow! pavgusb");
    else
        failures += pass("3DNow! pavgusb");

    trying("3DNow! pfadd");
    dnow_pfadd(left, right, output);
    if (output[0] != 1.75f || output[1] != 0.75f)
        failures += fail("3DNow! pfadd");
    else
        failures += pass("3DNow! pfadd");

    trying("3DNow! extension pswapd");
    dnow_pswapd(words, swapped);
    if (swapped[0] != words[1] || swapped[1] != words[0])
        failures += fail("3DNow! extension pswapd");
    else
        failures += pass("3DNow! extension pswapd");
    return failures;
}

static int run_sse(void)
{
    const f32 left[4] = { 1.0f, -2.0f, 3.5f, 4.0f };
    const f32 right[4] = { 0.5f, 4.0f, -1.5f, 2.0f };
    f32 output[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    unsigned i;

    trying("SSE addps");
    sse_addps(left, right, output);
    for (i = 0U; i < 4U; ++i) {
        if (output[i] != left[i] + right[i])
            return fail("SSE addps");
    }
    return pass("SSE addps");
}

static int run_sse2(void)
{
    const u32 left[4] = { 1U, 20U, 300U, 4000U };
    const u32 right[4] = { 4U, 5U, 6U, 7U };
    const u32 expected[4] = { 5U, 25U, 306U, 4007U };
    u32 output[4] = { 0U, 0U, 0U, 0U };
    unsigned i;

    trying("SSE2 paddd");
    sse2_paddd(left, right, output);
    for (i = 0U; i < 4U; ++i) {
        if (output[i] != expected[i])
            return fail("SSE2 paddd");
    }
    return pass("SSE2 paddd");
}

static int run_sse3(void)
{
    const f32 input[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    const f32 expected[4] = { 3.0f, 7.0f, 3.0f, 7.0f };
    f32 output[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    unsigned i;

    trying("SSE3 haddps");
    sse3_haddps(input, output);
    for (i = 0U; i < 4U; ++i) {
        if (output[i] != expected[i])
            return fail("SSE3 haddps");
    }
    return pass("SSE3 haddps");
}

static int run_ssse3(void)
{
    unsigned char input[16];
    unsigned char mask[16];
    unsigned char output[16];
    int i;

    for (i = 0; i < 16; ++i) {
        input[i] = (unsigned char)i;
        mask[i] = (unsigned char)(15 - i);
        output[i] = 0U;
    }
    trying("SSSE3 pshufb");
    ssse3_pshufb(input, mask, output);
    for (i = 0; i < 16; ++i) {
        if (output[i] != (unsigned char)(15 - i))
            return fail("SSSE3 pshufb");
    }
    return pass("SSSE3 pshufb");
}

int main(int argc, char **argv)
{
    struct cpuid_regs standard;
    struct cpuid_regs extended;
    char vendor[13];
    u32 max_standard, max_extended = 0U;
    u32 standard_edx = 0U, standard_ecx = 0U, extended_edx = 0U;
    u32 cr4_value;
    int has_mmx, has_sse, has_sse2, has_sse3, has_ssse3;
    int has_3dnow, has_3dnowext, has_ammx, has_osfxsr;
    int required = 0, failures = 0;
    volatile double x87_value;

    if (argc < 2 || (strcmp(argv[1], "athlon-lx-proxy") != 0 &&
                     strcmp(argv[1], "atom-n270") != 0)) {
        printf("Usage: ISAPROBE.EXE athlon-lx-proxy|atom-n270\n");
        printf("Do not run this CPUID probe on the Am486 baseline.\n");
        return 2;
    }

    trying("CPUID leaf 0");
    cpuid_leaf(0U, &standard);
    max_standard = standard.eax;
    memcpy(vendor, &standard.ebx, 4);
    memcpy(vendor + 4, &standard.edx, 4);
    memcpy(vendor + 8, &standard.ecx, 4);
    vendor[12] = '\0';
    printf("ISA probe profile=%s vendor=%s max_standard=%lu\n",
           argv[1], vendor, max_standard);
    fflush(stdout);

    if (max_standard >= 1U) {
        trying("CPUID leaf 1");
        cpuid_leaf(1U, &standard);
        standard_edx = standard.edx;
        standard_ecx = standard.ecx;
    }
    trying("CPUID extended maximum leaf");
    cpuid_leaf(0x80000000UL, &extended);
    max_extended = extended.eax;
    if (max_extended >= 0x80000001UL) {
        trying("CPUID extended features leaf");
        cpuid_leaf(0x80000001UL, &extended);
        extended_edx = extended.edx;
    }

    has_mmx = (standard_edx & (1UL << 23)) != 0U;
    has_sse = (standard_edx & (1UL << 25)) != 0U;
    has_sse2 = (standard_edx & (1UL << 26)) != 0U;
    has_sse3 = (standard_ecx & (1UL << 0)) != 0U;
    has_ssse3 = (standard_ecx & (1UL << 9)) != 0U;
    has_ammx = (extended_edx & (1UL << 22)) != 0U;
    has_3dnowext = (extended_edx & (1UL << 30)) != 0U;
    has_3dnow = (extended_edx & (1UL << 31)) != 0U;
    cr4_value = read_cr4();
    has_osfxsr = (cr4_value & (1UL << 9)) != 0U;

    printf("CPUID flags: MMX=%d AMMX=%d 3DNow=%d 3DNowExt=%d SSE=%d SSE2=%d SSE3=%d SSSE3=%d\n",
           has_mmx, has_ammx, has_3dnow, has_3dnowext,
           has_sse, has_sse2, has_sse3, has_ssse3);
    printf("CR4=%08lx OSFXSR=%d\n", cr4_value, has_osfxsr);
    fflush(stdout);

    if (strcmp(argv[1], "athlon-lx-proxy") == 0) {
        required = 1;
        if (!has_mmx || !has_ammx || !has_3dnow || !has_3dnowext)
            failures += fail("Required Athlon proxy CPUID feature set");
    } else {
        required = 2;
        if (!has_mmx || !has_sse || !has_sse2)
            failures += fail("Required Atom N270 CPUID feature set");
    }

    if (has_mmx) failures += run_mmx();
    else printf("MMX: SKIP (CPUID flag clear)\n");
    if (has_ammx) failures += run_ammx();
    else printf("AMD MMX extensions: SKIP (CPUID flag clear)\n");
    if (has_3dnow) failures += run_3dnow();
    else printf("3DNow!: SKIP (CPUID flag clear)\n");
    if (!has_osfxsr) {
        if (has_sse) printf("SSE: SKIP (CR4.OSFXSR clear)\n");
        else printf("SSE: SKIP (CPUID flag clear)\n");
        if (has_sse2) printf("SSE2: SKIP (CR4.OSFXSR clear)\n");
        else printf("SSE2: SKIP (CPUID flag clear)\n");
        if (has_sse3) printf("SSE3: SKIP (CR4.OSFXSR clear)\n");
        else printf("SSE3: SKIP (CPUID flag clear)\n");
        if (has_ssse3) printf("SSSE3: SKIP (CR4.OSFXSR clear)\n");
        else printf("SSSE3: SKIP (CPUID flag clear)\n");
    } else {
        if (has_sse) failures += run_sse();
        else printf("SSE: SKIP (CPUID flag clear)\n");
        if (has_sse2) failures += run_sse2();
        else printf("SSE2: SKIP (CPUID flag clear)\n");
        if (has_sse3) failures += run_sse3();
        else printf("SSE3: SKIP (CPUID flag clear)\n");
        if (has_ssse3) failures += run_ssse3();
        else printf("SSSE3: SKIP (CPUID flag clear)\n");
    }

    trying("x87 after SIMD tests");
    x87_value = 1.25 + 2.75;
    if (x87_value != 4.0) failures += fail("x87 after SIMD tests");
    else failures += pass("x87 after SIMD tests");

    if (failures == 0 && required != 0) {
        printf("ISA probe result: PASS\n");
        return 0;
    }
    printf("ISA probe result: FAIL (%d failures)\n", failures);
    return 2;
}
