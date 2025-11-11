#include <stdint.h>

// MMIO base address for SIMD accelerator
#define SIMD_BASE 0x40000000UL

// Register offsets
#define SIMD_SRCA   (SIMD_BASE + 0x00)
#define SIMD_SRCB   (SIMD_BASE + 0x08)
#define SIMD_DST    (SIMD_BASE + 0x10)
#define SIMD_LEN    (SIMD_BASE + 0x18)
#define SIMD_CMD    (SIMD_BASE + 0x20)
#define SIMD_STATUS (SIMD_BASE + 0x28)

// Operation types
#define OP_ELEM_MUL 1

// Helper functions for MMIO access
static inline void simd_write(uint64_t addr, uint64_t val) {
    volatile uint64_t* ptr = (volatile uint64_t*)addr;
    *ptr = val;
}

static inline uint64_t simd_read(uint64_t addr) {
    volatile uint64_t* ptr = (volatile uint64_t*)addr;
    return *ptr;
}

int main() {
    // Test data: 8 element arrays
    float a[8] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
    float b[8] = {2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0};
    float c[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    
    // Configure SIMD accelerator
    simd_write(SIMD_SRCA, (uint64_t)a);
    simd_write(SIMD_SRCB, (uint64_t)b);
    simd_write(SIMD_DST, (uint64_t)c);
    simd_write(SIMD_LEN, 8);
    
    // Start operation
    simd_write(SIMD_CMD, OP_ELEM_MUL);
    
    // Wait for completion
    uint64_t status;
    do {
        status = simd_read(SIMD_STATUS);
    } while (status != 0);  // 0 = idle/done
    
    // Verify results
    int pass = 1;
    for (int i = 0; i < 8; i++) {
        float expected = a[i] * b[i];
        if (c[i] != expected) {
            pass = 0;
        }
    }
    
    return pass ? 0 : 1;
}
