/*
 * NFLLM: small native DOS inference engine for the Am486 acceptance target.
 *
 * This backend implements LLaMA transformer math with a 386+x87 baseline and
 * optional manually declared ISA kernels. It supports F32, Q8_0, Q4_0, Q2_0,
 * Q2_K, IQ3_XXS, and IQ3_S GGUF weight storage, greedy generation, and no
 * threads, networking, or host APIs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "src/nfllm_abi.h"
#include "src/nfllm_registry.h"
#include "src/nfllm_logic.h"

typedef unsigned char u8;
typedef unsigned int u32;

#define MAX_TENSORS       128
#define MAX_DIMS          4
#define MAX_VOCAB         2048
#define MAX_LAYERS        32
#define MAX_CONTEXT       2048
#define MAX_PROMPT_BYTES  1024
#define MAX_QUANT_COLUMNS 1024
#define GGML_TYPE_F32     0
#define GGML_TYPE_Q4_0    2
#define GGML_TYPE_Q8_0    8
#define GGML_TYPE_Q2_K    10
#define GGML_TYPE_IQ3_XXS 18
#define GGML_TYPE_IQ3_S   21
#define GGML_TYPE_Q2_0    42
#define GGML_Q4_0_BLOCK_BYTES 18
#define GGML_Q8_0_BLOCK_BYTES 34
#define GGML_Q2_0_BLOCK_BYTES 18
#define GGML_Q2_K_BLOCK_BYTES 84
#define GGML_IQ3_XXS_BLOCK_BYTES 98
#define GGML_IQ3_S_BLOCK_BYTES 110

static u32 g_direct_q8_rows;
static u32 g_direct_q4_rows;
static u32 g_direct_q20_rows;
static u32 g_direct_q2k_rows;
static u32 g_direct_iq3xxs_rows;
static u32 g_direct_iq3s_rows;
static int g_force_quantized_decode;
static int g_force_quantized_scalar;

/* Read CR4 only for an explicitly selected Atom profile; never on the 486. */
static u32 read_cr4(void);
#pragma aux read_cr4 = \
    "db 0fh, 020h, 0e0h" \
    __value [__eax] \
    __modify [__eax]

static int quantized_type_info(u32 type, u32 *block_elements, u32 *block_bytes)
{
    if (!block_elements || !block_bytes) return 0;
    switch (type) {
    case GGML_TYPE_Q4_0:
        *block_elements = 32U; *block_bytes = GGML_Q4_0_BLOCK_BYTES; return 1;
    case GGML_TYPE_Q8_0:
        *block_elements = 32U; *block_bytes = GGML_Q8_0_BLOCK_BYTES; return 1;
    case GGML_TYPE_Q2_0:
        *block_elements = 64U; *block_bytes = GGML_Q2_0_BLOCK_BYTES; return 1;
    case GGML_TYPE_Q2_K:
        *block_elements = 256U; *block_bytes = GGML_Q2_K_BLOCK_BYTES; return 1;
    case GGML_TYPE_IQ3_XXS:
        *block_elements = 256U; *block_bytes = GGML_IQ3_XXS_BLOCK_BYTES; return 1;
    case GGML_TYPE_IQ3_S:
        *block_elements = 256U; *block_bytes = GGML_IQ3_S_BLOCK_BYTES; return 1;
    default:
        return 0;
    }
}

static u32 dequantize_operation_for_type(u32 type)
{
    switch (type) {
    case GGML_TYPE_Q8_0: return NFLLM_OP_DEQUANTIZE_ROW_Q8_0;
    case GGML_TYPE_Q4_0: return NFLLM_OP_DEQUANTIZE_ROW_Q4_0;
    case GGML_TYPE_Q2_0: return NFLLM_OP_DEQUANTIZE_ROW_Q2_0;
    case GGML_TYPE_Q2_K: return NFLLM_OP_DEQUANTIZE_ROW_Q2_K;
    case GGML_TYPE_IQ3_XXS: return NFLLM_OP_DEQUANTIZE_ROW_IQ3_XXS;
    case GGML_TYPE_IQ3_S: return NFLLM_OP_DEQUANTIZE_ROW_IQ3_S;
    default: return 0U;
    }
}

static u32 direct_matvec_operation_for_type(u32 type)
{
    switch (type) {
    case GGML_TYPE_Q2_0: return NFLLM_OP_MATVEC_Q2_0_ROW;
    case GGML_TYPE_Q2_K: return NFLLM_OP_MATVEC_Q2_K_ROW;
    case GGML_TYPE_IQ3_XXS: return NFLLM_OP_MATVEC_IQ3_XXS_ROW;
    case GGML_TYPE_IQ3_S: return NFLLM_OP_MATVEC_IQ3_S_ROW;
    default: return 0U;
    }
}

enum GgufType {
    GGUF_UINT8 = 0,
    GGUF_INT8 = 1,
    GGUF_UINT16 = 2,
    GGUF_INT16 = 3,
    GGUF_UINT32 = 4,
    GGUF_INT32 = 5,
    GGUF_FLOAT32 = 6,
    GGUF_BOOL = 7,
    GGUF_STRING = 8,
    GGUF_ARRAY = 9,
    GGUF_UINT64 = 10,
    GGUF_INT64 = 11,
    GGUF_FLOAT64 = 12
};

struct Reader {
    const u8 *data;
    u32 size;
    u32 pos;
    int failed;
};

struct TensorInfo {
    char name[64];
    u32 n_dims;
    u32 dims[MAX_DIMS];
    u32 type;
    u32 relative_offset;
    u32 file_offset;
    u32 byte_count;
};

struct Model {
    const u8 *file;
    u32 file_size;
    u32 version;
    u32 n_tensors;
    u32 alignment;
    u32 data_offset;
    char architecture[32];
    char tokenizer_model[32];
    u32 n_vocab;
    const u8 *tokens[MAX_VOCAB];
    u32 token_lengths[MAX_VOCAB];
    float token_scores[MAX_VOCAB];
    int token_types[MAX_VOCAB];
    u32 embedding_length;
    u32 layer_count;
    u32 head_count;
    u32 head_count_kv;
    u32 feed_forward_length;
    u32 rope_dimensions;
    u32 context_length;
    u32 bos_id;
    u32 eos_id;
    u32 unknown_token_id;
    u32 score_count;
    u32 token_type_count;
    float rope_base;
    float rms_epsilon;
    int add_bos;
    int have_tokens;
    int have_scores;
    int have_token_types;
    struct TensorInfo tensors[MAX_TENSORS];
};

static int range_ok(const struct Reader *r, u32 amount)
{
    return !r->failed && amount <= r->size - r->pos;
}

static u32 read_u32(struct Reader *r)
{
    u32 value;
    if (!range_ok(r, 4)) { r->failed = 1; return 0; }
    value = (u32)r->data[r->pos] |
            ((u32)r->data[r->pos + 1] << 8) |
            ((u32)r->data[r->pos + 2] << 16) |
            ((u32)r->data[r->pos + 3] << 24);
    r->pos += 4;
    return value;
}

static u32 read_u64_small(struct Reader *r)
{
    u32 low;
    u32 high;
    low = read_u32(r);
    high = read_u32(r);
    if (high != 0) r->failed = 1;
    return low;
}

static float read_f32(struct Reader *r)
{
    union { u32 bits; float value; } convert;
    convert.bits = read_u32(r);
    return convert.value;
}

