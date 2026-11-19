#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/nfllm_logic.h"
#include "../src/nfllm_registry.h"

static int failures;
static uint32_t initialize_count;
static uint32_t shutdown_count;

static void check(int condition, const char *name)
{
    if (condition) printf("PASS: %s\n", name);
    else { printf("FAIL: %s\n", name); ++failures; }
}

static int32_t NFLLM_CALL dummy_kernel(const void *arguments)
{
    return arguments ? NFLLM_OK : NFLLM_E_ARGUMENT;
}

static int32_t NFLLM_CALL tracked_initialize(
    const struct nfllm_host_api_v1 *host, void **plugin_state)
{
    if (!host || !plugin_state) return NFLLM_E_ARGUMENT;
    ++initialize_count;
    *plugin_state = &initialize_count;
    return NFLLM_OK;
}

static void NFLLM_CALL tracked_shutdown(void *plugin_state)
{
    if (plugin_state == &initialize_count) ++shutdown_count;
}

static int test_logic(void)
{
    static const uint32_t values[] = {
        0U, 1U, 2U, 3U, 255U, 256U, 0x7fffffffUL,
        0x80000000UL, 0xfffffffeUL, 0xffffffffUL
    };
    uint32_t i, j, result, quotient, remainder;
    int ok = 1;
    for (i = 0U; i < 256U; ++i) {
        for (j = 0U; j < 256U; ++j) {
            if (nfllm_logic_and_u32(i, j) != (i & j) ||
                nfllm_logic_or_u32(i, j) != (i | j) ||
                nfllm_logic_xor_u32(i, j) != (i ^ j)) ok = 0;
        }
    }
    check(ok && nfllm_logic_not_u32(0U) == 0xffffffffUL &&
          nfllm_logic_not_u32(0xffffffffUL) == 0U,
          "boolean operations exhaustive over 8-bit inputs");

    ok = nfllm_logic_shl_u32(0x80000001UL, 1U, &result) == NFLLM_OK &&
         result == 2U &&
         nfllm_logic_shr_u32(0x80000001UL, 31U, &result) == NFLLM_OK &&
         result == 1U &&
         nfllm_logic_shl_u32(1U, 32U, &result) == NFLLM_E_SHIFT_RANGE &&
         result == 0U;
    check(ok, "logical shifts and invalid shift count");

    ok = 1;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (j = 0U; j < sizeof(values) / sizeof(values[0]); ++j) {
            uint32_t expected;
            nfllm_status_v1 status;
            expected = values[i] + values[j];
            status = nfllm_logic_add_u32(values[i], values[j], &result);
            if (result != expected || status !=
                (values[i] > 0xffffffffUL - values[j] ? NFLLM_E_OVERFLOW : NFLLM_OK))
                ok = 0;
            expected = values[i] - values[j];
            status = nfllm_logic_sub_u32(values[i], values[j], &result);
            if (result != expected || status !=
                (values[i] < values[j] ? NFLLM_E_OVERFLOW : NFLLM_OK)) ok = 0;
            expected = values[i] * values[j];
            status = nfllm_logic_mul_u32(values[i], values[j], &result);
            if (result != expected || status !=
                ((values[i] != 0U && values[j] > 0xffffffffUL / values[i]) ?
                 NFLLM_E_OVERFLOW : NFLLM_OK)) ok = 0;
            if (values[j] != 0U) {
                status = nfllm_logic_div_u32(values[i], values[j],
                                             &quotient, &remainder);
                if (status != NFLLM_OK || quotient != values[i] / values[j] ||
                    remainder != values[i] % values[j]) ok = 0;
            }
        }
    }
    check(ok, "u32 arithmetic, overflow flags, and division vectors");
    check(nfllm_logic_div_u32(1U, 0U, &quotient, &remainder) == NFLLM_E_DIV_ZERO &&
          quotient == 0U && remainder == 0U,
          "division by zero returns deterministic error outputs");
    check(nfllm_logic_compare_u32(1U, 2U) < 0 &&
          nfllm_logic_compare_u32(2U, 2U) == 0 &&
          nfllm_logic_compare_u32(3U, 2U) > 0,
          "unsigned comparison");

    ok = nfllm_logic_u32_to_f32_bits(0U) == 0U &&
         nfllm_logic_u32_to_f32_bits(1U) == 0x3f800000UL &&
         nfllm_logic_u32_to_f32_bits(16777217UL) == 0x4b800000UL &&
         nfllm_logic_u32_to_f32_bits(16777219UL) == 0x4b800002UL &&
         nfllm_logic_u32_to_f32_bits(0xffffffffUL) == 0x4f800000UL;
    check(ok, "u32 to IEEE binary32 round-to-nearest-even conversion");

    ok = nfllm_logic_f32_bits_to_u32(0x3fc00000UL, &result) == NFLLM_OK &&
         result == 1U &&
         nfllm_logic_f32_bits_to_u32(0x4f7fffffUL, &result) == NFLLM_OK &&
         result == 0xffffff00UL &&
         nfllm_logic_f32_bits_to_u32(0x80000000UL, &result) == NFLLM_OK &&
         result == 0U &&
         nfllm_logic_f32_bits_to_u32(0xbf800000UL, &result) == NFLLM_E_NUMERIC &&
         nfllm_logic_f32_bits_to_u32(0x7f800000UL, &result) == NFLLM_E_NUMERIC &&
         nfllm_logic_f32_bits_to_u32(0x4f800000UL, &result) == NFLLM_E_OVERFLOW;
    check(ok, "IEEE binary32 to u32 conversion and range errors");
    return ok;
}

