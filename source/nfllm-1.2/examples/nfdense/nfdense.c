#include "../../sdk/include/nfllm_plugin_sdk.h"
#include <stdlib.h>
#include <string.h>

#define NFDENSE_PLUGIN_ID 0x4e464431UL
#define NFDENSE_QUANT_ID 0x8000f001UL
#define NFDENSE_OP_DEQUANTIZE 0x8000f101UL
#define NFDENSE_OP_MATVEC 0x8000f102UL
#define NFDENSE_FORMAT_ID "NFDENSE1"
#define NFDENSE_ARCH_ID "tiny-dense-v1"

struct TinyDenseState {
    uint8_t token_text[2];
    struct nfllm_token_view_v1 tokens[2];
    struct nfllm_tensor_view_v1 tensors[1];
};

static nfllm_status_v1 NFLLM_CALL toy_int8_validate_row(
    const uint8_t *packed_row, uint32_t packed_bytes, uint32_t columns)
{
    if (!packed_row || !columns) return NFLLM_E_ARGUMENT;
    return packed_bytes == columns ? NFLLM_OK : NFLLM_E_SIZE;
}

static nfllm_status_v1 NFLLM_CALL toy_matvec_f32(const void *opaque)
{
    const struct nfllm_matvec_f32_args_v1 *args =
        (const struct nfllm_matvec_f32_args_v1 *)opaque;
    uint32_t row, column;
    if (!args || args->struct_size < sizeof(*args) ||
        args->abi_version != NFLLM_ABI_VERSION_1 || !args->weights ||
        !args->input || !args->output || !args->rows || !args->columns)
        return NFLLM_E_ARGUMENT;
    if (args->rows > 0xffffffffUL / args->columns) return NFLLM_E_OVERFLOW;
    for (row = 0U; row < args->rows; ++row) {
        float sum = 0.0f;
        for (column = 0U; column < args->columns; ++column)
            sum += args->weights[row * args->columns + column] *
                   args->input[column];
        args->output[row] = sum;
    }
    return NFLLM_OK;
}

static int32_t NFLLM_CALL nfdense_probe(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_model_file_view_v1 *file)
{
    static const uint8_t magic[8] = { 'N','F','D','E','N','S','E','1' };
    (void)host;
    return file && file->struct_size >= sizeof(*file) && file->bytes &&
        file->byte_count >= 12U && memcmp(file->bytes, magic, sizeof(magic)) == 0;
}

static nfllm_status_v1 NFLLM_CALL nfdense_open(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_model_file_view_v1 *file,
    struct nfllm_model_view_v1 *model_view, void **format_state)
{
    struct TinyDenseState *state;
    (void)host;
    if (!file || !model_view || !format_state) return NFLLM_E_ARGUMENT;
    *format_state = 0;
    if (file->struct_size < sizeof(*file) || file->byte_count != 12U ||
        !nfdense_probe(host, file)) return NFLLM_E_UNSUPPORTED;
    state = (struct TinyDenseState *)calloc(1U, sizeof(*state));
    if (!state) return NFLLM_E_MEMORY;
    state->token_text[0] = (uint8_t)'a';
    state->token_text[1] = (uint8_t)'b';
    state->tokens[0].bytes = &state->token_text[0];
    state->tokens[0].byte_count = 1U;
    state->tokens[0].score = 0.0f;
    state->tokens[0].type = 0;
    state->tokens[1].bytes = &state->token_text[1];
    state->tokens[1].byte_count = 1U;
    state->tokens[1].score = 0.0f;
    state->tokens[1].type = 0;
    state->tensors[0].struct_size = sizeof(state->tensors[0]);
    state->tensors[0].abi_version = NFLLM_PLUGIN_ABI_V1;
    strcpy(state->tensors[0].name, "transition.weights.i8");
    state->tensors[0].dimensions = 2U;
    state->tensors[0].dims[0] = 2U;
    state->tensors[0].dims[1] = 2U;
    state->tensors[0].data_type_id = NFDENSE_QUANT_ID;
    state->tensors[0].data = file->bytes + 8U;
    state->tensors[0].byte_count = 4U;

