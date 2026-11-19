/*
 * NFLLM's original bounded graph executor and activation planner.
 * It uses the public NFLLM plugin SDK and the neutral graph ABI defined in
 * nfllm_graph_sdk.h. It does not link to TinyEngine, MCUNet, TensorFlow or
 * LiteRT source code or libraries.
 */
#include "../../sdk/include/nfllm_graph_sdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRAPH_MAX_ARENA_BYTES (48UL * 1024UL * 1024UL)
#define GRAPH_MAX_SINGLE_TENSOR (16UL * 1024UL * 1024UL)

struct ActivationPlan {
    uint32_t bytes;
    uint32_t first_operator;
    uint32_t last_operator;
    uint32_t offset;
    int allocated;
};

static uint32_t graph_element_bytes(uint32_t element_type)
{
    if (element_type == NFLLM_TFLITE_TYPE_INT8) return 1U;
    if (element_type == NFLLM_TFLITE_TYPE_INT32) return 4U;
    return 0U;
}

static int graph_tensor_bytes(const struct nfllm_graph_tensor_v1 *tensor,
                               uint32_t *bytes)
{
    uint32_t i, elements = 1U, element_bytes;
    if (!tensor || !bytes || tensor->rank == 0U ||
        tensor->rank > NFLLM_GRAPH_MAX_RANK) return 0;
    element_bytes = graph_element_bytes(tensor->element_type);
    if (!element_bytes) return 0;
    for (i = 0U; i < tensor->rank; ++i) {
        if (!tensor->dims[i] || elements > 0xffffffffUL / tensor->dims[i])
            return 0;
        elements *= tensor->dims[i];
    }
    if (elements > 0xffffffffUL / element_bytes) return 0;
    *bytes = elements * element_bytes;
    return *bytes <= GRAPH_MAX_SINGLE_TENSOR;
}

static int32_t NFLLM_CALL graph_supports(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_model_view_v1 *model_view)
{
    const struct nfllm_model_graph_v1 *graph;
    uint32_t i, j;
    (void)host;
    if (!model_view || model_view->struct_size < sizeof(*model_view) ||
        model_view->abi_version != NFLLM_PLUGIN_ABI_V1 ||
        !model_view->format_id || strcmp(model_view->format_id,
                                         NFLLM_GRAPH_FORMAT_ID) != 0 ||
        !model_view->architecture_id || strcmp(model_view->architecture_id,
                                                NFLLM_GRAPH_ARCH_ID) != 0 ||
        !model_view->format_private || !model_view->tensors ||
        !model_view->tensor_count) return 0;
    graph = (const struct nfllm_model_graph_v1 *)model_view->format_private;
    if (graph->struct_size < sizeof(*graph) ||
        graph->abi_version != NFLLM_GRAPH_ABI_V1 || !graph->tensors ||
        !graph->operators || !graph->operator_count || graph->input_count != 1U ||
        graph->output_count != 1U || !graph->inputs || !graph->outputs ||
        graph->tensor_count != model_view->tensor_count) return 0;
    if (graph->inputs[0] < 0 || graph->outputs[0] < 0 ||
        (uint32_t)graph->inputs[0] >= graph->tensor_count ||
        (uint32_t)graph->outputs[0] >= graph->tensor_count) return 0;
    if (graph->tensors[graph->inputs[0]].element_type != NFLLM_TFLITE_TYPE_INT8 ||
        graph->tensors[graph->outputs[0]].element_type != NFLLM_TFLITE_TYPE_INT8)
        return 0;
    for (i = 0U; i < graph->tensor_count; ++i) {
        uint32_t bytes;
        const struct nfllm_graph_tensor_v1 *tensor = &graph->tensors[i];
        if (tensor->struct_size < sizeof(*tensor) ||
            tensor->abi_version != NFLLM_GRAPH_ABI_V1 ||
            !graph_tensor_bytes(tensor, &bytes)) return 0;
        if (tensor->constant_data && tensor->constant_bytes < bytes) return 0;
        if (tensor->element_type == NFLLM_TFLITE_TYPE_INT8 &&
            (!tensor->scale_count || !tensor->zero_point_count)) return 0;
    }
    for (i = 0U; i < graph->operator_count; ++i) {
        const struct nfllm_graph_operator_v1 *op = &graph->operators[i];
        if (op->struct_size < sizeof(*op) ||
            op->abi_version != NFLLM_GRAPH_ABI_V1 ||
            op->input_count > NFLLM_GRAPH_MAX_OPERATOR_INPUTS ||
            op->output_count != 1U) return 0;
        switch (op->builtin_operator) {
        case NFLLM_TFLITE_OP_PAD:
            if (op->input_count != 2U) return 0;
            break;
        case NFLLM_TFLITE_OP_CONV_2D:
        case NFLLM_TFLITE_OP_DEPTHWISE_CONV_2D:
            if (op->input_count != 3U || !op->stride_width ||
                !op->stride_height || !op->dilation_width ||
                !op->dilation_height) return 0;
            break;
        case NFLLM_TFLITE_OP_ADD:
            if (op->input_count != 2U) return 0;
            break;
        case NFLLM_TFLITE_OP_AVERAGE_POOL_2D:
            if (op->input_count != 1U || !op->stride_width ||
                !op->stride_height || !op->filter_width ||
                !op->filter_height) return 0;
            break;
        case NFLLM_TFLITE_OP_RESHAPE:
            if (op->input_count < 1U || op->input_count > 2U) return 0;
            break;
        default:
            return 0;
        }
        for (j = 0U; j < op->input_count; ++j) {
            if (op->inputs[j] < -1 || (op->inputs[j] >= 0 &&
                (uint32_t)op->inputs[j] >= graph->tensor_count)) return 0;
        }
        for (j = 0U; j < op->output_count; ++j) {
            if (op->outputs[j] < 0 ||
                (uint32_t)op->outputs[j] >= graph->tensor_count) return 0;
        }
    }
    return 1;
}