static int test_registry_capacity(void)
{
    enum { TEST_PLUGIN_COUNT = 40U };
    struct nfllm_host_api_v1 host;
    static struct nfllm_plugin_descriptor_v1 plugins[TEST_PLUGIN_COUNT + 2U];
    static struct nfllm_kernel_descriptor_v1 kernels[TEST_PLUGIN_COUNT + 2U];
    const struct nfllm_kernel_descriptor_v1 *selected;
    uint32_t selected_id;
    uint32_t p;
    char diagnostic[NFLLM_DIAGNOSTIC_CAPACITY];
    int ok;
    memset(&host, 0, sizeof(host));
    host.struct_size = sizeof(host);
    host.abi_version = NFLLM_ABI_VERSION_1;
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
    ok = nfllm_registry_initialize(&host) == NFLLM_OK;

    for (p = 0U; p < TEST_PLUGIN_COUNT + 2U; ++p) {
        plugins[p].struct_size = sizeof(plugins[p]);
        plugins[p].abi_version = NFLLM_ABI_VERSION_1;
        plugins[p].plugin_id = 0x71000000UL + p;
        plugins[p].required_capabilities = 0U;
        plugins[p].flags = 0U;
        plugins[p].name = "capacity-test";
        plugins[p].initialize = 0;
        plugins[p].shutdown = 0;
        kernels[p].struct_size = sizeof(kernels[p]);
        kernels[p].abi_version = NFLLM_ABI_VERSION_1;
        kernels[p].operation_id = 0x20000000UL + p;
        kernels[p].input_type = NFLLM_VALUE_U32;
        kernels[p].output_type = NFLLM_VALUE_U32;
        kernels[p].required_capabilities = 0U;
        kernels[p].name = "capacity-test.kernel";
        kernels[p].execute = dummy_kernel;
    }

    for (p = 0U; p < TEST_PLUGIN_COUNT; ++p)
        if (nfllm_registry_register_priority(
                &plugins[p], &kernels[p], 1U, 100) != NFLLM_OK)
            ok = 0;

    for (p = 0U; p < TEST_PLUGIN_COUNT; ++p) {
        uint32_t operation = 0x20000000UL + p;
        if (nfllm_registry_resolve(operation, plugins[p].plugin_id, 0U,
                                   &selected, &selected_id, diagnostic,
                                   sizeof(diagnostic)) != NFLLM_OK ||
            selected_id != plugins[p].plugin_id || !selected)
            ok = 0;
    }

    plugins[TEST_PLUGIN_COUNT].plugin_id = 0x72000000UL;
    kernels[TEST_PLUGIN_COUNT].operation_id = 0x21000000UL;
    plugins[TEST_PLUGIN_COUNT + 1U].plugin_id = 0x72000001UL;
    kernels[TEST_PLUGIN_COUNT + 1U].operation_id = 0x21000000UL;
    if (nfllm_registry_register_priority(&plugins[TEST_PLUGIN_COUNT],
            &kernels[TEST_PLUGIN_COUNT], 1U, 200) != NFLLM_OK ||
        nfllm_registry_register_priority(&plugins[TEST_PLUGIN_COUNT + 1U],
            &kernels[TEST_PLUGIN_COUNT + 1U], 1U, 10) != NFLLM_OK ||
        nfllm_registry_resolve(0x21000000UL, NFLLM_PLUGIN_ID_NONE, 0U,
            &selected, &selected_id, diagnostic, sizeof(diagnostic)) != NFLLM_OK ||
        selected_id != plugins[TEST_PLUGIN_COUNT + 1U].plugin_id)
        ok = 0;
    check(ok, "registry grows past 8 entries and selects by configured priority");
    nfllm_registry_shutdown();
    return ok;
}

static void legacy_matvec(const float *weights, uint32_t rows,
                          uint32_t columns, const float *input, float *output)
{
    uint32_t row, column;
    for (row = 0U; row < rows; ++row) {
        const float *w = weights + row * columns;
        float sum = 0.0f;
        for (column = 0U; column < columns; ++column)
            sum += w[column] * input[column];
        output[row] = sum;
    }
}

static void legacy_rms(const float *input, const float *weight,
                       uint32_t count, float epsilon, float *output)
{
    uint32_t i;
    double sum = 0.0;
    float inverse;
    for (i = 0U; i < count; ++i) sum += (double)input[i] * input[i];
    inverse = (float)(1.0 / sqrt(sum / count + epsilon));
    for (i = 0U; i < count; ++i) output[i] = input[i] * inverse * weight[i];
}

static void legacy_rope(float *vector, uint32_t heads, uint32_t head_dim,
                        uint32_t rope_dim, uint32_t position, float base)
{
    uint32_t head, pair;
    double step;
    if (rope_dim == 0U) return;
    step = pow((double)base, -2.0 / rope_dim);
    for (head = 0U; head < heads; ++head) {
        float frequency = 1.0f;
        for (pair = 0U; pair < rope_dim / 2U; ++pair) {
            uint32_t offset = head * head_dim + pair * 2U;
            double angle = (double)position * frequency;
            float c = (float)cos(angle);
            float s = (float)sin(angle);
            float a = vector[offset];
            float b = vector[offset + 1U];
            vector[offset] = a * c - b * s;
            vector[offset + 1U] = a * s + b * c;
            frequency = (float)(frequency * step);
        }
    }
}

