#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

// MMIO base address for SIMD accelerator
#define SIMD_MMIO_BASE 0x40000000UL

// MMIO register offsets
#define SIMD_SRCA_OFFSET   0x00
#define SIMD_SRCB_OFFSET   0x08
#define SIMD_DST_OFFSET    0x10
#define SIMD_LEN_OFFSET    0x18
#define SIMD_KICK_OFFSET   0x20
#define SIMD_STATUS_OFFSET 0x28
#define SIMD_OPTYPE_OFFSET 0x30
#define SIMD_DIMK_OFFSET   0x38
#define SIMD_DIMN_OFFSET   0x40

// Helper to write to MMIO registers
static inline void mmio_write(uintptr_t offset, uint64_t value) {
    volatile uint64_t* addr = (volatile uint64_t*)(SIMD_MMIO_BASE + offset);
    *addr = value;
    asm volatile("fence" ::: "memory");
}

static inline uint64_t mmio_read(uintptr_t offset) {
    volatile uint64_t* addr = (volatile uint64_t*)(SIMD_MMIO_BASE + offset);
    asm volatile("fence" ::: "memory");
    return *addr;
}

// SIMD Custom Instructions using MMIO
static inline void xcop_optype(uint32_t op) {
    mmio_write(SIMD_OPTYPE_OFFSET, op);
}

static inline void xcop_len(uint32_t len) {
    mmio_write(SIMD_LEN_OFFSET, len);
}

static inline void xcop_dimk(uint32_t dimk) {
    mmio_write(SIMD_DIMK_OFFSET, dimk);
}

static inline void xcop_dimn(uint32_t dimn) {
    mmio_write(SIMD_DIMN_OFFSET, dimn);
}

static inline void xcop_srca(uintptr_t a) {
    mmio_write(SIMD_SRCA_OFFSET, a);
}

static inline void xcop_srcb(uintptr_t b) {
    mmio_write(SIMD_SRCB_OFFSET, b);
}

static inline void xcop_dst(uintptr_t d) {
    mmio_write(SIMD_DST_OFFSET, d);
}

static inline void xcop_kick() {
    mmio_write(SIMD_KICK_OFFSET, 1);
}

static inline void xcop_wait() {
    while (mmio_read(SIMD_STATUS_OFFSET) != 0) {
        asm volatile("nop" ::: "memory");
    }
}

int main(int argc, char* argv[]) {
    // Get number of runs from command line (default: 100)
    int num_runs = 100;
    if (argc > 1) {
        num_runs = atoi(argv[1]);
        if (num_runs <= 0) num_runs = 100;
    }
    
    printf("========================================\n");
    printf("FC Layer GEMM Test (SIMD Accelerator)\n");
    printf("========================================\n\n");
    printf("Number of inference runs: %d\n\n", num_runs);
    
    // FC layer GEMM: C[10x1] = W[10x8192] * X[8192x1]
    const int FC_M = 10;       // Output classes
    const int FC_K = 8192;     // Input features (16x16x32 from pooling)
    const int FC_N = 1;        // Batch size
    
    printf("GEMM dimensions: C[%dx%d] = W[%dx%d] * X[%dx%d]\n", 
           FC_M, FC_N, FC_M, FC_K, FC_K, FC_N);
    printf("Expected batching: K=%d with 4-way SIMD = %d batches\n\n",
           FC_K, FC_K / 4);
    
    // Allocate matrices as uint64_t arrays
    uint64_t* fc_weights = new uint64_t[FC_M * FC_K];  // 10 x 8192 = 81,920 elements
    uint64_t* fc_input = new uint64_t[FC_K * FC_N];    // 8192 x 1 = 8,192 elements
    uint64_t* fc_output = new uint64_t[FC_M * FC_N];   // 10 x 1 = 10 elements
    
    printf("Initializing data...\n");
    
    // Initialize input (all ones)
    for (int i = 0; i < FC_K; i++) {
        fc_input[i] = 1000;  // Fixed-point representation of 1.0
    }
    
    // Initialize weights (all ones)
    for (int i = 0; i < FC_M * FC_K; i++) {
        fc_weights[i] = 1;
    }
    
    // Initialize output to zero
    for (int i = 0; i < FC_M; i++) {
        fc_output[i] = 0;
    }
    
    printf("Data initialized.\n");
    printf("  Weights address: %p\n", fc_weights);
    printf("  Input address: %p\n", fc_input);
    printf("  Output address: %p\n", fc_output);
    printf("\n========================================\n");
    printf("Starting %d inference runs...\n", num_runs);
    printf("========================================\n\n");
    
    // Run multiple inferences
    for (int run = 0; run < num_runs; run++) {
        if (run % 10 == 0 && run > 0) {
            printf("Completed %d/%d runs...\n", run, num_runs);
        }
        
        // Reset output to zero for each run
        for (int i = 0; i < FC_M; i++) {
            fc_output[i] = 0;
        }
        
        // Configure SIMD accelerator for GEMM operation
        xcop_optype(1);              // opType = 1 (GEMM)
        xcop_len(FC_M);              // M dimension (rows of A/C)
        xcop_dimk(FC_K);             // K dimension (cols of A, rows of B)
        xcop_dimn(FC_N);             // N dimension (cols of B/C)
        xcop_srca((uintptr_t)fc_weights);  // A matrix address
        xcop_srcb((uintptr_t)fc_input);    // B matrix address
        xcop_dst((uintptr_t)fc_output);    // C matrix address
        
        // Launch and wait for GEMM
        xcop_kick();
        xcop_wait();
    }
    
    printf("\n========================================\n");
    printf("All %d runs completed!\n", num_runs);
    printf("========================================\n\n");
    
    // Display results
    printf("FC Output (first 10 elements):\n");
    for (int i = 0; i < 10; i++) {
        printf("  fc_output[%d] = %lu\n", i, fc_output[i]);
    }
    
    // Expected result: each output should be 8192 * 1 = 8192
    // (sum of 8192 inputs, each = 1, times weights of 1)
    printf("\nExpected value per output: %d\n", FC_K);
    printf("Verification: ");
    bool pass = true;
    for (int i = 0; i < FC_M; i++) {
        if (fc_output[i] != FC_K) {
            printf("FAIL (fc_output[%d] = %lu, expected %d)\n", i, fc_output[i], FC_K);
            pass = false;
            break;
        }
    }
    if (pass) {
        printf("PASS - All outputs correct!\n");
    }
    
    // Clean up
    delete[] fc_weights;
    delete[] fc_input;
    delete[] fc_output;
    
    printf("\n========================================\n");
    printf("Test Complete!\n");
    printf("========================================\n");
    
    return 0;
}
