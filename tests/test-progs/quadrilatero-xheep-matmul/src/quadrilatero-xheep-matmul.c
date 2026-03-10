#include <stdint.h>

#define MAT_OPCODE 0x2bU
#define TYPE_INT32 0
#define SIMD_FACTOR 1
#define SIMD_SHIFT 2
#define SIZE 8

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

#define HEAD_LINE_MMASA_W ENCODE_MMASA_W(2, 1, 0)

typedef int32_t data_in_t;
typedef int32_t data_out_t;

static inline void
matrix_load_w(unsigned md, const data_in_t *base, uintptr_t stride)
{
    register const data_in_t *base_reg asm("a0") = base;
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
    }
}

static inline void
matrix_store_w_m2(data_out_t *base, uintptr_t stride)
{
    register data_out_t *base_reg asm("a0") = base;
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
    asm volatile(".word %0" : : "i"(HEAD_LINE_MMASA_W) : "memory");
}

static data_in_t matrix_a[SIZE][SIZE] __attribute__((aligned(16))) = {
    {1, 2, 3, 4, 5, 6, 7, 8},
    {2, 3, 4, 5, 6, 7, 8, 9},
    {3, 4, 5, 6, 7, 8, 9, 10},
    {4, 5, 6, 7, 8, 9, 10, 11},
    {5, 6, 7, 8, 9, 10, 11, 12},
    {6, 7, 8, 9, 10, 11, 12, 13},
    {7, 8, 9, 10, 11, 12, 13, 14},
    {8, 9, 10, 11, 12, 13, 14, 15},
};

static data_in_t matrix_bt[SIZE][SIZE] __attribute__((aligned(16))) = {
    {1, 2, 3, 4, 5, 6, 7, 8},
    {0, 1, 2, 3, 4, 5, 6, 7},
    {2, 3, 4, 5, 6, 7, 8, 9},
    {1, 0, 1, 0, 1, 0, 1, 0},
    {3, 2, 1, 0, 1, 2, 3, 4},
    {4, 3, 2, 1, 0, 1, 2, 3},
    {5, 4, 3, 2, 1, 0, 1, 2},
    {6, 5, 4, 3, 2, 1, 0, 1},
};

static data_out_t matrix_c[SIZE][SIZE] __attribute__((aligned(16)));

static void __attribute__((noinline))
matrixMul_4x4(data_in_t *addrA, data_in_t *addrB, data_out_t *addrC,
              int K, int N, int M, int shift)
{
    for (int m0 = 0; m0 < M; m0 += 4) {
        const uintptr_t c_row = (uintptr_t)(N * (int)sizeof(data_out_t) * m0);
        const uintptr_t a_row = (uintptr_t)(K * (int)sizeof(data_in_t) * m0);

        for (int n0 = 0; n0 < N; n0 += 4) {
            data_out_t *startAddrC =
                (data_out_t *)((uintptr_t)addrC + c_row +
                               (uintptr_t)(n0 * (int)sizeof(data_out_t)));

            matrix_zero_m2();

            for (int k0 = 0; k0 < K; k0 += 4) {
                const uintptr_t k_bytes =
                    (uintptr_t)(k0 * (int)sizeof(data_in_t));
                data_in_t *startAddrB =
                    (data_in_t *)((uintptr_t)addrB + k_bytes +
                                  (uintptr_t)(K * (int)sizeof(data_in_t) * n0));
                data_in_t *startAddrA =
                    (data_in_t *)((uintptr_t)addrA + a_row + k_bytes);

                matrix_load_w(1, startAddrB, (uintptr_t)(N << shift));
                matrix_load_w(0, startAddrA,
                              (uintptr_t)(K * (int)sizeof(data_in_t)));
                matrix_mmasa_w_m2_m1_m0();
            }

            matrix_store_w_m2(startAddrC,
                              (uintptr_t)(N * (int)sizeof(data_out_t)));
        }
    }
}

static int
check_results(int K, int N, int M)
{
    for (int m = 0; m < M; ++m) {
        for (int n = 0; n < N; ++n) {
            int32_t ref = 0;
            for (int k = 0; k < K; ++k) {
                ref += matrix_a[m][k] * matrix_bt[n][k];
            }
            if (matrix_c[m][n] != ref) {
                return 1 + m * N + n;
            }
        }
    }

    return 0;
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
    const int k_size = SIZE / SIMD_FACTOR;
    const int n_size = SIZE;
    const int m_size = SIZE;

    matrixMul_4x4(&matrix_a[0][0], &matrix_bt[0][0], &matrix_c[0][0],
                  k_size, n_size, m_size, SIMD_SHIFT);

    return check_results(k_size, n_size, m_size);
}
