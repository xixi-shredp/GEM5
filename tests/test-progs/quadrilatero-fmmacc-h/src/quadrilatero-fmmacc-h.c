#include <stdint.h>

#define MAT_OPCODE 0x2bU

#define ENCODE_MLD_W(md, rs1, rs2) \
    ((((uint32_t)0x00) << 25) | \
     (((uint32_t)(rs2)) << 20) | (((uint32_t)(rs1)) << 15) | \
     (((uint32_t)0x0) << 12) | (((uint32_t)0x2) << 10) | \
     (((uint32_t)(md)) << 7) | MAT_OPCODE)

#define ENCODE_MST_W(ms3, rs1, rs2) \
    ((((uint32_t)0x06) << 25) | \
     (((uint32_t)(rs2)) << 20) | (((uint32_t)(rs1)) << 15) | \
     (((uint32_t)0x0) << 12) | (((uint32_t)0x2) << 10) | \
     (((uint32_t)(ms3)) << 7) | MAT_OPCODE)

#define ENCODE_FMMACC_H(md, ms1, ms2) \
    ((((uint32_t)0x01) << 27) | (((uint32_t)(ms2)) << 21) | \
     (((uint32_t)(ms1)) << 18) | (((uint32_t)(md)) << 15) | \
     (((uint32_t)0x08) << 7) | MAT_OPCODE)

#define ENCODE_MZERO(md) \
    ((((uint32_t)0x1f) << 27) | (((uint32_t)(md)) << 15) | MAT_OPCODE)

static inline void
matrix_load_w(int md, const uint32_t *base, uintptr_t stride)
{
    register const uint32_t *base_reg asm("a0") = base;
    register uintptr_t stride_reg asm("a1") = stride;

    switch (md) {
      case 0:
        asm volatile(".word %0" : : "i"(ENCODE_MLD_W(0, 10, 11)),
                     "r"(base_reg), "r"(stride_reg) : "memory");
        return;
      case 1:
        asm volatile(".word %0" : : "i"(ENCODE_MLD_W(1, 10, 11)),
                     "r"(base_reg), "r"(stride_reg) : "memory");
        return;
    }

    __builtin_unreachable();
}

static inline void
matrix_store_w_m2(uint32_t *base, uintptr_t stride)
{
    register uint32_t *base_reg asm("a0") = base;
    register uintptr_t stride_reg asm("a1") = stride;
    asm volatile(".word %0" : : "i"(ENCODE_MST_W(2, 10, 11)),
                 "r"(base_reg), "r"(stride_reg) : "memory");
}

static inline void
matrix_zero_m2(void)
{
    asm volatile(".word %0" : : "i"(ENCODE_MZERO(2)) : "memory");
}

static inline void
matrix_fmmacc_h_m2_m1_m0(void)
{
    asm volatile(".word %0" : : "i"(ENCODE_FMMACC_H(2, 1, 0)) : "memory");
}

static inline uint32_t
float_to_bits(float value)
{
    union {
        float f;
        uint32_t u;
    } conv = {.f = value};
    return conv.u;
}

static inline float
bits_to_float(uint32_t value)
{
    union {
        uint32_t u;
        float f;
    } conv = {.u = value};
    return conv.f;
}

static float
half_to_float(uint16_t h)
{
    const uint32_t sign = (uint32_t)(h >> 15) & 0x1u;
    const uint32_t exp = (uint32_t)(h >> 10) & 0x1fu;
    const uint32_t frac = (uint32_t)h & 0x3ffu;
    uint32_t bits;

    if (exp == 0) {
        if (frac == 0) {
            bits = sign << 31;
        } else {
            uint32_t mant = frac;
            int shift = 0;
            while ((mant & 0x400u) == 0) {
                mant <<= 1;
                ++shift;
            }
            mant &= 0x3ffu;
            bits = (sign << 31) |
                   ((uint32_t)(127 - 15 - shift) << 23) |
                   (mant << 13);
        }
    } else if (exp == 0x1f) {
        bits = (sign << 31) | 0x7f800000u | (frac << 13);
    } else {
        bits = (sign << 31) |
               ((uint32_t)(exp + (127 - 15)) << 23) |
               (frac << 13);
    }

    return bits_to_float(bits);
}

static inline uint16_t
get_half_lane(uint32_t value, int lane)
{
    return (uint16_t)(value >> (lane * 16));
}

static uint32_t a_bits[4][4] __attribute__((aligned(16))) = {
    {0x3c003e00, 0x40004200, 0xbc003800, 0x4400c000},
    {0x35553a00, 0x3c00b800, 0x3e003f00, 0x41004280},
    {0x00013c00, 0x7bff0400, 0x80013c00, 0x3555b555},
    {0x3c00c000, 0x42004300, 0x38003a00, 0xbc00be00},
};

static uint32_t b_bits[4][4] __attribute__((aligned(16))) = {
    {0x40003c00, 0x4200c000, 0x35553a00, 0x3e00b800},
    {0x3c00bc00, 0x38004000, 0x42004300, 0x3a00be00},
    {0x04007bff, 0x3c000001, 0xb5553555, 0x41004280},
    {0xc0003c00, 0x43004200, 0xbe00bc00, 0x3f003e00},
};

static uint32_t out_bits[4][4] __attribute__((aligned(16)));

int main(void);

__attribute__((noreturn)) void
_start(void)
{
    asm volatile(
        ".option push\n"
        ".option norelax\n"
        "la gp, __global_pointer$\n"
        ".option pop\n"
        :
        :
        : "gp", "memory");

    register long code asm("a0") = main();

    asm volatile(
        "li a7, 93\n"
        "ecall\n"
        :
        : "r"(code)
        : "a7", "memory");

    __builtin_unreachable();
}

int
main(void)
{
    float ref[4][4];

    matrix_load_w(0, &a_bits[0][0], 16);
    matrix_load_w(1, &b_bits[0][0], 16);
    matrix_zero_m2();
    matrix_fmmacc_h_m2_m1_m0();
    matrix_store_w_m2(&out_bits[0][0], 16);

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float acc = 0.0f;
            for (int word = 0; word < 4; ++word) {
                for (int lane = 0; lane < 2; ++lane) {
                    acc += half_to_float(get_half_lane(a_bits[row][word], lane)) *
                           half_to_float(get_half_lane(b_bits[col][word], lane));
                }
            }
            ref[row][col] = acc;
        }
    }

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (out_bits[row][col] != float_to_bits(ref[row][col])) {
                return 1 + row * 4 + col;
            }
        }
    }

    return 0;
}
