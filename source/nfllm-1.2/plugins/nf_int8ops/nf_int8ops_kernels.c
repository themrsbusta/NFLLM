/* Original scalar INT8 kernels for NFLLM's TFLite operator ABI. */
#include "../../sdk/include/nfllm_graph_sdk.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRAPH_MAX_CHANNELS 4096U

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_i32_le(const uint8_t *p)
{
    return (int32_t)read_u32_le(p);
}

static const uint8_t *tensor_bytes(const struct nfllm_model_graph_v1 *graph,
                                   uint8_t *const *activations, int32_t index)
{
    const struct nfllm_graph_tensor_v1 *tensor;
    if (!graph || !activations || index < 0 ||
        (uint32_t)index >= graph->tensor_count) return 0;
    tensor = &graph->tensors[index];
    if (tensor->constant_data && tensor->constant_bytes)
        return tensor->constant_data;
    return activations[index];
}

static uint8_t *output_bytes(const struct nfllm_model_graph_v1 *graph,
                             uint8_t *const *activations, int32_t index)
{
    if (!graph || !activations || index < 0 ||
        (uint32_t)index >= graph->tensor_count) return 0;
    return activations[index];
}

static int checked_element_count(const struct nfllm_graph_tensor_v1 *tensor,
                                 uint32_t *count)
{
    uint32_t i, product = 1U;
    if (!tensor || !count || !tensor->rank ||
        tensor->rank > NFLLM_GRAPH_MAX_RANK) return 0;
    for (i = 0U; i < tensor->rank; ++i) {
        if (!tensor->dims[i] || product > 0xffffffffUL / tensor->dims[i])
            return 0;
        product *= tensor->dims[i];
    }
    *count = product;
    return 1;
}

static int8_t tensor_zero_point(const struct nfllm_graph_tensor_v1 *tensor,
                                uint32_t channel)
{
    uint32_t index;
    int64_t value;
    if (!tensor || !tensor->zero_points || !tensor->zero_point_count) return 0;
    index = tensor->zero_point_count == 1U ? 0U : channel;
    if (index >= tensor->zero_point_count) return 0;
    value = tensor->zero_points[index];
    if (value < -128) value = -128;
    if (value > 127) value = 127;
    return (int8_t)value;
}

static double tensor_scale(const struct nfllm_graph_tensor_v1 *tensor,
                           uint32_t channel)
{
    uint32_t index;
    if (!tensor || !tensor->scales || !tensor->scale_count) return 0.0;
    index = tensor->scale_count == 1U ? 0U : channel;
    if (index >= tensor->scale_count) return 0.0;
    return (double)tensor->scales[index];
}