static int read_string(struct Reader *r, const u8 **text, u32 *length)
{
    u32 count = read_u64_small(r);
    if (r->failed || !range_ok(r, count)) { r->failed = 1; return 0; }
    *text = r->data + r->pos;
    *length = count;
    r->pos += count;
    return 1;
}

static int text_is(const char *key, const char *expected)
{
    return strcmp(key, expected) == 0;
}

static int copy_string_value(struct Reader *r, char *out, u32 capacity)
{
    const u8 *text;
    u32 length;
    if (!read_string(r, &text, &length) || length >= capacity) {
        r->failed = 1;
        return 0;
    }
    memcpy(out, text, length);
    out[length] = '\0';
    return 1;
}

static int skip_scalar(struct Reader *r, u32 type)
{
    u32 count;
    switch (type) {
    case GGUF_UINT8: case GGUF_INT8: case GGUF_BOOL: count = 1; break;
    case GGUF_UINT16: case GGUF_INT16: count = 2; break;
    case GGUF_UINT32: case GGUF_INT32: case GGUF_FLOAT32: count = 4; break;
    case GGUF_UINT64: case GGUF_INT64: case GGUF_FLOAT64: count = 8; break;
    case GGUF_STRING: {
        const u8 *unused;
        u32 length;
        return read_string(r, &unused, &length);
    }
    default:
        r->failed = 1;
        return 0;
    }
    if (!range_ok(r, count)) { r->failed = 1; return 0; }
    r->pos += count;
    return 1;
}

static int parse_array(struct Reader *r, struct Model *m, const char *key)
{
    u32 element_type = read_u32(r);
    u32 count = read_u64_small(r);
    u32 i;
    if (r->failed || element_type == GGUF_ARRAY || count > 1000000UL) {
        r->failed = 1;
        return 0;
    }

    if (text_is(key, "tokenizer.ggml.tokens")) {
        if (element_type != GGUF_STRING || count == 0 || count > MAX_VOCAB) {
            r->failed = 1; return 0;
        }
        m->n_vocab = count;
        for (i = 0; i < count; ++i) {
            if (!read_string(r, &m->tokens[i], &m->token_lengths[i])) return 0;
        }
        m->have_tokens = 1;
        return 1;
    }

    if (text_is(key, "tokenizer.ggml.scores")) {
        if (element_type != GGUF_FLOAT32 || count > MAX_VOCAB) { r->failed = 1; return 0; }
        for (i = 0; i < count; ++i) m->token_scores[i] = read_f32(r);
        m->score_count = count;
        m->have_scores = 1;
        return !r->failed;
    }

    if (text_is(key, "tokenizer.ggml.token_type")) {
        if (element_type != GGUF_INT32 || count > MAX_VOCAB) { r->failed = 1; return 0; }
        for (i = 0; i < count; ++i) m->token_types[i] = (int)read_u32(r);
        m->token_type_count = count;
        m->have_token_types = 1;
        return !r->failed;
    }

    for (i = 0; i < count && !r->failed; ++i) skip_scalar(r, element_type);
    return !r->failed;
}

static int parse_metadata_value(struct Reader *r, struct Model *m,
                                const char *key, u32 type)
{
    u32 value;
    if (type == GGUF_ARRAY) return parse_array(r, m, key);

    if (type == GGUF_STRING && text_is(key, "general.architecture"))
        return copy_string_value(r, m->architecture, sizeof(m->architecture));
    if (type == GGUF_STRING && text_is(key, "tokenizer.ggml.model"))
        return copy_string_value(r, m->tokenizer_model, sizeof(m->tokenizer_model));

    if (type == GGUF_UINT32) {
        value = read_u32(r);
        if (text_is(key, "general.alignment")) m->alignment = value;
        else if (text_is(key, "llama.embedding_length")) m->embedding_length = value;
        else if (text_is(key, "llama.block_count")) m->layer_count = value;
        else if (text_is(key, "llama.attention.head_count")) m->head_count = value;
        else if (text_is(key, "llama.attention.head_count_kv")) m->head_count_kv = value;
        else if (text_is(key, "llama.feed_forward_length")) m->feed_forward_length = value;
        else if (text_is(key, "llama.rope.dimension_count")) m->rope_dimensions = value;
        else if (text_is(key, "llama.context_length")) m->context_length = value;
        else if (text_is(key, "tokenizer.ggml.bos_token_id")) m->bos_id = value;
        else if (text_is(key, "tokenizer.ggml.eos_token_id")) m->eos_id = value;
        else if (text_is(key, "tokenizer.ggml.unknown_token_id")) m->unknown_token_id = value;
        return !r->failed;
    }

    if (type == GGUF_FLOAT32) {
        float value_f = read_f32(r);
        if (text_is(key, "llama.rope.freq_base")) m->rope_base = value_f;
        else if (text_is(key, "llama.attention.layer_norm_rms_epsilon")) m->rms_epsilon = value_f;
        return !r->failed;
    }

    if (type == GGUF_BOOL) {
        u32 value_b;
        if (!range_ok(r, 1)) { r->failed = 1; return 0; }
        value_b = r->data[r->pos++];
        if (text_is(key, "tokenizer.ggml.add_bos_token")) m->add_bos = value_b != 0;
        return 1;
    }

    return skip_scalar(r, type);
}

static int parse_gguf(struct Model *m)
{
    struct Reader r;
    u32 magic, metadata_count, i, j;
    u32 raw_tensor_count;
    memset(&r, 0, sizeof(r));
    r.data = m->file;
    r.size = m->file_size;
    m->alignment = 32;
    m->rope_base = 10000.0f;
    m->rms_epsilon = 0.00001f;
    m->eos_id = 0xffffffffUL;
    m->unknown_token_id = 0;
    m->add_bos = 1;

    magic = read_u32(&r);
    m->version = read_u32(&r);
    raw_tensor_count = read_u64_small(&r);
    metadata_count = read_u64_small(&r);
    if (r.failed || magic != 0x46554747UL || m->version < 2 || m->version > 3 ||
        raw_tensor_count == 0 || raw_tensor_count > MAX_TENSORS || metadata_count > 512) {
        return 0;
    }
    m->n_tensors = raw_tensor_count;

    for (i = 0; i < metadata_count && !r.failed; ++i) {
        const u8 *key_bytes;
        u32 key_length;
        char key[128];
        u32 value_type;
        if (!read_string(&r, &key_bytes, &key_length) || key_length >= sizeof(key)) {
            r.failed = 1; break;
        }
        memcpy(key, key_bytes, key_length); key[key_length] = '\0';
        value_type = read_u32(&r);
        parse_metadata_value(&r, m, key, value_type);
    }

    if (r.failed) return 0;
    for (i = 0; i < m->n_tensors && !r.failed; ++i) {
        struct TensorInfo *t = &m->tensors[i];
        const u8 *name;
        u32 name_length;
        if (!read_string(&r, &name, &name_length) || name_length == 0 || name_length >= sizeof(t->name)) {
            r.failed = 1; break;
        }
        memcpy(t->name, name, name_length); t->name[name_length] = '\0';
        t->n_dims = read_u32(&r);
        if (t->n_dims == 0 || t->n_dims > MAX_DIMS) { r.failed = 1; break; }
        for (j = 0; j < t->n_dims; ++j) {
            t->dims[j] = read_u64_small(&r);
            if (t->dims[j] == 0) r.failed = 1;
        }
        t->type = read_u32(&r);
        t->relative_offset = read_u64_small(&r);
    }
    if (r.failed || m->alignment == 0 || (m->alignment & (m->alignment - 1)) != 0) return 0;
    while ((r.pos & (m->alignment - 1)) != 0) {
        if (!range_ok(&r, 1)) return 0;
        ++r.pos;
    }
    m->data_offset = r.pos;

    for (i = 0; i < m->n_tensors; ++i) {
        struct TensorInfo *t = &m->tensors[i];
        u32 elements = 1;
        for (j = 0; j < t->n_dims; ++j) {
            if (t->dims[j] > 0xffffffffUL / elements) return 0;
            elements *= t->dims[j];
        }
        if (t->type == GGML_TYPE_F32) {
            if (elements > 0xffffffffUL / 4) return 0;
            t->byte_count = elements * 4;
        } else {
            u32 block_elements, block_bytes, blocks;
            if (!quantized_type_info(t->type, &block_elements, &block_bytes) ||
                (t->dims[0] % block_elements) != 0U ||
                (elements % block_elements) != 0U) return 0;
            blocks = elements / block_elements;
            if (blocks > 0xffffffffUL / block_bytes) return 0;
            t->byte_count = blocks * block_bytes;
        }
        if (t->relative_offset > 0xffffffffUL - m->data_offset) return 0;
        t->file_offset = m->data_offset + t->relative_offset;
        if (t->file_offset > m->file_size || t->byte_count > m->file_size - t->file_offset) return 0;
    }
    return 1;
}

