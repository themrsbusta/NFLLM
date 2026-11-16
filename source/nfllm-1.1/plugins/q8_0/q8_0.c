#include "../../src/nfllm_registry.h"

#define Q8_0_BLOCK_ELEMENTS 32U
#define Q8_0_BLOCK_BYTES 34U

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

static nfllm_status_v1 NFLLM_CALL q8_0_dequantize_row(const void *opaque)
{
    const struct nfllm_q8_0_dequantize_row_args_v1 *args =
        (const struct nfllm_q8_0_dequantize_row_args_v1 *)opaque;
    uint32_t blocks, expected_bytes, block, i;
    nfllm_status_v1 status;

    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->packed_row || !args->output || args->columns == 0U ||
        (args->columns % Q8_0_BLOCK_ELEMENTS) != 0U ||
        (((uintptr_t)args->output & (sizeof(float) - 1U)) != 0U))
        return NFLLM_E_ARGUMENT;
    blocks = args->columns / Q8_0_BLOCK_ELEMENTS;
    if (blocks > 0xffffffffUL / Q8_0_BLOCK_BYTES) return NFLLM_E_OVERFLOW;
    expected_bytes = blocks * Q8_0_BLOCK_BYTES;
    if (args->packed_bytes != expected_bytes) return NFLLM_E_SIZE;

    /* Validate every scale before writing, so malformed input has no partial output. */
    for (block = 0U; block < blocks; ++block) {
        uint16_t half = read_u16_le(args->packed_row + block * Q8_0_BLOCK_BYTES);
        if ((half & 0x7c00U) == 0x7c00U) return NFLLM_E_NUMERIC;
    }
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * Q8_0_BLOCK_BYTES;
        float scale;
        status = half_to_float(read_u16_le(packed), &scale);
        if (status != NFLLM_OK) return status;
        for (i = 0U; i < Q8_0_BLOCK_ELEMENTS; ++i) {
            int8_t quant = (int8_t)packed[2U + i];
            args->output[block * Q8_0_BLOCK_ELEMENTS + i] =
                (float)quant * scale;
        }
    }
    return NFLLM_OK;
}

static nfllm_status_v1 NFLLM_CALL q8_0_matvec_row(const void *opaque)
{
    const struct nfllm_q8_0_matvec_row_args_v1 *args =
        (const struct nfllm_q8_0_matvec_row_args_v1 *)opaque;
    uint32_t blocks, block, i;
    float result = 0.0f;
    if (!args) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->packed_row || !args->input || !args->output ||
        args->columns == 0U || (args->columns % Q8_0_BLOCK_ELEMENTS) != 0U ||
        (((uintptr_t)args->input & (sizeof(float) - 1U)) != 0U) ||
        (((uintptr_t)args->output & (sizeof(float) - 1U)) != 0U))
        return NFLLM_E_ARGUMENT;
    blocks = args->columns / Q8_0_BLOCK_ELEMENTS;
    if (blocks > 0xffffffffUL / Q8_0_BLOCK_BYTES) return NFLLM_E_OVERFLOW;
    if (args->packed_bytes != blocks * Q8_0_BLOCK_BYTES) return NFLLM_E_SIZE;
    for (block = 0U; block < blocks; ++block) {
        uint16_t half = read_u16_le(args->packed_row + block * Q8_0_BLOCK_BYTES);
        if ((half & 0x7c00U) == 0x7c00U) return NFLLM_E_NUMERIC;
    }
    for (block = 0U; block < blocks; ++block) {
        const uint8_t *packed = args->packed_row + block * Q8_0_BLOCK_BYTES;
        float scale, block_sum = 0.0f;
        if (half_to_float(read_u16_le(packed), &scale) != NFLLM_OK)
            return NFLLM_E_NUMERIC;
        for (i = 0U; i < Q8_0_BLOCK_ELEMENTS; ++i) {
            int8_t quant = (int8_t)packed[2U + i];
            block_sum += (float)quant * args->input[block * Q8_0_BLOCK_ELEMENTS + i];
        }
        result += scale * block_sum;
    }
    *args->output = result;
    return NFLLM_OK;
}

static int32_t NFLLM_CALL q8_0_initialize(
    const struct nfllm_host_api_v1 *host, void **plugin_state)
{
    if (!host || !plugin_state) return NFLLM_E_ARGUMENT;
    *plugin_state = 0;
    return NFLLM_OK;
}

static void NFLLM_CALL q8_0_shutdown(void *plugin_state)
{
    (void)plugin_state;
}

static const struct nfllm_plugin_descriptor_v1 q8_0_plugin = {
    sizeof(struct nfllm_plugin_descriptor_v1), NFLLM_ABI_VERSION_1,
    NFLLM_PLUGIN_ID_Q8_0, NFLLM_CAP_386 | NFLLM_CAP_X87, 0U,
    "q8_0-x87-dequant", q8_0_initialize, q8_0_shutdown
};

static const struct nfllm_kernel_descriptor_v1 q8_0_kernels[] = {
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_DEQUANTIZE_ROW_Q8_0, NFLLM_VALUE_NONE, NFLLM_VALUE_F32, 0U,
      "q8_0.dequantize_row", q8_0_dequantize_row },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_Q8_0_ROW, NFLLM_VALUE_F32, NFLLM_VALUE_F32, 0U,
      "q8_0.matvec_row.direct_386", q8_0_matvec_row }
};

nfllm_status_v1 NFLLM_CALL nfllm_register_q8_0_plugin(void)
{
    return nfllm_registry_register(&q8_0_plugin, q8_0_kernels,
                                   sizeof(q8_0_kernels) / sizeof(q8_0_kernels[0]));
}
