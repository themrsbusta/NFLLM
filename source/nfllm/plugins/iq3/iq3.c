#include "../../src/nfllm_registry.h"
#include "iq3_tables.h"

#define IQ3_BLOCK_ELEMENTS 256U
#define IQ3_XXS_BLOCK_BYTES 98U
#define IQ3_S_BLOCK_BYTES 110U

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static nfllm_status_v1 half_to_float(uint16_t half, float *value)
{
    uint32_t sign = ((uint32_t)half & 0x8000U) << 16;
    uint32_t exponent = ((uint32_t)half >> 10) & 0x1fU;
    uint32_t fraction = (uint32_t)half & 0x03ffU;
    union { uint32_t bits; float value; } converted;

    if (!value) return NFLLM_E_ARGUMENT;
    if (exponent == 0x1fU) return NFLLM_E_NUMERIC;
    if (exponent == 0U) {
        int32_t unbiased = -14;
        if (fraction == 0U) {
            converted.bits = sign;
        } else {
            while ((fraction & 0x0400U) == 0U) {
                fraction <<= 1;
                --unbiased;
            }
            fraction &= 0x03ffU;
            converted.bits = sign | ((uint32_t)(unbiased + 127) << 23) |
                             (fraction << 13);
        }
    } else {
        converted.bits = sign | ((exponent + 112U) << 23) | (fraction << 13);
    }
    *value = converted.value;
    return NFLLM_OK;
}

static nfllm_status_v1 validate_row(const uint8_t *packed, uint32_t packed_bytes,
                                    uint32_t columns, float *output,
                                    uint32_t block_bytes)
{
    uint32_t blocks;
    if (!packed || !output || columns == 0U ||
        (columns % IQ3_BLOCK_ELEMENTS) != 0U ||
        (((uintptr_t)output & (sizeof(float) - 1U)) != 0U))
        return NFLLM_E_ARGUMENT;
    blocks = columns / IQ3_BLOCK_ELEMENTS;
    if (blocks > 0xffffffffUL / block_bytes) return NFLLM_E_OVERFLOW;
    if (packed_bytes != blocks * block_bytes) return NFLLM_E_SIZE;
    return NFLLM_OK;
}

static uint8_t sign_mask(uint32_t encoded)
{
    uint32_t low = encoded & 0x7fU;
    uint32_t bits = low;
    uint32_t parity = 0U;
    while (bits != 0U) {
        parity ^= bits & 1U;
        bits >>= 1;
    }
    return (uint8_t)(low | (parity << 7));
}

static float signed_grid_value(uint32_t packed_grid, uint32_t grid_lane,
                               uint8_t signs, uint32_t sign_lane, float scale)
{
    uint32_t magnitude = (packed_grid >> (grid_lane * 8U)) & 0xffU;
    return scale * (float)magnitude *
           ((signs & (1U << sign_lane)) ? -1.0f : 1.0f);
}

static void accumulate_grid_dot(uint32_t packed_grid, uint8_t signs,
                               uint32_t sign_lane, float scale,
                               const float *input, uint32_t input_index,
                               float *sum)
{
    uint32_t j;
    for (j = 0U; j < 4U; ++j) {
        uint32_t magnitude = (packed_grid >> (j * 8U)) & 0xffU;
        volatile float weight = scale * (float)magnitude *
            ((signs & (1U << (sign_lane + j))) ? -1.0f : 1.0f);
        *sum += weight * input[input_index + j];
    }
}