static struct TensorInfo *find_tensor(struct Model *m, const char *name)
{
    u32 i;
    for (i = 0; i < m->n_tensors; ++i)
        if (strcmp(m->tensors[i].name, name) == 0) return &m->tensors[i];
    return NULL;
}

static const float *tensor_data(const struct Model *m, const struct TensorInfo *t)
{
    if (!t || t->type != GGML_TYPE_F32) return NULL;
    return (const float *)(m->file + t->file_offset);
}

static int check_shape(const struct TensorInfo *t, u32 n_dims, u32 d0, u32 d1)
{
    return t && t->n_dims == n_dims && t->dims[0] == d0 &&
           (n_dims < 2 || t->dims[1] == d1);
}

static int check_f32_shape(const struct TensorInfo *t, u32 n_dims,
                           u32 d0, u32 d1)
{
    return check_shape(t, n_dims, d0, d1) && t->type == GGML_TYPE_F32;
}

static int check_matrix_shape(const struct TensorInfo *t, u32 columns,
                              u32 rows)
{
    u32 block_elements, block_bytes;
    if (!check_shape(t, 2U, columns, rows)) return 0;
    if (t->type == GGML_TYPE_F32) return 1;
    return quantized_type_info(t->type, &block_elements, &block_bytes) &&
           (columns % block_elements) == 0U;
}

static int quantized_row_bytes(const struct TensorInfo *tensor, u32 columns,
                               u32 *row_bytes)
{
    u32 block_elements, block_bytes;
    u32 blocks;
    if (!tensor || !row_bytes || columns == 0U ||
        !quantized_type_info(tensor->type, &block_elements, &block_bytes) ||
        (columns % block_elements) != 0U) return 0;
    blocks = columns / block_elements;
    if (blocks > 0xffffffffUL / block_bytes) return 0;
    *row_bytes = blocks * block_bytes;
    return 1;
}

static int read_tensor_row_f32(const struct Model *m,
                               const struct TensorInfo *tensor,
                               u32 row, float *output)
{
    u32 columns, row_bytes;
    const u8 *packed;
    if (!m || !tensor || !output || tensor->n_dims != 2U ||
        row >= tensor->dims[1]) return 0;
    columns = tensor->dims[0];
    if (tensor->type == GGML_TYPE_F32) {
        u32 row_elements;
        if (columns > 0xffffffffUL / sizeof(float)) return 0;
        row_elements = row * columns;
        if (row_elements > 0xffffffffUL / sizeof(float) ||
            row_elements * sizeof(float) > tensor->byte_count ||
            columns * sizeof(float) > tensor->byte_count - row_elements * sizeof(float))
            return 0;
        memcpy(output, m->file + tensor->file_offset + row_elements * sizeof(float),
               columns * sizeof(float));
        return 1;
    }
    if (!quantized_row_bytes(tensor, columns, &row_bytes) ||
        row > tensor->byte_count / row_bytes ||
        row * row_bytes > tensor->byte_count - row_bytes) return 0;
    packed = m->file + tensor->file_offset + row * row_bytes;
    if (tensor->type == GGML_TYPE_Q8_0) {
        struct nfllm_q8_0_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args);
        args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed;
        args.packed_bytes = row_bytes;
        args.columns = columns;
        args.output = output;
        return nfllm_dispatch_dequantize_q8_0_row(&args) == NFLLM_OK;
    }
    if (tensor->type == GGML_TYPE_Q4_0) {
        struct nfllm_q4_0_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args);
        args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed;
        args.packed_bytes = row_bytes;
        args.columns = columns;
        args.output = output;
        return nfllm_dispatch_dequantize_q4_0_row(&args) == NFLLM_OK;
    }
    if (tensor->type == GGML_TYPE_Q2_0) {
        struct nfllm_q2_0_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args);
        args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed;
        args.packed_bytes = row_bytes;
        args.columns = columns;
        args.output = output;
        return nfllm_dispatch_dequantize_q2_0_row(&args) == NFLLM_OK;
    }
    if (tensor->type == GGML_TYPE_Q2_K) {
        struct nfllm_q2_k_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args);
        args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed;
        args.packed_bytes = row_bytes;
        args.columns = columns;
        args.output = output;
        return nfllm_dispatch_dequantize_q2_k_row(&args) == NFLLM_OK;
    }
    if (tensor->type == GGML_TYPE_IQ3_XXS) {
        struct nfllm_iq3_xxs_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args);
        args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed;
        args.packed_bytes = row_bytes;
        args.columns = columns;
        args.output = output;
        return nfllm_dispatch_dequantize_iq3_xxs_row(&args) == NFLLM_OK;
    }
    if (tensor->type == GGML_TYPE_IQ3_S) {
        struct nfllm_iq3_s_dequantize_row_args_v1 args;
        args.struct_size = sizeof(args);
        args.abi_version = NFLLM_ABI_VERSION_1;
        args.packed_row = packed;
        args.packed_bytes = row_bytes;
        args.columns = columns;
        args.output = output;
        return nfllm_dispatch_dequantize_iq3_s_row(&args) == NFLLM_OK;
    }
    return 0;
}

