#include "../../src/nfllm_registry.h"
#include "../nfllm_simd_quant_common.h"
#include <stdlib.h>

/*
 * Four-wide F32 dot product. This object is compiled for the 386 baseline;
 * SSE opcodes are embedded as bytes and are reached only after OSFXSR and
 * SSE2 capabilities were explicitly declared by the Atom test profile.
 */
static void sse2_dot4(float *output, const float *left, const float *right,
                      uint32_t groups);
#pragma aux sse2_dot4 = \
    "db 0fh, 057h, 0c0h" \
    "test ecx, ecx" \
    "jz short sse2_reduce" \
    "sse2_loop:" \
    "db 0fh, 010h, 00ah" \
    "db 0fh, 010h, 017h" \
    "db 0fh, 059h, 0cah" \
    "db 0fh, 058h, 0c1h" \
    "add edx, 16" \
    "add edi, 16" \
    "sub ecx, 1" \
    "jnz short sse2_loop" \
    "sse2_reduce:" \
    "db 0fh, 012h, 0c8h" \
    "db 0fh, 058h, 0c1h" \
    "db 0fh, 028h, 0c8h" \
    "db 0fh, 0c6h, 0c9h, 001h" \
    "db 0f3h, 0fh, 058h, 0c1h" \
    "db 0f3h, 0fh, 011h, 00h" \
    __parm [__eax] [__edx] [__edi] [__ecx] \
    __modify [__eax __edx __edi __ecx]

static void sse2_dot32(int32_t partial[4], const int16_t *left,
                       const int16_t *right);
#pragma aux sse2_dot32 = \
    "db 066h, 00fh, 0efh, 0c0h" \
    "mov ecx, 4" \
    "sse2_qdot_loop:" \
    "db 0f3h, 00fh, 06fh, 00ah" \
    "db 0f3h, 00fh, 06fh, 017h" \
    "db 066h, 00fh, 0f5h, 0cah" \
    "db 066h, 00fh, 0feh, 0c1h" \
    "add edx, 16" \
    "add edi, 16" \
    "sub ecx, 1" \
    "jnz short sse2_qdot_loop" \
    "db 0f3h, 00fh, 07fh, 000h" \
    __parm [__eax] [__edx] [__edi] \
    __modify [__eax __edx __edi __ecx]

static int32_t NFLLM_CALL sse2_matvec(const void *opaque)
{
    const struct nfllm_matvec_f32_args_v1 *args =
        (const struct nfllm_matvec_f32_args_v1 *)opaque;
    uint32_t row;
    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->weights || !args->input || !args->output ||
        args->rows == 0U || args->columns == 0U)
        return NFLLM_E_ARGUMENT;
    if (args->rows > 0xffffffffUL / args->columns) return NFLLM_E_OVERFLOW;

    for (row = 0U; row < args->rows; ++row) {
        const float *weights = args->weights + row * args->columns;
        uint32_t groups = args->columns / 4U;
        uint32_t column = groups * 4U;
        float sum = 0.0f;
        sse2_dot4(&sum, weights, args->input, groups);
        for (; column < args->columns; ++column)
            sum += weights[column] * args->input[column];
        args->output[row] = sum;
    }
    return NFLLM_OK;
}

