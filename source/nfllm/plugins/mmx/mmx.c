#include "../../src/nfllm_registry.h"
#include "../nfllm_simd_quant_common.h"
#include <stdlib.h>

/* MMX signed-byte dot for eight quantized values per iteration. */
static void mmx_dot8(int32_t *partial, const signed char *left,
                     const signed char *right, uint32_t groups);
#pragma aux mmx_dot8 = \
    "db 0fh, 0efh, 0c0h" \
    "db 0fh, 0efh, 0dbh" \
    "test ecx, ecx" \
    "jz short mmx_done" \
    "mmx_loop:" \
    "db 0fh, 06fh, 00ah" \
    "db 0fh, 06fh, 017h" \
    "db 0fh, 06fh, 0e3h" \
    "db 0fh, 064h, 0e1h" \
    "db 0fh, 06fh, 0ebh" \
    "db 0fh, 064h, 0eah" \
    "db 0fh, 06fh, 0f1h" \
    "db 0fh, 060h, 0f4h" \
    "db 0fh, 06fh, 0fah" \
    "db 0fh, 060h, 0fdh" \
    "db 0fh, 0f5h, 0f7h" \
    "db 0fh, 0feh, 0c6h" \
    "db 0fh, 06fh, 0f1h" \
    "db 0fh, 068h, 0f4h" \
    "db 0fh, 06fh, 0fah" \
    "db 0fh, 068h, 0fdh" \
    "db 0fh, 0f5h, 0f7h" \
    "db 0fh, 0feh, 0c6h" \
    "add edx, 8" \
    "add edi, 8" \
    "sub ecx, 1" \
    "jnz short mmx_loop" \
    "mmx_done:" \
    "db 0fh, 07fh, 00h" \
    "db 0fh, 077h" \
    __parm [__eax] [__edx] [__edi] [__ecx] \
    __modify [__eax __edx __edi __ecx]

static void mmx_dot32(int32_t *partial, const int16_t *left,
                      const int16_t *right);
#pragma aux mmx_dot32 = \
    "db 0fh, 0efh, 0c0h" \
    "mov ecx, 8" \
    "mmx_qdot_loop:" \
    "db 0fh, 06fh, 00ah" \
    "db 0fh, 06fh, 017h" \
    "db 0fh, 0f5h, 0cah" \
    "db 0fh, 0feh, 0c1h" \
    "add edx, 8" \
    "add edi, 8" \
    "sub ecx, 1" \
    "jnz short mmx_qdot_loop" \
    "db 0fh, 07fh, 00h" \
    "db 0fh, 077h" \
    __parm [__eax] [__edx] [__edi] \
    __modify [__eax __edx __edi __ecx]

static int finite_f32(float value)
{
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return (bits.u & 0x7f800000UL) != 0x7f800000UL;
}

static float abs_f32(float value)
{
    return value < 0.0f ? -value : value;
}

static signed char quantize_s8(float value, float scale)
{
    float scaled = value / scale;
    int rounded = (int)(scaled + (scaled < 0.0f ? -0.5f : 0.5f));
    if (rounded > 127) rounded = 127;
    if (rounded < -127) rounded = -127;
    return (signed char)rounded;
}