static uint32_t overlap_end(uint32_t a, uint32_t a_bytes,
                            uint32_t b, uint32_t b_bytes)
{
    uint32_t a_end = a + a_bytes, b_end = b + b_bytes;
    return a_end < b_end ? a_end : b_end;
}

static int time_ranges_overlap(const struct ActivationPlan *left,
                               const struct ActivationPlan *right)
{
    return left->first_operator <= right->last_operator &&
        right->first_operator <= left->last_operator;
}

static int plan_activations(const struct nfllm_model_graph_v1 *graph,
                            struct ActivationPlan *plan,
                            uint32_t *arena_size)
{
    uint32_t i, j, k, candidate, bytes, end;
    for (i = 0U; i < graph->tensor_count; ++i) {
        plan[i].bytes = 0U;
        plan[i].first_operator = 0xffffffffUL;
        plan[i].last_operator = 0U;
        plan[i].offset = 0U;
        plan[i].allocated = 0;
    }
    for (i = 0U; i < graph->input_count; ++i) {
        uint32_t index = (uint32_t)graph->inputs[i];
        plan[index].first_operator = 0U;
    }
    for (i = 0U; i < graph->operator_count; ++i) {
        const struct nfllm_graph_operator_v1 *op = &graph->operators[i];
        for (j = 0U; j < op->input_count; ++j) {
            if (op->inputs[j] < 0) continue;
            k = (uint32_t)op->inputs[j];
            if (plan[k].last_operator < i) plan[k].last_operator = i;
        }
        for (j = 0U; j < op->output_count; ++j) {
            k = (uint32_t)op->outputs[j];
            if (plan[k].first_operator == 0xffffffffUL)
                plan[k].first_operator = i;
            if (plan[k].last_operator < i) plan[k].last_operator = i;
        }
    }
    for (i = 0U; i < graph->output_count; ++i) {
        k = (uint32_t)graph->outputs[i];
        plan[k].last_operator = graph->operator_count;
    }
    for (i = 0U; i < graph->tensor_count; ++i) {
        if (graph->tensors[i].constant_data && graph->tensors[i].constant_bytes)
            continue;
        if (!graph_tensor_bytes(&graph->tensors[i], &bytes) ||
            plan[i].first_operator == 0xffffffffUL) return 0;
        plan[i].bytes = bytes;
        plan[i].allocated = 1;
        candidate = 0U;
        for (;;) {
            int conflict = 0;
            for (j = 0U; j < i; ++j) {
                if (!plan[j].allocated ||
                    !time_ranges_overlap(&plan[i], &plan[j])) continue;
                if (candidate < plan[j].offset + plan[j].bytes &&
                    plan[j].offset < candidate + plan[i].bytes) {
                    candidate = plan[j].offset + plan[j].bytes;
                    conflict = 1;
                    break;
                }
            }
            if (!conflict) break;
        }
        if (candidate > 0xffffffffUL - bytes) return 0;
        plan[i].offset = candidate;
        end = candidate + bytes;
        if (end > *arena_size) *arena_size = end;
        if (*arena_size > GRAPH_MAX_ARENA_BYTES) return 0;
    }
    return 1;
}

