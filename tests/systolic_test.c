/*
 * Systolic Array Accelerator Test Program
 * 
 * This program performs GEMM (C = A × B) using the systolic array accelerator
 * via MMIO register interface.
 * 
 * MMIO Register Map (base: 0x50000000):
 *   0x00: regMatA    - Base address of matrix A
 *   0x08: regMatB    - Base address of matrix B
 *   0x10: regMatC    - Base address of matrix C (output)
 *   0x18: regDimM    - Dimension M (rows of A, rows of C)
 *   0x20: regDimK    - Dimension K (cols of A, rows of B)
 *   0x28: regDimN    - Dimension N (cols of B, cols of C)
 *   0x30: regCmd     - Command register (bit 0: start)
 *   0x38: regStatus  - Status register (bit 0: busy)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// MMIO base address for systolic accelerator
#define SYSTOLIC_BASE 0x50000000ULL

// MMIO register offsets
#define REG_MAT_A       0x00
#define REG_MAT_B       0x08
#define REG_MAT_C       0x10
#define REG_DIM_M       0x18
#define REG_DIM_K       0x20
#define REG_DIM_N       0x28
#define REG_CMD         0x30
#define REG_STATUS      0x38

// Helper functions for MMIO access
static inline void mmio_write(uint64_t offset, uint64_t value) {
    volatile uint64_t *ptr = (volatile uint64_t *)(SYSTOLIC_BASE + offset);
    *ptr = value;
}

static inline uint64_t mmio_read(uint64_t offset) {
    volatile uint64_t *ptr = (volatile uint64_t *)(SYSTOLIC_BASE + offset);
    return *ptr;
}

// Matrix initialization and verification
void init_matrix_a(uint64_t *A, int M, int K) {
    for (int i = 0; i < M; i++) {
        for (int k = 0; k < K; k++) {
            // Simple pattern: A[i][k] = i + k
            A[i * K + k] = i + k;
        }
    }
}

void init_matrix_b(uint64_t *B, int K, int N) {
    for (int k = 0; k < K; k++) {
        for (int j = 0; j < N; j++) {
            // Simple pattern: B[k][j] = k * j + 1
            B[k * N + j] = k * j + 1;
        }
    }
}

void init_matrix_c(uint64_t *C, int M, int N) {
    // Initialize to zero
    memset(C, 0, M * N * sizeof(uint64_t));
}

void compute_reference(uint64_t *A, uint64_t *B, uint64_t *C_ref, 
                       int M, int K, int N) {
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            uint64_t sum = 0;
            for (int k = 0; k < K; k++) {
                sum += A[i * K + k] * B[k * N + j];
            }
            C_ref[i * N + j] = sum;
        }
    }
}

int verify_result(uint64_t *C, uint64_t *C_ref, int M, int N) {
    int errors = 0;
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            int idx = i * N + j;
            if (C[idx] != C_ref[idx]) {
                printf("MISMATCH at C[%d][%d]: got %lu, expected %lu\n",
                       i, j, C[idx], C_ref[idx]);
                errors++;
                if (errors >= 10) {
                    printf("... (stopping after 10 errors)\n");
                    return errors;
                }
            }
        }
    }
    return errors;
}

void print_matrix(const char *name, uint64_t *M, int rows, int cols) {
    printf("%s:\n", name);
    for (int i = 0; i < rows; i++) {
        printf("  ");
        for (int j = 0; j < cols; j++) {
            printf("%4lu ", M[i * cols + j]);
        }
        printf("\n");
    }
}

void run_gemm_test(int M, int K, int N) {
    printf("\n========================================\n");
    printf("GEMM Test: C[%d×%d] = A[%d×%d] × B[%d×%d]\n", M, N, M, K, K, N);
    printf("========================================\n");
    
    // Allocate matrices
    uint64_t *A = (uint64_t *)malloc(M * K * sizeof(uint64_t));
    uint64_t *B = (uint64_t *)malloc(K * N * sizeof(uint64_t));
    uint64_t *C = (uint64_t *)malloc(M * N * sizeof(uint64_t));
    uint64_t *C_ref = (uint64_t *)malloc(M * N * sizeof(uint64_t));
    
    if (!A || !B || !C || !C_ref) {
        printf("ERROR: Memory allocation failed\n");
        return;
    }
    
    // Initialize matrices
    printf("Initializing matrices...\n");
    init_matrix_a(A, M, K);
    init_matrix_b(B, K, N);
    init_matrix_c(C, M, N);
    
    // Print input matrices for small sizes
    if (M <= 8 && K <= 8 && N <= 8) {
        print_matrix("Matrix A", A, M, K);
        print_matrix("Matrix B", B, K, N);
    }
    
    // Configure systolic accelerator
    printf("\nConfiguring systolic accelerator...\n");
    printf("  Matrix A address: 0x%lx\n", (uint64_t)A);
    printf("  Matrix B address: 0x%lx\n", (uint64_t)B);
    printf("  Matrix C address: 0x%lx\n", (uint64_t)C);
    
    mmio_write(REG_MAT_A, (uint64_t)A);
    mmio_write(REG_MAT_B, (uint64_t)B);
    mmio_write(REG_MAT_C, (uint64_t)C);
    mmio_write(REG_DIM_M, M);
    mmio_write(REG_DIM_K, K);
    mmio_write(REG_DIM_N, N);
    
    // Start computation
    printf("Starting GEMM operation...\n");
    mmio_write(REG_CMD, 0x1);  // Set start bit
    
    // Wait for completion (poll status register)
    printf("Waiting for accelerator...\n");
    int iterations = 0;
    while (mmio_read(REG_STATUS) & 0x1) {
        iterations++;
        // Add a small delay to avoid hammering MMIO
        for (volatile int i = 0; i < 100; i++);
    }
    printf("Accelerator finished (polled %d times)\n", iterations);
    
    // Compute reference result on CPU
    printf("Computing reference result on CPU...\n");
    compute_reference(A, B, C_ref, M, K, N);
    
    // Verify result
    printf("Verifying result...\n");
    int errors = verify_result(C, C_ref, M, N);
    
    if (errors == 0) {
        printf("✓ PASS: All results match!\n");
    } else {
        printf("✗ FAIL: %d mismatches found\n", errors);
    }
    
    // Print result for small matrices
    if (M <= 8 && N <= 8) {
        print_matrix("Result C (accelerator)", C, M, N);
        print_matrix("Expected C (CPU)", C_ref, M, N);
    }
    
    // Cleanup
    free(A);
    free(B);
    free(C);
    free(C_ref);
}

int main() {
    printf("======================================\n");
    printf("Systolic Array Accelerator Test\n");
    printf("======================================\n");
    printf("MMIO Base: 0x%llx\n", SYSTOLIC_BASE);
    
    // Test 1: Small matrix that fits in one tile (4×4 PE array)
    run_gemm_test(4, 4, 4);
    
    // Test 2: Small non-square matrix
    run_gemm_test(4, 8, 4);
    
    // Test 3: Matrix requiring tiling (larger than PE array)
    run_gemm_test(8, 8, 8);
    
    // Test 4: Rectangular matrix with tiling
    run_gemm_test(12, 16, 8);
    
    printf("\n======================================\n");
    printf("All tests complete!\n");
    printf("======================================\n");
    
    return 0;
}