static int round_i32(double value, int32_t *out)
{
    if (!out || value != value || value > 2147483000.0 ||
        value < -2147483000.0) return 0;
    *out = (int32_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
    return 1;
}

static int quantize_multiplier(double real_multiplier,
                               int32_t *multiplier, int32_t *shift)
{
    double fraction;
    int exponent;
    int64_t fixed;
    if (!multiplier || !shift || real_multiplier < 0.0 ||
        real_multiplier != real_multiplier) return 0;
    if (real_multiplier == 0.0) {
        *multiplier = 0; *shift = 0; return 1;
    }
    fraction = frexp(real_multiplier, &exponent);
    fixed = (int64_t)(fraction * 2147483648.0 + 0.5);
    if (fixed == 2147483648LL) {
        fixed >>= 1;
        ++exponent;
    }
    if (exponent < -31) { fixed = 0; exponent = 0; }
    if (exponent > 30) return 0;
    if (fixed > 2147483647LL) return 0;
    *multiplier = (int32_t)fixed;
    *shift = (int32_t)exponent;
    return 1;
}

static int32_t rounding_divide_by_power_of_two(int32_t value, int exponent)
{
    uint32_t mask, remainder, threshold;
    int32_t base;
    if (exponent <= 0) return value;
    if (exponent >= 32) return 0;
    mask = (1UL << exponent) - 1U;
    remainder = ((uint32_t)value) & mask;
    threshold = (mask >> 1) + (value < 0 ? 1U : 0U);
    base = value >> exponent;
    return base + (remainder > threshold ? 1 : 0);
}

static int32_t saturating_rounding_high_mul(int32_t left, int32_t right)
{
    int64_t product, nudge;
    if (left == (-2147483647L - 1L) && right == (-2147483647L - 1L))
        return 2147483647L;
    product = (int64_t)left * (int64_t)right;
    nudge = product >= 0 ? (1LL << 30) : (1LL - (1LL << 30));
    return (int32_t)((product + nudge) / (1LL << 31));
}

static int32_t multiply_by_quantized_multiplier(int32_t value,
                                                int32_t multiplier,
                                                int32_t shift)
{
    int32_t left_shift, right_shift, shifted, high;
    int64_t wide;
    left_shift = shift > 0 ? shift : 0;
    right_shift = shift > 0 ? 0 : -shift;
    wide = (int64_t)value * (1LL << left_shift);
    if (wide > 2147483647LL) shifted = 2147483647L;
    else if (wide < (-2147483647LL - 1LL)) shifted = -2147483647L - 1L;
    else shifted = (int32_t)wide;
    high = saturating_rounding_high_mul(shifted, multiplier);
    return rounding_divide_by_power_of_two(high, right_shift);
}

static int activation_limits(const struct nfllm_graph_tensor_v1 *output,
                             uint32_t activation, int32_t *minimum,
                             int32_t *maximum)
{
    int32_t zp, high;
    double scale;
    if (!output || !minimum || !maximum || !output->scale_count ||
        !output->zero_point_count) return 0;
    scale = tensor_scale(output, 0U);
    zp = tensor_zero_point(output, 0U);
    if (scale <= 0.0) return 0;
    *minimum = -128;
    *maximum = 127;
    if (activation == 1U || activation == 3U) *minimum = zp;
    else if (activation == 2U) {
        if (!round_i32(-1.0 / scale, &high)) return 0;
        *minimum = high + zp;
    }
    if (activation == 2U) {
        if (!round_i32(1.0 / scale, &high)) return 0;
        *maximum = high + zp;
    } else if (activation == 3U) {
        if (!round_i32(6.0 / scale, &high)) return 0;
        *maximum = high + zp;
    }
    if (*minimum < -128) *minimum = -128;
    if (*minimum > 127) *minimum = 127;
    if (*maximum < -128) *maximum = -128;
    if (*maximum > 127) *maximum = 127;
    return *minimum <= *maximum;
}

static int operator_call(const void *opaque,
                         const struct nfllm_graph_operator_call_v1 **call)
{
    const struct nfllm_graph_operator_call_v1 *candidate =
        (const struct nfllm_graph_operator_call_v1 *)opaque;
    if (!candidate || candidate->struct_size < sizeof(*candidate) ||
        candidate->abi_version != NFLLM_GRAPH_ABI_V1 ||
        !candidate->graph || !candidate->operation || !candidate->activations ||
        candidate->graph->struct_size < sizeof(*candidate->graph) ||
        candidate->graph->abi_version != NFLLM_GRAPH_ABI_V1 ||
        candidate->operation->struct_size < sizeof(*candidate->operation) ||
        candidate->operation->abi_version != NFLLM_GRAPH_ABI_V1)
        return 0;
    *call = candidate;
    return 1;
}

static int padded_geometry(const struct nfllm_graph_operator_v1 *op,
                            uint32_t input_h, uint32_t input_w,
                            uint32_t filter_h, uint32_t filter_w,
                            uint32_t *out_h, uint32_t *out_w,
                            uint32_t *pad_top, uint32_t *pad_left)
{
    uint32_t need_h, need_w, total_h, total_w, effective_h, effective_w;
    uint32_t dilation_h, dilation_w;
    if (!op || !op->stride_height || !op->stride_width ||
        !filter_h || !filter_w) return 0;
    dilation_h = op->dilation_height ? op->dilation_height : 1U;
    dilation_w = op->dilation_width ? op->dilation_width : 1U;
    if (filter_h - 1U > (0xffffffffUL - 1U) / dilation_h ||
        filter_w - 1U > (0xffffffffUL - 1U) / dilation_w) return 0;
    effective_h = (filter_h - 1U) * dilation_h + 1U;
    effective_w = (filter_w - 1U) * dilation_w + 1U;
    if (op->padding == 0U) {
        *out_h = (input_h + op->stride_height - 1U) / op->stride_height;
        *out_w = (input_w + op->stride_width - 1U) / op->stride_width;
        if (*out_h - 1U > (0xffffffffUL - effective_h) / op->stride_height ||
            *out_w - 1U > (0xffffffffUL - effective_w) / op->stride_width)
            return 0;
        need_h = (*out_h - 1U) * op->stride_height + effective_h;
        need_w = (*out_w - 1U) * op->stride_width + effective_w;
        total_h = need_h > input_h ? need_h - input_h : 0U;
        total_w = need_w > input_w ? need_w - input_w : 0U;
        *pad_top = total_h / 2U;
        *pad_left = total_w / 2U;
        return 1;
    }
    if (op->padding != 1U || input_h < effective_h || input_w < effective_w)
        return 0;
    *out_h = (input_h - effective_h) / op->stride_height + 1U;
    *out_w = (input_w - effective_w) / op->stride_width + 1U;
    *pad_top = 0U;
    *pad_left = 0U;
    return 1;
}

static nfllm_status_v1 NFLLM_CALL run_pad(const void *opaque)
{
    const struct nfllm_graph_operator_call_v1 *call;
    const struct nfllm_graph_operator_v1 *op;
    const struct nfllm_graph_tensor_v1 *in, *pads, *out;
    const int32_t *padding_values;
    const int8_t *input;
    int8_t *output;
    uint32_t out_count, i, in_index, out_index, in_coord[4], out_coord[4];
    int32_t before[4], after[4];
    if (!operator_call(opaque, &call)) return NFLLM_E_ARGUMENT;
    op = call->operation;
    if (op->builtin_operator != NFLLM_TFLITE_OP_PAD || op->input_count != 2U ||
        op->output_count != 1U || op->inputs[0] < 0 || op->inputs[1] < 0)
        return NFLLM_E_ARGUMENT;
    in = &call->graph->tensors[op->inputs[0]];
    pads = &call->graph->tensors[op->inputs[1]];
    out = &call->graph->tensors[op->outputs[0]];
    input = (const int8_t *)tensor_bytes(call->graph, call->activations,
                                          op->inputs[0]);
    output = (int8_t *)output_bytes(call->graph, call->activations,
                                     op->outputs[0]);
    padding_values = (const int32_t *)tensor_bytes(call->graph,
        call->activations, op->inputs[1]);
    if (!input || !output || !padding_values || in->element_type != 9U ||
        out->element_type != 9U || in->rank != out->rank || in->rank != 4U ||
        pads->element_type != NFLLM_TFLITE_TYPE_INT32 || pads->rank != 2U ||
        pads->dims[0] != 4U || pads->dims[1] != 2U ||
        pads->constant_bytes < 32U || !checked_element_count(out, &out_count))
        return NFLLM_E_SIZE;
    for (i = 0U; i < 4U; ++i) {
        before[i] = read_i32_le((const uint8_t *)padding_values + i * 8U);
        after[i] = read_i32_le((const uint8_t *)padding_values + i * 8U + 4U);
        if (before[i] < 0 || after[i] < 0 ||
            in->dims[i] + (uint32_t)before[i] + (uint32_t)after[i] != out->dims[i])
            return NFLLM_E_SIZE;
    }
    for (i = 0U; i < out_count; ++i) {
        uint32_t remainder = i, d;
        int inside = 1;
        for (d = 4U; d > 0U; --d) {
            uint32_t axis = d - 1U;
            out_coord[axis] = remainder % out->dims[axis];
            remainder /= out->dims[axis];
            if (out_coord[axis] < (uint32_t)before[axis] ||
                out_coord[axis] - (uint32_t)before[axis] >= in->dims[axis]) {
                inside = 0;
                break;
            }
            in_coord[axis] = out_coord[axis] - (uint32_t)before[axis];
        }
        if (!inside) output[i] = tensor_zero_point(in, 0U);
        else {
            in_index = ((in_coord[0] * in->dims[1] + in_coord[1]) *
                        in->dims[2] + in_coord[2]) * in->dims[3] + in_coord[3];
            out_index = i;
            output[out_index] = input[in_index];
        }
    }
    return NFLLM_OK;
}

static int conv_parameters(const struct nfllm_graph_tensor_v1 *input,
                           const struct nfllm_graph_tensor_v1 *weights,
                           const struct nfllm_graph_tensor_v1 *output,
                           const struct nfllm_graph_operator_v1 *op,
                           uint32_t *out_h, uint32_t *out_w,
                           uint32_t *pad_top, uint32_t *pad_left)
{
    uint32_t expected_h, expected_w;
    if (!input || !weights || !output || !op || input->rank != 4U ||
        weights->rank != 4U || output->rank != 4U ||
        input->element_type != NFLLM_TFLITE_TYPE_INT8 ||
        weights->element_type != NFLLM_TFLITE_TYPE_INT8 ||
        output->element_type != NFLLM_TFLITE_TYPE_INT8 ||
        !weights->dims[1] || !weights->dims[2]) return 0;
    expected_h = weights->dims[1];
    expected_w = weights->dims[2];
    if (!padded_geometry(op, input->dims[1], input->dims[2],
                         expected_h, expected_w, out_h, out_w,
                         pad_top, pad_left)) return 0;
    return output->dims[0] == input->dims[0] &&
        output->dims[1] == *out_h && output->dims[2] == *out_w;
}

static int make_channel_multipliers(
    const struct nfllm_graph_tensor_v1 *input,
    const struct nfllm_graph_tensor_v1 *weights,
    const struct nfllm_graph_tensor_v1 *output,
    uint32_t channels, int32_t *multipliers, int32_t *shifts)
{
    double input_scale = tensor_scale(input, 0U);
    double output_scale = tensor_scale(output, 0U);
    uint32_t channel;
    if (input_scale <= 0.0 || output_scale <= 0.0 ||
        !input->scale_count || !output->scale_count ||
        (weights->scale_count != 1U && weights->scale_count < channels) ||
        (weights->zero_point_count != 1U &&
         weights->zero_point_count < channels)) return 0;
    for (channel = 0U; channel < channels; ++channel) {
        double scale = input_scale * tensor_scale(weights, channel) / output_scale;
        if (tensor_scale(weights, channel) <= 0.0 ||
            !quantize_multiplier(scale, &multipliers[channel], &shifts[channel]))
            return 0;
    }
    return 1;
}

static nfllm_status_v1 run_convolution(
    const struct nfllm_graph_operator_call_v1 *call, int depthwise)
{
    const struct nfllm_graph_operator_v1 *op = call->operation;
    const struct nfllm_graph_tensor_v1 *input, *weights, *bias = 0, *output;
    const int8_t *input_data, *weight_data;
    const int32_t *bias_data = 0;
    int8_t *output_data;
    int32_t *multipliers = 0, *shifts = 0;
    int32_t activation_min, activation_max;
    uint32_t input_index, filter_index, bias_index, output_index;
    uint32_t out_h, out_w, pad_top, pad_left, batches, in_h, in_w;
    uint32_t in_c, out_c, filter_h, filter_w, channel, y, x, batch;
    uint32_t fy, fx, ic, mult, oc;
    uint32_t bias_bytes;
    nfllm_status_v1 result = NFLLM_E_SIZE;
    if (op->input_count != 3U || op->output_count != 1U ||
        op->inputs[0] < 0 || op->inputs[1] < 0 || op->outputs[0] < 0)
        return NFLLM_E_ARGUMENT;
    input_index = (uint32_t)op->inputs[0];
    filter_index = (uint32_t)op->inputs[1];
    bias_index = op->inputs[2] >= 0 ? (uint32_t)op->inputs[2] : 0xffffffffUL;
    output_index = (uint32_t)op->outputs[0];
    if (input_index >= call->graph->tensor_count ||
        filter_index >= call->graph->tensor_count ||
        output_index >= call->graph->tensor_count ||
        (bias_index != 0xffffffffUL && bias_index >= call->graph->tensor_count))
        return NFLLM_E_SIZE;
    input = &call->graph->tensors[input_index];
    weights = &call->graph->tensors[filter_index];
    output = &call->graph->tensors[output_index];
    if (bias_index != 0xffffffffUL) bias = &call->graph->tensors[bias_index];
    input_data = (const int8_t *)tensor_bytes(call->graph, call->activations,
                                               (int32_t)input_index);
    weight_data = (const int8_t *)tensor_bytes(call->graph, call->activations,
                                                (int32_t)filter_index);
    output_data = (int8_t *)output_bytes(call->graph, call->activations,
                                         (int32_t)output_index);
    if (bias) {
        const uint8_t *raw_bias = tensor_bytes(call->graph, call->activations,
                                                (int32_t)bias_index);
        if (!raw_bias || bias->element_type != NFLLM_TFLITE_TYPE_INT32 ||
            !bias->constant_bytes) return NFLLM_E_SIZE;
        bias_data = (const int32_t *)(uintptr_t)raw_bias;
        bias_bytes = bias->constant_bytes;
    } else bias_bytes = 0U;
    if (!input_data || !weight_data || !output_data || input->rank != 4U ||
        weights->rank != 4U || output->rank != 4U ||
        input->element_type != NFLLM_TFLITE_TYPE_INT8 ||
        weights->element_type != NFLLM_TFLITE_TYPE_INT8 ||
        output->element_type != NFLLM_TFLITE_TYPE_INT8) goto done;
    batches = input->dims[0]; in_h = input->dims[1]; in_w = input->dims[2];
    in_c = input->dims[3];
    filter_h = weights->dims[1]; filter_w = weights->dims[2];
    if (!filter_h || !filter_w || !in_c || !op->dilation_width ||
        !op->dilation_height || !batches) goto done;
    if (depthwise) {
        out_c = weights->dims[3];
        mult = op->depth_multiplier;
        if (weights->dims[0] != 1U || !mult || in_c > 0xffffffffUL / mult ||
            in_c * mult != out_c || output->dims[3] != out_c) goto done;
    } else {
        out_c = weights->dims[0];
        if (weights->dims[3] != in_c || output->dims[3] != out_c) goto done;
    }
    if (!out_c || out_c > GRAPH_MAX_CHANNELS ||
        !conv_parameters(input, weights, output, op, &out_h, &out_w,
                         &pad_top, &pad_left)) goto done;
    if (bias && bias_bytes < out_c * 4U) goto done;
    multipliers = (int32_t *)malloc(out_c * sizeof(int32_t));
    shifts = (int32_t *)malloc(out_c * sizeof(int32_t));
    if (!multipliers || !shifts) { result = NFLLM_E_MEMORY; goto done; }
    if (!make_channel_multipliers(input, weights, output, out_c,
                                  multipliers, shifts) ||
        !activation_limits(output, op->fused_activation,
                           &activation_min, &activation_max)) goto done;
    for (batch = 0U; batch < batches; ++batch) {
        for (y = 0U; y < out_h; ++y) {
            int32_t origin_y = (int32_t)(y * op->stride_height) -
                               (int32_t)pad_top;
            for (x = 0U; x < out_w; ++x) {
                int32_t origin_x = (int32_t)(x * op->stride_width) -
                                   (int32_t)pad_left;
                for (channel = 0U; channel < out_c; ++channel) {
                    int64_t accumulator = 0;
                    int32_t input_zp = tensor_zero_point(input, 0U);
                    int32_t weight_zp = tensor_zero_point(weights, channel);
                    int32_t out_value;
                    if (bias_data) accumulator = read_i32_le(
                        (const uint8_t *)bias_data + channel * 4U);
                    for (fy = 0U; fy < filter_h; ++fy) {
                        int32_t iy = origin_y + (int32_t)(fy * op->dilation_height);
                        if (iy < 0 || (uint32_t)iy >= in_h) continue;
                        for (fx = 0U; fx < filter_w; ++fx) {
                            int32_t ix = origin_x + (int32_t)(fx * op->dilation_width);
                            if (ix < 0 || (uint32_t)ix >= in_w) continue;
                            if (depthwise) {
                                uint32_t source_c = channel / mult;
                                uint32_t input_offset =
                                    ((batch * in_h + (uint32_t)iy) * in_w +
                                      (uint32_t)ix) * in_c + source_c;
                                uint32_t filter_offset =
                                    (fy * filter_w + fx) * out_c + channel;
                                accumulator +=
                                    ((int32_t)input_data[input_offset] - input_zp) *
                                    ((int32_t)weight_data[filter_offset] - weight_zp);
                            } else {
                                uint32_t input_base =
                                    ((batch * in_h + (uint32_t)iy) * in_w +
                                      (uint32_t)ix) * in_c;
                                uint32_t weight_base =
                                    ((channel * filter_h + fy) * filter_w + fx) * in_c;
                                for (ic = 0U; ic < in_c; ++ic)
                                    accumulator +=
                                        ((int32_t)input_data[input_base + ic] - input_zp) *
                                        ((int32_t)weight_data[weight_base + ic] - weight_zp);
                            }
                        }
                    }
                    if (accumulator > 2147483647LL) accumulator = 2147483647LL;
                    if (accumulator < (-2147483647LL - 1LL))
                        accumulator = (-2147483647LL - 1LL);
                    out_value = multiply_by_quantized_multiplier((int32_t)accumulator,
                        multipliers[channel], shifts[channel]);
                    out_value += tensor_zero_point(output, 0U);
                    if (out_value < activation_min) out_value = activation_min;
                    if (out_value > activation_max) out_value = activation_max;
                    output_data[((batch * out_h + y) * out_w + x) * out_c + channel] =
                        (int8_t)out_value;
                }
            }
        }
    }
    result = NFLLM_OK;
done:
    free(multipliers);
    free(shifts);
    return result;
}

static nfllm_status_v1 NFLLM_CALL run_conv2d(const void *opaque)
{
    const struct nfllm_graph_operator_call_v1 *call;
    if (!operator_call(opaque, &call) || call->operation->builtin_operator !=
        NFLLM_TFLITE_OP_CONV_2D) return NFLLM_E_ARGUMENT;
    return run_convolution(call, 0);
}

static nfllm_status_v1 NFLLM_CALL run_depthwise(const void *opaque)
{
    const struct nfllm_graph_operator_call_v1 *call;
    if (!operator_call(opaque, &call) || call->operation->builtin_operator !=
        NFLLM_TFLITE_OP_DEPTHWISE_CONV_2D) return NFLLM_E_ARGUMENT;
    return run_convolution(call, 1);
}

static int broadcast_index(const struct nfllm_graph_tensor_v1 *input,
                           const struct nfllm_graph_tensor_v1 *output,
                           uint32_t flat_output, uint32_t *flat_input)
{
    uint32_t coord[4] = { 0U, 0U, 0U, 0U };
    uint32_t input_axis, output_axis, remainder = flat_output, axis;
    if (!input || !output || !flat_input || input->rank > output->rank)
        return 0;
    for (axis = output->rank; axis > 0U; --axis) {
        uint32_t current = axis - 1U;
        coord[current] = remainder % output->dims[current];
        remainder /= output->dims[current];
    }
    *flat_input = 0U;
    for (input_axis = 0U; input_axis < input->rank; ++input_axis) {
        uint32_t input_dim;
        output_axis = output->rank - input->rank + input_axis;
        input_dim = input->dims[input_axis];
        if (input_dim != 1U && input_dim != output->dims[output_axis]) return 0;
        if (*flat_input > 0xffffffffUL / input_dim) return 0;
        *flat_input = *flat_input * input_dim +
            (input_dim == 1U ? 0U : coord[output_axis]);
    }
    return 1;
}

static nfllm_status_v1 NFLLM_CALL run_add(const void *opaque)
{
    const struct nfllm_graph_operator_call_v1 *call;
    const struct nfllm_graph_operator_v1 *op;
    const struct nfllm_graph_tensor_v1 *a, *b, *out;
    const int8_t *a_data, *b_data;
    int8_t *out_data;
    int32_t m1, s1, m2, s2, mo, so, left_shift = 20;
    int32_t act_min, act_max;
    double max_input_scale, input1_real, input2_real, output_real;
    uint32_t elements, i, a_index, b_index;
    if (!operator_call(opaque, &call)) return NFLLM_E_ARGUMENT;
    op = call->operation;
    if (op->builtin_operator != NFLLM_TFLITE_OP_ADD || op->input_count != 2U ||
        op->output_count != 1U || op->inputs[0] < 0 || op->inputs[1] < 0)
        return NFLLM_E_ARGUMENT;
    a = &call->graph->tensors[op->inputs[0]];
    b = &call->graph->tensors[op->inputs[1]];
    out = &call->graph->tensors[op->outputs[0]];
    a_data = (const int8_t *)tensor_bytes(call->graph, call->activations,
                                           op->inputs[0]);
    b_data = (const int8_t *)tensor_bytes(call->graph, call->activations,
                                           op->inputs[1]);
    out_data = (int8_t *)output_bytes(call->graph, call->activations,
                                       op->outputs[0]);
    if (!a_data || !b_data || !out_data || a->element_type != 9U ||
        b->element_type != 9U || out->element_type != 9U ||
        !checked_element_count(out, &elements) || a->rank > 4U || b->rank > 4U)
        return NFLLM_E_SIZE;
    max_input_scale = tensor_scale(a, 0U);
    if (tensor_scale(b, 0U) > max_input_scale) max_input_scale = tensor_scale(b, 0U);
    if (max_input_scale <= 0.0 || tensor_scale(out, 0U) <= 0.0) return NFLLM_E_SIZE;
    max_input_scale *= 2.0;
    input1_real = tensor_scale(a, 0U) / max_input_scale;
    input2_real = tensor_scale(b, 0U) / max_input_scale;
    output_real = max_input_scale /
        ((double)(1UL << left_shift) * tensor_scale(out, 0U));
    if (!quantize_multiplier(input1_real, &m1, &s1) ||
        !quantize_multiplier(input2_real, &m2, &s2) ||
        !quantize_multiplier(output_real, &mo, &so) ||
        !activation_limits(out, op->fused_activation, &act_min, &act_max))
        return NFLLM_E_NUMERIC;
    for (i = 0U; i < elements; ++i) {
        int32_t x, y, left, right, result;
        if (!broadcast_index(a, out, i, &a_index) ||
            !broadcast_index(b, out, i, &b_index)) return NFLLM_E_SIZE;
        x = ((int32_t)a_data[a_index] - tensor_zero_point(a, 0U)) *
            (1L << left_shift);
        y = ((int32_t)b_data[b_index] - tensor_zero_point(b, 0U)) *
            (1L << left_shift);
        left = multiply_by_quantized_multiplier(x, m1, s1);
        right = multiply_by_quantized_multiplier(y, m2, s2);
        result = multiply_by_quantized_multiplier(left + right, mo, so) +
            tensor_zero_point(out, 0U);
        if (result < act_min) result = act_min;
        if (result > act_max) result = act_max;
        out_data[i] = (int8_t)result;
    }
    return NFLLM_OK;
}

static nfllm_status_v1 NFLLM_CALL run_average_pool(const void *opaque)
{
    const struct nfllm_graph_operator_call_v1 *call;
    const struct nfllm_graph_operator_v1 *op;
    const struct nfllm_graph_tensor_v1 *input, *output;
    const int8_t *input_data;
    int8_t *output_data;
    uint32_t out_h, out_w, pad_top, pad_left, y, x, channel, batch;
    int32_t act_min, act_max;
    if (!operator_call(opaque, &call)) return NFLLM_E_ARGUMENT;
    op = call->operation;
    if (op->builtin_operator != NFLLM_TFLITE_OP_AVERAGE_POOL_2D ||
        op->input_count != 1U || op->output_count != 1U || op->inputs[0] < 0)
        return NFLLM_E_ARGUMENT;
    input = &call->graph->tensors[op->inputs[0]];
    output = &call->graph->tensors[op->outputs[0]];
    input_data = (const int8_t *)tensor_bytes(call->graph, call->activations,
                                               op->inputs[0]);
    output_data = (int8_t *)output_bytes(call->graph, call->activations,
                                          op->outputs[0]);
    if (!input_data || !output_data || input->rank != 4U || output->rank != 4U ||
        input->element_type != 9U || output->element_type != 9U ||
        tensor_scale(input, 0U) != tensor_scale(output, 0U) ||
        tensor_zero_point(input, 0U) != tensor_zero_point(output, 0U) ||
        !op->filter_width || !op->filter_height ||
        !padded_geometry(op, input->dims[1], input->dims[2], op->filter_height,
                         op->filter_width, &out_h, &out_w, &pad_top, &pad_left) ||
        output->dims[0] != input->dims[0] || output->dims[1] != out_h ||
        output->dims[2] != out_w || output->dims[3] != input->dims[3] ||
        !activation_limits(output, op->fused_activation, &act_min, &act_max))
        return NFLLM_E_SIZE;
    for (batch = 0U; batch < input->dims[0]; ++batch) {
        for (y = 0U; y < out_h; ++y) {
            int32_t origin_y = (int32_t)(y * op->stride_height) -
                               (int32_t)pad_top;
            for (x = 0U; x < out_w; ++x) {
                int32_t origin_x = (int32_t)(x * op->stride_width) -
                                   (int32_t)pad_left;
                uint32_t fy, fx;
                for (channel = 0U; channel < input->dims[3]; ++channel) {
                    int32_t sum = 0;
                    int32_t count = 0;
                    int32_t average;
                    for (fy = 0U; fy < op->filter_height; ++fy) {
                        int32_t iy = origin_y + (int32_t)fy;
                        if (iy < 0 || (uint32_t)iy >= input->dims[1]) continue;
                        for (fx = 0U; fx < op->filter_width; ++fx) {
                            int32_t ix = origin_x + (int32_t)fx;
                            uint32_t offset;
                            if (ix < 0 || (uint32_t)ix >= input->dims[2]) continue;
                            offset = ((batch * input->dims[1] + (uint32_t)iy) *
                                input->dims[2] + (uint32_t)ix) * input->dims[3] + channel;
                            sum += input_data[offset];
                            ++count;
                        }
                    }
                    if (!count) return NFLLM_E_SIZE;
                    average = sum > 0 ? (sum + count / 2) / count :
                        (sum - count / 2) / count;
                    if (average < act_min) average = act_min;
                    if (average > act_max) average = act_max;
                    output_data[((batch * out_h + y) * out_w + x) *
                        input->dims[3] + channel] = (int8_t)average;
                }
            }
        }
    }
    return NFLLM_OK;
}

static nfllm_status_v1 NFLLM_CALL run_reshape(const void *opaque)
{
    const struct nfllm_graph_operator_call_v1 *call;
    const struct nfllm_graph_operator_v1 *op;
    const struct nfllm_graph_tensor_v1 *input, *output;
    const uint8_t *in_data;
    uint8_t *out_data;
    uint32_t in_count, out_count;
    if (!operator_call(opaque, &call)) return NFLLM_E_ARGUMENT;
    op = call->operation;
    if (op->builtin_operator != NFLLM_TFLITE_OP_RESHAPE ||
        op->input_count < 1U || op->output_count != 1U || op->inputs[0] < 0)
        return NFLLM_E_ARGUMENT;
    input = &call->graph->tensors[op->inputs[0]];
    output = &call->graph->tensors[op->outputs[0]];
    in_data = tensor_bytes(call->graph, call->activations, op->inputs[0]);
    out_data = output_bytes(call->graph, call->activations, op->outputs[0]);
    if (!in_data || !out_data || input->element_type != output->element_type ||
        !checked_element_count(input, &in_count) ||
        !checked_element_count(output, &out_count) || in_count != out_count)
        return NFLLM_E_SIZE;
    if (input->element_type == NFLLM_TFLITE_TYPE_INT32) {
        in_count *= 4U;
    } else if (input->element_type != NFLLM_TFLITE_TYPE_INT8) {
        return NFLLM_E_UNSUPPORTED;
    }
    memcpy(out_data, in_data, in_count);
    return NFLLM_OK;
}

static const struct nfllm_kernel_descriptor_v1 graph_kernels[] = {
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_GRAPH_PAD, NFLLM_VALUE_NONE, NFLLM_VALUE_NONE,
      NFLLM_CAP_386, "nfllm.graph.pad.int8", run_pad },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_GRAPH_CONV2D, NFLLM_VALUE_NONE, NFLLM_VALUE_NONE,
      NFLLM_CAP_386 | NFLLM_CAP_X87, "nfllm.graph.conv2d.int8", run_conv2d },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_GRAPH_DEPTHWISE_CONV2D, NFLLM_VALUE_NONE, NFLLM_VALUE_NONE,
      NFLLM_CAP_386 | NFLLM_CAP_X87, "nfllm.graph.depthwise.int8", run_depthwise },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_GRAPH_ADD, NFLLM_VALUE_NONE, NFLLM_VALUE_NONE,
      NFLLM_CAP_386 | NFLLM_CAP_X87, "nfllm.graph.add.int8", run_add },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_GRAPH_AVERAGE_POOL, NFLLM_VALUE_NONE, NFLLM_VALUE_NONE,
      NFLLM_CAP_386, "nfllm.graph.average_pool.int8", run_average_pool },
    { sizeof(struct nfllm_kernel_descriptor_v1), NFLLM_ABI_VERSION_1,
      NFLLM_OP_GRAPH_RESHAPE, NFLLM_VALUE_NONE, NFLLM_VALUE_NONE,
      NFLLM_CAP_386, "nfllm.graph.reshape", run_reshape }
};

static const struct nfllm_plugin_bundle_v1 nfint8ops_bundle = {
    sizeof(struct nfllm_plugin_bundle_v1), NFLLM_PLUGIN_ABI_V1,
    NFLLM_PLUGIN_ID_INT8_OPS, NFLLM_CAP_386,
    "NFLLM Int8Ops Kernel Pack", "1.0.0",
    0, 0, graph_kernels,
    (uint32_t)(sizeof(graph_kernels) / sizeof(graph_kernels[0])),
    0, 0U
};

nfllm_status_v1 NFLLM_CALL NFLLM_PluginQueryV1(
    const struct nfllm_plugin_host_v1 *host,
    const struct nfllm_plugin_bundle_v1 **bundle)
{
    if (!host || !bundle) return NFLLM_E_ARGUMENT;
    if (host->struct_size < sizeof(*host) ||
        host->abi_version != NFLLM_PLUGIN_ABI_V1) return NFLLM_E_ABI;
    *bundle = &nfint8ops_bundle;
    return NFLLM_OK;
}
