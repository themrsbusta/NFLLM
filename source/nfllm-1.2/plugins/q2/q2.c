#include "../../src/nfllm_registry.h"
#include "../../src/nfllm_plugin_sdk.h"

#define Q2_0_BLOCK_ELEMENTS 64U
#define Q2_0_BLOCK_BYTES 18U
#define Q2_K_BLOCK_ELEMENTS 256U
#define Q2_K_BLOCK_BYTES 84U

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
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
                                    uint32_t block_elements, uint32_t block_bytes)
{
    uint32_t blocks;
    if (!packed || !output || columns == 0U ||
        (columns % block_elements) != 0U ||
        (((uintptr_t)output & (sizeof(float) - 1U)) != 0U))
        return NFLLM_E_ARGUMENT;
    blocks = columns / block_elements;
    if (blocks > 0xffffffffUL / block_bytes) return NFLLM_E_OVERFLOW;
    if (packed_bytes != blocks * block_bytes) return NFLLM_E_SIZE;
    return NFLLM_OK;
}

static nfllm_status_v1 NFLLM_CALL q2_0_dequantize_row(const void *opaque)
{
    const struct nfllm_q2_0_dequantize_row_args_v1 *args =
        (const struct nfllm_q2_0_dequantize_row_args_v1 *)opaque;
    uint32_t blocks, block, i;
    nfllm_status_v1 status;

    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    status = validate_row(args->packed_row, args->packed_bytes, args->columns,
                          args->output, Q2_0_BLOCK_ELEMENTS, Q2_0_BLOCK_BYTES);
    if (status != NFLLM_OK) return status;
    blocks = args->columns / Q2_0_BLOCK_ELEMENTS;
    for (block = 0U; block < blocks; ++block) {
        if ((read_u16_le(args->packed_row + block * Q2_0_BLOCK_BYTES) & 0x7c00U) ==
            0x7c00U) return NFLLM_E_NUMERIC;
    }
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * Q2_0_BLOCK_BYTES;
        float scale;
        status = half_to_float(read_u16_le(packed), &scale);
        if (status != NFLLM_OK) return status;
        for (i = 0U; i < Q2_0_BLOCK_ELEMENTS; ++i) {
            uint32_t byte_index = i / 4U;
            uint32_t shift = (i & 3U) * 2U;
            int32_t quant = (int32_t)((packed[2U + byte_index] >> shift) & 3U);
            args->output[block * Q2_0_BLOCK_ELEMENTS + i] =
                (float)(quant - 1) * scale;
        }
    }
    return NFLLM_OK;
}

static nfllm_status_v1 NFLLM_CALL q2_k_dequantize_row(const void *opaque)
{
    const struct nfllm_q2_k_dequantize_row_args_v1 *args =
        (const struct nfllm_q2_k_dequantize_row_args_v1 *)opaque;
    uint32_t blocks, block, out, is, n, shift, group, i;
    nfllm_status_v1 status;

    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    status = validate_row(args->packed_row, args->packed_bytes, args->columns,
                          args->output, Q2_K_BLOCK_ELEMENTS, Q2_K_BLOCK_BYTES);
    if (status != NFLLM_OK) return status;
    blocks = args->columns / Q2_K_BLOCK_ELEMENTS;
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * Q2_K_BLOCK_BYTES;
        if ((read_u16_le(packed + 80U) & 0x7c00U) == 0x7c00U ||
            (read_u16_le(packed + 82U) & 0x7c00U) == 0x7c00U)
            return NFLLM_E_NUMERIC;
    }
    out = 0U;
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * Q2_K_BLOCK_BYTES;
        const uint8_t *scales = packed;
        const uint8_t *quants = packed + 16U;
        float d, dmin;
        status = half_to_float(read_u16_le(packed + 80U), &d);
        if (status != NFLLM_OK) return status;
        status = half_to_float(read_u16_le(packed + 82U), &dmin);
        if (status != NFLLM_OK) return status;
        is = 0U;
        for (n = 0U; n < Q2_K_BLOCK_ELEMENTS; n += 128U) {
            for (shift = 0U; shift < 8U; shift += 2U) {
                for (group = 0U; group < 2U; ++group) {
                    uint8_t scale_min = scales[is++];
                    float dl = d * (float)(scale_min & 0x0fU);
                    float ml = dmin * (float)(scale_min >> 4);
                    const uint8_t *q = quants + (n / 128U) * 32U + group * 16U;
                    for (i = 0U; i < 16U; ++i) {
                        uint32_t quant = (q[i] >> shift) & 3U;
                        args->output[out++] = dl * (float)quant - ml;
                    }
                }
            }
        }
    }
    return out == args->columns ? NFLLM_OK : NFLLM_E_STATE;
}

