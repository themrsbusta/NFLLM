#include "../src/nfllm_registry.h"
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
    const float weights[10] = {
        1.0f, 2.0f, 3.0f, 4.0f, 5.0f,
        -0.5f, 0.25f, 1.5f, -2.0f, 3.0f
    };
    const float input[5] = {-1.0f, 0.5f, 2.0f, -0.25f, 0.75f};
    const float expected[2] = {8.75f, 6.375f};
    float output[2] = {0.0f, 0.0f};
    volatile double x87_left = 1.25;
    volatile double x87_right = 2.0;
    volatile double x87_value;
    struct nfllm_host_api_v1 host;
    struct nfllm_matvec_f32_args_v1 args;
    char diagnostic[NFLLM_DIAGNOSTIC_CAPACITY];
    int failures = 0;
    unsigned row;

    host.struct_size = sizeof(host);
    host.abi_version = NFLLM_ABI_VERSION_1;
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87 |
                        NFLLM_CAP_MMX | NFLLM_CAP_3DNOW;
    host.context = 0;
    host.log = 0;
    if (nfllm_registry_initialize(&host) != NFLLM_OK ||
        nfllm_register_3dnow_plugin() != NFLLM_OK ||
        nfllm_register_reference_plugin() != NFLLM_OK) {
        puts("FAIL: register 3DNow and reference plugins");
        return 1;
    }

    failures += check(nfllm_dispatcher_configure(
                          NFLLM_PLUGIN_ID_3DNOW, host.capabilities,
                          diagnostic, sizeof(diagnostic)) == NFLLM_OK &&
                      strstr(diagnostic, "3dnow.matvec.f32") != 0,
                      "manual 3DNow! matvec selected");

    args.struct_size = sizeof(args);
    args.abi_version = NFLLM_ABI_VERSION_1;
    args.weights = weights;
    args.rows = 2U;
    args.columns = 5U;
    args.input = input;
    args.output = output;
    failures += check(nfllm_dispatch_matvec_f32(&args) == NFLLM_OK,
                      "3DNow! matvec executes on the declared proxy");
    x87_value = x87_left * x87_right;
    for (row = 0U; row < 2U; ++row)
        failures += check(fabs((double)output[row] - expected[row]) < 1.0e-6,
                          row == 0U ? "odd-length row 0 matches scalar reference" :
                                      "odd-length row 1 matches scalar reference");
    failures += check(x87_value == 2.5,
                      "x87 arithmetic works after plugin EMMS cleanup");

    nfllm_registry_shutdown();
    printf("3DNOW SELFTEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
