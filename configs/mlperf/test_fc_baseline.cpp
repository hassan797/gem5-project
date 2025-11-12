#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

int main(int argc, char* argv[]) {
    // Get number of runs from command line (default: 10)
    int num_runs = 10;
    if (argc > 1) {
        num_runs = atoi(argv[1]);
        if (num_runs <= 0) num_runs = 10;
    }
    
    printf("========================================\n");
    printf("FC Layer GEMM Test (Baseline - NO SIMD)\n");
    printf("========================================\n\n");
    printf("Number of inference runs: %d\n\n", num_runs);
    
    // FC layer GEMM: C[10x1] = W[10x8192] * X[8192x1]
    const int FC_M = 10;       // Output classes
    const int FC_K = 8192;     // Input features (16x16x32 from pooling)
    const int FC_N = 1;        // Batch size
    
    printf("GEMM dimensions: C[%dx%d] = W[%dx%d] * X[%dx%d]\n", 
           FC_M, FC_N, FC_M, FC_K, FC_K, FC_N);
    printf("Baseline computation: Sequential MACs (NO SIMD batching)\n\n");
    
    // Allocate matrices as uint64_t arrays (same as SIMD version for fair comparison)
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
    
    printf("Data initialized.\n");
    printf("  Weights: %d elements (%.2f KB)\n", FC_M * FC_K, (FC_M * FC_K * 8) / 1024.0);
    printf("  Input: %d elements (%.2f KB)\n", FC_K, (FC_K * 8) / 1024.0);
    printf("  Output: %d elements\n\n", FC_M);
    
    printf("Starting %d inference runs...\n", num_runs);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    // Run multiple inferences
    for (int run = 0; run < num_runs; run++) {
        // Initialize output to zero
        for (int i = 0; i < FC_M; i++) {
            fc_output[i] = 0;
        }
        
        // Baseline GEMM: Sequential computation (no SIMD)
        // C[i] = sum(W[i][k] * X[k]) for all k
        for (int i = 0; i < FC_M; i++) {
            uint64_t sum = 0;
            for (int k = 0; k < FC_K; k++) {
                sum += fc_weights[i * FC_K + k] * fc_input[k];
            }
            fc_output[i] = sum;
        }
        
        if ((run + 1) % 10 == 0 || run == 0) {
            printf("  Completed run %d/%d\n", run + 1, num_runs);
        }
    }
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\n========================================\n");
    printf("All %d runs completed!\n", num_runs);
    printf("========================================\n\n");
    
    // Display results
    printf("FC Output (first 10 elements):\n");
    for (int i = 0; i < 10; i++) {
        printf("  fc_output[%d] = %lu\n", i, fc_output[i]);
    }
    
    // Expected result: each output should be 8192 * 1000 = 8,192,000
    printf("\nExpected value per output: %d\n", FC_K * 1000);
    printf("Verification: ");
    bool pass = true;
    for (int i = 0; i < FC_M; i++) {
        if (fc_output[i] != FC_K * 1000) {
            printf("FAIL (fc_output[%d] = %lu, expected %d)\n", i, fc_output[i], FC_K * 1000);
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
    printf("Baseline Test Complete!\n");
    printf("========================================\n");
    
    return 0;
}