/*
 * Direct packed dot products. The decoded scalar is consumed immediately;
 * unlike the fallback path, no columns-sized F32 row is materialized.
 */
static nfllm_status_v1 NFLLM_CALL q2_0_matvec_row(const void *opaque)
{
    const struct nfllm_packed_matvec_row_args_v1 *args =
        (const struct nfllm_packed_matvec_row_args_v1 *)opaque;
    uint32_t blocks, block, i;
    float result = 0.0f;
    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->packed_row || !args->input || !args->output ||
        args->columns == 0U || (args->columns % Q2_0_BLOCK_ELEMENTS) != 0U ||
        (((uintptr_t)args->input & (sizeof(float) - 1U)) != 0U) ||
        (((uintptr_t)args->output & (sizeof(float) - 1U)) != 0U))
        return NFLLM_E_ARGUMENT;
    blocks = args->columns / Q2_0_BLOCK_ELEMENTS;
    if (blocks > 0xffffffffUL / Q2_0_BLOCK_BYTES) return NFLLM_E_OVERFLOW;
    if (args->packed_bytes != blocks * Q2_0_BLOCK_BYTES) return NFLLM_E_SIZE;
    for (block = 0U; block < blocks; ++block) {
        if ((read_u16_le(args->packed_row + block * Q2_0_BLOCK_BYTES) &
             0x7c00U) == 0x7c00U) return NFLLM_E_NUMERIC;
    }
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * Q2_0_BLOCK_BYTES;
        float d;
        if (half_to_float(read_u16_le(packed), &d) != NFLLM_OK)
            return NFLLM_E_NUMERIC;
        for (i = 0U; i < Q2_0_BLOCK_ELEMENTS; ++i) {
            uint32_t shift = (i & 3U) * 2U;
            int32_t quant = (int32_t)((packed[2U + i / 4U] >> shift) & 3U);
            volatile float weight = (float)(quant - 1) * d;
            result += weight * args->input[block * Q2_0_BLOCK_ELEMENTS + i];
        }
    }
    *args->output = result;
    return NFLLM_OK;
}

static nfllm_status_v1 NFLLM_CALL q2_k_matvec_row(const void *opaque)
{
    const struct nfllm_packed_matvec_row_args_v1 *args =
        (const struct nfllm_packed_matvec_row_args_v1 *)opaque;
    uint32_t blocks, block, n, shift, group, i, is, column;
    float result = 0.0f;
    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->packed_row || !args->input || !args->output ||
        args->columns == 0U || (args->columns % Q2_K_BLOCK_ELEMENTS) != 0U ||
        (((uintptr_t)args->input & (sizeof(float) - 1U)) != 0U) ||
        (((uintptr_t)args->output & (sizeof(float) - 1U)) != 0U))
        return NFLLM_E_ARGUMENT;
    blocks = args->columns / Q2_K_BLOCK_ELEMENTS;
    if (blocks > 0xffffffffUL / Q2_K_BLOCK_BYTES) return NFLLM_E_OVERFLOW;
    if (args->packed_bytes != blocks * Q2_K_BLOCK_BYTES) return NFLLM_E_SIZE;
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * Q2_K_BLOCK_BYTES;
        if ((read_u16_le(packed + 80U) & 0x7c00U) == 0x7c00U ||
            (read_u16_le(packed + 82U) & 0x7c00U) == 0x7c00U)
            return NFLLM_E_NUMERIC;
    }
    column = 0U;
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * Q2_K_BLOCK_BYTES;
        const uint8_t *scales = packed;
        const uint8_t *quants = packed + 16U;
        float d, dmin;
        if (half_to_float(read_u16_le(packed + 80U), &d) != NFLLM_OK ||
            half_to_float(read_u16_le(packed + 82U), &dmin) != NFLLM_OK)
            return NFLLM_E_NUMERIC;
        is = 0U;
        for (n = 0U; n < Q2_K_BLOCK_ELEMENTS; n += 128U) {
            for (shift = 0U; shift < 8U; shift += 2U) {
                for (group = 0U; group < 2U; ++group) {
                    uint8_t scale_min = scales[is++];
                    float dl = d * (float)(scale_min & 0x0fU);
                    float ml = dmin * (float)(scale_min >> 4);
                    const uint8_t *q = quants + (n / 128U) * 32U + group * 16U;
                    for (i = 0U; i < 16U; ++i) {
                        uint32_t quant = (q[i] >> shift) & 3U;
                        volatile float weight = dl * (float)quant - ml;
                        result += weight * args->input[column++];
                    }
                }
            }
        }
    }
    if (column != args->columns) return NFLLM_E_STATE;
    *args->output = result;
    return NFLLM_OK;
}