static int read_input_file(const char *path, uint8_t *destination,
                           uint32_t expected_bytes)
{
    FILE *file;
    size_t got;
    int extra;
    if (!path || !*path || !destination || !expected_bytes) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    got = fread(destination, 1U, expected_bytes, file);
    extra = fgetc(file);
    fclose(file);
    return got == expected_bytes && extra == EOF;
}

static uint32_t operator_kernel_id(uint32_t builtin)
{
    switch (builtin) {
    case NFLLM_TFLITE_OP_PAD: return NFLLM_OP_GRAPH_PAD;
    case NFLLM_TFLITE_OP_CONV_2D: return NFLLM_OP_GRAPH_CONV2D;
    case NFLLM_TFLITE_OP_DEPTHWISE_CONV_2D:
        return NFLLM_OP_GRAPH_DEPTHWISE_CONV2D;
    case NFLLM_TFLITE_OP_ADD: return NFLLM_OP_GRAPH_ADD;
    case NFLLM_TFLITE_OP_AVERAGE_POOL_2D: return NFLLM_OP_GRAPH_AVERAGE_POOL;
    case NFLLM_TFLITE_OP_RESHAPE: return NFLLM_OP_GRAPH_RESHAPE;
    default: return 0U;
    }
}

static nfllm_status_v1 NFLLM_CALL graph_generate(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_model_view_v1 *model_view,
    const char *prompt, const struct nfllm_generation_options_v1 *options,
    nfllm_emit_token_v1 emit_token, void *emit_context,
    uint32_t *generated_tokens)
{
    const struct nfllm_model_graph_v1 *graph;
    struct ActivationPlan *plan = 0;
    uint8_t *arena = 0;
    uint8_t **activations = 0;
    uint32_t arena_size = 0U, input_index, output_index, input_bytes;
    uint32_t i;
    nfllm_status_v1 status = NFLLM_E_STATE;
    if (!host || !host->resolve_extension || !model_view || !options ||
        options->struct_size < sizeof(*options) ||
        options->abi_version != NFLLM_PLUGIN_ABI_V1 || !emit_token ||
        !generated_tokens) return NFLLM_E_ARGUMENT;
    *generated_tokens = 0U;
    if (!graph_supports(host, model_view)) return NFLLM_E_UNSUPPORTED;
    if (!options->max_new_tokens) return NFLLM_OK;
    graph = (const struct nfllm_model_graph_v1 *)model_view->format_private;
    input_index = (uint32_t)graph->inputs[0];
    output_index = (uint32_t)graph->outputs[0];
    if (!graph_tensor_bytes(&graph->tensors[input_index], &input_bytes) ||
        graph->tensors[input_index].element_type != NFLLM_TFLITE_TYPE_INT8)
        return NFLLM_E_SIZE;
    plan = (struct ActivationPlan *)calloc(graph->tensor_count, sizeof(*plan));
    activations = (uint8_t **)calloc(graph->tensor_count, sizeof(*activations));
    if (!plan || !activations) { status = NFLLM_E_MEMORY; goto cleanup; }
    if (!plan_activations(graph, plan, &arena_size) || !arena_size) {
        status = NFLLM_E_UNSUPPORTED; goto cleanup;
    }
    arena = (uint8_t *)malloc(arena_size);
    if (!arena) { status = NFLLM_E_MEMORY; goto cleanup; }
    memset(arena, 0, arena_size);
    for (i = 0U; i < graph->tensor_count; ++i) {
        if (graph->tensors[i].constant_data && graph->tensors[i].constant_bytes)
            activations[i] = (uint8_t *)(uintptr_t)graph->tensors[i].constant_data;
        else if (plan[i].allocated)
            activations[i] = arena + plan[i].offset;
    }
    if (!activations[input_index] ||
        !read_input_file(prompt, activations[input_index], input_bytes)) {
        status = NFLLM_E_SIZE; goto cleanup;
    }
    for (i = 0U; i < graph->operator_count; ++i) {
        const struct nfllm_graph_operator_v1 *op = &graph->operators[i];
        const struct nfllm_kernel_descriptor_v1 *kernel = 0;
        struct nfllm_graph_operator_call_v1 call;
        const void *descriptor = 0;
        uint32_t kernel_id = operator_kernel_id(op->builtin_operator);
        if (!kernel_id) { status = NFLLM_E_UNSUPPORTED; goto cleanup; }
        status = host->resolve_extension(host->context, NFLLM_EXT_OPERATOR,
            kernel_id, 0U, &descriptor);
        if (status != NFLLM_OK || !descriptor) goto cleanup;
        kernel = (const struct nfllm_kernel_descriptor_v1 *)descriptor;
        if (kernel->struct_size < sizeof(*kernel) ||
            kernel->abi_version != NFLLM_ABI_VERSION_1 ||
            kernel->operation_id != kernel_id || !kernel->execute) {
            status = NFLLM_E_ABI; goto cleanup;
        }
        call.struct_size = sizeof(call);
        call.abi_version = NFLLM_GRAPH_ABI_V1;
        call.graph = graph;
        call.operation = op;
        call.activations = activations;
        status = kernel->execute(&call);
        if (status != NFLLM_OK) goto cleanup;
    }
    if (!activations[output_index] ||
        graph->tensors[output_index].element_type != NFLLM_TFLITE_TYPE_INT8 ||
        graph->tensors[output_index].scale_count != 1U ||
        graph->tensors[output_index].zero_point_count < 1U) {
        status = NFLLM_E_SIZE; goto cleanup;
    }
    {
        uint32_t output_bytes, element_count, j, best = 0U;
        char result_text[96];
        const int8_t *logits = (const int8_t *)activations[output_index];
        int length;
        if (!graph_tensor_bytes(&graph->tensors[output_index], &output_bytes)) {
            status = NFLLM_E_SIZE; goto cleanup;
        }
        element_count = output_bytes;
        if (element_count != 2U) {
            status = NFLLM_E_UNSUPPORTED; goto cleanup;
        }
        for (j = 1U; j < element_count; ++j)
            if (logits[j] > logits[best]) best = j;
        length = sprintf(result_text, "class=%lu logits=[%d,%d]",
            (unsigned long)best, (int)logits[0], (int)logits[1]);
        if (length <= 0 || (uint32_t)length >= sizeof(result_text)) {
            status = NFLLM_E_NUMERIC; goto cleanup;
        }
        status = emit_token(emit_context, best,
            (const uint8_t *)result_text, (uint32_t)length);
        if (status == NFLLM_OK) *generated_tokens = 1U;
    }
cleanup:
    free(arena);
    free(activations);
    free(plan);
    return status;
}

