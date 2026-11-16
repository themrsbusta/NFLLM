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
    union { uint32_t u; float f; } nonfinite;
    int failures = 0;

    host.struct_size = sizeof(host);
    host.abi_version = NFLLM_ABI_VERSION_1;
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87 | NFLLM_CAP_MMX;
    host.context = 0;
    host.log = 0;
    if (nfllm_registry_initialize(&host) != NFLLM_OK ||
        nfllm_register_mmx_plugin() != NFLLM_OK ||
        nfllm_register_reference_plugin() != NFLLM_OK) {
        puts("FAIL: register MMX and reference plugins");
        return 1;
    }

    failures += check(nfllm_dispatcher_configure(
                          NFLLM_PLUGIN_ID_MMX, host.capabilities,
                          diagnostic, sizeof(diagnostic)) == NFLLM_OK &&
                      strstr(diagnostic, "mmx.matvec.f32.quantized-int8") != 0,
                      "manual MMX matvec selected");

    args.struct_size = sizeof(args);
    args.abi_version = NFLLM_ABI_VERSION_1;
    args.weights = weights;
    args.rows = 2U;
    args.columns = 9U;
    args.input = input;
    args.output = output;
    failures += check(nfllm_dispatch_matvec_f32(&args) == NFLLM_OK,
                      "MMX signed-byte dot executes for a complete group and odd tail");
    failures += check(fabs((double)output[0] - expected[0]) < 0.15,
                      "MMX row 0 remains within quantization tolerance");
    failures += check(fabs((double)output[1] - expected[1]) < 0.15,
                      "MMX row 1 remains within quantization tolerance");
    failures += nfllm_simd_test_quantized_matrices();

    nonfinite.u = 0x7f800000UL;
    {
        float invalid_input[9];
        memcpy(invalid_input, input, sizeof(input));
        invalid_input[3] = nonfinite.f;
        output[0] = -99.0f;
        args.input = invalid_input;
        failures += check(nfllm_dispatch_matvec_f32(&args) == NFLLM_E_NUMERIC &&
                          output[0] == -99.0f,
                          "non-finite input is rejected without partial output");
        args.input = input;
    }

    x87_value = x87_left * x87_right;
    failures += check(x87_value == 2.5,
                      "x87 arithmetic works after MMX EMMS cleanup");

    nfllm_registry_shutdown();
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
    if (nfllm_registry_initialize(&host) != NFLLM_OK ||
        nfllm_register_mmx_plugin() != NFLLM_OK ||
        nfllm_register_reference_plugin() != NFLLM_OK) {
        puts("FAIL: initialize 486 capability-filter check");
        return 1;
    }
    failures += check(nfllm_dispatcher_configure(
                          NFLLM_PLUGIN_ID_NONE, host.capabilities,
                          diagnostic, sizeof(diagnostic)) == NFLLM_OK &&
                      strstr(diagnostic, "reference.matvec.f32") != 0,
                      "386+x87 profile automatically falls back without executing MMX");
    nfllm_registry_shutdown();
    printf("MMX SELFTEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