static int32_t NFLLM_CALL q2_initialize(
    const struct nfllm_host_api_v1 *host, void **plugin_state)
{
    if (!host || !plugin_state) return NFLLM_E_ARGUMENT;
    *plugin_state = 0;
    return NFLLM_OK;
}

static void NFLLM_CALL q2_shutdown(void *plugin_state)
{
    (void)plugin_state;
}

static const struct nfllm_plugin_descriptor_v1 q2_plugin = {
    sizeof(struct nfllm_plugin_descriptor_v1), NFLLM_ABI_VERSION_1,
    NFLLM_PLUGIN_ID_Q2, NFLLM_CAP_386 | NFLLM_CAP_X87, 0U,
    "q2-x87-dequant", q2_initialize, q2_shutdown
};

static const struct nfllm_kernel_descriptor_v1 q2_kernels[] = {
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_DEQUANTIZE_ROW_Q2_0, NFLLM_VALUE_NONE, NFLLM_VALUE_F32, 0U,
      "q2_0.dequantize_row", q2_0_dequantize_row },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_DEQUANTIZE_ROW_Q2_K, NFLLM_VALUE_NONE, NFLLM_VALUE_F32, 0U,
      "q2_k.dequantize_row", q2_k_dequantize_row },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_Q2_0_ROW, NFLLM_VALUE_F32, NFLLM_VALUE_F32, 0U,
      "q2_0.direct_matvec_row", q2_0_matvec_row },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_Q2_K_ROW, NFLLM_VALUE_F32, NFLLM_VALUE_F32, 0U,
      "q2_k.direct_matvec_row", q2_k_matvec_row }
};

static const struct nfllm_quantization_v1 q2_quantizations[] = {
    { sizeof(struct nfllm_quantization_v1), NFLLM_PLUGIN_ABI_V1,
      42U, 2U, 64U, 18U, NFLLM_OP_DEQUANTIZE_ROW_Q2_0,
      NFLLM_OP_MATVEC_Q2_0_ROW, "Q2_0", 0 },
    { sizeof(struct nfllm_quantization_v1), NFLLM_PLUGIN_ABI_V1,
      10U, 2U, 256U, 84U, NFLLM_OP_DEQUANTIZE_ROW_Q2_K,
      NFLLM_OP_MATVEC_Q2_K_ROW, "Q2_K", 0 }
};
static const struct nfllm_extension_v1 q2_extensions[] = {
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_QUANTIZATION, 42U, 0U, 0, "Q2_0", &q2_quantizations[0] },
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_QUANTIZATION, 10U, 0U, 0, "Q2_K", &q2_quantizations[1] }
};
NFLLM_DEFINE_PLUGIN_QUERY_V1(NFLLM_PLUGIN_ID_Q2,
    NFLLM_CAP_386 | NFLLM_CAP_X87, "q2-x87-dequant", "1.2.0",
    q2_kernels, q2_extensions,
    (uint32_t)(sizeof(q2_extensions) / sizeof(q2_extensions[0])))

#if !defined(NFLLM_DYNAMIC_PLUGIN_EXPORT)
nfllm_status_v1 NFLLM_CALL nfllm_register_q2_plugin(void)
{
    return nfllm_registry_register(&q2_plugin, q2_kernels,
                                   sizeof(q2_kernels) / sizeof(q2_kernels[0]));
}
#endif