static int matvec(const struct Model *m, const struct TensorInfo *tensor,
                  u32 rows, u32 columns, const float *input, float *output)
{
    struct nfllm_matvec_f32_args_v1 args;
    u32 row;
    if (!m || !tensor || !input || !output || tensor->n_dims != 2U ||
        tensor->dims[0] != columns || tensor->dims[1] != rows) return 0;
    args.struct_size = sizeof(args);
    args.abi_version = NFLLM_ABI_VERSION_1;
    args.input = input;
    args.rows = 1U;
    args.columns = columns;
    if (tensor->type == GGML_TYPE_F32) {
        args.weights = tensor_data(m, tensor);
        args.rows = rows;
        args.output = output;
        return nfllm_dispatch_matvec_f32(&args) == NFLLM_OK;
    }
    if (columns > MAX_QUANT_COLUMNS) return 0;
    if (!g_force_quantized_decode && !g_force_quantized_scalar &&
        (tensor->type == GGML_TYPE_Q8_0 || tensor->type == GGML_TYPE_Q4_0)) {
        u32 row_bytes;
        u32 packed_bytes;
        const u8 *packed_matrix;
        struct nfllm_quantized_matvec_matrix_args_v1 matrix_args;
        nfllm_status_v1 matrix_status = NFLLM_E_NOT_FOUND;
        if (quantized_row_bytes(tensor, columns, &row_bytes) &&
            rows <= 0xffffffffUL / row_bytes) {
            packed_bytes = rows * row_bytes;
            if (packed_bytes <= tensor->byte_count) {
                packed_matrix = m->file + tensor->file_offset;
                matrix_args.struct_size = sizeof(matrix_args);
                matrix_args.abi_version = NFLLM_ABI_VERSION_1;
                matrix_args.packed_matrix = packed_matrix;
                matrix_args.packed_bytes = packed_bytes;
                matrix_args.rows = rows;
                matrix_args.columns = columns;
                matrix_args.input = input;
                matrix_args.output = output;
                if (tensor->type == GGML_TYPE_Q8_0)
                    matrix_status = nfllm_dispatch_matvec_q8_0_matrix(&matrix_args);
                else
                    matrix_status = nfllm_dispatch_matvec_q4_0_matrix(&matrix_args);
                if (matrix_status == NFLLM_OK) {
                    if (tensor->type == GGML_TYPE_Q8_0) g_direct_q8_rows += rows;
                    else g_direct_q4_rows += rows;
                    return 1;
                }
                if (matrix_status != NFLLM_E_NOT_FOUND &&
                    matrix_status != NFLLM_E_CAPABILITY) return 0;
            }
        }
    }
    {
        float weight_row[MAX_QUANT_COLUMNS];
        for (row = 0U; row < rows; ++row) {
            u32 row_bytes;
            const u8 *packed;
            nfllm_status_v1 direct_status = NFLLM_E_NOT_FOUND;
            if (!g_force_quantized_decode &&
                (direct_matvec_operation_for_type(tensor->type) != 0U ||
                 tensor->type == GGML_TYPE_Q8_0 ||
                 tensor->type == GGML_TYPE_Q4_0)) {
                if (
                quantized_row_bytes(tensor, columns, &row_bytes) &&
                row <= tensor->byte_count / row_bytes &&
                row * row_bytes <= tensor->byte_count - row_bytes) {
                packed = m->file + tensor->file_offset + row * row_bytes;
                if (tensor->type == GGML_TYPE_Q8_0) {
                    struct nfllm_q8_0_matvec_row_args_v1 direct_args;
                    direct_args.struct_size = sizeof(direct_args);
                    direct_args.abi_version = NFLLM_ABI_VERSION_1;
                    direct_args.packed_row = packed;
                    direct_args.packed_bytes = row_bytes;
                    direct_args.columns = columns;
                    direct_args.input = input;
                    direct_args.output = output + row;
                    direct_status = g_force_quantized_scalar ?
                        nfllm_dispatch_matvec_q8_0_row_scalar(&direct_args) :
                        nfllm_dispatch_matvec_q8_0_row(&direct_args);
                } else if (tensor->type == GGML_TYPE_Q4_0) {
                    struct nfllm_q4_0_matvec_row_args_v1 direct_args;
                    direct_args.struct_size = sizeof(direct_args);
                    direct_args.abi_version = NFLLM_ABI_VERSION_1;
                    direct_args.packed_row = packed;
                    direct_args.packed_bytes = row_bytes;
                    direct_args.columns = columns;
                    direct_args.input = input;
                    direct_args.output = output + row;
                    direct_status = g_force_quantized_scalar ?
                        nfllm_dispatch_matvec_q4_0_row_scalar(&direct_args) :
                        nfllm_dispatch_matvec_q4_0_row(&direct_args);
                } else {
                    struct nfllm_packed_matvec_row_args_v1 direct_args;
                    direct_args.struct_size = sizeof(direct_args);
                    direct_args.abi_version = NFLLM_ABI_VERSION_1;
                    direct_args.packed_row = packed;
                    direct_args.packed_bytes = row_bytes;
                    direct_args.columns = columns;
                    direct_args.input = input;
                    direct_args.output = output + row;
                    direct_status = nfllm_dispatch_matvec_packed_row(
                        direct_matvec_operation_for_type(tensor->type),
                        &direct_args);
                }
                if (direct_status == NFLLM_OK) {
                    if (tensor->type == GGML_TYPE_Q8_0) ++g_direct_q8_rows;
                    else if (tensor->type == GGML_TYPE_Q4_0) ++g_direct_q4_rows;
                    else if (tensor->type == GGML_TYPE_Q2_0) ++g_direct_q20_rows;
                    else if (tensor->type == GGML_TYPE_Q2_K) ++g_direct_q2k_rows;
                    else if (tensor->type == GGML_TYPE_IQ3_XXS) ++g_direct_iq3xxs_rows;
                    else if (tensor->type == GGML_TYPE_IQ3_S) ++g_direct_iq3s_rows;
                    continue;
                }
                if (direct_status != NFLLM_E_NOT_FOUND) return 0;
                }
            }
            if (!read_tensor_row_f32(m, tensor, row, weight_row)) return 0;
            args.weights = weight_row;
            args.output = output + row;
            if (nfllm_dispatch_matvec_f32(&args) != NFLLM_OK) return 0;
        }
    }
    return 1;
}

static int rms_norm(const float *input, const float *weight,
                    u32 count, float epsilon, float *output)
{
    struct nfllm_rms_norm_f32_args_v1 args;
    args.struct_size = sizeof(args);
    args.abi_version = NFLLM_ABI_VERSION_1;
    args.input = input;
    args.weight = weight;
    args.count = count;
    args.epsilon = epsilon;
    args.output = output;
    return nfllm_dispatch_rms_norm_f32(&args) == NFLLM_OK;
}

static int rotate_rope(float *vector, u32 heads, u32 head_dim,
                       u32 rope_dim, u32 position, float base)
{
    struct nfllm_rope_f32_args_v1 args;
    args.struct_size = sizeof(args);
    args.abi_version = NFLLM_ABI_VERSION_1;
    args.vector = vector;
    args.heads = heads;
    args.head_dim = head_dim;
    args.rope_dim = rope_dim;
    args.position = position;
    args.base = base;
    return nfllm_dispatch_rope_f32(&args) == NFLLM_OK;
}

