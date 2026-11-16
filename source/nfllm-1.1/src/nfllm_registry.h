#ifndef NFLLM_REGISTRY_H
#define NFLLM_REGISTRY_H

#include "nfllm_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NFLLM_REGISTRY_MAX_PLUGINS
#define NFLLM_REGISTRY_MAX_PLUGINS 8U
#endif
#ifndef NFLLM_REGISTRY_MAX_KERNELS_PER_PLUGIN
#define NFLLM_REGISTRY_MAX_KERNELS_PER_PLUGIN 8U
#endif
#define NFLLM_DIAGNOSTIC_CAPACITY 256U

nfllm_status_v1 NFLLM_CALL nfllm_registry_initialize(
    const struct nfllm_host_api_v1 *host);
nfllm_status_v1 NFLLM_CALL nfllm_registry_register(
    const struct nfllm_plugin_descriptor_v1 *plugin,
    const struct nfllm_kernel_descriptor_v1 *kernels,
    uint32_t kernel_count);
void NFLLM_CALL nfllm_registry_shutdown(void);
nfllm_status_v1 NFLLM_CALL nfllm_registry_resolve(
    uint32_t operation_id, uint32_t requested_plugin_id,
    uint32_t host_capabilities,
    const struct nfllm_kernel_descriptor_v1 **kernel,
    uint32_t *selected_plugin_id,
    char *diagnostic, uint32_t diagnostic_capacity);

nfllm_status_v1 NFLLM_CALL nfllm_register_reference_plugin(void);
nfllm_status_v1 NFLLM_CALL nfllm_register_mmx_plugin(void);
nfllm_status_v1 NFLLM_CALL nfllm_register_3dnow_plugin(void);
nfllm_status_v1 NFLLM_CALL nfllm_register_sse2_plugin(void);
nfllm_status_v1 NFLLM_CALL nfllm_register_q8_0_plugin(void);
nfllm_status_v1 NFLLM_CALL nfllm_register_q4_0_plugin(void);
nfllm_status_v1 NFLLM_CALL nfllm_register_q2_plugin(void);
nfllm_status_v1 NFLLM_CALL nfllm_register_iq3_plugin(void);
nfllm_status_v1 NFLLM_CALL nfllm_register_builtin_plugins(void);
nfllm_status_v1 NFLLM_CALL nfllm_dispatcher_configure(
    uint32_t requested_plugin_id, uint32_t host_capabilities,
    char *diagnostic, uint32_t diagnostic_capacity);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_f32(
    const struct nfllm_matvec_f32_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_q8_0_row(
    const struct nfllm_q8_0_dequantize_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_q4_0_row(
    const struct nfllm_q4_0_dequantize_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q8_0_row(
    const struct nfllm_q8_0_matvec_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q8_0_row_scalar(
    const struct nfllm_q8_0_matvec_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q4_0_row(
    const struct nfllm_q4_0_matvec_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q4_0_row_scalar(
    const struct nfllm_q4_0_matvec_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q8_0_matrix(
    const struct nfllm_quantized_matvec_matrix_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_q4_0_matrix(
    const struct nfllm_quantized_matvec_matrix_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_matvec_packed_row(
    uint32_t operation_id,
    const struct nfllm_packed_matvec_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_q2_0_row(
    const struct nfllm_q2_0_dequantize_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_q2_k_row(
    const struct nfllm_q2_k_dequantize_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_iq3_xxs_row(
    const struct nfllm_iq3_xxs_dequantize_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_dequantize_iq3_s_row(
    const struct nfllm_iq3_s_dequantize_row_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_rms_norm_f32(
    const struct nfllm_rms_norm_f32_args_v1 *arguments);
nfllm_status_v1 NFLLM_CALL nfllm_dispatch_rope_f32(
    const struct nfllm_rope_f32_args_v1 *arguments);

#ifdef __cplusplus
}
#endif

#endif
