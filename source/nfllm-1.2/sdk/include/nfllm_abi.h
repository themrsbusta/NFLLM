#ifndef NFLLM_ABI_H
#define NFLLM_ABI_H

/* Stable, versioned C boundary shared by the NFLLM host and extension DLLs. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFLLM_ABI_VERSION_1 1UL
#define NFLLM_PLUGIN_ID_REFERENCE 0x52454631UL /* "REF1" */
#define NFLLM_PLUGIN_ID_Q8_0 0x51383030UL /* "Q800" */
#define NFLLM_PLUGIN_ID_Q4_0 0x51343030UL /* "Q400" */
#define NFLLM_PLUGIN_ID_Q2 0x51323030UL /* "Q200" */
#define NFLLM_PLUGIN_ID_IQ3 0x49335133UL /* "I3Q3" */
#define NFLLM_PLUGIN_ID_MMX 0x4d4d5831UL /* "MMX1" */
#define NFLLM_PLUGIN_ID_3DNOW 0x33444e31UL /* "3DN1" */
#define NFLLM_PLUGIN_ID_SSE2 0x53534532UL /* "SSE2" */
#define NFLLM_PLUGIN_ID_NONE 0UL

#define NFLLM_CAP_386 0x00000001UL
#define NFLLM_CAP_X87 0x00000002UL
#define NFLLM_CAP_MMX 0x00000004UL
#define NFLLM_CAP_3DNOW 0x00000008UL
#define NFLLM_CAP_3DNOW_EXT 0x00000010UL
#define NFLLM_CAP_SSE 0x00000020UL
#define NFLLM_CAP_SSE2 0x00000040UL
#define NFLLM_CAP_AMD_MMX_EXT 0x00000080UL
#define NFLLM_CAP_SSE3 0x00000100UL
#define NFLLM_CAP_SSSE3 0x00000200UL
#define NFLLM_CAP_SSE41 0x00000400UL
#define NFLLM_CAP_SSE42 0x00000800UL
#define NFLLM_CAP_SSE4A 0x00001000UL
#define NFLLM_CAP_CPUID 0x00002000UL

#if defined(__WATCOMC__)
#define NFLLM_CALL __cdecl
#else
#define NFLLM_CALL
#endif

#define NFLLM_OP_MATVEC_F32 0x00000101UL
#define NFLLM_OP_RMS_NORM_F32 0x00000102UL
#define NFLLM_OP_ROPE_F32 0x00000103UL
#define NFLLM_OP_DEQUANTIZE_ROW_Q8_0 0x00000201UL
#define NFLLM_OP_DEQUANTIZE_ROW_Q4_0 0x00000202UL
#define NFLLM_OP_DEQUANTIZE_ROW_Q2_0 0x00000203UL
#define NFLLM_OP_DEQUANTIZE_ROW_Q2_K 0x00000204UL
#define NFLLM_OP_DEQUANTIZE_ROW_IQ3_XXS 0x00000205UL
#define NFLLM_OP_DEQUANTIZE_ROW_IQ3_S 0x00000206UL
#define NFLLM_OP_MATVEC_Q8_0_ROW 0x00000301UL
#define NFLLM_OP_MATVEC_Q4_0_ROW 0x00000302UL
#define NFLLM_OP_MATVEC_Q2_0_ROW 0x00000303UL
#define NFLLM_OP_MATVEC_Q2_K_ROW 0x00000304UL
#define NFLLM_OP_MATVEC_IQ3_XXS_ROW 0x00000305UL
#define NFLLM_OP_MATVEC_IQ3_S_ROW 0x00000306UL
#define NFLLM_OP_MATVEC_Q8_0_MATRIX 0x00000307UL
#define NFLLM_OP_MATVEC_Q4_0_MATRIX 0x00000308UL

typedef int32_t nfllm_status_v1;

struct nfllm_host_api_v1;

enum nfllm_status_code_v1 {
    NFLLM_OK = 0,
    NFLLM_E_ARGUMENT = -1,
    NFLLM_E_ABI = -2,
    NFLLM_E_SIZE = -3,
    NFLLM_E_CAPABILITY = -4,
    NFLLM_E_DUPLICATE = -5,
    NFLLM_E_FULL = -6,
    NFLLM_E_NOT_FOUND = -7,
    NFLLM_E_UNSUPPORTED = -8,
    NFLLM_E_OVERFLOW = -9,
    NFLLM_E_DIV_ZERO = -10,
    NFLLM_E_SHIFT_RANGE = -11,
    NFLLM_E_NUMERIC = -12,
    NFLLM_E_STATE = -13,
    NFLLM_E_MEMORY = -14
};

enum nfllm_log_level_v1 {
    NFLLM_LOG_ERROR = 1,
    NFLLM_LOG_WARNING = 2,
    NFLLM_LOG_INFO = 3,
    NFLLM_LOG_DEBUG = 4
};

