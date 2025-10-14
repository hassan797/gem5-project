// tests/coproc_test.c
#include <stdint.h>

static inline void xcop_srca(uint64_t a){
    asm volatile(".insn r 0x0B,0x0,0x01, x0,%0,x0" :: "r"(a) : "memory");
}
static inline void xcop_srcb(uint64_t b){
    asm volatile(".insn r 0x0B,0x1,0x01, x0,%0,x0" :: "r"(b) : "memory");
}
static inline void xcop_dst (uint64_t d){
    asm volatile(".insn r 0x0B,0x2,0x01, x0,%0,x0" :: "r"(d) : "memory");
}
static inline void xcop_len (uint64_t n){
    asm volatile(".insn r 0x0B,0x3,0x01, x0,%0,x0" :: "r"(n) : "memory");
}
static inline void xcop_kick(void){
    // order all prior MMIO writes before starting the device
    asm volatile("fence iorw, iorw\n\t"
                 ".insn r 0x0B,0x4,0x01, x0,x0,x0\n\t"
                 : : : "memory");
}

#define N 8
static uint64_t A[N], B[N], C[N];

int main(void)
{
    for (int i = 0; i < N; i++) { A[i] = i + 1; B[i] = 2 * i; C[i] = 0; }

    // Program MMIO registers via custom ops (addresses are virtual in SE mode)
    xcop_srca((uint64_t)(uintptr_t)A);
    xcop_srcb((uint64_t)(uintptr_t)B);
    xcop_dst ((uint64_t)(uintptr_t)C);
    xcop_len (N);
    xcop_kick();

    // Busy-wait on STATUS (BASE+0x28)
    volatile uint64_t *STATUS = (uint64_t*)(uintptr_t)(0x40000000ull + 0x28);
    while (*STATUS & 1) { /* spin */ }

    // Verify results
    int ok = 1;
    for (int i = 0; i < N; i++) {
        uint64_t exp = A[i] * B[i];
        if (C[i] != exp) {
            ok = 0;
            // Write failure indicator (0xDEADBEEF) to first element
            C[0] = 0xDEADBEEFDEADBEEFULL;
            break;
        }
    }
    // Write success/failure marker
    if (ok) {
        // Success: write magic value 0xC0FFEE to last element
        C[N-1] = 0xC0FFEEC0FFEEULL;
    }
    return ok ? 0 : 1;
}


// Minimal exit and entry point for freestanding build (-nostdlib)
static inline void do_exit(int code){
    register long a0 asm("a0") = code;
    register long a7 asm("a7") = 93; // SYS_exit for RISC-V Linux ABI; gem5 SE catches this
    asm volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
    while (1) { }
}

void _start(void){
    int r = main();
    do_exit(r);
}
