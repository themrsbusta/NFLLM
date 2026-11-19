#ifndef NFLLM_GRAPH_SDK_H
#define NFLLM_GRAPH_SDK_H

/*
 * NFLLM's public graph interchange ABI.
 *
 * This header describes a bounded subset of the public TFLite FlatBuffer
 * schema and the NFLLM operator call contract. It contains no TinyEngine,
 * MCUNet, TensorFlow or LiteRT implementation code.
 */
#include "nfllm_plugin_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFLLM_GRAPH_ABI_V1 1UL
#define NFLLM_GRAPH_FORMAT_ID "TFLITE_V3"
#define NFLLM_GRAPH_ARCH_ID "NFLLM_GRAPHVM_INT8_V1"
#define NFLLM_GRAPH_MAX_RANK 4U
#define NFLLM_GRAPH_MAX_OPERATOR_INPUTS 4U
#define NFLLM_GRAPH_MAX_OPERATOR_OUTPUTS 2U

#define NFLLM_PLUGIN_ID_MODEL_IO 0x4d494f31UL /* MIO1 */
#define NFLLM_PLUGIN_ID_GRAPH_VM 0x47564d31UL /* GVM1 */
#define NFLLM_PLUGIN_ID_INT8_OPS 0x49384f50UL /* I8OP */

#define NFLLM_EXT_ID_TFLITE_FORMAT 0x54464c31UL
#define NFLLM_EXT_ID_GRAPH_VM 0x47564131UL /* GVA1 */

/* TFLite BuiltinOperator numeric identifiers used by the model schema. */
#define NFLLM_TFLITE_OP_ADD 0U
#define NFLLM_TFLITE_OP_AVERAGE_POOL_2D 1U
#define NFLLM_TFLITE_OP_CONV_2D 3U
#define NFLLM_TFLITE_OP_DEPTHWISE_CONV_2D 4U
#define NFLLM_TFLITE_OP_RESHAPE 22U
#define NFLLM_TFLITE_OP_PAD 34U

/* TFLite tensor element identifiers. */
#define NFLLM_TFLITE_TYPE_INT32 2U
#define NFLLM_TFLITE_TYPE_INT8 9U

/* Operator callbacks exported by the independent scalar operator package. */
#define NFLLM_OP_GRAPH_PAD 0x4e460001UL
#define NFLLM_OP_GRAPH_CONV2D 0x4e460002UL
#define NFLLM_OP_GRAPH_DEPTHWISE_CONV2D 0x4e460003UL
#define NFLLM_OP_GRAPH_ADD 0x4e460004UL
#define NFLLM_OP_GRAPH_AVERAGE_POOL 0x4e460005UL
#define NFLLM_OP_GRAPH_RESHAPE 0x4e460006UL

struct nfllm_graph_tensor_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    char name[NFLLM_MODEL_TENSOR_NAME_MAX];
    uint32_t rank;
    uint32_t dims[NFLLM_GRAPH_MAX_RANK];
    uint32_t element_type;
    uint32_t buffer_index;
    const uint8_t *constant_data;
    uint32_t constant_bytes;
    const float *scales;
    uint32_t scale_count;
    const int64_t *zero_points;
    uint32_t zero_point_count;
    int32_t quantized_dimension;
};

struct nfllm_graph_operator_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t builtin_operator;
    uint32_t input_count;
    int32_t inputs[NFLLM_GRAPH_MAX_OPERATOR_INPUTS];
    uint32_t output_count;
    int32_t outputs[NFLLM_GRAPH_MAX_OPERATOR_OUTPUTS];
    uint32_t padding; /* TFLite: 0=SAME, 1=VALID. */
    uint32_t stride_width;
    uint32_t stride_height;
    uint32_t filter_width;
    uint32_t filter_height;
    uint32_t dilation_width;
    uint32_t dilation_height;
    uint32_t depth_multiplier;
    uint32_t fused_activation; /* 0=NONE, 1=RELU, 2=RELU_N1_TO_1, 3=RELU6. */
};

struct nfllm_model_graph_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const struct nfllm_graph_tensor_v1 *tensors;
    uint32_t tensor_count;
    const struct nfllm_graph_operator_v1 *operators;
    uint32_t operator_count;
    const int32_t *inputs;
    uint32_t input_count;
    const int32_t *outputs;
    uint32_t output_count;
};

/* The architecture keeps these activation pointers alive for one invocation.
 * Constant tensor bytes remain owned by the TFLite format plug-in. */
struct nfllm_graph_operator_call_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const struct nfllm_model_graph_v1 *graph;
    const struct nfllm_graph_operator_v1 *operation;
    uint8_t *const *activations;
};

#ifdef __cplusplus
}
#endif

#endif
