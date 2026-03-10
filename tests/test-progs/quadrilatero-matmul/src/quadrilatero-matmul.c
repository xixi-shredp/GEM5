#include <stdint.h>

#define MAT_OPCODE 0x2bU
#define SIZE 8
#define TILE 4

#define ENCODE_MLD_W(md, rs1, rs2) \
    ((((uint32_t)0x00) << 25) | (((uint32_t)(rs2)) << 20) | \
     (((uint32_t)(rs1)) << 15) | (((uint32_t)0x0) << 12) | \
     (((uint32_t)0x2) << 10) | (((uint32_t)(md)) << 7) | MAT_OPCODE)

#define ENCODE_MST_W(ms3, rs1, rs2) \
    ((((uint32_t)0x06) << 25) | (((uint32_t)(rs2)) << 20) | \
     (((uint32_t)(rs1)) << 15) | (((uint32_t)0x0) << 12) | \
     (((uint32_t)0x2) << 10) | (((uint32_t)(ms3)) << 7) | MAT_OPCODE)

#define ENCODE_MMASA_W(md, ms1, ms2) \
    ((((uint32_t)0x1e) << 27) | (((uint32_t)(ms2)) << 21) | \
     (((uint32_t)(ms1)) << 18) | (((uint32_t)(md)) << 15) | \
     (((uint32_t)0x10) << 7) | MAT_OPCODE)

#define ENCODE_MZERO(md) \
    ((((uint32_t)0x1f) << 27) | (((uint32_t)(md)) << 15) | MAT_OPCODE)

static inline void
matrix_load_w_m0(const int32_t *base, uintptr_t stride)
{
    register const int32_t *base_reg asm("a0") = base;
    register uintptr_t stride_reg asm("a1") = stride;
    asm volatile(".word %0" : : "i"(ENCODE_MLD_W(0, 10, 11)),
                 "r"(base_reg), "r"(stride_reg) : "memory");
}

static inline void
matrix_load_w_m1(const int32_t *base, uintptr_t stride)
{
    register const int32_t *base_reg asm("a0") = base;
    register uintptr_t stride_reg asm("a1") = stride;
    asm volatile(".word %0" : : "i"(ENCODE_MLD_W(1, 10, 11)),
                 "r"(base_reg), "r"(stride_reg) : "memory");
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
matrix_mmasa_w_m2_m1_m0(void)
{
    asm volatile(".word %0" : : "i"(ENCODE_MMASA_W(2, 1, 0)) : "memory");
}

static int32_t a[SIZE][SIZE] __attribute__((aligned(16))) = {
    {1, 2, 3, 4, 5, 6, 7, 8},
    {2, 3, 4, 5, 6, 7, 8, 9},
    {3, 4, 5, 6, 7, 8, 9, 10},
    {4, 5, 6, 7, 8, 9, 10, 11},
    {5, 6, 7, 8, 9, 10, 11, 12},
    {6, 7, 8, 9, 10, 11, 12, 13},
    {7, 8, 9, 10, 11, 12, 13, 14},
    {8, 9, 10, 11, 12, 13, 14, 15},
};

static int32_t b_t[SIZE][SIZE] __attribute__((aligned(16))) = {
    {1, 2, 3, 4, 5, 6, 7, 8},
    {0, 1, 2, 3, 4, 5, 6, 7},
    {2, 3, 4, 5, 6, 7, 8, 9},
    {1, 0, 1, 0, 1, 0, 1, 0},
    {3, 2, 1, 0, 1, 2, 3, 4},
    {4, 3, 2, 1, 0, 1, 2, 3},
    {5, 4, 3, 2, 1, 0, 1, 2},
    {6, 5, 4, 3, 2, 1, 0, 1},
};

static int32_t c[SIZE][SIZE] __attribute__((aligned(16)));

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

static void
matmul_quad(void)
{
    for (int m = 0; m < SIZE; m += TILE) {
        for (int n = 0; n < SIZE; n += TILE) {
            matrix_zero_m2();
            for (int k = 0; k < SIZE; k += TILE) {
                matrix_load_w_m1(&b_t[n][k], SIZE * sizeof(int32_t));
                matrix_load_w_m0(&a[m][k], SIZE * sizeof(int32_t));
                matrix_mmasa_w_m2_m1_m0();
            }
            matrix_store_w_m2(&c[m][n], SIZE * sizeof(int32_t));
        }
    }
}

static int
check_results(void)
{
    for (int m = 0; m < SIZE; ++m) {
        for (int n = 0; n < SIZE; ++n) {
            int32_t ref = 0;
            for (int k = 0; k < SIZE; ++k) {
                ref += a[m][k] * b_t[n][k];
            }
            if (c[m][n] != ref) {
                return 1 + m * SIZE + n;
            }
        }
    }
    return 0;
}

int
main(void)
{
    matmul_quad();
    return check_results();
}