static int test_registry_and_equivalence(void)
{
    struct nfllm_host_api_v1 host;
    struct nfllm_plugin_descriptor_v1 plugin_a;
    struct nfllm_plugin_descriptor_v1 plugin_b;
    struct nfllm_plugin_descriptor_v1 invalid_plugin;
    struct nfllm_kernel_descriptor_v1 kernel_a;
    struct nfllm_kernel_descriptor_v1 kernel_b;
    struct nfllm_kernel_descriptor_v1 invalid_kernel;
    struct nfllm_kernel_descriptor_v1 duplicate_kernels[2];
    const struct nfllm_kernel_descriptor_v1 *selected;
    uint32_t selected_id;
    char diagnostic[NFLLM_DIAGNOSTIC_CAPACITY];
    nfllm_status_v1 status;
    int ok = 1;
    float weights[6] = { 1.0f, 2.0f, 3.0f, -1.0f, 0.5f, 4.0f };
    float input[3] = { 2.0f, -3.0f, 0.25f };
    float expected[2], actual[2];
    float rms_input[4] = { 0.25f, -1.5f, 2.0f, 4.0f };
    float rms_weight[4] = { 1.0f, 0.75f, -0.5f, 2.0f };
    float expected_rms[4], actual_rms[4];
    float expected_rope[8] = { 1.0f, 0.25f, -2.0f, 3.0f,
                               0.5f, -1.0f, 2.5f, 4.0f };
    float actual_rope[8];
    struct nfllm_matvec_f32_args_v1 matvec_args;
    struct nfllm_rms_norm_f32_args_v1 rms_args;
    struct nfllm_rope_f32_args_v1 rope_args;
    char misaligned_storage[sizeof(float) * 2U];

    memset(&host, 0, sizeof(host));
    host.struct_size = sizeof(host);
    host.abi_version = NFLLM_ABI_VERSION_1;
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
    check(nfllm_registry_initialize(&host) == NFLLM_OK,
          "registry accepts host ABI v1");

    memset(&plugin_a, 0, sizeof(plugin_a));
    plugin_a.struct_size = sizeof(plugin_a);
    plugin_a.abi_version = NFLLM_ABI_VERSION_1;
    plugin_a.plugin_id = 0x54455331UL;
    plugin_a.required_capabilities = NFLLM_CAP_X87;
    plugin_a.name = "fallback-test-first";
    plugin_a.initialize = tracked_initialize;
    plugin_a.shutdown = tracked_shutdown;
    memset(&kernel_a, 0, sizeof(kernel_a));
    kernel_a.struct_size = sizeof(kernel_a);
    kernel_a.abi_version = NFLLM_ABI_VERSION_1;
    kernel_a.operation_id = NFLLM_OP_MATVEC_F32;
    kernel_a.input_type = NFLLM_VALUE_F32;
    kernel_a.output_type = NFLLM_VALUE_F32;
    kernel_a.name = "test.incompatible";
    kernel_a.execute = dummy_kernel;

    invalid_plugin = plugin_a;
    invalid_plugin.struct_size = 0U;
    check(nfllm_registry_register(&invalid_plugin, &kernel_a, 1U) ==
          NFLLM_E_SIZE,
          "plugin descriptor size rejected");
    invalid_plugin = plugin_a;
    invalid_plugin.abi_version = 99U;
    check(nfllm_registry_register(&invalid_plugin, &kernel_a, 1U) ==
          NFLLM_E_ABI,
          "plugin ABI version rejected");
    invalid_plugin = plugin_a;
    invalid_plugin.required_capabilities = 0x80000000UL;
    check(nfllm_registry_register(&invalid_plugin, &kernel_a, 1U) ==
          NFLLM_E_UNSUPPORTED,
          "unknown plugin capability rejected");
    invalid_kernel = kernel_a;
    invalid_kernel.struct_size = 0U;
    check(nfllm_registry_register(&plugin_a, &invalid_kernel, 1U) ==
          NFLLM_E_SIZE,
          "kernel descriptor size rejected");
    invalid_kernel = kernel_a;
    invalid_kernel.abi_version = 99U;
    check(nfllm_registry_register(&plugin_a, &invalid_kernel, 1U) ==
          NFLLM_E_ABI,
          "kernel ABI version rejected");
    invalid_kernel = kernel_a;
    invalid_kernel.input_type = NFLLM_VALUE_I32;
    check(nfllm_registry_register(&plugin_a, &invalid_kernel, 1U) ==
          NFLLM_E_UNSUPPORTED,
          "F32 operation descriptor type mismatch rejected");
    invalid_kernel = kernel_a;
    invalid_kernel.execute = 0;
    check(nfllm_registry_register(&plugin_a, &invalid_kernel, 1U) ==
          NFLLM_E_ARGUMENT,
          "missing kernel callback rejected");

    plugin_b = plugin_a;
    plugin_b.plugin_id = 0x54455332UL;
    plugin_b.required_capabilities = 0U;
    plugin_b.name = "fallback-test-second";
    plugin_b.initialize = 0;
    plugin_b.shutdown = 0;
    kernel_b = kernel_a;
    kernel_b.name = "test.fallback";
    check(nfllm_registry_register(&plugin_a, &kernel_a, 1U) == NFLLM_OK &&
          nfllm_registry_register(&plugin_b, &kernel_b, 1U) == NFLLM_OK,
          "static plugin registration and initialization callbacks");
    status = nfllm_registry_resolve(NFLLM_OP_MATVEC_F32, NFLLM_PLUGIN_ID_NONE,
                                    0U, &selected, &selected_id,
                                    diagnostic, sizeof(diagnostic));
    check(status == NFLLM_OK && selected_id == plugin_b.plugin_id &&
          strstr(diagnostic, "fallback") != 0,
          "automatic dispatcher falls back after capability rejection");
    check(nfllm_registry_resolve(NFLLM_OP_MATVEC_F32, plugin_a.plugin_id, 0U,
                                 &selected, &selected_id, diagnostic,
                                 sizeof(diagnostic)) == NFLLM_E_CAPABILITY,
          "manual backend selection rejects missing capabilities");
    check(nfllm_registry_register(&plugin_a, &kernel_a, 1U) == NFLLM_E_DUPLICATE,
          "duplicate plugin ID rejected");
    duplicate_kernels[0] = kernel_a;
    duplicate_kernels[1] = kernel_a;
    plugin_b.plugin_id = 0x54455333UL;
    check(nfllm_registry_register(&plugin_b, duplicate_kernels, 2U) ==
          NFLLM_E_DUPLICATE,
          "duplicate operation within a plugin rejected");
    check(initialize_count == 1U, "plugin initialize called once");
    nfllm_registry_shutdown();
    check(shutdown_count == 1U, "plugin shutdown called once");

    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87 |
                        NFLLM_CAP_MMX | NFLLM_CAP_3DNOW;
    check(nfllm_registry_initialize(&host) == NFLLM_OK &&
          nfllm_register_3dnow_plugin() == NFLLM_OK &&
          nfllm_register_reference_plugin() == NFLLM_OK,
          "3DNow and reference backends registration");
    check(nfllm_dispatcher_configure(NFLLM_PLUGIN_ID_3DNOW,
                                     host.capabilities, diagnostic,
                                     sizeof(diagnostic)) == NFLLM_OK &&
          strstr(diagnostic, "3dnow.matvec.f32") != 0 &&
          strstr(diagnostic, "reference.rms_norm.f32") != 0 &&
          strstr(diagnostic, "reference.rope.f32") != 0,
          "manual 3DNow matvec selection keeps x87 RMSNorm and RoPE fallback");
    nfllm_registry_shutdown();

    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
    check(nfllm_registry_initialize(&host) == NFLLM_OK &&
          nfllm_register_reference_plugin() == NFLLM_OK,
          "reference backend registration");
    check(nfllm_dispatcher_configure(NFLLM_PLUGIN_ID_NONE,
                                     host.capabilities, diagnostic,
                                     sizeof(diagnostic)) == NFLLM_OK,
          "automatic dispatcher configuration");
    check(nfllm_dispatcher_configure(NFLLM_PLUGIN_ID_REFERENCE,
                                     host.capabilities, diagnostic,
                                     sizeof(diagnostic)) == NFLLM_OK,
          "manual reference backend configuration");
    check(nfllm_dispatcher_configure(0x11111111UL, host.capabilities,
                                     diagnostic, sizeof(diagnostic)) ==
          NFLLM_E_NOT_FOUND,
          "unknown manual backend rejected");
    check(nfllm_dispatcher_configure(NFLLM_PLUGIN_ID_NONE, NFLLM_CAP_386,
                                     diagnostic, sizeof(diagnostic)) ==
          NFLLM_E_CAPABILITY,
          "automatic mode refuses incompatible reference");
    check(nfllm_dispatcher_configure(NFLLM_PLUGIN_ID_REFERENCE,
                                     host.capabilities, diagnostic,
                                     sizeof(diagnostic)) == NFLLM_OK,
          "dispatcher can be safely reconfigured after rejection");

    legacy_matvec(weights, 2U, 3U, input, expected);
    memset(&matvec_args, 0, sizeof(matvec_args));
    matvec_args.struct_size = sizeof(matvec_args);
    matvec_args.abi_version = NFLLM_ABI_VERSION_1;
    matvec_args.weights = weights;
    matvec_args.rows = 2U;
    matvec_args.columns = 3U;
    matvec_args.input = input;
    matvec_args.output = actual;
    status = nfllm_dispatch_matvec_f32(&matvec_args);
    check(status == NFLLM_OK && memcmp(expected, actual, sizeof(actual)) == 0,
          "dispatcher matvec bitwise equivalent to NFLLM 0.1");

    legacy_rms(rms_input, rms_weight, 4U, 1.0e-5f, expected_rms);
    memset(&rms_args, 0, sizeof(rms_args));
    rms_args.struct_size = sizeof(rms_args);
    rms_args.abi_version = NFLLM_ABI_VERSION_1;
    rms_args.input = rms_input;
    rms_args.weight = rms_weight;
    rms_args.count = 4U;
    rms_args.epsilon = 1.0e-5f;
    rms_args.output = actual_rms;
    status = nfllm_dispatch_rms_norm_f32(&rms_args);
    check(status == NFLLM_OK &&
          memcmp(expected_rms, actual_rms, sizeof(actual_rms)) == 0,
          "dispatcher RMSNorm bitwise equivalent to NFLLM 0.1");

    memcpy(actual_rope, expected_rope, sizeof(actual_rope));
    legacy_rope(expected_rope, 2U, 4U, 4U, 7U, 10000.0f);
    memset(&rope_args, 0, sizeof(rope_args));
    rope_args.struct_size = sizeof(rope_args);
    rope_args.abi_version = NFLLM_ABI_VERSION_1;
    rope_args.vector = actual_rope;
    rope_args.heads = 2U;
    rope_args.head_dim = 4U;
    rope_args.rope_dim = 4U;
    rope_args.position = 7U;
    rope_args.base = 10000.0f;
    status = nfllm_dispatch_rope_f32(&rope_args);
    check(status == NFLLM_OK &&
          memcmp(expected_rope, actual_rope, sizeof(actual_rope)) == 0,
          "dispatcher RoPE bitwise equivalent to NFLLM 0.1");

    matvec_args.abi_version = 99U;
    check(nfllm_dispatch_matvec_f32(&matvec_args) == NFLLM_E_ABI,
          "kernel callback rejects incompatible argument ABI");
    matvec_args.abi_version = NFLLM_ABI_VERSION_1;
    matvec_args.struct_size = sizeof(matvec_args) - 1U;
    check(nfllm_dispatch_matvec_f32(&matvec_args) == NFLLM_E_SIZE,
          "dispatcher rejects short kernel argument struct");
    matvec_args.struct_size = sizeof(matvec_args);
    matvec_args.input = (const float *)(misaligned_storage + 1);
    check(nfllm_dispatch_matvec_f32(&matvec_args) == NFLLM_E_ARGUMENT,
          "dispatcher rejects misaligned F32 input");
    matvec_args.input = input;
    nfllm_registry_shutdown();
    return failures == 0;
}

