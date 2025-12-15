#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

// MMIO base address for Systolic Array accelerator
#define SYSTOLIC_MMIO_BASE 0x50000000UL

// MMIO register offsets (8-byte registers)
#define SYSTOLIC_MATA_OFFSET   0x00   // Matrix A address
#define SYSTOLIC_MATB_OFFSET   0x08   // Matrix B address
#define SYSTOLIC_MATC_OFFSET   0x10   // Matrix C address
#define SYSTOLIC_DIMM_OFFSET   0x18   // M dimension
#define SYSTOLIC_DIMK_OFFSET   0x20   // K dimension
#define SYSTOLIC_DIMN_OFFSET   0x28   // N dimension
#define SYSTOLIC_CMD_OFFSET    0x30   // Command register (bit 0 = start)
#define SYSTOLIC_STATUS_OFFSET 0x38   // Status register (bit 0 = busy)

// Helper to write to MMIO registers
static inline void mmio_write(uintptr_t offset, uint64_t value) {
    volatile uint64_t* addr = (volatile uint64_t*)(SYSTOLIC_MMIO_BASE + offset);
    *addr = value;
    asm volatile("" ::: "memory");
}

static inline uint64_t mmio_read(uintptr_t offset) {
    volatile uint64_t* addr = (volatile uint64_t*)(SYSTOLIC_MMIO_BASE + offset);
    asm volatile("" ::: "memory");
    return *addr;
}

// Systolic Accelerator MMIO Interface
static inline void systolic_mata(uintptr_t a) {
    mmio_write(SYSTOLIC_MATA_OFFSET, a);
}

static inline void systolic_matb(uintptr_t b) {
    mmio_write(SYSTOLIC_MATB_OFFSET, b);
}

static inline void systolic_matc(uintptr_t c) {
    mmio_write(SYSTOLIC_MATC_OFFSET, c);
}

static inline void systolic_dimm(uint64_t m) {
    mmio_write(SYSTOLIC_DIMM_OFFSET, m);
}

static inline void systolic_dimk(uint64_t k) {
    mmio_write(SYSTOLIC_DIMK_OFFSET, k);
}

static inline void systolic_dimn(uint64_t n) {
    mmio_write(SYSTOLIC_DIMN_OFFSET, n);
}

static inline void systolic_kick() {
    mmio_write(SYSTOLIC_CMD_OFFSET, 1);
}

static inline void systolic_wait() {
    while (mmio_read(SYSTOLIC_STATUS_OFFSET) & 0x1) {
        asm volatile("nop" ::: "memory");
    }
}

int main(int argc, char* argv[]) {
    // Get number of runs from command line (default: 100)
    int num_runs = 1;  // Single run for testing (change to 100 for benchmarking)
    if (argc > 1) {
        num_runs = atoi(argv[1]);
        if (num_runs <= 0) num_runs = 1;
    }
    
    printf("========================================\n");
    printf("FC Layer GEMM Test (Systolic Accelerator)\n");
    printf("========================================\n\n");
    printf("Number of inference runs: %d\n\n", num_runs);
    
    // FC layer GEMM: C[10x1] = W[10x8192] * X[8192x1]
    // Matrix A: weights W[10x8192] (M=10, K=8192)
    // Matrix B: input X[8192x1]    (K=8192, N=1)
    // Matrix C: output Y[10x1]     (M=10, N=1)
    const int FC_M = 10;       // Output dimension
    const int FC_K = 8192;     // Input features (16x16x32 from pooling)
    const int FC_N = 1;        // Batch size
    
    printf("GEMM dimensions: C[%dx%d] = A[%dx%d] * B[%dx%d]\n", 
           FC_M, FC_N, FC_M, FC_K, FC_K, FC_N);
    printf("Systolic Array PE configuration will be set via command line\n");
    printf("  With 4x4:  M=3 tiles, N=1 tile, K=2048 tiles = 6144 ops\n");
    printf("  With 8x8:  M=2 tiles, N=1 tile, K=1024 tiles = 2048 ops\n");
    printf("  With 16x16: M=1 tile, N=1 tile, K=512 tiles = 512 ops\n\n");
    
    // Allocate matrices as uint64_t arrays (matching gem5 element size)
    uint64_t* fc_weights = new uint64_t[FC_M * FC_K];  // 10 x 8192 = 81,920 elements
    uint64_t* fc_input = new uint64_t[FC_K * FC_N];    // 8192 x 1 = 8,192 elements
    uint64_t* fc_output = new uint64_t[FC_M * FC_N];   // 10 x 1 = 10 elements
    
    printf("Initializing data...\n");
    
    // Initialize input (all ones)
    for (int i = 0; i < FC_K; i++) {
        fc_input[i] = 1;
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
    printf("  Weights address: %p (%lu elements, %lu bytes)\n", 
           fc_weights, FC_M * FC_K, FC_M * FC_K * sizeof(uint64_t));
    printf("  Input address: %p (%lu elements, %lu bytes)\n", 
           fc_input, FC_K * FC_N, FC_K * FC_N * sizeof(uint64_t));
    printf("  Output address: %p (%lu elements, %lu bytes)\n", 
           fc_output, FC_M * FC_N, FC_M * FC_N * sizeof(uint64_t));
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
        
        // Configure systolic accelerator for GEMM operation
        systolic_mata((uintptr_t)fc_weights);  // Matrix A (weights)
        systolic_matb((uintptr_t)fc_input);    // Matrix B (input)
        systolic_matc((uintptr_t)fc_output);   // Matrix C (output)
        systolic_dimm(FC_M);                   // M dimension
        systolic_dimk(FC_K);                   // K dimension
        systolic_dimn(FC_N);                   // N dimension
        
        // Launch and wait for GEMM
        systolic_kick();
        systolic_wait();
    }
    
    printf("\n========================================\n");
    printf("All %d runs completed!\n", num_runs);
    printf("========================================\n\n");
    
    // Display results
    printf("FC Output (first 10 elements):\n");
    for (int i = 0; i < 10 && i < FC_M; i++) {
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
