#include "nfllm_registry.h"
#include <stdlib.h>
#include <string.h>

struct RegistryEntry {
    const struct nfllm_plugin_descriptor_v1 *plugin;
    const struct nfllm_kernel_descriptor_v1 *kernels;
    uint32_t kernel_count;
    int32_t priority;
    void *plugin_state;
};

static struct RegistryEntry *g_entries;
static uint32_t g_entry_count;
static uint32_t g_entry_capacity;
static struct nfllm_host_api_v1 g_host;
static int g_initialized;
static const struct nfllm_kernel_descriptor_v1 *g_matvec;
static const struct nfllm_kernel_descriptor_v1 *g_rms_norm;
static const struct nfllm_kernel_descriptor_v1 *g_rope;
static uint32_t g_preferred_plugin_id;

static int valid_name(const char *name)
{
    return name != 0 && name[0] != '\0';
}

static nfllm_status_v1 reserve_entry(void)
{
    struct RegistryEntry *grown;
    uint32_t capacity;
    if (g_entry_count < g_entry_capacity) return NFLLM_OK;
    capacity = g_entry_capacity ? g_entry_capacity * 2U :
                                  NFLLM_REGISTRY_INITIAL_CAPACITY;
    if (capacity < g_entry_capacity ||
        capacity > 0xffffffffUL / sizeof(*g_entries))
        return NFLLM_E_OVERFLOW;
    grown = (struct RegistryEntry *)realloc(
        g_entries, capacity * sizeof(*g_entries));
    if (!grown) return NFLLM_E_MEMORY;
    memset(grown + g_entry_capacity, 0,
           (capacity - g_entry_capacity) * sizeof(*g_entries));
    g_entries = grown;
    g_entry_capacity = capacity;
    return NFLLM_OK;
}

static int aligned_float_pointer(const void *pointer)
{
    return pointer != 0 &&
           (((uintptr_t)pointer & ((uintptr_t)sizeof(float) - 1U)) == 0U);
}

static int operation_requires_f32(uint32_t operation_id)
{
    return operation_id == NFLLM_OP_MATVEC_F32 ||
           operation_id == NFLLM_OP_MATVEC_Q8_0_ROW ||
           operation_id == NFLLM_OP_MATVEC_Q4_0_ROW ||
           operation_id == NFLLM_OP_MATVEC_Q2_0_ROW ||
           operation_id == NFLLM_OP_MATVEC_Q2_K_ROW ||
           operation_id == NFLLM_OP_MATVEC_IQ3_XXS_ROW ||
           operation_id == NFLLM_OP_MATVEC_IQ3_S_ROW ||
           operation_id == NFLLM_OP_MATVEC_Q8_0_MATRIX ||
           operation_id == NFLLM_OP_MATVEC_Q4_0_MATRIX ||
           operation_id == NFLLM_OP_RMS_NORM_F32 ||
           operation_id == NFLLM_OP_ROPE_F32;
}

#define NFLLM_KNOWN_CAPABILITIES (NFLLM_CAP_386 | NFLLM_CAP_X87 | \
    NFLLM_CAP_MMX | NFLLM_CAP_3DNOW | NFLLM_CAP_3DNOW_EXT | \
    NFLLM_CAP_SSE | NFLLM_CAP_SSE2 | NFLLM_CAP_AMD_MMX_EXT | \
    NFLLM_CAP_SSE3 | NFLLM_CAP_SSSE3 | NFLLM_CAP_SSE41 | NFLLM_CAP_SSE42 | \
    NFLLM_CAP_SSE4A | NFLLM_CAP_CPUID)

static void write_diagnostic(char *out, uint32_t capacity, const char *text)
{
    uint32_t i;
    if (!out || capacity == 0U) return;
    for (i = 0U; i + 1U < capacity && text[i] != '\0'; ++i) out[i] = text[i];
    out[i] = '\0';
}