static void write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)(value >> 8);
}

static int test_quantized_row_decoders(void)
{
    struct nfllm_host_api_v1 host;
    struct nfllm_q8_0_dequantize_row_args_v1 q8_args;
    struct nfllm_q4_0_dequantize_row_args_v1 q4_args;
    struct nfllm_q2_0_dequantize_row_args_v1 q20_args;
    struct nfllm_q2_k_dequantize_row_args_v1 q2k_args;
    struct nfllm_iq3_xxs_dequantize_row_args_v1 iq3xxs_args;
    struct nfllm_iq3_s_dequantize_row_args_v1 iq3s_args;
    uint8_t q8_packed[68];
    uint8_t q4_packed[36];
    uint8_t q20_packed[18];
    uint8_t q2k_packed[84];
    uint8_t iq3xxs_packed[98];
    uint8_t iq3s_packed[110];
    float q8_output[64], q8_expected[64];
    float q4_output[64], q4_expected[64];
    float q20_output[64], q20_expected[64];
    float q2k_output[256], q2k_expected[256];
    float iq3xxs_output[256], iq3xxs_expected[256];
    float iq3s_output[256], iq3s_expected[256];
    uint32_t i;
    int ok = 1;

    memset(&host, 0, sizeof(host));
    host.struct_size = sizeof(host);
    host.abi_version = NFLLM_ABI_VERSION_1;
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
    check(nfllm_registry_initialize(&host) == NFLLM_OK &&
          nfllm_register_q8_0_plugin() == NFLLM_OK &&
          nfllm_register_q4_0_plugin() == NFLLM_OK &&
          nfllm_register_q2_plugin() == NFLLM_OK &&
          nfllm_register_iq3_plugin() == NFLLM_OK,
          "Q8/Q4/Q2/IQ3 static decoder plugins register independently");

    memset(q8_packed, 0, sizeof(q8_packed));
    write_u16_le(q8_packed, 0x3c00U); /* 1.0 */
    write_u16_le(q8_packed + 34U, 0x3800U); /* 0.5 */
    for (i = 0U; i < 32U; ++i) {
        int8_t quant = (int8_t)((int32_t)i - 16);
        q8_packed[2U + i] = (uint8_t)quant;
        q8_packed[36U + i] = (uint8_t)quant;
        q8_expected[i] = (float)quant;
        q8_expected[32U + i] = (float)quant * 0.5f;
    }
    memset(&q8_args, 0, sizeof(q8_args));
    q8_args.struct_size = sizeof(q8_args);
    q8_args.abi_version = NFLLM_ABI_VERSION_1;
    q8_args.packed_row = q8_packed;
    q8_args.packed_bytes = sizeof(q8_packed);
    q8_args.columns = 64U;
    q8_args.output = q8_output;
    check(nfllm_dispatch_dequantize_q8_0_row(&q8_args) == NFLLM_OK &&
          memcmp(q8_output, q8_expected, sizeof(q8_output)) == 0,
          "Q8_0 signed values and per-block FP16 scales decode exactly");

    q8_args.columns = 63U;
    check(nfllm_dispatch_dequantize_q8_0_row(&q8_args) == NFLLM_E_ARGUMENT,
          "Q8_0 decoder rejects rows not divisible by 32");
    q8_args.columns = 64U;
    q8_args.packed_bytes = sizeof(q8_packed) - 1U;
    check(nfllm_dispatch_dequantize_q8_0_row(&q8_args) == NFLLM_E_SIZE,
          "Q8_0 decoder rejects incorrect packed row size");
    q8_args.packed_bytes = sizeof(q8_packed);
    q8_args.abi_version = 99U;
    check(nfllm_dispatch_dequantize_q8_0_row(&q8_args) == NFLLM_E_ABI,
          "Q8_0 decoder rejects incompatible ABI version");
    q8_args.abi_version = NFLLM_ABI_VERSION_1;
    write_u16_le(q8_packed, 0x7c00U); /* infinity is invalid as a scale */
    q8_output[0] = 123.0f;
    check(nfllm_dispatch_dequantize_q8_0_row(&q8_args) == NFLLM_E_NUMERIC &&
          q8_output[0] == 123.0f,
          "Q8_0 decoder rejects non-finite scales without partial output");
    write_u16_le(q8_packed, 0x0001U); /* smallest positive FP16 subnormal */
    q8_packed[2] = 1U;
    q8_args.columns = 32U;
    q8_args.packed_bytes = 34U;
    check(nfllm_dispatch_dequantize_q8_0_row(&q8_args) == NFLLM_OK &&
          q8_output[0] == 5.9604644775390625e-8f,
          "Q8_0 decoder preserves FP16 subnormal scales");

    memset(q4_packed, 0, sizeof(q4_packed));
    write_u16_le(q4_packed, 0x3c00U); /* 1.0 */
    write_u16_le(q4_packed + 18U, 0xbc00U); /* -1.0 */
    for (i = 0U; i < 16U; ++i) {
        uint8_t low = (uint8_t)i;
        uint8_t high = (uint8_t)(15U - i);
        q4_packed[2U + i] = (uint8_t)(low | (high << 4));
        q4_packed[20U + i] = (uint8_t)(9U | (7U << 4));
        q4_expected[i] = (float)((int32_t)i - 8);
        q4_expected[i + 16U] = (float)(7 - (int32_t)i);
        q4_expected[i + 32U] = -1.0f;
        q4_expected[i + 48U] = 1.0f;
    }
    memset(&q4_args, 0, sizeof(q4_args));
    q4_args.struct_size = sizeof(q4_args);
    q4_args.abi_version = NFLLM_ABI_VERSION_1;
    q4_args.packed_row = q4_packed;
    q4_args.packed_bytes = sizeof(q4_packed);
    q4_args.columns = 64U;
    q4_args.output = q4_output;
    check(nfllm_dispatch_dequantize_q4_0_row(&q4_args) == NFLLM_OK &&
          memcmp(q4_output, q4_expected, sizeof(q4_output)) == 0,
          "Q4_0 nibble order, centering and signed scales decode exactly");
    q4_args.columns = 31U;
    check(nfllm_dispatch_dequantize_q4_0_row(&q4_args) == NFLLM_E_ARGUMENT,
          "Q4_0 decoder rejects rows not divisible by 32");
    q4_args.columns = 64U;
    q4_args.packed_bytes = sizeof(q4_packed) + 1U;
    check(nfllm_dispatch_dequantize_q4_0_row(&q4_args) == NFLLM_E_SIZE,
          "Q4_0 decoder rejects incorrect packed row size");

    memset(q20_packed, 0, sizeof(q20_packed));
    write_u16_le(q20_packed, 0x3c00U);
    for (i = 0U; i < 16U; ++i) q20_packed[2U + i] = 0xe4U;
    for (i = 0U; i < 64U; ++i) q20_expected[i] = (float)((int32_t)(i & 3U) - 1);
    memset(&q20_args, 0, sizeof(q20_args));
    q20_args.struct_size = sizeof(q20_args);
    q20_args.abi_version = NFLLM_ABI_VERSION_1;
    q20_args.packed_row = q20_packed;
    q20_args.packed_bytes = sizeof(q20_packed);
    q20_args.columns = 64U;
    q20_args.output = q20_output;
    check(nfllm_dispatch_dequantize_q2_0_row(&q20_args) == NFLLM_OK &&
          memcmp(q20_output, q20_expected, sizeof(q20_output)) == 0,
          "Q2_0 four two-bit codes and FP16 scale decode exactly");
    q20_args.columns = 63U;
    check(nfllm_dispatch_dequantize_q2_0_row(&q20_args) == NFLLM_E_ARGUMENT,
          "Q2_0 decoder rejects rows not divisible by 64");

    memset(q2k_packed, 0, sizeof(q2k_packed));
    memset(q2k_packed, 0x21, 16U);
    memset(q2k_packed + 16U, 0xe4, 64U);
    write_u16_le(q2k_packed + 80U, 0x3c00U);
    write_u16_le(q2k_packed + 82U, 0x3800U);
    for (i = 0U; i < 256U; ++i)
        q2k_expected[i] = (float)((int32_t)((i % 128U) / 32U) - 1);
    memset(&q2k_args, 0, sizeof(q2k_args));
    q2k_args.struct_size = sizeof(q2k_args);
    q2k_args.abi_version = NFLLM_ABI_VERSION_1;
    q2k_args.packed_row = q2k_packed;
    q2k_args.packed_bytes = sizeof(q2k_packed);
    q2k_args.columns = 256U;
    q2k_args.output = q2k_output;
    check(nfllm_dispatch_dequantize_q2_k_row(&q2k_args) == NFLLM_OK &&
          memcmp(q2k_output, q2k_expected, sizeof(q2k_output)) == 0,
          "Q2_K packed scales, minima and 2-bit groups decode exactly");

    memset(iq3xxs_packed, 0, sizeof(iq3xxs_packed));
    write_u16_le(iq3xxs_packed, 0x3c00U);
    iq3xxs_packed[66U] = 1U;
    for (i = 0U; i < 256U; ++i) iq3xxs_expected[i] = 1.0f;
    iq3xxs_expected[0] = -1.0f;
    iq3xxs_expected[7] = -1.0f;
    memset(&iq3xxs_args, 0, sizeof(iq3xxs_args));
    iq3xxs_args.struct_size = sizeof(iq3xxs_args);
    iq3xxs_args.abi_version = NFLLM_ABI_VERSION_1;
    iq3xxs_args.packed_row = iq3xxs_packed;
    iq3xxs_args.packed_bytes = sizeof(iq3xxs_packed);
    iq3xxs_args.columns = 256U;
    iq3xxs_args.output = iq3xxs_output;
    check(nfllm_dispatch_dequantize_iq3_xxs_row(&iq3xxs_args) == NFLLM_OK &&
          memcmp(iq3xxs_output, iq3xxs_expected, sizeof(iq3xxs_output)) == 0,
          "IQ3_XXS codebook, packed sign mask and super-scale decode exactly");

    memset(iq3s_packed, 0, sizeof(iq3s_packed));
    write_u16_le(iq3s_packed, 0x3c00U);
    iq3s_packed[74U] = 0x81U;
    for (i = 0U; i < 256U; ++i) iq3s_expected[i] = 1.0f;
    iq3s_expected[0] = -1.0f;
    iq3s_expected[7] = -1.0f;
    memset(&iq3s_args, 0, sizeof(iq3s_args));
    iq3s_args.struct_size = sizeof(iq3s_args);
    iq3s_args.abi_version = NFLLM_ABI_VERSION_1;
    iq3s_args.packed_row = iq3s_packed;
    iq3s_args.packed_bytes = sizeof(iq3s_packed);
    iq3s_args.columns = 256U;
    iq3s_args.output = iq3s_output;
    check(nfllm_dispatch_dequantize_iq3_s_row(&iq3s_args) == NFLLM_OK &&
          memcmp(iq3s_output, iq3s_expected, sizeof(iq3s_output)) == 0,
          "IQ3_S grid indexes, direct sign bits and sub-scales decode exactly");
    nfllm_registry_shutdown();
    return failures == 0;
}

