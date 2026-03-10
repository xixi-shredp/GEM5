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

#define ENCODE_MMASA_W(md, ms1, ms2) \
    ((((uint32_t)0x1e) << 27) | (((uint32_t)(ms2)) << 21) | \
     (((uint32_t)(ms1)) << 18) | (((uint32_t)(md)) << 15) | \
     (((uint32_t)0x10) << 7) | MAT_OPCODE)

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
        break;
      case 1:
        asm volatile(".word %0" : : "i"(ENCODE_MLD_W(1, 10, 11)),
                     "r"(base_reg), "r"(stride_reg) : "memory");
        break;
      default:
        __builtin_unreachable();
    }
}

static inline void
matrix_store_w(int ms, int32_t *base, uintptr_t stride)
{
    register int32_t *base_reg asm("a0") = base;
    register uintptr_t stride_reg asm("a1") = stride;

    switch (ms) {
      case 2:
        asm volatile(".word %0" : : "i"(ENCODE_MST_W(2, 10, 11)),
                     "r"(base_reg), "r"(stride_reg) : "memory");
        break;
      default:
        __builtin_unreachable();
    }
}

static inline void
matrix_zero(int md)
{
    switch (md) {
      case 2:
        asm volatile(".word %0" : : "i"(ENCODE_MZERO(2)) : "memory");
        break;
      default:
        __builtin_unreachable();
    }
}

static inline void
matrix_mmasa_w(int md, int ms1, int ms2)
{
    if (md == 2 && ms1 == 1 && ms2 == 0) {
        asm volatile(".word %0" : : "i"(ENCODE_MMASA_W(2, 1, 0)) : "memory");
        return;
    }

    __builtin_unreachable();
}

static int32_t a[4][4] __attribute__((aligned(16))) = {
    {1, 2, 3, 4},
    {5, 6, 7, 8},
    {9, 10, 11, 12},
    {13, 14, 15, 16},
};

static int32_t b[4][4] __attribute__((aligned(16))) = {
    {2, 0, 1, 3},
    {1, 2, 0, 1},
    {3, 1, 2, 0},
    {0, 1, 3, 2},
};

static int32_t c[4][4] __attribute__((aligned(16)));

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
    int32_t ref[4][4] = {{0}};

    matrix_load_w(0, &a[0][0], 16);
    matrix_load_w(1, &b[0][0], 16);
    matrix_zero(2);
    matrix_mmasa_w(2, 1, 0);
    matrix_store_w(2, &c[0][0], 16);

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            int32_t acc = 0;
            for (int k = 0; k < 4; ++k) {
                acc += a[i][k] * b[j][k];
            }
            ref[i][j] = acc;
        }
    }

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (c[i][j] != ref[i][j]) {
                return 1 + i * 4 + j;
            }
        }
    }

    return 0;
}