static const struct nfllm_kernel_descriptor_v1 *find_kernel(
    const struct RegistryEntry *entry, uint32_t operation_id)
{
    uint32_t i;
    for (i = 0U; i < entry->kernel_count; ++i)
        if (entry->kernels[i].operation_id == operation_id)
            return &entry->kernels[i];
    return 0;
}

nfllm_status_v1 NFLLM_CALL nfllm_registry_initialize(
    const struct nfllm_host_api_v1 *host)
{
    if (!host) return NFLLM_E_ARGUMENT;
    if (host->struct_size < sizeof(*host)) return NFLLM_E_SIZE;
    if (host->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    nfllm_registry_shutdown();
    g_entries = (struct RegistryEntry *)calloc(
        NFLLM_REGISTRY_INITIAL_CAPACITY, sizeof(*g_entries));
    if (!g_entries) return NFLLM_E_MEMORY;
    g_entry_capacity = NFLLM_REGISTRY_INITIAL_CAPACITY;
    g_host = *host;
    g_entry_count = 0U;
    g_matvec = 0;
    g_rms_norm = 0;
    g_rope = 0;
    g_preferred_plugin_id = NFLLM_PLUGIN_ID_NONE;
    g_initialized = 1;
    return NFLLM_OK;
}

nfllm_status_v1 NFLLM_CALL nfllm_registry_register(
    const struct nfllm_plugin_descriptor_v1 *plugin,
    const struct nfllm_kernel_descriptor_v1 *kernels,
    uint32_t kernel_count)
{
    return nfllm_registry_register_priority(plugin, kernels, kernel_count, 0);
}

nfllm_status_v1 NFLLM_CALL nfllm_registry_register_priority(
    const struct nfllm_plugin_descriptor_v1 *plugin,
    const struct nfllm_kernel_descriptor_v1 *kernels,
    uint32_t kernel_count, int32_t priority)
{
    uint32_t i, j;
    const uint32_t known_capabilities = NFLLM_KNOWN_CAPABILITIES;
    void *plugin_state = 0;
    nfllm_status_v1 status;
    if (!g_initialized) return NFLLM_E_STATE;
    if (!plugin || (kernel_count != 0U && !kernels))
        return NFLLM_E_ARGUMENT;
    if (plugin->struct_size < sizeof(*plugin)) return NFLLM_E_SIZE;
    if (plugin->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (plugin->plugin_id == NFLLM_PLUGIN_ID_NONE || !valid_name(plugin->name))
        return NFLLM_E_ARGUMENT;
    if ((plugin->required_capabilities & ~known_capabilities) != 0U)
        return NFLLM_E_UNSUPPORTED;
    if (kernel_count > NFLLM_REGISTRY_MAX_KERNELS_PER_PLUGIN)
        return NFLLM_E_FULL;
    if ((plugin->initialize == 0) != (plugin->shutdown == 0))
        return NFLLM_E_ARGUMENT;

    for (i = 0U; i < g_entry_count; ++i)
        if (g_entries[i].plugin->plugin_id == plugin->plugin_id)
            return NFLLM_E_DUPLICATE;

    for (i = 0U; i < kernel_count; ++i) {
        if (kernels[i].struct_size < sizeof(kernels[i])) return NFLLM_E_SIZE;
        if (kernels[i].abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
        if (kernels[i].operation_id == 0U || !valid_name(kernels[i].name) ||
            !kernels[i].execute)
            return NFLLM_E_ARGUMENT;
        if (kernels[i].input_type > NFLLM_VALUE_F32 ||
            kernels[i].output_type > NFLLM_VALUE_F32 ||
            (kernels[i].required_capabilities & ~known_capabilities) != 0U)
            return NFLLM_E_UNSUPPORTED;
        if (operation_requires_f32(kernels[i].operation_id) &&
            (kernels[i].input_type != NFLLM_VALUE_F32 ||
             kernels[i].output_type != NFLLM_VALUE_F32))
            return NFLLM_E_UNSUPPORTED;
        for (j = 0U; j < i; ++j)
            if (kernels[j].operation_id == kernels[i].operation_id)
                return NFLLM_E_DUPLICATE;
    }

    status = reserve_entry();
    if (status != NFLLM_OK) return status;

    if (plugin->initialize) {
        status = plugin->initialize(&g_host, &plugin_state);
        if (status != NFLLM_OK) return status;
    }
    g_entries[g_entry_count].plugin = plugin;
    g_entries[g_entry_count].kernels = kernels;
    g_entries[g_entry_count].kernel_count = kernel_count;
    g_entries[g_entry_count].plugin_state = plugin_state;
    g_entries[g_entry_count].priority = priority;
    ++g_entry_count;
    if (g_host.log) g_host.log(g_host.context, NFLLM_LOG_INFO,
                               "registered NFLLM plugin");
    return NFLLM_OK;
}

void NFLLM_CALL nfllm_registry_shutdown(void)
{
    while (g_entry_count != 0U) {
        struct RegistryEntry *entry = &g_entries[--g_entry_count];
        if (entry->plugin && entry->plugin->shutdown)
            entry->plugin->shutdown(entry->plugin_state);
        memset(entry, 0, sizeof(*entry));
    }
    free(g_entries);
    g_entries = 0;
    g_entry_capacity = 0U;
    g_matvec = 0;
    g_rms_norm = 0;
    g_rope = 0;
    g_preferred_plugin_id = NFLLM_PLUGIN_ID_NONE;
    g_initialized = 0;
    memset(&g_host, 0, sizeof(g_host));
}

nfllm_status_v1 NFLLM_CALL nfllm_registry_resolve(
    uint32_t operation_id, uint32_t requested_plugin_id,
    uint32_t host_capabilities,
    const struct nfllm_kernel_descriptor_v1 **kernel,
    uint32_t *selected_plugin_id,
    char *diagnostic, uint32_t diagnostic_capacity)
{
    uint32_t i;
    int32_t best_priority = 0x7fffffffL;
    int saw_operation = 0;
    int saw_incompatible = 0;
    if (!kernel || !selected_plugin_id) return NFLLM_E_ARGUMENT;
    *kernel = 0;
    *selected_plugin_id = NFLLM_PLUGIN_ID_NONE;
    if (diagnostic && diagnostic_capacity) diagnostic[0] = '\0';
    if (!g_initialized) return NFLLM_E_STATE;

    for (i = 0U; i < g_entry_count; ++i) {
        const struct RegistryEntry *entry = &g_entries[i];
        const struct nfllm_kernel_descriptor_v1 *candidate;
        uint32_t needed;
        if (requested_plugin_id != NFLLM_PLUGIN_ID_NONE &&
            entry->plugin->plugin_id != requested_plugin_id) continue;
        candidate = find_kernel(entry, operation_id);
        if (!candidate) continue;
        saw_operation = 1;
        needed = entry->plugin->required_capabilities |
                 candidate->required_capabilities;
        if ((needed & ~host_capabilities) != 0U) {
            saw_incompatible = 1;
            if (requested_plugin_id != NFLLM_PLUGIN_ID_NONE) {
                write_diagnostic(diagnostic, diagnostic_capacity,
                                 "selected plugin lacks declared host capabilities");
                return NFLLM_E_CAPABILITY;
            }
            continue;
        }
        if (!*kernel || entry->priority < best_priority) {
            *kernel = candidate;
            *selected_plugin_id = entry->plugin->plugin_id;
            best_priority = entry->priority;
        }
    }
    if (*kernel) {
        if (saw_incompatible)
            write_diagnostic(diagnostic, diagnostic_capacity,
                             "selected compatible implementation by configured priority");
        else
            write_diagnostic(diagnostic, diagnostic_capacity,
                             "selected compatible implementation by configured priority");
        return NFLLM_OK;
    }
    if (saw_operation && saw_incompatible) return NFLLM_E_CAPABILITY;
    write_diagnostic(diagnostic, diagnostic_capacity,
                     "no registered implementation for requested operation");
    return NFLLM_E_NOT_FOUND;
}

static void append_text(char *out, uint32_t capacity, uint32_t *used,
                        const char *text)
{
    uint32_t i = 0U;
    if (!out || !used || capacity == 0U) return;
    while (text[i] && *used + 1U < capacity) out[(*used)++] = text[i++];
    out[*used] = '\0';
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatcher_configure(
    uint32_t requested_plugin_id, uint32_t host_capabilities,
    char *diagnostic, uint32_t diagnostic_capacity)
{
    const struct nfllm_kernel_descriptor_v1 *matvec = 0;
    const struct nfllm_kernel_descriptor_v1 *rms_norm = 0;
    const struct nfllm_kernel_descriptor_v1 *rope = 0;
    uint32_t plugin_id;
    uint32_t used = 0U;
    char reason[96];
    nfllm_status_v1 status;
    if (diagnostic && diagnostic_capacity) diagnostic[0] = '\0';
    g_matvec = 0;
    g_rms_norm = 0;
    g_rope = 0;
    g_preferred_plugin_id = NFLLM_PLUGIN_ID_NONE;
    status = nfllm_registry_resolve(NFLLM_OP_MATVEC_F32, requested_plugin_id,
                                    host_capabilities, &matvec, &plugin_id,
                                    reason, sizeof(reason));
    if (status != NFLLM_OK) return status;
    if (requested_plugin_id != NFLLM_PLUGIN_ID_NONE &&
        plugin_id != requested_plugin_id) return NFLLM_E_STATE;
    status = nfllm_registry_resolve(NFLLM_OP_RMS_NORM_F32, requested_plugin_id,
                                    host_capabilities, &rms_norm, &plugin_id,
                                    reason, sizeof(reason));
    if (status == NFLLM_E_NOT_FOUND &&
        requested_plugin_id != NFLLM_PLUGIN_ID_NONE)
        status = nfllm_registry_resolve(NFLLM_OP_RMS_NORM_F32,
                                        NFLLM_PLUGIN_ID_NONE,
                                        host_capabilities, &rms_norm,
                                        &plugin_id, reason, sizeof(reason));
    if (status != NFLLM_OK) return status;
    status = nfllm_registry_resolve(NFLLM_OP_ROPE_F32, requested_plugin_id,
                                    host_capabilities, &rope, &plugin_id,
                                    reason, sizeof(reason));
    if (status == NFLLM_E_NOT_FOUND &&
        requested_plugin_id != NFLLM_PLUGIN_ID_NONE)
        status = nfllm_registry_resolve(NFLLM_OP_ROPE_F32,
                                        NFLLM_PLUGIN_ID_NONE,
                                        host_capabilities, &rope,
                                        &plugin_id, reason, sizeof(reason));
    if (status != NFLLM_OK) return status;
    g_matvec = matvec;
    g_rms_norm = rms_norm;
    g_rope = rope;
    g_preferred_plugin_id = requested_plugin_id;
    append_text(diagnostic, diagnostic_capacity, &used, "matvec=");
    append_text(diagnostic, diagnostic_capacity, &used, matvec->name);
    append_text(diagnostic, diagnostic_capacity, &used, "; rms_norm=");
    append_text(diagnostic, diagnostic_capacity, &used, rms_norm->name);
    append_text(diagnostic, diagnostic_capacity, &used, "; rope=");
    append_text(diagnostic, diagnostic_capacity, &used, rope->name);
    append_text(diagnostic, diagnostic_capacity, &used,
                requested_plugin_id == NFLLM_PLUGIN_ID_NONE ? "; selection=auto" :
                                                               "; selection=manual");
    return NFLLM_OK;
}

static nfllm_status_v1 invoke(const struct nfllm_kernel_descriptor_v1 *kernel,
                              const void *arguments)
{
    if (!kernel) return NFLLM_E_STATE;
    return kernel->execute(arguments);
}

static nfllm_status_v1 dispatch_dequantize_row(
    uint32_t operation_id, const void *arguments,
    uint32_t struct_size, uint32_t abi_version, const uint8_t *packed_row,
    uint32_t packed_bytes, uint32_t columns, float *output,
    uint32_t argument_size, uint32_t block_elements, uint32_t block_bytes)
{
    const struct nfllm_kernel_descriptor_v1 *kernel = 0;
    uint32_t selected_plugin_id;
    uint32_t blocks, expected_bytes;
    nfllm_status_v1 status;
    if (!arguments) return NFLLM_E_ARGUMENT;
    if (struct_size < argument_size) return NFLLM_E_SIZE;
    if (abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!packed_row || !aligned_float_pointer(output) || columns == 0U ||
        block_elements == 0U || (columns % block_elements) != 0U)
        return NFLLM_E_ARGUMENT;
    blocks = columns / block_elements;
    if (blocks > 0xffffffffUL / block_bytes) return NFLLM_E_OVERFLOW;
    expected_bytes = blocks * block_bytes;
    if (packed_bytes != expected_bytes) return NFLLM_E_SIZE;
    status = nfllm_registry_resolve(operation_id, NFLLM_PLUGIN_ID_NONE,
                                    g_host.capabilities, &kernel,
                                    &selected_plugin_id, 0, 0U);
    if (status != NFLLM_OK) return status;
    return invoke(kernel, arguments);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_q8_0_row(
    const struct nfllm_q8_0_dequantize_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_dequantize_row(
        NFLLM_OP_DEQUANTIZE_ROW_Q8_0, arguments, arguments->struct_size,
        arguments->abi_version, arguments->packed_row, arguments->packed_bytes,
        arguments->columns, arguments->output, sizeof(*arguments), 32U, 34U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_q4_0_row(
    const struct nfllm_q4_0_dequantize_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_dequantize_row(
        NFLLM_OP_DEQUANTIZE_ROW_Q4_0, arguments, arguments->struct_size,
        arguments->abi_version, arguments->packed_row, arguments->packed_bytes,
        arguments->columns, arguments->output, sizeof(*arguments), 32U, 18U);
}

static nfllm_status_v1 dispatch_quant_matvec_row(
    uint32_t operation_id, uint32_t requested_plugin_id, const void *arguments,
    uint32_t struct_size, uint32_t abi_version,
    const uint8_t *packed_row, uint32_t packed_bytes, uint32_t columns,
    const float *input, float *output, uint32_t argument_size,
    uint32_t block_elements, uint32_t block_bytes)
{
    const struct nfllm_kernel_descriptor_v1 *kernel = 0;
    uint32_t selected_plugin_id;
    nfllm_status_v1 status;
    if (!arguments) return NFLLM_E_ARGUMENT;
    if (struct_size < argument_size) return NFLLM_E_SIZE;
    if (abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!packed_row || !aligned_float_pointer(input) ||
        !aligned_float_pointer(output) || columns == 0U ||
        (columns % block_elements) != 0U)
        return NFLLM_E_ARGUMENT;
    if (columns / block_elements > 0xffffffffUL / block_bytes)
        return NFLLM_E_OVERFLOW;
    if (packed_bytes != (columns / block_elements) * block_bytes)
        return NFLLM_E_SIZE;
    status = nfllm_registry_resolve(operation_id, requested_plugin_id,
                                    g_host.capabilities, &kernel,
                                    &selected_plugin_id, 0, 0U);
    if (status != NFLLM_OK) return status;
    return invoke(kernel, arguments);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q8_0_row(
    const struct nfllm_q8_0_matvec_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_quant_matvec_row(
        NFLLM_OP_MATVEC_Q8_0_ROW, g_preferred_plugin_id,
        arguments, arguments->struct_size,
        arguments->abi_version, arguments->packed_row, arguments->packed_bytes,
        arguments->columns, arguments->input, arguments->output,
        sizeof(*arguments), 32U, 34U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q8_0_row_scalar(
    const struct nfllm_q8_0_matvec_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_quant_matvec_row(
        NFLLM_OP_MATVEC_Q8_0_ROW, NFLLM_PLUGIN_ID_Q8_0,
        arguments, arguments->struct_size, arguments->abi_version,
        arguments->packed_row, arguments->packed_bytes, arguments->columns,
        arguments->input, arguments->output, sizeof(*arguments), 32U, 34U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q4_0_row(
    const struct nfllm_q4_0_matvec_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_quant_matvec_row(
        NFLLM_OP_MATVEC_Q4_0_ROW, g_preferred_plugin_id,
        arguments, arguments->struct_size,
        arguments->abi_version, arguments->packed_row, arguments->packed_bytes,
        arguments->columns, arguments->input, arguments->output,
        sizeof(*arguments), 32U, 18U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q4_0_row_scalar(
    const struct nfllm_q4_0_matvec_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_quant_matvec_row(
        NFLLM_OP_MATVEC_Q4_0_ROW, NFLLM_PLUGIN_ID_Q4_0,
        arguments, arguments->struct_size, arguments->abi_version,
        arguments->packed_row, arguments->packed_bytes, arguments->columns,
        arguments->input, arguments->output, sizeof(*arguments), 32U, 18U);
}

static nfllm_status_v1 dispatch_quant_matvec_matrix(
    uint32_t operation_id,
    const struct nfllm_quantized_matvec_matrix_args_v1 *arguments,
    uint32_t block_bytes)
{
    const struct nfllm_kernel_descriptor_v1 *kernel = 0;
    uint32_t selected_plugin_id;
    uint32_t blocks, row_bytes;
    nfllm_status_v1 status;
    if (!arguments) return NFLLM_E_ARGUMENT;
    if (arguments->struct_size < sizeof(*arguments)) return NFLLM_E_SIZE;
    if (arguments->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!arguments->packed_matrix || !aligned_float_pointer(arguments->input) ||
        !aligned_float_pointer(arguments->output) || arguments->rows == 0U ||
        arguments->columns == 0U || (arguments->columns % 32U) != 0U)
        return NFLLM_E_ARGUMENT;
    blocks = arguments->columns / 32U;
    if (blocks > 0xffffffffUL / block_bytes) return NFLLM_E_OVERFLOW;
    row_bytes = blocks * block_bytes;
    if (arguments->rows > 0xffffffffUL / row_bytes) return NFLLM_E_OVERFLOW;
    if (arguments->packed_bytes != arguments->rows * row_bytes)
        return NFLLM_E_SIZE;
    status = nfllm_registry_resolve(operation_id, g_preferred_plugin_id,
                                    g_host.capabilities, &kernel,
                                    &selected_plugin_id, 0, 0U);
    if (status != NFLLM_OK) return status;
    return invoke(kernel, arguments);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q8_0_matrix(
    const struct nfllm_quantized_matvec_matrix_args_v1 *arguments)
{
    return dispatch_quant_matvec_matrix(NFLLM_OP_MATVEC_Q8_0_MATRIX,
                                        arguments, 34U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q4_0_matrix(
    const struct nfllm_quantized_matvec_matrix_args_v1 *arguments)
{
    return dispatch_quant_matvec_matrix(NFLLM_OP_MATVEC_Q4_0_MATRIX,
                                        arguments, 18U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_packed_row(
    uint32_t operation_id,
    const struct nfllm_packed_matvec_row_args_v1 *arguments)
{
    uint32_t block_elements, block_bytes;
    if (!arguments) return NFLLM_E_ARGUMENT;
    switch (operation_id) {
    case NFLLM_OP_MATVEC_Q2_0_ROW:
        block_elements = 64U; block_bytes = 18U; break;
    case NFLLM_OP_MATVEC_Q2_K_ROW:
        block_elements = 256U; block_bytes = 84U; break;
    case NFLLM_OP_MATVEC_IQ3_XXS_ROW:
        block_elements = 256U; block_bytes = 98U; break;
    case NFLLM_OP_MATVEC_IQ3_S_ROW:
        block_elements = 256U; block_bytes = 110U; break;
    default:
        return NFLLM_E_UNSUPPORTED;
    }
    return dispatch_quant_matvec_row(
        operation_id, g_preferred_plugin_id, arguments, arguments->struct_size,
        arguments->abi_version, arguments->packed_row, arguments->packed_bytes,
        arguments->columns, arguments->input, arguments->output,
        sizeof(*arguments), block_elements, block_bytes);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_q2_0_row(
    const struct nfllm_q2_0_dequantize_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_dequantize_row(
        NFLLM_OP_DEQUANTIZE_ROW_Q2_0, arguments, arguments->struct_size,
        arguments->abi_version, arguments->packed_row, arguments->packed_bytes,
        arguments->columns, arguments->output, sizeof(*arguments), 64U, 18U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_q2_k_row(
    const struct nfllm_q2_k_dequantize_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_dequantize_row(
        NFLLM_OP_DEQUANTIZE_ROW_Q2_K, arguments, arguments->struct_size,
        arguments->abi_version, arguments->packed_row, arguments->packed_bytes,
        arguments->columns, arguments->output, sizeof(*arguments), 256U, 84U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_iq3_xxs_row(
    const struct nfllm_iq3_xxs_dequantize_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_dequantize_row(
        NFLLM_OP_DEQUANTIZE_ROW_IQ3_XXS, arguments, arguments->struct_size,
        arguments->abi_version, arguments->packed_row, arguments->packed_bytes,
        arguments->columns, arguments->output, sizeof(*arguments), 256U, 98U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_iq3_s_row(
    const struct nfllm_iq3_s_dequantize_row_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    return dispatch_dequantize_row(
        NFLLM_OP_DEQUANTIZE_ROW_IQ3_S, arguments, arguments->struct_size,
        arguments->abi_version, arguments->packed_row, arguments->packed_bytes,
        arguments->columns, arguments->output, sizeof(*arguments), 256U, 110U);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_f32(
    const struct nfllm_matvec_f32_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    if (arguments->struct_size < sizeof(*arguments)) return NFLLM_E_SIZE;
    if (arguments->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!aligned_float_pointer(arguments->weights) ||
        !aligned_float_pointer(arguments->input) ||
        !aligned_float_pointer(arguments->output) ||
        arguments->rows == 0U || arguments->columns == 0U)
        return NFLLM_E_ARGUMENT;
    if (arguments->rows > 0xffffffffUL / arguments->columns)
        return NFLLM_E_OVERFLOW;
    return invoke(g_matvec, arguments);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_rms_norm_f32(
    const struct nfllm_rms_norm_f32_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    if (arguments->struct_size < sizeof(*arguments)) return NFLLM_E_SIZE;
    if (arguments->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (!aligned_float_pointer(arguments->input) ||
        !aligned_float_pointer(arguments->weight) ||
        !aligned_float_pointer(arguments->output) || arguments->count == 0U)
        return NFLLM_E_ARGUMENT;
    return invoke(g_rms_norm, arguments);
}

nfllm_status_v1 NFLLM_CALL nfllm_dispatch_rope_f32(
    const struct nfllm_rope_f32_args_v1 *arguments)
{
    if (!arguments) return NFLLM_E_ARGUMENT;
    if (arguments->struct_size < sizeof(*arguments)) return NFLLM_E_SIZE;
    if (arguments->abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    if (arguments->rope_dim == 0U) return invoke(g_rope, arguments);
    if (!aligned_float_pointer(arguments->vector) ||
        arguments->heads == 0U || arguments->head_dim == 0U ||
        arguments->rope_dim > arguments->head_dim ||
        (arguments->rope_dim & 1U) != 0U)
        return NFLLM_E_ARGUMENT;
    if (arguments->head_dim > 0xffffffffUL / arguments->heads)
        return NFLLM_E_OVERFLOW;
    return invoke(g_rope, arguments);
}
