#include "../../src/nfllm_registry.h"

/*
 * Packed single-precision dot product. The caller supplies an even element
 * count. EMMS restores the x87 tag state before control returns to C.
 */
static void dnow_dot_pairs(float *output, const float *left,
                           const float *right, uint32_t pairs);
#pragma aux dnow_dot_pairs = \
    /* MMX MOVQ/EMMS and 3DNow! PF* opcodes are encoded explicitly because
       this translation unit must remain compiled for the 386 baseline. */ \
    "db 0fh, 0efh, 0c0h" \
    "test ecx, ecx" \
    "jz short dnow_done" \
    "dnow_loop:" \
    "db 0fh, 06fh, 0ah" \
    "db 0fh, 06fh, 17h" \
    "db 0fh, 0fh, 0cah, 0b4h" \
    "db 0fh, 0fh, 0c1h, 09eh" \
    "add edx, 8" \
    "add edi, 8" \
    "sub ecx, 1" \
    "jnz short dnow_loop" \
    "dnow_done:" \
    "db 0fh, 0fh, 0c0h, 0aeh" \
    "db 0fh, 07eh, 00h" \
    "db 0fh, 077h" \
    __parm [__eax] [__edx] [__edi] [__ecx] \
    __modify [__eax __edx __edi __ecx]

static nfllm_status_v1 NFLLM_CALL dnow_matvec(const void *opaque)
{
    const struct nfllm_matvec_f32_args_v1 *args =
        (const struct nfllm_matvec_f32_args_v1 *)opaque;
    uint32_t row, pairs;
    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->weights || !args->input || !args->output ||
        args->rows == 0U || args->columns == 0U)
        return NFLLM_E_ARGUMENT;
    if (args->rows > 0xffffffffUL / args->columns) return NFLLM_E_OVERFLOW;

    pairs = args->columns / 2U;
    for (row = 0U; row < args->rows; ++row) {
        const float *weights = args->weights + row * args->columns;
        float sum = 0.0f;
        dnow_dot_pairs(&sum, weights, args->input, pairs);
        if ((args->columns & 1U) != 0U)
            sum += weights[args->columns - 1U] * args->input[args->columns - 1U];
        args->output[row] = sum;
    }
    return NFLLM_OK;
}

static int32_t NFLLM_CALL dnow_initialize(
    const struct nfllm_host_api_v1 *host, void **plugin_state)
{
    if (!host || !plugin_state) return NFLLM_E_ARGUMENT;
    *plugin_state = 0;
    return NFLLM_OK;
}

static void NFLLM_CALL dnow_shutdown(void *plugin_state)
{
    (void)plugin_state;
}

static const struct nfllm_plugin_descriptor_v1 dnow_plugin = {
    sizeof(struct nfllm_plugin_descriptor_v1), NFLLM_ABI_VERSION_1,
    NFLLM_PLUGIN_ID_3DNOW,
    NFLLM_CAP_386 | NFLLM_CAP_X87 | NFLLM_CAP_MMX | NFLLM_CAP_3DNOW, 0U,
    "3dnow-f32", dnow_initialize, dnow_shutdown
};

static const struct nfllm_kernel_descriptor_v1 dnow_kernels[] = {
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_F32, NFLLM_VALUE_F32, NFLLM_VALUE_F32,
      NFLLM_CAP_MMX | NFLLM_CAP_3DNOW, "3dnow.matvec.f32", dnow_matvec }
};

nfllm_status_v1 NFLLM_CALL nfllm_register_3dnow_plugin(void)
{
    return nfllm_registry_register(&dnow_plugin, dnow_kernels,
                                   sizeof(dnow_kernels) / sizeof(dnow_kernels[0]));
}
