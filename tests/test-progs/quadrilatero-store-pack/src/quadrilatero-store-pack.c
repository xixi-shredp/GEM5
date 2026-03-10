#include <stdint.h>

#define MAT_OPCODE 0x2bU

#define ENCODE_MLD_W(md, rs1, rs2) \
    ((((uint32_t)0x00) << 25) | \
     (((uint32_t)(rs2)) << 20) | (((uint32_t)(rs1)) << 15) | \
     (((uint32_t)0x0) << 12) | (((uint32_t)0x2) << 10) | \
     (((uint32_t)(md)) << 7) | MAT_OPCODE)

#define ENCODE_MST_B(ms3, rs1, rs2) \
    ((((uint32_t)0x05) << 25) | \
     (((uint32_t)(rs2)) << 20) | (((uint32_t)(rs1)) << 15) | \
     (((uint32_t)0x0) << 12) | (((uint32_t)0x0) << 10) | \
     (((uint32_t)(ms3)) << 7) | MAT_OPCODE)

#define ENCODE_MST_H(ms3, rs1, rs2) \
    ((((uint32_t)0x05) << 25) | \
     (((uint32_t)(rs2)) << 20) | (((uint32_t)(rs1)) << 15) | \
     (((uint32_t)0x0) << 12) | (((uint32_t)0x1) << 10) | \
     (((uint32_t)(ms3)) << 7) | MAT_OPCODE)

static inline void
matrix_load_w_m0(const int32_t *base, uintptr_t stride)
{
    register const int32_t *base_reg asm("a0") = base;
    register uintptr_t stride_reg asm("a1") = stride;
    asm volatile(".word %0" : : "i"(ENCODE_MLD_W(0, 10, 11)),
                 "r"(base_reg), "r"(stride_reg) : "memory");
}

static inline void
matrix_store_h_m0(uint16_t *base, uintptr_t stride)
{
    register uint16_t *base_reg asm("a0") = base;
    register uintptr_t stride_reg asm("a1") = stride;
    asm volatile(".word %0" : : "i"(ENCODE_MST_H(0, 10, 11)),
                 "r"(base_reg), "r"(stride_reg) : "memory");
}

static inline void
matrix_store_b_m0(uint8_t *base, uintptr_t stride)
{
    register uint8_t *base_reg asm("a0") = base;
    register uintptr_t stride_reg asm("a1") = stride;
    asm volatile(".word %0" : : "i"(ENCODE_MST_B(0, 10, 11)),
                 "r"(base_reg), "r"(stride_reg) : "memory");
}

static int32_t src[4][4] __attribute__((aligned(16))) = {
    {0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00},
    {0x01234567, 0x89abcdef, 0x13579bdf, 0x2468ace0},
    {0xdeadbeef, 0xcafebabe, 0x0badc0de, 0xfeedface},
    {0x76543210, 0xf0e1d2c3, 0x3c2d1e0f, 0xa5a5a5a5},
};

static uint16_t dst_h[4][8] __attribute__((aligned(16)));
static uint8_t dst_b[4][16] __attribute__((aligned(16)));

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
    matrix_load_w_m0(&src[0][0], 16);
    matrix_store_h_m0(&dst_h[0][0], 16);
    matrix_store_b_m0(&dst_b[0][0], 16);

    for (int row = 0; row < 4; ++row) {
        for (int word = 0; word < 4; ++word) {
            uint32_t value = (uint32_t)src[row][word];
            uint16_t lo16 = (uint16_t)(value & 0xffffu);
            uint16_t hi16 = (uint16_t)((value >> 16) & 0xffffu);
            int hcol = word * 2;
            if (dst_h[row][hcol] != lo16 || dst_h[row][hcol + 1] != hi16) {
                return 1 + row * 8 + hcol;
            }

            int bcol = word * 4;
            if (dst_b[row][bcol + 0] != (uint8_t)(value & 0xffu) ||
                dst_b[row][bcol + 1] != (uint8_t)((value >> 8) & 0xffu) ||
                dst_b[row][bcol + 2] != (uint8_t)((value >> 16) & 0xffu) ||
                dst_b[row][bcol + 3] != (uint8_t)((value >> 24) & 0xffu)) {
                return 100 + row * 16 + bcol;
            }
        }
    }

    return 0;
}
