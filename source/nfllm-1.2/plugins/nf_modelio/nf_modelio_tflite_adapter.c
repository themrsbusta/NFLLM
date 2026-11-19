/*
 * Independently authored TFLite v3 FlatBuffer reader for NFLLM.
 * The parser uses the public FlatBuffers/TFLite schema layout; it does not
 * include generated schema code or implementation code from TinyEngine,
 * MCUNet, TensorFlow or LiteRT.
 */
#include "../../sdk/include/nfllm_graph_sdk.h"
#include <stdlib.h>
#include <string.h>

#define TFLITE_MAX_TENSORS 4096U
#define TFLITE_MAX_OPERATORS 8192U
#define TFLITE_MAX_BUFFERS 4096U
#define TFLITE_MAX_VECTOR_BYTES 0x40000000UL

struct TfliteState {
    const uint8_t *bytes;
    uint32_t byte_count;
    struct nfllm_graph_tensor_v1 *graph_tensors;
    struct nfllm_tensor_view_v1 *public_tensors;
    struct nfllm_graph_operator_v1 *operators;
    int32_t *inputs;
    int32_t *outputs;
    float **owned_scales;
    int64_t **owned_zero_points;
    uint32_t tensor_count;
    uint32_t operator_count;
    uint32_t input_count;
    uint32_t output_count;
    struct nfllm_model_graph_v1 graph;
};

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_i32(const uint8_t *p)
{
    return (int32_t)read_u32(p);
}

static int64_t read_i64(const uint8_t *p)
{
    uint64_t value = (uint64_t)read_u32(p) |
        ((uint64_t)read_u32(p + 4U) << 32);
    return (int64_t)value;
}