static nfllm_status_v1 NFLLM_CALL iq3_xxs_dequantize_row(const void *opaque)
{
    const struct nfllm_iq3_xxs_dequantize_row_args_v1 *args =
        (const struct nfllm_iq3_xxs_dequantize_row_args_v1 *)opaque;
    uint32_t blocks, block, ib32, l, j, out;
    nfllm_status_v1 status;

    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    status = validate_row(args->packed_row, args->packed_bytes, args->columns,
                          args->output, IQ3_XXS_BLOCK_BYTES);
    if (status != NFLLM_OK) return status;
    blocks = args->columns / IQ3_BLOCK_ELEMENTS;
    for (block = 0U; block < blocks; ++block) {
        if ((read_u16_le(args->packed_row + block * IQ3_XXS_BLOCK_BYTES) &
             0x7c00U) == 0x7c00U) return NFLLM_E_NUMERIC;
    }
    out = 0U;
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * IQ3_XXS_BLOCK_BYTES;
        const uint8_t *qs = packed + 2U;
        const uint8_t *scales_and_signs = qs + 64U;
        float d;
        status = half_to_float(read_u16_le(packed), &d);
        if (status != NFLLM_OK) return status;
        for (ib32 = 0U; ib32 < 8U; ++ib32) {
            uint32_t aux = read_u32_le(scales_and_signs + 4U * ib32);
            uint32_t scale_code = aux >> 28;
            float scale = d * (0.5f + (float)scale_code) * 0.5f;
            for (l = 0U; l < 4U; ++l) {
                uint8_t signs = sign_mask((aux >> (7U * l)) & 0x7fU);
                uint32_t grid1 = nfllm_iq3xxs_grid[qs[2U * l]];
                uint32_t grid2 = nfllm_iq3xxs_grid[qs[2U * l + 1U]];
                for (j = 0U; j < 4U; ++j) {
                    args->output[out + j] = signed_grid_value(grid1, j, signs, j, scale);
                    args->output[out + j + 4U] = signed_grid_value(grid2, j, signs, j + 4U, scale);
                }
                out += 8U;
            }
            qs += 8U;
        }
    }
    return out == args->columns ? NFLLM_OK : NFLLM_E_STATE;
}

static nfllm_status_v1 NFLLM_CALL iq3_s_dequantize_row(const void *opaque)
{
    const struct nfllm_iq3_s_dequantize_row_args_v1 *args =
        (const struct nfllm_iq3_s_dequantize_row_args_v1 *)opaque;
    uint32_t blocks, block, sub, half_index, l, j, out;
    nfllm_status_v1 status;

    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    status = validate_row(args->packed_row, args->packed_bytes, args->columns,
                          args->output, IQ3_S_BLOCK_BYTES);
    if (status != NFLLM_OK) return status;
    blocks = args->columns / IQ3_BLOCK_ELEMENTS;
    for (block = 0U; block < blocks; ++block) {
        if ((read_u16_le(args->packed_row + block * IQ3_S_BLOCK_BYTES) &
             0x7c00U) == 0x7c00U) return NFLLM_E_NUMERIC;
    }
    out = 0U;
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * IQ3_S_BLOCK_BYTES;
        const uint8_t *qs = packed + 2U;
        const uint8_t *qh = qs + 64U;
        const uint8_t *signs = qh + 8U;
        const uint8_t *scales = signs + 32U;
        float d;
        status = half_to_float(read_u16_le(packed), &d);
        if (status != NFLLM_OK) return status;
        for (sub = 0U; sub < 4U; ++sub) {
            uint8_t scale_pair = scales[sub];
            float scale[2];
            scale[0] = d * (1.0f + 2.0f * (float)(scale_pair & 0x0fU));
            scale[1] = d * (1.0f + 2.0f * (float)(scale_pair >> 4));
            for (half_index = 0U; half_index < 2U; ++half_index) {
                const uint8_t *q_group = qs + sub * 16U + half_index * 8U;
                const uint8_t *sign_group = signs + sub * 8U + half_index * 4U;
                uint8_t high = qh[sub * 2U + half_index];
                for (l = 0U; l < 4U; ++l) {
                    uint32_t index1 = q_group[2U * l] |
                        (((uint32_t)high << (8U - 2U * l)) & 0x100U);
                    uint32_t index2 = q_group[2U * l + 1U] |
                        (((uint32_t)high << (7U - 2U * l)) & 0x100U);
                    uint8_t sign_bits = sign_group[l];
                    uint32_t grid1 = nfllm_iq3s_grid[index1];
                    uint32_t grid2 = nfllm_iq3s_grid[index2];
                    for (j = 0U; j < 4U; ++j) {
                        args->output[out + j] = signed_grid_value(grid1, j, sign_bits, j, scale[half_index]);
                        args->output[out + j + 4U] = signed_grid_value(grid2, j, sign_bits, j + 4U, scale[half_index]);
                    }
                    out += 8U;
                }
            }
        }
    }
    return out == args->columns ? NFLLM_OK : NFLLM_E_STATE;
}

