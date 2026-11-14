#include "nfllm_logic.h"

uint32_t NFLLM_CALL nfllm_logic_and_u32(uint32_t a, uint32_t b) { return a & b; }
uint32_t NFLLM_CALL nfllm_logic_or_u32(uint32_t a, uint32_t b) { return a | b; }
uint32_t NFLLM_CALL nfllm_logic_xor_u32(uint32_t a, uint32_t b) { return a ^ b; }
uint32_t NFLLM_CALL nfllm_logic_not_u32(uint32_t a) { return ~a; }

nfllm_status_v1 NFLLM_CALL nfllm_logic_shl_u32(uint32_t value,
                                               uint32_t shift,
                                               uint32_t *result)
{
    if (!result) return NFLLM_E_ARGUMENT;
    if (shift >= 32U) { *result = 0U; return NFLLM_E_SHIFT_RANGE; }
    *result = value << shift;
    return NFLLM_OK;
}

nfllm_status_v1 NFLLM_CALL nfllm_logic_shr_u32(uint32_t value,
                                               uint32_t shift,
                                               uint32_t *result)
{
    if (!result) return NFLLM_E_ARGUMENT;
    if (shift >= 32U) { *result = 0U; return NFLLM_E_SHIFT_RANGE; }
    *result = value >> shift;
    return NFLLM_OK;
}

nfllm_status_v1 NFLLM_CALL nfllm_logic_add_u32(uint32_t a, uint32_t b,
                                               uint32_t *result)
{
    uint32_t sum = 0U;
    uint32_t carry = 0U;
    uint32_t bit;
    if (!result) return NFLLM_E_ARGUMENT;
    for (bit = 0U; bit < 32U; ++bit) {
        uint32_t aa = (a >> bit) & 1U;
        uint32_t bb = (b >> bit) & 1U;
        uint32_t partial = aa ^ bb;
        uint32_t out_bit = partial ^ carry;
        carry = (aa & bb) | (partial & carry);
        sum |= out_bit << bit;
    }
    *result = sum;
    return carry ? NFLLM_E_OVERFLOW : NFLLM_OK;
}

nfllm_status_v1 NFLLM_CALL nfllm_logic_sub_u32(uint32_t a, uint32_t b,
                                               uint32_t *result)
{
    uint32_t difference = 0U;
    uint32_t borrow = 0U;
    uint32_t bit;
    if (!result) return NFLLM_E_ARGUMENT;
    for (bit = 0U; bit < 32U; ++bit) {
        uint32_t aa = (a >> bit) & 1U;
        uint32_t bb = (b >> bit) & 1U;
        uint32_t out_bit = aa ^ bb ^ borrow;
        uint32_t next_borrow = ((~aa) & (bb | borrow)) | (bb & borrow);
        difference |= out_bit << bit;
        borrow = next_borrow;
    }
    *result = difference;
    return borrow ? NFLLM_E_OVERFLOW : NFLLM_OK;
}

nfllm_status_v1 NFLLM_CALL nfllm_logic_mul_u32(uint32_t a, uint32_t b,
                                               uint32_t *result)
{
    uint32_t low = 0U;
    uint32_t overflow = 0U;
    uint32_t bit;
    if (!result) return NFLLM_E_ARGUMENT;
    for (bit = 0U; bit < 32U; ++bit) {
        if (b & 1U) {
            uint32_t next;
            nfllm_status_v1 status = nfllm_logic_add_u32(low, a, &next);
            if (status == NFLLM_E_OVERFLOW) overflow = 1U;
            low = next;
        }
        b >>= 1;
        if (b != 0U && (a & 0x80000000UL)) overflow = 1U;
        a <<= 1;
    }
    *result = low;
    return overflow ? NFLLM_E_OVERFLOW : NFLLM_OK;
}

nfllm_status_v1 NFLLM_CALL nfllm_logic_div_u32(uint32_t numerator,
                                               uint32_t denominator,
                                               uint32_t *quotient,
                                               uint32_t *remainder)
{
    uint32_t q = 0U;
    uint32_t r = 0U;
    int bit;
    if (!quotient || !remainder) return NFLLM_E_ARGUMENT;
    *quotient = 0U;
    *remainder = 0U;
    if (denominator == 0U) return NFLLM_E_DIV_ZERO;
    for (bit = 31; bit >= 0; --bit) {
        uint32_t carry = r >> 31;
        r = (r << 1) | ((numerator >> bit) & 1U);
        if (carry || r >= denominator) {
            r -= denominator;
            q |= 1UL << bit;
        }
    }
    *quotient = q;
    *remainder = r;
    return NFLLM_OK;
}

int32_t NFLLM_CALL nfllm_logic_compare_u32(uint32_t a, uint32_t b)
{
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

uint32_t NFLLM_CALL nfllm_logic_u32_to_f32_bits(uint32_t value)
{
    uint32_t msb = 31U;
    uint32_t significand;
    uint32_t exponent;
    uint32_t shift;
    uint32_t remainder;
    uint32_t halfway;
    if (value == 0U) return 0U;
    while (msb != 0U && ((value >> msb) & 1U) == 0U) --msb;
    exponent = msb + 127U;
    if (msb <= 23U) {
        significand = value << (23U - msb);
    } else {
        shift = msb - 23U;
        significand = value >> shift;
        remainder = value & ((1UL << shift) - 1U);
        halfway = 1UL << (shift - 1U);
        if (remainder > halfway ||
            (remainder == halfway && (significand & 1U))) {
            ++significand;
            if (significand == 0x01000000UL) {
                significand >>= 1;
                ++exponent;
            }
        }
    }
    return (exponent << 23) | (significand & 0x007fffffUL);
}

nfllm_status_v1 NFLLM_CALL nfllm_logic_f32_bits_to_u32(
    uint32_t bits, uint32_t *result)
{
    uint32_t sign = bits >> 31;
    uint32_t exponent = (bits >> 23) & 0xffU;
    uint32_t fraction = bits & 0x007fffffUL;
    uint32_t significand;
    int32_t shift;
    if (!result) return NFLLM_E_ARGUMENT;
    *result = 0U;
    if (exponent == 0xffU) return NFLLM_E_NUMERIC;
    if (sign && (exponent != 0U || fraction != 0U)) return NFLLM_E_NUMERIC;
    if (exponent == 0U) return NFLLM_OK;
    shift = (int32_t)exponent - 127 - 23;
    significand = 0x00800000UL | fraction;
    if (shift >= 0) {
        if (shift >= 32 || significand > (0xffffffffUL >> shift))
            return NFLLM_E_OVERFLOW;
        *result = significand << shift;
    } else {
        uint32_t right = (uint32_t)(-shift);
        *result = right >= 32U ? 0U : significand >> right;
    }
    return NFLLM_OK;
}
