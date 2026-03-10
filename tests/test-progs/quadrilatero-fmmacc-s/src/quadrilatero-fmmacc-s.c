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

#define ENCODE_FMMACC_S(md, ms1, ms2) \
    ((((uint32_t)0x01) << 27) | (((uint32_t)(ms2)) << 21) | \
     (((uint32_t)(ms1)) << 18) | (((uint32_t)(md)) << 15) | \
     (((uint32_t)0x10) << 7) | MAT_OPCODE)

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
matrix_fmmacc_s_m2_m1_m0(void)
{
    asm volatile(".word %0" : : "i"(ENCODE_FMMACC_S(2, 1, 0)) : "memory");
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

static uint32_t a_bits[4][4] __attribute__((aligned(16))) = {
    {0x3f800000, 0x40000000, 0x40400000, 0x40800000},
    {0x3f000000, 0x3fc00000, 0x40200000, 0x40600000},
    {0xbf800000, 0x3f400000, 0xc0000000, 0x3e800000},
    {0x41200000, 0xc1200000, 0x3eaaaaab, 0x3f2aaaab},
};

static uint32_t b_bits[4][4] __attribute__((aligned(16))) = {
    {0x40000000, 0x40400000, 0x40800000, 0x40a00000},
    {0x3f800000, 0xbf800000, 0x3f000000, 0xbf000000},
    {0x3e800000, 0x3f000000, 0x3f800000, 0x40000000},
    {0xc0000000, 0x3fc00000, 0x40200000, 0xc0400000},
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
    matrix_fmmacc_s_m2_m1_m0();
    matrix_store_w_m2(&out_bits[0][0], 16);

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float acc = 0.0f;
            for (int k = 0; k < 4; ++k) {
                acc += bits_to_float(a_bits[row][k]) *
                       bits_to_float(b_bits[col][k]);
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