static int test_direct_quantized_matvec(void)
{
    struct nfllm_host_api_v1 host;
    struct nfllm_q8_0_dequantize_row_args_v1 q8_decode;
    struct nfllm_q4_0_dequantize_row_args_v1 q4_decode;
    struct nfllm_q8_0_matvec_row_args_v1 q8_args;
    struct nfllm_q4_0_matvec_row_args_v1 q4_args;
    uint8_t q8_packed[34], q4_packed[18];
    float input[32], decoded[32], output = -999.0f, expected = 0.0f;
    uint32_t i;
    nfllm_status_v1 status;
    memset(&host, 0, sizeof(host));
    host.struct_size = sizeof(host);
    host.abi_version = NFLLM_ABI_VERSION_1;
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
    status = nfllm_registry_initialize(&host);
    if (status == NFLLM_OK) status = nfllm_register_q8_0_plugin();
    if (status == NFLLM_OK) status = nfllm_register_q4_0_plugin();
    check(status == NFLLM_OK, "direct quantized matvec plugins register on 386+x87");
    if (status != NFLLM_OK) {
        nfllm_registry_shutdown();
        return 0;
    }

    q8_packed[0] = 0U; q8_packed[1] = 0x3cU;
    for (i = 0U; i < 32U; ++i) {
        int32_t quant = (int32_t)i - 16;
        q8_packed[2U + i] = (uint8_t)quant;
        input[i] = (float)(i + 1U);
        expected += (float)quant * input[i];
    }
    memset(&q8_args, 0, sizeof(q8_args));
    q8_args.struct_size = sizeof(q8_args);
    q8_args.abi_version = NFLLM_ABI_VERSION_1;
    q8_args.packed_row = q8_packed;
    q8_args.packed_bytes = sizeof(q8_packed);
    q8_args.columns = 32U;
    q8_args.input = input;
    q8_args.output = &output;
    status = nfllm_dispatch_matvec_q8_0_row(&q8_args);
    check(status == NFLLM_OK && output == expected,
          "Q8_0 direct packed dot product matches integer reference vector");
    q8_packed[0] = 0U; q8_packed[1] = 0x38U;
    memset(&q8_decode, 0, sizeof(q8_decode));
    q8_decode.struct_size = sizeof(q8_decode);
    q8_decode.abi_version = NFLLM_ABI_VERSION_1;
    q8_decode.packed_row = q8_packed;
    q8_decode.packed_bytes = sizeof(q8_packed);
    q8_decode.columns = 32U;
    q8_decode.output = decoded;
    for (i = 0U; i < 32U; ++i) input[i] = (float)(i + 1U) * 0.1f;
    expected = 0.0f;
    check(nfllm_dispatch_dequantize_q8_0_row(&q8_decode) == NFLLM_OK,
          "Q8_0 differential reference row dequantizes");
    for (i = 0U; i < 32U; ++i) expected += decoded[i] * input[i];
    check(nfllm_dispatch_matvec_q8_0_row(&q8_args) == NFLLM_OK &&
          fabs((double)(output - expected)) < 1.0e-4,
          "Q8_0 direct matvec agrees with dequantize-then-dot reference");
    q8_packed[1] = 0x7cU;
    output = -999.0f;
    check(nfllm_dispatch_matvec_q8_0_row(&q8_args) == NFLLM_E_NUMERIC &&
          output == -999.0f,
          "Q8_0 direct matvec rejects non-finite scale without partial output");

    q4_packed[0] = 0U; q4_packed[1] = 0x3cU;
    memset(q4_packed + 2U, 0x99, sizeof(q4_packed) - 2U);
    memset(input, 0, sizeof(input));
    for (i = 0U; i < 32U; ++i) input[i] = 1.0f;
    memset(&q4_args, 0, sizeof(q4_args));
    q4_args.struct_size = sizeof(q4_args);
    q4_args.abi_version = NFLLM_ABI_VERSION_1;
    q4_args.packed_row = q4_packed;
    q4_args.packed_bytes = sizeof(q4_packed);
    q4_args.columns = 32U;
    q4_args.input = input;
    q4_args.output = &output;
    output = -999.0f;
    check(nfllm_dispatch_matvec_q4_0_row(&q4_args) == NFLLM_OK &&
          output == 32.0f,
          "Q4_0 direct packed dot product matches reference vector");
    memset(&q4_decode, 0, sizeof(q4_decode));
    q4_decode.struct_size = sizeof(q4_decode);
    q4_decode.abi_version = NFLLM_ABI_VERSION_1;
    q4_decode.packed_row = q4_packed;
    q4_decode.packed_bytes = sizeof(q4_packed);
    q4_decode.columns = 32U;
    q4_decode.output = decoded;
    for (i = 0U; i < 32U; ++i) input[i] = (float)(i + 1U) * 0.1f;
    expected = 0.0f;
    check(nfllm_dispatch_dequantize_q4_0_row(&q4_decode) == NFLLM_OK,
          "Q4_0 differential reference row dequantizes");
    for (i = 0U; i < 32U; ++i) expected += decoded[i] * input[i];
    check(nfllm_dispatch_matvec_q4_0_row(&q4_args) == NFLLM_OK &&
          fabs((double)(output - expected)) < 1.0e-4,
          "Q4_0 direct matvec agrees with dequantize-then-dot reference");
    q4_args.packed_bytes = sizeof(q4_packed) - 1U;
    check(nfllm_dispatch_matvec_q4_0_row(&q4_args) == NFLLM_E_SIZE,
          "direct quantized matvec rejects incorrect packed row size");
    nfllm_registry_shutdown();
    return failures == 0;
}

