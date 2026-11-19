/*
 * NFLLM compatibility probe for the NF-DOS / AMD Am486DX4 target.
 * Build with Open Watcom as LE, then bind DOS/32A; run in the DOS acceptance VM.
 */

#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned long u32;

struct CpuidRegs {
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
};

struct DpmiMemInfo {
    u32 largest_block_avail;
    u32 max_unlocked_pages;
    u32 max_locked_pages;
    u32 linear_address_space_pages;
    u32 total_unlocked_pages;
    u32 free_physical_pages;
    u32 total_physical_pages;
    u32 free_linear_pages;
    u32 paging_file_pages;
    u32 reserved[3];
};

u32 read_eflags(void);
#pragma aux read_eflags = \
    "pushfd" \
    "pop eax" \
    __value [__eax] \
    __modify [__eax]

void write_eflags(u32 flags);
#pragma aux write_eflags = \
    "push eax" \
    "popfd" \
    __parm [__eax] \
    __modify [__eax]

void cpuid_leaf(u32 leaf, struct CpuidRegs *out);
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

static int cpuid_id_flag_is_writable(void)
{
    const u32 id_bit = 0x00200000UL;
    u32 original = read_eflags();
    u32 observed;

    write_eflags(original ^ id_bit);
    observed = read_eflags();
    write_eflags(original);
    return ((original ^ observed) & id_bit) != 0;
}

static void print_cpu(void)
{
    struct CpuidRegs regs;
    char vendor[13];
    u32 max_leaf;

    if (!cpuid_id_flag_is_writable()) {
        printf("CPUID probe: EFLAGS.ID nao mudou neste contexto do extender; CPUID nao executado; suporte nao confirmado.\n");
        return;
    }

    cpuid_leaf(0, &regs);
    max_leaf = regs.eax;
    memcpy(vendor + 0, &regs.ebx, 4);
    memcpy(vendor + 4, &regs.edx, 4);
    memcpy(vendor + 8, &regs.ecx, 4);
    vendor[12] = '\0';
    printf("CPU vendor: %s; CPUID max leaf: %lu\n", vendor, max_leaf);

    if (max_leaf >= 1) {
        cpuid_leaf(1, &regs);
        printf("CPU signature: family %lu, model %lu, stepping %lu\n",
               (regs.eax >> 8) & 0x0f,
               (regs.eax >> 4) & 0x0f,
               regs.eax & 0x0f);
        printf("CPU FPU flag: %s\n", (regs.edx & 1UL) ? "presente" : "ausente");
    }
}

static void print_dos_version(void)
{
    union REGS inregs;
    union REGS outregs;

    memset(&inregs, 0, sizeof(inregs));
    inregs.h.ah = 0x30;
    intdos(&inregs, &outregs);
    printf("DOS version: %u.%02u\n", outregs.h.al, outregs.h.ah);
}

static void print_dpmi_memory(void)
{
    union REGS regs;
    struct SREGS sregs;
    struct DpmiMemInfo info;

    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    memset(&info, 0xff, sizeof(info));
    regs.x.eax = 0x00000500UL;
    sregs.es = FP_SEG(&info);
    regs.x.edi = FP_OFF(&info);
    int386x(0x31, &regs, &regs, &sregs);

    if (regs.x.cflag) {
        printf("DPMI memory query: indisponivel (INT 31h, AX=0500h).\n");
        return;
    }
    printf("DPMI AX=0500 largest free block: %lu bytes\n", info.largest_block_avail);
    printf("DPMI advisory page fields: max_unlocked=%lu max_locked=%lu linear=%lu unlocked=%lu free_physical=%lu total_physical=%lu free_linear=%lu paging_file=%lu pages\n",
           info.max_unlocked_pages, info.max_locked_pages,
           info.linear_address_space_pages, info.total_unlocked_pages,
           info.free_physical_pages, info.total_physical_pages,
           info.free_linear_pages, info.paging_file_pages);
}

static int verify_model_file(const char *path)
{
    FILE *file;
    long size;
    unsigned char magic[4];

    if (path == NULL) {
        printf("GGUF file: nao informado (passe o caminho como argumento).\n");
        return 1;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        printf("GGUF file: falha ao abrir %s\n", path);
        return 0;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (size = ftell(file)) < 4L ||
        fseek(file, 0L, SEEK_SET) != 0 || fread(magic, 1, 4, file) != 4) {
        printf("GGUF file: leitura/tamanho falhou para %s\n", path);
        fclose(file);
        return 0;
    }
    fclose(file);
    printf("GGUF file: %s; %ld bytes; magic %c%c%c%c\n",
           path, size, magic[0], magic[1], magic[2], magic[3]);
    return memcmp(magic, "GGUF", 4) == 0;
}

int main(int argc, char **argv)
{
    volatile double fpu_input = 1.5;
    volatile double fpu_value;
    clock_t before;
    clock_t elapsed;
    int model_ok;

    printf("NFLLM compatibility probe 1.1\n");
    printf("Target contract: NF-DOS / DOS/32A-bound LE / 386 ISA baseline / Am486DX4.\n");
    printf("sizeof(char/int/long/pointer): %u/%u/%u/%u\n",
           (unsigned)sizeof(char), (unsigned)sizeof(int),
           (unsigned)sizeof(long), (unsigned)sizeof(void *));
    print_dos_version();
    print_cpu();
    print_dpmi_memory();

    before = clock();
    fpu_value = fpu_input * 2.0 + 2.25;
    elapsed = clock() - before;
    printf("x87 arithmetic: %.2f; clock ticks: %lu\n",
           (double)fpu_value, (u32)elapsed);

    model_ok = verify_model_file(argc > 1 ? argv[1] : NULL);
    printf("Probe result: %s\n", model_ok ? "PASS" : "FAIL");
    return model_ok ? 0 : 2;
}