    model_view->struct_size = sizeof(*model_view);
    model_view->abi_version = NFLLM_PLUGIN_ABI_V1;
    model_view->format_id = NFDENSE_FORMAT_ID;
    model_view->architecture_id = NFDENSE_ARCH_ID;
    model_view->tokenizer_id = "ascii-two-token";
    model_view->context_length = 1U;
    model_view->vocabulary_size = 2U;
    model_view->tokens = state->tokens;
    model_view->tensors = state->tensors;
    model_view->tensor_count = 1U;
    model_view->format_private = state;
    *format_state = state;
    return NFLLM_OK;
}

static void NFLLM_CALL nfdense_close(void *format_state)
{
    free(format_state);
}

static int32_t NFLLM_CALL nfdense_supports(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_model_view_v1 *model_view)
{
    (void)host;
    return model_view && model_view->struct_size >= sizeof(*model_view) &&
        model_view->abi_version == NFLLM_PLUGIN_ABI_V1 &&
        model_view->format_id && strcmp(model_view->format_id, NFDENSE_FORMAT_ID) == 0 &&
        model_view->architecture_id && strcmp(model_view->architecture_id, NFDENSE_ARCH_ID) == 0 &&
        model_view->vocabulary_size == 2U && model_view->tensor_count == 1U &&
        model_view->tokens && model_view->tensors;
}

static nfllm_status_v1 NFLLM_CALL nfdense_generate(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_model_view_v1 *model_view,
    const char *prompt, const struct nfllm_generation_options_v1 *options,
    nfllm_emit_token_v1 emit_token, void *emit_context,
    uint32_t *generated_tokens)
{
    const struct nfllm_tensor_view_v1 *tensor;
    const struct nfllm_quantization_v1 *quantization;
    const int8_t *weights;
    const void *descriptor = 0;
    uint32_t input_index, step;
    nfllm_status_v1 status;
    if (!host || !host->resolve_extension || !model_view || !options ||
        options->struct_size < sizeof(*options) ||
        options->abi_version != NFLLM_PLUGIN_ABI_V1 || !emit_token ||
        !generated_tokens) return NFLLM_E_ARGUMENT;
    *generated_tokens = 0U;
    if (!nfdense_supports(host, model_view) || !model_view->tokens[0].bytes ||
        !model_view->tokens[1].bytes) return NFLLM_E_UNSUPPORTED;
    tensor = &model_view->tensors[0];
    if (tensor->struct_size < sizeof(*tensor) || tensor->dimensions != 2U ||
        tensor->dims[0] != 2U || tensor->dims[1] != 2U ||
        tensor->data_type_id != NFDENSE_QUANT_ID || tensor->byte_count != 4U ||
        !tensor->data) return NFLLM_E_SIZE;
    status = host->resolve_extension(host->context, NFLLM_EXT_QUANTIZATION,
        NFDENSE_QUANT_ID, 0U, &descriptor);
    if (status != NFLLM_OK || !descriptor) return NFLLM_E_NOT_FOUND;
    quantization = (const struct nfllm_quantization_v1 *)descriptor;
    if (quantization->struct_size < sizeof(*quantization) ||
        quantization->abi_version != NFLLM_PLUGIN_ABI_V1 ||
        quantization->format_id != NFDENSE_QUANT_ID ||
        !quantization->validate_row) return NFLLM_E_ABI;
    weights = (const int8_t *)tensor->data;
    status = quantization->validate_row((const uint8_t *)weights, 2U, 2U);
    if (status != NFLLM_OK ||
        quantization->validate_row((const uint8_t *)weights + 2U, 2U, 2U) != NFLLM_OK)
        return NFLLM_E_SIZE;
    input_index = prompt && *prompt &&
        (prompt[strlen(prompt) - 1U] == 'B' || prompt[strlen(prompt) - 1U] == 'b');
    for (step = 0U; step < options->max_new_tokens; ++step) {
        int32_t score_a = weights[input_index];
        int32_t score_b = weights[2U + input_index];
        uint32_t token_id = score_b > score_a ? 1U : 0U;
        status = emit_token(emit_context, token_id,
            model_view->tokens[token_id].bytes,
            model_view->tokens[token_id].byte_count);
        if (status != NFLLM_OK) return status;
        ++*generated_tokens;
        input_index = token_id;
    }
    return NFLLM_OK;
}