static nfllm_status_v1 NFLLM_CALL iq3_xxs_matvec_row(const void *opaque)
{
    const struct nfllm_packed_matvec_row_args_v1 *args =
        (const struct nfllm_packed_matvec_row_args_v1 *)opaque;
    uint32_t blocks, block, ib32, l, out;
    float result = 0.0f;
    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->packed_row || !args->input || !args->output ||
        args->columns == 0U || (args->columns % IQ3_BLOCK_ELEMENTS) != 0U ||
        (((uintptr_t)args->input & (sizeof(float) - 1U)) != 0U) ||
        (((uintptr_t)args->output & (sizeof(float) - 1U)) != 0U))
        return NFLLM_E_ARGUMENT;
    blocks = args->columns / IQ3_BLOCK_ELEMENTS;
    if (blocks > 0xffffffffUL / IQ3_XXS_BLOCK_BYTES) return NFLLM_E_OVERFLOW;
    if (args->packed_bytes != blocks * IQ3_XXS_BLOCK_BYTES) return NFLLM_E_SIZE;
    for (block = 0U; block < blocks; ++block) {
        if ((read_u16_le(args->packed_row + block * IQ3_XXS_BLOCK_BYTES) &
             0x7c00U) == 0x7c00U) return NFLLM_E_NUMERIC;
    }
    out = 0U;
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * IQ3_XXS_BLOCK_BYTES;
        const uint8_t *qs = packed + 2U;
        const uint8_t *scales_and_signs = qs + 64U;
        float d;
        if (half_to_float(read_u16_le(packed), &d) != NFLLM_OK)
            return NFLLM_E_NUMERIC;
        for (ib32 = 0U; ib32 < 8U; ++ib32) {
            uint32_t aux = read_u32_le(scales_and_signs + 4U * ib32);
            uint32_t scale_code = aux >> 28;
            float scale = d * (0.5f + (float)scale_code) * 0.5f;
            for (l = 0U; l < 4U; ++l) {
                uint8_t signs = sign_mask((aux >> (7U * l)) & 0x7fU);
                uint32_t grid1 = nfllm_iq3xxs_grid[qs[2U * l]];
                uint32_t grid2 = nfllm_iq3xxs_grid[qs[2U * l + 1U]];
                accumulate_grid_dot(grid1, signs, 0U, scale, args->input,
                                    out, &result);
                accumulate_grid_dot(grid2, signs, 4U, scale, args->input,
                                    out + 4U, &result);
                out += 8U;
            }
            qs += 8U;
        }
    }
    if (out != args->columns) return NFLLM_E_STATE;
    *args->output = result;
    return NFLLM_OK;
}