enum nfllm_value_type_v1 {
    NFLLM_VALUE_NONE = 0,
    NFLLM_VALUE_U32 = 1,
    NFLLM_VALUE_I32 = 2,
    NFLLM_VALUE_F32 = 3
};

typedef void (NFLLM_CALL *nfllm_log_fn_v1)(void *context, int32_t level,
                                           const char *message);
typedef int32_t (NFLLM_CALL *nfllm_plugin_initialize_fn_v1)(
    const struct nfllm_host_api_v1 *host, void **plugin_state);
typedef void (NFLLM_CALL *nfllm_plugin_shutdown_fn_v1)(void *plugin_state);
typedef int32_t (NFLLM_CALL *nfllm_kernel_fn_v1)(const void *arguments);
typedef int32_t (NFLLM_CALL *nfllm_quant_validate_fn_v1)(
    const void *tensor_view, uint32_t tensor_view_size);
typedef int32_t (NFLLM_CALL *nfllm_model_hook_fn_v1)(void *plugin_state,
                                                      const void *model_view,
                                                      uint32_t model_view_size);
typedef int32_t (NFLLM_CALL *nfllm_forward_hook_fn_v1)(void *plugin_state,
                                                        uint32_t token,
                                                        uint32_t position);

struct nfllm_host_api_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t capabilities;
    void *context;
    nfllm_log_fn_v1 log;
};

struct nfllm_plugin_descriptor_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t plugin_id;
    uint32_t required_capabilities;
    uint32_t flags;
    const char *name;
    nfllm_plugin_initialize_fn_v1 initialize;
    nfllm_plugin_shutdown_fn_v1 shutdown;
};

struct nfllm_kernel_descriptor_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t operation_id;
    uint32_t input_type;
    uint32_t output_type;
    uint32_t required_capabilities;
    const char *name;
    nfllm_kernel_fn_v1 execute;
};

struct nfllm_quant_descriptor_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t format_id;
    uint32_t bits_per_weight;
    uint32_t block_elements;
    uint32_t flags;
    const char *name;
    nfllm_quant_validate_fn_v1 validate_tensor;
};

struct nfllm_extension_hooks_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    nfllm_model_hook_fn_v1 model_loaded;
    nfllm_model_hook_fn_v1 model_unloading;
    nfllm_forward_hook_fn_v1 before_forward;
    nfllm_forward_hook_fn_v1 after_forward;
};

struct nfllm_matvec_f32_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const float *weights;
    uint32_t rows;
    uint32_t columns;
    const float *input;
    float *output;
};

/* Format-specific row decoders keep packed storage separate from F32 math. */
struct nfllm_q8_0_dequantize_row_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_row;
    uint32_t packed_bytes;
    uint32_t columns;
    float *output;
};

struct nfllm_q4_0_dequantize_row_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_row;
    uint32_t packed_bytes;
    uint32_t columns;
    float *output;
};

/* Direct packed-row dot products avoid materializing a temporary F32 row. */
struct nfllm_q8_0_matvec_row_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_row;
    uint32_t packed_bytes;
    uint32_t columns;
    const float *input;
    float *output;
};

struct nfllm_q4_0_matvec_row_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_row;
    uint32_t packed_bytes;
    uint32_t columns;
    const float *input;
    float *output;
};

/* Batched quantized products quantize the shared activation vector once. */
struct nfllm_quantized_matvec_matrix_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_matrix;
    uint32_t packed_bytes;
    uint32_t rows;
    uint32_t columns;
    const float *input;
    float *output;
};

struct nfllm_packed_matvec_row_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_row;
    uint32_t packed_bytes;
    uint32_t columns;
    const float *input;
    float *output;
};

struct nfllm_q2_0_dequantize_row_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_row;
    uint32_t packed_bytes;
    uint32_t columns;
    float *output;
};

struct nfllm_q2_k_dequantize_row_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_row;
    uint32_t packed_bytes;
    uint32_t columns;
    float *output;
};

struct nfllm_iq3_xxs_dequantize_row_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_row;
    uint32_t packed_bytes;
    uint32_t columns;
    float *output;
};

struct nfllm_iq3_s_dequantize_row_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const uint8_t *packed_row;
    uint32_t packed_bytes;
    uint32_t columns;
    float *output;
};

struct nfllm_rms_norm_f32_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const float *input;
    const float *weight;
    uint32_t count;
    float epsilon;
    float *output;
};

struct nfllm_rope_f32_args_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    float *vector;
    uint32_t heads;
    uint32_t head_dim;
    uint32_t rope_dim;
    uint32_t position;
    float base;
};

#ifdef __cplusplus
}
#endif

#endif
