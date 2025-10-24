// RVV (RISC-V Vector Extension) array multiplication test
// Uses true SIMD via vector instructions instead of accelerator

#include <stdint.h>
#include <stddef.h>

// Simple print functions for gem5 SE mode
static void print_str(const char *s) {
    while (*s) {
        asm volatile("li a7, 64\n"       // sys_write
                     "li a0, 1\n"        // fd = stdout
                     "mv a1, %0\n"       // buffer
                     "li a2, 1\n"        // count = 1
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
    // Print in reverse
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

// Test arrays - 8 elements
#define N 8
static uint64_t A[N] = {1, 2, 3, 4, 5, 6, 7, 8};
static uint64_t B[N] = {2, 2, 2, 2, 2, 2, 2, 2};
static uint64_t C[N];

// Vector multiply using RVV intrinsics
void vector_multiply_rvv(uint64_t *c, uint64_t *a, uint64_t *b, size_t n) {
    print_str("\n=== Using RISC-V Vector Extension ===\n");
    
    // RVV assembly for element-wise multiply
    // Process multiple elements at once (SIMD!)
    size_t vl;
    
    for (size_t i = 0; i < n; ) {
        // Set vector length for 64-bit elements
        asm volatile("vsetvli %0, %1, e64, m1, ta, ma"
                     : "=r"(vl) : "r"(n - i));
        
        print_str("Processing ");
        print_num(vl);
        print_str(" elements in parallel (SIMD!)\n");
        
        // Load vectors
        asm volatile("vle64.v v0, (%0)" : : "r"(&a[i]));
        asm volatile("vle64.v v1, (%0)" : : "r"(&b[i]));
        
        // Vector multiply: v2 = v0 * v1 (multiple elements simultaneously!)
        asm volatile("vmul.vv v2, v0, v1");
        
        // Store result
        asm volatile("vse64.v v2, (%0)" : : "r"(&c[i]));
        
        i += vl;
    }
}

// Scalar multiply (traditional, one at a time)
void scalar_multiply(uint64_t *c, uint64_t *a, uint64_t *b, size_t n) {
    print_str("\n=== Using Scalar (non-SIMD) ===\n");
    for (size_t i = 0; i < n; i++) {
        c[i] = a[i] * b[i];
        print_str("Processing element ");
        print_num(i);
        print_str(" (one at a time)\n");
    }
}

int main(void) {
    print_str("\n========================================\n");
    print_str("RISC-V Vector Extension (RVV) Test\n");
    print_str("========================================\n");
    
    print_str("\nInput arrays:\n");
    print_str("A = [1, 2, 3, 4, 5, 6, 7, 8]\n");
    print_str("B = [2, 2, 2, 2, 2, 2, 2, 2]\n");
    
    // Test 1: Vector multiply (SIMD!)
    print_str("\n--- TEST 1: Vector Multiply ---\n");
    vector_multiply_rvv(C, A, B, N);
    
    print_str("\nResults:\n");
    for (int i = 0; i < N; i++) {
        print_str("C[");
        print_num(i);
        print_str("] = ");
        print_num(C[i]);
        print_str(" (expected ");
        print_num(A[i] * 2);
        print_str(")\n");
    }
    
    // Verify
    int ok = 1;
    for (int i = 0; i < N; i++) {
        if (C[i] != A[i] * B[i]) {
            ok = 0;
            break;
        }
    }
    
    if (ok) {
        print_str("\n*** VECTOR TEST PASSED ***\n");
    } else {
        print_str("\n*** VECTOR TEST FAILED ***\n");
    }
    
    // Test 2: Compare with scalar
    print_str("\n--- TEST 2: Scalar Multiply (for comparison) ---\n");
    uint64_t C_scalar[N];
    scalar_multiply(C_scalar, A, B, N);
    
    print_str("\n========================================\n");
    print_str("Summary:\n");
    print_str("- Vector: Processes multiple elements simultaneously (SIMD)\n");
    print_str("- Scalar: Processes one element at a time\n");
    print_str("- Both should give same results!\n");
    print_str("========================================\n\n");
    
    return ok ? 0 : 1;
}