static const struct nfllm_model_format_v1 nfdense_format = {
    sizeof(struct nfllm_model_format_v1), NFLLM_PLUGIN_ABI_V1,
    NFDENSE_FORMAT_ID, nfdense_probe, nfdense_open, nfdense_close
};
static const struct nfllm_neural_architecture_v1 nfdense_architecture = {
    sizeof(struct nfllm_neural_architecture_v1), NFLLM_PLUGIN_ABI_V1,
    NFDENSE_ARCH_ID, nfdense_supports, nfdense_generate
};
static const struct nfllm_quantization_v1 nfdense_quantization = {
    sizeof(struct nfllm_quantization_v1), NFLLM_PLUGIN_ABI_V1,
    NFDENSE_QUANT_ID, 8U, 2U, 2U, NFDENSE_OP_DEQUANTIZE,
    NFDENSE_OP_MATVEC, "NFDENSE_INT8_ROW", toy_int8_validate_row
};
static const struct nfllm_math_backend_v1 nfdense_math_backend = {
    sizeof(struct nfllm_math_backend_v1), NFLLM_PLUGIN_ABI_V1,
    "nfdense-scalar", "Scalar reference operators for the NFDENSE example."
};
static const struct nfllm_hardware_optimization_v1 nfdense_hardware = {
    sizeof(struct nfllm_hardware_optimization_v1), NFLLM_PLUGIN_ABI_V1,
    "386-x87", "Baseline 386 and x87 build; no newer ISA is required.",
    NFLLM_CAP_386 | NFLLM_CAP_X87
};
static const struct nfllm_kernel_descriptor_v1 nfdense_kernels[] = {
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_F32, NFLLM_VALUE_F32, NFLLM_VALUE_F32,
      NFLLM_CAP_X87, "nfdense.scalar.matvec_f32", toy_matvec_f32 }
};
static const struct nfllm_extension_v1 nfdense_extensions[] = {
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_QUANTIZATION, NFDENSE_QUANT_ID, 0U, 0,
      "NFDENSE_INT8_ROW", &nfdense_quantization },
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_MODEL_FORMAT, NFDENSE_PLUGIN_ID, 0U, 0,
      NFDENSE_FORMAT_ID, &nfdense_format },
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_NEURAL_ARCHITECTURE, NFDENSE_PLUGIN_ID, 0U, 0,
      NFDENSE_ARCH_ID, &nfdense_architecture },
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_MATH_BACKEND, NFDENSE_PLUGIN_ID, 0U, 0,
      "nfdense-scalar", &nfdense_math_backend },
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_HARDWARE_OPTIMIZATION, NFDENSE_PLUGIN_ID,
      NFLLM_CAP_386 | NFLLM_CAP_X87, 0,
      "386-x87", &nfdense_hardware }
};

NFLLM_DEFINE_PLUGIN_QUERY_V1(NFDENSE_PLUGIN_ID,
    NFLLM_CAP_386 | NFLLM_CAP_X87, "nfdense-toy-neural", "1.0.0",
    nfdense_kernels, nfdense_extensions,
    (uint32_t)(sizeof(nfdense_extensions) / sizeof(nfdense_extensions[0])))

