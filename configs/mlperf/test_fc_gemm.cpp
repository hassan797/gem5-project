/*
 * Minimal test for SIMD GEMM integration in MLPerf FC layer
 * Tests: C[10x1] = W[10x8192] * X[8192x1]
 * Expected: K=8192 should be batched in groups of 4
 */

#include <cstdio>
#include <cstdint>

// SIMD Accelerator Custom Instructions
static inline void xcop_optype(uint64_t op) {
    asm volatile(".insn r 0x0B,0x5,0x01, x0,%0,x0" :: "r"(op) : "memory");
}

static inline void xcop_dimk(uint64_t k) {
    asm volatile(".insn r 0x0B,0x6,0x01, x0,%0,x0" :: "r"(k) : "memory");
}

static inline void xcop_dimn(uint64_t n) {
    asm volatile(".insn r 0x0B,0x7,0x01, x0,%0,x0" :: "r"(n) : "memory");
}

static inline void xcop_srca(uint64_t a) {
    asm volatile(".insn r 0x0B,0x0,0x01, x0,%0,x0" :: "r"(a) : "memory");
}

static inline void xcop_srcb(uint64_t b) {
    asm volatile(".insn r 0x0B,0x1,0x01, x0,%0,x0" :: "r"(b) : "memory");
}

static inline void xcop_dst(uint64_t d) {
    asm volatile(".insn r 0x0B,0x2,0x01, x0,%0,x0" :: "r"(d) : "memory");
}

static inline void xcop_len(uint64_t m) {
    asm volatile(".insn r 0x0B,0x3,0x01, x0,%0,x0" :: "r"(m) : "memory");
}

static inline void xcop_kick(void) {
    asm volatile("fence iorw, iorw\n\t"
                 ".insn r 0x0B,0x4,0x01, x0,x0,x0\n\t"
                 : : : "memory");
}

static inline void xcop_wait(void) {
    asm volatile("fence iorw, iorw\n\t" : : : "memory");
}

int main() {
    printf("========================================\n");
    printf("MLPerf FC Layer GEMM Test\n");
    printf("========================================\n\n");
    
    // FC layer dimensions: C[10x1] = W[10x8192] * X[8192x1]
    const int FC_M = 10;                  // 10 outputs
    const int FC_K = 16 * 16 * 32;        // 8192 inputs (from pooling layer)
    const int FC_N = 1;                   // batch size 1
    
    printf("FC Layer Configuration:\n");
    printf("  Matrix multiplication: C[%dx%d] = W[%dx%d] * X[%dx%d]\n",
           FC_M, FC_N, FC_M, FC_K, FC_K, FC_N);
    printf("  Expected batching: K=%d processed in batches of 4\n", FC_K);
    printf("  Expected batches: %d batches per output element\n\n", (FC_K + 3) / 4);
    
    // Allocate matrices
    uint64_t* fc_weights = new uint64_t[FC_M * FC_K];
    uint64_t* fc_input = new uint64_t[FC_K * FC_N];
    uint64_t* fc_output = new uint64_t[FC_M * FC_N];
    
    // Initialize with test pattern
    printf("Initializing matrices...\n");
    for (int i = 0; i < FC_K; i++) {
        fc_input[i] = 1;  // All inputs = 1
    }
    
    for (int i = 0; i < FC_M * FC_K; i++) {
        fc_weights[i] = 1;  // All weights = 1
    }
    
    for (int i = 0; i < FC_M; i++) {
        fc_output[i] = 0;  // Clear output
    }
    
    printf("  Weights: %d elements\n", FC_M * FC_K);
    printf("  Input: %d elements\n", FC_K);
    printf("  Output: %d elements\n\n", FC_M);
    
    // Configure SIMD accelerator for GEMM
    printf("Configuring SIMD GEMM accelerator...\n");
    xcop_optype(1);                      // opType = 1 (GEMM)
    xcop_len(FC_M);                      // M dimension
    xcop_dimk(FC_K);                     // K dimension
    xcop_dimn(FC_N);                     // N dimension
    xcop_srca((uint64_t)fc_weights);     // A matrix
    xcop_srcb((uint64_t)fc_input);       // B matrix
    xcop_dst((uint64_t)fc_output);       // C matrix
    
    printf("  opType: 1 (GEMM)\n");
    printf("  M: %d (rows of output)\n", FC_M);
    printf("  K: %d (dot product length)\n", FC_K);
    printf("  N: %d (columns of output)\n", FC_N);
    printf("  A (weights): 0x%lx\n", (uint64_t)fc_weights);
    printf("  B (input): 0x%lx\n", (uint64_t)fc_input);
    printf("  C (output): 0x%lx\n\n", (uint64_t)fc_output);
    
    printf("Starting GEMM operation...\n");
    printf("(Watch for 4-way batching in debug output)\n\n");
    
    xcop_kick();                         // Start GEMM
    xcop_wait();                         // Wait for completion
    
    printf("\nGEMM operation complete!\n\n");
    
    // Verify results
    printf("Verifying results:\n");
    bool all_correct = true;
    for (int i = 0; i < FC_M; i++) {
        uint64_t expected = FC_K;  // Sum of K ones
        if (fc_output[i] == expected) {
            printf("  C[%d] = %lu ✓ (expected %lu)\n", i, fc_output[i], expected);
        } else {
            printf("  C[%d] = %lu ✗ (expected %lu)\n", i, fc_output[i], expected);
            all_correct = false;
        }
    }
    
    printf("\n========================================\n");
    if (all_correct) {
        printf("TEST PASSED!\n");
        printf("All %d outputs computed correctly.\n", FC_M);
    } else {
        printf("TEST FAILED!\n");
        printf("Some outputs were incorrect.\n");
    }
    printf("========================================\n");
    
    // Cleanup
    delete[] fc_weights;
    delete[] fc_input;
    delete[] fc_output;
    
    return all_correct ? 0 : 1;
}
