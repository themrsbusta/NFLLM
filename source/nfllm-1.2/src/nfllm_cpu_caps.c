#include "nfllm_cpu_caps.h"
#include <dos.h>
#include <i86.h>
#include <string.h>

struct NfllmCpuidRegisters {
    uint32_t eax, ebx, ecx, edx;
};

static uint32_t read_eflags(void);
#pragma aux read_eflags = \
    "pushfd" \
    "pop eax" \
    __value [__eax] \
    __modify [__eax]

static void write_eflags(uint32_t flags);
#pragma aux write_eflags = \
    "push eax" \
    "popfd" \
    __parm [__eax] \
    __modify [__eax]

static void cpuid_leaf(uint32_t leaf, struct NfllmCpuidRegisters *out);
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

static int cpuid_available(void)
{
    const uint32_t id_bit = 0x00200000UL;
    uint32_t original = read_eflags();
    uint32_t observed;
    write_eflags(original ^ id_bit);
    observed = read_eflags();
    write_eflags(original);
    return ((original ^ observed) & id_bit) != 0U;
}

static uint32_t legacy_x87_from_bios(void)
{
    union REGS inregs, outregs;
    memset(&inregs, 0, sizeof(inregs));
    memset(&outregs, 0, sizeof(outregs));
    int386(0x11, &inregs, &outregs);
    return (outregs.w.ax & 0x0002U) ? NFLLM_CAP_X87 : 0U;
}

uint32_t NFLLM_CALL nfllm_detect_cpu_capabilities(void)
{
    struct NfllmCpuidRegisters regs;
    uint32_t capabilities = NFLLM_CAP_386;
    uint32_t max_basic, max_extended;

    if (!cpuid_available()) return capabilities | legacy_x87_from_bios();

    capabilities |= NFLLM_CAP_CPUID;
    cpuid_leaf(0U, &regs);
    max_basic = regs.eax;
    if (max_basic >= 1U) {
        cpuid_leaf(1U, &regs);
        if (regs.edx & (1UL << 0)) capabilities |= NFLLM_CAP_X87;
        if (regs.edx & (1UL << 23)) capabilities |= NFLLM_CAP_MMX;
        if (regs.edx & (1UL << 25)) capabilities |= NFLLM_CAP_SSE;
        if (regs.edx & (1UL << 26)) capabilities |= NFLLM_CAP_SSE2;
        if (regs.ecx & (1UL << 0)) capabilities |= NFLLM_CAP_SSE3;
        if (regs.ecx & (1UL << 9)) capabilities |= NFLLM_CAP_SSSE3;
        if (regs.ecx & (1UL << 19)) capabilities |= NFLLM_CAP_SSE41;
        if (regs.ecx & (1UL << 20)) capabilities |= NFLLM_CAP_SSE42;
    }

    cpuid_leaf(0x80000000UL, &regs);
    max_extended = regs.eax;
    if (max_extended >= 0x80000001UL) {
        cpuid_leaf(0x80000001UL, &regs);
        if (regs.edx & (1UL << 22)) capabilities |= NFLLM_CAP_AMD_MMX_EXT;
        if (regs.edx & (1UL << 30)) capabilities |= NFLLM_CAP_3DNOW_EXT;
        if (regs.edx & (1UL << 31)) capabilities |= NFLLM_CAP_3DNOW;
        if (regs.ecx & (1UL << 6)) capabilities |= NFLLM_CAP_SSE4A;
    }
    return capabilities;
}