static int validate_model(struct Model *m)
{
    u32 i;
    if (strcmp(m->architecture, "llama") != 0 ||
        strcmp(m->tokenizer_model, "llama") != 0 || !m->have_tokens ||
        !m->have_scores || !m->have_token_types || m->n_vocab == 0 ||
        m->score_count != m->n_vocab || m->token_type_count != m->n_vocab ||
        m->n_vocab > MAX_VOCAB || m->embedding_length == 0 ||
        m->layer_count == 0 || m->layer_count > MAX_LAYERS ||
        m->head_count == 0 || m->head_count_kv == 0 ||
        m->head_count % m->head_count_kv != 0 ||
        m->embedding_length % m->head_count != 0 ||
        m->feed_forward_length == 0 || m->feed_forward_length > 1024 ||
        m->rope_dimensions > m->embedding_length / m->head_count ||
        (m->rope_dimensions & 1) != 0 || m->bos_id >= m->n_vocab ||
        m->unknown_token_id >= m->n_vocab ||
        (m->eos_id != 0xffffffffUL && m->eos_id >= m->n_vocab) ||
        m->context_length == 0 || m->context_length > MAX_CONTEXT) return 0;

    if (!check_matrix_shape(find_tensor(m, "token_embd.weight"),
                            m->embedding_length, m->n_vocab) ||
        !check_f32_shape(find_tensor(m, "output_norm.weight"), 1,
                         m->embedding_length, 0) ||
        !check_matrix_shape(find_tensor(m, "output.weight"),
                            m->embedding_length, m->n_vocab)) return 0;

    for (i = 0; i < m->layer_count; ++i) {
        char name[64];
        u32 kv_length = (m->embedding_length / m->head_count) * m->head_count_kv;
        sprintf(name, "blk.%lu.attn_q.weight", (unsigned long)i);
        if (!check_matrix_shape(find_tensor(m, name), m->embedding_length, m->embedding_length)) return 0;
        sprintf(name, "blk.%lu.attn_k.weight", (unsigned long)i);
        if (!check_matrix_shape(find_tensor(m, name), m->embedding_length, kv_length)) return 0;
        sprintf(name, "blk.%lu.attn_v.weight", (unsigned long)i);
        if (!check_matrix_shape(find_tensor(m, name), m->embedding_length, kv_length)) return 0;
        sprintf(name, "blk.%lu.attn_output.weight", (unsigned long)i);
        if (!check_matrix_shape(find_tensor(m, name), m->embedding_length, m->embedding_length)) return 0;
        sprintf(name, "blk.%lu.attn_norm.weight", (unsigned long)i);
        if (!check_f32_shape(find_tensor(m, name), 1, m->embedding_length, 0)) return 0;
        sprintf(name, "blk.%lu.ffn_norm.weight", (unsigned long)i);
        if (!check_f32_shape(find_tensor(m, name), 1, m->embedding_length, 0)) return 0;
        sprintf(name, "blk.%lu.ffn_gate.weight", (unsigned long)i);
        if (!check_matrix_shape(find_tensor(m, name), m->embedding_length, m->feed_forward_length)) return 0;
        sprintf(name, "blk.%lu.ffn_up.weight", (unsigned long)i);
        if (!check_matrix_shape(find_tensor(m, name), m->embedding_length, m->feed_forward_length)) return 0;
        sprintf(name, "blk.%lu.ffn_down.weight", (unsigned long)i);
        if (!check_matrix_shape(find_tensor(m, name), m->feed_forward_length, m->embedding_length)) return 0;
    }
    for (i = 0U; i < m->n_tensors; ++i) {
        const struct TensorInfo *t = &m->tensors[i];
        if (t->type != GGML_TYPE_F32) {
            u32 block_elements, block_bytes;
            if (t->n_dims != 2U ||
                !quantized_type_info(t->type, &block_elements, &block_bytes) ||
                (t->dims[0] % block_elements) != 0U) return 0;
        }
    }
    return 1;
}

static int token_type(const struct Model *m, u32 id)
{
    return m->have_token_types ? m->token_types[id] : 1;
}

static int token_match(const struct Model *m, u32 id,
                       const u8 *text, u32 text_length, u32 position,
                       u32 *matched)
{
    const u8 *token = m->tokens[id];
    u32 length = m->token_lengths[id];
    if (id == m->bos_id || id == m->eos_id || token_type(m, id) == 3 ||
        token_type(m, id) == 5 || token_type(m, id) == 2) return 0;

    if (token_type(m, id) == 6 && length == 6 && token[0] == '<' &&
        token[1] == '0' && (token[2] == 'x' || token[2] == 'X') &&
        token[5] == '>') {
        int hi, lo;
        u8 byte;
        hi = (token[3] >= '0' && token[3] <= '9') ? token[3] - '0' :
             (token[3] >= 'a' && token[3] <= 'f') ? token[3] - 'a' + 10 :
             (token[3] >= 'A' && token[3] <= 'F') ? token[3] - 'A' + 10 : -1;
        lo = (token[4] >= '0' && token[4] <= '9') ? token[4] - '0' :
             (token[4] >= 'a' && token[4] <= 'f') ? token[4] - 'a' + 10 :
             (token[4] >= 'A' && token[4] <= 'F') ? token[4] - 'A' + 10 : -1;
        if (hi < 0 || lo < 0 || position >= text_length) return 0;
        byte = (u8)((hi << 4) | lo);
        if (text[position] != byte) return 0;
        *matched = 1;
        return 1;
    }

    if (length == 0 || length > text_length - position) return 0;
    if (memcmp(token, text + position, length) != 0) return 0;
    *matched = length;
    return 1;
}

static int tokenize_prompt(const struct Model *m, const char *prompt,
                           u32 *ids, u32 *id_count)
{
    /* The DOS/4GW default stack is deliberately not relied on for large work buffers. */
    static u8 normalized[MAX_PROMPT_BYTES];
    static float best[MAX_PROMPT_BYTES + 1];
    static short previous_position[MAX_PROMPT_BYTES + 1];
    static short previous_token[MAX_PROMPT_BYTES + 1];
    u32 normalized_length = 0;
    u32 i, j, back_count = 0;
    static u32 reverse_ids[MAX_CONTEXT];
    int add_bos = m->add_bos;

    if (prompt == NULL) prompt = "";
    if (*prompt == '\0') {
        if (!add_bos) add_bos = 1;
        ids[0] = m->bos_id;
        *id_count = 1;
        return 1;
    }

    /* SentencePiece's visible U+2581 marker is UTF-8 E2 96 81. */
    normalized[normalized_length++] = 0xe2;
    normalized[normalized_length++] = 0x96;
    normalized[normalized_length++] = 0x81;
    for (i = 0; prompt[i] != '\0'; ++i) {
        if (prompt[i] == ' ' || prompt[i] == '\t') {
            if (normalized_length + 3 > MAX_PROMPT_BYTES) return 0;
            normalized[normalized_length++] = 0xe2;
            normalized[normalized_length++] = 0x96;
            normalized[normalized_length++] = 0x81;
        } else {
            if (normalized_length + 1 > MAX_PROMPT_BYTES) return 0;
            normalized[normalized_length++] = (u8)prompt[i];
        }
    }

    for (i = 0; i <= normalized_length; ++i) {
        best[i] = -1.0e30f;
        previous_position[i] = -1;
        previous_token[i] = -1;
    }
    best[0] = 0.0f;
    for (i = 0; i < normalized_length; ++i) {
        if (best[i] <= -1.0e29f) continue;
        for (j = 0; j < m->n_vocab; ++j) {
            u32 matched = 0;
            u32 destination;
            float candidate;
            if (!token_match(m, j, normalized, normalized_length, i, &matched)) continue;
            destination = i + matched;
            candidate = best[i] + m->token_scores[j];
            if (candidate > best[destination]) {
                best[destination] = candidate;
                previous_position[destination] = (short)i;
                previous_token[destination] = (short)j;
            }
        }
        /* Unknown-token fallback guarantees forward progress for unseen bytes. */
        {
            float fallback_score = best[i] - 100.0f;
            if (previous_position[i + 1] < 0 || fallback_score > best[i + 1]) {
                best[i + 1] = fallback_score;
                previous_position[i + 1] = (short)i;
                previous_token[i + 1] = (short)m->unknown_token_id;
            }
        }
    }

    i = normalized_length;
    while (i > 0) {
        if (previous_position[i] < 0 || previous_token[i] < 0 || back_count >= MAX_CONTEXT - 1)
            return 0;
        reverse_ids[back_count++] = (u32)previous_token[i];
        i = (u32)previous_position[i];
    }
    *id_count = 0;
    if (add_bos) ids[(*id_count)++] = m->bos_id;
    while (back_count > 0) ids[(*id_count)++] = reverse_ids[--back_count];
    return *id_count > 0 && *id_count <= m->context_length;
}