static float read_f32(const uint8_t *p)
{
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static int has_bytes(uint32_t size, uint32_t offset, uint32_t count)
{
    return offset <= size && count <= size - offset;
}

/* A FlatBuffer table starts with a signed displacement to its vtable. */
static int table_field(const uint8_t *bytes, uint32_t size, uint32_t table,
                       uint32_t field_index, uint32_t *field)
{
    int32_t displacement;
    uint32_t vtable, vtable_size, entry;
    if (!field || !has_bytes(size, table, 4U)) return 0;
    displacement = read_i32(bytes + table);
    if (displacement == 0 || displacement == (-2147483647L - 1L)) return 0;
    if (displacement > 0) {
        if ((uint32_t)displacement > table) return 0;
        vtable = table - (uint32_t)displacement;
    } else {
        uint32_t forward = (uint32_t)(-(int64_t)displacement);
        if (forward > size - table) return 0;
        vtable = table + forward;
    }
    if (!has_bytes(size, vtable, 4U)) return 0;
    vtable_size = read_u16(bytes + vtable);
    entry = 4U + field_index * 2U;
    if (entry + 2U > vtable_size || !has_bytes(size, vtable + entry, 2U))
        return 0;
    entry = read_u16(bytes + vtable + entry);
    if (!entry) return 0;
    if (entry > size - table || !has_bytes(size, table + entry, 1U)) return 0;
    *field = table + entry;
    return 1;
}

static int indirect_table(const uint8_t *bytes, uint32_t size,
                          uint32_t field, uint32_t *table)
{
    uint32_t distance;
    if (!has_bytes(size, field, 4U)) return 0;
    distance = read_u32(bytes + field);
    if (!distance || distance > size - field) return 0;
    *table = field + distance;
    return has_bytes(size, *table, 4U);
}

static int table_u32(const uint8_t *bytes, uint32_t size, uint32_t table,
                     uint32_t index, uint32_t fallback, uint32_t *value)
{
    uint32_t field;
    if (!table_field(bytes, size, table, index, &field)) {
        *value = fallback;
        return 1;
    }
    if (!has_bytes(size, field, 4U)) return 0;
    *value = read_u32(bytes + field);
    return 1;
}

static int table_i32(const uint8_t *bytes, uint32_t size, uint32_t table,
                     uint32_t index, int32_t fallback, int32_t *value)
{
    uint32_t field;
    if (!table_field(bytes, size, table, index, &field)) {
        *value = fallback;
        return 1;
    }
    if (!has_bytes(size, field, 4U)) return 0;
    *value = read_i32(bytes + field);
    return 1;
}

static int table_i8(const uint8_t *bytes, uint32_t size, uint32_t table,
                    uint32_t index, int8_t fallback, int8_t *value)
{
    uint32_t field;
    if (!table_field(bytes, size, table, index, &field)) {
        *value = fallback;
        return 1;
    }
    if (!has_bytes(size, field, 1U)) return 0;
    *value = (int8_t)bytes[field];
    return 1;
}

static int table_u8_as_u32(const uint8_t *bytes, uint32_t size, uint32_t table,
                           uint32_t index, uint8_t fallback, uint32_t *value)
{
    uint32_t field;
    if (!table_field(bytes, size, table, index, &field)) {
        *value = fallback;
        return 1;
    }
    if (!has_bytes(size, field, 1U)) return 0;
    *value = bytes[field];
    return 1;
}

static int table_vector(const uint8_t *bytes, uint32_t size, uint32_t table,
                        uint32_t field_index, uint32_t element_size,
                        uint32_t *data, uint32_t *count)
{
    uint32_t field, vector, length;
    if (!table_field(bytes, size, table, field_index, &field)) {
        *data = 0U; *count = 0U;
        return 1;
    }
    if (!indirect_table(bytes, size, field, &vector) ||
        !has_bytes(size, vector, 4U)) return 0;
    length = read_u32(bytes + vector);
    if (length > TFLITE_MAX_VECTOR_BYTES / (element_size ? element_size : 1U) ||
        (element_size && length > 0xffffffffUL / element_size) ||
        !has_bytes(size, vector + 4U, length * element_size)) return 0;
    *data = vector + 4U;
    *count = length;
    return 1;
}

static int vector_table(const uint8_t *bytes, uint32_t size, uint32_t data,
                        uint32_t count, uint32_t index, uint32_t *table)
{
    uint32_t entry;
    if (index >= count || index > 0xffffffffUL / 4U) return 0;
    entry = data + index * 4U;
    return has_bytes(size, entry, 4U) &&
        indirect_table(bytes, size, entry, table);
}

static int copy_schema_string(const uint8_t *bytes, uint32_t size,
                              uint32_t table, uint32_t field_index,
                              char *out, uint32_t capacity)
{
    uint32_t field, string, length, copy_count;
    if (!capacity) return 0;
    out[0] = '\0';
    if (!table_field(bytes, size, table, field_index, &field)) return 1;
    if (!indirect_table(bytes, size, field, &string) ||
        !has_bytes(size, string, 4U)) return 0;
    length = read_u32(bytes + string);
    if (!has_bytes(size, string + 4U, length)) return 0;
    copy_count = length < capacity - 1U ? length : capacity - 1U;
    memcpy(out, bytes + string + 4U, copy_count);
    out[copy_count] = '\0';
    return 1;
}

static void free_tflite_state(struct TfliteState *state)
{
    uint32_t i;
    if (!state) return;
    if (state->owned_scales)
        for (i = 0U; i < state->tensor_count; ++i) free(state->owned_scales[i]);
    if (state->owned_zero_points)
        for (i = 0U; i < state->tensor_count; ++i) free(state->owned_zero_points[i]);
    free(state->owned_scales);
    free(state->owned_zero_points);
    free(state->graph_tensors);
    free(state->public_tensors);
    free(state->operators);
    free(state->inputs);
    free(state->outputs);
    free(state);
}

static int parse_quantization(struct TfliteState *state, uint32_t tensor_index,
                              uint32_t tensor_table)
{
    struct nfllm_graph_tensor_v1 *tensor = &state->graph_tensors[tensor_index];
    uint32_t field, quant_table, scale_data, scale_count, zero_data, zero_count;
    uint32_t i, quant_dim;
    int32_t signed_quant_dim;
    if (!table_field(state->bytes, state->byte_count, tensor_table, 4U, &field))
        return 1;
    if (!indirect_table(state->bytes, state->byte_count, field, &quant_table))
        return 0;
    if (!table_vector(state->bytes, state->byte_count, quant_table, 2U, 4U,
                      &scale_data, &scale_count) ||
        !table_vector(state->bytes, state->byte_count, quant_table, 3U, 8U,
                      &zero_data, &zero_count) ||
        !table_i32(state->bytes, state->byte_count, quant_table, 6U, 0,
                   &signed_quant_dim)) return 0;
    if ((scale_count && !zero_count) || (!scale_count && zero_count) ||
        (scale_count && zero_count != 1U && zero_count != scale_count)) return 0;
    if (scale_count) {
        state->owned_scales[tensor_index] =
            (float *)malloc(scale_count * sizeof(float));
        if (!state->owned_scales[tensor_index]) return 0;
        for (i = 0U; i < scale_count; ++i)
            state->owned_scales[tensor_index][i] = read_f32(
                state->bytes + scale_data + i * 4U);
        state->owned_zero_points[tensor_index] =
            (int64_t *)malloc(zero_count * sizeof(int64_t));
        if (!state->owned_zero_points[tensor_index]) return 0;
        for (i = 0U; i < zero_count; ++i)
            state->owned_zero_points[tensor_index][i] = read_i64(
                state->bytes + zero_data + i * 8U);
    }
    tensor->scales = state->owned_scales[tensor_index];
    tensor->scale_count = scale_count;
    tensor->zero_points = state->owned_zero_points[tensor_index];
    tensor->zero_point_count = zero_count;
    quant_dim = (uint32_t)signed_quant_dim;
    tensor->quantized_dimension = signed_quant_dim;
    (void)quant_dim;
    return 1;
}

static int parse_tensor(struct TfliteState *state, uint32_t index,
                        uint32_t tensor_table, uint32_t buffer_data,
                        uint32_t buffer_bytes)
{
    struct nfllm_graph_tensor_v1 *tensor = &state->graph_tensors[index];
    struct nfllm_tensor_view_v1 *public_tensor = &state->public_tensors[index];
    uint32_t shape_data, shape_count, i, buffer_index;
    int32_t dimension;
    int8_t type;
    if (!table_vector(state->bytes, state->byte_count, tensor_table, 0U, 4U,
                      &shape_data, &shape_count) ||
        !table_i8(state->bytes, state->byte_count, tensor_table, 1U, 0, &type) ||
        !table_u32(state->bytes, state->byte_count, tensor_table, 2U, 0U,
                   &buffer_index)) return 0;
    if (!shape_count || shape_count > NFLLM_GRAPH_MAX_RANK ||
        buffer_index > TFLITE_MAX_BUFFERS) return 0;
    tensor->struct_size = sizeof(*tensor);
    tensor->abi_version = NFLLM_GRAPH_ABI_V1;
    tensor->rank = shape_count;
    tensor->element_type = (uint8_t)type;
    tensor->buffer_index = buffer_index;
    for (i = 0U; i < shape_count; ++i) {
        dimension = read_i32(state->bytes + shape_data + i * 4U);
        if (dimension <= 0) return 0;
        tensor->dims[i] = (uint32_t)dimension;
    }
    if (type != NFLLM_TFLITE_TYPE_INT8 && type != NFLLM_TFLITE_TYPE_INT32)
        return 0;
    if (buffer_data != 0U && buffer_bytes != 0U) {
        tensor->constant_data = state->bytes + buffer_data;
        tensor->constant_bytes = buffer_bytes;
    }
    if (!copy_schema_string(state->bytes, state->byte_count, tensor_table,
                            3U, tensor->name, sizeof(tensor->name))) return 0;
    if (!parse_quantization(state, index, tensor_table)) return 0;

    public_tensor->struct_size = sizeof(*public_tensor);
    public_tensor->abi_version = NFLLM_PLUGIN_ABI_V1;
    memcpy(public_tensor->name, tensor->name, sizeof(public_tensor->name));
    public_tensor->dimensions = shape_count;
    for (i = 0U; i < shape_count; ++i) public_tensor->dims[i] = tensor->dims[i];
    public_tensor->data_type_id = tensor->element_type;
    public_tensor->data = tensor->constant_data;
    public_tensor->byte_count = tensor->constant_bytes;
    return 1;
}

static int read_vector_i32(const uint8_t *bytes, uint32_t size,
                           uint32_t table, uint32_t field_index,
                           int32_t **values, uint32_t *value_count)
{
    uint32_t data, count, i;
    int32_t *copy;
    if (!table_vector(bytes, size, table, field_index, 4U, &data, &count) ||
        count > 0x00010000UL) return 0;
    copy = count ? (int32_t *)malloc(count * sizeof(int32_t)) : 0;
    if (count && !copy) return 0;
    for (i = 0U; i < count; ++i) copy[i] = read_i32(bytes + data + i * 4U);
    *values = copy;
    *value_count = count;
    return 1;
}

static int parse_operator(struct TfliteState *state, uint32_t index,
                          uint32_t operator_table, const uint32_t *opcodes,
                          uint32_t opcode_count)
{
    struct nfllm_graph_operator_v1 *operation = &state->operators[index];
    uint32_t opcode_index, field, options_table, opt_type, i;
    int32_t *input_values = 0, *output_values = 0;
    uint32_t input_count = 0U, output_count = 0U;
    int8_t deprecated;
    if (!table_u32(state->bytes, state->byte_count, operator_table, 0U, 0U,
                   &opcode_index) || opcode_index >= opcode_count ||
        !read_vector_i32(state->bytes, state->byte_count, operator_table, 1U,
                         &input_values, &input_count) ||
        !read_vector_i32(state->bytes, state->byte_count, operator_table, 2U,
                         &output_values, &output_count)) goto fail;
    if (input_count > NFLLM_GRAPH_MAX_OPERATOR_INPUTS || !output_count ||
        output_count > NFLLM_GRAPH_MAX_OPERATOR_OUTPUTS) goto fail;
    operation->struct_size = sizeof(*operation);
    operation->abi_version = NFLLM_GRAPH_ABI_V1;
    operation->builtin_operator = opcodes[opcode_index];
    operation->input_count = input_count;
    operation->output_count = output_count;
    for (i = 0U; i < input_count; ++i) operation->inputs[i] = input_values[i];
    for (i = 0U; i < output_count; ++i) operation->outputs[i] = output_values[i];
    free(input_values); input_values = 0;
    free(output_values); output_values = 0;

    if (!table_u8_as_u32(state->bytes, state->byte_count, operator_table, 3U,
                         0U, &opt_type)) return 0;
    if (table_field(state->bytes, state->byte_count, operator_table, 4U,
                    &field)) {
        if (!indirect_table(state->bytes, state->byte_count, field,
                            &options_table)) return 0;
        switch (operation->builtin_operator) {
        case NFLLM_TFLITE_OP_CONV_2D:
            if (opt_type != 1U ||
                !table_u8_as_u32(state->bytes, state->byte_count, options_table,
                           0U, 0U, &operation->padding) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           1U, 1U, &operation->stride_width) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           2U, 1U, &operation->stride_height) ||
                !table_u8_as_u32(state->bytes, state->byte_count, options_table,
                           3U, 0U, &operation->fused_activation) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           4U, 1U, &operation->dilation_width) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           5U, 1U, &operation->dilation_height)) return 0;
            break;
        case NFLLM_TFLITE_OP_DEPTHWISE_CONV_2D:
            if (opt_type != 2U ||
                !table_u8_as_u32(state->bytes, state->byte_count, options_table,
                           0U, 0U, &operation->padding) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           1U, 1U, &operation->stride_width) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           2U, 1U, &operation->stride_height) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           3U, 1U, &operation->depth_multiplier) ||
                !table_u8_as_u32(state->bytes, state->byte_count, options_table,
                           4U, 0U, &operation->fused_activation) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           5U, 1U, &operation->dilation_width) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           6U, 1U, &operation->dilation_height)) return 0;
            break;
        case NFLLM_TFLITE_OP_ADD:
            if (opt_type != 11U ||
                !table_u8_as_u32(state->bytes, state->byte_count, options_table,
                           0U, 0U, &operation->fused_activation)) return 0;
            break;
        case NFLLM_TFLITE_OP_AVERAGE_POOL_2D:
            if (opt_type != 5U ||
                !table_u8_as_u32(state->bytes, state->byte_count, options_table,
                           0U, 0U, &operation->padding) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           1U, 1U, &operation->stride_width) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           2U, 1U, &operation->stride_height) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           3U, 1U, &operation->filter_width) ||
                !table_u32(state->bytes, state->byte_count, options_table,
                           4U, 1U, &operation->filter_height) ||
                !table_u8_as_u32(state->bytes, state->byte_count, options_table,
                           5U, 0U, &operation->fused_activation)) return 0;
            operation->dilation_width = 1U;
            operation->dilation_height = 1U;
            break;
        case NFLLM_TFLITE_OP_PAD:
            if (opt_type != 22U) return 0;
            break;
        case NFLLM_TFLITE_OP_RESHAPE:
            if (opt_type != 17U) return 0;
            break;
        default:
            /* Unknown operators remain parseable and are rejected by supports(). */
            break;
        }
    } else {
        if (!table_i8(state->bytes, state->byte_count, operator_table, 3U,
                      0, &deprecated)) return 0;
        (void)deprecated;
    }
    return 1;
