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

#define ENCODE_MMAQA_B(md, ms1, ms2) \
    ((((uint32_t)0x02) << 27) | (((uint32_t)(ms2)) << 21) | \
     (((uint32_t)(ms1)) << 18) | (((uint32_t)(md)) << 15) | \
     (((uint32_t)0x00) << 7) | MAT_OPCODE)

#define ENCODE_MZERO(md) \
    ((((uint32_t)0x1f) << 27) | (((uint32_t)(md)) << 15) | MAT_OPCODE)

static inline void
matrix_load_w(int md, const int32_t *base, uintptr_t stride)
{
    register const int32_t *base_reg asm("a0") = base;
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
matrix_store_w_m2(int32_t *base, uintptr_t stride)
{
    register int32_t *base_reg asm("a0") = base;
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
matrix_mmaqa_b_m2_m1_m0(void)
{
    asm volatile(".word %0" : : "i"(ENCODE_MMAQA_B(2, 1, 0)) : "memory");
}

static int32_t a_words[4][4] __attribute__((aligned(16))) = {
    {0x01020304, 0xfefdfcfb, 0x10111213, 0x80818283},
    {0x20212223, 0x7f7e7d7c, 0xa0a1a2a3, 0x40414243},
    {0xff0102fe, 0x30313233, 0xc0c1c2c3, 0x05060708},
    {0x11121314, 0x90919293, 0x21222324, 0xd0d1d2d3},
};

static int32_t b_words[4][4] __attribute__((aligned(16))) = {
    {0x03020100, 0x7f0001ff, 0x20212223, 0x10111213},
    {0xf0f1f2f3, 0x04030201, 0x80818283, 0x05060708},
    {0x11100f0e, 0x90919293, 0x30313233, 0xfffefdfc},
    {0xa5a4a3a2, 0x13121110, 0x40414243, 0x7c7d7e7f},
};

static int32_t out_words[4][4] __attribute__((aligned(16)));

static inline int8_t
get_byte_lane(int32_t value, int lane)
{
    return (int8_t)((uint32_t)value >> (lane * 8));
}

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
    int32_t ref[4][4];

    matrix_load_w(0, &a_words[0][0], 16);
    matrix_load_w(1, &b_words[0][0], 16);
    matrix_zero_m2();
    matrix_mmaqa_b_m2_m1_m0();
    matrix_store_w_m2(&out_words[0][0], 16);

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            int32_t acc = 0;
            for (int word = 0; word < 4; ++word) {
                for (int lane = 0; lane < 4; ++lane) {
                    acc += (int32_t)get_byte_lane(a_words[row][word], lane) *
                           (int32_t)get_byte_lane(b_words[col][word], lane);
                }
            }
            ref[row][col] = acc;
        }
    }

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (out_words[row][col] != ref[row][col]) {
                return 1 + row * 4 + col;
            }
        }
    }

    return 0;
}
