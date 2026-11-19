#ifndef NFLLM_PLUGIN_SDK_H
#define NFLLM_PLUGIN_SDK_H

/* Public, C-only SDK for independently built CauseWay DLL plugins.
 * Keep every public structure size/versioned and pass no compiler-owned
 * containers, exceptions, or C++ objects across this boundary.
 */
#include "nfllm_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFLLM_PLUGIN_ABI_V1 1UL
#define NFLLM_PLUGIN_QUERY_V1 "_NFLLM_PluginQueryV1"
#define NFLLM_PLUGIN_ID_TEXT_MAX 32U
#define NFLLM_MODEL_TENSOR_NAME_MAX 64U
#define NFLLM_MODEL_MAX_DIMS 4U

enum nfllm_extension_kind_v1 {
    NFLLM_EXT_OPERATOR = 1,
    NFLLM_EXT_QUANTIZATION = 2,
    NFLLM_EXT_MODEL_FORMAT = 3,
    NFLLM_EXT_NEURAL_ARCHITECTURE = 4,
    NFLLM_EXT_MATH_BACKEND = 5,
    NFLLM_EXT_HARDWARE_OPTIMIZATION = 6
};

/* Lower priority numbers are preferred. CPU capabilities are only a safety
 * filter; they never imply that a newer ISA is faster. Equal priorities are
 * stable by plugin id and extension id.
 */
struct nfllm_plugin_host_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t cpu_capabilities;
    void *context;
    nfllm_log_fn_v1 log;
    nfllm_status_v1 (NFLLM_CALL *resolve_extension)(
        void *context, uint32_t kind, uint32_t extension_id,
        uint32_t required_capabilities, const void **interface_descriptor);
};

struct nfllm_extension_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t kind;
    uint32_t extension_id;
    uint32_t required_capabilities;
    int32_t priority;
    const char *name;
    const void *interface_descriptor;
};

typedef nfllm_status_v1 (NFLLM_CALL *nfllm_plugin_initialize_v1)(
    const struct nfllm_plugin_host_v1 *host, void **plugin_state);
typedef void (NFLLM_CALL *nfllm_plugin_shutdown_v1)(void *plugin_state);

struct nfllm_plugin_bundle_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t plugin_id;
    uint32_t required_capabilities;
    const char *name;
    const char *version;
    nfllm_plugin_initialize_v1 initialize;
    nfllm_plugin_shutdown_v1 shutdown;
    const struct nfllm_kernel_descriptor_v1 *kernels;
    uint32_t kernel_count;
    const struct nfllm_extension_v1 *extensions;
    uint32_t extension_count;
};

typedef nfllm_status_v1 (NFLLM_CALL *nfllm_plugin_query_v1_fn)(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_plugin_bundle_v1 **bundle);

/* A quantization implementation is independent of any model container. The
 * container supplies its stable format id; these callbacks validate and
 * decode packed rows or execute a direct row operation.
 */
struct nfllm_quantization_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t format_id;
    uint32_t bits_per_weight;
    uint32_t block_elements;
    uint32_t block_bytes;
    uint32_t dequantize_operation_id;
    uint32_t matvec_operation_id;
    const char *name;
    nfllm_status_v1 (NFLLM_CALL *validate_row)(
        const uint8_t *packed_row, uint32_t packed_bytes,
        uint32_t columns);
};

/* Model container plugins return a format-neutral view. The returned view,
 * tensors, token table, and backing data remain owned by the format plugin
 * until close() is called. Architecture plugins must not retain them after
 * their own destroy callback.
 */
struct nfllm_model_file_view_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *path;
    const uint8_t *bytes;
    uint32_t byte_count;
};

struct nfllm_tensor_view_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    char name[NFLLM_MODEL_TENSOR_NAME_MAX];
    uint32_t dimensions;
    uint32_t dims[NFLLM_MODEL_MAX_DIMS];
    uint32_t data_type_id;
    const uint8_t *data;
    uint32_t byte_count;
};

struct nfllm_token_view_v1 {
    const uint8_t *bytes;
    uint32_t byte_count;
    float score;
    int32_t type;
};

struct nfllm_model_view_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *format_id;
    const char *architecture_id;
    const char *tokenizer_id;
    uint32_t context_length;
    uint32_t vocabulary_size;
    const struct nfllm_token_view_v1 *tokens;
    const struct nfllm_tensor_view_v1 *tensors;
    uint32_t tensor_count;
    const void *format_private;
};

struct nfllm_model_format_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *format_id;
    int32_t (NFLLM_CALL *probe)(
        const struct nfllm_plugin_host_v1 *host,
        const struct nfllm_model_file_view_v1 *file);
    nfllm_status_v1 (NFLLM_CALL *open)(
        const struct nfllm_plugin_host_v1 *host,
        const struct nfllm_model_file_view_v1 *file,
        struct nfllm_model_view_v1 *model_view, void **format_state);
    void (NFLLM_CALL *close)(void *format_state);
};

struct nfllm_generation_options_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t max_new_tokens;
    uint32_t flags;
};

typedef nfllm_status_v1 (NFLLM_CALL *nfllm_emit_token_v1)(
    void *context, uint32_t token_id, const uint8_t *text,
    uint32_t text_bytes);

struct nfllm_neural_architecture_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *architecture_id;
    int32_t (NFLLM_CALL *supports)(
        const struct nfllm_plugin_host_v1 *host,
        const struct nfllm_model_view_v1 *model_view);
    nfllm_status_v1 (NFLLM_CALL *generate)(
        const struct nfllm_plugin_host_v1 *host,
        const struct nfllm_model_view_v1 *model_view,
        const char *prompt,
        const struct nfllm_generation_options_v1 *options,
        nfllm_emit_token_v1 emit_token, void *emit_context,
        uint32_t *generated_tokens);
};

struct nfllm_math_backend_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *backend_id;
    const char *description;
};

struct nfllm_hardware_optimization_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *hardware_id;
    const char *description;
    uint32_t required_capabilities;
};

/* Use only in a single-plugin DLL translation unit. The build exports the
 * resulting symbol as _NFLLM_PluginQueryV1 with the CauseWay linker.
 */
#if defined(NFLLM_DYNAMIC_PLUGIN_EXPORT)
#define NFLLM_DEFINE_PLUGIN_QUERY_V1(plugin_id_value, required_caps_value, \
        plugin_name_value, plugin_version_value, kernel_array, \
        extension_array, extension_count_value) \
    static const struct nfllm_plugin_bundle_v1 nfllm_dynamic_bundle_v1 = { \
        sizeof(struct nfllm_plugin_bundle_v1), NFLLM_PLUGIN_ABI_V1, \
        (plugin_id_value), (required_caps_value), (plugin_name_value), \
        (plugin_version_value), 0, 0, (kernel_array), \
        (uint32_t)(sizeof(kernel_array) / sizeof((kernel_array)[0])), \
        (extension_array), (extension_count_value) \
    }; \
    nfllm_status_v1 NFLLM_CALL NFLLM_PluginQueryV1( \
        const struct nfllm_plugin_host_v1 *host, \
        const struct nfllm_plugin_bundle_v1 **bundle) \
    { \
        if (!host || !bundle) return NFLLM_E_ARGUMENT; \
        if (host->struct_size < sizeof(*host) || \
            host->abi_version != NFLLM_PLUGIN_ABI_V1) return NFLLM_E_ABI; \
        *bundle = &nfllm_dynamic_bundle_v1; \
        return NFLLM_OK; \
    }
#endif

#ifdef __cplusplus
}
#endif

#endif