static nfllm_status_v1 NFLLM_CALL sse2_quantized_matvec_matrix(
    const void *opaque, uint32_t operation_id)
{
    const struct nfllm_quantized_matvec_matrix_args_v1 *args =
        (const struct nfllm_quantized_matvec_matrix_args_v1 *)opaque;
    uint32_t block_bytes = operation_id == NFLLM_OP_MATVEC_Q8_0_MATRIX ? 34U : 18U;
    uint32_t row_bytes, row, block, i;
    int16_t *quantized_input = 0;
    float *temporary_output = 0;
    float input_scale = 0.0f;
    nfllm_status_v1 status;

    status = nfllm_simd_validate_matrix(args, block_bytes, &row_bytes);
    if (status != NFLLM_OK) return status;
    if (args->columns > 0xffffffffUL / (uint32_t)sizeof(int16_t) ||
        args->rows > 0xffffffffUL / (uint32_t)sizeof(float))
        return NFLLM_E_OVERFLOW;
    quantized_input = (int16_t *)malloc(args->columns * sizeof(int16_t));
    temporary_output = (float *)malloc(args->rows * sizeof(float));
    if (!quantized_input || !temporary_output) {
        status = NFLLM_E_MEMORY;
        goto cleanup;
    }
    status = nfllm_simd_quantize_input(args->input, args->columns,
                                       quantized_input, &input_scale);
    if (status != NFLLM_OK) goto cleanup;

    if (input_scale == 0.0f) {
        for (row = 0U; row < args->rows; ++row) temporary_output[row] = 0.0f;
    } else {
        for (row = 0U; row < args->rows; ++row) {
            const uint8_t *packed_row = args->packed_matrix + row * row_bytes;
            float result = 0.0f;
            for (block = 0U; block < args->columns / 32U; ++block) {
                const uint8_t *packed = packed_row + block * block_bytes;
                int16_t expanded_weights[32];
                int32_t partial[4];
                int32_t integer_sum;
                float weight_scale;
                if (nfllm_simd_half_to_float(
                        nfllm_simd_read_u16_le(packed), &weight_scale) != NFLLM_OK) {
                    status = NFLLM_E_NUMERIC;
                    goto cleanup;
                }
                if (operation_id == NFLLM_OP_MATVEC_Q8_0_MATRIX) {
                    for (i = 0U; i < 32U; ++i) {
                        int32_t q = (int32_t)packed[2U + i];
                        if (q >= 128) q -= 256;
                        expanded_weights[i] = (int16_t)q;
                    }
                } else {
                    for (i = 0U; i < 16U; ++i) {
                        expanded_weights[i] =
                            (int16_t)((int32_t)(packed[2U + i] & 0x0fU) - 8);
                        expanded_weights[i + 16U] =
                            (int16_t)((int32_t)(packed[2U + i] >> 4) - 8);
                    }
                }
                sse2_dot32(partial, expanded_weights,
                           quantized_input + block * 32U);
                integer_sum = partial[0] + partial[1] + partial[2] + partial[3];
                result += ((float)integer_sum * weight_scale) * input_scale;
            }
            temporary_output[row] = result;
        }
    }
    if (!nfllm_simd_commit_output(args->output, temporary_output, args->rows))
        status = NFLLM_E_NUMERIC;
    else
        status = NFLLM_OK;

cleanup:
    if (temporary_output) free(temporary_output);
    if (quantized_input) free(quantized_input);
    return status;
}

static nfllm_status_v1 NFLLM_CALL sse2_q8_0_matrix(const void *opaque)
{
    return sse2_quantized_matvec_matrix(opaque, NFLLM_OP_MATVEC_Q8_0_MATRIX);
}

static nfllm_status_v1 NFLLM_CALL sse2_q4_0_matrix(const void *opaque)
{
    return sse2_quantized_matvec_matrix(opaque, NFLLM_OP_MATVEC_Q4_0_MATRIX);
}

static int32_t NFLLM_CALL sse2_initialize(
    const struct nfllm_host_api_v1 *host, void **plugin_state)
{
    if (!host || !plugin_state) return NFLLM_E_ARGUMENT;
    *plugin_state = 0;
    return NFLLM_OK;
}

static void NFLLM_CALL sse2_shutdown(void *plugin_state)
{
    (void)plugin_state;
}

static const struct nfllm_plugin_descriptor_v1 sse2_plugin = {
    sizeof(struct nfllm_plugin_descriptor_v1), NFLLM_ABI_VERSION_1,
    NFLLM_PLUGIN_ID_SSE2, NFLLM_CAP_X87 | NFLLM_CAP_SSE | NFLLM_CAP_SSE2,
    0U, "sse2-f32-q8q4", sse2_initialize, sse2_shutdown
};

static const struct nfllm_kernel_descriptor_v1 sse2_kernels[] = {
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_F32, NFLLM_VALUE_F32, NFLLM_VALUE_F32,
      NFLLM_CAP_SSE | NFLLM_CAP_SSE2, "sse2.matvec.f32", sse2_matvec },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_Q8_0_MATRIX, NFLLM_VALUE_F32, NFLLM_VALUE_F32,
      NFLLM_CAP_SSE | NFLLM_CAP_SSE2, "sse2.matvec.q8_0.matrix", sse2_q8_0_matrix },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_Q4_0_MATRIX, NFLLM_VALUE_F32, NFLLM_VALUE_F32,
      NFLLM_CAP_SSE | NFLLM_CAP_SSE2, "sse2.matvec.q4_0.matrix", sse2_q4_0_matrix }
};

nfllm_status_v1 NFLLM_CALL nfllm_register_sse2_plugin(void)
{
    return nfllm_registry_register(&sse2_plugin, sse2_kernels,
                                   sizeof(sse2_kernels) / sizeof(sse2_kernels[0]));
}
