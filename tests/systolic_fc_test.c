/*
 * MLPerf-style FC Layer Test for Systolic Array Accelerator
 *
 * Tests fully connected layer operations similar to MLPerf workloads:
 * - Test 1: Small FC (10 outputs, 64 inputs) - warmup
 * - Test 2: MLPerf FC (10 outputs, 8192 inputs) - actual MLPerf tiny
 * - Test 3: Medium FC (64 outputs, 1024 inputs)
 * - Test 4: Large FC (256 outputs, 2048 inputs)
 *
 * FC Layer computation: Y = W × X
 * Where:
 *   W [M×K] = weight matrix (M outputs, K inputs)
 *   X [K×1] = input vector (K input features)
 *   Y [M×1] = output vector (M output values)
 *
 * Uses MMIO interface to systolic array accelerator.
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

// Initialize FC layer weights
void init_fc_weights(uint64_t *W, int M, int K) {
    for (int i = 0; i < M; i++) {
        for (int k = 0; k < K; k++) {
            // Pattern: All weights = 1 for simple verification
            // Real MLPerf would use trained weights
            W[i * K + k] = 1;
        }
    }
}

// Initialize FC layer input activations
void init_fc_input(uint64_t *X, int K) {
    for (int k = 0; k < K; k++) {
        // Pattern: All inputs = 1
        // Real MLPerf would use actual activation values
        X[k] = 1;
    }
}

// Initialize FC layer output to zero
void init_fc_output(uint64_t *Y, int M) {
    memset(Y, 0, M * sizeof(uint64_t));
}

// Compute FC layer on CPU (reference)
void compute_fc_reference(uint64_t *W, uint64_t *X, uint64_t *Y_ref,
                          int M, int K)
{
    for (int i = 0; i < M; i++) {
        uint64_t sum = 0;
        for (int k = 0; k < K; k++) {
            sum += W[i * K + k] * X[k];
        }
        Y_ref[i] = sum;
    }
}

// Verify FC layer result
int verify_fc_result(uint64_t *Y, uint64_t *Y_ref, int M,
                     const char *test_name)
{
    int errors = 0;
    printf("\nVerifying %s:\n", test_name);

    for (int i = 0; i < M && errors < 10; i++) {
        if (Y[i] != Y_ref[i]) {
            printf("  MISMATCH at output[%d]: got %lu, expected %lu\n",
                   i, Y[i], Y_ref[i]);
            errors++;
        }
    }

    if (errors == 0) {
        printf("  ✓ PASS: All %d outputs correct!\n", M);
        return 1;
    } else {
        printf("  ✗ FAIL: %d mismatches found\n", errors);
        return 0;
    }
}

// Run FC layer test
void run_fc_test(int M, int K, const char *test_name)
{
    printf("\n========================================\n");
    printf("%s\n", test_name);
    printf("========================================\n");
    printf("FC Layer: Y[%d×1] = W[%d×%d] × X[%d×1]\n", M, M, K, K);
    printf("  Weights: %d parameters (%.2f KB)\n", M * K,
           (M * K * 8) / 1024.0);
    printf("  Input: %d activations\n", K);
    printf("  Output: %d values\n", M);

    // Allocate matrices
    uint64_t *W = (uint64_t *)malloc(M * K * sizeof(uint64_t));  // Weights
    uint64_t *X = (uint64_t *)malloc(K * sizeof(uint64_t));       // Input
    uint64_t *Y = (uint64_t *)malloc(M * sizeof(uint64_t));       // Output
    uint64_t *Y_ref = (uint64_t *)malloc(M * sizeof(uint64_t));   // Reference

    if (!W || !X || !Y || !Y_ref) {
        printf("ERROR: Memory allocation failed\n");
        return;
    }

    // Initialize FC layer data
    printf("\nInitializing FC layer...\n");
    init_fc_weights(W, M, K);
    init_fc_input(X, K);
    init_fc_output(Y, M);

    printf("  W address: 0x%lx\n", (uint64_t)W);
    printf("  X address: 0x%lx\n", (uint64_t)X);
    printf("  Y address: 0x%lx\n", (uint64_t)Y);

    // Configure systolic accelerator for FC layer
    // FC: Y = W × X is equivalent to GEMM: C[M×1] = A[M×K] × B[K×1]
    printf("\nConfiguring systolic accelerator...\n");
    mmio_write(REG_MAT_A, (uint64_t)W);      // A = W (weights)
    mmio_write(REG_MAT_B, (uint64_t)X);      // B = X (input)
    mmio_write(REG_MAT_C, (uint64_t)Y);      // C = Y (output)
    mmio_write(REG_DIM_M, M);                // M = number of outputs
    mmio_write(REG_DIM_K, K);                // K = number of inputs
    mmio_write(REG_DIM_N, 1);                // N = 1 (vector)

    // Start computation
    printf("Starting FC layer computation...\n");
    mmio_write(REG_CMD, 0x1);  // Set start bit

    // Wait for completion
    printf("Waiting for accelerator...\n");
    int iterations = 0;
    while (mmio_read(REG_STATUS) & 0x1) {
        iterations++;
        for (volatile int i = 0; i < 100; i++);  // Small delay
    }
    printf("Accelerator finished (polled %d times)\n", iterations);

    // Compute reference result on CPU
    printf("Computing reference on CPU...\n");
    compute_fc_reference(W, X, Y_ref, M, K);

    // Verify and print results
    if (verify_fc_result(Y, Y_ref, M, test_name)) {
        printf("\n✓ %s PASSED\n", test_name);
    } else {
        printf("\n✗ %s FAILED\n", test_name);
    }

    // Show sample outputs for small tests
    if (M <= 16) {
        printf("\nSample outputs (first %d):\n", M);
        for (int i = 0; i < M; i++) {
            printf("  Y[%2d] = %lu (expected %lu) %s\n",
                   i, Y[i], Y_ref[i], (Y[i] == Y_ref[i]) ? "✓" : "✗");
        }
    }

    // Cleanup
    free(W);
    free(X);
    free(Y);
    free(Y_ref);
}

int main() {
    printf("======================================================\n");
    printf("MLPerf-style FC Layer Test for Systolic Array\n");
    printf("======================================================\n");
    printf("Testing fully connected layer operations\n");
    printf("MMIO Base: 0x%llx\n", SYSTOLIC_BASE);
    printf("======================================================\n");

    // Test 1: Small FC layer (warmup)
    run_fc_test(10, 64, "Test 1: Small FC (10 outputs, 64 inputs)");

    // Test 2: MLPerf Tiny FC layer (actual MLPerf dimensions)
    run_fc_test(10, 8192, "Test 2: MLPerf Tiny FC (10 outputs, 8192 inputs)");

    // Test 3: Medium FC layer
    run_fc_test(64, 1024, "Test 3: Medium FC (64 outputs, 1024 inputs)");

    // Test 4: Large FC layer
    run_fc_test(256, 2048, "Test 4: Large FC (256 outputs, 2048 inputs)");

    printf("\n======================================================\n");
    printf("All FC layer tests complete!\n");
    printf("======================================================\n");
    printf("Simulation Complete!\n");
    printf("======================================================\n");

    return 0;
}