static nfllm_status_v1 NFLLM_CALL iq3_s_matvec_row(const void *opaque)
{
    const struct nfllm_packed_matvec_row_args_v1 *args =
        (const struct nfllm_packed_matvec_row_args_v1 *)opaque;
    uint32_t blocks, block, sub, half_index, l, out;
    float result = 0.0f;
    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->packed_row || !args->input || !args->output ||
        args->columns == 0U || (args->columns % IQ3_BLOCK_ELEMENTS) != 0U ||
        (((uintptr_t)args->input & (sizeof(float) - 1U)) != 0U) ||
        (((uintptr_t)args->output & (sizeof(float) - 1U)) != 0U))
        return NFLLM_E_ARGUMENT;
    blocks = args->columns / IQ3_BLOCK_ELEMENTS;
    if (blocks > 0xffffffffUL / IQ3_S_BLOCK_BYTES) return NFLLM_E_OVERFLOW;
    if (args->packed_bytes != blocks * IQ3_S_BLOCK_BYTES) return NFLLM_E_SIZE;
    for (block = 0U; block < blocks; ++block) {
        if ((read_u16_le(args->packed_row + block * IQ3_S_BLOCK_BYTES) &
             0x7c00U) == 0x7c00U) return NFLLM_E_NUMERIC;
    }
    out = 0U;
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * IQ3_S_BLOCK_BYTES;
        const uint8_t *qs = packed + 2U;
        const uint8_t *qh = qs + 64U;
        const uint8_t *signs = qh + 8U;
        const uint8_t *scales = signs + 32U;
        float d;
        if (half_to_float(read_u16_le(packed), &d) != NFLLM_OK)
            return NFLLM_E_NUMERIC;
        for (sub = 0U; sub < 4U; ++sub) {
            uint8_t scale_pair = scales[sub];
            float scale[2];
            scale[0] = d * (1.0f + 2.0f * (float)(scale_pair & 0x0fU));
            scale[1] = d * (1.0f + 2.0f * (float)(scale_pair >> 4));
            for (half_index = 0U; half_index < 2U; ++half_index) {
                const uint8_t *q_group = qs + sub * 16U + half_index * 8U;
                const uint8_t *sign_group = signs + sub * 8U + half_index * 4U;
                uint8_t high = qh[sub * 2U + half_index];
                for (l = 0U; l < 4U; ++l) {
                    uint32_t index1 = q_group[2U * l] |
                        (((uint32_t)high << (8U - 2U * l)) & 0x100U);
                    uint32_t index2 = q_group[2U * l + 1U] |
                        (((uint32_t)high << (7U - 2U * l)) & 0x100U);
                    uint8_t sign_bits = sign_group[l];
                    uint32_t grid1 = nfllm_iq3s_grid[index1];
                    uint32_t grid2 = nfllm_iq3s_grid[index2];
                    accumulate_grid_dot(grid1, sign_bits, 0U,
                                        scale[half_index], args->input,
                                        out, &result);
                    accumulate_grid_dot(grid2, sign_bits, 4U,
                                        scale[half_index], args->input,
                                        out + 4U, &result);
                    out += 8U;
                }
            }
        }
    }
    if (out != args->columns) return NFLLM_E_STATE;
    *args->output = result;
    return NFLLM_OK;
}

static int32_t NFLLM_CALL iq3_initialize(
    const struct nfllm_host_api_v1 *host, void **plugin_state)
{
    if (!host || !plugin_state) return NFLLM_E_ARGUMENT;
    *plugin_state = 0;
    return NFLLM_OK;
}

static void NFLLM_CALL iq3_shutdown(void *plugin_state)
{
    (void)plugin_state;
}

static const struct nfllm_plugin_descriptor_v1 iq3_plugin = {
    sizeof(struct nfllm_plugin_descriptor_v1), NFLLM_ABI_VERSION_1,
    NFLLM_PLUGIN_ID_IQ3, NFLLM_CAP_386 | NFLLM_CAP_X87, 0U,
    "iq3-x87-dequant", iq3_initialize, iq3_shutdown
};

static const struct nfllm_kernel_descriptor_v1 iq3_kernels[] = {
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_DEQUANTIZE_ROW_IQ3_XXS, NFLLM_VALUE_NONE, NFLLM_VALUE_F32, 0U,
      "iq3_xxs.dequantize_row", iq3_xxs_dequantize_row },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_DEQUANTIZE_ROW_IQ3_S, NFLLM_VALUE_NONE, NFLLM_VALUE_F32, 0U,
      "iq3_s.dequantize_row", iq3_s_dequantize_row },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_IQ3_XXS_ROW, NFLLM_VALUE_F32, NFLLM_VALUE_F32, 0U,
      "iq3_xxs.direct_matvec_row", iq3_xxs_matvec_row },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_IQ3_S_ROW, NFLLM_VALUE_F32, NFLLM_VALUE_F32, 0U,
      "iq3_s.direct_matvec_row", iq3_s_matvec_row }
};

nfllm_status_v1 NFLLM_CALL nfllm_register_iq3_plugin(void)
{
    return nfllm_registry_register(&iq3_plugin, iq3_kernels,
                                   sizeof(iq3_kernels) / sizeof(iq3_kernels[0]));
}
