#ifndef NFLLM_SIMD_QUANT_COMMON_H
#define NFLLM_SIMD_QUANT_COMMON_H

#define NFLLM_SIMD_QUANT_BLOCK_ELEMENTS 32U

static int nfllm_simd_float_finite(float value)
{
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return (bits.u & 0x7f800000UL) != 0x7f800000UL;
}

static uint16_t nfllm_simd_read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static nfllm_status_v1 nfllm_simd_half_to_float(uint16_t half, float *value)
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

static nfllm_status_v1 nfllm_simd_validate_matrix(
    const struct nfllm_quantized_matvec_matrix_args_v1 *args,
    uint32_t block_bytes, uint32_t *row_bytes)
{
    uint32_t blocks, row, block;
    if (!args || !row_bytes) return NFLLM_E_ARGUMENT;
    if (args->struct_size < sizeof(*args)) return NFLLM_E_SIZE;
    if (args->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!args->packed_matrix || !args->input || !args->output ||
        args->rows == 0U || args->columns == 0U ||
        (args->columns % NFLLM_SIMD_QUANT_BLOCK_ELEMENTS) != 0U ||
        (((uintptr_t)args->input & (sizeof(float) - 1U)) != 0U) ||
        (((uintptr_t)args->output & (sizeof(float) - 1U)) != 0U))
        return NFLLM_E_ARGUMENT;
    blocks = args->columns / NFLLM_SIMD_QUANT_BLOCK_ELEMENTS;
    if (blocks > 0xffffffffUL / block_bytes) return NFLLM_E_OVERFLOW;
    *row_bytes = blocks * block_bytes;
    if (args->rows > 0xffffffffUL / *row_bytes) return NFLLM_E_OVERFLOW;
    if (args->packed_bytes != args->rows * *row_bytes) return NFLLM_E_SIZE;
    for (row = 0U; row < args->rows; ++row) {
        const uint8_t *packed_row = args->packed_matrix + row * *row_bytes;
        for (block = 0U; block < blocks; ++block) {
            uint16_t scale = nfllm_simd_read_u16_le(
                packed_row + block * block_bytes);
            if ((scale & 0x7c00U) == 0x7c00U) return NFLLM_E_NUMERIC;
        }
    }
    return NFLLM_OK;
}

static nfllm_status_v1 nfllm_simd_quantize_input(
    const float *input, uint32_t columns, int16_t *quantized,
    float *input_scale)
{
    uint32_t i;
    float max_input = 0.0f;
    if (!input || !quantized || !input_scale) return NFLLM_E_ARGUMENT;
    for (i = 0U; i < columns; ++i) {
        float value = input[i];
        float magnitude;
        if (!nfllm_simd_float_finite(value)) return NFLLM_E_NUMERIC;
        magnitude = value < 0.0f ? -value : value;
        if (magnitude > max_input) max_input = magnitude;
    }
    if (max_input == 0.0f) {
        for (i = 0U; i < columns; ++i) quantized[i] = 0;
        *input_scale = 0.0f;
        return NFLLM_OK;
    }
    *input_scale = max_input / 32767.0f;
    if (*input_scale == 0.0f) *input_scale = max_input;
    for (i = 0U; i < columns; ++i) {
        float scaled = input[i] / *input_scale;
        int32_t rounded;
        if (scaled >= 32767.0f) rounded = 32767;
        else if (scaled <= -32767.0f) rounded = -32767;
        else rounded = (int32_t)(scaled + (scaled < 0.0f ? -0.5f : 0.5f));
        quantized[i] = (int16_t)rounded;
    }
    return NFLLM_OK;
}

static int nfllm_simd_commit_output(float *output, const float *temporary,
                                   uint32_t rows)
{
    uint32_t row;
    for (row = 0U; row < rows; ++row)
        if (!nfllm_simd_float_finite(temporary[row])) return 0;
    for (row = 0U; row < rows; ++row) output[row] = temporary[row];
    return 1;
}

#endif
