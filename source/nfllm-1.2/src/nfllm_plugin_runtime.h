#ifndef NFLLM_PLUGIN_RUNTIME_H
#define NFLLM_PLUGIN_RUNTIME_H

#include "nfllm_plugin_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nfllm_runtime_extension_v1 {
    uint32_t plugin_id;
    uint32_t kind;
    uint32_t extension_id;
    uint32_t required_capabilities;
    int32_t priority;
    const char *plugin_name;
    const char *name;
    const void *interface_descriptor;
};

nfllm_status_v1 NFLLM_CALL nfllm_plugin_runtime_load(
    const char *plugin_directory,
    const struct nfllm_host_api_v1 *registry_host,
    const struct nfllm_plugin_host_v1 *plugin_host,
    uint32_t cpu_capabilities);
void NFLLM_CALL nfllm_plugin_runtime_shutdown(void);
uint32_t NFLLM_CALL nfllm_plugin_runtime_plugin_count(void);
uint32_t NFLLM_CALL nfllm_plugin_runtime_extension_count(uint32_t kind);
nfllm_status_v1 NFLLM_CALL nfllm_plugin_runtime_extension_at(
    uint32_t kind, uint32_t cpu_capabilities, uint32_t index,
    struct nfllm_runtime_extension_v1 *extension);
nfllm_status_v1 NFLLM_CALL nfllm_plugin_runtime_find_extension(
    uint32_t kind, uint32_t extension_id, uint32_t cpu_capabilities,
    uint32_t preferred_plugin_id, const void **interface_descriptor);

/* Returns NFLLM_E_NOT_FOUND only when no installed model-format plugin claims
 * the file; other statuses mean a format claimed it and the extension path
 * either completed or failed. The callback owns token rendering/output.
 */
nfllm_status_v1 NFLLM_CALL nfllm_plugin_runtime_generate_external(
    const char *path, const char *prompt, uint32_t max_new_tokens,
    nfllm_emit_token_v1 emit_token, void *emit_context,
    uint32_t *generated_tokens, char *diagnostic,
    uint32_t diagnostic_capacity);

#ifdef __cplusplus
}
#endif

#endif