static void print_token(const struct Model *m, u32 id)
{
    const u8 *text;
    u32 length, i;
    if (id >= m->n_vocab) { printf("<invalid>"); return; }
    text = m->tokens[id];
    length = m->token_lengths[id];
    for (i = 0; i < length; ++i) {
        if (i + 2 < length && text[i] == 0xe2 && text[i + 1] == 0x96 && text[i + 2] == 0x81) {
            putchar(' '); i += 2;
        } else if (text[i] >= 0x20 && text[i] < 0x7f) {
            putchar(text[i]);
        } else {
            putchar('?');
        }
    }
}

static u32 choose_greedy(const float *logits, u32 count)
{
    u32 best = 0, i;
    float value = logits[0];
    for (i = 1; i < count; ++i) {
        if (logits[i] > value) { value = logits[i]; best = i; }
    }
    return best;
}

static int forward_one(const struct Model *m, u32 token, u32 position,
                       float *keys, float *values, float *attention_scores,
                       float *logits)
{
    u32 d = m->embedding_length;
    u32 nhead = m->head_count;
    u32 nkv = m->head_count_kv;
    u32 head_dim = d / nhead;
    u32 kv_dim = head_dim * nkv;
    u32 nctx = m->context_length;
    u32 layer, i, h, t;
    static float x[256], norm[256], q[256], k[256], v[256];
    static float context[256], projected[256], gate[1024], up[1024], down[256];
    struct TensorInfo *tensor;
    u32 group_size = nhead / nkv;

    if (token >= m->n_vocab || position >= nctx || d > 256 || kv_dim > 256 ||
        m->feed_forward_length > 1024) return 0;
    tensor = find_tensor((struct Model *)m, "token_embd.weight");
    if (!read_tensor_row_f32(m, tensor, token, x)) return 0;

    for (layer = 0; layer < m->layer_count; ++layer) {
        char name[64];
        struct TensorInfo *wq, *wk, *wv, *wo, *wgate, *wup, *wdown;
        const float *attn_norm, *ffn_norm;
        u32 cache_offset = layer * nctx * kv_dim;

        sprintf(name, "blk.%lu.attn_norm.weight", (unsigned long)layer);
        attn_norm = tensor_data(m, find_tensor((struct Model *)m, name));
        if (!rms_norm(x, attn_norm, d, m->rms_epsilon, norm)) return 0;

        sprintf(name, "blk.%lu.attn_q.weight", (unsigned long)layer);
        wq = find_tensor((struct Model *)m, name);
        sprintf(name, "blk.%lu.attn_k.weight", (unsigned long)layer);
        wk = find_tensor((struct Model *)m, name);
        sprintf(name, "blk.%lu.attn_v.weight", (unsigned long)layer);
        wv = find_tensor((struct Model *)m, name);
        if (!matvec(m, wq, d, d, norm, q) ||
            !matvec(m, wk, kv_dim, d, norm, k) ||
            !matvec(m, wv, kv_dim, d, norm, v)) return 0;
        if (!rotate_rope(q, nhead, head_dim, m->rope_dimensions,
                         position, m->rope_base) ||
            !rotate_rope(k, nkv, head_dim, m->rope_dimensions,
                         position, m->rope_base)) return 0;

        memcpy(keys + cache_offset + position * kv_dim, k, kv_dim * sizeof(float));
        memcpy(values + cache_offset + position * kv_dim, v, kv_dim * sizeof(float));
        memset(context, 0, d * sizeof(float));

        for (h = 0; h < nhead; ++h) {
            u32 kv_head = h / group_size;
            const float *query = q + h * head_dim;
            float maximum = -1.0e30f;
            double total = 0.0;
            for (t = 0; t <= position; ++t) {
                const float *key = keys + cache_offset + t * kv_dim + kv_head * head_dim;
                float score = 0.0f;
                for (i = 0; i < head_dim; ++i) score += query[i] * key[i];
                score *= (float)(1.0 / sqrt((double)head_dim));
                attention_scores[t] = score;
                if (score > maximum) maximum = score;
            }
            for (t = 0; t <= position; ++t) {
                attention_scores[t] = (float)exp((double)(attention_scores[t] - maximum));
                total += attention_scores[t];
            }
            if (total == 0.0) return 0;
            for (t = 0; t <= position; ++t) {
                const float *value = values + cache_offset + t * kv_dim + kv_head * head_dim;
                float probability = (float)(attention_scores[t] / total);
                for (i = 0; i < head_dim; ++i)
                    context[h * head_dim + i] += probability * value[i];
            }
        }

        sprintf(name, "blk.%lu.attn_output.weight", (unsigned long)layer);
        wo = find_tensor((struct Model *)m, name);
        if (!matvec(m, wo, d, d, context, projected)) return 0;
        for (i = 0; i < d; ++i) x[i] += projected[i];

        sprintf(name, "blk.%lu.ffn_norm.weight", (unsigned long)layer);
        ffn_norm = tensor_data(m, find_tensor((struct Model *)m, name));
        if (!rms_norm(x, ffn_norm, d, m->rms_epsilon, norm)) return 0;
        sprintf(name, "blk.%lu.ffn_gate.weight", (unsigned long)layer);
        wgate = find_tensor((struct Model *)m, name);
        sprintf(name, "blk.%lu.ffn_up.weight", (unsigned long)layer);
        wup = find_tensor((struct Model *)m, name);
        sprintf(name, "blk.%lu.ffn_down.weight", (unsigned long)layer);
        wdown = find_tensor((struct Model *)m, name);
        if (!matvec(m, wgate, m->feed_forward_length, d, norm, gate) ||
            !matvec(m, wup, m->feed_forward_length, d, norm, up)) return 0;
        for (i = 0; i < m->feed_forward_length; ++i) {
            float sigmoid = (float)(1.0 / (1.0 + exp(-(double)gate[i])));
            gate[i] = gate[i] * sigmoid * up[i];
        }
        if (!matvec(m, wdown, d, m->feed_forward_length, gate, down)) return 0;
        for (i = 0; i < d; ++i) x[i] += down[i];
    }

    tensor = find_tensor((struct Model *)m, "output_norm.weight");
    if (!rms_norm(x, tensor_data(m, tensor), d, m->rms_epsilon, norm)) return 0;
    tensor = find_tensor((struct Model *)m, "output.weight");
    if (!matvec(m, tensor, m->n_vocab, d, norm, logits)) return 0;
    return 1;
}