fail:
    free(input_values);
    free(output_values);
    return 0;
}

static int parse_model(struct TfliteState *state)
{
    uint32_t root, model_version, field, subgraph_vec, subgraph_count;
    uint32_t subgraph, tensor_vec, buffer_vec, buffer_count;
    uint32_t opcodes_vec, opcode_count, ops_vec, ops_count;
    uint32_t i, table, type, buffers_data, buffer_data, buffer_bytes;
    uint32_t *buffer_offsets = 0, *buffer_sizes = 0, *opcodes = 0;
    uint32_t data, count;
    int8_t deprecated;
    int ok = 0;
    if (state->byte_count < 8U || memcmp(state->bytes + 4U, "TFL3", 4U) != 0)
        return 0;
    root = read_u32(state->bytes);
    if (!has_bytes(state->byte_count, root, 4U) ||
        !table_u32(state->bytes, state->byte_count, root, 0U, 0U,
                   &model_version) || model_version != 3U ||
        !table_vector(state->bytes, state->byte_count, root, 2U, 4U,
                      &subgraph_vec, &subgraph_count) || subgraph_count != 1U ||
        !table_vector(state->bytes, state->byte_count, root, 1U, 4U,
                      &opcodes_vec, &opcode_count) || !opcode_count ||
        !table_vector(state->bytes, state->byte_count, root, 4U, 4U,
                      &buffer_vec, &buffer_count) || !buffer_count ||
        !vector_table(state->bytes, state->byte_count, subgraph_vec,
                      subgraph_count, 0U, &subgraph) ||
        !table_vector(state->bytes, state->byte_count, subgraph, 0U, 4U,
                      &tensor_vec, &state->tensor_count) ||
        !state->tensor_count || state->tensor_count > TFLITE_MAX_TENSORS ||
        !table_vector(state->bytes, state->byte_count, subgraph, 3U, 4U,
                      &ops_vec, &state->operator_count) ||
        state->operator_count > TFLITE_MAX_OPERATORS ||
        !table_vector(state->bytes, state->byte_count, subgraph, 1U, 4U,
                      &data, &count) || !count || count > 16U) return 0;

    buffer_offsets = (uint32_t *)calloc(buffer_count, sizeof(uint32_t));
    buffer_sizes = (uint32_t *)calloc(buffer_count, sizeof(uint32_t));
    opcodes = (uint32_t *)calloc(opcode_count, sizeof(uint32_t));
    state->graph_tensors = (struct nfllm_graph_tensor_v1 *)calloc(
        state->tensor_count, sizeof(*state->graph_tensors));
    state->public_tensors = (struct nfllm_tensor_view_v1 *)calloc(
        state->tensor_count, sizeof(*state->public_tensors));
    state->owned_scales = (float **)calloc(state->tensor_count, sizeof(float *));
    state->owned_zero_points = (int64_t **)calloc(state->tensor_count,
                                                  sizeof(int64_t *));
    state->operators = state->operator_count ?
        (struct nfllm_graph_operator_v1 *)calloc(state->operator_count,
                                                  sizeof(*state->operators)) : 0;
    if (!buffer_offsets || !buffer_sizes || !opcodes || !state->graph_tensors ||
        !state->public_tensors || !state->owned_scales ||
        !state->owned_zero_points ||
        (state->operator_count && !state->operators)) goto done;

    for (i = 0U; i < buffer_count; ++i) {
        uint32_t vector_data, vector_count;
        if (!vector_table(state->bytes, state->byte_count, buffer_vec,
                          buffer_count, i, &table) ||
            !table_vector(state->bytes, state->byte_count, table, 0U, 1U,
                          &vector_data, &vector_count)) goto done;
        buffer_offsets[i] = vector_data;
        buffer_sizes[i] = vector_count;
    }
    for (i = 0U; i < state->tensor_count; ++i) {
        if (!vector_table(state->bytes, state->byte_count, tensor_vec,
                          state->tensor_count, i, &table)) goto done;
        /* Read the buffer index first, then attach its bounded byte span. */
        if (!table_u32(state->bytes, state->byte_count, table, 2U, 0U,
                       &buffer_data) || buffer_data >= buffer_count) goto done;
        buffer_bytes = buffer_sizes[buffer_data];
        buffer_data = buffer_offsets[buffer_data];
        if (!parse_tensor(state, i, table, buffer_data, buffer_bytes)) goto done;
    }
    for (i = 0U; i < opcode_count; ++i) {
        uint32_t primary;
        if (!vector_table(state->bytes, state->byte_count, opcodes_vec,
                          opcode_count, i, &table) ||
            !table_i8(state->bytes, state->byte_count, table, 0U, 0,
                      &deprecated)) goto done;
        if (!table_field(state->bytes, state->byte_count, table, 3U, &field))
            primary = (uint8_t)deprecated;
        else {
            if (!has_bytes(state->byte_count, field, 4U)) goto done;
            primary = read_u32(state->bytes + field);
        }
        opcodes[i] = primary;
    }
    for (i = 0U; i < state->operator_count; ++i) {
        if (!vector_table(state->bytes, state->byte_count, ops_vec,
                          state->operator_count, i, &table) ||
            !parse_operator(state, i, table, opcodes, opcode_count)) goto done;
    }
    if (!read_vector_i32(state->bytes, state->byte_count, subgraph, 1U,
                         &state->inputs, &state->input_count) ||
        !read_vector_i32(state->bytes, state->byte_count, subgraph, 2U,
                         &state->outputs, &state->output_count) ||
        !state->input_count || !state->output_count ||
        state->input_count > 16U || state->output_count > 16U) goto done;
    state->graph.struct_size = sizeof(state->graph);
    state->graph.abi_version = NFLLM_GRAPH_ABI_V1;
    state->graph.tensors = state->graph_tensors;
    state->graph.tensor_count = state->tensor_count;
    state->graph.operators = state->operators;
    state->graph.operator_count = state->operator_count;
    state->graph.inputs = state->inputs;
    state->graph.input_count = state->input_count;
    state->graph.outputs = state->outputs;
    state->graph.output_count = state->output_count;
    ok = 1;
done:
    free(buffer_offsets);
    free(buffer_sizes);
    free(opcodes);
    return ok;
}

