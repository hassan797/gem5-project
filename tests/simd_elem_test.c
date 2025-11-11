/*
 * SIMD Element-wise Multiplication Test
 * Tests 4-way batching with SIMD accelerator
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// SIMD accelerator MMIO registers
#define SIMD_BASE 0x40000000ULL
#define SIMD_SRC_A   (SIMD_BASE + 0x00)
#define SIMD_SRC_B   (SIMD_BASE + 0x08)
#define SIMD_DST     (SIMD_BASE + 0x10)
#define SIMD_LEN     (SIMD_BASE + 0x18)
#define SIMD_CMD     (SIMD_BASE + 0x20)
#define SIMD_STATUS  (SIMD_BASE + 0x28)
#define SIMD_OPTYPE  (SIMD_BASE + 0x30)

// Custom RISC-V instructions for SIMD accelerator
// Encoding: opcode=0x0b (custom-0), FUNCT7=0x01, Rs1=x10 (a0), Rd=x0
#define xcop_srca(addr) \
    __asm__ volatile ("mv a0, %0\n\t" \
                      ".word 0x0205000b\n\t" \
                      : : "r"((uint64_t)(addr)) : "a0")

#define xcop_srcb(addr) \
    __asm__ volatile ("mv a0, %0\n\t" \
                      ".word 0x0205100b\n\t" \
                      : : "r"((uint64_t)(addr)) : "a0")

#define xcop_dst(addr) \
    __asm__ volatile ("mv a0, %0\n\t" \
                      ".word 0x0205200b\n\t" \
                      : : "r"((uint64_t)(addr)) : "a0")

#define xcop_len(length) \
    __asm__ volatile ("mv a0, %0\n\t" \
                      ".word 0x0205300b\n\t" \
                      : : "r"((uint64_t)(length)) : "a0")

#define xcop_kick() \
    __asm__ volatile (".word 0x0205400b\n\t" ::: "memory")

#define xcop_optype(val) \
    __asm__ volatile ("mv a0, %0\n\t" \
                      ".word 0x0205500b\n\t" \
                      : : "r"((uint64_t)(val)) : "a0")

#define xcop_wait() \
    do { \
        uint64_t status; \
        __asm__ volatile (".word 0x0205650b\n\t" \
                          "mv %0, a0\n\t" \
                          : "=r"(status) :: "a0"); \
        while (status & 0x1) { \
            __asm__ volatile (".word 0x0205650b\n\t" \
                              "mv %0, a0\n\t" \
                              : "=r"(status) :: "a0"); \
        } \
    } while(0)

int main() {
    // Test data - 10 elements to test partial batching
    // With numLanes=4, we expect:
    //   Batch 0: elements 0-3 (4 elements)
    //   Batch 1: elements 4-7 (4 elements)  
    //   Batch 2: elements 8-9 (2 elements)
    uint64_t A[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint64_t B[10] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    uint64_t C[10] = {0};
    uint64_t expected[10];
    
    // Calculate expected results
    printf("Expected results:\n");
    for (int i = 0; i < 10; i++) {
        expected[i] = A[i] * B[i];
        printf("  C[%d] = %lu * %lu = %lu\n", i, A[i], B[i], expected[i]);
    }
    
    printf("\n");
    printf("=================================================\n");
    printf("Testing SIMD Element-wise Multiplication\n");
    printf("=================================================\n");
    printf("Test configuration:\n");
    printf("  - Array length: 10 elements\n");
    printf("  - SIMD lanes: 4\n");
    printf("  - Expected batches: 3 (4+4+2 elements)\n");
    printf("  - Operation: element-wise multiply (opType=0)\n");
    printf("\n");
    
    // Configure SIMD accelerator
    printf("Configuring SIMD accelerator...\n");
    xcop_optype(0);           // opType = 0 (element-wise multiply)
    xcop_srca((uint64_t)A);   // Source A address
    xcop_srcb((uint64_t)B);   // Source B address
    xcop_dst((uint64_t)C);    // Destination address
    xcop_len(10);             // Process 10 elements
    
    printf("Starting computation...\n");
    xcop_kick();              // Start the accelerator
    xcop_wait();              // Wait for completion
    
    printf("Computation complete!\n\n");
    
    // Verify results
    printf("Verifying results:\n");
    int errors = 0;
    for (int i = 0; i < 10; i++) {
        if (C[i] == expected[i]) {
            printf("  ✓ C[%d] = %lu (correct)\n", i, C[i]);
        } else {
            printf("  ✗ C[%d] = %lu (expected %lu) ERROR!\n", i, C[i], expected[i]);
            errors++;
        }
    }
    
    printf("\n");
    printf("=================================================\n");
    if (errors == 0) {
        printf("TEST PASSED! All %d elements computed correctly.\n", 10);
        printf("=================================================\n");
        return 0;
    } else {
        printf("TEST FAILED! %d errors found.\n", errors);
        printf("=================================================\n");
        return 1;
    }
}
