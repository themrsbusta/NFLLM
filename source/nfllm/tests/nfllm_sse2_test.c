#include "../src/nfllm_registry.h"
#include "nfllm_simd_quant_test.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int check(int condition, const char *name)
{
    printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    return condition ? 0 : 1;
}

int main(void)
{
    const float weights[18] = {
        1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f,
        -0.5f, 0.25f, 1.5f, -2.0f, 3.0f, 1.0f, -1.0f, 0.5f, 2.0f
    };
    const float input[9] = {
        -1.0f, 0.5f, 2.0f, -0.25f, 0.75f, -1.5f, 0.25f, 2.5f, -0.5f
    };
    const double expected[2] = {17.0, 4.875};
    float output[2] = {-99.0f, -99.0f};
    volatile double x87_left = 1.25;
    volatile double x87_right = 2.0;
    volatile double x87_value;
    struct nfllm_host_api_v1 host;
    struct nfllm_matvec_f32_args_v1 args;
    char diagnostic[NFLLM_DIAGNOSTIC_CAPACITY];
    uint32_t row;
    int failures = 0;

    host.struct_size = sizeof(host);
    host.abi_version = NFLLM_ABI_VERSION_1;
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87 |
                        NFLLM_CAP_SSE | NFLLM_CAP_SSE2;
    host.context = 0;
    host.log = 0;
    if (nfllm_registry_initialize(&host) != NFLLM_OK ||
        nfllm_register_sse2_plugin() != NFLLM_OK ||
        nfllm_register_reference_plugin() != NFLLM_OK) {
        puts("FAIL: register SSE2 and reference plugins");
        return 1;
    }
    failures += check(nfllm_dispatcher_configure(
                          NFLLM_PLUGIN_ID_SSE2, host.capabilities,
                          diagnostic, sizeof(diagnostic)) == NFLLM_OK &&
                      strstr(diagnostic, "sse2.matvec.f32") != 0,
                      "manual SSE2 matvec selected after capability declaration");
    args.struct_size = sizeof(args);
    args.abi_version = NFLLM_ABI_VERSION_1;
    args.weights = weights;
    args.rows = 2U;
    args.columns = 9U;
    args.input = input;
    args.output = output;
    failures += check(nfllm_dispatch_matvec_f32(&args) == NFLLM_OK,
                      "SSE2 matvec executes packed groups and odd scalar tail");
    for (row = 0U; row < 2U; ++row)
        failures += check(fabs((double)output[row] - expected[row]) < 1.0e-5,
                          row == 0U ? "SSE2 row 0 is within F32 tolerance" :
                                      "SSE2 row 1 is within F32 tolerance");
    failures += nfllm_simd_test_quantized_matrices();
    x87_value = x87_left * x87_right;
    failures += check(x87_value == 2.5,
                      "x87 arithmetic remains available after SSE2 kernel");
    nfllm_registry_shutdown();

    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87 | NFLLM_CAP_MMX;
    if (nfllm_registry_initialize(&host) != NFLLM_OK ||
        nfllm_register_sse2_plugin() != NFLLM_OK ||
        nfllm_register_reference_plugin() != NFLLM_OK) {
        puts("FAIL: initialize safe non-SSE fallback check");
        return 1;
    }
    failures += check(nfllm_dispatcher_configure(
                          NFLLM_PLUGIN_ID_NONE, host.capabilities,
                          diagnostic, sizeof(diagnostic)) == NFLLM_OK &&
                      strstr(diagnostic, "reference.matvec.f32") != 0,
                      "386+x87 fallback does not select or execute SSE2");
    nfllm_registry_shutdown();
    printf("SSE2 SELFTEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
