#include "nfllm_registry.h"
#include <math.h>

static nfllm_status_v1 validate_header(uint32_t struct_size,
                                       uint32_t abi_version,
                                       uint32_t required_size)
{
    if (struct_size < required_size) return NFLLM_E_SIZE;
    if (abi_version != NFLLM_ABI_VERSION_1) return NFLLM_E_ABI;
    return NFLLM_OK;
}

static int32_t NFLLM_CALL reference_matvec(const void *opaque)
{
    const struct nfllm_matvec_f32_args_v1 *args =
        (const struct nfllm_matvec_f32_args_v1 *)opaque;
    uint32_t row, column;
    nfllm_status_v1 status;
    if (!args) return NFLLM_E_ARGUMENT;
    status = validate_header(args->struct_size, args->abi_version, sizeof(*args));
    if (status != NFLLM_OK) return status;
    if (!args->weights || !args->input || !args->output ||
        args->rows == 0U || args->columns == 0U) return NFLLM_E_ARGUMENT;
    if (args->rows > 0xffffffffUL / args->columns) return NFLLM_E_OVERFLOW;
    for (row = 0U; row < args->rows; ++row) {
        const float *weights = args->weights + row * args->columns;
        float sum = 0.0f;
        for (column = 0U; column < args->columns; ++column)
            sum += weights[column] * args->input[column];
        args->output[row] = sum;
    }
    return NFLLM_OK;
}

static int32_t NFLLM_CALL reference_rms_norm(const void *opaque)
{
    const struct nfllm_rms_norm_f32_args_v1 *args =
        (const struct nfllm_rms_norm_f32_args_v1 *)opaque;
    uint32_t i;
    double sum = 0.0;
    float inverse;
    nfllm_status_v1 status;
    if (!args) return NFLLM_E_ARGUMENT;
    status = validate_header(args->struct_size, args->abi_version, sizeof(*args));
    if (status != NFLLM_OK) return status;
    if (!args->input || !args->weight || !args->output || args->count == 0U ||
        args->epsilon < 0.0f) return NFLLM_E_ARGUMENT;
    for (i = 0U; i < args->count; ++i)
        sum += (double)args->input[i] * args->input[i];
    inverse = (float)(1.0 / sqrt(sum / args->count + args->epsilon));
    for (i = 0U; i < args->count; ++i)
        args->output[i] = args->input[i] * inverse * args->weight[i];
    return NFLLM_OK;
}

static int32_t NFLLM_CALL reference_rope(const void *opaque)
{
    const struct nfllm_rope_f32_args_v1 *args =
        (const struct nfllm_rope_f32_args_v1 *)opaque;
    uint32_t head, pair;
    double step;
    nfllm_status_v1 status;
    if (!args) return NFLLM_E_ARGUMENT;
    status = validate_header(args->struct_size, args->abi_version, sizeof(*args));
    if (status != NFLLM_OK) return status;
    if (args->rope_dim == 0U) return NFLLM_OK;
    if (!args->vector || args->heads == 0U || args->head_dim == 0U ||
        args->rope_dim > args->head_dim || (args->rope_dim & 1U) != 0U ||
        args->base <= 0.0f) return NFLLM_E_ARGUMENT;
    if (args->head_dim > 0xffffffffUL / args->heads) return NFLLM_E_OVERFLOW;
    step = pow((double)args->base, -2.0 / args->rope_dim);
    for (head = 0U; head < args->heads; ++head) {
        float frequency = 1.0f;
        for (pair = 0U; pair < args->rope_dim / 2U; ++pair) {
            uint32_t offset = head * args->head_dim + pair * 2U;
            double angle = (double)args->position * frequency;
            float cosine = (float)cos(angle);
            float sine = (float)sin(angle);
            float a = args->vector[offset];
            float b = args->vector[offset + 1U];
            args->vector[offset] = a * cosine - b * sine;
            args->vector[offset + 1U] = a * sine + b * cosine;
            frequency = (float)(frequency * step);
        }
    }
    return NFLLM_OK;
}

static int32_t NFLLM_CALL reference_initialize(
    const struct nfllm_host_api_v1 *host, void **plugin_state)
{
    if (!host || !plugin_state) return NFLLM_E_ARGUMENT;
    *plugin_state = 0;
    return NFLLM_OK;
}

static void NFLLM_CALL reference_shutdown(void *plugin_state)
{
    (void)plugin_state;
}

static const struct nfllm_plugin_descriptor_v1 reference_plugin = {
    sizeof(struct nfllm_plugin_descriptor_v1), NFLLM_ABI_VERSION_1,
    NFLLM_PLUGIN_ID_REFERENCE, NFLLM_CAP_X87, 0U,
    "reference-x87", reference_initialize, reference_shutdown
};

static const struct nfllm_kernel_descriptor_v1 reference_kernels[] = {
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_MATVEC_F32, NFLLM_VALUE_F32, NFLLM_VALUE_F32, 0U,
      "reference.matvec.f32", reference_matvec },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_RMS_NORM_F32, NFLLM_VALUE_F32, NFLLM_VALUE_F32, 0U,
      "reference.rms_norm.f32", reference_rms_norm },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_ROPE_F32, NFLLM_VALUE_F32, NFLLM_VALUE_F32, 0U,
      "reference.rope.f32", reference_rope }
};

nfllm_status_v1 NFLLM_CALL nfllm_register_reference_plugin(void)
{
    return nfllm_registry_register(&reference_plugin, reference_kernels,
                                   sizeof(reference_kernels) /
                                       sizeof(reference_kernels[0]));
}
