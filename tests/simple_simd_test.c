/*
 * Simple SIMD Accelerator Test
 * Tests element-wise multiplication with 8 elements (2 batches of 4)
 */

#include <stdint.h>

// Custom instruction macros - write to MMIO registers
#define xcop_srca(addr) \
    asm volatile (".insn r 0x0B, 0x0, 0x01, x0, %0, x0" : : "r"(addr))

#define xcop_srcb(addr) \
    asm volatile (".insn r 0x0B, 0x1, 0x01, x0, %0, x0" : : "r"(addr))

#define xcop_dst(addr) \
    asm volatile (".insn r 0x0B, 0x2, 0x01, x0, %0, x0" : : "r"(addr))

#define xcop_len(length) \
    asm volatile (".insn r 0x0B, 0x3, 0x01, x0, %0, x0" : : "r"(length))

#define xcop_optype(optype) \
    asm volatile (".insn r 0x0B, 0x5, 0x01, x0, %0, x0" : : "r"(optype))

#define xcop_kick() \
    asm volatile (".insn r 0x0B, 0x4, 0x01, x0, x1, x0" : : : "memory")

#define xcop_wait() \
    asm volatile (".insn r 0x0B, 0x6, 0x01, x0, x0, x0" : : : "memory")

int main() {
    // Test arrays: 8 elements each
    uint64_t A[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint64_t B[8] = {2, 3, 4, 5, 6, 7, 8, 9};
    uint64_t C[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    
    // Configure accelerator
    xcop_optype(0);           // Operation type: element-wise multiply
    xcop_srca((uint64_t)A);   // Source A address
    xcop_srcb((uint64_t)B);   // Source B address
    xcop_dst((uint64_t)C);    // Destination address
    xcop_len(8);              // Number of elements
    
    // Start and wait
    xcop_kick();              // Start operation
    xcop_wait();              // Wait for completion
    
    // Check results
    int errors = 0;
    for (int i = 0; i < 8; i++) {
        if (C[i] != A[i] * B[i]) {
            errors++;
        }
    }
    
    return errors;
}