static int load_model(const char *path, struct Model *m)
{
    FILE *file;
    long length;
    memset(m, 0, sizeof(*m));
    file = fopen(path, "rb");
    if (!file) { printf("ERROR: cannot open model %s\n", path); return 0; }
    if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 24L ||
        length > 0x7fffffffL || fseek(file, 0L, SEEK_SET) != 0) {
        printf("ERROR: invalid model file length\n"); fclose(file); return 0;
    }
    m->file_size = (u32)length;
    m->file = (const u8 *)malloc(m->file_size);
    if (!m->file) { printf("ERROR: not enough memory for GGUF (%lu bytes)\n", m->file_size); fclose(file); return 0; }
    if (fread((void *)m->file, 1, m->file_size, file) != m->file_size) {
        printf("ERROR: short read while loading model\n"); fclose(file); free((void *)m->file); m->file = NULL; return 0;
    }
    fclose(file);
    if (!parse_gguf(m)) {
        printf("ERROR: invalid or unsupported GGUF structure\n");
        free((void *)m->file); m->file = NULL; return 0;
    }
    if (!validate_model(m)) {
        printf("ERROR: model is outside the NFLLM LLaMA F32 profile\n");
        free((void *)m->file); m->file = NULL; return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    static struct Model model;
    static u32 prompt_ids[MAX_CONTEXT];
    struct nfllm_host_api_v1 host_api;
    u32 prompt_count = 0, max_new = 1, generated = 0, position, token;
    u32 requested_plugin_id = NFLLM_PLUGIN_ID_NONE;
    u32 requested_cpu_capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
    const char *requested_isa_profile = "386+x87";
    nfllm_status_v1 dispatch_status;
    u32 head_dim, kv_dim, cache_elements, cache_bytes, layer_cache;
    u32 auxiliary_bytes, heap_payload_bytes;
    float *keys, *values, *attention_scores, *logits;
    clock_t prompt_started, prompt_finished, generation_started, generation_finished;
    const char *prompt = "";
    char dispatch_diagnostic[NFLLM_DIAGNOSTIC_CAPACITY];
    unsigned long i;

    printf("NFLLM 0.1 - NF-DOS / Am486DX4\n");
    printf("F32 LLaMA GGUF; 386+x87 baseline; optional manually declared ISA kernels; single-threaded.\n");
    if (argc < 2) {
        printf("Usage: NFLLM.EXE model.gguf [-p prompt] [-n new_tokens] [--isa 486|geode-gx1|geode-lx|atom-n270] [--backend auto|reference|mmx|3dnow|sse2] [--quantized-matvec direct|scalar|decode]\n");
        return 1;
    }
    for (i = 2; i < (unsigned long)argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < (unsigned long)argc) prompt = argv[++i];
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < (unsigned long)argc) {
            max_new = (u32)atoi(argv[++i]);
            if (max_new == 0) max_new = 1;
        } else if ((strcmp(argv[i], "--backend") == 0 ||
                    strcmp(argv[i], "-b") == 0) &&
                   i + 1 < (unsigned long)argc) {
            ++i;
            if (strcmp(argv[i], "auto") == 0)
                requested_plugin_id = NFLLM_PLUGIN_ID_NONE;
            else if (strcmp(argv[i], "reference") == 0)
                requested_plugin_id = NFLLM_PLUGIN_ID_REFERENCE;
            else if (strcmp(argv[i], "mmx") == 0)
                requested_plugin_id = NFLLM_PLUGIN_ID_MMX;
            else if (strcmp(argv[i], "3dnow") == 0)
                requested_plugin_id = NFLLM_PLUGIN_ID_3DNOW;
            else if (strcmp(argv[i], "sse2") == 0)
                requested_plugin_id = NFLLM_PLUGIN_ID_SSE2;
            else {
                printf("ERROR: unknown backend %s (use auto, reference, mmx, 3dnow, or sse2)\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--quantized-matvec") == 0 &&
                   i + 1 < (unsigned long)argc) {
            ++i;
            if (strcmp(argv[i], "direct") == 0) {
                g_force_quantized_decode = 0;
                g_force_quantized_scalar = 0;
            } else if (strcmp(argv[i], "scalar") == 0) {
                g_force_quantized_decode = 0;
                g_force_quantized_scalar = 1;
            } else if (strcmp(argv[i], "decode") == 0) {
                g_force_quantized_decode = 1;
                g_force_quantized_scalar = 0;
            }
            else {
                printf("ERROR: unknown quantized matvec path %s (use direct, scalar, or decode)\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--isa") == 0 &&
                   i + 1 < (unsigned long)argc) {
            const char *profile = argv[++i];
            if (strcmp(profile, "486") == 0) {
                requested_cpu_capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87;
                requested_isa_profile = "386+x87";
            } else if (strcmp(profile, "geode-gx1") == 0) {
                requested_cpu_capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87 |
                                             NFLLM_CAP_MMX;
                requested_isa_profile = "Geode GX1 (manual declaration)";
            } else if (strcmp(profile, "geode-lx") == 0) {
                requested_cpu_capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87 |
                    NFLLM_CAP_MMX | NFLLM_CAP_AMD_MMX_EXT |
                    NFLLM_CAP_3DNOW | NFLLM_CAP_3DNOW_EXT;
                requested_isa_profile = "Geode LX (manual declaration)";
            } else if (strcmp(profile, "atom-n270") == 0) {
                u32 cr4 = read_cr4();
                requested_cpu_capabilities = NFLLM_CAP_386 | NFLLM_CAP_X87 |
                                             NFLLM_CAP_MMX;
                requested_isa_profile = "Atom N270 (manual; OSFXSR checked)";
                if ((cr4 & (1UL << 9)) != 0U) {
                    requested_cpu_capabilities |= NFLLM_CAP_SSE | NFLLM_CAP_SSE2 |
                                                  NFLLM_CAP_SSE3 | NFLLM_CAP_SSSE3;
                } else {
                    printf("ISA profile: CR4.OSFXSR=0; SSE/SSE2/SSE3/SSSE3 kernels disabled.\n");
                }
            } else {
                printf("ERROR: unknown ISA profile %s\n", profile);
                return 1;
            }
        } else {
            printf("ERROR: unknown option %s\n", argv[i]);
            return 1;
        }
    }

    printf("Quantized matvec path: %s\n",
           g_force_quantized_decode ? "decode+F32" :
           (g_force_quantized_scalar ? "direct scalar row kernel" :
                                       "direct packed when available"));

    if (!load_model(argv[1], &model)) return 2;
    printf("GGUF v%lu: arch=%s tokenizer=%s; tensor formats F32/Q8_0/Q4_0/Q2_0/Q2_K/IQ3_XXS/IQ3_S\n", model.version,
           model.architecture, model.tokenizer_model);
    {
        u32 f32_tensors = 0U, q8_tensors = 0U, q4_tensors = 0U;
        u32 q20_tensors = 0U, q2k_tensors = 0U, iq3xxs_tensors = 0U, iq3s_tensors = 0U;
        for (i = 0U; i < model.n_tensors; ++i) {
            if (model.tensors[i].type == GGML_TYPE_F32) ++f32_tensors;
            else if (model.tensors[i].type == GGML_TYPE_Q8_0) ++q8_tensors;
            else if (model.tensors[i].type == GGML_TYPE_Q4_0) ++q4_tensors;
            else if (model.tensors[i].type == GGML_TYPE_Q2_0) ++q20_tensors;
            else if (model.tensors[i].type == GGML_TYPE_Q2_K) ++q2k_tensors;
            else if (model.tensors[i].type == GGML_TYPE_IQ3_XXS) ++iq3xxs_tensors;
            else if (model.tensors[i].type == GGML_TYPE_IQ3_S) ++iq3s_tensors;
        }
        printf("Tensor types: F32=%lu Q8_0=%lu Q4_0=%lu Q2_0=%lu Q2_K=%lu IQ3_XXS=%lu IQ3_S=%lu\n",
               f32_tensors, q8_tensors, q4_tensors, q20_tensors, q2k_tensors,
               iq3xxs_tensors, iq3s_tensors);
    }
    printf("Model: vocab=%lu dim=%lu layers=%lu heads=%lu kv_heads=%lu ffn=%lu ctx=%lu\n",
           model.n_vocab, model.embedding_length, model.layer_count,
           model.head_count, model.head_count_kv, model.feed_forward_length,
           model.context_length);
    printf("GGUF bytes=%lu tensors=%lu alignment=%lu\n",
           model.file_size, model.n_tensors, model.alignment);

    if (!tokenize_prompt(&model, prompt, prompt_ids, &prompt_count)) {
        printf("ERROR: prompt could not be tokenized or exceeds context\n");
        free((void *)model.file); return 3;
    }
    if (max_new > model.context_length - prompt_count) max_new = model.context_length - prompt_count;
    if (max_new == 0) { printf("ERROR: no context left for generation\n"); free((void *)model.file); return 3; }

    head_dim = model.embedding_length / model.head_count;
    kv_dim = head_dim * model.head_count_kv;
    if (model.layer_count > 0xffffffffUL / model.context_length / kv_dim) {
        printf("ERROR: KV cache size overflow\n"); free((void *)model.file); return 4;
    }
    cache_elements = model.layer_count * model.context_length * kv_dim;
    if (cache_elements > 0xffffffffUL / sizeof(float)) {
        printf("ERROR: KV cache too large\n"); free((void *)model.file); return 4;
    }
    cache_bytes = cache_elements * sizeof(float);
    keys = (float *)malloc(cache_bytes);
    values = (float *)malloc(cache_bytes);
    attention_scores = (float *)malloc(model.context_length * sizeof(float));
    logits = (float *)malloc(model.n_vocab * sizeof(float));
    if (!keys || !values || !attention_scores || !logits) {
        printf("ERROR: not enough extender memory for KV cache\n");
        free(keys); free(values); free(attention_scores); free(logits); free((void *)model.file);
        return 4;
    }
    memset(keys, 0, cache_bytes); memset(values, 0, cache_bytes);
    layer_cache = model.context_length * kv_dim;
    auxiliary_bytes = model.context_length * sizeof(float) + model.n_vocab * sizeof(float);
    heap_payload_bytes = model.file_size + 2UL * cache_bytes + auxiliary_bytes;
    printf("Prompt tokens=%lu; KV cache=%lu bytes; DOS/4GW memory budget checked by allocation.\n",
           prompt_count, 2UL * cache_bytes);
    printf("Heap payload: model=%lu KV=%lu auxiliary=%lu total=%lu bytes (allocator/runtime overhead excluded).\n",
           model.file_size, 2UL * cache_bytes, auxiliary_bytes, heap_payload_bytes);

    memset(&host_api, 0, sizeof(host_api));
    host_api.struct_size = sizeof(host_api);
    host_api.abi_version = NFLLM_ABI_VERSION_1;
    /* The 386 baseline and x87 requirement are an explicit target profile.
       Runtime CPU enumeration is deliberately disabled; the compatibility
       probe reports EFLAGS.ID behavior separately. */
    host_api.capabilities = requested_cpu_capabilities;
    dispatch_status = nfllm_registry_initialize(&host_api);
    if (dispatch_status == NFLLM_OK)
        dispatch_status = nfllm_register_builtin_plugins();
    if (dispatch_status == NFLLM_OK) {
        for (i = 0U; i < model.n_tensors; ++i) {
            u32 operation_id = dequantize_operation_for_type(model.tensors[i].type);
            if (operation_id != 0U) {
                const struct nfllm_kernel_descriptor_v1 *decoder = 0;
                u32 decoder_plugin_id = NFLLM_PLUGIN_ID_NONE;
                dispatch_status = nfllm_registry_resolve(
                    operation_id, NFLLM_PLUGIN_ID_NONE, host_api.capabilities,
                    &decoder, &decoder_plugin_id, dispatch_diagnostic,
                    sizeof(dispatch_diagnostic));
                if (dispatch_status != NFLLM_OK) {
                    printf("ERROR: no linked row decoder for GGML type %lu\n",
                           model.tensors[i].type);
                    break;
                }
            }
        }
    }
    if (dispatch_status == NFLLM_OK)
        dispatch_status = nfllm_dispatcher_configure(
            requested_plugin_id, host_api.capabilities, dispatch_diagnostic,
            sizeof(dispatch_diagnostic));
    if (dispatch_status != NFLLM_OK) {
        printf("ERROR: dispatcher initialization failed (%ld)\n",
               (long)dispatch_status);
        nfllm_registry_shutdown();
        free(keys); free(values); free(attention_scores); free(logits);
        free((void *)model.file);
        return 6;
    }
    printf("Dispatcher: %s; declared CPU profile=%s (manual ISA profile; no automatic CPUID).\n",
           dispatch_diagnostic, requested_isa_profile);

    prompt_started = clock();
    for (position = 0; position < prompt_count; ++position) {
        if (!forward_one(&model, prompt_ids[position], position, keys, values,
                         attention_scores, logits)) {
            printf("ERROR: forward pass failed at prompt position %lu\n", position);
            nfllm_registry_shutdown();
            free(keys); free(values); free(attention_scores); free(logits); free((void *)model.file);
            return 5;
        }
    }

    prompt_finished = clock();
    printf("Prompt evaluation tokens=%lu ticks=%lu clock_rate=%lu\n",
           prompt_count, (unsigned long)(prompt_finished - prompt_started),
           (unsigned long)CLOCKS_PER_SEC);

    printf("Generated tokens: ");
    generation_started = clock();
    for (generated = 0; generated < max_new; ++generated) {
        token = choose_greedy(logits, model.n_vocab);
        printf("\nTOKEN[%lu] id=%lu text=\"", generated, token);
        print_token(&model, token);
        printf("\"");
        if (token == model.eos_id) { ++generated; break; }
        if (generated + 1 < max_new) {
            ++position;
            if (!forward_one(&model, token, position, keys, values,
                             attention_scores, logits)) {
                printf("\nERROR: forward pass failed during generation\n");
                nfllm_registry_shutdown();
                free(keys); free(values); free(attention_scores); free(logits); free((void *)model.file);
                return 5;
            }
        }
    }
    generation_finished = clock();
    printf("\nGeneration tokens=%lu ticks=%lu clock_rate=%lu\n",
           generated, (unsigned long)(generation_finished - generation_started),
           (unsigned long)CLOCKS_PER_SEC);
    printf("Direct packed matvec rows: Q8_0=%lu Q4_0=%lu Q2_0=%lu Q2_K=%lu IQ3_XXS=%lu IQ3_S=%lu\n",
           (unsigned long)g_direct_q8_rows, (unsigned long)g_direct_q4_rows,
           (unsigned long)g_direct_q20_rows, (unsigned long)g_direct_q2k_rows,
           (unsigned long)g_direct_iq3xxs_rows, (unsigned long)g_direct_iq3s_rows);
    printf("Status: PASS - logits and token IDs were computed from GGUF weights.\n");

    nfllm_registry_shutdown();
    free(keys); free(values); free(attention_scores); free(logits); free((void *)model.file);
    (void)layer_cache;
    return 0;
}