static int32_t NFLLM_CALL tflite_probe(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_model_file_view_v1 *file)
{
    (void)host;
    return file && file->struct_size >= sizeof(*file) && file->bytes &&
        file->byte_count >= 8U && memcmp(file->bytes + 4U, "TFL3", 4U) == 0;
}

static nfllm_status_v1 NFLLM_CALL tflite_open(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_model_file_view_v1 *file,
    struct nfllm_model_view_v1 *model_view, void **format_state)
{
    struct TfliteState *state;
    uint32_t i;
    (void)host;
    if (!file || !model_view || !format_state) return NFLLM_E_ARGUMENT;
    *format_state = 0;
    if (file->struct_size < sizeof(*file) || !file->bytes || file->byte_count < 8U)
        return NFLLM_E_ARGUMENT;
    state = (struct TfliteState *)calloc(1U, sizeof(*state));
    if (!state) return NFLLM_E_MEMORY;
    state->bytes = file->bytes;
    state->byte_count = file->byte_count;
    if (!parse_model(state)) {
        free_tflite_state(state);
        return NFLLM_E_UNSUPPORTED;
    }
    for (i = 0U; i < state->tensor_count; ++i) {
        if (state->graph_tensors[i].rank > NFLLM_MODEL_MAX_DIMS) {
            free_tflite_state(state);
            return NFLLM_E_UNSUPPORTED;
        }
    }
    model_view->struct_size = sizeof(*model_view);
    model_view->abi_version = NFLLM_PLUGIN_ABI_V1;
    model_view->format_id = NFLLM_GRAPH_FORMAT_ID;
    model_view->architecture_id = NFLLM_GRAPH_ARCH_ID;
    model_view->tokenizer_id = "none-classification";
    model_view->context_length = 1U;
    model_view->vocabulary_size = 0U;
    model_view->tokens = 0;
    model_view->tensors = state->public_tensors;
    model_view->tensor_count = state->tensor_count;
    model_view->format_private = &state->graph;
    *format_state = state;
    return NFLLM_OK;
}