static const struct nfllm_neural_architecture_v1 graph_architecture = {
    sizeof(struct nfllm_neural_architecture_v1), NFLLM_PLUGIN_ABI_V1,
    NFLLM_GRAPH_ARCH_ID, graph_supports, graph_generate
};

static const struct nfllm_math_backend_v1 graph_math_backend = {
    sizeof(struct nfllm_math_backend_v1), NFLLM_PLUGIN_ABI_V1,
    "nfllm.int8.scalar",
    "Original scalar 386 graph executor with planned activation storage."
};

static const struct nfllm_extension_v1 nfgraphvm_extensions[] = {
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_NEURAL_ARCHITECTURE, NFLLM_EXT_ID_GRAPH_VM, 0U, 0,
      "NFLLM GraphVM INT8 Architecture", &graph_architecture },
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_MATH_BACKEND, NFLLM_PLUGIN_ID_GRAPH_VM, 0U, 0,
      "nfllm.int8.scalar", &graph_math_backend }
};

static const struct nfllm_plugin_bundle_v1 nfgraphvm_bundle = {
    sizeof(struct nfllm_plugin_bundle_v1), NFLLM_PLUGIN_ABI_V1,
    NFLLM_PLUGIN_ID_GRAPH_VM, NFLLM_CAP_386,
    "NFLLM GraphVM INT8 Runtime", "1.0.0",
    0, 0, 0, 0U, nfgraphvm_extensions,
    (uint32_t)(sizeof(nfgraphvm_extensions) / sizeof(nfgraphvm_extensions[0]))
};

nfllm_status_v1 NFLLM_CALL NFLLM_PluginQueryV1(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_plugin_bundle_v1 **bundle)
{
    if (!host || !bundle) return NFLLM_E_ARGUMENT;
    if (host->struct_size < sizeof(*host) ||
        host->abi_version != NFLLM_PLUGIN_ABI_V1) return NFLLM_E_ABI;
    *bundle = &nfgraphvm_bundle;
    return NFLLM_OK;
}