static nfllm_status_v1 NFLLM_CALL mmx_matvec(const void *opaque)
{
    const struct nfllm_matvec_f32_args_v1 *args =
        (const struct nfllm_matvec_f32_args_v1 *)opaque;
    signed char *quantized_input = 0;
    signed char *quantized_weights = 0;
    float *temporary_output = 0;
    uint32_t row, column, rows_bytes;
    float max_input = 0.0f;
    float input_scale;
    nfllm_status_v1 result = NFLLM_OK;

    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->weights || !args->input || !args->output ||
        args->rows == 0U || args->columns == 0U ||
        args->columns > 131071U ||
        (((uintptr_t)args->weights & 3U) != 0U) ||
        (((uintptr_t)args->input & 3U) != 0U) ||
        (((uintptr_t)args->output & 3U) != 0U))
        return NFLLM_E_ARGUMENT;
    if (args->rows > 0xffffffffUL / args->columns ||
        args->rows > 0xffffffffUL / (uint32_t)sizeof(float))
        return NFLLM_E_OVERFLOW;

    rows_bytes = args->rows * (uint32_t)sizeof(float);
    quantized_input = (signed char *)malloc(args->columns);
    quantized_weights = (signed char *)malloc(args->columns);
    temporary_output = (float *)malloc(rows_bytes);
    if (!quantized_input || !quantized_weights || !temporary_output) {
        result = NFLLM_E_MEMORY;
        goto cleanup;
    }

    for (column = 0U; column < args->columns; ++column) {
        float value = args->input[column];
        float magnitude;
        if (!finite_f32(value)) { result = NFLLM_E_NUMERIC; goto cleanup; }
        magnitude = abs_f32(value);
        if (magnitude > max_input) max_input = magnitude;
    }
    if (max_input == 0.0f) {
        for (row = 0U; row < args->rows; ++row) temporary_output[row] = 0.0f;
    } else {
        input_scale = max_input / 127.0f;
        if (input_scale == 0.0f) input_scale = max_input;
        for (column = 0U; column < args->columns; ++column)
            quantized_input[column] = quantize_s8(args->input[column], input_scale);

        for (row = 0U; row < args->rows; ++row) {
            const float *weights = args->weights + row * args->columns;
            float max_weight = 0.0f;
            float weight_scale, output;
            int32_t partial[2] = {0, 0};
            int32_t integer_sum;
            uint32_t groups = args->columns / 8U;

            for (column = 0U; column < args->columns; ++column) {
                float value = weights[column];
                float magnitude;
                if (!finite_f32(value)) { result = NFLLM_E_NUMERIC; goto cleanup; }
                magnitude = abs_f32(value);
                if (magnitude > max_weight) max_weight = magnitude;
            }
            if (max_weight == 0.0f) {
                temporary_output[row] = 0.0f;
                continue;
            }
            weight_scale = max_weight / 127.0f;
            if (weight_scale == 0.0f) weight_scale = max_weight;
            for (column = 0U; column < args->columns; ++column)
                quantized_weights[column] = quantize_s8(weights[column], weight_scale);

            mmx_dot8(partial, quantized_weights, quantized_input, groups);
            integer_sum = partial[0] + partial[1];
            for (column = groups * 8U; column < args->columns; ++column)
                integer_sum += (int32_t)quantized_weights[column] *
                               (int32_t)quantized_input[column];
            output = ((float)integer_sum * weight_scale) * input_scale;
            if (!finite_f32(output)) { result = NFLLM_E_NUMERIC; goto cleanup; }
            temporary_output[row] = output;
        }
    }
    for (row = 0U; row < args->rows; ++row) args->output[row] = temporary_output[row];

cleanup:
    if (temporary_output) free(temporary_output);
    if (quantized_weights) free(quantized_weights);
    if (quantized_input) free(quantized_input);
    return result;
}

static nfllm_status_v1 NFLLM_CALL mmx_quantized_matvec_matrix(
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
                int32_t partial[2];
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
                mmx_dot32(partial, expanded_weights,
                          quantized_input + block * 32U);
                integer_sum = partial[0] + partial[1];
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

static nfllm_status_v1 NFLLM_CALL mmx_q8_0_matrix(const void *opaque)
{
    return mmx_quantized_matvec_matrix(opaque, NFLLM_OP_MATVEC_Q8_0_MATRIX);
}

static nfllm_status_v1 NFLLM_CALL mmx_q4_0_matrix(const void *opaque)
{
    return mmx_quantized_matvec_matrix(opaque, NFLLM_OP_MATVEC_Q4_0_MATRIX);
}

static int32_t NFLLM_CALL mmx_initialize(
    const struct nfllm_host_api_v1 *host, void **plugin_state)
{
    if (!host || !plugin_state) return NFLLM_E_ARGUMENT;
    *plugin_state = 0;
    return NFLLM_OK;
}

static void NFLLM_CALL mmx_shutdown(void *plugin_state)
{
    (void)plugin_state;
}

static const struct nfllm_plugin_descriptor_v1 mmx_plugin = {
    sizeof(struct nfllm_plugin_descriptor_v1), NFLLM_ABI_VERSION_1,
    NFLLM_PLUGIN_ID_MMX,
    NFLLM_CAP_386 | NFLLM_CAP_X87 | NFLLM_CAP_MMX, 0U,
    "mmx-q8q4-quantized-matvec", mmx_initialize, mmx_shutdown
};

static const struct nfllm_kernel_descriptor_v1 mmx_kernels[] = {
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_F32, NFLLM_VALUE_F32, NFLLM_VALUE_F32,
      NFLLM_CAP_MMX, "mmx.matvec.f32.quantized-int8", mmx_matvec },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_Q8_0_MATRIX, NFLLM_VALUE_F32, NFLLM_VALUE_F32,
      NFLLM_CAP_MMX, "mmx.matvec.q8_0.matrix", mmx_q8_0_matrix },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_Q4_0_MATRIX, NFLLM_VALUE_F32, NFLLM_VALUE_F32,
      NFLLM_CAP_MMX, "mmx.matvec.q4_0.matrix", mmx_q4_0_matrix }
};

nfllm_status_v1 NFLLM_CALL nfllm_register_mmx_plugin(void)
{
    return nfllm_registry_register(&mmx_plugin, mmx_kernels,
                                   sizeof(mmx_kernels) / sizeof(mmx_kernels[0]));
}