static void NFLLM_CALL tflite_close(void *format_state)
{
    free_tflite_state((struct TfliteState *)format_state);
}

static const struct nfllm_model_format_v1 tflite_format = {
    sizeof(struct nfllm_model_format_v1), NFLLM_PLUGIN_ABI_V1,
    NFLLM_GRAPH_FORMAT_ID, tflite_probe, tflite_open, tflite_close
};

static const struct nfllm_extension_v1 tflite_extensions[] = {
    { sizeof(struct nfllm_extension_v1), NFLLM_PLUGIN_ABI_V1,
      NFLLM_EXT_MODEL_FORMAT, NFLLM_EXT_ID_TFLITE_FORMAT, 0U, 0,
      "TFLite v3 FlatBuffer reader (independent)", &tflite_format }
};

static const struct nfllm_plugin_bundle_v1 tflite_bundle = {
    sizeof(struct nfllm_plugin_bundle_v1), NFLLM_PLUGIN_ABI_V1,
    NFLLM_PLUGIN_ID_MODEL_IO, 0U, "NFLLM ModelIO TFLite v3 Adapter", "1.0.0",
    0, 0, 0, 0U, tflite_extensions, 1U
};

nfllm_status_v1 NFLLM_CALL NFLLM_PluginQueryV1(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_plugin_bundle_v1 **bundle)
{
    if (!host || !bundle) return NFLLM_E_ARGUMENT;
    if (host->struct_size < sizeof(*host) ||
        host->abi_version != NFLLM_PLUGIN_ABI_V1) return NFLLM_E_ABI;
    *bundle = &tflite_bundle;
    return NFLLM_OK;
}
