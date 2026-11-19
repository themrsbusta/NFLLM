#ifndef NFLLM_SIMD_QUANT_TEST_H
#define NFLLM_SIMD_QUANT_TEST_H

#include "../src/nfllm_registry.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int nfllm_simd_test_check(int condition, const char *name)
{
    printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    return condition ? 0 : 1;
}

static void nfllm_simd_test_store_scale(uint8_t *block)
{
    block[0] = 0x00U;
    block[1] = 0x3cU;
}

static int nfllm_simd_test_quantized_matrices(void)
{
    uint8_t q8[136];
    uint8_t q4[72];
    float input[64];
    float output[2];
    struct nfllm_quantized_matvec_matrix_args_v1 args;
    uint32_t row, block, i;
    int failures = 0;
    union { uint32_t u; float f; } nonfinite;

    for (i = 0U; i < 64U; ++i)
        input[i] = ((int32_t)(i % 13U) - 6) * 0.17f;
    for (row = 0U; row < 2U; ++row) {
        for (block = 0U; block < 2U; ++block) {
            uint8_t *q8_block = q8 + (row * 2U + block) * 34U;
            uint8_t *q4_block = q4 + (row * 2U + block) * 18U;
            nfllm_simd_test_store_scale(q8_block);
            nfllm_simd_test_store_scale(q4_block);
            for (i = 0U; i < 32U; ++i) {
                int32_t q = (int32_t)((i * 7U + row * 5U + block * 3U) % 31U) - 15;
                q8_block[2U + i] = (uint8_t)q;
            }
            for (i = 0U; i < 16U; ++i) {
                uint8_t low = (uint8_t)((i * 3U + row + block) % 16U);
                uint8_t high = (uint8_t)((i * 5U + row * 2U + block) % 16U);
                q4_block[2U + i] = (uint8_t)(low | (uint8_t)(high << 4));
            }
        }
    }

    args.struct_size = sizeof(args);
    args.abi_version = NFLLM_ABI_VERSION_1;
    args.rows = 2U;
    args.columns = 64U;
    args.input = input;
    args.output = output;

    args.packed_matrix = q8;
    args.packed_bytes = sizeof(q8);
    output[0] = output[1] = -99.0f;
    failures += nfllm_simd_test_check(
        nfllm_dispatch_matvec_q8_0_matrix(&args) == NFLLM_OK,
        "Q8_0 SIMD matrix kernel processes multiple packed rows");
    for (row = 0U; row < 2U; ++row) {
        double expected = 0.0;
        for (block = 0U; block < 2U; ++block) {
            const uint8_t *packed = q8 + (row * 2U + block) * 34U;
            for (i = 0U; i < 32U; ++i) {
                int32_t q = (int32_t)packed[2U + i];
                if (q >= 128) q -= 256;
                expected += (double)q * input[block * 32U + i];
            }
        }
        failures += nfllm_simd_test_check(
            fabs((double)output[row] - expected) < 0.02,
            row == 0U ? "Q8_0 row 0 stays within quantized-input tolerance" :
                        "Q8_0 row 1 stays within quantized-input tolerance");
    }
    q8[34U + 1U] = 0x7cU;
    output[0] = output[1] = -99.0f;
    failures += nfllm_simd_test_check(
        nfllm_dispatch_matvec_q8_0_matrix(&args) == NFLLM_E_NUMERIC &&
        output[0] == -99.0f && output[1] == -99.0f,
        "Q8_0 invalid scale is rejected before any output is written");
    q8[34U + 1U] = 0x3cU;
    args.packed_bytes = sizeof(q8) - 1U;
    failures += nfllm_simd_test_check(
        nfllm_dispatch_matvec_q8_0_matrix(&args) == NFLLM_E_SIZE,
        "Q8_0 matrix rejects truncated packed input");
    args.packed_bytes = sizeof(q8);

    args.packed_matrix = q4;
    args.packed_bytes = sizeof(q4);
    output[0] = output[1] = -99.0f;
    failures += nfllm_simd_test_check(
        nfllm_dispatch_matvec_q4_0_matrix(&args) == NFLLM_OK,
        "Q4_0 SIMD matrix kernel processes multiple packed rows");
    for (row = 0U; row < 2U; ++row) {
        double expected = 0.0;
        for (block = 0U; block < 2U; ++block) {
            const uint8_t *packed = q4 + (row * 2U + block) * 18U;
            for (i = 0U; i < 16U; ++i) {
                int32_t low = (int32_t)(packed[2U + i] & 0x0fU) - 8;
                int32_t high = (int32_t)(packed[2U + i] >> 4) - 8;
                expected += (double)low * input[block * 32U + i];
                expected += (double)high * input[block * 32U + i + 16U];
            }
        }
        failures += nfllm_simd_test_check(
            fabs((double)output[row] - expected) < 0.02,
            row == 0U ? "Q4_0 row 0 stays within quantized-input tolerance" :
                        "Q4_0 row 1 stays within quantized-input tolerance");
    }
    q4[18U + 1U] = 0x7cU;
    output[0] = output[1] = -99.0f;
    failures += nfllm_simd_test_check(
        nfllm_dispatch_matvec_q4_0_matrix(&args) == NFLLM_E_NUMERIC &&
        output[0] == -99.0f && output[1] == -99.0f,
        "Q4_0 invalid scale is rejected before any output is written");
    q4[18U + 1U] = 0x3cU;

    nonfinite.u = 0x7f800000UL;
    input[0] = nonfinite.f;
    output[0] = output[1] = -99.0f;
    failures += nfllm_simd_test_check(
        nfllm_dispatch_matvec_q4_0_matrix(&args) == NFLLM_E_NUMERIC &&
        output[0] == -99.0f && output[1] == -99.0f,
        "non-finite activation is rejected without partial matrix output");

    return failures;
}

#endif
