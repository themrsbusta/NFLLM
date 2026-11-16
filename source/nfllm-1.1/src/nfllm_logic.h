#ifndef NFLLM_LOGIC_H
#define NFLLM_LOGIC_H

#include "nfllm_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t NFLLM_CALL nfllm_logic_and_u32(uint32_t a, uint32_t b);
uint32_t NFLLM_CALL nfllm_logic_or_u32(uint32_t a, uint32_t b);
uint32_t NFLLM_CALL nfllm_logic_xor_u32(uint32_t a, uint32_t b);
uint32_t NFLLM_CALL nfllm_logic_not_u32(uint32_t a);
nfllm_status_v1 NFLLM_CALL nfllm_logic_shl_u32(uint32_t value, uint32_t shift,
                                                uint32_t *result);
nfllm_status_v1 NFLLM_CALL nfllm_logic_shr_u32(uint32_t value, uint32_t shift,
                                                uint32_t *result);

/* add/sub/mul return the low 32 bits and report unsigned overflow. */
nfllm_status_v1 NFLLM_CALL nfllm_logic_add_u32(uint32_t a, uint32_t b,
                                                uint32_t *result);
nfllm_status_v1 NFLLM_CALL nfllm_logic_sub_u32(uint32_t a, uint32_t b,
                                                uint32_t *result);
nfllm_status_v1 NFLLM_CALL nfllm_logic_mul_u32(uint32_t a, uint32_t b,
                                                uint32_t *result);
nfllm_status_v1 NFLLM_CALL nfllm_logic_div_u32(uint32_t numerator,
                                                uint32_t denominator,
                                                uint32_t *quotient,
                                                uint32_t *remainder);
int32_t NFLLM_CALL nfllm_logic_compare_u32(uint32_t a, uint32_t b);

/* IEEE-754 binary32 bit conversions; u32-to-f32 uses round-to-nearest-even. */
uint32_t NFLLM_CALL nfllm_logic_u32_to_f32_bits(uint32_t value);
nfllm_status_v1 NFLLM_CALL nfllm_logic_f32_bits_to_u32(
    uint32_t bits, uint32_t *result);

#ifdef __cplusplus
}
#endif

#endif