static int test_direct_q2_iq3_matvec(void)
{
    struct nfllm_host_api_v1 host;
    struct nfllm_packed_matvec_row_args_v1 direct;
    struct nfllm_matvec_f32_args_v1 reference;
    struct nfllm_q2_0_dequantize_row_args_v1 q20_decode;
    struct nfllm_q2_k_dequantize_row_args_v1 q2k_decode;
    struct nfllm_iq3_xxs_dequantize_row_args_v1 i3x_decode;
    struct nfllm_iq3_s_dequantize_row_args_v1 i3s_decode;
    uint8_t q20_packed[8U * 18U];
    uint8_t q2k_packed[2U * 84U];
    uint8_t i3x_packed[2U * 98U];
    uint8_t i3s_packed[2U * 110U];
    float input[512], decoded[512], output, expected;
    char diagnostic[NFLLM_DIAGNOSTIC_CAPACITY];
    uint32_t i;
    nfllm_status_v1 status;

    memset(&host, 0, sizeof(host));
    host.struct_size = sizeof(host);
    host.abi_version = NFLLM_ABI_VERSION_1;
    host.capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
    status = nfllm_registry_initialize(&host);
    if (status == NFLLM_OK) status = nfllm_register_reference_plugin();
    if (status == NFLLM_OK) status = nfllm_register_q2_plugin();
    if (status == NFLLM_OK) status = nfllm_register_iq3_plugin();
    if (status == NFLLM_OK)
        status = nfllm_dispatcher_configure(NFLLM_PLUGIN_ID_NONE,
                                             host.capabilities,
                                             diagnostic, sizeof(diagnostic));
    check(status == NFLLM_OK,
          "direct Q2_0/Q2_K/IQ3_XXS/IQ3_S plugins register on 386+x87");
    if (status != NFLLM_OK) {
        nfllm_registry_shutdown();
        return 0;
    }

    for (i = 0U; i < 512U; ++i)
        input[i] = (float)((int32_t)(i % 17U) - 8) * 0.125f +
                    (float)(i % 5U) * 0.03125f;
    memset(&direct, 0, sizeof(direct));
    direct.struct_size = sizeof(direct);
    direct.abi_version = NFLLM_ABI_VERSION_1;
    direct.columns = 512U;
    direct.input = input;
    direct.output = &output;
    memset(&reference, 0, sizeof(reference));
    reference.struct_size = sizeof(reference);
    reference.abi_version = NFLLM_ABI_VERSION_1;
    reference.rows = 1U;
    reference.columns = 512U;
    reference.input = input;
    reference.output = &expected;

    memset(q20_packed, 0, sizeof(q20_packed));
    for (i = 0U; i < 8U; ++i) {
        uint32_t j;
        write_u16_le(q20_packed + i * 18U, 0x3800U); /* 0.5 */
        for (j = 0U; j < 16U; ++j)
            q20_packed[i * 18U + 2U + j] = (uint8_t)(j * 53U + i * 31U + 7U);
    }
    memset(&q20_decode, 0, sizeof(q20_decode));
    q20_decode.struct_size = sizeof(q20_decode);
    q20_decode.abi_version = NFLLM_ABI_VERSION_1;
    q20_decode.packed_row = q20_packed;
    q20_decode.packed_bytes = sizeof(q20_packed);
    q20_decode.columns = 512U;
    q20_decode.output = decoded;
    status = nfllm_dispatch_dequantize_q2_0_row(&q20_decode);
    reference.weights = decoded;
    if (status == NFLLM_OK) status = nfllm_dispatch_matvec_f32(&reference);
    direct.packed_row = q20_packed;
    direct.packed_bytes = sizeof(q20_packed);
    output = -999.0f;
    if (status == NFLLM_OK)
        status = nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_Q2_0_ROW,
                                                   &direct);
    check(status == NFLLM_OK &&
          fabs((double)(output - expected)) <= 0.001 * (fabs((double)expected) + 1.0),
          "Q2_0 direct packed matvec matches dequantize-then-dot across eight blocks");
    write_u16_le(q20_packed + 7U * 18U, 0x7c00U);
    output = -999.0f;
    check(nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_Q2_0_ROW,
                                           &direct) == NFLLM_E_NUMERIC &&
          output == -999.0f,
          "Q2_0 direct matvec rejects a late non-finite scale without output write");

    for (i = 0U; i < 2U; ++i) {
        uint32_t j;
        uint8_t *block = q2k_packed + i * 84U;
        for (j = 0U; j < 16U; ++j) block[j] = (uint8_t)(j * 29U + i * 47U + 17U);
        for (j = 0U; j < 64U; ++j) block[16U + j] = (uint8_t)(j * 43U + i * 61U + 91U);
        write_u16_le(block + 80U, 0x3800U);
        write_u16_le(block + 82U, 0x3400U);
    }
    memset(&q2k_decode, 0, sizeof(q2k_decode));
    q2k_decode.struct_size = sizeof(q2k_decode);
    q2k_decode.abi_version = NFLLM_ABI_VERSION_1;
    q2k_decode.packed_row = q2k_packed;
    q2k_decode.packed_bytes = sizeof(q2k_packed);
    q2k_decode.columns = 512U;
    q2k_decode.output = decoded;
    status = nfllm_dispatch_dequantize_q2_k_row(&q2k_decode);
    reference.weights = decoded;
    if (status == NFLLM_OK) status = nfllm_dispatch_matvec_f32(&reference);
    direct.packed_row = q2k_packed;
    direct.packed_bytes = sizeof(q2k_packed);
    output = -999.0f;
    if (status == NFLLM_OK)
        status = nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_Q2_K_ROW,
                                                   &direct);
    check(status == NFLLM_OK &&
          fabs((double)(output - expected)) <= 0.001 * (fabs((double)expected) + 1.0),
          "Q2_K direct packed matvec matches all scale/min groups across two blocks");
    write_u16_le(q2k_packed + 84U + 82U, 0x7c00U);
    output = -999.0f;
    check(nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_Q2_K_ROW,
                                           &direct) == NFLLM_E_NUMERIC &&
          output == -999.0f,
          "Q2_K direct matvec validates both late FP16 scales before output write");

    for (i = 0U; i < sizeof(i3x_packed); ++i) i3x_packed[i] = (uint8_t)(i * 67U + 23U);
    write_u16_le(i3x_packed, 0x3555U);
    write_u16_le(i3x_packed + 98U, 0x3555U);
    memset(&i3x_decode, 0, sizeof(i3x_decode));
    i3x_decode.struct_size = sizeof(i3x_decode);
    i3x_decode.abi_version = NFLLM_ABI_VERSION_1;
    i3x_decode.packed_row = i3x_packed;
    i3x_decode.packed_bytes = sizeof(i3x_packed);
    i3x_decode.columns = 512U;
    i3x_decode.output = decoded;
    status = nfllm_dispatch_dequantize_iq3_xxs_row(&i3x_decode);
    reference.weights = decoded;
    if (status == NFLLM_OK) status = nfllm_dispatch_matvec_f32(&reference);
    direct.packed_row = i3x_packed;
    direct.packed_bytes = sizeof(i3x_packed);
    output = -999.0f;
    if (status == NFLLM_OK)
        status = nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_IQ3_XXS_ROW,
                                                   &direct);
    check(status == NFLLM_OK &&
          fabs((double)(output - expected)) <= 0.001 * (fabs((double)expected) + 1.0),
          "IQ3_XXS direct matvec matches grid order, parity signs and scales");
    write_u16_le(i3x_packed + 98U, 0x7c00U);
    output = -999.0f;
    check(nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_IQ3_XXS_ROW,
                                           &direct) == NFLLM_E_NUMERIC &&
          output == -999.0f,
          "IQ3_XXS direct matvec rejects a late non-finite scale without output write");

    for (i = 0U; i < sizeof(i3s_packed); ++i) i3s_packed[i] = (uint8_t)(i * 41U + 19U);
    write_u16_le(i3s_packed, 0x3555U);
    write_u16_le(i3s_packed + 110U, 0x3555U);
    memset(&i3s_decode, 0, sizeof(i3s_decode));
    i3s_decode.struct_size = sizeof(i3s_decode);
    i3s_decode.abi_version = NFLLM_ABI_VERSION_1;
    i3s_decode.packed_row = i3s_packed;
    i3s_decode.packed_bytes = sizeof(i3s_packed);
    i3s_decode.columns = 512U;
    i3s_decode.output = decoded;
    status = nfllm_dispatch_dequantize_iq3_s_row(&i3s_decode);
    reference.weights = decoded;
    if (status == NFLLM_OK) status = nfllm_dispatch_matvec_f32(&reference);
    direct.packed_row = i3s_packed;
    direct.packed_bytes = sizeof(i3s_packed);
    output = -999.0f;
    if (status == NFLLM_OK)
        status = nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_IQ3_S_ROW,
                                                   &direct);
    check(status == NFLLM_OK &&
          fabs((double)(output - expected)) <= 0.001 * (fabs((double)expected) + 1.0),
          "IQ3_S direct matvec matches high-bit indexes, signs and sub-scales");
    write_u16_le(i3s_packed + 110U, 0x7c00U);
    output = -999.0f;
    check(nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_IQ3_S_ROW,
                                           &direct) == NFLLM_E_NUMERIC &&
          output == -999.0f,
          "IQ3_S direct matvec rejects a late non-finite scale without output write");

    direct.struct_size = sizeof(direct) - 1U;
    check(nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_IQ3_S_ROW,
                                           &direct) == NFLLM_E_SIZE,
          "packed-row dispatcher rejects a short ABI-v1 argument struct");
    direct.struct_size = sizeof(direct);
    direct.packed_bytes = sizeof(i3s_packed) - 1U;
    check(nfllm_dispatch_matvec_packed_row(NFLLM_OP_MATVEC_IQ3_S_ROW,
                                           &direct) == NFLLM_E_SIZE,
          "packed-row dispatcher rejects a malformed packed byte count");
    check(nfllm_dispatch_matvec_packed_row(0xffffffffUL, &direct) == NFLLM_E_UNSUPPORTED,
          "packed-row dispatcher rejects unknown operation IDs");

    nfllm_registry_shutdown();
    return failures == 0;
}

int main(void)
{
    printf("NFLLM self-tests\n");
    (void)test_logic();
    (void)test_registry_capacity();
    (void)test_registry_and_equivalence();
    (void)test_quantized_row_decoders();
    (void)test_direct_quantized_matvec();
    (void)test_direct_q2_iq3_matvec();
    if (failures == 0) {
        printf("SELFTEST: PASS\n");
        return 0;
    }
    printf("SELFTEST: FAIL (%d)\n", failures);
    return 1;
}
