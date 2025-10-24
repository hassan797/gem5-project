// RISC-V Vector Extension (RVV) GEMM (Matrix Multiplication)
// C[M×N] = A[M×K] × B[K×N] using SIMD

#include <stdint.h>
#include <stddef.h>

// Simple print functions
static void print_str(const char *s) {
    while (*s) {
        asm volatile("li a7, 64\n"
                     "li a0, 1\n"
                     "mv a1, %0\n"
                     "li a2, 1\n"
                     "ecall\n"
                     : : "r"(s) : "a0", "a1", "a2", "a7");
        s++;
    }
}

static void print_num(uint64_t n) {
    char buf[32];
    int i = 0;
    if (n == 0) {
        print_str("0");
        return;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        char c = buf[--i];
        asm volatile("li a7, 64\n"
                     "li a0, 1\n"
                     "mv a1, %0\n"
                     "li a2, 1\n"
                     "ecall\n"
                     : : "r"(&c) : "a0", "a1", "a2", "a7");
    }
}

// Matrix dimensions
#define M 4
#define K 4
#define N 4

// Matrices
static uint64_t A[M][K];
static uint64_t B[K][N];
static uint64_t C[M][N];
static uint64_t C_scalar[M][N];

// Scalar GEMM (traditional, for comparison)
void gemm_scalar(uint64_t c[M][N], uint64_t a[M][K], uint64_t b[K][N]) {
    print_str("\n=== Scalar GEMM (traditional) ===\n");
    
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            c[i][j] = 0;
            for (int k = 0; k < K; k++) {
                c[i][j] += a[i][k] * b[k][j];
                
                print_str("C[");
                print_num(i);
                print_str("][");
                print_num(j);
                print_str("] += A[");
                print_num(i);
                print_str("][");
                print_num(k);
                print_str("] * B[");
                print_num(k);
                print_str("][");
                print_num(j);
                print_str("]\n");
            }
        }
    }
}

// RVV GEMM (vectorized innermost loop)
void gemm_rvv(uint64_t c[M][N], uint64_t a[M][K], uint64_t b[K][N]) {
    print_str("\n=== RVV GEMM (SIMD) ===\n");
    
    // For each output element C[i][j]
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            c[i][j] = 0;
            
            print_str("Computing C[");
            print_num(i);
            print_str("][");
            print_num(j);
            print_str("] with SIMD dot product\n");
            
            // Vectorize the k-loop (dot product)
            // C[i][j] = sum(A[i][k] * B[k][j]) for k=0..K-1
            
            size_t vl;
            uint64_t sum = 0;
            
            for (size_t k = 0; k < K; ) {
                // Set vector length
                asm volatile("vsetvli %0, %1, e64, m1, ta, ma"
                             : "=r"(vl) : "r"(K - k));
                
                print_str("  Processing ");
                print_num(vl);
                print_str(" elements in parallel (k=");
                print_num(k);
                print_str(" to ");
                print_num(k + vl - 1);
                print_str(")\n");
                
                // Load A[i][k:k+vl-1] into v0
                asm volatile("vle64.v v0, (%0)" : : "r"(&a[i][k]));
                
                // For B, we need B[k:k+vl-1][j]
                // This is non-contiguous in memory (column access)
                // We'll use strided load: stride = N * 8 bytes
                uint64_t stride = N * 8;
                asm volatile("vlse64.v v1, (%0), %1" 
                             : : "r"(&b[k][j]), "r"(stride));
                
                // Vector multiply: v2 = v0 * v1
                asm volatile("vmul.vv v2, v0, v1");
                
                // Reduce: sum all elements of v2
                // Use vredsum to accumulate
                // First, zero out v3
                asm volatile("vmv.v.i v3, 0");
                
                // Reduce sum: v3 = sum(v2)
                asm volatile("vredsum.vs v3, v2, v3");
                
                // Extract scalar from v3[0]
                uint64_t partial_sum;
                asm volatile("vmv.x.s %0, v3" : "=r"(partial_sum));
                
                sum += partial_sum;
                k += vl;
            }
            
            c[i][j] = sum;
        }
    }
}

int main(void) {
    print_str("\n========================================\n");
    print_str("RVV GEMM Test: C = A × B\n");
    print_str("Matrix dimensions: 4×4\n");
    print_str("========================================\n");
    
    // Initialize A
    print_str("\nMatrix A (4×4):\n");
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < K; j++) {
            A[i][j] = i * K + j + 1;
        }
    }
    for (int i = 0; i < M; i++) {
        print_str("  [");
        for (int j = 0; j < K; j++) {
            print_num(A[i][j]);
            if (j < K-1) print_str(", ");
        }
        print_str("]\n");
    }
    
    // Initialize B (identity matrix)
    print_str("\nMatrix B (4×4 identity):\n");
    for (int i = 0; i < K; i++) {
        for (int j = 0; j < N; j++) {
            B[i][j] = (i == j) ? 1 : 0;
        }
    }
    for (int i = 0; i < K; i++) {
        print_str("  [");
        for (int j = 0; j < N; j++) {
            print_num(B[i][j]);
            if (j < N-1) print_str(", ");
        }
        print_str("]\n");
    }
    
    // Test 1: Scalar GEMM
    print_str("\n--- TEST 1: Scalar GEMM ---\n");
    gemm_scalar(C_scalar, A, B);
    
    print_str("\nScalar Result C:\n");
    for (int i = 0; i < M; i++) {
        print_str("  [");
        for (int j = 0; j < N; j++) {
            print_num(C_scalar[i][j]);
            if (j < N-1) print_str(", ");
        }
        print_str("]\n");
    }
    
    // Test 2: RVV GEMM
    print_str("\n--- TEST 2: RVV GEMM ---\n");
    gemm_rvv(C, A, B);
    
    print_str("\nRVV Result C:\n");
    for (int i = 0; i < M; i++) {
        print_str("  [");
        for (int j = 0; j < N; j++) {
            print_num(C[i][j]);
            if (j < N-1) print_str(", ");
        }
        print_str("]\n");
    }
    
    // Verify
    print_str("\n--- VERIFICATION ---\n");
    int ok = 1;
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            uint64_t expected = A[i][j];  // Since B is identity
            if (C[i][j] != expected) {
                print_str("ERROR: C[");
                print_num(i);
                print_str("][");
                print_num(j);
                print_str("] = ");
                print_num(C[i][j]);
                print_str(", expected ");
                print_num(expected);
                print_str("\n");
                ok = 0;
            }
            if (C_scalar[i][j] != expected) {
                print_str("ERROR: C_scalar[");
                print_num(i);
                print_str("][");
                print_num(j);
                print_str("] = ");
                print_num(C_scalar[i][j]);
                print_str(", expected ");
                print_num(expected);
                print_str("\n");
                ok = 0;
            }
        }
    }
    
    if (ok) {
        print_str("\n*** ALL TESTS PASSED ***\n");
        print_str("Both scalar and RVV produce correct results!\n");
    } else {
        print_str("\n*** TESTS FAILED ***\n");
    }
    
    print_str("\n========================================\n");
    print_str("Summary:\n");
    print_str("- Scalar: Processes one multiply-add at a time\n");
    print_str("- RVV: Vectorizes dot product (4 multiply-adds in parallel)\n");
    print_str("- Expected speedup: ~4x for the k-loop\n");
    print_str("========================================\n\n");
    
    return ok ? 0 : 1;
}
