#include "nfllm_registry.h"

/* Linked modules register here. Keep registration explicit and deterministic. */
nfllm_status_v1 NFLLM_CALL nfllm_register_builtin_plugins(void)
{
    /* More specialized kernels precede the baseline so capability-filtered
       automatic resolution can select them without weakening the fallback. */
    nfllm_status_v1 status = nfllm_register_3dnow_plugin();
    if (status != NFLLM_OK) return status;
    /* Keep the unbenchmarked MMX quantizer behind the scalar baseline. */
    status = nfllm_register_reference_plugin();
    if (status != NFLLM_OK) return status;
    /* SSE2 remains opt-in until DOS/DPMI XMM-state behavior is established. */
    status = nfllm_register_sse2_plugin();
    if (status != NFLLM_OK) return status;
    status = nfllm_register_mmx_plugin();
    if (status != NFLLM_OK) return status;
    status = nfllm_register_q8_0_plugin();
    if (status != NFLLM_OK) return status;
    status = nfllm_register_q4_0_plugin();
    if (status != NFLLM_OK) return status;
    status = nfllm_register_q2_plugin();
    if (status != NFLLM_OK) return status;
    return nfllm_register_iq3_plugin();
}
